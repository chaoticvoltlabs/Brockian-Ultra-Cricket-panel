/**
 * @file  ui_controls.h
 * @brief Page 4: scene triggers + direct light/switch controls.
 *
 * Top section  : up to 4 scene/automation buttons (stateless triggers).
 * Bottom section: up to 6 direct target buttons (on/off/unavailable).
 */

#ifndef UI_CONTROLS_H
#define UI_CONTROLS_H

#include "lvgl.h"

#define CTRL_SCENES_MAX   4
#define CTRL_TARGETS_MAX  6

typedef enum {
    CTRL_OFF,
    CTRL_ON,
    CTRL_UNAVAILABLE,
} ui_ctrl_state_t;

typedef void (*ui_controls_target_press_cb_t)(int index);

typedef enum {
    UI_CTRL_PRESS_SHORT,
    UI_CTRL_PRESS_LONG,
} ui_ctrl_press_kind_t;

typedef void (*ui_controls_scene_press_cb_t)(int index, ui_ctrl_press_kind_t kind);

/** Build the control page as a child of @p parent. Call once. */
void ui_controls_create(lv_obj_t *parent);

/** Set an optional callback for direct target button presses. */
void ui_controls_set_target_press_cb(ui_controls_target_press_cb_t cb);

/** Set an optional callback for scene/action button presses. */
void ui_controls_set_scene_press_cb(ui_controls_scene_press_cb_t cb);

/** Update which top-row scene button is visually active. */
void ui_controls_set_active_scene(int index);

/** Show small debug identity text at the bottom-right of page 3. */
void ui_controls_set_debug_identity(const char *ipv4, const char *mac);

/** Update a scene button label and whether it is available. */
void ui_controls_set_scene_slot(int index, const char *label, bool enabled);

/** Update a direct-control button label. */
void ui_controls_set_target_label(int index, const char *label);

/**
 * Update a target button's visual state.
 * @param index  0–5
 * @param state  CTRL_OFF / CTRL_ON / CTRL_UNAVAILABLE
 */
void ui_controls_set_target_state(int index, ui_ctrl_state_t state);

/** Read the current visual state for a direct-control button. */
ui_ctrl_state_t ui_controls_get_target_state(int index);

/** Re-apply palette-dependent styles after a theme switch. */
void ui_controls_apply_theme(void);

#endif /* UI_CONTROLS_H */
