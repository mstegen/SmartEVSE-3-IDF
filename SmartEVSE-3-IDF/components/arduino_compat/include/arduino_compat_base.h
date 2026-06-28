/*
 * arduino_compat_base.h — the original core shim (time, GPIO, Serial, Prefs).
 * This is the "minimal Arduino" surface; the broader API (WiFi, MDNS, HTTP,
 * Update, SPI, FS) is provided by separate headers and rolled up by
 * arduino_compat.h.
 */
#ifndef ARDUINO_COMPAT_BASE_H
#define ARDUINO_COMPAT_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <algorithm>         /* for std::min / std::max */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"              /* for nvs_handle_t in Preferences */
#include "driver/gpio.h"      /* for gpio_num_t in pinMode/digitalWrite */
#include "esp_timer.h"        /* for esp_timer_get_time() */
#include "esp_random.h"       /* for esp_random() */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Time ----------------------------------------------------------------- */
static inline uint32_t millis(void)            { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline uint32_t micros(void)            { return (uint32_t)(esp_timer_get_time()); }
static inline void     delay(uint32_t ms)      { vTaskDelay(pdMS_TO_TICKS(ms)); }
static inline void     delayMicroseconds(uint32_t us) {
    uint64_t end = esp_timer_get_time() + us;
    while (esp_timer_get_time() < end) {}
}

/* ---- Stub Serial (UART0 debug sink) ------------------------------------- *
 * The v3 source uses `Serial` for diagnostic output (e.g.
 * `Update.printError(Serial)`). The shim provides a tiny Print-compatible
 * sink that always succeeds. The actual UART0 logging in production uses
 * ESP-IDF's esp_log; this sink is just enough to keep Arduino-style
 * references compiling.
 *
 * We define the Print base class here (instead of in the eModbus shim)
 * so that both the eModbus shim's `Print` is the same type and the
 * arduino_compat's `_SerialStubPrint` derives from it. The
 * eModbus-side Stream.h only forward-declares the same Print class.
 */
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buf, size_t len) {
        size_t n = 0;
        for (size_t i = 0; i < len; i++) n += write(buf[i]);
        return n;
    }
    virtual int availableForWrite() { return 64; }

    /* Arduino-style print/println/printf overloads used by both
     * eModbus's Logging.cpp and the v3 source's serial diagnostics.
     * These delegate to write(). */
    size_t print(const char *s) { if (!s) return 0; return write((const uint8_t *)s, strlen(s)); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { char b[20]; snprintf(b, sizeof(b), "%d", v); return print(b); }
    size_t print(unsigned int v) { char b[20]; snprintf(b, sizeof(b), "%u", v); return print(b); }
    size_t print(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); return print(b); }
    size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); return print(b); }
    size_t print(double v, int prec = 2) { char b[32]; snprintf(b, sizeof(b), "%.*f", prec, v); return print(b); }

    size_t println() { return write((const uint8_t *)"\r\n", 2); }
    size_t println(const char *s) { size_t n = print(s); n += println(); return n; }
    size_t println(int v) { size_t n = print(v); n += println(); return n; }
    size_t println(unsigned int v) { size_t n = print(v); n += println(); return n; }
    size_t println(long v) { size_t n = print(v); n += println(); return n; }
    size_t println(unsigned long v) { size_t n = print(v); n += println(); return n; }
    size_t println(double v, int prec = 2) { size_t n = print(v, prec); n += println(); return n; }

    int printf(const char *fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n > 0) {
            if ((size_t)n > sizeof(buf)) n = sizeof(buf);
            write((const uint8_t *)buf, (size_t)n);
        }
        return n;
    }
};
/* Default `Serial` (UART0). On ESP-IDF stdout is routed to the UART0
 * console (CONFIG_ESP_CONSOLE_UART_NUM=0), which is the USB serial port
 * the ESP-IDF monitor reads. Writing here therefore reaches that port. */
