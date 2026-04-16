idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "touch.c"
        "panel_alert.c"
        "clock_ui.c"
        "outlook_ui.c"
        "control_ui.c"
        "control_post.c"
        "indoor_ui.c"
        "weather_col.c"
        "lv_mem_psram.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi
        nvs_flash
        esp_netif
        esp_event
        esp_lcd
        esp_timer
        esp_http_client
        driver
        esp_driver_i2c
        freertos
        log
        lwip
        esp_mm
)

# Tell LVGL to look for lv_conf.h in our INCLUDE_DIRS (".")
target_compile_definitions(__idf_main PRIVATE LV_CONF_INCLUDE_SIMPLE)

# Math library for cosf/sinf in clock_ui.c
target_link_libraries(__idf_main PRIVATE m)
