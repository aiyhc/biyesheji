#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/*
 * 蜂鸣器驱动（有源蜂鸣器 / 低电平触发）
 *
 * 硬件说明：
 *   - 控制引脚：GPIO9
 *   - 低电平发声：输出 0 响，输出 1 停
 *   - 若使用三极管/MOS 驱动，注意基极/栅极电平逻辑与本驱动保持一致
 */

/**
 * @brief 初始化蜂鸣器控制引脚
 *
 * 将 GPIO9 配置为推挽输出，并默认输出高电平（蜂鸣器不响）。
 * 可重复调用，内部有幂等保护。
 *
 * @return ESP_OK           初始化成功
 *         ESP_ERR_INVALID_STATE / 其他   GPIO 配置失败
 */
esp_err_t Buzzer_Init(void);

/**
 * @brief 打开蜂鸣器（输出低电平，发声）
 */
void Buzzer_On(void);

/**
 * @brief 关闭蜂鸣器（输出高电平，停止发声）
 */
void Buzzer_Off(void);

/**
 * @brief 设置蜂鸣器开关状态
 *
 * @param on true = 响，false = 停
 */
void Buzzer_Set(bool on);

/**
 * @brief 阻塞式鸣叫指定时长（内部会调用 vTaskDelay）
 *
 * 注意：该函数会阻塞当前任务，仅适合在非实时任务中调用。
 *
 * @param duration_ms 鸣叫时长，单位毫秒
 */
void Buzzer_Beep(uint32_t duration_ms);

/**
 * @brief 阻塞式鸣叫若干次（蜂鸣 - 间隔 循环）
 *
 * @param times        鸣叫次数
 * @param on_ms        每次鸣叫时长，单位毫秒
 * @param off_ms       每次之间的间隔时长，单位毫秒
 */
void Buzzer_BeepTimes(uint32_t times, uint32_t on_ms, uint32_t off_ms);

#endif