class _SerialStubPrint : public Print {
public:
    void   begin(uint32_t /*baud*/) {}
    size_t write(uint8_t c) override { putchar(c); return 1; }
    size_t write(const uint8_t *buf, size_t len) override { return fwrite(buf, 1, len, stdout); }
    size_t printf(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        return n < 0 ? 0 : (size_t)n;
    }
    operator bool() const { return true; }
    bool connected() { return true; }
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() { fflush(stdout); }
};
extern _SerialStubPrint Serial;

/* ---- GPIO ----------------------------------------------------------------- */
#define LOW          0x0
#define HIGH         0x1
#define INPUT        0x01
#define OUTPUT       0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x07
#define RISING       0x01
#define FALLING      0x02
#define CHANGE       0x03

void pinMode     (uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead (uint8_t pin);
int  analogRead  (uint8_t pin);

/* Arduino-style ISR attach: a thin wrapper over gpio_isr_handler_add.
 * The v3 source uses attachInterrupt(pin, isr, mode) for the CP pulse
 * detector. The shim installs a GPIO ISR service if not already done
 * and routes the matching pin to the user function. */
typedef void (*arduino_isr_t)(void);
void attachInterrupt(uint8_t pin, arduino_isr_t isr, int mode);

/* UART config bits used by the v3 source: 8N1 = 8 data bits, no
 * parity, 1 stop bit. */
#define SERIAL_8N1  0x800001C
#define SERIAL_8E1  0x800003C
#define SERIAL_8O1  0x800005C
#define SERIAL_8N2  0x800001E

/* ---- LEDC PWM (Arduino style) ------------------------------------------- *
 * The Arduino API exposes ledcSetup(channel, freq, res_bits) and
 * ledcAttachPin(pin, channel) and ledcWrite(channel, duty). The v6
 * LEDC driver splits timer and channel configuration, so the shim
 * presents the legacy single-call API and creates the underlying
 * timer/channel pair on demand.
 */
double ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution_bits);
void    ledcAttachPin(uint8_t pin, uint8_t channel);
void    ledcWrite(uint8_t channel, uint32_t duty);
void    ledcWriteTone(uint8_t channel, uint32_t freq);
void    ledcDetachPin(uint8_t pin);

/* ---- Serial (HardwareSerial-style) --------------------------------------- */
typedef struct {
    int      port;        /* UART_NUM_0/1/2 */
    int      tx_pin;
    int      rx_pin;
    int      installed;
    uint32_t baud;        /* last value passed to Serial_begin */
} SerialHandle;

void Serial_begin           (SerialHandle *h, uint32_t baud);
int  Serial_available       (SerialHandle *h);
int  Serial_read            (SerialHandle *h);
int  Serial_peek            (SerialHandle *h);
int  Serial_readBytesUntil  (SerialHandle *h, char term, char *buf, int len);
void Serial_flush           (SerialHandle *h);
void Serial_print           (SerialHandle *h, const char *s);
void Serial_println         (SerialHandle *h, const char *s);
void Serial_printf          (SerialHandle *h, const char *fmt, ...)
                             __attribute__((format(printf, 2, 3)));

/* Pre-initialised handles for Serial1 (RS485) and Serial2 (CH32 link).
 * The handle structs are private to the shim; clients obtain them via
 * the Serial{1,2}_get() accessors. The eModbus component wraps each
 * handle in a C++ HardwareSerial class (its own `Serial1` global).
 */
SerialHandle *Serial1_get(void);
SerialHandle *Serial2_get(void);
size_t Serial_write_byte(SerialHandle *h, uint8_t c);
size_t Serial_write_buf (SerialHandle *h, const uint8_t *buf, size_t len);

/* ---- Preferences (NVS-backed) -------------------------------------------- */
/* In C, the storage is a plain struct. In C++, the storage is the
 * Preferences class (defined further down) which has the same
 * memory layout (single inheritance with no extra fields) so the
 * C-style accessors can take a pointer to either and reach the
 * same `handle`/`readonly`/`namespace_name` fields. */
#ifdef __cplusplus
/* In C++ the class is declared further down. Forward-declare it
 * here so the C-style accessors below can take a `Preferences*`. */
class Preferences;
#else
struct Preferences_t {
    nvs_handle_t handle;
    int          readonly;
    char         namespace_name[16];
};
typedef struct Preferences_t Preferences;
#endif

