// Arduino.h - Arduino surface for the eModbus component.
//
// eModbus's source files #include <Arduino.h> expecting the Arduino core's
// Print, Stream, HardwareSerial, and a global Serial1 instance. We provide
// just enough of those here, backed by the arduino_compat shim that
// already wraps native ESP-IDF v6 UART drivers.
//
// This file is ONLY in the eModbus component's include path. The main
// app code uses the regular arduino_compat.h from components/arduino_compat/.

#ifndef ARDUINO_H_EMODBUS
#define ARDUINO_H_EMODBUS

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "Stream.h"          // eModbus shim: brings in arduino_compat.h + Print

/* UART config bits used by the v3 source. These match the legacy
 * Arduino macros (SERIAL_8N1 etc.). eModbus's begin() ignores them,
 * but the call site still requires the symbol to be defined. */
#ifndef SERIAL_8N1
#define SERIAL_8N1  0x800001C
#define SERIAL_8E1  0x800003C
#define SERIAL_8O1  0x800005C
#define SERIAL_8N2  0x800001E
#endif

// ---- HardwareSerial: a Stream wrapping a SerialHandle -------------------
class HardwareSerial : public Stream {
public:
    HardwareSerial(SerialHandle *h) : _h(h) {}

    // Arduino-style: begin(baud, config=SERIAL_8N1, rx=-1, tx=-1, invert=false, bufSize=256)
    void begin(uint32_t baud, int /*config*/ = 0x800001C,
               int8_t rx = -1, int8_t tx = -1,
               bool /*invert*/ = false, int /*bufSize*/ = 256) {
        if (rx >= 0) _h->rx_pin = rx;
        if (tx >= 0) _h->tx_pin = tx;
        Serial_begin(_h, baud);
    }
    void end() {}
    int available() override { return Serial_available(_h); }
    int read() override { return Serial_read(_h); }
    int peek() { return Serial_peek(_h); }
    void flush() { Serial_flush(_h); }
    size_t write(uint8_t c) { return Serial_write_byte(_h, c); }
    size_t write(const uint8_t *buf, size_t len) { return Serial_write_buf(_h, buf, len); }
    // Arduino accepts strings/chars as well; route them through the byte write.
    size_t write(const char *s) { if (!s) return 0; return write((const uint8_t *)s, strlen(s)); }
    size_t write(char c) { return write((uint8_t)c); }

    // Stubs for the Stream pure-virtuals that eModbus doesn't actually call.
    int  readBytes(char *, int) override { return 0; }
    int  readBytesUntil(char, char *, int) override { return 0; }
    String readStringUntil(char) override { return String(""); }
    bool connected() override { return false; }

    // Arduino HardwareSerial extras used by eModbus.
    uint32_t baudRate() const { return _h->baud; }
    void setRxFIFOFull(uint8_t /*thresh*/) { /* not supported by IDF v6 driver; ignored */ }

    // For the RTUutils::prepareHardwareSerial() call.
    void setRxBufferSize(size_t) {}
    void setTxBufferSize(size_t) {}

    operator SerialHandle *() { return _h; }
private:
    SerialHandle *_h;
};

// ---- The global Serial1 instance used by SmartEVSE-3 --------------------
// SmartEVSE-3 uses Serial1 on UART_NUM_1 (RS-485). The shim's
// SerialHandle for it is created in Serial_init() (called from app_main).
extern HardwareSerial Serial1;

// ---- Stub `Serial` (the default Arduino UART0 debug port) ---------------
// eModbus's Logging.cpp references `&Serial` to set up a log device. We
// don't actually use it (log level is forced to ERROR in CMake), so a
// tiny silent sink is enough. The actual class is owned by the
// arduino_compat shim (components/arduino_compat/include/arduino_compat_base.h)
// so we forward-declare it here. Both translations units agree on the
// same class type and the linker resolves the single `Serial` instance.
#include "arduino_compat_base.h"  /* for _SerialStubPrint (real definition) */

#endif // ARDUINO_H_EMODBUS
