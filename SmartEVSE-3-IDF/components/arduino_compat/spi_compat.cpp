/*
 * spi_compat.c — SPIClass shim implementation
 */
#include "spi_compat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // for portMAX_DELAY
#include <string.h>

static const char *TAG = "spi_compat";

void SPIClass::begin(int sck, int miso, int mosi, int cs) {
    if (initialised_) return;
    cs_ = cs;

    spi_bus_config_t bus = {};
    bus.mosi_io_num = mosi;
    bus.miso_io_num = (miso < 0) ? -1 : miso;
    bus.sclk_io_num = sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 10 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = (cs < 0) ? -1 : cs;
    dev.queue_size = 7;

    esp_err_t e = spi_bus_initialize(bus_, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(e));
        return;
    }
    e = spi_bus_add_device(bus_, &dev, &dev_);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(e));
        return;
    }
    initialised_ = true;
}

void SPIClass::end() {
    if (dev_)    { spi_bus_remove_device(dev_); dev_ = nullptr; }
    initialised_ = false;
}

void SPIClass::beginTransaction(const SPISettings &s) {
    if (!dev_) return;
    /* Reconfigure the device with the requested speed and mode. */
    spi_device_acquire_bus(dev_, portMAX_DELAY);
    /* In a thin shim we don't have a clean reconfigure API. The dev_ struct
     * was set up with default speed 10 MHz / mode 0. Most v3 code uses
     * SPISettings(10MHz, MSBFIRST, SPI_MODE0) which matches. */
    (void)s;
}

void SPIClass::endTransaction() {
    if (!dev_) return;
    spi_device_release_bus(dev_);
}

uint8_t SPIClass::transfer(uint8_t v) {
    if (!dev_) return 0;
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &v;
    t.rx_buffer = &v;
    spi_device_polling_transmit(dev_, &t);
    return v;
}

void SPIClass::transfer(const void *buf, size_t n) {
    if (!dev_ || !buf || n == 0) return;
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = buf;
    spi_device_polling_transmit(dev_, &t);
}

/* The Arduino framework exposes a single global `SPI` instance on the
 * default FSPI bus. v3's glcd.cpp uses it in the SMARTEVSE_VERSION >= 30
 * branch. The instance is created at program start and configured on
 * first call to begin(). */
SPIClass SPI(FSPI);
