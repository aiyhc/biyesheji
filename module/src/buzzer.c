/*
 * @file        Buzzer.c
 * @brief       蜂鸣器驱动（有源蜂鸣器 / 低电平触发，控制引脚 GPIO9）
 *
 * 电平约定：
 *   - 输出低电平 (0) -> 蜂鸣器发声
 *   - 输出高电平 (1) -> 蜂鸣器停止
 * 因此上电初始化后默认为高电平，避免上电即鸣。
 */

#include "Buzzer.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 蜂鸣器控制引脚 */
#define BUZZER_GPIO          GPIO_NUM_9
/* 低电平触发：ON 为低，OFF 为高 */
#define BUZZER_LEVEL_ON      0
#define BUZZER_LEVEL_OFF     1

static const char *TAG = "Buzzer";

/* 标记是否已完成初始化（幂等保护） */
static bool s_buzzer_initialized;

esp_err_t Buzzer_Init(void)
{
	if (s_buzzer_initialized) {
		return ESP_OK;
	}

	gpio_config_t io_config = {
		.pin_bit_mask = 1ULL << BUZZER_GPIO,   /* 选中 GPIO9 */
		.mode = GPIO_MODE_OUTPUT,              /* 推挽输出 */
		.pull_up_en = GPIO_PULLUP_DISABLE,     /* 无需内部上下拉 */
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,        /* 输出引脚不需要中断 */
	};

	esp_err_t err = gpio_config(&io_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 初始为高电平，蜂鸣器静音 */
	gpio_set_level(BUZZER_GPIO, BUZZER_LEVEL_OFF);

	s_buzzer_initialized = true;
	ESP_LOGI(TAG, "Buzzer initialized on GPIO%d (active low)", BUZZER_GPIO);
	return ESP_OK;
}

void Buzzer_On(void)
{
	gpio_set_level(BUZZER_GPIO, BUZZER_LEVEL_ON);
}

void Buzzer_Off(void)
{
	gpio_set_level(BUZZER_GPIO, BUZZER_LEVEL_OFF);
}

void Buzzer_Set(bool on)
{
	gpio_set_level(BUZZER_GPIO, on ? BUZZER_LEVEL_ON : BUZZER_LEVEL_OFF);
}

void Buzzer_Beep(uint32_t duration_ms)
{
	Buzzer_On();
	vTaskDelay(pdMS_TO_TICKS(duration_ms));
	Buzzer_Off();
}

void Buzzer_BeepTimes(uint32_t times, uint32_t on_ms, uint32_t off_ms)
{
	for (uint32_t i = 0; i < times; i++) {
		Buzzer_On();
		vTaskDelay(pdMS_TO_TICKS(on_ms));
		Buzzer_Off();
		/* 最后一次鸣叫后不再插入间隔 */
		if (i + 1 < times) {
			vTaskDelay(pdMS_TO_TICKS(off_ms));
		}
	}
}
