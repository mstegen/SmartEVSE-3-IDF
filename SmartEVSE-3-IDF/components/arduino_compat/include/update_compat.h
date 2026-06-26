/*
 * update_compat.h — Arduino Update class shim over esp_ota_ops.h
 *
 * Implements the subset of the Arduino Update class used by SmartEVSE-3 v3:
 *   Update.begin(size, U_FLASH)       -> bool
 *   Update.write(buf, len)            -> int
 *   Update.writeStream(Stream&)       -> int
 *   Update.end(setBoot)               -> bool
 *   Update.abort()
 *   Update.isFinished()               -> bool
 *   Update.hasError()                 -> bool
 *   Update.getError()                 -> int
 *   Update.errorString()              -> String
 *   Update.printError(Serial)
 *   Update.onProgress(cb)
 *   ESP.getFreeSketchSpace()          -> uint32_t
 *   ESP.restart()
 *   ESP.partitionRead / partitionEraseRange  (used by validate_sig())
 *   U_FLASH / U_SPIFFS / UPDATE_SIZE_UNKNOWN constants
 */
#ifndef UPDATE_COMPAT_H
#define UPDATE_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_mac.h"               /* esp_efuse_mac_get_default() in v6 */
#include "arduino_compat_base.h"   /* for String, SerialHandle */

#define U_FLASH 0
#define U_SPIFFS 1
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFFUL

class Stream;  /* forward decl, real type in http_compat.h */

typedef void (*UpdateProgressCb)(uint32_t progress, uint32_t size);

class UpdateClass {
public:
    bool begin(uint32_t size, int partition = U_FLASH);
    int  write(uint8_t *buf, size_t len);
    int  writeStream(Stream &s);
    bool end(bool setBoot = true);
    void abort();
    bool isFinished() const { return finished_; }
    bool hasError()   const { return err_ != 0; }
    int  getError()   const { return err_; }
    String errorString() const;
    void printError(SerialHandle *h) const;
    /* The v3 source uses Update.printError(Serial) where `Serial` is
     * the UART0 debug Print. Accept any Print-compatible object by
     * reference so the call site doesn't need a cast. The actual
     * implementation only uses `h->printString(msg)` so a void *
     * cast is fine. */
    template <typename T> void printError(T &p) const { printError((SerialHandle *)(void *)&p); }
    void onProgress(UpdateProgressCb cb) { progress_cb_ = cb; }
    uint32_t progress() const { return progress_; }
    uint32_t size()    const { return size_; }
private:
    esp_ota_handle_t handle_ = 0;
    const esp_partition_t *part_ = nullptr;
    uint32_t size_  = 0;
    uint32_t written_ = 0;
    uint32_t progress_ = 0;
    int  err_  = 0;
    bool finished_ = false;
    UpdateProgressCb progress_cb_ = nullptr;
};

extern UpdateClass Update;

/* Arduino-compatible ESP facade (subset used by v3 source) */
class ESPClass {
public:
    uint32_t getFreeSketchSpace();
    void     restart();
    bool     partitionRead(const esp_partition_t *p, size_t off, uint32_t *buf, size_t sz);
    esp_err_t partitionEraseRange(const esp_partition_t *p, uint32_t off, uint32_t sz);    uint64_t getEfuseMac() {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        return ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) |
               ((uint64_t)mac[2] << 24) | ((uint64_t)mac[3] << 16) |
               ((uint64_t)mac[4] <<  8) | ((uint64_t)mac[5] <<  0);
    }
    /* Heap-introspection helpers used by the v3 source. */
    uint32_t getFreeHeap()    const;
    uint32_t getHeapSize()    const;
    uint32_t getMinFreeHeap() const;
    uint32_t getMaxAllocHeap()const;
};
extern ESPClass ESP;

#endif
