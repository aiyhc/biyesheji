#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief 使用指定凭据连接 Wi-Fi 接入点 (AP)
 *
 * 首次调用时自动启动 station 模式并完成 Wi-Fi 子系统初始化。
 * 连接过程为异步执行，断开后每隔 3 秒自动重连。
 * - 开放网络可传入空字符串作为密码
 * - 凭据后续可由 BLE 配置功能 (BLE provisioning) 提供
 *
 * @param ssid     目标 AP 的 SSID，不能为空指针且不能为空字符串
 * @param password 目标 AP 的密码，开放网络传空串；WPA/WPA2 至少 8 个字符
 * @return ESP_OK                 连接请求已提交
 *         ESP_ERR_INVALID_ARG    参数非法（SSID/密码为空、长度超限或 WPA 密码 < 8）
 *         其他                   底层驱动初始化/配置失败
 */
esp_err_t wifi_connect(const char *ssid, const char *password);

/**
 * @brief 查询当前是否已连接到 Wi-Fi 接入点并获取到 IP 地址
 *
 * 连接成功（收到 IP_EVENT_STA_GOT_IP）后返回 true，
 * 发生断开（WIFI_EVENT_STA_DISCONNECTED）后返回 false。
 *
 * @return true 已连接；false 未连接
 */
bool wifi_is_connected(void);