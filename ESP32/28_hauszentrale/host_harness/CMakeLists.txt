cmake_minimum_required(VERSION 3.20)
project(hauszentrale_host LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

# Path to LVGL library (git submodule at ../extern/lvgl)
set(LVGL_ROOT_DIR ${CMAKE_SOURCE_DIR}/../../extern/lvgl)

# Use the sdl2 config
set(LV_BUILD_CONF_PATH ${CMAKE_SOURCE_DIR}/lv_conf.h)

# Find SDL2 (needed by lvgl, but we don't link it)
find_package(SDL2 REQUIRED)

# Build LVGL as a library, overriding the SDL include path
add_subdirectory(${LVGL_ROOT_DIR} lvgl_build EXCLUDE_FROM_ALL)

target_compile_definitions(lvgl PRIVATE
    LV_SDL_INCLUDE_PATH="SDL2/SDL.h"
)

target_include_directories(lvgl PRIVATE ${SDL2_INCLUDE_DIRS})

# Add lodepng header to include path (PRIVATE on lvgl doesn't propagate to our exe)
target_include_directories(lvgl PUBLIC
    ${LVGL_ROOT_DIR}/src/libs/lodepng
)

# Our host harness executable.
#
# src/ui/haus_ui.c is the PRODUCTIVE UI — the very file the ESP32 firmware
# compiles (platformio build_src_filter includes <src/>). The harness never
# carries its own copy, so the PNGs always reflect the device code.
add_executable(hauszentrale_host
    src/main.c
    ${CMAKE_SOURCE_DIR}/../src/ui/haus_ui.c
    ${CMAKE_SOURCE_DIR}/../src/ui/lv_font_haus_mono_21.c
    ${CMAKE_SOURCE_DIR}/../src/ui/lv_font_haus_body_21.c
    ${CMAKE_SOURCE_DIR}/../src/ui/lv_font_haus_value_32.c
    ${CMAKE_SOURCE_DIR}/../src/ui/lv_font_haus_large_52.c
    ${CMAKE_SOURCE_DIR}/../src/ui/lv_font_haus_value_110.c
    ${CMAKE_SOURCE_DIR}/../src/haus_chart.cpp
    src/haus_gui_mock.cpp
)

target_compile_definitions(hauszentrale_host PRIVATE
    HAUS_HOST
    LV_CONF_PATH="${CMAKE_SOURCE_DIR}/lv_conf.h"
)

target_link_libraries(hauszentrale_host PRIVATE lvgl)

# LVGL's own targets pull in both liblvgl.a and liblvgl_thorvg.a; the macOS
# linker warns about the duplicates. Silence just that warning.
if(APPLE)
    target_link_options(hauszentrale_host PRIVATE
        -Wl,-no_warn_duplicate_libraries)
endif()

# Ensure lv_conf.h is findable
target_include_directories(hauszentrale_host PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LVGL_ROOT_DIR}/src/libs/lodepng
)

# Project-local headers (haus_gui.h, haus_history.h, …) and shared headers
# (haus_can.h, haus_nrf.h) live in different include roots.
target_include_directories(hauszentrale_host PRIVATE
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/../../../include
)
