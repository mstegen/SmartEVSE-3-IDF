// emodbus_serial1.cpp - definition of the Serial1 HardwareSerial instance
// that eModbus's Logging.cpp and ModbusServerRTU.cpp reference.
//
// Serial1 is the RS-485 UART (UART_NUM_1) used by SmartEVSE-3. The
// underlying SerialHandle is owned by the arduino_compat shim; this
// file just constructs the C++ wrapper around it.
#include "Arduino.h"

HardwareSerial Serial1(Serial1_get());

// Silent stub of the default `Serial` (UART0 debug). eModbus's Logging
// takes its address and stores it in LOGDEVICE; we discard everything.
_SerialStubPrint Serial;
