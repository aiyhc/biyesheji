#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Buzzer.h"
#include "Key.h"
#include "OLED.h"
#include "wifi.h"

static const char *TAG = "main";


/* UI 刷新和按键扫描参数 */
#define UI_PAGE_COUNT			15
#define UI_TASK_PERIOD_MS		10
#define UI_REFRESH_PERIOD_MS	1000
#define UI_TASK_STACK_SIZE		4096
#define UI_TASK_PRIORITY		5



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
	{ "语音交互", "VOICE: NOT READY", "" },
};

static uint8_t s_current_page;

static void ui_render_page(void)
{
	char header[32];
	char line2[48];
	char line3[48];
	char footer[17];
	const UiPage *page = &s_pages[s_current_page];

	snprintf(header, sizeof(header), "%02u %s", s_current_page + 1, page->title);
	snprintf(line2, sizeof(line2), "%s", page->line2);
	snprintf(line3, sizeof(line3), "%s", page->line3);

	if (s_current_page == 1) {
		snprintf(line3, sizeof(line3), "WiFi:%s",
				 wifi_is_connected() ? "ON" : "OFF");
	} else if (s_current_page == 8) {
		snprintf(line3, sizeof(line3), "WiFi:%s",
				 wifi_is_connected() ? "ON" : "OFF");
	} else if (s_current_page == 10) {
		snprintf(line3, sizeof(line3), "WiFi: %s",
				 wifi_is_connected() ? "CONNECTED" : "CONNECTING");
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

	/* 创建页面任务：扫描按键并绘制当前 UI 页面 */
	BaseType_t created = xTaskCreate(ui_task, "ui", UI_TASK_STACK_SIZE,
									 NULL, UI_TASK_PRIORITY, NULL);
	if (created != pdPASS) {
		ESP_LOGE(TAG, "Failed to create UI task");
	}

	esp_err_t err = wifi_connect("123", "12345678");
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Wi-Fi connection request failed: %s", esp_err_to_name(err));
	}
}

