/*
 * display_port.c — connects LVGL to the display
 *
 * What happens here:
 *   - SPI bus:  set up the pins and the bus to the display
 *   - HX8357:   start the display controller (see hx8357.c)
 *   - LVGL:     a draw buffer, a "flush" function and a task that runs LVGL
 *
 * Memory: a full screen is 480 x 320 x 2 bytes = 307 kB. This board has no
 * PSRAM, so that does not fit. LVGL therefore draws the screen in strips of
 * 32 lines (30 kB) and sends them one after another ("partial" mode).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"

#include "board_config.h"
#include "display_port.h"
#include "hx8357.h"

static const char *TAG = "display";

#define DRAW_BUF_LINES      32
#define DRAW_BUF_SIZE       (LCD_H_RES * DRAW_BUF_LINES * 2)   // 2 bytes per pixel
#define LVGL_TICK_MS        2
#define LVGL_TASK_STACK     (8 * 1024)
#define LVGL_TASK_PRIORITY  2

static SemaphoreHandle_t lvgl_mutex;
static esp_lcd_panel_io_handle_t lcd_io;

/* Called by the SPI driver when a strip of pixels has been sent completely.
 * We tell LVGL that it may draw the next strip.
 * Note: this runs inside an interrupt, so keep it short. */
static bool on_pixels_sent(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                           void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

/* LVGL calls this when a strip is drawn and has to go to the display */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int width = lv_area_get_width(area);
    int height = lv_area_get_height(area);

    /* LVGL stores a pixel low byte first, the display wants high byte first */
    lv_draw_sw_rgb565_swap(px_map, width * height);

    hx8357_set_window(lcd_io, area->x1, area->y1, area->x2, area->y2);
    hx8357_write_pixels(lcd_io, px_map, width * height * 2);
    /* No lv_display_flush_ready() here: on_pixels_sent() does that later */
}

/* LVGL needs to know how much time has passed; a timer tells it every 2 ms */
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

bool display_lock(int timeout_ms)
{
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mutex, ticks) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGive(lvgl_mutex);
}

/* The LVGL task: an endless loop that lets LVGL redraw what has changed */
static void lvgl_task(void *arg)
{
    for (;;) {
        uint32_t delay_ms = 10;
        if (display_lock(-1)) {
            delay_ms = lv_timer_handler();   // returns: "call me again in X ms"
            display_unlock();
        }
        if (delay_ms > 50) delay_ms = 50;
        if (delay_ms < 5)  delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void display_backlight(bool on)
{
#if PIN_LCD_BL >= 0
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
#endif
}

void display_init(void)
{
#if PIN_LCD_BL >= 0
    gpio_reset_pin(PIN_LCD_BL);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, 0);           // light off until the first picture
#endif

    /* ---- SPI bus ----
     * '.name = value' inside { } fills in a struct by field name,
     * like keyword arguments in Python. Fields you leave out become 0. */
    spi_bus_config_t bus_config = {
        .sclk_io_num = PIN_LCD_CLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUF_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    /* ---- LVGL display object (needed below as callback argument) ---- */
    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    /* ---- Panel IO: sends commands and pixels over SPI, handles CS and D/C ---- */
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_pixels_sent,
        .user_ctx = disp,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                             &io_config, &lcd_io));

    ESP_ERROR_CHECK(hx8357_init(lcd_io, PIN_LCD_RST, LCD_FLIPPED));
    ESP_LOGI(TAG, "HX8357D started, %d x %d, SPI at %d MHz",
             LCD_H_RES, LCD_V_RES, LCD_SPI_HZ / 1000000);

    /* ---- LVGL draw buffer ----
     * MALLOC_CAP_DMA = memory the SPI driver can send from directly. */
    uint8_t *draw_buf = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    assert(draw_buf != NULL);
    lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    /* ---- LVGL clock ---- */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    /* ---- LVGL task ---- */
    lvgl_mutex = xSemaphoreCreateMutex();
    assert(lvgl_mutex != NULL);
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, NULL, 1);
}
