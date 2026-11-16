/*
 * hx8357.h — small driver for the HX8357D display controller (SPI)
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

/* Resets the display and sends the start-up commands.
 * The display ends up in landscape (480 x 320), 16-bit colour. */
esp_err_t hx8357_init(esp_lcd_panel_io_handle_t io, int rst_gpio, bool flipped);

/* Selects the rectangle that the next block of pixels will fill
 * (x2 and y2 are the last column and row, inclusive). */
void hx8357_set_window(esp_lcd_panel_io_handle_t io, int x1, int y1, int x2, int y2);

/* Sends pixels (RGB565, high byte first) into the selected rectangle.
 * Returns right away; the SPI driver sends the data in the background. */
void hx8357_write_pixels(esp_lcd_panel_io_handle_t io, const void *pixels, size_t len_bytes);
