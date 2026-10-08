#pragma once
#include <cstddef>
#include <cstdint>

namespace oceanblast {
inline void decodeFramebuffer(const uint8_t* source, uint32_t* pixels, bool rgb565,
                              size_t stride, unsigned width = 240, unsigned height = 160) {
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = source + y * stride;
        for (unsigned x = 0; x < width; ++x) {
            uint32_t r, g, b;
            if (rgb565) {
                const uint16_t value = row[x * 2] | (uint16_t(row[x * 2 + 1]) << 8);
                r = value >> 11; g = (value >> 5) & 63; b = value & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            } else {
                const auto* pair = row + (x / 2) * 3;
                const uint16_t value = x & 1 ? ((pair[1] & 15) << 8) | pair[2]
                                             : (pair[0] << 4) | (pair[1] >> 4);
                r = ((value >> 8) & 15) * 17; g = ((value >> 4) & 15) * 17; b = (value & 15) * 17;
            }
            pixels[y * width + x] = (r << 16) | (g << 8) | b;
        }
    }
}
}
