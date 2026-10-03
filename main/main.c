#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "BLE.h"
#include "Buzzer.h"
#include "Key.h"
#include "OLED.h"
#include "OLED_Icon.h"
#include "wifi.h"

static const char *TAG = "main";

/* BLE 广播使用的设备名（手机扫描时可看到） */
#define BLE_DEVICE_NAME			"ESP32S3-BLE"

/* UI 刷新和按键扫描参数 */
#define UI_PAGE_COUNT			15
#define UI_TASK_PERIOD_MS		10
#define UI_REFRESH_PERIOD_MS	1000
#define UI_TASK_STACK_SIZE		4096
#define UI_TASK_PRIORITY		5
#define DEBUG_TASK_PERIOD_MS	UI_TASK_PERIOD_MS
#define DEBUG_TASK_PRIORITY		UI_TASK_PRIORITY

/* 最近一次收到的 BLE 数据最多在 OLED 上显示 16 个字符（一行宽度） */
#define DEBUG_RX_TEXT_MAX		16

/* 调试信息（由 BLE 回调更新，由调试任务读取并显示） */
static volatile uint32_t s_ble_rx_count;					/* 累计收到的数据包数 */
static char				 s_ble_rx_text[DEBUG_RX_TEXT_MAX + 1];	/* 最近收到的数据 */
static volatile bool	 s_ble_rx_updated;					/* 是否有新数据待显示 */

/* BLE 是否初始化成功：失败信息由调试任务统一显示，避免被状态刷新覆盖 */
static bool s_ble_ready;
static portMUX_TYPE s_ble_data_mux = portMUX_INITIALIZER_UNLOCKED;

/* 第 1 行右侧的状态图标位置（每个图标占 2 个字符位：WiFi 在左，蓝牙在右） */
#define WIFI_ICON_COLUMN		13
#define BLE_ICON_COLUMN			15

/* 函数定义在文件末尾，这里先声明（前置声明） */
static void ble_rx_handler(const uint8_t *data, uint16_t length);

/**
 * @brief 调试任务：周期刷新 OLED，显示 BLE 连接状态与接收到的数据
 *
 * 只在状态发生变化时刷新对应行，减少软件 I2C 的写入量
 * （软件 I2C 逐位翻转，整屏刷新约需几百毫秒）。
 */
typedef struct {
	const char *title;
	const char *line2;
	const char *line3;
} UiPage;

static const UiPage s_pages[UI_PAGE_COUNT] = {
	{ "启动界面", "ESP32-S3 OLED", "系统启动中" },
	{ "主界面", "照度 -- 温度 --", "" },
	{ "窗帘控制", "电机: 未接入", "控制器: 未配置" },
	{ "湿度调节", "当前 -- 目标 60%", "湿度调节: 关闭" },
	{ "环境数据", "光照 -- lx", "湿度 --% 温度 --" },
	{ "自动模式", "光照联动: 关闭", "湿度调节: 关闭" },
	{ "系统设置", "WiFi: 123", "时间: 未配置" },
	{ "水位/水箱", "水位: --", "传感器未连接" },
	{ "设备状态", "窗帘: 未接入", "雾化器: 未接入" },
	{ "按键说明", "K1/K3: 上一页", "K2/K4: 下一页" },
	{ "WiFi连接", "SSID: 123", "" },
	{ "NTP时间同步", "NTP: 未配置", "SNTP: 未启动" },
	{ "低水位报警", "水位: 未知", "传感器未连接" },
	{ "雾化器运行", "设备: 未接入", "湿度 --%" },
	{ "语音交互", "BLE: WAIT", "" },
};

static uint8_t s_current_page;

static void ui_render_page(void)
{
	char header[32];
	char line2[48];
	char line3[48];
	char footer[17];
	char rx_text[DEBUG_RX_TEXT_MAX + 1];
	uint32_t rx_count;
	const UiPage *page = &s_pages[s_current_page];

	portENTER_CRITICAL(&s_ble_data_mux);
	rx_count = s_ble_rx_count;
	memcpy(rx_text, s_ble_rx_text, sizeof(rx_text));
	portEXIT_CRITICAL(&s_ble_data_mux);

	snprintf(header, sizeof(header), "%02u %s", s_current_page + 1, page->title);
	snprintf(line2, sizeof(line2), "%s", page->line2);
	snprintf(line3, sizeof(line3), "%s", page->line3);

	if (s_current_page == 1) {
		snprintf(line3, sizeof(line3), "WiFi:%s BLE:%s",
				 wifi_is_connected() ? "ON" : "OFF",
				 s_ble_ready && BLE_IsConnected() ? "ON" : "OFF");
	} else if (s_current_page == 8) {
		snprintf(line3, sizeof(line3), "WiFi:%s BLE:%s",
				 wifi_is_connected() ? "ON" : "OFF",
				 s_ble_ready && BLE_IsConnected() ? "ON" : "OFF");
	} else if (s_current_page == 10) {
		snprintf(line3, sizeof(line3), "WiFi: %s",
				 wifi_is_connected() ? "CONNECTED" : "CONNECTING");
	} else if (s_current_page == 14) {
		snprintf(line2, sizeof(line2), "BLE: %s",
				 s_ble_ready && BLE_IsConnected() ? "CONNECTED" : "WAITING");
		snprintf(line3, sizeof(line3), "RX:%u %.10s", (unsigned)rx_count, rx_text);
	}

	snprintf(footer, sizeof(footer), "K1/3<%02u/15>K2/4", s_current_page + 1);
	OLED_ShowStringPad(1, 1, header);
	OLED_ShowStringPad(2, 1, line2);
	OLED_ShowStringPad(3, 1, line3);
	OLED_ShowStringPad(4, 1, footer);
	ESP_LOGI(TAG, "OLED page %u: %s", s_current_page + 1, page->title);
}

