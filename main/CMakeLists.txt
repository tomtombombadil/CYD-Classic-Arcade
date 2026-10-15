file(GLOB MAIN_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.c)
idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "." "boards"
    REQUIRES arcade_core bluepad32 btstack LovyanGFX esp_driver_dac esp_driver_gpio esp_driver_gptimer esp_driver_ledc esp_driver_spi
             esp_driver_uart esp_driver_sdspi esp_partition esp_timer fatfs nvs_flash sdmmc)
