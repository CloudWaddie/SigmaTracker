idf_component_register(SRCS "app.cpp" "core.cpp" "display.cpp" "input.cpp" "power.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_usb_serial_jtag esp_timer nvs_flash usb esp_driver_i2c
    EMBED_FILES "dictionary.bin")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
