/*
 * @file        wifi.c
 * @brief       ESP32 Wi-Fi STA 模式连接管理模块
 *
 * 本模块封装了 ESP-IDF 的 Wi-Fi 连接流程，简化上层调用：
 *   1. 负责 NVS、网络接口、默认事件循环等基础子系统的一次性初始化
 *   2. 注册 Wi-Fi 与 IP 相关事件回调，实现自动重连与日志输出
 *   3. 提供 wifi_connect() 入口，支持运行期重新配置 SSID/密码
 */

#include "wifi.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

/* 日志标签，所有本模块的日志输出均带此前缀 */
static const char *TAG = "wifi";

/* 断开连接后等待多久再重连，可按需修改 */
#define WIFI_RECONNECT_INTERVAL_SECONDS 3U

/* 标记 Wi-Fi 子系统是否已完成一次性的初始化（幂等保护） */
static bool s_wifi_initialized;

/* 标记 esp_wifi_start() 是否已被调用，防止重复启动 */
static bool s_wifi_started;
static bool s_sntp_started;
/* 标记当前是否已连接到 AP 并获取到 IP（供上层查询连接状态） */
static bool s_wifi_connected;
static TimerHandle_t s_reconnect_timer;

static void wifi_reconnect_timer_callback(TimerHandle_t timer)
{
	(void)timer;
	esp_err_t err = esp_wifi_connect();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Reconnect request failed: %s", esp_err_to_name(err));
		if (xTimerReset(s_reconnect_timer, 0) != pdPASS) {
			ESP_LOGE(TAG, "Failed to schedule reconnect");
		}
	}
}

/**
 * @brief Wi-Fi 与 IP 相关事件的统一处理函数
 *
 * 处理两类事件：
	 *  - WIFI_EVENT_STA_DISCONNECTED：发生断连，等待配置的时间后重连
 *  - IP_EVENT_STA_GOT_IP：成功获取到 IP 地址，打印日志
 *
 * @param arg         注册时传入的用户参数（本模块未使用）
 * @param event_base  事件基（这里是 WIFI_EVENT 或 IP_EVENT）
 * @param event_id    具体事件 ID
 * @param event_data  事件携带的数据（IP 事件为 ip_event_got_ip_t）
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
							   int32_t event_id, void *event_data)
{
	(void)arg;  // 未使用的形参，避免编译器警告

	/* 处理断连事件：自动重连，保证链路可用性 */
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		s_wifi_connected = false;
		ESP_LOGW(TAG, "Disconnected; retrying in %u seconds",
				 WIFI_RECONNECT_INTERVAL_SECONDS);
		if (xTimerReset(s_reconnect_timer, 0) != pdPASS) {
			ESP_LOGE(TAG, "Failed to schedule reconnect");
		}
	}
	/* 处理获取 IP 事件：连接成功，打印分配的 IP 地址 */
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
		xTimerStop(s_reconnect_timer, 0);
		s_wifi_connected = true;
		setenv("TZ", "CST-8", 1);
		tzset();
		if (!s_sntp_started) {
			esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
			esp_sntp_setservername(0, "pool.ntp.org");
			esp_sntp_init();
			s_sntp_started = true;
			ESP_LOGI(TAG, "SNTP time synchronization started");
		}
		ESP_LOGI(TAG, "Connected; IP address: " IPSTR, IP2STR(&event->ip_info.ip));
	}
}

/**
 * @brief Wi-Fi 子系统一次性初始化
 *
 * 依次完成：NVS -> 网络接口 -> 默认事件循环 -> 默认 STA 网络接口
 *         -> Wi-Fi 驱动初始化 -> 注册事件回调。
 * 多次调用安全（已初始化则直接返回 ESP_OK）。
 *
 * @return ESP_OK                    初始化成功
 *         ESP_ERR_INVALID_STATE     相关组件已被其它模块初始化（视为可接受）
 *         其他                      真实错误码
 */
