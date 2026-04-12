/**
 * @file  ui_pages.h
 * @brief Right-side page container with horizontal swipe + snap.
 *
 * The left weather column stays fixed on the screen. Only the right
 * portion (x=300..800, 500 px wide) is scrollable and holds multiple
 * pages of right-side content (page 1: compass+strip, page 2: indoor,
 * page 3: controls).
 */

#ifndef UI_PAGES_H
#define UI_PAGES_H

#include "lvgl.h"

/** Width of the right-side page area (screen width minus left column). */
#define PAGE_RIGHT_W   500
#define PAGE_RIGHT_X   300

/**
 * Create the scroll container on the screen. Call once after the left
 * column has been placed. Internally creates three 500×480 child pages.
 */
void ui_pages_create(lv_obj_t *screen);

/** Return the right-side container for page @p index (0, 1, or 2). */
lv_obj_t *ui_pages_get_right_page(int index);

/** Register a callback that fires when the visible page changes. */
typedef void (*ui_pages_change_cb_t)(int page);
void ui_pages_set_change_cb(ui_pages_change_cb_t cb);

#endif /* UI_PAGES_H */
