/*
 * @file        Led.c
 * @brief       状态 LED 驱动（4 路，高电平点亮）
 *
 * 引脚分配：
 *   LED_RED   -> GPIO10
 *   LED_GREEN -> GPIO11
 *   LED_BLUE  -> GPIO12
 *   LED_WHITE -> GPIO13
 *
 * 电平约定：高电平点亮，低电平熄灭。
 */

#include "Led.h"

#include "driver/gpio.h"
#include "esp_log.h"

/* 各 LED 对应的 GPIO，顺序必须与 Led_Id 枚举一致 */
static const gpio_num_t s_led_gpio[LED_COUNT] = {
	[LED_RED] = GPIO_NUM_10,
	[LED_GREEN] = GPIO_NUM_11,
	[LED_BLUE] = GPIO_NUM_12,
	[LED_WHITE] = GPIO_NUM_13,
};

#define LED_LEVEL_ON   1
#define LED_LEVEL_OFF  0

static const char *TAG = "Led";

/* 标记是否已完成初始化（幂等保护） */
static bool s_led_initialized;

esp_err_t Led_Init(void)
{
	if (s_led_initialized) {
		return ESP_OK;
	}

	/* 一次性配置 4 个 LED 引脚：推挽输出，无上下拉 */
	uint64_t pin_mask = 0;
	for (int i = 0; i < LED_COUNT; i++) {
		pin_mask |= (1ULL << s_led_gpio[i]);
	}

	gpio_config_t io_config = {
		.pin_bit_mask = pin_mask,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};

	esp_err_t err = gpio_config(&io_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 默认全部熄灭 */
	Led_AllOff();

	s_led_initialized = true;
	ESP_LOGI(TAG, "LEDs initialized on GPIO10/11/12/13 (active high)");
	return ESP_OK;
}

void Led_On(Led_Id id)
{
	gpio_set_level(s_led_gpio[id], LED_LEVEL_ON);
}

void Led_Off(Led_Id id)
{
	gpio_set_level(s_led_gpio[id], LED_LEVEL_OFF);
}

void Led_Toggle(Led_Id id)
{
	gpio_set_level(s_led_gpio[id], !gpio_get_level(s_led_gpio[id]));
}

void Led_Set(Led_Id id, bool on)
{
	gpio_set_level(s_led_gpio[id], on ? LED_LEVEL_ON : LED_LEVEL_OFF);
}

void Led_SetAll(bool red, bool green, bool blue, bool white)
{
	Led_Set(LED_RED, red);
	Led_Set(LED_GREEN, green);
	Led_Set(LED_BLUE, blue);
	Led_Set(LED_WHITE, white);
}

void Led_AllOff(void)
{
	Led_SetAll(false, false, false, false);
}
