/*
 * wifi_compat.h — Arduino WiFi class shim over native ESP-IDF v6.0.1
 *
 * Provides the small subset of the Arduino `WiFi` global that SmartEVSE-3 v3
 * actually uses:
 *   WiFi.status()            -> wl_status_t
 *   WiFi.isConnected()       -> bool
 *   WiFi.localIP()           -> IPAddress
 *   WiFi.SSID()              -> String
 *   WiFi.RSSI()              -> int
 *   WiFi.BSSIDstr()          -> String
 *   WiFi.dnsIP()             -> IPAddress
 *   WiFi.begin(ssid, pass)   -> start STA connection
 *   WiFi.begin()             -> start STA with stored creds
 *   WiFi.mode(m)             -> esp_wifi_set_mode
 *   WiFi.disconnect(b)       -> disconnect
 *   WiFi.reconnect()         -> reconnect
 *   WiFi.softAP(ssid,pass)   -> AP mode
 *   WiFi.softAPdisconnect()  -> stop AP
 *   WiFi.softAPIP()          -> IPAddress (AP)
 *   WiFi.setHostname(s)      -> set DHCP hostname
 *   WiFi.setAutoReconnect(b) -> persistent reconnect
 *   WiFi.onEvent(cb)         -> register event handler
 *   WiFi.getMode()           -> wifi_mode_t
 *   WiFi.macAddress()        -> String
 *
 * The "configuration portal" (mode 2) and the WiFi event handler are
 * implemented on top of esp_netif + esp_event + esp_wifi. The config portal
 * is a NAT-less AP that serves a captive-form HTTP page on /.
 */
#ifndef WIFI_COMPAT_H
#define WIFI_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include "arduino_compat_base.h"   /* for String */
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi status enums (subset of Arduino wl_status_t) */
typedef enum {
    WL_NO_SHIELD       = 255,
    WL_IDLE_STATUS     = 0,
    WL_NO_SSID_AVAIL   = 1,
    WL_SCAN_COMPLETED  = 2,
    WL_CONNECTED       = 3,
    WL_CONNECT_FAILED  = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED    = 6
} wl_status_t;

typedef enum {
    WIFI_OFF    = 0,
    WIFI_STA    = 1,
    WIFI_AP     = 2,
    WIFI_AP_STA = 3
} wifi_mode_compat_t;

class IPAddress {
public:
    IPAddress() : a(0), b(0), c(0), d(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a(a), b(b), c(c), d(d) {}
    IPAddress(uint32_t v) {
        a = (v >> 24) & 0xff; b = (v >> 16) & 0xff; c = (v >> 8) & 0xff; d = v & 0xff;
    }
    uint8_t operator[](int i) const { return ((uint8_t*)&a)[i]; }
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a, b, c, d);
        return String(buf);
    }
    uint32_t toUint32() const { return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d; }
    bool operator==(const IPAddress &o) const { return a==o.a && b==o.b && c==o.c && d==o.d; }
    bool operator!=(const IPAddress &o) const { return !(*this == o); }
private:
    uint8_t a, b, c, d;
};

/* WiFi event handler signature (matches the WiFiEventCb_t shape Arduino uses) */
typedef void (*WiFiEventCb_t)(int event);

/* Arduino-style WiFi event enums. These mirror the esp_event ARP/WiFi
 * IDs the v3 source checks (e.g. ARDUINO_EVENT_WIFI_STA_GOT_IP,
 * ARDUINO_EVENT_SC_GOT_SSID_PSWD). The numeric values match the
 * esp_event_base_t / event_id range used by the IDF WiFi driver. */
typedef enum {
    ARDUINO_EVENT_WIFI_READY               = 0,
    ARDUINO_EVENT_WIFI_SCAN_DONE           = 1,
    ARDUINO_EVENT_WIFI_STA_START           = 2,
    ARDUINO_EVENT_WIFI_STA_STOP            = 3,
    ARDUINO_EVENT_WIFI_STA_CONNECTED       = 4,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED    = 5,
    ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE = 6,
    ARDUINO_EVENT_WIFI_STA_GOT_IP          = 7,
    ARDUINO_EVENT_WIFI_STA_LOST_IP         = 8,
    ARDUINO_EVENT_WIFI_AP_START            = 9,
    ARDUINO_EVENT_WIFI_AP_STOP             = 10,
    ARDUINO_EVENT_WIFI_AP_STACONNECTED     = 11,
    ARDUINO_EVENT_WIFI_AP_STADISCONNECTED  = 12,
    ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED    = 13,
    ARDUINO_EVENT_SC_SCAN_DONE             = 14,
    ARDUINO_EVENT_SC_FOUND_CHANNEL         = 15,
    ARDUINO_EVENT_SC_GOT_SSID_PSWD         = 16,
    ARDUINO_EVENT_SC_SEND_ACK_DONE         = 17,
    ARDUINO_EVENT_SC_END                   = 18,
} WiFiEvent_t;

