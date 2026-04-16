# Waveshare 7B ESP32-S3 Showcase

Reference/showcase firmware for the Waveshare ESP32-S3-Touch-LCD-7B.

## Pages

- page 1: clock
- page 2: outlook / storm page
- page 3: control showcase
- page 4: indoor climate

## Intent

This target is a public showcase/reference for the larger 7 inch panel work.
Some page-3 labels and control targets are still example content and are expected to be adapted for a real installation.

## Stability notes

These constraints are intentional and tied to the current stable 7B baseline:

- Keep `CONFIG_LV_CONF_SKIP=y` in [`sdkconfig.defaults`](./sdkconfig.defaults). This target uses the managed `lvgl__lvgl` component in Kconfig mode, so `main/lv_conf.h` is not the source of truth for LVGL config.
- Keep the three-way LVGL heap override together in [`CMakeLists.txt`](./CMakeLists.txt):
  - `LV_MEM_CUSTOM=1` compile definitions on `lvgl__lvgl`
  - `main/` on the LVGL include path
  - `target_link_libraries(${lvgl_lib} PUBLIC idf::main)`
- Keep [`main/lv_mem_psram.c`](./main/lv_mem_psram.c) limited to LVGL heap objects. Do not move framebuffers or display draw buffers onto this allocator; those stay owned by `display.c` in fast memory.
- Keep the indoor grid geometry in [`main/indoor_ui.c`](./main/indoor_ui.c) aligned with the left weather column:
  - `GRID_X=340`
  - `CELL_W=160`
  - `CELL_H=140`
  - `COL_GAP=10`
  - `ROW_SPACING=26`
- Keep `CONFIG_LV_FONT_MONTSERRAT_36=y` and `CONFIG_LV_FONT_MONTSERRAT_48=y` in [`sdkconfig.defaults`](./sdkconfig.defaults). The weather column depends on those sizes.

## LVGL compatibility

This 7 inch target is on LVGL `8.3.11` via the managed component. The 4.3 inch target is on a newer LVGL line, so ports are not mechanical.

Known differences that matter here:

- use `lv_event_get_draw_ctx(e)`, not `lv_event_get_layer(e)`
- use `lv_draw_line(draw_ctx, dsc, &p1, &p2)`, not the newer shorthand variants
- `lv_obj_set_style_margin_top/bottom` is not available in LVGL 8.3; use widget padding or explicit spacer objects instead

## Local config

Create `main/config.h` from the example file and fill in local values.

## Build

```bash
cd targets/waveshare-7b-esp32s3-showcase
idf.py build
```