static void ui_task(void *arg)
{
	(void)arg;
	bool key_ready = (Key_Init() == ESP_OK);
	if (!key_ready) {
		ESP_LOGE(TAG, "Key initialization failed; page buttons are unavailable");
	}

	TickType_t boot_tick = xTaskGetTickCount();
	TickType_t last_refresh_tick = 0;
	bool page_dirty = true;
	ui_render_page();

	for (;;) {
		if (key_ready) {
			Key_Scan();
			uint32_t events = Key_GetEvent();
			uint32_t previous_mask = (1U << KEY_1) | (1U << KEY_3);
			uint32_t next_mask = (1U << KEY_2) | (1U << KEY_4);

			if (events & previous_mask) {
				s_current_page = (s_current_page + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT;
				page_dirty = true;
			} else if (events & next_mask) {
				s_current_page = (s_current_page + 1) % UI_PAGE_COUNT;
				page_dirty = true;
			}
		}

		TickType_t now = xTaskGetTickCount();
		if (s_current_page == 0 && now - boot_tick >= pdMS_TO_TICKS(2500)) {
			s_current_page = 1;
			page_dirty = true;
		}

		if (page_dirty || now - last_refresh_tick >= pdMS_TO_TICKS(UI_REFRESH_PERIOD_MS)) {
			ui_render_page();
			last_refresh_tick = now;
			page_dirty = false;
		}
		vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
	}
}
void app_main(void)
{
	OLED_Init();
	ESP_LOGI(TAG, "OLED initialized");

	Buzzer_Init();

	/*
	 * 配置 BLE：初始化并开始广播，等待手机连接。
	 * 这里只记录初始化结果，第 2 行的状态显示统一由调试任务负责，
	 * 避免两处重复刷新（否则初始化失败的提示会被状态刷新覆盖）。
	 */
	BLE_SetRxCallback(ble_rx_handler);
	esp_err_t err = BLE_Init(BLE_DEVICE_NAME);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(err));
		s_ble_ready = false;
	} else {
		ESP_LOGI(TAG, "BLE ready, device name: %s", BLE_DEVICE_NAME);
		s_ble_ready = true;
	}

	/* 创建页面任务：扫描按键并绘制当前 UI 页面 */
	BaseType_t created = xTaskCreate(ui_task, "ui", UI_TASK_STACK_SIZE,
									 NULL, UI_TASK_PRIORITY, NULL);
	if (created != pdPASS) {
		ESP_LOGE(TAG, "Failed to create UI task");
	}

	err = wifi_connect("123", "12345678");
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Wi-Fi connection request failed: %s", esp_err_to_name(err));
	}
}

/* ------------------------------------------------------------------ */
/* 以下为回调函数实现（由 BLE 模块在收到手机数据时调用）                */
/* ------------------------------------------------------------------ */

/**
 * @brief BLE 接收到手机下发数据时的回调
 *
 * 这里只做“应用层”的事情：打印日志、记录统计数据供调试任务显示。
 * 具体的业务处理（如根据指令控制 LED/蜂鸣器等）也应写在这一层，
 * 因为“收到数据后该做什么”是应用逻辑，不属于 BLE 通信模块的职责。
 *
 * 注意：该回调运行在 BLE 主机任务中，不能长时间阻塞（不要用 vTaskDelay）。
 */
static void ble_rx_handler(const uint8_t *data, uint16_t length)
{
	ESP_LOGI(TAG, "BLE received %u byte(s): %.*s", length, length, (const char *)data);

	uint16_t copy_length = (length > DEBUG_RX_TEXT_MAX) ? DEBUG_RX_TEXT_MAX : length;
	portENTER_CRITICAL(&s_ble_data_mux);
	s_ble_rx_count++;
	memcpy(s_ble_rx_text, data, copy_length);
	s_ble_rx_text[copy_length] = '\0';
	s_ble_rx_updated = true;
	portEXIT_CRITICAL(&s_ble_data_mux);
}