/* Arduino-style WiFi event info struct. The v3 source only reads the
 * SmartConfig ssid/password fields, so we only model that subset. */
typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        bool    bssid_set;
        uint8_t bssid[6];
        uint8_t channel;
    } sc_got_ssid_pswd;
} WiFiEventInfo_t;

/* Backwards-compat: a 2-arg event callback that matches the legacy
 * Arduino `onEvent(WiFiEvent_t, WiFiEventInfo_t)` signature. */
typedef void (*WiFiEventCb2_t)(WiFiEvent_t event, WiFiEventInfo_t info);

/* Initialize the WiFi subsystem (netifs, event loop, default config). */
void WiFiClass_init(void);

/* Lifecycle */
wl_status_t WiFi_status(void);
bool        WiFi_isConnected(void);
int         WiFi_RSSI(void);
String      WiFi_SSID(void);
String      WiFi_BSSIDstr(void);
String      WiFi_macAddress(void);
IPAddress   WiFi_localIP(void);
IPAddress   WiFi_dnsIP(void);
IPAddress   WiFi_softAPIP(void);
int         WiFi_getMode(void);

/* Configuration / state changes */
void     WiFi_begin(const char *ssid, const char *pass);
void     WiFi_begin_static(const char *ssid, const char *pass, uint32_t ip);
void     WiFi_mode(int m);
void     WiFi_disconnect(bool wipe);
void     WiFi_reconnect(void);
void     WiFi_softAP(const char *ssid, const char *pass);
void     WiFi_softAPdisconnect(bool b);
void     WiFi_setHostname(const char *name);
void     WiFi_setAutoReconnect(bool b);
void     WiFi_onEvent(WiFiEventCb_t cb);

/* Returns true if WiFi subsystem is up. */
bool     WiFi_isUp(void);

#ifdef __cplusplus
}

/* Singleton-like facade: the source code uses `WiFi.foo()`. */
class WiFiClass {
public:
    void     init()              { WiFiClass_init(); }
    wl_status_t status()         { return WiFi_status(); }
    bool     isConnected()       { return WiFi_isConnected(); }
    int      RSSI()              { return WiFi_RSSI(); }
    String   SSID()              { return WiFi_SSID(); }
    String   BSSIDstr()          { return WiFi_BSSIDstr(); }
    String   macAddress()        { return WiFi_macAddress(); }
    IPAddress localIP()          { return WiFi_localIP(); }
    IPAddress dnsIP()            { return WiFi_dnsIP(); }
    IPAddress softAPIP()         { return WiFi_softAPIP(); }
    int      getMode()           { return WiFi_getMode(); }
    void     begin(const char *ssid, const char *pass) { WiFi_begin(ssid, pass); }
    void     begin()             { WiFi_begin(NULL, NULL); }
    void     mode(int m)         { WiFi_mode(m); }
    void     disconnect(bool b)  { WiFi_disconnect(b); }
    void     reconnect()         { WiFi_reconnect(); }
    void     softAP(const char *s, const char *p) { WiFi_softAP(s, p); }
    void     softAP(const String &s, const String &p) { WiFi_softAP(s.c_str(), p.c_str()); }
    void     softAPdisconnect(bool b) { WiFi_softAPdisconnect(b); }
    void     setHostname(const char *s) { WiFi_setHostname(s); }
    void     setAutoReconnect(bool b)   { WiFi_setAutoReconnect(b); }
    void     onEvent(WiFiEventCb_t cb)  { WiFi_onEvent(cb); }
    void     onEvent(WiFiEventCb2_t cb)  {
        /* Bridge the legacy 2-arg signature to the 1-arg esp_event
         * handler by wrapping it in a small lambda that constructs
         * an empty WiFiEventInfo_t. The shim stores a single handler
         * at a time so this is sufficient for the v3 code. */
        static WiFiEventCb2_t s_cb2 = cb;
        WiFi_onEvent([](int e){
            if (s_cb2) {
                WiFiEventInfo_t info{};
                s_cb2(static_cast<WiFiEvent_t>(e), info);
            }
        });
    }
};

extern WiFiClass WiFi;

#endif /* __cplusplus */

#endif /* WIFI_COMPAT_H */
