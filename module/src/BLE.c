/*
 * @file        BLE.c
 * @brief       ESP32-S3 BLE 从机（外设）通信模块，基于 NimBLE 协议栈
 *
 * 功能说明：
 *   1. 初始化 NimBLE 协议栈，注册一个自定义 GATT 服务（UUID 与 Nordic UART
 *      Service 兼容，方便手机 App 直接使用）
 *   2. 服务包含两个特征：
 *        - RX（Write / Write Without Response）：手机 -> ESP32
 *        - TX（Notify）                        ：ESP32 -> 手机
 *   3. 手机连接后可通过 RX 下发数据，模块通过注册的回调上报给上层应用
 *   4. 断开连接后自动重新开始广播，等待下一次连接
 */

#include "BLE.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* 日志标签，所有本模块的日志输出均带此前缀 */
static const char *TAG = "BLE";

/* 默认设备名（BLE_Init() 传入 NULL 时使用） */
#define BLE_DEFAULT_DEVICE_NAME  "ESP32S3-BLE"

/* 接收缓冲区大小（一次写入超过此长度的数据会被截断） */
#define BLE_RX_BUFFER_SIZE       256

/* 广播启动失败后的重试周期（毫秒），避免因偶发错误导致设备永久搜不到 */
#define BLE_ADV_RETRY_MS         2000

/* 128 位 UUID 定义：6E400001/0002/0003-B5A3-F393-E0A9-E50E24DCCA9E
 * （即 Nordic UART Service，按小端顺序书写） */
static const ble_uuid128_t s_service_uuid =
	BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
					 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid =
	BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
					 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid =
	BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
					 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* 模块运行状态 */
static bool     s_initialized;                              /* 是否已完成初始化 */
static uint8_t  s_own_addr_type;                            /* 本机地址类型 */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;    /* 当前连接句柄 */
static uint16_t s_tx_val_handle;                            /* TX 特征值句柄（Notify 用） */
static bool     s_notify_enabled;                           /* 手机是否已打开 Notify */
static BLE_RxCallback_t s_rx_callback;                      /* 上层注册的接收回调 */
static TimerHandle_t s_adv_retry_timer;                     /* 广播失败重试定时器 */

static void ble_advertise(void);

/* 广播启动失败时，延时后重试，保证设备最终一定能被搜到 */
static void ble_adv_retry_cb(TimerHandle_t timer)
{
	(void)timer;
	if (!BLE_IsConnected()) {
		ble_advertise();
	}
}

/* ------------------------------------------------------------------ */
/* GATT 回调                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief GATT 数据访问回调：处理手机对特征的读写请求
 *
 * 目前仅处理 RX 特征的写操作（手机下发数据），其余操作返回不支持。
 *
 * @return 0 表示处理成功，非 0 为 ATT 错误码
 */
static int ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
							  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	(void)conn_handle;
	(void)arg;

	switch (ctxt->op) {
	case BLE_GATT_ACCESS_OP_WRITE_CHR: {
		/* attr_handle 为手机打开 Notify 时也会触发，这里只关心 RX 的写入 */
		if (attr_handle == s_tx_val_handle) {
			break;
		}

		uint8_t buffer[BLE_RX_BUFFER_SIZE];
		uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
		if (length > sizeof(buffer)) {
			length = sizeof(buffer);
		}

		int rc = os_mbuf_copydata(ctxt->om, 0, length, buffer);
		if (rc != 0) {
			ESP_LOGE(TAG, "Failed to copy RX data: %d", rc);
			return BLE_ATT_ERR_UNLIKELY;
		}

		ESP_LOGI(TAG, "Received %u byte(s)", length);

		/* 上报给上层应用 */
		if (s_rx_callback != NULL) {
			s_rx_callback(buffer, length);
		}
		break;
	}

	default:
		/* 未实现读操作等其它访问方式 */
		return BLE_ATT_ERR_UNLIKELY;
	}

	return 0;
}

/* GATT 服务定义：一个主服务，包含 RX（写）与 TX（通知）两个特征 */
static const struct ble_gatt_svc_def s_gatt_services[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &s_service_uuid.u,
		.characteristics = (struct ble_gatt_chr_def[]) {
			{
				/* RX：手机 -> ESP32 */
				.uuid = &s_rx_uuid.u,
				.access_cb = ble_gatt_access_cb,
				.flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
			},
			{
				/* TX：ESP32 -> 手机（Notify） */
				.uuid = &s_tx_uuid.u,
				.access_cb = ble_gatt_access_cb,
				.flags = BLE_GATT_CHR_F_NOTIFY,
				.val_handle = &s_tx_val_handle,
			},
			{
				0, /* 特征数组结束标志 */
			}
		},
	},
	{
		0, /* 服务数组结束标志 */
	}
};

