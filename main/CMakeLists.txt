file(GLOB MAIN_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.c)
set(MAIN_REQUIRES arcade_core bluepad32 btstack LovyanGFX esp_driver_gpio esp_driver_gptimer esp_driver_ledc
                  esp_driver_spi esp_driver_uart esp_driver_sdspi esp_partition esp_timer fatfs nvs_flash sdmmc)
if(IDF_TARGET STREQUAL "esp32")
    # The built-in DAC (CYD boards)
    list(APPEND MAIN_REQUIRES esp_driver_dac)
else()
    # ESP32-S3 boards: ES8311 codec (I2S + I2C), SDMMC card slot, native USB
    list(APPEND MAIN_REQUIRES esp_driver_i2s esp_driver_sdmmc esp_driver_usb_serial_jtag)
endif()
idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "." "boards"
    REQUIRES ${MAIN_REQUIRES})

# The panic handler goes through crash_note.cpp first, which keeps what the
# crash was in RTC memory for the log (Settings > Log)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