static esp_err_t wifi_initialize(void)
{
	/* 幂等保护：仅执行一次 */
	if (s_wifi_initialized) {
		return ESP_OK;
	}

	/* 1. 初始化 NVS：Wi-Fi 驱动依赖其存储校准数据等 */
	esp_err_t err = nvs_flash_init();
	if (err != ESP_OK) {
		return err;
	}

	/* 2. 初始化底层 TCP/IP 协议栈：允许多次调用 */
	err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		return err;
	}

	/* 3. 创建默认事件循环：供 Wi-Fi / IP 事件分发使用 */
	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		return err;
	}

	/* 4. 创建默认 STA 网络接口（用于 IP 事件等） */
	if (esp_netif_create_default_wifi_sta() == NULL) {
		return ESP_FAIL;
	}

	/* 使用单次定时器，按配置间隔安排重连 */
	s_reconnect_timer = xTimerCreate("wifi_retry",
									 pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_SECONDS * 1000U),
									 pdFALSE, NULL, wifi_reconnect_timer_callback);
	if (s_reconnect_timer == NULL) {
		return ESP_ERR_NO_MEM;
	}

	/* 5. 按默认配置初始化 Wi-Fi 驱动 */
	wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&init_config);
	if (err != ESP_OK) {
		return err;
	}

	/* 6. 注册全部 Wi-Fi 事件回调（断连重连等） */
	err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									 wifi_event_handler, NULL);
	if (err != ESP_OK) {
		return err;
	}

	/* 7. 注册获取 IP 事件回调（用于日志提示） */
	err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
									 wifi_event_handler, NULL);
	if (err != ESP_OK) {
		return err;
	}

	s_wifi_initialized = true;
	return ESP_OK;
}

/**
 * @brief 连接到指定 Wi-Fi 接入点
 *
 * 执行流程：
 *   1. 参数校验（SSID/密码非空、长度合法、WPA 密码 >= 8）
 *   2. 触发一次性初始化
 *   3. 设置 station 模式并写入新配置
 *   4. 若首次启动则启动 Wi-Fi；否则先断开旧连接（让事件回调触发重连）
 *   5. 主动发起连接请求
 *
 * @param ssid     目标 AP 的 SSID
 * @param password 目标 AP 的密码
 * @return 见头文件 wifi_connect() 说明
 */
esp_err_t wifi_connect(const char *ssid, const char *password)
{
	/* 参数校验：SSID 和密码指针不可为空，且 SSID 不能是空串 */
	if (ssid == NULL || password == NULL || ssid[0] == '\0') {
		return ESP_ERR_INVALID_ARG;
	}

	/* 进一步校验长度限制与 WPA 最短密码要求 */
	size_t ssid_length = strlen(ssid);
	size_t password_length = strlen(password);
	if (ssid_length > sizeof(((wifi_config_t *)0)->sta.ssid) ||
		password_length > sizeof(((wifi_config_t *)0)->sta.password) ||
		(password_length > 0 && password_length < 8)) {
		return ESP_ERR_INVALID_ARG;
	}

	/* 确保子系统已初始化（首次调用会执行真正的初始化逻辑） */
	esp_err_t err = wifi_initialize();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 构造 wifi_config_t 并填入新凭据 */
	wifi_config_t wifi_config = {0};
	memcpy(wifi_config.sta.ssid, ssid, ssid_length);
	memcpy(wifi_config.sta.password, password, password_length);

	/* 切换为 station 模式（一般已是 STA，但显式设置以防被其它模块修改） */
	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		return err;
	}

	/* 应用新的 SSID/密码配置 */
	err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	if (err != ESP_OK) {
		return err;
	}

	/* 首次调用时启动 Wi-Fi；后续调用则通过断开让事件回调自动重连 */
	if (!s_wifi_started) {
		err = esp_wifi_start();
		if (err != ESP_OK) {
			return err;
		}
		s_wifi_started = true;
	} else {
		/*
		 * 已经启动过：先主动断开。
		 * - 断开成功：交给事件回调触发 esp_wifi_connect() 重连
		 * - 当前未连接（NOT_CONNECT）：直接返回，由下面主动发起连接
		 * - 其他错误：原样返回错误码
		 */
		err = esp_wifi_disconnect();
		if (err == ESP_OK) {
			return ESP_OK;
		}
		if (err != ESP_ERR_WIFI_NOT_CONNECT) {
			return err;
		}
	}

	/* 主动发起连接请求（首次启动，或上次已处于 NOT_CONNECT 时） */
	err = esp_wifi_connect();
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "Connection requested for SSID: %s", ssid);
	}
	return err;
}

bool wifi_is_connected(void)
{
	return s_wifi_connected;
}