/**
 * @file  demo_data.c
 * @brief Fake weather data generator with smooth random walk.
 *
 * Uses esp_random() (hardware RNG) for randomness.  Values drift slowly
 * from a realistic baseline so the display updates feel natural.
 * The LVGL timer callback runs inside lv_timer_handler(), which is
 * already protected by the LVGL API lock -- no extra locking needed.
 */

#include "demo_data.h"
#include "ui_weather.h"
#include "ui_compass.h"
#include "ui_wind_strip.h"
#include "lvgl.h"
#include "esp_random.h"

/* ── Current state ──────────────────────────────────────────────────── */
static demo_data_t s_data = {
    .temp       = 11.7f,
    .feel_temp  =  0.9f,
    .wind_bft   =  3.0f,
    .gust_bft   =  5.0f,
    .humidity   = 43.0f,
    .pressure   = 995.0f,
    .wind_dir   = 225.0f,
};

/* ── Random helpers ─────────────────────────────────────────────────── */

/** Return a float in [-1.0, +1.0] using hardware RNG. */
static float rand_f(void)
{
    uint32_t r = esp_random();
    return ((float)(r & 0xFFFF) / 32768.0f) - 1.0f;   /* -1 .. +1 */
}

/** Clamp value between min and max. */
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ── Timer callback ─────────────────────────────────────────────────── */

static void demo_timer_cb(lv_timer_t *t)
{
    (void)t;

    /* Random walk: small delta per tick, clamped to valid range */
    s_data.temp     += rand_f() * 0.3f;
    s_data.temp      = clampf(s_data.temp, -10.0f, 35.0f);

    s_data.wind_bft += rand_f() * 0.2f;
    s_data.wind_bft  = clampf(s_data.wind_bft, 0.0f, 8.0f);

    s_data.gust_bft  = s_data.wind_bft + 1.0f + (rand_f() + 1.0f);  /* +1..+3 */
    s_data.gust_bft  = clampf(s_data.gust_bft, s_data.wind_bft + 0.5f, 12.0f);

    s_data.humidity += rand_f() * 1.0f;
    s_data.humidity  = clampf(s_data.humidity, 20.0f, 95.0f);

    s_data.pressure += rand_f() * 0.5f;
    s_data.pressure  = clampf(s_data.pressure, 980.0f, 1030.0f);

    s_data.wind_dir += rand_f() * 8.0f;
    if (s_data.wind_dir < 0)    s_data.wind_dir += 360.0f;
    if (s_data.wind_dir >= 360) s_data.wind_dir -= 360.0f;

    /* Derived values */
    s_data.feel_temp = s_data.temp - s_data.wind_bft * 1.2f - 2.0f;

    /* Push to UI */
    ui_weather_update(&s_data);
    ui_compass_set_direction(s_data.wind_dir);
    ui_wind_strip_update(s_data.wind_bft, s_data.gust_bft);
}

/* ── Public API ─────────────────────────────────────────────────────── */

void demo_data_init(void)
{
    /* Push initial values to UI */
    ui_weather_update(&s_data);
    ui_compass_set_direction(s_data.wind_dir);
    ui_wind_strip_update(s_data.wind_bft, s_data.gust_bft);

    /* Create LVGL timer -- fires every 4 s */
    lv_timer_create(demo_timer_cb, 4000, NULL);
}
