/*
 * OneWire driver backed by the official espressif/onewire_bus IDF component.
 * Provides the same OneWire32 class interface consumed by OneWire.cpp so no
 * changes are needed in the callers.
 *
 * The onewire_bus component handles all RMT channel setup, GPIO configuration,
 * timing, and the open-drain pin mode internally -- eliminating all the manual
 * RMT channel management that was the source of previous issues.
 */

#include "OneWireESP32.h"

OneWire32::OneWire32(uint8_t pin) {
onewire_bus_config_t bus_cfg = {};
bus_cfg.bus_gpio_num      = pin;
bus_cfg.flags.en_pull_up  = false;  /* external 4.7kO pull-up on v3 PCB */

onewire_bus_rmt_config_t rmt_cfg = {};
rmt_cfg.max_rx_bytes = 10;  /* reset presence (1) + ReadROM response (8) + spare */

onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus);
}

OneWire32::~OneWire32() {
if (bus) {
onewire_bus_del(bus);
bus = nullptr;
}
}

bool OneWire32::reset() {
return bus && onewire_bus_reset(bus) == ESP_OK;
}

/* Write a single byte (len=8) to the bus.
 * Bit-level writes (len<8) are only used by search(), which delegates to the
 * IDF device iterator, so those cases are not needed here. */
bool OneWire32::write(const uint8_t data, uint8_t len) {
if (!bus || len != 8) return false;
return onewire_bus_write_bytes(bus, &data, 1) == ESP_OK;
}

/* Read a single byte (len=8) from the bus.
 * Bit-level reads (len<8) are only used by search(), which delegates to the
 * IDF device iterator, so those cases are not needed here. */
bool OneWire32::read(uint8_t &data, uint8_t len) {
if (!bus || len != 8) return false;
return onewire_bus_read_bytes(bus, &data, 1) == ESP_OK;
}

/* Send temperature-conversion request to all DS18B20s (Skip ROM + Convert). */
void OneWire32::request() {
if (!bus || !reset()) return;
uint8_t cmd = ONEWIRE_CMD_SKIP_ROM;
onewire_bus_write_bytes(bus, &cmd, 1);
cmd = 0x44; /* DS18B20 Convert T */
onewire_bus_write_bytes(bus, &cmd, 1);
}

/* Read the 8-byte ROM of the single device on the bus (ReadROM, 0x33).
 * Returns OWR_OK on success, OWR_TIMEOUT if no device, OWR_DRIVER if not init. */
uint8_t OneWire32::readRom(uint8_t data[8]) {
if (!bus) return OWR_DRIVER;
if (!reset()) return OWR_TIMEOUT;
uint8_t cmd = 0x33; /* Read ROM */
if (onewire_bus_write_bytes(bus, &cmd, 1) != ESP_OK) return OWR_TIMEOUT;
if (onewire_bus_read_bytes(bus, data, 8) != ESP_OK) return OWR_TIMEOUT;
return OWR_OK;
}

/* Read temperature from a DS18B20 at the given 64-bit address. */
uint8_t OneWire32::getTemp(uint64_t &addr, float &temp) {
if (!bus) return OWR_DRIVER;
if (!reset()) return OWR_TIMEOUT;

/* Match ROM -- address this specific device. */
uint8_t cmd = ONEWIRE_CMD_MATCH_ROM;
if (onewire_bus_write_bytes(bus, &cmd, 1) != ESP_OK) return OWR_TIMEOUT;
if (onewire_bus_write_bytes(bus, (uint8_t *)&addr, 8) != ESP_OK) return OWR_TIMEOUT;

cmd = 0xBE; /* Read Scratchpad */
if (onewire_bus_write_bytes(bus, &cmd, 1) != ESP_OK) return OWR_TIMEOUT;

uint8_t data[9];
if (onewire_bus_read_bytes(bus, data, 9) != ESP_OK) return OWR_TIMEOUT;

/* Verify CRC8 -- result must be 0 over all 9 bytes. */
if (onewire_crc8(0, data, 9) != 0) return OWR_CRC;

uint16_t zero = 0;
for (int i = 0; i < 9; i++) zero += data[i];
if (zero == 0 || zero == 0x8f7) return OWR_BAD_DATA;

int16_t t = (data[1] << 8) | data[0];
temp = (float)t / 16.0f;
return OWR_OK;
}

/* Enumerate devices on the bus using the IDF device iterator.
 * Returns the number of devices found (up to `total`). */
uint8_t OneWire32::search(uint64_t addresses[], uint8_t total) {
if (!bus || total == 0) return 0;
uint8_t found = 0;
onewire_device_iter_handle_t iter = nullptr;
if (onewire_new_device_iter(bus, &iter) != ESP_OK) return 0;
onewire_device_t dev;
while (found < total && onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
addresses[found++] = dev.address;
}
onewire_del_device_iter(iter);
return found;
}
