/*
 * board_config.h — which ESP32 pin goes to which display pin
 *
 * Board:   Wemos LOLIN32 Lite (ESP32-D0WDQ6, 4 MB flash, no PSRAM)
 * Display: Adafruit 3.5" TFT 320x480 breakout (HX8357D), wired in SPI mode
 *
 * '#define' is a text replacement done before compiling. It is the usual way
 * to write constants in C, like NAME = value at the top of a Python module.
 */
#pragma once

#define LCD_SPI_HOST   SPI3_HOST   // the "VSPI" bus of the ESP32
#define PIN_LCD_CLK    18          // display pin CLK
#define PIN_LCD_MOSI   23          // display pin MOSI
#define PIN_LCD_MISO   19          // display pin MISO (optional, not used yet)
#define PIN_LCD_CS     5           // display pin CS
#define PIN_LCD_DC     17          // display pin D/C
#define PIN_LCD_RST    16          // display pin RST

/* Backlight: the "Lite" pin has a pull-up on the breakout, so the light is
 * on when the pin is not connected. Set to a GPIO number (for example 4) to
 * switch the backlight from the program; -1 means "not connected". */
#define PIN_LCD_BL     (-1)

/* SPI clock. 20 MHz is safe with loose jumper wires. With short wires you can
 * try 26 or 40 MHz for a faster screen refresh. */
#define LCD_SPI_HZ     (20 * 1000 * 1000)

/* Screen size in landscape */
#define LCD_H_RES      480
#define LCD_V_RES      320

/* Is the picture upside down? Change 0 into 1. */
#define LCD_FLIPPED    0
