#include <iostream>
#include <cassert>
#include "memory/bus.h"
#include "display/display.h"

using namespace oceanblast;

int main() {
    std::cout << "Running OceanBlast Keypad & GPIO Test Suite..." << std::endl;
    Bus bus;
    bus.reset();

    // 1. Check default pull-up states (all released = 1)
    u32 gpfDefault = bus.getMmio(0x56000054);
    u32 gpgDefault = bus.getMmio(0x56000064);
    if ((gpfDefault & 0xFF) != 0xFF) {
        std::cerr << "FAIL: GPFDAT default is not 0xFF: 0x" << std::hex << gpfDefault << std::dec << std::endl;
        return 1;
    }
    if ((gpgDefault & 0xFFFF) != 0xFFFF) {
        std::cerr << "FAIL: GPGDAT default is not 0xFFFF: 0x" << std::hex << gpgDefault << std::dec << std::endl;
        return 1;
    }
    std::cout << "PASS: GPIO pull-up default state (all high)" << std::endl;

    // 2. Unmask interrupts in INTMSK and EINTMASK
    bus.write32(0x4A000008, 0x00000000); // INTMSK = unmask all
    bus.write32(0x560000A4, 0x00000000); // EINTMASK = unmask all

    // 3. Test BTN_UP (GPF2 / EINT2)
    bus.setButtonMask(BTN_UP);
    u32 gpfUp = bus.getMmio(0x56000054);
    if ((gpfUp & (1 << 2)) != 0) {
        std::cerr << "FAIL: GPF2 is not active-low 0 on BTN_UP" << std::endl;
        return 1;
    }
    if (!bus.hasPendingIrq() || bus.getMmio(0x4A000014) != 2) {
        std::cerr << "FAIL: EINT2 (IRQ 2) was not triggered on BTN_UP" << std::endl;
        return 1;
    }
    bus.write32(0x4A000000, 1 << 2); // clear SRCPND
    bus.write32(0x4A000010, 1 << 2); // clear INTPND
    std::cout << "PASS: BTN_UP pulls GPF2 low and triggers EINT2 (IRQ 2)" << std::endl;

    // 4. Test BTN_A (GPF0 / EINT0)
    bus.setButtonMask(BTN_UP | BTN_A);
    u32 gpfA = bus.getMmio(0x56000054);
    if ((gpfA & (1 << 0)) != 0) {
        std::cerr << "FAIL: GPF0 is not active-low 0 on BTN_A" << std::endl;
        return 1;
    }
    if (!bus.hasPendingIrq() || bus.getMmio(0x4A000014) != 0) {
        std::cerr << "FAIL: EINT0 (IRQ 0) was not triggered on BTN_A" << std::endl;
        return 1;
    }
    bus.write32(0x4A000000, 0xFFFFFFFF);
    bus.write32(0x4A000010, 0xFFFFFFFF);
    std::cout << "PASS: BTN_A pulls GPF0 low and triggers EINT0 (IRQ 0)" << std::endl;

    // Reset buttons and clear pending
    bus.setButtonMask(0);
    bus.write32(0x4A000000, 0xFFFFFFFF);
    bus.write32(0x4A000010, 0xFFFFFFFF);

    // 5. Test BTN_START (GPG10 / EINT18 -> IRQ 5)
    bus.setButtonMask(BTN_START);
    u32 gpgStart = bus.getMmio(0x56000064);
    if ((gpgStart & (1 << 10)) != 0) {
        std::cerr << "FAIL: GPG10 is not active-low 0 on BTN_START" << std::endl;
        return 1;
    }
    u32 eintPend = bus.getMmio(0x560000A8);
    if ((eintPend & (1 << 18)) == 0) {
        std::cerr << "FAIL: EINTPEND bit 18 not set on BTN_START" << std::endl;
        return 1;
    }
    if (!bus.hasPendingIrq() || bus.getMmio(0x4A000014) != 5) {
        std::cerr << "FAIL: EINT8_23 (IRQ 5) was not triggered on BTN_START" << std::endl;
        return 1;
    }
    std::cout << "PASS: BTN_START pulls GPG10 low, sets EINTPEND, and triggers EINT8_23 (IRQ 5)" << std::endl;

    // 6. Test W1C clear of EINTPEND
    bus.write32(0x560000A8, 1 << 18);
    if ((bus.getMmio(0x560000A8) & (1 << 18)) != 0) {
        std::cerr << "FAIL: EINTPEND bit 18 did not clear on W1C write" << std::endl;
        return 1;
    }
    std::cout << "PASS: EINTPEND W1C clear functionality" << std::endl;

    // 7. Test release of all buttons (returns to all 1s)
    bus.setButtonMask(0);
    if ((bus.getMmio(0x56000054) & 0xFF) != 0xFF || (bus.getMmio(0x56000064) & 0xFFFF) != 0xFFFF) {
        std::cerr << "FAIL: GPIO lines did not restore to high after release" << std::endl;
        return 1;
    }
    std::cout << "PASS: Button release restores pull-up high state" << std::endl;

    bus.setButtonMask(BTN_C | BTN_REWIND | BTN_FORWARD | BTN_PLAY | BTN_STOP);
    if ((bus.getMmio(0x56000054)&(1<<4)) || (bus.getMmio(0x56000064)&((1<<0)|(1<<9)|(1<<10)|(1<<13)))) return 1;
    if ((bus.getMmio(0x560000A8)&((1<<4)|(1<<8)|(1<<17)|(1<<18)|(1<<21))) != ((1<<4)|(1<<8)|(1<<17)|(1<<18)|(1<<21))) return 1;
    std::cout << "PASS: Additional action and Player transport/seek inputs drive active-low GPIO and EINT pending bits\n";
    bus.setButtonMask(0);
    bus.write32(0x560000A8, 0xFFFFFFFF);
    bus.write32(0x4A000000, 0xFFFFFFFF);
    bus.write32(0x4A000010, 0xFFFFFFFF);
    if ((bus.getMmio(0x56000054)&0xff)!=0xff || (bus.getMmio(0x56000064)&0xffff)!=0xffff) return 1;
    std::cout << "PASS: Additional input release restores pull-ups\n";

    // Test multi-bit EINTPEND re-assertion (BTN_SELECT asserts both EINT17 and EINT19)
    bus.setButtonMask(BTN_SELECT);
    if ((bus.getMmio(0x560000A8) & ((1 << 17) | (1 << 19))) != ((1 << 17) | (1 << 19))) return 1;
    // Simulate kernel servicing EINT19 first and clearing SRCPND bit 5
    bus.write32(0x560000A8, 1 << 19);
    bus.write32(0x4A000000, 1 << 5);
    bus.write32(0x4A000010, 1 << 5);
    // Because EINT17 is still pending and unmasked, SRCPND bit 5 must re-assert for a second IRQ pass
    if (!bus.hasPendingIrq() || bus.getMmio(0x4A000014) != 5) {
        std::cerr << "FAIL: IRQ 5 did not re-assert while EINT17 remained pending in EINTPEND" << std::endl;
        return 1;
    }
    bus.write32(0x560000A8, 1 << 17);
    bus.write32(0x4A000000, 1 << 5);
    bus.write32(0x4A000010, 1 << 5);
    if (bus.hasPendingIrq()) {
        std::cerr << "FAIL: IRQ 5 remained asserted after all EINTPEND bits were cleared" << std::endl;
        return 1;
    }
    bus.setButtonMask(0);
    bus.write32(0x560000A8, 0xFFFFFFFF);
    bus.write32(0x4A000000, 0xFFFFFFFF);
    bus.write32(0x4A000010, 0xFFFFFFFF);
    std::cout << "PASS: Multi-bit EINTPEND re-asserts IRQ 5 until all unmasked sub-interrupts are serviced\n";
    // A masked pin edge is retained and forwarded when its EINT is unmasked.
    bus.write32(0x560000A4, 0x00fffff0);
    bus.setButtonMask(BTN_PLAY);
    if (bus.hasPendingIrq() || !(bus.getMmio(0x560000A8) & (1u << 17))) return 1;
    bus.write32(0x560000A4, 0);
    if (!bus.hasPendingIrq() || bus.getMmio(0x4A000014) != 5) return 1;
    bus.write32(0x560000A8, 1u << 17);
    bus.write32(0x4A000000, 1u << 5);
    bus.write32(0x4A000010, 1u << 5);
    std::cout << "PASS: Unmasking a retained EINT forwards its group interrupt\n";
    // Select already holds the Play GPIO low; adding/removing Play creates no edge.
    bus.setButtonMask(BTN_SELECT);
    bus.write32(0x560000A8, 0xffffffff);
    bus.write32(0x4A000000, 0xffffffff);
    bus.write32(0x4A000010, 0xffffffff);
    bus.setButtonMask(BTN_SELECT | BTN_PLAY);
    bus.setButtonMask(BTN_SELECT);
    if (bus.hasPendingIrq() || bus.getMmio(0x560000A8)) return 1;
    if (bus.getMmio(0x56000064) & (1u << 9)) return 1;
    bus.setButtonMask(0);
    if ((bus.getMmio(0x560000A8) & ((1u << 17) | (1u << 19))) != ((1u << 17) | (1u << 19))) return 1;
    std::cout << "PASS: Shared host aliases create interrupts only on physical pin edges\n";
    ButtonPulses pulses;
    pulses.press(BTN_PLAY, 100, 180);
    pulses.press(BTN_FORWARD, 150, 1500);
    pulses.expire(280);
    if (pulses.mask() != BTN_FORWARD) return 1;
    pulses.expire(1649);
    if (pulses.mask() != BTN_FORWARD) return 1;
    pulses.expire(1650);
    if (pulses.mask()) return 1;
    pulses.press(BTN_STOP, 2000, 0);
    pulses.expire(2179);
    if (pulses.mask() != BTN_STOP) return 1;
    pulses.clear();
    if (pulses.mask()) return 1;
    std::cout << "PASS: Media pulses expire independently and focus loss clears them\n";
    Display input;
    input.setButtonState(BTN_A,true); input.setButtonState(BTN_A,false);
    if (input.consumeButtonMask()!=BTN_A || input.consumeButtonMask()!=0) return 1;
    std::cout << "PASS: Quick press and release retain separate guest transitions" << std::endl;
    input.setButtonState(BTN_A,true); input.setButtonState(BTN_A,true); input.setButtonState(BTN_A,false);
    if (input.consumeButtonMask()!=BTN_A || input.consumeButtonMask()!=0) return 1;
    std::cout << "PASS: Host key repeat does not duplicate transitions" << std::endl;
    input.setButtonState(BTN_A,true); input.setButtonState(BTN_B,true); input.releaseButtons();
    if (input.consumeButtonMask()!=BTN_A || input.consumeButtonMask()!=(BTN_A|BTN_B) || input.consumeButtonMask()!=0) return 1;
    std::cout << "PASS: Simultaneous buttons and focus loss retain ordered states" << std::endl;
    input.setButtonState(BTN_A,true); input.setButtonState(BTN_A,false); input.synchronizeButtons();
    if (input.consumeButtonMask()!=0) return 1;
    std::cout << "PASS: Paused input synchronization discards stale pulses" << std::endl;
    input.keyboardButton('Z',BTN_A,true); input.keyboardButton('K',BTN_A,true);
    input.keyboardButton('Z',BTN_A,false);
    if(input.getButtonMask()!=BTN_A) return 1;
    input.keyboardButton('K',BTN_A,false);
    if(input.getButtonMask()!=0) return 1;
    std::cout << "PASS: Releasing one keyboard alias does not release another held alias\n";
    std::cout << "\nKeypad, GPIO and input-transition tests PASSED!" << std::endl;
    return 0;
}
