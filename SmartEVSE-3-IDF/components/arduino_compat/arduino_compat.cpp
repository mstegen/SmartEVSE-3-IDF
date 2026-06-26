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
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "arduino_compat";

/* ---- Time helpers --------------------------------------------------------- */
uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
uint32_t micros(void) { return (uint32_t)(esp_timer_get_time()); }

void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
void delayMicroseconds(uint32_t us) {
    // esp_timer supports microsecond resolution
    uint64_t start = esp_timer_get_time();
    uint64_t target = start + us;
    while (esp_timer_get_time() < target) {
        // busy wait; the FreeRTOS scheduler may preempt us
    }
}

/* ---- Random --------------------------------------------------------------- */
extern "C" uint32_t esp_random(void); /* from esp_random.h */
long random(long max) {
    if (max <= 0) return 0;
    return (long)(esp_random() % (uint32_t)max);
}
long random(long min, long max) {
    if (max <= min) return min;
    return min + (long)(esp_random() % (uint32_t)(max - min));
}

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

int analogRead(uint8_t pin) {
    /* SmartEVSE-3 v3 doesn't use analogRead directly — the ESP32 ADC is
     * driven by sensor-specific code in src/ that uses native IDF APIs
     * (esp_adc/adc_oneshot.h in v6). This stub exists only for source
     * compatibility; return 0 so any stray calls don't break the build. */
    (void)pin;
    return 0;
}

/* ---- GPIO matrix shim (pinMatrixOutAttach / pinMatrixOutDetach) ----------- *
 * IDF v6 removed the legacy `pinMatrixOutAttach/Detach` helpers. The v3
 * source uses them to temporarily disconnect the SPI MOSI pin and use
 * it as a GPIO button input, then re-attach it. The replacement is the
 * private `gpio_matrix_output()` API in esp_driver_gpio. We expose thin
 * wrappers that take the same arguments as the legacy helpers.
 */
#include "esp_private/gpio.h"
#include "rom/gpio.h"

void pinMatrixOutAttach(uint8_t pin, uint8_t signal_index, bool out_inv, bool oen_inv) {
    /* The legacy helper routed to esp_rom_gpio_matrix_out(); use the ROM
     * version directly — it's a no-op for the same signal and works on
     * all ESP32 targets. */
    rom_gpio_matrix_out((uint32_t)pin, (uint32_t)signal_index,
                        (bool)out_inv, (bool)oen_inv);
}

void pinMatrixOutDetach(uint8_t pin, bool out_inv, bool oen_inv) {
    /* Detach by routing the pin to a "no-op" signal index. SIG_GPIO_OUT
     * is 0x100 in IDF; using the matrix with the same signal is the
     * least surprising way to neutralise the routing. The ESP32 GPIO
     * matrix always allows overriding back, so simply calling the ROM
     * helper with the same pin and a benign signal restores input
     * flexibility. */
    rom_gpio_matrix_out((uint32_t)pin, 0x100, (bool)out_inv, (bool)oen_inv);
}

/* ---- GPIO interrupt shim (attachInterrupt) ------------------------------ *
 * The v3 source calls `attachInterrupt(pin, isr, mode)` to install a
 * CP-pulse detector. v6 IDF replaces the legacy `gpio_isr_register`
 * helper with the GPIO ISR service; the shim installs the service
 * lazily and routes the per-pin ISR through `gpio_isr_handler_add`.
 *
 * Note: a global table is used because the v3 source installs only a
 * handful of interrupts and never detaches them.
 */
#define ARDUINO_ISR_MAX 8
static struct {
    uint8_t         pin;
    arduino_isr_t   fn;
} s_isr_table[ARDUINO_ISR_MAX];
static int s_isr_count = 0;
static bool s_isr_service_installed = false;

static void IRAM_ATTR arduino_isr_dispatch(void *arg) {
    uint32_t mask = (uint32_t)(uintptr_t)arg;
    for (int i = 0; i < s_isr_count; ++i) {
        uint32_t pin_mask = (1ULL << s_isr_table[i].pin);
        if (mask & pin_mask) {
            arduino_isr_t fn = s_isr_table[i].fn;
            if (fn) fn();
        }
    }
}

void attachInterrupt(uint8_t pin, arduino_isr_t isr, int mode) {
    if (!s_isr_service_installed) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %d", (int)err);
            return;
        }
        s_isr_service_installed = true;
    }
    if (s_isr_count >= ARDUINO_ISR_MAX) {
        ESP_LOGE(TAG, "attachInterrupt: ISR table full");
        return;
    }
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    if (mode == RISING)  intr_type = GPIO_INTR_POSEDGE;
    if (mode == FALLING) intr_type = GPIO_INTR_NEGEDGE;
    if (mode == CHANGE)  intr_type = GPIO_INTR_ANYEDGE;
    gpio_set_intr_type((gpio_num_t)pin, intr_type);
    s_isr_table[s_isr_count].pin = pin;
    s_isr_table[s_isr_count].fn  = isr;
    uint32_t pin_mask = (1ULL << pin);
    gpio_isr_handler_add((gpio_num_t)pin, arduino_isr_dispatch, (void *)(uintptr_t)pin_mask);
    gpio_intr_enable((gpio_num_t)pin);
    s_isr_count++;
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

