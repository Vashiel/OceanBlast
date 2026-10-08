#include "display/framebuffer.h"
#include "memory/bus.h"
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok;
    };
    uint32_t pixels[4]{};
    const uint8_t packed[] = {0xf0, 0x00, 0xf0, 0x99, 0x99, 0x00, 0xff, 0xff};
    oceanblast::decodeFramebuffer(packed, pixels, false, 5, 2, 2);
    check("RGB444 scanline padding does not enter the next row", pixels[0] == 0xff0000 && pixels[1] == 0x00ff00 && pixels[2] == 0x0000ff && pixels[3] == 0xffffff);
    const uint8_t rgb565[] = {0x00, 0xf8, 0xe0, 0x07, 0x99, 0x99, 0x1f, 0x00, 0xff, 0xff};
    oceanblast::decodeFramebuffer(rgb565, pixels, true, 6, 2, 2);
    check("RGB565 scanline padding does not enter the next row", pixels[0] == 0xff0000 && pixels[1] == 0x00ff00 && pixels[2] == 0x0000ff && pixels[3] == 0xffffff);
    oceanblast::Bus bus; bus.reset();
    bus.write32(0x4d000000, 0x579);
    bus.write32(0x4d00001c, 240 | (16 << 11));
    check("LCD stride includes PAGEWIDTH and OFFSIZE in halfwords", bus.getFramebufferStride() == 512 && bus.getFramebufferSize() == 81920);
    bus.write32(0x4d000000, 0x14c9);
    bus.write32(0x4d00001c, 180);
    check("Packed RGB444 retains a 360-byte native stride", bus.getFramebufferStride() == 360 && bus.getFramebufferSize() == 57600);
    return failures ? 1 : 0;
}
