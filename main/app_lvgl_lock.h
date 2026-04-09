/**
 * @file  app_lvgl_lock.h
 * @brief Shared LVGL API lock, defined in main.c.
 *
 * Any task that is NOT the LVGL task itself (HTTP client, etc.) must
 * acquire this lock around calls into LVGL. The LVGL task already holds
 * it around lv_timer_handler, so code running inside an LVGL timer
 * callback must NOT acquire it again.
 */

#ifndef APP_LVGL_LOCK_H
#define APP_LVGL_LOCK_H

#include <sys/lock.h>

extern _lock_t lvgl_api_lock;

#endif /* APP_LVGL_LOCK_H */
