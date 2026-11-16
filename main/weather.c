/*
 * weather.c — fetch weather data from OpenWeatherMap and unpack it
 *
 * We use two free endpoints:
 *   /data/2.5/weather   -> the weather right now (a small answer, ~0.5 kB)
 *   /data/2.5/forecast  -> 5 days in steps of 3 hours (a big answer, ~20 kB)
 *
 * The small answer is read completely and unpacked with cJSON (comparable to
 * json.loads in Python).
 *
 * The big answer is a problem on this board: cJSON would need well over
 * 100 kB of RAM to unpack 20 kB of JSON, and there is no PSRAM. So we never
 * keep the whole answer in memory. The answer arrives in small chunks; a
 * "scanner" looks at every character, cuts out one forecast at a time (each
 * is a small { ... } object of ~0.7 kB) and only that piece goes to cJSON.
 * Compare with reading a big file line by line in Python instead of
 * calling read() on the whole file.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

#include "weather.h"
#include "credentials.h"

static const char *TAG = "weather";

#define NOW_BUF_SIZE    2048     // the "weather right now" answer
#define ITEM_BUF_SIZE   1536     // one forecast object
#define CHUNK_SIZE      512      // how much we read from the network at a time

/* ================= HTTP ================= */

/* A "function pointer type": on_data_t is a function that receives a chunk
 * of the answer. In Python you would just pass a function as an argument;
 * in C you first describe what such a function looks like. */
typedef void (*on_data_t)(const char *data, int len, void *ctx);

/* Does a GET request and hands the answer to 'on_data' chunk by chunk.
 * Returns the HTTP status code (200 = OK), or -1 on a network error. */
static int http_get(const char *url, on_data_t on_data, void *ctx)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,   // check the HTTPS certificate
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return -1;

    int status = -1;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);

        char chunk[CHUNK_SIZE];
        int len;
        while ((len = esp_http_client_read(client, chunk, sizeof(chunk))) > 0) {
            if (status == 200) {
                on_data(chunk, len, ctx);
            }
        }
        if (len < 0) status = -1;                      // connection broke halfway
    }
    esp_http_client_cleanup(client);
    return status;
}

static void error_for_status(int status, char *error, int error_len)
{
    if (status == -1)       snprintf(error, error_len, "No connection to API");
    else if (status == 401) snprintf(error, error_len, "HTTP 401: API key invalid or not active yet");
    else if (status == 429) snprintf(error, error_len, "HTTP 429: too many requests");
    else                    snprintf(error, error_len, "HTTP %d", status);
}

/* ================= JSON helpers ================= */

/* Small helpers to read a number or text from JSON safely.
 * If the field does not exist, you get the fallback value. */
static double json_num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static void json_str(const cJSON *obj, const char *key, char *dest, size_t dest_len)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    snprintf(dest, dest_len, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

/* "weather" is a list in the JSON; we take the first element */
static void parse_condition(const cJSON *parent, char *main, size_t main_len,
                            char *desc, size_t desc_len)
{
    const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(parent, "weather"), 0);
    json_str(first, "main", main, main_len);
    if (desc != NULL) {
        json_str(first, "description", desc, desc_len);
    }
}

/* ================= Part 1: the weather right now ================= */

typedef struct {
    char buf[NOW_BUF_SIZE];
    int  len;
} text_buffer_t;

/* Collects the chunks into one string (the answer is small) */
static void collect_text(const char *data, int len, void *ctx)
{
    text_buffer_t *tb = ctx;
    int room = (int)sizeof(tb->buf) - 1 - tb->len;
    if (len > room) len = room;
    memcpy(tb->buf + tb->len, data, len);
    tb->len += len;
    tb->buf[tb->len] = '\0';              // C strings end with a 0 byte
}

