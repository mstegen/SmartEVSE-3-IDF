/*
 * wifi_compat.c — Arduino WiFi shim implementation
 *
 * Wraps esp_wifi / esp_netif / esp_event in a small facade so the existing
 * SmartEVSE-3 v3 source code (which calls WiFi.status(), WiFi.begin(),
 * WiFi.softAP(), ...) compiles and runs against native ESP-IDF v6.0.1.
 *
 * This is intentionally not a full Arduino WiFi emulation. It only
 * implements the methods called by SmartEVSE-3 v3.
 */
#include "wifi_compat.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_compat";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;
static int s_retry_num = 0;
static const int s_max_retry = 5;
static WiFiEventCb_t s_user_cb = NULL;
static bool s_connected = false;
static char s_connected_ssid[33] = {0};
static char s_connected_bssid[18] = {0};
static int  s_connected_rssi = 0;

#ifdef __cplusplus
WiFiClass WiFi;
#endif

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry to connect to the AP (%d/%d)", s_retry_num, s_max_retry);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        s_connected = false;
        if (s_user_cb) s_user_cb(WIFI_EVENT_STA_DISCONNECTED);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_user_cb) s_user_cb(IP_EVENT_STA_GOT_IP);
    }
}

void WiFiClass_init(void) {
    if (s_wifi_event_group) return;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
}

wl_status_t WiFi_status(void) {
    if (!s_wifi_event_group) return WL_IDLE_STATUS;
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) return WL_CONNECTED;
    if (bits & WIFI_FAIL_BIT)      return WL_CONNECT_FAILED;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return WL_CONNECTED;
    return WL_IDLE_STATUS;
}

bool WiFi_isConnected(void) { return s_connected; }
bool WiFi_isUp(void) { return s_wifi_event_group != NULL; }

int WiFi_RSSI(void) {
    if (!s_connected) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_connected_rssi = ap.rssi;
    }
    return s_connected_rssi;
}

String WiFi_SSID(void) {
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0]) {
        snprintf(s_connected_ssid, sizeof(s_connected_ssid), "%s", (char*)cfg.sta.ssid);
    }
    return String(s_connected_ssid);
}

String WiFi_BSSIDstr(void) {
    if (!s_connected) return String("");
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(s_connected_bssid, sizeof(s_connected_bssid),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        return String(s_connected_bssid);
    }
    return String("");
}

String WiFi_macAddress(void) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

IPAddress WiFi_localIP(void) {
    if (!s_sta_netif) return IPAddress(0,0,0,0);
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK) {
        return IPAddress(info.ip.addr & 0xff,
                         (info.ip.addr >> 8) & 0xff,
                         (info.ip.addr >> 16) & 0xff,
                         (info.ip.addr >> 24) & 0xff);
    }
    return IPAddress(0,0,0,0);
}

IPAddress WiFi_dnsIP(void) {
    if (!s_sta_netif) return IPAddress(0,0,0,0);
    esp_netif_dns_info_t info;
    if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &info) == ESP_OK) {
        return IPAddress(info.ip.u_addr.ip4.addr & 0xff,
                         (info.ip.u_addr.ip4.addr >> 8) & 0xff,
                         (info.ip.u_addr.ip4.addr >> 16) & 0xff,
                         (info.ip.u_addr.ip4.addr >> 24) & 0xff);
    }
    return IPAddress(0,0,0,0);
}

IPAddress WiFi_softAPIP(void) {
    if (!s_ap_netif) return IPAddress(0,0,0,0);
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        return IPAddress(info.ip.addr & 0xff,
                         (info.ip.addr >> 8) & 0xff,
                         (info.ip.addr >> 16) & 0xff,
                         (info.ip.addr >> 24) & 0xff);
    }
    return IPAddress(192,168,4,1);
}

int WiFi_getMode(void) {
    wifi_mode_t m;
    if (esp_wifi_get_mode(&m) == ESP_OK) {
        switch (m) {
            case WIFI_MODE_STA:    return WIFI_STA;
            case WIFI_MODE_AP:     return WIFI_AP;
            case WIFI_MODE_APSTA:  return WIFI_AP_STA;
            default:               return WIFI_OFF;
        }
    }
    return WIFI_OFF;
}

void WiFi_begin(const char *ssid, const char *pass) {
    if (!s_wifi_event_group) WiFiClass_init();

    /* Persist creds to NVS so the v3-style `WiFi.begin()` (no args) on
     * subsequent boots picks them up. */
    if (ssid) {
        nvs_handle_t h;
        if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "ssid", ssid);
            if (pass) nvs_set_str(h, "pswd", pass);
            nvs_commit(h);
            nvs_close(h);
        }
    }

    /* Switch to STA, populate config, connect. */
    wifi_mode_t cur;
    esp_wifi_get_mode(&cur);
    if (cur != WIFI_MODE_STA && cur != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
    wifi_config_t cfg = {0};
    if (ssid) {
        strncpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
        if (pass) strncpy((char*)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    } else {
        /* Load from NVS */
        nvs_handle_t h;
        if (nvs_open("nvs.net80211", NVS_READONLY, &h) == ESP_OK) {
            size_t s = sizeof(cfg.sta.ssid);
            nvs_get_str(h, "ssid", (char*)cfg.sta.ssid, &s);
            s = sizeof(cfg.sta.password);
            nvs_get_str(h, "pswd", (char*)cfg.sta.password, &s);
            nvs_close(h);
        }
    }
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    s_retry_num = 0;
    if (cur == WIFI_MODE_NULL) esp_wifi_start();
    esp_wifi_connect();
}

void WiFi_mode(int m) {
    if (!s_wifi_event_group) WiFiClass_init();
    switch (m) {
        case WIFI_OFF:    esp_wifi_set_mode(WIFI_MODE_NULL);   break;
        case WIFI_STA:    esp_wifi_set_mode(WIFI_MODE_STA);    break;
        case WIFI_AP:     esp_wifi_set_mode(WIFI_MODE_AP);     break;
        case WIFI_AP_STA: esp_wifi_set_mode(WIFI_MODE_APSTA);  break;
    }
    esp_wifi_start();
}

void WiFi_disconnect(bool wipe) {
    if (wipe) {
        nvs_handle_t h;
        if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    esp_wifi_disconnect();
    s_connected = false;
}

void WiFi_reconnect(void) {
    esp_wifi_connect();
}

void WiFi_softAP(const char *ssid, const char *pass) {
    if (!s_wifi_event_group) WiFiClass_init();
    wifi_config_t ap = {0};
    strncpy((char*)ap.ap.ssid, ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen(ssid);
    if (pass && strlen(pass) >= 8) {
        strncpy((char*)ap.ap.password, pass, sizeof(ap.ap.password) - 1);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    ap.ap.max_connection = 4;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
}

void WiFi_softAPdisconnect(bool b) {
    (void)b;
    wifi_config_t ap = {0};
    ap.ap.ssid[0] = '\0';
    esp_wifi_set_config(WIFI_IF_AP, &ap);
}

void WiFi_setHostname(const char *name) {
    if (s_sta_netif && name) esp_netif_set_hostname(s_sta_netif, name);
}

void WiFi_setAutoReconnect(bool b) {
    (void)b;  /* esp_wifi handles reconnection internally */
}

void WiFi_onEvent(WiFiEventCb_t cb) {
    s_user_cb = cb;
}
