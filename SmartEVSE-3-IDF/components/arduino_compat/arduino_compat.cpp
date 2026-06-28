// arduino_compat.cpp - backing implementation for the Arduino compatibility
// shim declared in arduino_compat.h. The goal is to let the existing v3
// source code (esp32.cpp, network_common.cpp, etc.) compile against native
// ESP-IDF v6.0.1 with a one-line #include "arduino_compat.h" swap.
//
// Scope (intentionally narrow):
//   - pinMode / digitalWrite / digitalRead / analogRead
//   - delay / millis / micros
//   - LOW / HIGH / INPUT / OUTPUT / INPUT_PULLUP / RISING / FALLING / CHANGE
//   - String class (delegates to std::string for the very few places it
//     appears; the report showed ~0 real String usage in core logic)
//   - SerialCompat / Serial1 / Serial2 (UART wrappers over uart_driver_*)
//   - PreferencesCompat (NVS-backed key/value store with the Arduino
//     method names used in esp32.cpp: getUChar/putUChar/getUShort/...)
//
// Out of scope (handled in the app component CMake by deleting the
// corresponding include lines from src/*.cpp):
//   - WiFi.*           => esp_wifi / esp_netif
//   - HTTPClient       => esp_http_client
//   - ESPmDNS          => mdns.h
//   - Update           => esp_https_ota
//   - Arduino OTA      => esp_https_ota
//   - SPI              => driver/spi_master.h
//   - Wire             => driver/i2c.h
// Note: this is a C++ translation unit, so we use // line comments rather
// than /* block comments to avoid GCC's nested-comment warning.

#include "arduino_compat.h"

#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include "driver/gpio.h"        // v6: provided by esp_driver_gpio
#include "driver/uart.h"         // v6: provided by esp_driver_uart
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "arduino_compat";

/* ---- GPIO helpers --------------------------------------------------------- */
void pinMode(uint8_t pin, uint8_t mode) {
    if (pin == (uint8_t)-1) return; /* SPI_MISO=-1, SPI_SS=-1 sentinels */
    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << pin);
    io.intr_type = GPIO_INTR_DISABLE;
    if (mode == INPUT)            io.mode = GPIO_MODE_INPUT;
    else if (mode == OUTPUT)      io.mode = GPIO_MODE_OUTPUT;
    else if (mode == INPUT_PULLUP)io.mode = GPIO_MODE_INPUT;
    if (mode == INPUT_PULLUP)     io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);
}

void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin == (uint8_t)-1) return;
    gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
}

int digitalRead(uint8_t pin) {
    if (pin == (uint8_t)-1) return 0;
    return gpio_get_level((gpio_num_t)pin) ? HIGH : LOW;
}

/* ---- SerialCompat (UART wrapper) ------------------------------------------ *
 * SmartEVSE-3 uses Serial1 (RS485) and (in v4) Serial2 (CH32 link). The
 * Arduino API exposed: begin(baud, config), printf(), available(), read(),
 * print()/println(). We provide just enough to make the v3 code compile
 * and work.
 */

//static uart_port_t uart_num_for(SerialHandle *h) { return (uart_port_t)h->port; }