int   Preferences_begin    (Preferences *p, const char *name, int readonly);
void  Preferences_end      (Preferences *p);
void  Preferences_clear    (Preferences *p);
void  Preferences_remove   (Preferences *p, const char *k);
bool  Preferences_isKey    (Preferences *p, const char *k);

uint8_t  Preferences_getUChar (Preferences *p, const char *k, uint8_t  def);
uint16_t Preferences_getUShort(Preferences *p, const char *k, uint16_t def);
uint32_t Preferences_getULong (Preferences *p, const char *k, uint32_t def);
int8_t   Preferences_getChar  (Preferences *p, const char *k, int8_t   def);
int16_t  Preferences_getShort (Preferences *p, const char *k, int16_t  def);
int32_t  Preferences_getLong  (Preferences *p, const char *k, int32_t  def);
size_t   Preferences_getStringLength(Preferences *p, const char *k);
size_t   Preferences_getString(Preferences *p, const char *k, char *out, size_t maxlen);
size_t   Preferences_getBytes (Preferences *p, const char *k, void *out, size_t maxlen);

size_t   Preferences_putUChar (Preferences *p, const char *k, uint8_t  v);
size_t   Preferences_putUShort(Preferences *p, const char *k, uint16_t v);
size_t   Preferences_putULong (Preferences *p, const char *k, uint32_t v);
size_t   Preferences_putChar  (Preferences *p, const char *k, int8_t   v);
size_t   Preferences_putShort (Preferences *p, const char *k, int16_t  v);
size_t   Preferences_putLong  (Preferences *p, const char *k, int32_t  v);
size_t   Preferences_putString(Preferences *p, const char *k, const char *v);
size_t   Preferences_putBytes (Preferences *p, const char *k, const void *v, size_t len);
size_t   Preferences_putBool  (Preferences *p, const char *k, bool v);
bool     Preferences_getBool  (Preferences *p, const char *k, bool defv);

/* nvs_flash_init wrapper — call once at startup. */
void nvs_init_once(void);

#ifdef __cplusplus
}

/* ---- Arduino String replacement (very thin std::string shim) -------------- */
#ifdef __cplusplus
/* Bring std::min / std::max into the global namespace so the v3
 * source's `min(a, b)` and `max(a, b)` calls resolve the same way
 * they do under Arduino. We use `using` declarations (not `using
 * namespace std;`) so the rest of the std:: names stay qualified. */
using std::min;
using std::max;
#endif
class String : public std::string {
public:
    String() : std::string() {}
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(unsigned long v) : std::string(std::to_string(v)) {}
    String(float v) : std::string(std::to_string(v)) {}
    String(double v) : std::string(std::to_string(v)) {}

    const char *c_str() const { return std::string::c_str(); }
    unsigned int length() const { return (unsigned int)std::string::length(); }
    int          toInt()  const { return std::stoi(*this); }
    float        toFloat()const { return std::stof(*this); }
    bool         isEmpty() const { return std::string::empty(); }
    /* Boolean / ! operators — Arduino's String is "truthy" if non-empty. */
    operator bool() const { return !std::string::empty(); }
    bool operator!() const { return std::string::empty(); }

