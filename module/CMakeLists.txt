idf_component_register(SRCS "src/wifi.c" "src/OLED.c" "src/OLED_Font.c" "src/OLED_ChineseFont.c" "src/OLED_Icon.c" "src/Buzzer.c" "src/Led.c" "src/Key.c" "src/BLE.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event esp_netif esp_wifi esp_driver_gpio esp_driver_i2c nvs_flash bt)
