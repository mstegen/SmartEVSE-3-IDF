/*
 * http_compat.h — Arduino HTTPClient + WiFiClient shim
 *
 * Backed by native ESP-IDF esp_http_client.h. Implements just enough
 * of the Arduino API surface for the SmartEVSE-3 v3 source code:
 *   HTTPClient httpClient;
 *   httpClient.setFollowRedirects(mode);
 *   httpClient.setTimeout(ms);
 *   httpClient.setReuse(b);
 *   httpClient.begin(url[, ca_cert]);
 *   httpClient.addHeader(k, v);
 *   httpClient.collectHeaders(arr, n);
 *   httpClient.GET();
 *   httpClient.end();
 *   httpClient.getStream()        -> Stream*
 *   httpClient.getStreamPtr()     -> WiFiClient*
 *   httpClient.getSize()          -> int
 *   httpClient.header(k)          -> String
 *   httpClient.connected()        -> bool
 */
#ifndef HTTP_COMPAT_H
#define HTTP_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include "esp_http_client.h"
#include "arduino_compat_base.h"   /* for String, SerialHandle */

/* HTTP status codes the v3 source checks for */
#define HTTP_CODE_OK              200
#define HTTP_CODE_MOVED_PERMANENTLY 301
#define HTTP_CODE_FOUND            302
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
#define HTTPC_NO_FOLLOW_REDIRECTS     0

/* All of the classes below are C++; no extern "C" wrapper. */

/* Stream is the Arduino base class for both WiFiClient and HTTPClient::getStream().
 * The v3 code uses these as bare pointers. We provide the minimal interface
 * (read, readBytesUntil, readStringUntil, available, connected) plus the
 * peek()/write()/flush() defaults so this same Stream type also satisfies
 * eModbus's RTU API and ArduinoJson's `deserializeJson(<Stream*>)` call.
 * peek() and write() have safe no-op defaults so derived classes that don't
 * implement them (e.g. WiFiClient) stay concrete. */
class Stream {
public:
    virtual ~Stream() {}
    virtual int  available() = 0;
    virtual int  read() = 0;
    virtual int  peek() { return -1; }                          // default: no lookahead
    virtual void flush() {}                                     // default: no-op
    virtual size_t write(uint8_t) { return 0; }                 // default: no-op
    virtual size_t write(const uint8_t *buf, size_t len) {     // default: byte-loop
        size_t n = 0;
        while (len--) n += write(*buf++);
        return n;
    }
    // Arduino's Print also accepts const char * and char for the single-arg
    // write. Route them through the byte-buffer overload so callers don't
    // have to cast at every call site.
    virtual size_t write(const char *s) {
        if (!s) return 0;
        return write((const uint8_t *)s, strlen(s));
    }
    virtual size_t write(char c) { return write((uint8_t)c); }
    virtual int  readBytes(char *buf, int len) = 0;
    virtual int  readBytesUntil(char term, char *buf, int len) = 0;
    virtual String readStringUntil(char term) = 0;
    virtual bool  connected() = 0;
};

class WiFiClient : public Stream {
public:
    WiFiClient();
    ~WiFiClient() override;
    int  available() override;
    int  read() override;
    int  readBytes(char *buf, int len) override;
    int  readBytesUntil(char term, char *buf, int len) override;
    String readStringUntil(char term) override;
    bool  connected() override;
    /* Internal: bind to an esp_http_client handle (used by HTTPClient). */
    void _bind(esp_http_client_handle_t h) { h_ = h; }
private:
    esp_http_client_handle_t h_ = nullptr;
};

class HTTPClient {
public:
    HTTPClient();
    ~HTTPClient();
    void setFollowRedirects(int mode);
    void setTimeout(int ms);
    void setReuse(bool b);
    bool begin(const char *url, const char *ca_cert = nullptr);
    void addHeader(const char *k, const char *v);
    void collectHeaders(const char * const *arr, int n);
    int  GET();
    void end();
    Stream &getStream();
    WiFiClient *getStreamPtr();
    int   getSize();
    String header(const char *k);
    bool  connected();

private:
    esp_http_client_handle_t h_ = nullptr;
    Stream *stream_ = nullptr;
    WiFiClient *client_ = nullptr;
    int code_ = 0;
    int size_ = 0;
    char **saved_headers_ = nullptr;
    int  saved_headers_n_ = 0;
};

#endif