    String &operator=(const char *s) { std::string::operator=(s ? s : ""); return *this; }
    bool    operator==(const char *s) const { return compare(s ? s : "") == 0; }
    bool    operator!=(const char *s) const { return !(*this == s); }
    String  substring(unsigned int beginIndex, unsigned int endIndex = (unsigned int)-1) const {
        if (beginIndex >= size()) return String();
        if (endIndex > size()) endIndex = (unsigned int)size();
        return String(substr(beginIndex, endIndex - beginIndex));
    }
    int     indexOf(const char *s) const { auto p = find(s ? s : ""); return p == std::string::npos ? -1 : (int)p; }
    int     indexOf(const String &s) const { return indexOf(s.c_str()); }
    bool    startsWith(const char *prefix) const { return prefix && rfind(prefix, 0) == 0; }
    bool    startsWith(const String &prefix) const { return startsWith(prefix.c_str()); }
    bool    startsWith(char c) const { return !empty() && (*this)[0] == c; }
    bool    endsWith(const char *suffix) const {
        if (!suffix) return false;
        size_t sl = std::char_traits<char>::length(suffix);
        return sl <= size() && compare(size() - sl, sl, suffix) == 0;
    }
    String &trim() {
        // Left trim
        size_t start = 0;
        while (start < size() && isspace((unsigned char)(*this)[start])) ++start;
        if (start > 0) erase(0, start);
        // Right trim
        while (!empty() && isspace((unsigned char)back())) pop_back();
        return *this;
    }
    /* Arduino's Print interface used by ArduinoJson's Writer. We
     * implement a `write(const char *s, size_t n)` that just appends
     * to the underlying std::string. */
    size_t write(const char *s, size_t n) {
        if (!s || n == 0) return 0;
        append(s, n);
        return n;
    }
    size_t write(const uint8_t *s, size_t n) {
        return write(reinterpret_cast<const char *>(s), n);
    }
    size_t write(uint8_t c) { push_back((char)c); return 1; }
};

/* ---- Arduino Preferences class (C++ method-style API) ------------------ */
/* The Arduino Preferences library is used as `Preferences preferences;`
 * followed by `preferences.begin("name", false)` and
 * `preferences.putUChar/getUChar/putBytes/getBytes/end()`. The v3
 * source code uses this style. The C++ class has the same memory
 * layout as the C struct (handle + readonly + namespace_name
 * [16]) so the C-style accessors can take its address and reach
 * those fields directly. */
class Preferences {
public:
    nvs_handle_t handle;
    int          readonly;
    char         namespace_name[16];

    Preferences() : handle(0), readonly(0), namespace_name{} {}
    bool begin(const char *name, bool readOnly = false) {
        return Preferences_begin(this, name, readOnly ? 1 : 0) != 0;
    }
    void end()                 { Preferences_end(this); }
    void clear()               { Preferences_clear(this); }
    void remove(const char *k) { Preferences_remove(this, k); }
    bool isKey(const char *k)  { return Preferences_isKey(this, k); }

    uint8_t  getUChar (const char *k, uint8_t  def = 0) { return Preferences_getUChar (this, k, def); }
    uint16_t getUShort(const char *k, uint16_t def = 0) { return Preferences_getUShort(this, k, def); }
    uint32_t getULong (const char *k, uint32_t def = 0) { return Preferences_getULong (this, k, def); }
    int8_t   getChar  (const char *k, int8_t   def = 0) { return Preferences_getChar  (this, k, def); }
    int16_t  getShort (const char *k, int16_t  def = 0) { return Preferences_getShort (this, k, def); }
    int32_t  getLong  (const char *k, int32_t  def = 0) { return Preferences_getLong  (this, k, def); }
    bool     getBool  (const char *k, bool     def = false) { return Preferences_getBool(this, k, def); }
    size_t   getBytes (const char *k, void *out, size_t maxlen) { return Preferences_getBytes(this, k, out, maxlen); }
    size_t   getString(const char *k, char *out, size_t maxlen) { return Preferences_getString(this, k, out, maxlen); }
    String   getString(const char *k, const char *def = "") {
        size_t len = Preferences_getStringLength(this, k);
        if (len == 0) return String(def ? def : "");    /* key missing */
        std::string buf;
        buf.resize(len);                                /* len includes null terminator */
        size_t n = Preferences_getString(this, k, &buf[0], len);
        buf.resize(n);                                  /* n = chars without null */
        return String(buf);
    }
    String   getString(const char *k, const String &def) { return getString(k, def.c_str()); }
    uint32_t getUInt  (const char *k, uint32_t def = 0) { return Preferences_getULong(this, k, def); }