static bool parse_now(const char *json, weather_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return false;

    const cJSON *m = cJSON_GetObjectItem(root, "main");
    out->temp       = json_num(m, "temp", 0);
    out->feels_like = json_num(m, "feels_like", 0);
    out->temp_min   = json_num(m, "temp_min", 0);
    out->temp_max   = json_num(m, "temp_max", 0);
    out->humidity   = json_num(m, "humidity", 0);
    out->pressure   = json_num(m, "pressure", 0);

    const cJSON *wind = cJSON_GetObjectItem(root, "wind");
    out->wind_kmh = json_num(wind, "speed", 0) * 3.6;          // m/s -> km/h
    out->wind_deg = json_num(wind, "deg", 0);
    double gust   = json_num(wind, "gust", -1);                // not always present
    out->gust_kmh = gust < 0 ? -1 : gust * 3.6;

    out->clouds       = json_num(cJSON_GetObjectItem(root, "clouds"), "all", 0);
    out->visibility_m = json_num(root, "visibility", 0);
    out->rain_1h      = json_num(cJSON_GetObjectItem(root, "rain"), "1h", 0);

    const cJSON *sys = cJSON_GetObjectItem(root, "sys");
    out->sunrise   = json_num(sys, "sunrise", 0);
    out->sunset    = json_num(sys, "sunset", 0);
    out->dt        = json_num(root, "dt", 0);
    out->tz_offset = json_num(root, "timezone", 0);
    parse_condition(root, out->main, sizeof(out->main), out->description, sizeof(out->description));

    cJSON_Delete(root);      // in C you clean up yourself: no garbage collector!
    return true;
}

/* ================= Part 2: the forecast, one object at a time ================= */

/* Everything the scanner has to remember between two chunks */
typedef struct {
    weather_t *out;
    int64_t today;            // day number of "now" in local time
    int  noon_distance[DAILY_COUNT];   // how far the chosen forecast is from 13:00

    int  matched;             // how many characters of "list":[ we have seen
    bool in_list;             // are we inside the list of forecasts?
    bool in_string, escaped;  // are we inside a "text"? was the previous char a \ ?
    int  depth;               // how many { are open
    int  len;                 // characters collected in 'item'
    bool too_long;
    char item[ITEM_BUF_SIZE];
} scanner_t;

/* Day number (days since 1970) in local time. Two moments on the same local
 * calendar day give the same number. */
static int64_t local_day(int64_t utc, int tz_offset)
{
    return (utc + tz_offset) / 86400;
}

/* One complete forecast object has been cut out: unpack it and use it */
static void handle_forecast_item(scanner_t *s)
{
    cJSON *item = cJSON_Parse(s->item);
    if (item == NULL) return;
    weather_t *out = s->out;

    int64_t dt = json_num(item, "dt", 0);
    const cJSON *m = cJSON_GetObjectItem(item, "main");
    float temp = json_num(m, "temp", 0);
    int pop = (int)(json_num(item, "pop", 0) * 100 + 0.5);

    /* The first few go on the screen as 3-hour forecasts */
    if (out->forecast_count < FORECAST_COUNT) {
        forecast_t *f = &out->forecast[out->forecast_count++];
        f->dt = dt;
        f->temp = temp;
        f->pop = pop;
        parse_condition(item, f->main, sizeof(f->main), f->description, sizeof(f->description));
    }

    /* All of them count towards the summary of their day.
     * Day 1 = tomorrow, which is index 0 in the 'daily' array. */
    int index = (int)(local_day(dt, out->tz_offset) - s->today) - 1;
    if (index >= 0 && index < DAILY_COUNT) {
        daily_t *d = &out->daily[index];
        if (index >= out->daily_count) {           // first forecast of this day
            out->daily_count = index + 1;
            d->dt = dt;
            d->temp_min = FLT_MAX;
            d->temp_max = -FLT_MAX;
            d->pop = 0;
            d->main[0] = '\0';
            s->noon_distance[index] = 24 * 3600;
        }
        float low  = json_num(m, "temp_min", temp);
        float high = json_num(m, "temp_max", temp);
        if (low < d->temp_min)  d->temp_min = low;
        if (high > d->temp_max) d->temp_max = high;
        if (pop > d->pop)       d->pop = pop;

        /* The category of the day = the forecast closest to 13:00 local time */
        int seconds_of_day = (int)((dt + out->tz_offset) % 86400);
        int distance = abs(seconds_of_day - 13 * 3600);
        if (distance < s->noon_distance[index]) {
            s->noon_distance[index] = distance;
            parse_condition(item, d->main, sizeof(d->main), NULL, 0);
        }
    }
    cJSON_Delete(item);
}

