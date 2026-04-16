cmake_minimum_required(VERSION 3.16...4.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(echarts_clock)

# Route LVGL's heap through our PSRAM wrapper (see main/lv_mem_psram.c).
# Keep the compile definitions, include path, and idf::main link together:
# with CONFIG_LV_CONF_SKIP=y the managed lvgl__lvgl component ignores lv_conf.h,
# so this is the only path that makes LV_MEM_CUSTOM resolve correctly.
# lv_conf_internal.h guards each macro with #ifndef, so these -D flags win.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PUBLIC
    LV_MEM_CUSTOM=1
    "LV_MEM_CUSTOM_INCLUDE=\"lv_mem_psram.h\""
    LV_MEM_CUSTOM_ALLOC=lv_mem_psram_alloc
    LV_MEM_CUSTOM_FREE=lv_mem_psram_free
    LV_MEM_CUSTOM_REALLOC=lv_mem_psram_realloc
)
target_include_directories(${lvgl_lib} PUBLIC
    "${CMAKE_SOURCE_DIR}/main"
)
target_link_libraries(${lvgl_lib} PUBLIC idf::main)