/* ------------------------------------------------------------------ */
/* GAP 回调与广播                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief GAP 事件回调：处理连接、断开、订阅通知等事件
 */
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
	(void)arg;

	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		if (event->connect.status == 0) {
			s_conn_handle = event->connect.conn_handle;
			ESP_LOGI(TAG, "Device connected, conn_handle=%d", s_conn_handle);
		} else {
			/* 连接失败：重新开始广播 */
			ESP_LOGW(TAG, "Connection failed (status=%d), restarting advertising",
					 event->connect.status);
			ble_advertise();
		}
		break;

	case BLE_GAP_EVENT_DISCONNECT:
		ESP_LOGI(TAG, "Device disconnected (reason=%d)", event->disconnect.reason);
		s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
		s_notify_enabled = false;
		/* 断开后重新广播，等待下一次连接 */
		ble_advertise();
		break;

	case BLE_GAP_EVENT_ADV_COMPLETE:
		ESP_LOGI(TAG, "Advertising completed, restarting");
		ble_advertise();
		break;

	case BLE_GAP_EVENT_SUBSCRIBE:
		/* 手机打开/关闭 TX 通知时触发 */
		s_notify_enabled = (event->subscribe.cur_notify != 0);
		ESP_LOGI(TAG, "Notify %s", s_notify_enabled ? "enabled" : "disabled");
		break;

	case BLE_GAP_EVENT_MTU:
		ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
		break;

	case BLE_GAP_EVENT_CONN_UPDATE:
		ESP_LOGI(TAG, "Connection parameters updated");
		break;

	default:
		break;
	}

	return 0;
}

/**
 * @brief 配置广播数据并开始广播
 *
 * 广播包中放设备名，扫描响应包中放 128 位服务 UUID
 * （广播包长度上限 31 字节，两者分开存放可避免超长）。
 */
static void ble_advertise(void)
{
	struct ble_gap_adv_params adv_params;
	struct ble_hs_adv_fields fields;
	struct ble_hs_adv_fields rsp_fields;
	int rc;

	/* 已连接时不需要广播 */
	if (BLE_IsConnected()) {
		return;
	}

	/* 广播数据：标志位 + 完整设备名 */
	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

	const char *name = ble_svc_gap_device_name();
	fields.name = (uint8_t *)name;
	fields.name_len = strlen(name);
	fields.name_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
		goto retry;
	}

	/* 扫描响应数据：完整服务 UUID（手机扫描时可据此识别本服务） */
	memset(&rsp_fields, 0, sizeof(rsp_fields));
	rsp_fields.uuids128 = &s_service_uuid;
	rsp_fields.num_uuids128 = 1;
	rsp_fields.uuids128_is_complete = 1;

	rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to set scan response fields: %d", rc);
		goto retry;
	}

	/* 可连接、可被通用发现，一直广播直到连接建立 */
	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
	adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);   /* 100ms 广播间隔 */
	adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

	rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
						   &adv_params, ble_gap_event_cb, NULL);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
		goto retry;
	}

	/* 打印本机蓝牙地址，方便手机端核对 */
	ble_addr_t addr;
	if (ble_hs_id_copy_addr(s_own_addr_type, addr.val, NULL) == 0) {
		ESP_LOGI(TAG, "Advertising started, name: \"%s\", "
				 "address: %02x:%02x:%02x:%02x:%02x:%02x",
				 name, addr.val[5], addr.val[4], addr.val[3],
				 addr.val[2], addr.val[1], addr.val[0]);
	} else {
		ESP_LOGI(TAG, "Advertising started, name: \"%s\"", name);
	}
	ESP_LOGI(TAG, "Waiting for a phone to connect (use a BLE scanner App)");
	return;

retry:
	/* 启动失败：延时后自动重试，避免设备一直搜不到 */
	if (s_adv_retry_timer != NULL) {
		ESP_LOGW(TAG, "Retrying advertising in %d ms", BLE_ADV_RETRY_MS);
		xTimerStart(s_adv_retry_timer, 0);
	}
}

/* ------------------------------------------------------------------ */
/* NimBLE 主机任务相关                                                */
/* ------------------------------------------------------------------ */

/* 协议栈复位回调：清空连接状态 */
static void ble_on_reset(int reason)
{
	ESP_LOGW(TAG, "Protocol stack reset, reason=%d", reason);
	s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
	s_notify_enabled = false;
}

