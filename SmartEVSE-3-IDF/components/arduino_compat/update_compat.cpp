/*
 * update_compat.c — Update + ESP shim implementation
 */
#include "update_compat.h"
#include "http_compat.h"   /* Stream forward decl + http_compat */
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

//static const char *TAG = "update_compat";

UpdateClass Update;
ESPClass ESP;

bool UpdateClass::begin(uint32_t size, int partition) {
    if (partition == U_SPIFFS) {
        part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    } else {
        /* For app OTA, always use the NEXT (non-running) partition.
         * Hardcoding ota_0 fails with ESP_ERR_OTA_PARTITION_CONFLICT
         * when the device is already running from ota_0. */
        part_ = esp_ota_get_next_update_partition(NULL);
    }
    if (!part_) { err_ = -1; return false; }
    if (size == UPDATE_SIZE_UNKNOWN) {
        /* Use partition size minus a small safety margin (matches Arduino) */
        size = (part_->size & ~0xFFF) - 0x1000;
    }
    esp_err_t e = esp_ota_begin(part_, size, &handle_);
    if (e != ESP_OK) { err_ = e; return false; }
    size_   = size;
    written_ = 0;
    progress_ = 0;
    finished_ = false;
    err_ = 0;
    return true;
}

int UpdateClass::write(uint8_t *buf, size_t len) {
    if (!handle_ || !part_) { err_ = -2; return 0; }
    esp_err_t e = esp_ota_write(handle_, buf, len);
    if (e != ESP_OK) { err_ = e; return 0; }
    written_ += len;
    if (progress_cb_) progress_cb_(written_, size_);
    return len;
}

int UpdateClass::writeStream(Stream &s) {
    uint8_t buf[1024];
    int total = 0;
    while (s.available() || s.connected()) {
        int n = s.readBytes((char*)buf, sizeof(buf));
        if (n <= 0) {
            vTaskDelay(1);
            continue;
        }
        int w = write(buf, n);
        if (w != n) { err_ = -3; return total; }
        total += w;
        if (total >= (int)size_) break;
    }
    return total;
}

bool UpdateClass::end(bool setBoot) {
    if (!handle_) { err_ = -4; return false; }
    esp_err_t e = esp_ota_end(handle_);
    handle_ = 0;
    if (e != ESP_OK) { err_ = e; return false; }
    if (setBoot) esp_ota_set_boot_partition(part_);
    finished_ = true;
    return true;
}

void UpdateClass::abort() {
    if (handle_) { esp_ota_abort(handle_); handle_ = 0; }
    err_ = -5;
    finished_ = false;
}

String UpdateClass::errorString() const {
    char buf[64];
    if (err_ == 0) snprintf(buf, sizeof(buf), "No error");
    else           snprintf(buf, sizeof(buf), "Error %d (%s)", err_, esp_err_to_name((esp_err_t)err_));
    return String(buf);
}

void UpdateClass::printError(SerialHandle *h) const {
    String s = errorString();
    Serial_println(h, s.c_str());
}

uint32_t ESPClass::getFreeSketchSpace() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    if (!running || !next) return 0;
    return next->size;
}

void ESPClass::restart() {
    /* Give the calling task a moment to flush any in-flight log. */
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

bool ESPClass::partitionRead(const esp_partition_t *p, size_t off, uint32_t *buf, size_t sz) {
    if (!p || !buf) return false;
    esp_err_t e = esp_partition_read(p, off, buf, sz);
    return e == ESP_OK;
}

esp_err_t ESPClass::partitionEraseRange(const esp_partition_t *p, uint32_t off, uint32_t sz) {
    if (!p) return ESP_ERR_INVALID_ARG;
    return esp_partition_erase_range(p, off, sz);
}


#include "esp_heap_caps.h"
#include "esp_system.h"

uint32_t ESPClass::getFreeHeap() const     { return esp_get_free_heap_size(); }
uint32_t ESPClass::getHeapSize() const     { return heap_caps_get_total_size(MALLOC_CAP_DEFAULT); }
uint32_t ESPClass::getMinFreeHeap() const  { return esp_get_minimum_free_heap_size(); }
uint32_t ESPClass::getMaxAllocHeap() const { return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT); }

