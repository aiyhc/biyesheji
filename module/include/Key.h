#ifndef __KEY_H
#define __KEY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * 按键驱动（4 路，内部上拉 / 低电平有效）
 *
 * 硬件说明（按键一端接 GPIO，另一端接 GND）：
 *   - KEY_1 -> GPIO14
 *   - KEY_2 -> GPIO38
 *   - KEY_3 -> GPIO47
 *   - KEY_4 -> GPIO48
 *
 * 电平约定：
 *   - 按下时引脚被拉低 (0)
 *   - 松开时内部上拉拉高 (1)
 *
 * 使用方式（推荐）：
 *   Key_Init();                          // 初始化一次
 *   在周期任务中每 10ms 调用 Key_Scan();
 *   通过 Key_GetEvent() 获取"按下"事件，通过 Key_IsDown() 查询当前是否按住。
 */

/* 按键编号 */
typedef enum {
	KEY_1 = 0,       /* GPIO14 */
	KEY_2,           /* GPIO38 */
	KEY_3,           /* GPIO47 */
	KEY_4,           /* GPIO48 */
	KEY_COUNT,       /* 按键总数（4） */
} Key_Id;

/**
 * @brief 初始化全部按键引脚
 *
 * 配置为输入模式并启用内部上拉，默认松开（高电平）。可重复调用。
 *
 * @return ESP_OK         初始化成功
 *         其他          GPIO 配置失败的错误码
 */
esp_err_t Key_Init(void);

/**
 * @brief 周期扫描按键（需每 10ms 左右调用一次）
 *
 * 完成软件消抖与状态更新，并在检测到"按下"瞬间记录事件，
 * 供 Key_GetEvent() 查询。
 */
void Key_Scan(void);

/**
 * @brief 查询指定按键当前是否被按住（已消抖状态）
 *
 * @param id 按键编号
 * @return true = 按住，false = 松开
 */
bool Key_IsDown(Key_Id id);

/**
 * @brief 获取并清除"按下"事件（边沿触发，只返回一次）
 *
 * 每一位对应一个按键：bit0=KEY_1 ... bit3=KEY_4。
 * 读取后事件标志会被清空，避免重复响应。
 *
 * @return 本次新按下按键的位掩码
 */
uint32_t Key_GetEvent(void);

/**
 * @brief 阻塞式读取：等待按键按下并返回其编号
 *
 * 注意：该函数内部会循环调用 Key_Scan 并延时，会阻塞当前任务。
 *
 * @return 被按下的按键编号
 */
Key_Id Key_WaitPress(void);

#endif
