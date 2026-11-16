/*
 * ui.c — the weather screen, in the style of GitHub (Primer design system)
 *
 * Layout (landscape, 480 x 320 pixels):
 *
 *  ┌──────────────────────────────────────────────────────────┐
 *  │ weather / breda  (OpenWeatherMap)        ● Updated 17:58 │  header bar
 *  ├──────────────────────────────────────────────────────────┤
 *  │ Now                                    min 9° · max 14°  │
 *  │  14°          │ Feels  12°            Humidity  82%      │
 *  │               │ Wind   18 km/h SW     Pressure  1013 hPa │
 *  │ (Light rain)  │ Gusts  31 km/h        Clouds    75%      │
 *  │               │ Sun    07:51 / 19:12  Visibility 10 km   │
 *  ├────────────────────────────┬─────────────────────────────┤
 *  │ Forecast          0.4 mm/h │ Next days        Mon 16 Nov │
 *  │ 18:00  13°  Light rain 40% │ Tue   9° / 13°  Rain    60% │
 *  │ 21:00  11°  Clouds         │ Wed   7° / 12°  Clouds      │
 *  │ ... (5 rows)               │ ... (4 rows)                │
 *  └────────────────────────────┴─────────────────────────────┘
 *
 * All positions are in pixels, with (0,0) at the top left.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "lvgl.h"
#include "ui.h"
#include "credentials.h"

/* ================= Colours =================
 * GitHub has a dark and a light theme. Set to 0 for light. */
#define UI_DARK_THEME 1

#if UI_DARK_THEME
#define C_CANVAS 0x0d1117       // background
#define C_SUBTLE 0x161b22       // box headers and header bar
#define C_BORDER 0x30363d       // borders
#define C_BORDER_MUTED 0x21262d // lines between table rows
#define C_FG 0xe6edf3           // normal text
#define C_FG_MUTED 0x9198a1     // grey text
#define C_ACCENT 0x4493f8       // blue (links)
#define C_SUCCESS 0x3fb950      // green
#define C_DANGER 0xf85149       // red
#define C_ATTENTION 0xd29922    // yellow
#define C_DONE 0xab7df8         // purple
#else
#define C_CANVAS 0xffffff
#define C_SUBTLE 0xf6f8fa
#define C_BORDER 0xd1d9e0
#define C_BORDER_MUTED 0xd8dee4
#define C_FG 0x1f2328
#define C_FG_MUTED 0x59636e
#define C_ACCENT 0x0969da
#define C_SUCCESS 0x1a7f37
#define C_DANGER 0xd1242f
#define C_ATTENTION 0x9a6700
#define C_DONE 0x8250df
#endif

#define DEG "\xC2\xB0" // the degree sign ° in UTF-8

#define SCREEN_W   480
#define STAT_COUNT 8

/* ================= Objects we update later =================
 * 'static' at file level = only visible in this file (a "private" global).
 * 'lv_obj_t *' is a pointer: a reference to an LVGL object, like a Python
 * variable that refers to an object. */
static lv_obj_t *temp_label, *minmax_label, *cond_pill, *cond_pill_text;
static lv_obj_t *status_dot, *status_text;
static lv_obj_t *stat_value[STAT_COUNT];
static lv_obj_t *rain_label, *date_label;
static lv_obj_t *fc_time[FORECAST_COUNT], *fc_temp[FORECAST_COUNT];
static lv_obj_t *fc_desc[FORECAST_COUNT], *fc_pop[FORECAST_COUNT];
static lv_obj_t *day_name[DAILY_COUNT], *day_temp[DAILY_COUNT];
static lv_obj_t *day_desc[DAILY_COUNT], *day_pop[DAILY_COUNT];

/* ================= Helpers for building the screen ================= */

/* A bare object without the default LVGL styling, not scrollable */
static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

