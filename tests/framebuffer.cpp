#include "display/framebuffer.h"
#include "display/display_profile.h"
#include "memory/bus.h"
#include <iostream>
#include <algorithm>
#include <vector>

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
    using namespace oceanblast;
    const auto known = identifyDisplayProfile(17301504, 0x9bce041a);
    const uint8_t crcFixture[] = {'1','2','3','4','5','6','7','8','9'};
    check("ROM CRC uses standard IEEE CRC32", cartridgeCrc32(crcFixture, sizeof(crcFixture)) == 0xcbf43926);
    check("Unknown ROM and wrong-size dump do not inherit Crazy Jack profile",
        identifyDisplayProfile(17301504, 1) == DisplayProfile::Registers &&
        identifyDisplayProfile(16777216, 0x9bce041a) == DisplayProfile::Registers);
    auto layout = resolveFramebufferLayout(known, true, 0x30300000, 0x14c9, false, 360);
    check("Crazy Jack boot splash retains native packed rows", !layout.rgb565 && layout.stride == 360 && !layout.compatibility);
    layout = resolveFramebufferLayout(known, true, 0x302a0000, 0x579, true, 480, 0, 0, true);
    check("Crazy Jack game buffer automatically selects padded packed rows", !layout.rgb565 && layout.stride == 480 && layout.compatibility);
    layout = resolveFramebufferLayout(known, true, 0x30310000, 0x14c9, false, 360, 0, 0, true);
    check("Returning to splash restores native layout", !layout.rgb565 && layout.stride == 360 && !layout.compatibility);
    layout = resolveFramebufferLayout(DisplayProfile::Registers, true, 0x302a0000, 0x579, true, 480, 0, 0, true);
    check("Other cartridges retain RGB565 even with identical LCD registers", layout.rgb565 && !layout.compatibility);
    layout = resolveFramebufferLayout(known, false, 0x302a0000, 0x579, true, 480, 0, 0, true);
    check("Explicit LCD selection disables compatibility profile", layout.rgb565 && !layout.compatibility);
    layout = resolveFramebufferLayout(known, true, 0x302a0000, 0x579, true, 480, 2, 512, true);
    check("Explicit decoder and stride take precedence", layout.rgb565 && layout.stride == 512 && !layout.compatibility);
    layout = resolveFramebufferLayout(known, true, 0x302a0000, 0x579, true, 512, 0, 0, true);
    check("Changed scanline configuration does not trigger profile", layout.rgb565 && layout.stride == 512 && !layout.compatibility);
    layout = resolveFramebufferLayout(known, true, 0x302a0000, 0x579, true, 480);
    check("RGB565 loading logo retains register decoder before handoff", layout.rgb565 && !layout.compatibility);
    std::vector<uint8_t> frame(76800, 0);
    CrazyJackDisplayTransition transition;
    transition.observe(0x302a0000, 0x579, 480, frame.data());
    check("Initially empty framebuffer does not trigger game mode", !transition.active());
    for (size_t y = 0; y < 160; ++y) std::fill_n(frame.data() + y * 480, 360, 1);
    transition.observe(0x302a0000, 0x579, 480, frame.data());
    check("Nonempty loading logo does not trigger game mode", !transition.active());
    for (size_t y = 0; y < 160; ++y) { std::fill_n(frame.data() + y * 480, 360, 0); frame[y * 480 + 400] = 1; }
    transition.observe(0x302a0000, 0x579, 480, frame.data());
    check("Cleared active rows trigger handoff despite old row padding", transition.active());
    frame[0] = 1;
    transition.observe(0x302a0000, 0x579, 480, frame.data());
    check("Dark game frames retain selected layout", transition.active());
    transition.observe(0x30300000, 0x14c9, 360, frame.data());
    check("Changing LCD configuration resets handoff state", !transition.active());
    return failures ? 1 : 0;
}
