/*
 * credentials.example.h — EXAMPLE, contains no real data
 *
 * Copy this file to credentials.h (same folder) and fill in your own values:
 *     cp main/credentials.example.h main/credentials.h
 *
 * credentials.h is in .gitignore, so it never ends up on GitHub.
 */
#pragma once

/* Your Wi-Fi network (2.4 GHz only; the ESP32 does not support 5 GHz) */
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

/* Your OpenWeatherMap API key: https://home.openweathermap.org/api_keys
 * A new key can take a few hours before it works (until then: HTTP 401). */
#define OWM_API_KEY     "paste-your-api-key-here"

/* Location: Breda, the Netherlands */
#define WEATHER_CITY    "Breda"
#define WEATHER_LAT     "51.5719"
#define WEATHER_LON     "4.7683"

/* How often the weather is fetched again (in minutes) */
#define WEATHER_REFRESH_MIN  10