    size_t putUChar (const char *k, uint8_t  v) { return Preferences_putUChar (this, k, v); }
    size_t putUShort(const char *k, uint16_t v) { return Preferences_putUShort(this, k, v); }
    size_t putULong (const char *k, uint32_t v) { return Preferences_putULong (this, k, v); }
    size_t putChar  (const char *k, int8_t   v) { return Preferences_putChar  (this, k, v); }
    size_t putShort (const char *k, int16_t  v) { return Preferences_putShort (this, k, v); }
    size_t putLong  (const char *k, int32_t  v) { return Preferences_putLong  (this, k, v); }
    size_t putBool  (const char *k, bool     v) { return Preferences_putBool  (this, k, v); }
    size_t putBytes (const char *k, const void *v, size_t len) { return Preferences_putBytes(this, k, v, len); }
    size_t putString(const char *k, const char *v) { return Preferences_putString(this, k, v); }
    size_t putString(const char *k, const String &v) { return Preferences_putString(this, k, v.c_str()); }
};

/* Single global Preferences object, matching the Arduino convention. */
extern Preferences preferences;

/* ---- Arduino timer / LEDC shims ---------------------------------------- */
/* The v3 source uses the Arduino `hw_timer_t` API:
 *   hw_timer_t *timerA = NULL;
 *   timerA = timerBegin(0, 80, true);
 *   timerAttachInterrupt(timerA, &onTimerA, true);
 *   timerAlarmWrite(timerA, 1000, true);
 *   timerAlarmEnable(timerA);
 *   timerWrite(timerA, 0);
 *   timerDetachInterrupt(timerA);
 *   timerEnd(timerA);
 *
 * In ESP-IDF v6 the timer API is the GPTimer subsystem
 * (`gptimer_handle_t`, `gptimer_new_timer`, etc.). The shim wraps
 * GPTimer behind the old hw_timer_t pointer so the v3 source
 * compiles unchanged. ISR dispatch is approximated using a
 * single shared gptimer alarm; the v3 source only ever installs
 * one timer, so this is sufficient.
 *
 * Similarly, the v3 source uses `ledcWrite(channel, duty)` and
 * `ledcWriteTone(channel, freq)` for PWM (LCD backlight, buzzer).
 * These map 1:1 onto the v6 LEDC driver; we forward directly. */
typedef void (*hw_timer_callback_t)(void);

typedef struct hw_timer_s {
    int                group;       /* legacy "timer number" */
    int                prescaler;   /* legacy prescaler */
    bool               auto_reload;
    uint64_t           alarm_value;
    hw_timer_callback_t callback;
    void              *user_data;
} hw_timer_t;

hw_timer_t *timerBegin   (uint8_t timer, uint16_t prescaler, bool countUp);
void        timerEnd     (hw_timer_t *t);
void        timerAttachInterrupt(hw_timer_t *t, hw_timer_callback_t cb, bool edge);
void        timerDetachInterrupt(hw_timer_t *t);
void        timerAlarmWrite(hw_timer_t *t, uint64_t value, bool autoreload);
void        timerAlarmEnable(hw_timer_t *t);
void        timerAlarmDisable(hw_timer_t *t);
void        timerWrite   (hw_timer_t *t, uint64_t value);
void        timerStart   (hw_timer_t *t);
void        timerStop    (hw_timer_t *t);

/* LEDC PWM API (Arduino style). The v3 source uses
 * `ledcWrite(channel, duty)`. The buzzer uses direct IDF LEDC API
 * (buzzer_init/buzzer_set_freq/buzzer_off in esp32.cpp). */
void ledcWrite     (uint8_t channel, uint32_t duty);
void ledcDetachPin (uint8_t pin);    /* nothing to do on IDF — included for compat */

/* Note: the existing ESPClass (in update_compat.h) is extended with
 * getFreeHeap / getHeapSize / getMinFreeHeap / getMaxAllocHeap
 * methods. Do NOT add another `ESPClassShim` here. */

#endif /* __cplusplus */

#ifdef __cplusplus
/* ---- Random (C++ only) --------------------------------------------------- */
/* Arduino's `random()` accepts (max) or (min, max) and returns a long.
 * Inlined here (not in the extern "C" block above) so it does not
 * conflict with the C standard library's `random(3)`. */
static inline long random(long max) {
    if (max <= 0) return 0;
    return (long)(esp_random() % (uint32_t)max);
}
static inline long random(long min, long max) {
    if (max <= min) return min;
    return min + (long)(esp_random() % (uint32_t)(max - min));
}
#endif

#endif /* ARDUINO_COMPAT_BASE_H */
