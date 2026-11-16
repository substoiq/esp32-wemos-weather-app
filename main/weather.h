#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FORECAST_COUNT 5      // number of 3-hour forecasts on the screen
#define DAILY_COUNT    4      // number of coming days on the screen

/* One 3-hour forecast, for example "21:00, 12°, light rain, 40%" */
typedef struct {
    int64_t dt;               // moment (Unix time, UTC)
    float   temp;             // °C
    int     pop;              // chance of precipitation in %
    char    main[16];         // main category: "Rain", "Clouds", ...
    char    description[48];  // description: "light rain"
} forecast_t;

/* Summary of one coming day, worked out from that day's 3-hour forecasts */
typedef struct {
    int64_t dt;               // a moment on that day (Unix time, UTC)
    float   temp_min, temp_max;
    int     pop;              // highest chance of precipitation that day, in %
    char    main[16];         // category around midday
} daily_t;

/* Everything we show on the screen.
 * A struct is comparable to a Python dataclass. */
typedef struct {
    float   temp, feels_like, temp_min, temp_max;
    int     humidity;         // %
    int     pressure;         // hPa
    int     clouds;           // cloud cover in %
    int     visibility_m;     // metres, at most 10000
    float   wind_kmh;
    float   gust_kmh;         // negative = not reported
    int     wind_deg;
    float   rain_1h;          // mm in the last hour (0 when dry)
    int64_t dt, sunrise, sunset;
    int     tz_offset;        // time zone difference with UTC in seconds
    char    main[16];
    char    description[48];
    forecast_t forecast[FORECAST_COUNT];
    int     forecast_count;
    daily_t daily[DAILY_COUNT];
    int     daily_count;
} weather_t;

/* Fetches the current weather and the forecast.
 * Returns true on success. On failure, 'error' holds a short description
 * (for example "HTTP 401: API key invalid"). */
bool weather_fetch(weather_t *out, char *error, int error_len);
