/**
 * @file  ui_pages.c
 * @brief Right-side page container with horizontal snap scrolling.
 *
 * A single scrollable lv_obj sits at (300, 0) with size (500, 480).
 * It contains N children, each 500×480, arranged as a flex row.
 * LV_SCROLL_SNAP_START + LV_OBJ_FLAG_SCROLL_ONE give clean manual
 * page-by-page swipe behaviour. No autoplay, no fancy physics.
 */

#include "ui_pages.h"
#include "buc_display.h"

#define PAGE_COUNT  4

static lv_obj_t *s_scroll;
static lv_obj_t *s_pages[PAGE_COUNT];
static ui_pages_change_cb_t s_change_cb;
static int s_cur_page = 0;

static void apply_page(int page, lv_anim_enable_t anim)
{
    if(page < 0) page = 0;
    if(page >= PAGE_COUNT) page = PAGE_COUNT - 1;

    s_cur_page = page;
    lv_obj_scroll_to_x(s_scroll, page * PAGE_RIGHT_W, anim);
    if (s_change_cb) s_change_cb(page);
}

/* Detect which page snapped into view after scroll ends. */
static void scroll_end_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    int32_t x = lv_obj_get_scroll_x(obj);
    int page = (x + PAGE_RIGHT_W / 2) / PAGE_RIGHT_W;
    if (page < 0) page = 0;
    if (page >= PAGE_COUNT) page = PAGE_COUNT - 1;

    if (page != s_cur_page) {
        s_cur_page = page;
        if (s_change_cb) s_change_cb(page);
    }

    lv_obj_scroll_to_x(obj, s_cur_page * PAGE_RIGHT_W, LV_ANIM_OFF);
}

static void gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    switch (dir) {
    case LV_DIR_LEFT:
        apply_page(s_cur_page + 1, LV_ANIM_ON);
        break;
    case LV_DIR_RIGHT:
        apply_page(s_cur_page - 1, LV_ANIM_ON);
        break;
    default:
        break;
    }
}

void ui_pages_create(lv_obj_t *screen)
{
    /* ── Scroll viewport ──────────────────────────────────────────── */
    s_scroll = lv_obj_create(screen);
    lv_obj_remove_style_all(s_scroll);
    lv_obj_set_pos(s_scroll, PAGE_RIGHT_X, 0);
    lv_obj_set_size(s_scroll, PAGE_RIGHT_W, LCD_V_RES);

    /* Horizontal flex row: children tile side by side */
    lv_obj_set_flex_flow(s_scroll, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_scroll, 0, 0);

    /* Swipe configuration */
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_scroll, LV_SCROLL_SNAP_START);
    lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(s_scroll, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_OFF);

    /* Page-change detection */
    lv_obj_add_event_cb(s_scroll, scroll_end_cb, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    /* ── Page children ────────────────────────────────────────────── */
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_pages[i] = lv_obj_create(s_scroll);
        lv_obj_remove_style_all(s_pages[i]);
        lv_obj_set_size(s_pages[i], PAGE_RIGHT_W, LCD_V_RES);
        lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
}

lv_obj_t *ui_pages_get_right_page(int index)
{
    if (index < 0 || index >= PAGE_COUNT) return NULL;
    return s_pages[index];
}

void ui_pages_set_change_cb(ui_pages_change_cb_t cb)
{
    s_change_cb = cb;
}