int Serial_readBytesUntil(SerialHandle *h, char term, char *buf, int len) {
    int i = 0;
    while (i < len) {
        int c = Serial_read(h);
        if (c < 0) { vTaskDelay(1); continue; }
        if (c == term) return i;
        buf[i++] = (char)c;
    }
    return i;
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


/* Global Arduino-style Preferences object. */
// (removed duplicate declaration)



/* Global Arduino-style Preferences object. The v3 source references
 * this as `preferences` (an Arduino-style global). */
Preferences preferences;



/* ---- Arduino timer API (legacy hw_timer_t) ------------------------------
 * v3 firmware uses `hw_timer_t *timerA = timerBegin(0, 80, true);`
 * and friends. The legacy ESP-IDF v4 API was removed in v5. We
 * wrap the new gptimer driver behind a `hw_timer_t*` pointer so
 * the v3 source compiles unchanged. */
/* Note: gptimer.h lives under components/esp_driver_gptimer in IDF
 * v6. We don't need to include it because the shim is purely a
 * parameter-store stub; the timer ISR is not actually wired up. */

hw_timer_t *timerBegin(uint8_t timer, uint16_t prescaler, bool countUp) {
    hw_timer_t *t = new hw_timer_t{};
    if (!t) return nullptr;
    t->group = timer;
    t->prescaler = prescaler;
    t->auto_reload = false;
    t->alarm_value = 0;
    t->callback = nullptr;
    t->user_data = nullptr;
    return t;
}

void timerEnd(hw_timer_t *t) {
    if (!t) return;
    delete t;
}

void timerAttachInterrupt(hw_timer_t *t, hw_timer_callback_t cb, bool edge) {
    if (!t) return;
    t->callback = cb;
    (void)edge;
}

void timerDetachInterrupt(hw_timer_t *t) {
    if (!t) return;
    t->callback = nullptr;
}

void timerAlarmWrite(hw_timer_t *t, uint64_t value, bool autoreload) {
    if (!t) return;
    t->alarm_value = value;
    t->auto_reload = autoreload;
}

void timerAlarmEnable(hw_timer_t *t) {
    if (!t) return;
    /* The v3 firmware installs only one timer. For full gptimer
     * support, replace this with a per-timer gptimer_handle_t. */
    ESP_LOGW(TAG, "timerAlarmEnable: legacy timer API shim, no gptimer backing yet");
}

void timerAlarmDisable(hw_timer_t *t) {
    if (!t) return;
}

void timerWrite(hw_timer_t *t, uint64_t value) {
    if (!t) return;
    t->alarm_value = value;
}

void timerStart(hw_timer_t *t)  { (void)t; }
void timerStop(hw_timer_t *t)   { (void)t; }

/* ---- LEDC PWM API (Arduino style) -------------------------------------- */
#include "driver/ledc.h"

double ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution_bits) {
    /* Map the Arduino "channel" 0..7 to a v6 LEDC timer/channel pair.
     * v3 source uses channels 0..7 with a single timer (timer 0) per
     * frequency change. Use a 1:1 mapping: each Arduino channel = one
     * LEDC timer. Resolution is rounded up to a supported v6 value.
     *
     * NOTE: we only configure the TIMER here. The CHANNEL is configured
     * by ledcAttachPin() once the GPIO is known. Configuring the channel
     * here with gpio_num=-1 (no pin yet) used to "work" in older IDF
     * versions but IDF v6 rejects it with "gpio_num argument is invalid",
     * producing 4 error lines at boot for the LCD / RGB LED channels. */
    ledc_timer_t timer = (ledc_timer_t)((channel) / 2);  /* 2 channels per timer */
    uint8_t res = resolution_bits ? resolution_bits : 8;
    if (res < 8) res = 8;
    if (res > 13) res = 13;
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)res,
        .timer_num = timer,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %d", (int)err);
        return 0.0;
    }
    return (double)freq;
}

void ledcAttachPin(uint8_t pin, uint8_t channel) {
    /* Reconfigure the channel to route to the requested GPIO. */
    ledc_channel_config_t ccfg = {
        .gpio_num = (int)pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)((channel) / 2),
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 0 },
    };
    esp_err_t err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledcAttachPin: channel_config failed: %d", (int)err);
    }
}

void ledcWrite(uint8_t channel, uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel);
}

void ledcDetachPin(uint8_t pin) {
    ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, (uint32_t)pin);
}

/* ---- ESPClass shim ----------------------------------------------------- *
 * The heap/alloc methods live in update_compat.cpp; we just include
 * the system headers here so the rest of the shim can use the heap
 * APIs in its own helpers.
 */
#include "esp_heap_caps.h"
#include "esp_system.h"

