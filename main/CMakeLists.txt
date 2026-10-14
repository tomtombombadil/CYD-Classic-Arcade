idf_component_register(
    SRCS "main.cpp" "pad_bt.c" "audio_out.cpp"
    INCLUDE_DIRS "." "boards"
    REQUIRES bluepad32 btstack LovyanGFX esp_driver_dac esp_timer nvs_flash)