/* Looks at one character of the answer.
 * The answer looks like: {"cod":"200",...,"list":[{...},{...},...],"city":{...}}
 * We wait for "list":[ and then cut out every {...} up to the closing ]. */
static void scan_char(scanner_t *s, char c)
{
    static const char start[] = "\"list\":[";

    if (!s->in_list) {
        if (c == start[s->matched]) {
            s->matched++;
            if (start[s->matched] == '\0') {       // whole pattern seen
                s->in_list = true;
            }
        } else {
            s->matched = (c == start[0]) ? 1 : 0;
        }
        return;
    }

    if (s->depth == 0) {                            // between two objects
        if (c == '{') {
            s->depth = 1;
            s->len = 0;
            s->too_long = false;
            s->item[s->len++] = c;
        } else if (c == ']') {
            s->in_list = false;                     // end of the list: done
            s->matched = 0;
        }
        return;
    }

    /* Inside an object: keep the character and track where we are */
    if (s->len < ITEM_BUF_SIZE - 1) {
        s->item[s->len++] = c;
    } else {
        s->too_long = true;
    }

    if (s->in_string) {                             // braces inside "text" do not count
        if (s->escaped)      s->escaped = false;
        else if (c == '\\')  s->escaped = true;
        else if (c == '"')   s->in_string = false;
    } else if (c == '"') {
        s->in_string = true;
    } else if (c == '{') {
        s->depth++;
    } else if (c == '}') {
        s->depth--;
        if (s->depth == 0 && !s->too_long) {        // object complete
            s->item[s->len] = '\0';
            handle_forecast_item(s);
        }
    }
}

static void scan_chunk(const char *data, int len, void *ctx)
{
    scanner_t *s = ctx;
    for (int i = 0; i < len; i++) {
        scan_char(s, data[i]);
    }
}

/* ================= Putting it together ================= */

bool weather_fetch(weather_t *out, char *error, int error_len)
{
    char url[256];
    bool ok = false;

    /* calloc = malloc + fill with zeros. These are too big for the stack of
     * the task, so they come from the heap and are freed at the end. */
    text_buffer_t *now = calloc(1, sizeof(text_buffer_t));
    scanner_t *scanner = calloc(1, sizeof(scanner_t));
    if (now == NULL || scanner == NULL) {
        snprintf(error, error_len, "Out of memory");
        goto done;      // in C, 'goto' to a clean-up step is a common pattern
    }

    /* ---- 1. The weather right now ---- */
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&units=metric&lang=en&appid=%s",
             WEATHER_LAT, WEATHER_LON, OWM_API_KEY);
    int status = http_get(url, collect_text, now);
    if (status != 200) {
        error_for_status(status, error, error_len);
        goto done;
    }
    if (!parse_now(now->buf, out)) {
        snprintf(error, error_len, "Invalid JSON from API");
        goto done;
    }

    /* ---- 2. The forecast ---- */
    out->forecast_count = 0;
    out->daily_count = 0;
    scanner->out = out;
    scanner->today = local_day(out->dt, out->tz_offset);
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/forecast?lat=%s&lon=%s&units=metric&lang=en&appid=%s",
             WEATHER_LAT, WEATHER_LON, OWM_API_KEY);
    status = http_get(url, scan_chunk, scanner);
    if (status != 200) {
        ESP_LOGW(TAG, "Fetching the forecast failed (status %d)", status);
    }

    ESP_LOGI(TAG, "%s: %.1f C, %s, %d forecasts, %d days",
             WEATHER_CITY, out->temp, out->description, out->forecast_count, out->daily_count);
    ok = true;

done:
    free(now);          // free(NULL) is allowed
    free(scanner);
    return ok;
}