void Serial_begin(SerialHandle *h, uint32_t baud) {
    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config((uart_port_t)h->port, &cfg);
    if (h->tx_pin >= 0) uart_set_pin((uart_port_t)h->port, h->tx_pin, h->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (!h->installed) {
        uart_driver_install((uart_port_t)h->port, 1024, 1024, 0, NULL, 0);
        h->installed = 1;
    }
    h->baud = baud;
}

int Serial_available(SerialHandle *h) {
    size_t n = 0;
    uart_get_buffered_data_len((uart_port_t)h->port, &n);
    return (int)n;
}

int Serial_read(SerialHandle *h) {
    uint8_t c = 0;
    return (uart_read_bytes((uart_port_t)h->port, &c, 1, 0) == 1) ? c : -1;
}

int Serial_peek(SerialHandle *h) {
    /* uart driver has no peek; approximate with a non-blocking read. */
    return Serial_read(h);
}

void Serial_flush(SerialHandle *h) {
    // Wait for TX FIFO to drain before returning — eModbus calls flush() then
    // immediately pulls RTS low; if we only flush the RX buffer the RS-485
    // transceiver gets disabled before the last byte leaves the UART hardware.
    uart_wait_tx_done((uart_port_t)h->port, pdMS_TO_TICKS(100));
}

void Serial_print(SerialHandle *h, const char *s) {
    if (!s) return;
    uart_write_bytes((uart_port_t)h->port, s, strlen(s));
}

void Serial_println(SerialHandle *h, const char *s) {
    Serial_print(h, s);
    uart_write_bytes((uart_port_t)h->port, "\r\n", 2);
}

void Serial_printf(SerialHandle *h, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) uart_write_bytes((uart_port_t)h->port, buf, (size_t)n);
}
size_t Serial_write_byte(SerialHandle *h, uint8_t c) {
    return uart_write_bytes((uart_port_t)h->port, &c, 1);
}
size_t Serial_write_buf (SerialHandle *h, const uint8_t *buf, size_t len) {
    return uart_write_bytes((uart_port_t)h->port, buf, len);
}
/* The handles are static so they don't collide with the eModbus shim's
 * `HardwareSerial Serial1` global. Clients go through Serial1_get().
 */
static SerialHandle Serial1_handle = {
    .port = UART_NUM_1,
    .tx_pin = -1,
    .rx_pin = -1,
    .installed = 0,
};
static SerialHandle Serial2_handle = {
    .port = UART_NUM_2,
    .tx_pin = -1,
    .rx_pin = -1,
    .installed = 0,
};
SerialHandle *Serial1_get(void) { return &Serial1_handle; }
SerialHandle *Serial2_get(void) { return &Serial2_handle; }

/* ---- PreferencesCompat (NVS) ---------------------------------------------- *
 * Mirrors the subset of the Arduino Preferences API used by esp32.cpp:
 *   begin(name, readonly=false)
 *   end()
 *   getUChar/getUShort/getULong  (return 0 if missing)
 *   putUChar/putUShort/putULong  (return size)
 *   isKey()
 *   clear()
 *   remove()
 */

int Preferences_begin(Preferences *p, const char *name, int readonly) {
    p->handle = 0;
    p->readonly = readonly;
    strncpy(p->namespace_name, name, sizeof(p->namespace_name) - 1);
    p->namespace_name[sizeof(p->namespace_name) - 1] = 0;
    esp_err_t err = nvs_open(name, readonly ? NVS_READONLY : NVS_READWRITE, &p->handle);
    return (err == ESP_OK) ? 1 : 0;
}

void Preferences_end(Preferences *p) {
    if (p && p->handle) { nvs_close(p->handle); p->handle = 0; }
}

void Preferences_clear(Preferences *p) { if (p && p->handle) nvs_erase_all(p->handle); }
void Preferences_remove(Preferences *p, const char *k) {
    if (!p || !p->handle || !k) return;
    nvs_erase_key(p->handle, k);
}

/* Arduino's Preferences getters return the supplied default value when the
 * key is missing (NVS returns ESP_ERR_NVS_NOT_FOUND and leaves the output
 * untouched), so each getter falls back to `def` on any non-OK result. */
uint8_t  Preferences_getUChar (Preferences *p, const char *k, uint8_t  def) { uint8_t  v=def; if (p && p->handle && nvs_get_u8 (p->handle,k,&v)!=ESP_OK) v=def; return v; }
uint16_t Preferences_getUShort(Preferences *p, const char *k, uint16_t def) { uint16_t v=def; if (p && p->handle && nvs_get_u16(p->handle,k,&v)!=ESP_OK) v=def; return v; }
uint32_t Preferences_getULong (Preferences *p, const char *k, uint32_t def) { uint32_t v=def; if (p && p->handle && nvs_get_u32(p->handle,k,&v)!=ESP_OK) v=def; return v; }
int8_t   Preferences_getChar  (Preferences *p, const char *k, int8_t   def) { int8_t   v=def; if (p && p->handle && nvs_get_i8 (p->handle,k,&v)!=ESP_OK) v=def; return v; }
int16_t  Preferences_getShort (Preferences *p, const char *k, int16_t  def) { int16_t  v=def; if (p && p->handle && nvs_get_i16(p->handle,k,&v)!=ESP_OK) v=def; return v; }
int32_t  Preferences_getLong  (Preferences *p, const char *k, int32_t  def) { int32_t  v=def; if (p && p->handle && nvs_get_i32(p->handle,k,&v)!=ESP_OK) v=def; return v; }

