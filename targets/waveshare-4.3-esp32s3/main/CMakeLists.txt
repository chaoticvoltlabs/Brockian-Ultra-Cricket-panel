idf_component_register(
    SRCS
        "main.c"
        "hw_init.c"
        "hw_touch.c"
        "ui_weather.c"
        "ui_compass.c"
        "ui_wind_strip.c"
        "ui_pages.c"
        "ui_indoor.c"
        "ui_controls.c"
        "ui_clock.c"
        "ui_theme.c"
        "net_wifi.c"
        "panel_api.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_lcd
        esp_driver_i2c
        esp_driver_gpio
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_client
)

# ── Auto-increment build number before each build ──────────────────
# Runs the Python script, generates main/build_number.h, and forces
# main.c to recompile so the new BUILD_NUMBER is always linked in.
add_custom_target(build_number ALL
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_DIR}/increment_build.py
    COMMENT "Incrementing build number"
)
add_dependencies(${COMPONENT_LIB} build_number)
set_source_files_properties(
    ${COMPONENT_DIR}/main.c
    PROPERTIES OBJECT_DEPENDS ${COMPONENT_DIR}/build_number.h
)
