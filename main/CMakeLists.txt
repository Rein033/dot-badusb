# The languages, the list, the glass and the keys (script.c, ducky.c, store.c, ui.c, gfx.c,
# keys.c) are pure C and run in the host tests too; the Active Object, the keyboard and the web server bring
# the SDK, TinyUSB and ESP-IDF.
#
# On the kode-sim host build (IDF_TARGET linux) there is no USB: hidkb_sim.c stands in for
# hidkb.c and TinyUSB / esp_hal_usb are left out, so the app still links and runs in the sim.
idf_build_get_property(kode_target IDF_TARGET)
if(kode_target STREQUAL "linux")
    set(KODE_HID_SRC hidkb_sim.c sim_stubs.c)
    set(KODE_USB_REQ)
else()
    set(KODE_HID_SRC hidkb.c)
    set(KODE_USB_REQ esp_hal_usb)
endif()

idf_component_register(
    SRCS "app_main.c" "app.c" "script.c" "ducky.c" "keys.c" "builtin.c" "store.c" "ui.c" "gfx.c"
         "fonts_aa.c" "icons_aa.c" ${KODE_HID_SRC} "web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES kode
    PRIV_REQUIRES kdrv khal esp_hw_support ${KODE_USB_REQ} esp_timer esp_http_server
)

# TinyUSB reads its settings from a tusb_config.h on its own include path: ours (main/tusb/).
# The sim has no TinyUSB component, so only wire it for a real chip.
if(NOT kode_target STREQUAL "linux")
    idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
    target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tusb")
endif()
