#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "Buzzer.h"
#include "Key.h"
#include "OLED.h"
#include "wifi.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define UI_PAGE_COUNT           15U
#define UI_HOME_PAGE            1U
#define UI_SCAN_PERIOD_MS       10U
#define UI_REFRESH_PERIOD_MS    1000U
#define UI_KEY_BEEP_MS          20U
#define UI_EVENT_QUEUE_LENGTH   16U
#define UI_TASK_STACK_SIZE      4096U
#define UI_TASK_PRIORITY        5U
#define KEY_TASK_STACK_SIZE     2048U
#define KEY_TASK_PRIORITY       6U

typedef struct {
	const char *title;
	const char *line2;
	const char *line3;
} ui_page_t;

static const char *TAG = "ui";
static QueueHandle_t s_key_event_queue;
static uint8_t s_current_page;
static bool s_detail_mode;

static const ui_page_t s_pages[UI_PAGE_COUNT] = {
	{ "启动界面", "ESP32-S3 OLED", "系统启动中" },
	{ "主界面", "照度 -- 温度 --", "" },
	{ "窗帘控制", "电机: 未接入", "控制器: 未配置" },
	{ "湿度调节", "当前 -- 目标 60%", "湿度调节: 关闭" },
	{ "环境数据", "光照 -- lx", "湿度 --% 温度 --" },
	{ "自动模式", "光照联动: 关闭", "湿度调节: 关闭" },
	{ "系统设置", "WiFi: 123", "时间: 未配置" },
	{ "水位/水箱", "水位: --", "传感器未连接" },
	{ "设备状态", "窗帘: 未接入", "雾化器: 未接入" },
	{ "按键说明", "K1/K2: PAGE", "K3: OK K4: BACK" },
	{ "WiFi连接", "SSID: 123", "" },
	{ "NTP时间同步", "NTP: 未配置", "SNTP: 未启动" },
	{ "低水位报警", "水位: 未知", "传感器未连接" },
	{ "雾化器运行", "设备: 未接入", "湿度 --%" },
	{ "语音交互", "VOICE: NOT READY", "" },
};

