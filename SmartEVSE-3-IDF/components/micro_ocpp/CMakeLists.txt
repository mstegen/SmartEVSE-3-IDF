# =============================================================================
# MicroOcpp — vendored as a CMake component
#
# Source: https://github.com/matth-x/MicroOcpp (commit
#   942fb88aa736a5f99d1a24c11fe657c49af2c7ea, the one pinned in the
#   original platformio.ini lib_deps).
#
# Only the library core is built here. The Mongoose transport adapter
# (MicroOcppMongooseClient) lives in `components/micro_ocpp_mongoose/`.
# =============================================================================

# Collect all .cpp / .c files recursively (MicroOcpp ships a deep
# src/ tree with .cpp files in many subdirectories).
file(GLOB_RECURSE MICRO_OCPP_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)
# Exclude the Mongoose transport adapter (lives in micro_ocpp_mongoose).
list(FILTER MICRO_OCPP_SOURCES EXCLUDE REGEX "Mongoose")
# Exclude the FTP-over-TLS adapter. SmartEVSE-3 doesn't use it and the
# v6 mbedTLS tree removed the public `mbedtls/entropy.h` header that
# FtpMbedTLS.cpp depends on. Re-enable if/when we wire up OCPP
# firmware updates via the FTP path.
list(FILTER MICRO_OCPP_SOURCES EXCLUDE REGEX "FtpMbedTLS")

idf_component_register(
    SRC_DIRS
        "src"
    SRCS "${MICRO_OCPP_SOURCES}"
    INCLUDE_DIRS
        "src"
    REQUIRES
        arduino_compat
        arduinojson
        mongoose
        mbedtls
        littlefs
)

# GCC 15 + -Werror flags strncpy() calls as truncated when the bound
# equals the destination size (a long-standing idiom in MicroOcpp's
# code). The behaviour is well-defined and the truncation is
# intentional; silence the warning here.
#
# ArduinoJson 7.0.4 deprecated DynamicJsonDocument in favour of
# JsonDocument, but MicroOcpp's JsonDoc typedef and the
# DynamicJsonDocument(size_t) constructor are used throughout the
# vendored library. Migrating to JsonDocument would require changing
# the constructor pattern (JsonDocument takes an Allocator*, not a
# size_t) and removing the .capacity() calls — a non-trivial patch
# across the vendored tree. Suppress the deprecation warning here
# until MicroOcpp upstream migrates.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-stringop-truncation
    -Wno-deprecated-declarations
)

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/MicroOcpp.h")
    message(FATAL_ERROR
        "MicroOcpp sources not found in components/micro_ocpp/src/. "
        "Clone matth-x/MicroOcpp at 942fb88a and copy src/ here.")
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MO_ENABLE_CONNECTOR_LOCK=1
    MO_ENABLE_MBEDTLS=1
    MO_PARTITION_LABEL=\"spiffs\"
    MO_CUSTOM_WS
    # Not defining a custom console will output on the serial debug output.
    # MO_CUSTOM_CONSOLE
    # if defined log OCPP traffic to (serial) output
    MO_TRAFFIC_OUT
    # Set micro-ocpp log level here. NONE will still output traffic, if defined.
    # MO_DBG_LEVEL=MO_DL_NONE
    # MO_DBG_LEVEL=MO_DL_ERROR
    MO_DBG_LEVEL=MO_DL_INFO
    # Run MicroOcpp in native ESP-IDF mode so it uses esp_log rather
    # than the Arduino <Arduino.h> console path. Use the ESP-IDF
    # LittleFS VFS adapter (esp_littlefs) for config persistence
    # instead of SPIFFS — LittleFS is power-fail safe and is what the
    # upstream Arduino build (ARDUINO_LITTLEFS) uses.
    MO_PLATFORM=MO_PLATFORM_ESPIDF
    MO_PLATFORM_ESPIDF=2
    MO_USE_FILEAPI=ESPIDF_LITTLEFS
    # FtpMbedTLS.cpp is excluded from the build (v6 mbedTLS removed
    # mbedtls/entropy.h). Gate the corresponding makeFtpClientMbedTLS
    # call site in MicroOcpp.cpp so the linker doesn't look for it.
    MO_ENABLE_FTP_MBEDTLS=0
)
