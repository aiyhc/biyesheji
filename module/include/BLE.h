#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief 收到手机/上位机写入数据的回调
 *
 * 注意：该回调运行在 NimBLE 主机任务上下文中，请勿在其中长时间阻塞
 *       （不要调用 vTaskDelay 或耗时阻塞操作）。
 *
 * @param data   收到的数据（不保证以 '\0' 结尾）
 * @param length 数据字节数
 */
typedef void (*BLE_RxCallback_t)(const uint8_t *data, uint16_t length);

/**
 * @brief 初始化 BLE 并开始广播（NimBLE 协议栈）
 *
 * 服务采用与 Nordic UART Service (NUS) 兼容的 128 位 UUID，
 * 手机端可使用 nRF Connect、蓝牙调试助手等 App 直接连接收发数据：
 *   - RX 特征（Write）：手机 -> ESP32，数据通过 BLE_SetRxCallback() 回调上报
 *   - TX 特征（Notify）：ESP32 -> 手机，由 BLE_Send() / BLE_SendString() 发送
 *
 * 函数内部会完成 NimBLE 协议栈初始化、GATT 服务注册、GAP 设备名设置，
 * 并创建主机任务与开始广播，调用后立即返回（不与手机连接同步等待）。
 * 多次调用安全（已初始化则直接返回 ESP_OK）。
 *
 * @param device_name 蓝牙设备名（广播名称），长度为 1~29；传 NULL 用默认名
 * @return ESP_OK              初始化成功并已开始广播
 *         其他               底层初始化失败的错误码
 */
esp_err_t BLE_Init(const char *device_name);

/**
 * @brief 查询当前是否有手机连接
 * @return true 已连接；false 未连接
 */
bool BLE_IsConnected(void);

/**
 * @brief 查询手机是否已订阅 TX 通知（打开 Notify）
 * @return true 已打开；false 未打开（此时 BLE_Send 会被忽略）
 */
bool BLE_IsNotifyEnabled(void);

/**
 * @brief 向手机发送数据（Notify 方式）
 *
 * 单包长度请勿超过协商后的 MTU（默认约 20~244 字节），长数据请自行分包。
 *
 * @param data   待发送数据
 * @param length 数据字节数
 * @return ESP_OK               已提交发送
 *         ESP_ERR_INVALID_ARG  data 为 NULL 或 length 为 0
 *         ESP_ERR_INVALID_STATE 未连接或对方未打开 Notify
 *         其他                 NimBLE 返回的错误
 */
esp_err_t BLE_Send(const uint8_t *data, uint16_t length);

/**
 * @brief 向手机发送字符串（Notify 方式，自动计算长度）
 * @param string 以 '\0' 结尾的字符串
 * @return 同 BLE_Send()
 */
esp_err_t BLE_SendString(const char *string);

/**
 * @brief 注册接收回调
 *
 * 应在本模块处理完数据前调用（例如 BLE_Init() 之后立即调用）。
 * 传 NULL 可取消回调。
 *
 * @param callback 回调函数指针
 */
void BLE_SetRxCallback(BLE_RxCallback_t callback);
