#include "panel_alert.h"

#include <stddef.h>

#define ALERT_IDLE   lv_color_hex(0x2B2B2B)
#define ALERT_ON     lv_color_hex(0xD8A23E)

#define ALERT_SIZE   32
#define ALERT_MAX    4

static lv_obj_t *s_storm_alerts[ALERT_MAX];
static bool s_storm_active = false;

static void storm_blink_cb(void *var, int32_t value)
{
    lv_obj_t *obj = var;
    if (obj == NULL) return;
    lv_obj_set_style_text_opa(obj, (lv_opa_t)value, 0);
}

static void storm_indicator_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_coord_t cx = a.x1 + ALERT_SIZE / 2;
    lv_coord_t top = a.y1 + 1;
    lv_coord_t bottom = a.y1 + ALERT_SIZE - 2;

    lv_point_t pts[4] = {
        { cx, top },
        { a.x1 + 2, bottom },
        { a.x2 - 2, bottom },
        { cx, top },
    };

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_obj_get_style_text_color(obj, 0);
    line_dsc.opa = lv_obj_get_style_text_opa(obj, 0);
    line_dsc.width = 3;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    for (int i = 0; i < 3; ++i) {
        lv_draw_line(draw_ctx, &line_dsc, &pts[i], &pts[i + 1]);
    }

    lv_draw_rect_dsc_t mark_dsc;
    lv_draw_rect_dsc_init(&mark_dsc);
    mark_dsc.bg_color = line_dsc.color;
    mark_dsc.bg_opa = line_dsc.opa;
    mark_dsc.radius = LV_RADIUS_CIRCLE;
    mark_dsc.border_width = 0;
    mark_dsc.shadow_width = 0;

    lv_area_t stem = {
        cx - 1,
        a.y1 + 10,
        cx + 1,
        a.y1 + 20,
    };
    lv_draw_rect(draw_ctx, &mark_dsc, &stem);

    lv_area_t dot = {
        cx - 2,
        a.y1 + 24,
        cx + 2,
        a.y1 + 28,
    };
    lv_draw_rect(draw_ctx, &mark_dsc, &dot);
}

static void apply_storm_state(lv_obj_t *obj)
{
    if (obj == NULL) return;

    lv_anim_del(obj, storm_blink_cb);

    if (s_storm_active) {
        lv_obj_set_style_text_color(obj, ALERT_ON, 0);
        lv_obj_set_style_text_opa(obj, LV_OPA_30, 0);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_exec_cb(&anim, storm_blink_cb);
        lv_anim_set_values(&anim, LV_OPA_30, LV_OPA_COVER);
        lv_anim_set_time(&anim, 1100);
        lv_anim_set_playback_time(&anim, 1100);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&anim);
    } else {
        lv_obj_set_style_text_color(obj, ALERT_IDLE, 0);
        lv_obj_set_style_text_opa(obj, LV_OPA_70, 0);
    }

    lv_obj_invalidate(obj);
}

lv_obj_t *panel_alert_create_storm(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, ALERT_SIZE, ALERT_SIZE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_text_opa(obj, LV_OPA_70, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, storm_indicator_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    for (size_t i = 0; i < ALERT_MAX; ++i) {
        if (s_storm_alerts[i] == NULL) {
            s_storm_alerts[i] = obj;
            break;
        }
    }

    apply_storm_state(obj);
    return obj;
}

void panel_alert_set_storm_active(bool active)
{
    if (active == s_storm_active) return;
    s_storm_active = active;
    for (size_t i = 0; i < ALERT_MAX; ++i) {
        apply_storm_state(s_storm_alerts[i]);
    }
}
