/*
 * arduino_compat.h � single-include umbrella for the Arduino compatibility
 * shim. Including this in a translation unit gives you:
 *   - millis/micros/delay
 *   - pinMode / digitalWrite / digitalRead / analogRead
 *   - LOW / HIGH / INPUT / OUTPUT / INPUT_PULLUP / RISING / FALLING / CHANGE
 *   - Serial1 / Serial2 (UART-backed SerialHandle)
 *   - Preferences (NVS-backed)
 *   - String (thin std::string wrapper)
 *   - WiFi (singleton over esp_wifi)
 *   - HTTPClient, WiFiClient, Stream
 *   - Update, ESP
 *   - SPIClass
 *   - LittleFS / FS / File (stubs)
 *
 * The shim is intentionally narrow: only the surface that SmartEVSE-3 v3
 * actually uses. Heavy APIs are routed through the native ESP-IDF drivers
 * so the runtime behaviour matches the original.
 */
#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include "arduino_compat_base.h"
#include "wifi_compat.h"
#include "http_compat.h"
#include "update_compat.h"
#include "spi_compat.h"
#include "fs_compat.h"

#endif /* ARDUINO_COMPAT_H */