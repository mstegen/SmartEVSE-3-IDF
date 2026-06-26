// Stream.h - Arduino surface for the eModbus component.
//
// eModbus's RTU sources include <Stream.h> and expect the Arduino
// `Stream` and `Print` classes. The `Stream` class itself is provided
// by the arduino_compat shim (specifically http_compat.h, which is
// pulled in by the umbrella `arduino_compat.h` that this file's parent
// Arduino.h also includes). The `Print` class is also now defined in
// arduino_compat_base.h (as the base of `_SerialStubPrint`); we just
// re-use it from here so the eModbus code can treat `Print` as a
// familiar type.

#ifndef EMODBUS_PRINT_SHIM_H_GUARD_2026_06_03
#define EMODBUS_PRINT_SHIM_H_GUARD_2026_06_03

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// Pull in the shim's `Stream` and `Print` classes so callers that only
// #include "Stream.h" (e.g. RTUutils.h) get a usable Stream + Print.
#include "arduino_compat.h"

#endif // EMODBUS_PRINT_SHIM_H_GUARD_2026_06_03
