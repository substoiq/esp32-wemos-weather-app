#pragma once
#include <stdbool.h>

void display_init(void);               // start the display and LVGL
void display_backlight(bool on);       // backlight on/off (only if PIN_LCD_BL is set)

/* LVGL is not thread-safe: call these two around every LVGL call that does
 * NOT come from an LVGL callback (for example from the weather task).
 * Compare with 'with lock:' around shared data in Python threading. */
bool display_lock(int timeout_ms);     // -1 = wait as long as needed
void display_unlock(void);