/* Query the stored string length (incl. null terminator) so the C++ wrapper
 * can allocate an exact-size buffer. Returns 0 when the key is missing. */
size_t   Preferences_getStringLength(Preferences *p, const char *k) {
    if (!p || !p->handle) return 0;
    size_t len = 0;
    if (nvs_get_str(p->handle, k, NULL, &len) != ESP_OK) return 0;
    return len;                                          /* includes null terminator */
}
size_t   Preferences_getString(Preferences *p, const char *k, char *out, size_t maxlen) {
    if (!p || !p->handle || !out || maxlen == 0) return 0;
    size_t len = maxlen;
    if (nvs_get_str(p->handle, k, out, &len) != ESP_OK) { out[0] = 0; return 0; }
    return len ? len - 1 : 0;                            /* return char count, excluding null */
}
size_t   Preferences_getBytes (Preferences *p, const char *k, void *out, size_t maxlen) {
    if (!p || !p->handle || !out) return 0;
    size_t len = maxlen;
    if (nvs_get_blob(p->handle, k, out, &len) != ESP_OK) return 0;
    return len;                                          /* number of bytes read */
}

size_t Preferences_putUChar (Preferences *p, const char *k, uint8_t  v) { return nvs_set_u8 (p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putUShort(Preferences *p, const char *k, uint16_t v) { return nvs_set_u16(p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putULong (Preferences *p, const char *k, uint32_t v) { return nvs_set_u32(p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putChar  (Preferences *p, const char *k, int8_t   v) { return nvs_set_i8 (p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putShort (Preferences *p, const char *k, int16_t  v) { return nvs_set_i16(p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putString(Preferences *p, const char *k, const char *v){ return nvs_set_str(p->handle,k,v)==ESP_OK?1:0; }
size_t Preferences_putBytes (Preferences *p, const char *k, const void *v, size_t len) { return nvs_set_blob(p->handle,k,v,len)==ESP_OK?1:0; }
size_t Preferences_putBool  (Preferences *p, const char *k, bool v) { return nvs_set_u8(p->handle, k, v ? 1 : 0) == ESP_OK ? 1 : 0; }
bool   Preferences_getBool  (Preferences *p, const char *k, bool defv) {
    uint8_t v = defv ? 1 : 0;
    if (nvs_get_u8(p->handle, k, &v) != ESP_OK) v = defv ? 1 : 0;
    return v != 0;
}
bool Preferences_isKey(Preferences *p, const char *k) {
    nvs_iterator_t it = NULL;
    nvs_entry_info_t info;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, p->namespace_name, NVS_TYPE_ANY, &it);
    bool found = false;
    while (err == ESP_OK) {
        if (nvs_entry_info(it, &info) == ESP_OK && strcmp(info.key, k) == 0) { found = true; break; }
        err = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    return found;
}

/* ---- OneTimeInit for NVS partition --------------------------------------- */
void nvs_init_once(void) {
    static bool done = false;
    if (done) return;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    done = true;
}

/* Global Arduino-style Preferences object. The v3 source references
 * this as `preferences` (an Arduino-style global). */
Preferences preferences;

/* ---- ESPClass shim ----------------------------------------------------- *
 * The heap/alloc methods live in update_compat.cpp; we just include
 * the system headers here so the rest of the shim can use the heap
 * APIs in its own helpers.
 */
#include "esp_heap_caps.h"
#include "esp_system.h"

