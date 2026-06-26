# =============================================================================
# eModbus 1.7.2 — vendored as a CMake component
#
# Source: https://github.com/miq19/eModbus (commit tagged 1.7.2)
# Pure C++; no Arduino dependency in the library itself. The Arduino wrapper
# layer that the library normally provides (modbus-esp8266.h bridge) is
# NOT compiled here — SmartEVSE-3 only uses ModbusClientRTU and
# ModbusServerRTU directly, both of which depend on a small set of FreeRTOS
# primitives that the IDF already provides.
#
# To populate this component, copy the `src/` and `extras/` folders from
# eModbus 1.7.2 into this directory. Then build only the RTU server + client
# translation units (we skip the TCP transports because SmartEVSE-3 uses
# RS-485 over UART1).
# =============================================================================

set(EMODBUS_RTU_SOURCES
    "src/ModbusClient.cpp"
    "src/ModbusClientRTU.cpp"
    "src/ModbusServer.cpp"
    "src/ModbusServerRTU.cpp"
    "src/Logging.cpp"
    "src/RTUutils.cpp"
    "src/CoilData.cpp"
    "src/ModbusMessage.cpp"
    "src/emodbus_serial1.cpp"
)

idf_component_register(
    SRC_DIRS
        "src"
    INCLUDE_DIRS
        "src"
        "include"
    REQUIRES
        arduino_compat   # for Serial1 used by eModbus
    EXCLUDE_SRCS
        "src/ModbusClientTCP.cpp"
        "src/ModbusClientTCPasync.cpp"
        "src/ModbusServerTCPasync.cpp"
        "src/ModbusServerTCPtemp.cpp"
)

# eModbus's source files are written for Arduino's `Print` class and
# `Serial` global. We provide a minimal Arduino.h in components/emodbus/
# include/ that supplies just enough surface to compile. The ESP32 branch
# of eModbus's options.h is selected by defining ESP32 here.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32=1
    ARDUINO=200
    LOG_LEVEL=LOG_LEVEL_ERROR
)

# If the vendored sources are not yet present, give a friendly error so the
# developer knows to fetch them.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/ModbusServerRTU.h")
    message(FATAL_ERROR
        "eModbus sources not found in components/emodbus/src/. "
        "Download eModbus v1.7.2 and copy its src/ + extras/ folders here.")
endif()
