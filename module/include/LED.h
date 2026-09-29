#ifndef __LED_H
#define __LED_H

#include <stdbool.h>

#include "esp_err.h"

/*
 * 状态 LED 驱动（4 路，高电平点亮）
 *
 * 硬件说明（LED 阳极接 GPIO，阴极经限流电阻 220Ω~1kΩ 接 GND）：
 *   - 红色 LED -> GPIO10
 *   - 绿色 LED -> GPIO11
 *   - 蓝色 LED -> GPIO12
 *   - 白色 LED -> GPIO13
 *
 * 电平约定：
 *   - 输出高电平 (1) -> 点亮
 *   - 输出低电平 (0) -> 熄灭
 */

/* LED 编号 */
typedef enum {
	LED_RED = 0,     /* GPIO10 */
	LED_GREEN,       /* GPIO11 */
	LED_BLUE,        /* GPIO12 */
	LED_WHITE,       /* GPIO13 */
	LED_COUNT,       /* LED 总数（4） */
} Led_Id;

/**
 * @brief 初始化全部 LED 引脚
 *
 * 将 4 个 GPIO 配置为推挽输出，并默认全部熄灭。可重复调用。
 *
 * @return ESP_OK         初始化成功
 *         其他          GPIO 配置失败的错误码
 */
esp_err_t Led_Init(void);

/**
 * @brief 点亮指定 LED
 * @param id LED 编号（Led_Id）
 */
void Led_On(Led_Id id);

/**
 * @brief 熄灭指定 LED
 * @param id LED 编号（Led_Id）
 */
void Led_Off(Led_Id id);

/**
 * @brief 翻转指定 LED 状态
 * @param id LED 编号（Led_Id）
 */
void Led_Toggle(Led_Id id);

/**
 * @brief 设置指定 LED 的开关状态
 * @param id LED 编号（Led_Id）
 * @param on true = 点亮，false = 熄灭
 */
void Led_Set(Led_Id id, bool on);

/**
 * @brief 一次设置全部 4 路 LED 的状态
 *
 * @param red   红色 LED 状态
 * @param green 绿色 LED 状态
 * @param blue  蓝色 LED 状态
 * @param white 白色 LED 状态
 */
void Led_SetAll(bool red, bool green, bool blue, bool white);

/**
 * @brief 熄灭全部 LED
 */
void Led_AllOff(void);

#endif
