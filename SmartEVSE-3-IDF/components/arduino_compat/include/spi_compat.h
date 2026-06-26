/*
 * spi_compat.h — Arduino SPIClass shim over driver/spi_master.h
 *
 * Just enough surface for SmartEVSE-3 v3:
 *   SPIClass lcd_spi(FSPI);
 *   lcd_spi.begin(clk, miso, mosi, cs);
 *   lcd_spi.beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE0));
 *   lcd_spi.transfer(buf, n);
 *   lcd_spi.transfer(val);
 *   lcd_spi.endTransaction();
 *   lcd_spi.end();
 *
 * The v3 source uses two SPI buses:
 *   FSPI  (the default SPI bus)  — used by EtherLCD and the v4 modem
 *   HSPI  (the secondary bus)     — used by the v4 modem
 *
 * For v3 (ESP32, no modem), the only SPI device is the LCD on the FSPI bus
 * with the LCD_A0 / LCD_RST pins as GPIO. We register it as a single SPI
 * device in begin() and use spi_device_polling_transmit() for transfer().
 */
#ifndef SPI_COMPAT_H
#define SPI_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

#define FSPI 0
#define HSPI 1

#define SPI_MODE0  0
#define SPI_MODE1  1
#define SPI_MODE2  2
#define SPI_MODE3  3
#define MSBFIRST   0
#define LSBFIRST   1

class SPISettings {
public:
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
        : clock_(clock), bit_order_(bitOrder), mode_(dataMode) {}
    uint32_t clock()     const { return clock_; }
    uint8_t  bitOrder()  const { return bit_order_; }
    uint8_t  mode()      const { return mode_; }
private:
    uint32_t clock_;
    uint8_t  bit_order_;
    uint8_t  mode_;
};

class SPIClass {
public:
    SPIClass(int bus) : bus_(bus == HSPI ? SPI2_HOST : SPI3_HOST) {}
    void begin(int sck, int miso, int mosi, int cs);
    void end();
    void beginTransaction(const SPISettings &s);
    void endTransaction();
    uint8_t transfer(uint8_t v);
    void    transfer(const void *buf, size_t n);
    /* Arduino also has SPIClass::writeBytes(buf, len). It is a no-op on
     * the tx-only path used by the v3 LCD driver (the device polls the
     * peripheral for the response, but the v3 code only writes a single
     * data byte at a time). Provide it as a thin wrapper so glcd.cpp's
     * `SPI.writeBytes(buf, len)` call site compiles. */
    void    writeBytes(const uint8_t *buf, size_t n) { transfer(buf, n); }
private:
    spi_host_device_t bus_;
    spi_device_handle_t dev_ = nullptr;
    int      cs_      = -1;
    bool     initialised_ = false;
};

/* The Arduino framework exposes a single global `SPI` instance on the
 * default SPI bus. v3's glcd.cpp uses `SPI.transfer(...)` in the
 * SMARTEVSE_VERSION >= 30 branch (i.e. for v3 hardware — the v4 board
 * uses the second HSPI bus under the name `LCD_SPI2`). */
extern SPIClass SPI;

#endif
