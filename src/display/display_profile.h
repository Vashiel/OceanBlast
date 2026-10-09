#pragma once
#include <cstddef>
#include <cstdint>

namespace oceanblast {
inline uint32_t cartridgeCrc32(const uint8_t* data, size_t size) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

enum class DisplayProfile { Registers, CrazyJack };
inline DisplayProfile identifyDisplayProfile(size_t size, uint32_t crc) {
    return size == 17301504 && crc == 0x9bce041a
        ? DisplayProfile::CrazyJack : DisplayProfile::Registers;
}

struct FramebufferLayout {
    bool rgb565;
    size_t stride;
    bool compatibility;
};

class CrazyJackDisplayTransition {
    bool sawBootImage = false;
    bool gameLayout = false;
public:
    bool active() const { return gameLayout; }
    void observe(uint32_t framebuffer, uint32_t lcdcon1, size_t stride,
                 const uint8_t* data) {
        if (framebuffer != 0x302a0000 || lcdcon1 != 0x579 || stride != 480 || !data) {
            sawBootImage = gameLayout = false;
            return;
        }
        if (gameLayout) return;
        // The RGB565 "3 Games" logo uses the same registers as game output.
        // Game startup clears the 360-byte active area of every padded row.
        size_t nonzero = 0;
        for (size_t y = 0; y < 160; ++y)
            for (size_t x = 0; x < 360; ++x) nonzero += data[y * stride + x] != 0;
        if (nonzero >= 4096) sawBootImage = true;
        else if (!nonzero && sawBootImage) gameLayout = true;
    }
};

inline FramebufferLayout resolveFramebufferLayout(DisplayProfile profile,
        bool automatic, uint32_t framebuffer, uint32_t lcdcon1,
        bool registerRgb565, size_t registerStride,
        int explicitFormat = 0, size_t explicitStride = 0, bool gameLayoutReady = false) {
    FramebufferLayout result{registerRgb565, registerStride, false};
    // This exact dump writes packed pixels into padded Linux framebuffer rows.
    // Boot splash buffers must retain their own native layout.
    if (automatic && gameLayoutReady && profile == DisplayProfile::CrazyJack &&
        framebuffer == 0x302a0000 && lcdcon1 == 0x579 && registerStride == 480) {
        result = {false, 480, true};
    }
    if (explicitFormat) { result.rgb565 = explicitFormat == 2; result.compatibility = false; }
    if (explicitStride) { result.stride = explicitStride; result.compatibility = false; }
    const size_t minimum = result.rgb565 ? 480 : 360;
    if (result.stride < minimum) result.stride = minimum;
    return result;
}
}
