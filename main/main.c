#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "BLE.h"
#include "Buzzer.h"
#include "OLED.h"
#include "OLED_Icon.h"
#include "wifi.h"

static const char *TAG = "main";

/* BLE 广播使用的设备名（手机扫描时可看到） */
#define BLE_DEVICE_NAME			"ESP32S3-BLE"

/* 调试任务参数 */
#define DEBUG_TASK_PERIOD_MS	1000	/* 刷新周期（ms） */
#define DEBUG_TASK_STACK_SIZE	4096	/* 任务栈大小（字节） */
#define DEBUG_TASK_PRIORITY		5		/* 任务优先级 */

/* 最近一次收到的 BLE 数据最多在 OLED 上显示 16 个字符（一行宽度） */
#define DEBUG_RX_TEXT_MAX		16

/* 调试信息（由 BLE 回调更新，由调试任务读取并显示） */
static volatile uint32_t s_ble_rx_count;					/* 累计收到的数据包数 */
static char				 s_ble_rx_text[DEBUG_RX_TEXT_MAX + 1];	/* 最近收到的数据 */
static volatile bool	 s_ble_rx_updated;					/* 是否有新数据待显示 */

/* BLE 是否初始化成功：失败信息由调试任务统一显示，避免被状态刷新覆盖 */
static bool s_ble_ready;

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
static void debug_task(void *arg)
{
	(void)arg;

	char	 buffer[32];
	bool	 last_connected = !BLE_IsConnected();	/* 取反以强制首次刷新 */
	bool	 last_notify	= !BLE_IsNotifyEnabled();
	bool	 last_wifi		= wifi_is_connected();
	uint32_t last_rx_count	= UINT32_MAX;			/* 强制首次刷新 */

	for (;;) {
		/* 第 1 行右侧：WiFi 图标（连上路由器时显示，未连接时清除） */
		bool wifi_up = wifi_is_connected();
		if (wifi_up != last_wifi) {
			last_wifi = wifi_up;
			OLED_ShowIcon(1, WIFI_ICON_COLUMN, wifi_up ? OLED_IconWifi : NULL);
			ESP_LOGI(TAG, "Wi-Fi %s", wifi_up ? "connected" : "disconnected");
		}

		/* 第 2 行：BLE 初始化结果 / 连接状态；第 1 行右侧：蓝牙图标 */
		bool connected = BLE_IsConnected();
		if (connected != last_connected || !s_ble_ready) {
			last_connected = connected;

			if (!s_ble_ready) {
				OLED_ShowStringPad(2, 1, "蓝牙初始化失败");
			} else {
				OLED_ShowStringPad(2, 1, connected ? "蓝牙已连接" : "等待连接");
			}
			/* 连接时显示蓝牙图标，未连接时清除 */
			OLED_ShowIcon(1, BLE_ICON_COLUMN, connected ? OLED_IconBluetooth : NULL);
			ESP_LOGI(TAG, "BLE %s", connected ? "connected" : "disconnected");
		}

		/* 第 3 行：Notify 状态 + 接收计数 */
		bool	 notify	  = BLE_IsNotifyEnabled();
		uint32_t rx_count = s_ble_rx_count;
		if (notify != last_notify || rx_count != last_rx_count) {
			last_notify	  = notify;
			last_rx_count = rx_count;
			snprintf(buffer, sizeof(buffer), "NTF:%-3s RX:%u",
					 notify ? "ON" : "OFF", (unsigned)rx_count);
			OLED_ShowStringPad(3, 1, buffer);
		}

		/* 第 4 行：最近一次收到的数据 */
		if (s_ble_rx_updated) {
			s_ble_rx_updated = false;
			snprintf(buffer, sizeof(buffer), ">%s", s_ble_rx_text);
			OLED_ShowStringPad(4, 1, buffer);
		}

		vTaskDelay(pdMS_TO_TICKS(DEBUG_TASK_PERIOD_MS));
	}
}

void app_main(void)
{
	OLED_Init();
	OLED_ShowStringPad(1, 1, "蓝牙调试");		/* 第 1 行标题，只在启动时写一次 */
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

	/* 创建调试任务：持续刷新 OLED 上的运行状态 */
	BaseType_t created = xTaskCreate(debug_task, "debug", DEBUG_TASK_STACK_SIZE,
									 NULL, DEBUG_TASK_PRIORITY, NULL);
	if (created != pdPASS) {
		ESP_LOGE(TAG, "Failed to create debug task");
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

	s_ble_rx_count++;

	uint16_t copy_length = (length > DEBUG_RX_TEXT_MAX) ? DEBUG_RX_TEXT_MAX : length;
	memcpy(s_ble_rx_text, data, copy_length);
	s_ble_rx_text[copy_length] = '\0';
	s_ble_rx_updated = true;
}
