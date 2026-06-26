// OneWire driver backed by the official espressif/onewire_bus IDF component.
// Provides the same OneWire32 class interface used by OneWire.cpp so that no
// changes are needed in the callers.
#pragma once

#include "arduino_compat.h"
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_device.h"
#include "onewire_cmd.h"
#include "onewire_crc.h"

#define OWR_OK		0
#define OWR_CRC		1
#define OWR_BAD_DATA	2
#define OWR_TIMEOUT	3
#define OWR_DRIVER	4

class OneWire32 {
	private:
		onewire_bus_handle_t bus = nullptr;
	public:
		OneWire32(uint8_t pin);
		~OneWire32();
		bool reset();
		void request();
		uint8_t readRom(uint8_t data[8]);
		uint8_t getTemp(uint64_t &addr, float &temp);
		uint8_t search(uint64_t addresses[], uint8_t total);
		bool read(uint8_t &data, uint8_t len = 8);
		bool write(const uint8_t data, uint8_t len = 8);
};
