#pragma once
#include <array>
#include <cstdint>

namespace oceanblast {
// 24C16-style EEPROM: eight 256-byte banks and 16-byte page writes.
// Erased contents belong to the emulated board, never to the cartridge image.
class I2cEeprom {
public:
    I2cEeprom() { bytes.fill(0xff); }
    const std::array<uint8_t, 2048>& contents() const { return bytes; }
    void restore(const std::array<uint8_t, 2048>& image) { bytes = image; resetBus(); }
    bool start(uint8_t addressByte) {
        dirty = 0; // A repeated START does not commit an unfinished page write.
        selected = (addressByte >> 1) >= 0x50 && (addressByte >> 1) <= 0x57;
        reading = (addressByte & 1) != 0;
        expectWordAddress = !reading;
        if (selected) pointer = ((addressByte >> 1) & 7) * 256 + (pointer & 255);
        return selected;
    }
    bool write(uint8_t value) {
        if (!selected || reading) return false;
        if (expectWordAddress) {
            pointer = (pointer & 0x700) | value;
            pageBase = pointer & ~15u;
            expectWordAddress = false;
        } else {
            staged[pointer & 15] = value;
            dirty |= uint16_t(1u << (pointer & 15));
            pointer = pageBase | ((pointer + 1) & 15);
        }
        return true;
    }
    uint8_t read() {
        if (!selected || !reading) return 0xff;
        const auto value = bytes[pointer];
        pointer = (pointer + 1) & 0x7ff;
        return value;
    }
    void stop() {
        for (unsigned i = 0; i < 16; ++i)
            if (dirty & (1u << i)) bytes[pageBase + i] = staged[i];
        dirty = 0;
        selected = false;
    }
    void resetBus() { selected = false; dirty = 0; pointer = 0; }
private:
    std::array<uint8_t, 2048> bytes;
    std::array<uint8_t, 16> staged{};
    unsigned pointer = 0, pageBase = 0;
    uint16_t dirty = 0;
    bool selected = false, reading = false, expectWordAddress = true;
};
}