static void ui_render_watch_screen(bool full_refresh)
{
	static const char *weekdays[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
	static const char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
									"JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
	char date_buf[17];
	char time_buf[16];
	char status_buf[32];
	char footer[32];
	time_t now = time(NULL);
	struct tm local_time;
	bool time_ready = now >= 1704067200 && localtime_r(&now, &local_time) != NULL;

	if (time_ready) {
		snprintf(date_buf, sizeof(date_buf), "%s %02u %s",
				 weekdays[local_time.tm_wday], (unsigned int)local_time.tm_mday,
				 months[local_time.tm_mon]);
		snprintf(time_buf, sizeof(time_buf), "%02u:%02u:%02u",
				 (unsigned int)local_time.tm_hour, (unsigned int)local_time.tm_min,
				 (unsigned int)local_time.tm_sec);
	} else {
		snprintf(date_buf, sizeof(date_buf), "TIME SYNCING...");
		snprintf(time_buf, sizeof(time_buf), "--:--:--");
	}
	snprintf(status_buf, sizeof(status_buf), "WIFI %s  %02u/%02u",
			 wifi_is_connected() ? "ON" : "OFF",
			 (unsigned int)(s_current_page + 1U),
			 (unsigned int)UI_PAGE_COUNT);
	snprintf(footer, sizeof(footer), "K1< K2> K3:OPEN");

	if (full_refresh) {
		OLED_ShowStringPad(1, 1, date_buf);
		OLED_ShowStringPad(4, 1, footer);
	}
	OLED_ShowStringPad(2, 1, time_buf);
	OLED_ShowStringPad(3, 1, status_buf);
}

static void ui_render(void)
{
	char header[32];
	char line2[48];
	char line3[48];
	char footer[20];
	const ui_page_t *page = &s_pages[s_current_page];

	if (s_current_page == UI_HOME_PAGE && !s_detail_mode) {
		ui_render_watch_screen(true);
		return;
	}

	snprintf(header, sizeof(header), "%s %02u/%02u",
			 s_detail_mode ? "DETAIL" : "SMART HOME",
			 (unsigned int)(s_current_page + 1U),
			 (unsigned int)UI_PAGE_COUNT);
	snprintf(line2, sizeof(line2), "%s", page->line2);
	snprintf(line3, sizeof(line3), "%s", page->line3);
	if (s_current_page == 1U || s_current_page == 8U) {
		snprintf(line3, sizeof(line3), "WiFi:%s",
				 wifi_is_connected() ? "ON" : "OFF");
	} else if (s_current_page == 10U) {
		snprintf(line3, sizeof(line3), "WiFi: %s",
				 wifi_is_connected() ? "CONNECTED" : "CONNECTING");
	}
	if (s_detail_mode) {
		snprintf(line2, sizeof(line2), "%s", page->line3);
		snprintf(line3, sizeof(line3), "PAGE %02u / %02u",
				 (unsigned int)(s_current_page + 1U),
				 (unsigned int)UI_PAGE_COUNT);
	}
	snprintf(footer, sizeof(footer), "K1< K2> K3OK K4<");

	OLED_ShowStringPad(1, 1, header);
	OLED_ShowStringPad(2, 1, line2);
	OLED_ShowStringPad(3, 1, line3);
	OLED_ShowStringPad(4, 1, footer);
}

static void ui_handle_key(Key_Id key)
{
	ESP_LOGD(TAG, "UI received K%u", (unsigned)key + 1U);

	switch (key) {
	case KEY_1:
		s_current_page = (s_current_page + UI_PAGE_COUNT - 1U) % UI_PAGE_COUNT;
		s_detail_mode = false;
		ESP_LOGD(TAG, "Previous card %u", (unsigned int)s_current_page + 1U);
		break;
	case KEY_2:
		s_current_page = (s_current_page + 1U) % UI_PAGE_COUNT;
		s_detail_mode = false;
		ESP_LOGD(TAG, "Next card %u", (unsigned int)s_current_page + 1U);
		break;
	case KEY_3:
		s_detail_mode = true;
		ESP_LOGD(TAG, "Confirmed page %u", (unsigned int)s_current_page + 1U);
		break;
	case KEY_4:
		s_current_page = UI_HOME_PAGE;
		s_detail_mode = false;
		ESP_LOGD(TAG, "Returned to watch face");
		break;
	default:
		break;
	}
	ui_render();
	Buzzer_Beep(UI_KEY_BEEP_MS);
}

static void ui_task(void *arg)
{
	(void)arg;
	TickType_t boot_tick = xTaskGetTickCount();
	TickType_t last_refresh_tick = boot_tick;
	ui_render();

	for (;;) {
		Key_Id key;
		if (xQueueReceive(s_key_event_queue, &key,
						  pdMS_TO_TICKS(UI_SCAN_PERIOD_MS)) == pdTRUE) {
			ui_handle_key(key);
			while (xQueueReceive(s_key_event_queue, &key, 0) == pdTRUE) {
				ui_handle_key(key);
			}
		}

		TickType_t now = xTaskGetTickCount();
		if (s_current_page == 0U &&
			now - boot_tick >= pdMS_TO_TICKS(2500U)) {
			s_current_page = UI_HOME_PAGE;
			s_detail_mode = false;
			ui_render();
			last_refresh_tick = now;
		}

		if (now - last_refresh_tick >= pdMS_TO_TICKS(UI_REFRESH_PERIOD_MS)) {
			if (s_current_page == UI_HOME_PAGE && !s_detail_mode) {
				ui_render_watch_screen(false);
			}
			last_refresh_tick = now;
		}
	}
}

static void key_scan_task(void *arg)
{
	(void)arg;
	for (;;) {
		Key_Scan();
		uint32_t events = Key_GetEvent();
		for (uint8_t i = 0; i < KEY_COUNT; i++) {
			if ((events & (1U << i)) != 0U) {
				Key_Id key = (Key_Id)i;
				if (xQueueSend(s_key_event_queue, &key, 0) != pdPASS) {
					ESP_LOGW(TAG, "Key event queue full; K%u dropped", i + 1U);
				}
			}
		}
		vTaskDelay(pdMS_TO_TICKS(UI_SCAN_PERIOD_MS));
	}
}

esp_err_t UI_Init(void)
{
	if (s_key_event_queue != NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	esp_err_t err = Key_Init();
	if (err != ESP_OK) {
		return err;
	}

	s_key_event_queue = xQueueCreate(UI_EVENT_QUEUE_LENGTH, sizeof(Key_Id));
	if (s_key_event_queue == NULL) {
		return ESP_ERR_NO_MEM;
	}

	TaskHandle_t key_task_handle = NULL;
	if (xTaskCreate(key_scan_task, "key_scan", KEY_TASK_STACK_SIZE, NULL,
					KEY_TASK_PRIORITY, &key_task_handle) != pdPASS) {
		vQueueDelete(s_key_event_queue);
		s_key_event_queue = NULL;
		return ESP_ERR_NO_MEM;
	}

	if (xTaskCreate(ui_task, "ui", UI_TASK_STACK_SIZE, NULL,
					UI_TASK_PRIORITY, NULL) != pdPASS) {
		vTaskDelete(key_task_handle);
		vQueueDelete(s_key_event_queue);
		s_key_event_queue = NULL;
		return ESP_ERR_NO_MEM;
	}

	ESP_LOGW(TAG, "UI ready: K1 previous, K2 next, K3 confirm, K4 back");
	return ESP_OK;
}