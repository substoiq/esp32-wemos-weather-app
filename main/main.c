/*
 * main.c — the starting point of the program
 *
 * In ESP-IDF everything starts in app_main() (comparable to main.py on a
 * MicroPython board). Here we switch the parts on and start a separate
 * FreeRTOS task that fetches the weather every few minutes. Meanwhile LVGL
 * runs in its own task (see display_port.c). So two tasks run at the same
 * time, which is why there is a "lock".
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "credentials.h"
#include "display_port.h"
#include "wifi.h"
#include "weather.h"
#include "ui.h"

static const char *TAG = "main";

/* Show a status in the header bar; safe to call from this task */
static void show_status(ui_status_t status, const char *text)
{
    if (display_lock(-1)) {
        ui_set_status(status, text);
        display_unlock();
    }
}

/* The weather task: an endless loop, like 'while True:' in MicroPython */
static void weather_task(void *arg)
{
    /* 'static' inside a function: the variable keeps existing, and it lives
     * in normal memory instead of on the small stack of the task */
    static weather_t weather;
    char error[64];

    for (;;) {
        if (!wifi_wait_connected(15000)) {
            show_status(UI_STATUS_ERROR, "No Wi-Fi: check credentials.h");
            continue;                        // wait again
        }

        show_status(UI_STATUS_LOADING, "Fetching weather...");
        if (weather_fetch(&weather, error, sizeof(error))) {
            if (display_lock(-1)) {
                ui_update(&weather);
                display_unlock();
            }
        } else {
            ESP_LOGE(TAG, "Fetch failed: %s", error);
            show_status(UI_STATUS_ERROR, error);
        }

        vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MIN * 60 * 1000));
    }
}

void app_main(void)
{
    /* NVS = small piece of flash where the Wi-Fi driver keeps its settings */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    display_init();

    if (display_lock(-1)) {
        ui_create();
        display_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(200));          // let the first picture be drawn...
    display_backlight(true);                 // ...and only then switch the light on

    wifi_start();

    /* Start the task: function, name, stack size (bytes), parameter, priority, handle.
     * HTTPS needs quite a lot of stack, hence 10 kB. */
    xTaskCreate(weather_task, "weather", 10 * 1024, NULL, 3, NULL);
}
