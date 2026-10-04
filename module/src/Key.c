/*
 * @file        Key.c
 * @brief       按键驱动（4 路，内部上拉 / 低电平有效，带软件消抖）
 *
 * 引脚分配：
 *   KEY_1 -> GPIO14
 *   KEY_2 -> GPIO38
 *   KEY_3 -> GPIO47
 *   KEY_4 -> GPIO48
 *
 * 电平约定：按下为低电平 (0)，松开为高电平 (1)。
 */

#include "Key.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 各按键对应的 GPIO，顺序必须与 Key_Id 枚举一致 */
static const gpio_num_t s_key_gpio[KEY_COUNT] = {
	[KEY_1] = GPIO_NUM_14,
	[KEY_2] = GPIO_NUM_38,
	[KEY_3] = GPIO_NUM_47,
	[KEY_4] = GPIO_NUM_48,
};

/* 按下时的电平 */
#define KEY_LEVEL_DOWN  0

/* 消抖需要连续采样的次数：Key_Scan 每 10ms 调用一次，2 次约 20ms */
#define KEY_DEBOUNCE_CNT  2

static const char *TAG = "Key";

/* 是否已完成初始化 */
static bool s_key_initialized;

/* 每个按键消抖后的稳定状态：true = 按住 */
static bool s_key_down[KEY_COUNT];

/* 消抖计数器：状态变化的连续采样计数 */
static uint8_t s_key_cnt[KEY_COUNT];
static int s_last_gpio_level[KEY_COUNT] = {-1, -1, -1, -1};

/* "按下"事件位掩码：bit0=KEY_1 ... bit3=KEY_4 */
static uint32_t s_key_event;

esp_err_t Key_Init(void)
{
	if (s_key_initialized) {
		return ESP_OK;
	}

	/* 一次性配置 4 个按键引脚：输入 + 内部上拉 */
	uint64_t pin_mask = 0;
	for (int i = 0; i < KEY_COUNT; i++) {
		pin_mask |= (1ULL << s_key_gpio[i]);
	}

	gpio_config_t io_config = {
		.pin_bit_mask = pin_mask,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,      /* 另一端接 GND，启用内部上拉 */
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,        /* 采用轮询方式，无需中断 */
	};

	esp_err_t err = gpio_config(&io_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
		return err;
	}

	/* 初始化为松开状态 */
	for (int i = 0; i < KEY_COUNT; i++) {
		s_key_down[i] = false;
		s_key_cnt[i] = 0;
		s_last_gpio_level[i] = gpio_get_level(s_key_gpio[i]);
		ESP_LOGW(TAG, "K%d input GPIO%d initial level=%d",
				 i + 1, s_key_gpio[i], s_last_gpio_level[i]);
	}
	s_key_event = 0;

	s_key_initialized = true;
	ESP_LOGW(TAG, "Keys ready: active-low GPIO14/38/47/48");
	return ESP_OK;
}

void Key_Scan(void)
{
	for (int i = 0; i < KEY_COUNT; i++) {
		int level = gpio_get_level(s_key_gpio[i]);
		if (level != s_last_gpio_level[i]) {
			s_last_gpio_level[i] = level;
			ESP_LOGD(TAG, "GPIO%d level changed to %d (K%d)",
					 s_key_gpio[i], level, i + 1);
		}
		bool raw_down = (level == KEY_LEVEL_DOWN);

		if (raw_down == s_key_down[i]) {
			/* 电平与当前稳定状态一致，清空变化计数 */
			s_key_cnt[i] = 0;
			continue;
		}

		/* 电平与稳定状态不同，累计连续采样次数 */
		if (++s_key_cnt[i] >= KEY_DEBOUNCE_CNT) {
			s_key_down[i] = raw_down;     /* 确认状态翻转 */
			s_key_cnt[i] = 0;

			/* 仅在"松开 -> 按下"的瞬间产生一次事件 */
			if (raw_down) {
				s_key_event |= (1U << i);
				ESP_LOGD(TAG, "K%d pressed on GPIO%d", i + 1,
						 s_key_gpio[i]);
			}
		}
	}
}

bool Key_IsDown(Key_Id id)
{
	return s_key_down[id];
}

uint32_t Key_GetEvent(void)
{
	uint32_t events = s_key_event;
	s_key_event = 0;      /* 读取后清空，事件只上报一次 */
	return events;
}

Key_Id Key_WaitPress(void)
{
	for (;;) {
		Key_Scan();
		uint32_t events = Key_GetEvent();
		for (int i = 0; i < KEY_COUNT; i++) {
			if (events & (1U << i)) {
				return (Key_Id)i;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
