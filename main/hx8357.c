/*
 * hx8357.c — small driver for the HX8357D display controller (SPI)
 *
 * ESP-IDF has no ready-made driver for this chip, so we talk to it ourselves.
 * That is less work than it sounds. The display understands "commands": one
 * byte that says what to do, followed by zero or more parameter bytes. The
 * D/C pin tells the display whether a byte is a command or data; the esp_lcd
 * "panel IO" layer of ESP-IDF toggles that pin for us.
 *
 * The start-up sequence below comes from the HX8357D datasheet and is the
 * same one the Adafruit Arduino library uses.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "hx8357.h"

/* Command numbers (from the datasheet) */
#define CMD_SWRESET   0x01    // software reset
#define CMD_SLPOUT    0x11    // leave sleep mode
#define CMD_DISPON    0x29    // display on
#define CMD_CASET     0x2A    // set column range (x)
#define CMD_PASET     0x2B    // set row range (y)
#define CMD_RAMWR     0x2C    // pixel data follows
#define CMD_TEON      0x35
#define CMD_MADCTL    0x36    // orientation
#define CMD_COLMOD    0x3A    // colour depth
#define CMD_TEARLINE  0x44
#define CMD_SETOSC    0xB0
#define CMD_SETPWR1   0xB1
#define CMD_SETRGB    0xB3
#define CMD_SETCYC    0xB4
#define CMD_SETCOM    0xB6
#define CMD_SETC      0xB9    // enable the extended commands
#define CMD_SETSTBA   0xC0
#define CMD_SETPANEL  0xCC
#define CMD_SETGAMMA  0xE0

/* MADCTL bits: mirror X, mirror Y, swap X and Y */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20

/* One start-up step: a command, its parameters, and a pause afterwards.
 * A struct is comparable to a Python dataclass. */
typedef struct {
    uint8_t cmd;
    uint8_t len;          // number of parameter bytes
    uint8_t data[34];
    uint16_t delay_ms;    // pause after this command
} init_step_t;

/* 'static const' = only visible in this file, and stored in flash, not RAM.
 * Compare with a module-level tuple of tuples in Python. */
static const init_step_t init_steps[] = {
    {CMD_SWRESET,  0, {0}, 10},
    {CMD_SETC,     3, {0xFF, 0x83, 0x57}, 300},
    {CMD_SETRGB,   4, {0x80, 0x00, 0x06, 0x06}, 0},
    {CMD_SETCOM,   1, {0x25}, 0},                       // VCOM voltage
    {CMD_SETOSC,   1, {0x68}, 0},                       // 70 Hz refresh
    {CMD_SETPANEL, 1, {0x05}, 0},                       // BGR panel, gate direction
    {CMD_SETPWR1,  6, {0x00, 0x15, 0x1C, 0x1C, 0x83, 0xAA}, 0},
    {CMD_SETSTBA,  6, {0x50, 0x50, 0x01, 0x3C, 0x1E, 0x08}, 0},
    {CMD_SETCYC,   7, {0x02, 0x40, 0x00, 0x2A, 0x2A, 0x0D, 0x78}, 0},
    {CMD_SETGAMMA, 34, {0x02, 0x0A, 0x11, 0x1D, 0x23, 0x35, 0x41, 0x4B, 0x4B,
                        0x42, 0x3A, 0x27, 0x1B, 0x08, 0x09, 0x03, 0x02, 0x0A,
                        0x11, 0x1D, 0x23, 0x35, 0x41, 0x4B, 0x4B, 0x42, 0x3A,
                        0x27, 0x1B, 0x08, 0x09, 0x03, 0x00, 0x01}, 0},
    {CMD_COLMOD,   1, {0x55}, 0},                       // 16 bits per pixel
    {CMD_TEON,     1, {0x00}, 0},
    {CMD_TEARLINE, 2, {0x00, 0x02}, 0},
    {CMD_SLPOUT,   0, {0}, 150},
    {CMD_DISPON,   0, {0}, 50},
};

esp_err_t hx8357_init(esp_lcd_panel_io_handle_t io, int rst_gpio, bool flipped)
{
    /* Hardware reset: pull the RST pin low for a moment */
    if (rst_gpio >= 0) {
        gpio_reset_pin(rst_gpio);
        gpio_set_direction(rst_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    /* sizeof(array) / sizeof(one element) = number of elements, like len() */
    int count = sizeof(init_steps) / sizeof(init_steps[0]);
    for (int i = 0; i < count; i++) {
        const init_step_t *s = &init_steps[i];
        esp_err_t err = esp_lcd_panel_io_tx_param(io, s->cmd, s->data, s->len);
        if (err != ESP_OK) {
            return err;
        }
        if (s->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(s->delay_ms));
        }
    }

    /* Landscape: swap X and Y, and mirror one axis. Mirroring the other axis
     * instead turns the picture 180 degrees. */
    uint8_t madctl = flipped ? (MADCTL_MV | MADCTL_MX) : (MADCTL_MV | MADCTL_MY);
    return esp_lcd_panel_io_tx_param(io, CMD_MADCTL, &madctl, 1);
}

void hx8357_set_window(esp_lcd_panel_io_handle_t io, int x1, int y1, int x2, int y2)
{
    /* Each coordinate is 16 bits, sent as two bytes: high byte first.
     * '>> 8' shifts the upper 8 bits down, '& 0xFF' keeps the lower 8 bits. */
    uint8_t cols[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    uint8_t rows[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    esp_lcd_panel_io_tx_param(io, CMD_CASET, cols, 4);
    esp_lcd_panel_io_tx_param(io, CMD_PASET, rows, 4);
}

void hx8357_write_pixels(esp_lcd_panel_io_handle_t io, const void *pixels, size_t len_bytes)
{
    esp_lcd_panel_io_tx_color(io, CMD_RAMWR, pixels, len_bytes);
}
