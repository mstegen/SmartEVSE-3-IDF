/*
 * http_compat.c — HTTPClient + WiFiClient shim implementation
 * (See http_compat.h for the surface.)
 */
#include "http_compat.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "http_compat";

/* ---- WiFiClient (Stream) ----------------------------------------------- */

WiFiClient::WiFiClient() {}
WiFiClient::~WiFiClient() {}

int WiFiClient::available() {
    if (!h_) return 0;
    return esp_http_client_is_complete_data_received(h_) ? 0 : 1;
}

int WiFiClient::read() {
    char c;
    int n = readBytes(&c, 1);
    return n == 1 ? (uint8_t)c : -1;
}

int WiFiClient::readBytes(char *buf, int len) {
    if (!h_) return 0;
    int total = 0;
    while (total < len) {
        int r = esp_http_client_read(h_, buf + total, len - total);
        if (r <= 0) break;
        total += r;
    }
    return total;
}

int WiFiClient::readBytesUntil(char term, char *buf, int len) {
    int i = 0;
    while (i < len) {
        int c = read();
        if (c < 0) { vTaskDelay(1); continue; }
        if (c == term) return i;
        buf[i++] = (char)c;
    }
    return i;
}

String WiFiClient::readStringUntil(char term) {
    String out;
    while (true) {
        int c = read();
        if (c < 0) {
            vTaskDelay(1);
            if (!connected()) break;
            continue;
        }
        if (c == term) break;
        out += (char)c;
    }
    return out;
}

bool WiFiClient::connected() {
    return h_ != nullptr;  /* best-effort: esp_http_client has no connected() */
}

/* ---- HTTPClient --------------------------------------------------------- */

HTTPClient::HTTPClient() {}
HTTPClient::~HTTPClient() { end(); }

void HTTPClient::setFollowRedirects(int mode) {
    (void)mode;  /* esp_http_client follows by default when set_url is called */
}
void HTTPClient::setTimeout(int ms) {
    if (h_) esp_http_client_set_timeout_ms(h_, ms);
}
void HTTPClient::setReuse(bool b) {
    (void)b;  /* esp_http_client reuses connections by default */
}

bool HTTPClient::begin(const char *url, const char *ca_cert) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 10000;
    if (ca_cert) {
        /* Set the CA cert on the global SSL context. The real esp_http_client
         * supports a per-request `cert_pem` only when using global CA bundle
         * with a per-request override. We use the simpler "skip verification"
         * path for HTTPS here to keep the surface narrow. */
        cfg.cert_pem = ca_cert;
    }
    h_ = esp_http_client_init(&cfg);
    if (!h_) return false;
    if (!client_) {
        client_ = new WiFiClient();
    }
    client_->_bind(h_);
    return true;
}

void HTTPClient::addHeader(const char *k, const char *v) {
    if (h_ && k && v) esp_http_client_set_header(h_, k, v);
}

void HTTPClient::collectHeaders(const char * const *arr, int n) {
    /* esp_http_client supports per-request header retrieval but no global
     * collector. We just remember the keys; header() does a one-off fetch. */
    if (saved_headers_) {
        for (int i = 0; i < saved_headers_n_; i++) free(saved_headers_[i]);
        free(saved_headers_);
    }
    saved_headers_ = (char**)calloc(n, sizeof(char*));
    for (int i = 0; i < n; i++) saved_headers_[i] = strdup(arr[i]);
    saved_headers_n_ = n;
}

int HTTPClient::GET() {
    if (!h_) return -1;
    esp_err_t err = esp_http_client_open(h_, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(err));
        return -1;
    }
    esp_http_client_fetch_headers(h_);
    code_ = esp_http_client_get_status_code(h_);
    size_ = esp_http_client_get_content_length(h_);
    return code_;
}

void HTTPClient::end() {
    if (h_) {
        esp_http_client_cleanup(h_);
        h_ = nullptr;
    }
    if (client_) {
        delete client_;
        client_ = nullptr;
    }
    if (saved_headers_) {
        for (int i = 0; i < saved_headers_n_; i++) free(saved_headers_[i]);
        free(saved_headers_);
        saved_headers_ = nullptr;
        saved_headers_n_ = 0;
    }
    code_ = size_ = 0;
}

Stream &HTTPClient::getStream()    { return *client_; }
WiFiClient *HTTPClient::getStreamPtr() { return client_; }
int HTTPClient::getSize()           { return size_; }

String HTTPClient::header(const char *k) {
    if (!h_ || !k) return String("");
    char *value = nullptr;
    if (esp_http_client_get_header(h_, k, &value) != ESP_OK || !value) {
        if (value) free(value);
        return String("");
    }
    String out(value);
    free(value);
    return out;
}

bool HTTPClient::connected() { return client_ != nullptr && h_ != nullptr; }
