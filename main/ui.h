#pragma once
#include "weather.h"

typedef enum {
    UI_STATUS_LOADING,   // yellow dot, like a running GitHub check
    UI_STATUS_OK,        // green
    UI_STATUS_ERROR,     // red
} ui_status_t;

void ui_create(void);                                   // build the screen
void ui_update(const weather_t *w);                     // show new weather data
void ui_set_status(ui_status_t status, const char *text);

/* Note: only call ui_update and ui_set_status between
 * display_lock() and display_unlock(). */