/* Gives a label a fixed width and one line; text that is too long ends in "..." */
static void one_line(lv_obj_t *label, int width, const lv_font_t *font)
{
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

/* A GitHub "Box": border, rounded corners and a grey header row.
 * 'header_right' is an "output parameter": the caller passes the address of a
 * variable (&name) and we store the new label in it. Python would simply
 * return a tuple; C functions return one value, so this is the usual way. */
static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *title, lv_obj_t **header_right)
{
    lv_obj_t *box = plain_obj(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(C_CANVAS), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_clip_corner(box, true, 0);

    lv_obj_t *head = plain_obj(box);
    lv_obj_set_size(head, w - 2, 28);
    lv_obj_set_style_bg_color(head, lv_color_hex(C_SUBTLE), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(head, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *t = make_label(head, &lv_font_montserrat_14, C_FG, 12, 0, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 12, 0);

    if (header_right) {
        *header_right = make_label(head, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "");
        lv_obj_align(*header_right, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    return box;
}

/* A GitHub "Label": rounded pill with a coloured border and text */
static lv_obj_t *make_pill(lv_obj_t *parent, lv_obj_t **text_out)
{
    lv_obj_t *pill = plain_obj(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(pill, 9, 0);

    *text_out = lv_label_create(pill);
    lv_obj_set_style_text_font(*text_out, &lv_font_montserrat_12, 0);
    lv_obj_align(*text_out, LV_ALIGN_LEFT_MID, 0, 0);
    return pill;
}

static void set_pill(lv_obj_t *pill, lv_obj_t *text, uint32_t color, const char *str)
{
    lv_color_t c = lv_color_hex(color);
    lv_obj_set_style_bg_color(pill, c, 0);
    lv_obj_set_style_border_color(pill, c, 0);
    lv_obj_set_style_text_color(text, c, 0);
    lv_label_set_text(text, str);
}

/* One table row inside a box, with a thin line above it (except the first) */
static lv_obj_t *make_row(lv_obj_t *box, int index, int row_h, int w)
{
    lv_obj_t *row = plain_obj(box);
    lv_obj_set_pos(row, 0, 28 + index * row_h);
    lv_obj_set_size(row, w, row_h);
    if (index > 0) {
        lv_obj_set_style_border_color(row, lv_color_hex(C_BORDER_MUTED), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
    }
    return row;
}

/* ================= Helpers for the data ================= */

/* Colour per weather type, the way GitHub colours labels */
static uint32_t color_for_condition(const char *main)
{
    if (strcmp(main, "Clear") == 0)
        return C_ATTENTION; // sun: yellow
    if (strcmp(main, "Rain") == 0 ||
        strcmp(main, "Drizzle") == 0)
        return C_ACCENT;    // rain: blue
    if (strcmp(main, "Thunderstorm") == 0)
        return C_DONE;      // thunder: purple
    if (strcmp(main, "Snow") == 0)
        return C_FG;        // snow: white
    return C_FG_MUTED;      // clouds, mist: grey
}

/* Unix time + time zone -> text, in a format like "%H:%M" (same codes as
 * time.strftime in Python) */
static void format_time(int64_t utc, int tz_offset, const char *format, char *out, size_t len)
{
    time_t t = (time_t)(utc + tz_offset);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, len, format, &tm);
}

static const char *wind_direction(int deg)
{
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return dirs[((deg + 22) / 45) % 8];
}

/* First letter upper case: "light rain" -> "Light rain" */
static void capitalize(const char *in, char *out, size_t len)
{
    snprintf(out, len, "%s", in);
    if (out[0])
        out[0] = toupper((unsigned char)out[0]);
}

static int round_temp(float t)
{
    int r = (int)lroundf(t);
    return r == 0 ? 0 : r; // prevents "-0"
}

/* ================= Building the screen ================= */

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_CANVAS), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Header bar ---- */
    lv_obj_t *bar = plain_obj(scr);
    lv_obj_set_size(bar, SCREEN_W, 32);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_SUBTLE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);

    // Left: breadcrumb like "owner / repository"
    lv_obj_t *crumb = plain_obj(bar);
    lv_obj_set_size(crumb, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(crumb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(crumb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(crumb, 6, 0);
    lv_obj_align(crumb, LV_ALIGN_LEFT_MID, 12, 0);

    make_label(crumb, &lv_font_montserrat_16, C_FG, 0, 0, "weather");
    make_label(crumb, &lv_font_montserrat_16, C_FG_MUTED, 0, 0, "/");
    char city[32];
    snprintf(city, sizeof(city), "%s", WEATHER_CITY);
    for (char *p = city; *p; p++)        // walk through the string until the 0 byte
        *p = tolower((unsigned char)*p);
    make_label(crumb, &lv_font_montserrat_16, C_ACCENT, 0, 0, city);

    lv_obj_t *src_text;
    lv_obj_t *src_pill = make_pill(crumb, &src_text);
    set_pill(src_pill, src_text, C_FG_MUTED, "OpenWeatherMap");
    lv_obj_set_style_bg_opa(src_pill, LV_OPA_TRANSP, 0); // GitHub "Public" label: border only
    lv_obj_set_style_margin_left(src_pill, 4, 0);

    // Right: status dot and text, like a CI check
    lv_obj_t *status = plain_obj(bar);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -12, 0);

    status_dot = plain_obj(status);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);

    // At most 150 px wide and one line high; longer text ends in "..."
    status_text = make_label(status, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "");
    lv_obj_set_style_max_width(status_text, 150, 0);
    lv_obj_set_height(status_text, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_label_set_long_mode(status_text, LV_LABEL_LONG_DOT);

    /* ---- Box 1: the weather right now ---- */
    lv_obj_t *now = make_box(scr, 8, 40, 464, 124, "Now", &minmax_label);

    temp_label = make_label(now, &lv_font_montserrat_48, C_FG, 14, 34, "--" DEG);
    cond_pill = make_pill(now, &cond_pill_text);
    lv_obj_set_pos(cond_pill, 12, 92);
    lv_obj_set_style_max_width(cond_pill_text, 104, 0);
    lv_obj_set_height(cond_pill_text, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_label_set_long_mode(cond_pill_text, LV_LABEL_LONG_DOT);
    set_pill(cond_pill, cond_pill_text, C_FG_MUTED, "Loading...");

    // Vertical divider
    lv_obj_t *divider = plain_obj(now);
    lv_obj_set_pos(divider, 142, 38);
    lv_obj_set_size(divider, 1, 76);
    lv_obj_set_style_bg_color(divider, lv_color_hex(C_BORDER_MUTED), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    // Two columns of four values: index 0-3 left, 4-7 right
    static const char *stat_names[STAT_COUNT] = {
        "Feels", "Wind", "Gusts", "Sun",
        "Humidity", "Pressure", "Clouds", "Visibility"};
    for (int i = 0; i < STAT_COUNT; i++) {
        int column = i / 4;                 // integer division, like // in Python
        int y = 36 + (i % 4) * 21;
        int name_x = column == 0 ? 156 : 310;
        int value_x = column == 0 ? 206 : 384;
        make_label(now, &lv_font_montserrat_14, C_FG_MUTED, name_x, y, stat_names[i]);
        stat_value[i] = make_label(now, &lv_font_montserrat_14, C_FG, value_x, y, "--");
    }

    /* ---- Box 2: the coming hours, as a GitHub table ---- */
    lv_obj_t *fc = make_box(scr, 8, 172, 228, 140, "Forecast", &rain_label);
    lv_obj_set_style_text_color(rain_label, lv_color_hex(C_ACCENT), 0);

    for (int i = 0; i < FORECAST_COUNT; i++) {
        lv_obj_t *row = make_row(fc, i, 22, 226);
        fc_time[i] = make_label(row, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "--:--");
        fc_temp[i] = make_label(row, &lv_font_montserrat_14, C_FG, 0, 0, "");
        fc_desc[i] = make_label(row, &lv_font_montserrat_12, C_FG, 0, 0, "");
        one_line(fc_desc[i], 100, &lv_font_montserrat_12);
        fc_pop[i] = make_label(row, &lv_font_montserrat_12, C_ACCENT, 0, 0, "");

        lv_obj_align(fc_time[i], LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_align(fc_temp[i], LV_ALIGN_LEFT_MID, 50, 0);
        lv_obj_align(fc_desc[i], LV_ALIGN_LEFT_MID, 84, 0);
        lv_obj_align(fc_pop[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }

    /* ---- Box 3: the coming days ---- */
    lv_obj_t *days = make_box(scr, 244, 172, 228, 140, "Next days", &date_label);

    for (int i = 0; i < DAILY_COUNT; i++) {
        lv_obj_t *row = make_row(days, i, 27, 226);
        day_name[i] = make_label(row, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "---");
        day_temp[i] = make_label(row, &lv_font_montserrat_14, C_FG, 0, 0, "");
        day_desc[i] = make_label(row, &lv_font_montserrat_12, C_FG, 0, 0, "");
        one_line(day_desc[i], 62, &lv_font_montserrat_12);
        day_pop[i] = make_label(row, &lv_font_montserrat_12, C_ACCENT, 0, 0, "");

        lv_obj_align(day_name[i], LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_align(day_temp[i], LV_ALIGN_LEFT_MID, 46, 0);
        lv_obj_align(day_desc[i], LV_ALIGN_LEFT_MID, 122, 0);
        lv_obj_align(day_pop[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }

    ui_set_status(UI_STATUS_LOADING, "Connecting to Wi-Fi...");
}

/* ================= Updating ================= */

void ui_set_status(ui_status_t status, const char *text)
{
    uint32_t color = status == UI_STATUS_OK      ? C_SUCCESS
                     : status == UI_STATUS_ERROR ? C_DANGER
                                                 : C_ATTENTION;
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(color), 0);
    lv_label_set_text(status_text, text);
}

void ui_update(const weather_t *w)
{
    /* 'char buf[64]' is room for a text of at most 63 characters; snprintf
     * writes into it, like an f-string that needs a place to be stored. */
    char buf[64], t1[8], t2[8];

    lv_label_set_text_fmt(temp_label, "%d" DEG, round_temp(w->temp));
    lv_label_set_text_fmt(minmax_label, "min %d" DEG "  max %d" DEG,
                          round_temp(w->temp_min), round_temp(w->temp_max));

    capitalize(w->description, buf, sizeof(buf));
    set_pill(cond_pill, cond_pill_text, color_for_condition(w->main), buf);

    /* Left column */
    lv_label_set_text_fmt(stat_value[0], "%d" DEG, round_temp(w->feels_like));
    lv_label_set_text_fmt(stat_value[1], "%d km/h %s", (int)lroundf(w->wind_kmh),
                          wind_direction(w->wind_deg));
    if (w->gust_kmh >= 0)
        lv_label_set_text_fmt(stat_value[2], "%d km/h", (int)lroundf(w->gust_kmh));
    else
        lv_label_set_text(stat_value[2], "--");
    format_time(w->sunrise, w->tz_offset, "%H:%M", t1, sizeof(t1));
    format_time(w->sunset, w->tz_offset, "%H:%M", t2, sizeof(t2));
    lv_label_set_text_fmt(stat_value[3], "%s / %s", t1, t2);

    /* Right column */
    lv_label_set_text_fmt(stat_value[4], "%d%%", w->humidity);
    lv_label_set_text_fmt(stat_value[5], "%d hPa", w->pressure);
    lv_label_set_text_fmt(stat_value[6], "%d%%", w->clouds);
    if (w->visibility_m >= 10000)
        lv_label_set_text(stat_value[7], "10+ km");
    else {
        snprintf(buf, sizeof(buf), "%.1f km", w->visibility_m / 1000.0);
        lv_label_set_text(stat_value[7], buf);
    }

    /* Rain in the last hour, only shown when it rained */
    if (w->rain_1h > 0) {
        snprintf(buf, sizeof(buf), "%.1f mm/h", w->rain_1h);
        lv_label_set_text(rain_label, buf);
    } else {
        lv_label_set_text(rain_label, "");
    }

    format_time(w->dt, w->tz_offset, "%a %d %b", buf, sizeof(buf));
    lv_label_set_text(date_label, buf);

    /* The coming hours */
    for (int i = 0; i < FORECAST_COUNT; i++) {
        if (i < w->forecast_count) {
            const forecast_t *f = &w->forecast[i];
            format_time(f->dt, w->tz_offset, "%H:%M", t1, sizeof(t1));
            lv_label_set_text(fc_time[i], t1);
            lv_label_set_text_fmt(fc_temp[i], "%d" DEG, round_temp(f->temp));
            capitalize(f->description, buf, sizeof(buf));
            lv_label_set_text(fc_desc[i], buf);
            lv_obj_set_style_text_color(fc_desc[i], lv_color_hex(color_for_condition(f->main)), 0);
            if (f->pop > 0)
                lv_label_set_text_fmt(fc_pop[i], "%d%%", f->pop);
            else
                lv_label_set_text(fc_pop[i], "");
        } else {
            lv_label_set_text(fc_time[i], "--:--");
            lv_label_set_text(fc_temp[i], "");
            lv_label_set_text(fc_desc[i], "");
            lv_label_set_text(fc_pop[i], "");
        }
    }

    /* The coming days */
    for (int i = 0; i < DAILY_COUNT; i++) {
        if (i < w->daily_count) {
            const daily_t *d = &w->daily[i];
            format_time(d->dt, w->tz_offset, "%a", buf, sizeof(buf));
            lv_label_set_text(day_name[i], buf);
            lv_label_set_text_fmt(day_temp[i], "%d" DEG " / %d" DEG,
                                  round_temp(d->temp_min), round_temp(d->temp_max));
            /* "Thunderstorm" is too wide for this column; shorten it */
            lv_label_set_text(day_desc[i], strcmp(d->main, "Thunderstorm") == 0 ? "Thunder" : d->main);
            lv_obj_set_style_text_color(day_desc[i], lv_color_hex(color_for_condition(d->main)), 0);
            if (d->pop > 0)
                lv_label_set_text_fmt(day_pop[i], "%d%%", d->pop);
            else
                lv_label_set_text(day_pop[i], "");
        } else {
            lv_label_set_text(day_name[i], "---");
            lv_label_set_text(day_temp[i], "");
            lv_label_set_text(day_desc[i], "");
            lv_label_set_text(day_pop[i], "");
        }
    }

    format_time(w->dt, w->tz_offset, "%H:%M", t1, sizeof(t1));
    snprintf(buf, sizeof(buf), "Updated %s", t1);
    ui_set_status(UI_STATUS_OK, buf);
}
