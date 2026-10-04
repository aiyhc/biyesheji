#include "esp_err.h"
#include "esp_log.h"

#include "Buzzer.h"
#include "OLED.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "main";

void app_main(void)
{
	OLED_Init();

	esp_err_t err = Buzzer_Init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Buzzer init failed: %s", esp_err_to_name(err));
	}

	err = UI_Init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(err));
	}

	err = wifi_connect("123", "12345678");
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Wi-Fi connection request failed: %s", esp_err_to_name(err));
	}
}