/* 协议栈就绪回调：获取本机地址并开始广播 */
static void ble_on_sync(void)
{
	int rc = ble_hs_util_ensure_addr(0);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
		goto retry;
	}

	/* 使用芯片公共地址 / 随机地址，由协议栈自动推断 */
	rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
		goto retry;
	}

	ESP_LOGI(TAG, "Protocol stack synced (addr type=%d)", s_own_addr_type);
	ble_advertise();
	return;

retry:
	/* 地址获取失败：延时重试，避免设备一直搜不到 */
	if (s_adv_retry_timer != NULL) {
		xTimerStart(s_adv_retry_timer, 0);
	}
}

/* NimBLE 主机任务：运行协议栈事件循环 */
static void ble_host_task(void *param)
{
	(void)param;

	ESP_LOGI(TAG, "BLE host task started");
	nimble_port_run();          /* 阻塞直到 nimble_port_stop() 被调用 */
	nimble_port_freertos_deinit();
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                           */
/* ------------------------------------------------------------------ */

esp_err_t BLE_Init(const char *device_name)
{
	if (s_initialized) {
		return ESP_OK;
	}

	/* 1. 初始化 NVS：BLE 协议栈存放配对信息等需要 NVS */
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		/* NVS 分区被占满或版本不匹配：擦除后重新初始化 */
		ESP_LOGW(TAG, "NVS needs to be erased, erasing...");
		err = nvs_flash_erase();
		if (err == ESP_OK) {
			err = nvs_flash_init();
		}
	}
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 2. 初始化 NimBLE 协议栈（含控制器） */
	err = nimble_port_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 3. 配置协议栈回调 */
	ble_hs_cfg.reset_cb = ble_on_reset;
	ble_hs_cfg.sync_cb = ble_on_sync;
	/* 无需配对即可读写特征（如需加密连接，可在此配置 sm_* 相关参数） */
	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

	/* 4. 初始化 GAP / GATT 服务 */
	ble_svc_gap_init();
	ble_svc_gatt_init();

	/*
	 * 5. 设置设备名
	 *
	 * 注意：必须放在 ble_svc_gap_init() 之后！
	 * 当 CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=y 时，ble_svc_gap_init() 内部会调用
	 * ble_svc_gap_init_name()，用 Kconfig 中的默认名（"nimble"）覆盖名字，
	 * 若先设置会被冲掉，导致广播名一直是 "nimble"。
	 */
	const char *name = (device_name != NULL && device_name[0] != '\0')
					   ? device_name : BLE_DEFAULT_DEVICE_NAME;
	int rc = ble_svc_gap_device_name_set(name);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to set device name: %d", rc);
		return ESP_FAIL;
	}

	/* 6. 注册自定义 GATT 服务 */
	rc = ble_gatts_count_cfg(s_gatt_services);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
		return ESP_FAIL;
	}
	rc = ble_gatts_add_svcs(s_gatt_services);
	if (rc != 0) {
		ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
		return ESP_FAIL;
	}

	/* 7. 创建广播重试定时器（广播失败时自动重试） */
	s_adv_retry_timer = xTimerCreate("ble_adv_retry",
									 pdMS_TO_TICKS(BLE_ADV_RETRY_MS),
									 pdFALSE, NULL, ble_adv_retry_cb);
	if (s_adv_retry_timer == NULL) {
		ESP_LOGE(TAG, "Failed to create advertising retry timer");
		return ESP_ERR_NO_MEM;
	}

	/* 8. 启动 NimBLE 主机任务（内部会调用 sync_cb 开始广播） */
	nimble_port_freertos_init(ble_host_task);

	s_initialized = true;
	ESP_LOGI(TAG, "BLE initialized (NimBLE), device name: %s",
			 (device_name != NULL && device_name[0] != '\0') ? device_name : BLE_DEFAULT_DEVICE_NAME);
	return ESP_OK;
}

bool BLE_IsConnected(void)
{
	return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool BLE_IsNotifyEnabled(void)
{
	return s_notify_enabled;
}

esp_err_t BLE_Send(const uint8_t *data, uint16_t length)
{
	if (data == NULL || length == 0) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!BLE_IsConnected() || !s_notify_enabled) {
		return ESP_ERR_INVALID_STATE;
	}

	/* 把数据拷贝到 mbuf 中，通过 Notify 发送 */
	struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
	if (om == NULL) {
		ESP_LOGE(TAG, "Failed to allocate mbuf");
		return ESP_ERR_NO_MEM;
	}

	int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
	if (rc != 0) {
		ESP_LOGW(TAG, "Notify failed: %d", rc);
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t BLE_SendString(const char *string)
{
	if (string == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	return BLE_Send((const uint8_t *)string, (uint16_t)strlen(string));
}

void BLE_SetRxCallback(BLE_RxCallback_t callback)
{
	s_rx_callback = callback;
}
