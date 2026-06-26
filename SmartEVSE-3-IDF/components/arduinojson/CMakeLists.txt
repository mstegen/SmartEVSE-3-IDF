# =============================================================================
# ArduinoJson 7.0.4 - vendored as a header-only component.
# MicroOcpp depends on it for JsonObject, StaticJsonDocument, etc.
# Source: https://github.com/bblanchon/ArduinoJson (v7.0.4)
#
# We disable the Arduino-specific optional integrations (Stream / Print
# / String / PROGMEM) so ArduinoJson never tries to `#include <Arduino.h>`.
# That header is provided by the eModbus shim and is unsafe to pull into
# unrelated TUs.
# =============================================================================
idf_component_register(
    INCLUDE_DIRS "src"
    SRC_DIRS     "."
)

target_compile_definitions(${COMPONENT_LIB} INTERFACE
    ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
    ARDUINOJSON_ENABLE_ARDUINO_PRINT=0
    ARDUINOJSON_ENABLE_ARDUINO_STRING=0
    ARDUINOJSON_ENABLE_PROGMEM=0
)
