/*
 * IPAddress.h - compatibility shim.
 *
 * The Arduino IPAddress class is used by MicroOcppMongooseClient.h
 * (it includes <IPAddress.h> when ARDUINO is defined). The full
 * implementation lives in components/arduino_compat/include/wifi_compat.h
 * as `class IPAddress`. This header is a thin alias that re-exports
 * it so vendored libraries that include <IPAddress.h> find the
 * same type.
 */
#ifndef ARDUINO_COMPAT_IPADDRESS_H
#define ARDUINO_COMPAT_IPADDRESS_H

#include "wifi_compat.h"   /* provides `class IPAddress` */

#endif /* ARDUINO_COMPAT_IPADDRESS_H */
