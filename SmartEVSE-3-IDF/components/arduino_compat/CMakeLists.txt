idf_component_register(
    SRC_DIRS
        "."
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
        driver
        esp_timer
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        app_update      # esp_ota_* moved to app_update in IDF v5+
        esp_partition
        esp_driver_spi  # driver/spi_master.h now lives here in v6
        esp_driver_gpio # driver/gpio.h now lives here in v6
        esp_driver_uart # driver/uart.h now lives here in v6
        esp_driver_ledc # driver/ledc.h now lives here in v6
        esp_adc         # esp_adc/adc_oneshot.h + esp_adc/adc_cali.h
        esp_ringbuf     # freertos/ringbuf.h now lives here in v6
)

# C++ shims (WiFi, HTTPClient, Update, ESP, String) require C++17.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# In IDF v6, -Werror=missing-field-initializers is on by default and
# structs like wifi_ap_config_t have new members. The shim's
# `wifi_config_t cfg = {0}` initializers trigger errors. Silence just
# this warning for the shim.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-missing-field-initializers
)

# The shim includes driver/spi_master.h and other IDF headers. The v6
# build system doesn't always propagate include dirs through REQUIRES,
# so we explicitly add the necessary PUBLIC include dirs.
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${idf_components}/nvs_flash/include"
    "${idf_components}/driver/include"
    "${idf_components}/esp_driver_spi/include"
    "${idf_components}/esp_driver_gpio/include"
    "${idf_components}/esp_wifi/include"
    "${idf_components}/esp_event/include"
    "${idf_components}/esp_netif/include"
    "${idf_components}/esp_http_client/include"
    "${idf_components}/app_update/include"
    "${idf_components}/esp_partition/include"
    "${idf_components}/esp_timer/include"
    "${idf_components}/esp_adc/include"
    "${idf_components}/esp_driver_rmt/include"
)


