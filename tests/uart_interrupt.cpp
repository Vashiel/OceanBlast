#include "memory/bus.h"
#include <iostream>
#include <sstream>

using namespace oceanblast;

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        failures += !passed;
    };
    constexpr u32 tx = 1u << 1, uart = 1u << 28;
    {
        Bus bus; bus.reset();
        bus.write32(0x50000004, 0x204); // TX IRQ mode, level trigger.
        check("UART0 empty condition latches TXD0 while masked",
              (bus.read32(0x4A000018) & tx) && !bus.hasPendingIrq());
        bus.write32(0x4A000008, ~uart);
        check("UART0 submask prevents parent interrupt", !bus.hasPendingIrq());
        bus.write32(0x4A00001C, 0x7ff & ~tx);
        check("UART0 sub-unmask delivers main IRQ 28",
              bus.hasPendingIrq() && bus.read32(0x4A000014) == 28);
        bus.write32(0x4A000018, tx);
        check("UART0 empty level reasserts after subpending W1C", bus.read32(0x4A000018) & tx);
        bus.write32(0x4A000000, uart);
        bus.write32(0x4A000010, uart);
        bus.tick();
        check("UART0 empty level reasserts after main acknowledgement", bus.hasPendingIrq());
        bus.write32(0x4A000000, uart);
        bus.write32(0x4A000010, uart);
        bus.tick(0);
        check("Zero-time tick retains UART level reassertion", bus.hasPendingIrq());
        bus.write32(0x4A00001C, 0x7ff);
        bus.write32(0x4A000000, uart);
        bus.write32(0x4A000010, uart);
        bus.tick();
        check("UART0 masking stops repeated parent delivery", !bus.hasPendingIrq());
    }
    {
        Bus bus; bus.reset();
        bus.write32(0x4A00001C, 0x7ff & ~tx);
        bus.write32(0x50000004, 0x204);
        check("UART0 main mask preserves pending source without IRQ", !bus.hasPendingIrq());
        bus.write32(0x4A000008, ~uart);
        check("UART0 main-unmask delivers the pending source", bus.hasPendingIrq());
    }
    {
        Bus bus; bus.reset();
        bus.write32(0x50000004, 0x4); // Edge trigger.
        bus.write32(0x4A000018, tx);
        bus.tick();
        check("UART0 edge trigger does not continuously relatch empty", !(bus.read32(0x4A000018) & tx));
        std::ostringstream output;
        auto* previous = std::cout.rdbuf(output.rdbuf());
        bus.write8(0x50000020, 'A');
        bus.write8(0x50000024, 'B'); // URXH cannot transmit.
        std::cout.rdbuf(previous);
        check("UART0 byte transmit completes and latches TXD0", (bus.read32(0x4A000018) & tx) && output.str() == "A");
    }
    {
        Bus bus; bus.reset();
        bus.write32(0x50000004, 0x8); // TX DMA mode.
        std::ostringstream output;
        auto* previous = std::cout.rdbuf(output.rdbuf());
        bus.write8(0x50000020, 'C');
        std::cout.rdbuf(previous);
        check("UART0 DMA mode does not generate TX IRQ", !(bus.read32(0x4A000018) & tx));
    }
    return failures ? 1 : 0;
}
