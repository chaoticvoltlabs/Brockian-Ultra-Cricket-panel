/**
 * @file  demo_data.h
 * @brief Locally-generated fake weather data with smooth random drift.
 */

#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include <stdint.h>

typedef struct {
    float temp;        /* outdoor temperature, degC    (-10 .. 35)   */
    float feel_temp;   /* wind-chill feels-like, degC  (derived)     */
    float wind_bft;    /* steady wind, Beaufort        (0 .. 8)      */
    float gust_bft;    /* gust, Beaufort               (wind+1 .. wind+3) */
    float humidity;    /* relative humidity, %          (20 .. 95)    */
    float pressure;    /* barometric pressure, hPa      (980 .. 1030) */
    float wind_dir;    /* wind direction, degrees       (0 .. 360)    */
} demo_data_t;

/**
 * @brief  Start the demo data timer.
 *         Creates an LVGL timer that fires every ~4 s, generates new values
 *         via random walk, and calls all UI update functions.
 *         Must be called after all ui_*_create() functions and from within
 *         the LVGL lock.
 */
void demo_data_init(void);

#endif /* DEMO_DATA_H */
