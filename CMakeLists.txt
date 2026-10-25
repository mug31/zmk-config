# Right-half OLED: portrait status screen (BT + battery on top, Goku below).
# Only built where CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM is set (corne_right, see build.yaml).
if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  zephyr_library()
  zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(src/goku_screen.c)
endif()
