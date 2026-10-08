#include "memory/bus.h"
#include <iostream>

using namespace oceanblast;

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        failures += !ok;
    };
    I2cEeprom chip;
    check("EEPROM rejects unrelated codec address", !chip.start(0x20));
    check("EEPROM acknowledges final bank address", chip.start(0xae));
    chip.write(0xfe); chip.write(0x12); chip.write(0x34); chip.write(0x56); chip.stop();
    chip.start(0xae); chip.write(0xfe); chip.start(0xaf);
    check("EEPROM random read preserves upper bank and word address", chip.read() == 0x12);
    check("EEPROM sequential read advances address", chip.read() == 0x34);
    check("EEPROM sequential read wraps entire chip", chip.read() == 0xff);
    chip.stop(); chip.start(0xae); chip.write(0xf0); chip.start(0xaf);
    check("EEPROM page write wraps within sixteen-byte page", chip.read() == 0x56);
    chip.stop(); chip.resetBus(); chip.start(0xae); chip.write(0xfe); chip.start(0xaf);
    check("EEPROM bus reset preserves stored bytes", chip.read() == 0x12);
    chip.stop(); chip.start(0xa0); chip.write(0); chip.start(0xa1);
    check("EEPROM erased locations read FF", chip.read() == 0xff);

    const auto saved = chip.contents();
    I2cEeprom restored; restored.restore(saved);
    restored.start(0xae); restored.write(0xfe); restored.start(0xaf);
    check("EEPROM image restore preserves committed bytes", restored.read() == 0x12);
    restored.stop(); restored.start(0xa0); restored.write(0); restored.write(0x42);
    restored.start(0xa0); restored.write(0); restored.start(0xa1);
    check("EEPROM repeated START discards unfinished page write", restored.read() == 0xff);

    Bus bus; bus.reset();
    constexpr u32 con = 0x54000000, stat = con + 4, ds = con + 12;
    auto start = [&](u8 address, bool irq = true) {
        bus.write32(con, irq ? 0xe8 : 0xc8);
        bus.write32(stat, address & 1 ? 0x90 : 0xd0);
        bus.write8(ds, address);
        bus.write32(stat, address & 1 ? 0xb0 : 0xf0);
        bus.tick(50);
    };
    auto send = [&](u8 value) { bus.write8(ds, value); bus.write32(con, 0xe8); bus.tick(50); };
    auto stop = [&]() { bus.write32(stat, bus.read32(stat) & ~0x20u); bus.write32(con, 0xc8); };
    bus.write32(0x4a000008, ~(1u << 27));
    start(0x20);
    check("I2C unknown slave reports NACK and completion interrupt", (bus.read32(stat) & 1) && bus.hasPendingIrq());
    stop(); bus.write32(0x4a000000, 1u << 27); bus.write32(0x4a000010, 1u << 27);
    start(0xa0, false);
    check("I2C disabled interrupt still exposes pending byte completion", !bus.hasPendingIrq() && (bus.read32(con) & 0x10));
    stop(); start(0xa0);
    check("I2C EEPROM address phase acknowledges without receiving data", !(bus.read32(stat) & 1) && bus.read32(ds) == 0xa0);
    send(0x23); send(0x67); send(0x89); stop();
    start(0xa0); send(0x23); start(0xa1);
    check("I2C repeated START receives a separate address completion", bus.read32(ds) == 0xa1);
    bus.write32(con, 0xe8); bus.tick(49);
    check("I2C next byte waits until completion time", !(bus.read32(con) & 0x10));
    bus.tick(1);
    check("I2C random read returns written data through IICDS", bus.read32(ds) == 0x67 && !(bus.read32(stat) & 1));
    bus.write32(con, 0x68); bus.tick(50);
    check("I2C final receive byte is available with master's NACK", bus.read32(ds) == 0x89 && (bus.read32(stat) & 1));
    bus.write32(con, 0xe8); stop(); bus.tick(100);
    check("I2C STOP cancels the scheduled next byte", !(bus.read32(con) & 0x10));
    start(0x20); stop(); start(0xa0);
    check("I2C next ACK clears prior read-only NACK status", !(bus.read32(stat) & 1));
    return failures ? 1 : 0;
}
