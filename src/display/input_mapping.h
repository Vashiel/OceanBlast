#pragma once
#include <array>
#include <cstdint>
#include "display.h"

namespace oceanblast {
namespace win {

// Host controller state representation across DirectInput and XInput devices.
struct HostPad {
    std::array<bool, 128> buttons{};
    std::array<int, 6> axes{};
    unsigned pov = 0xffffffffu;
};

constexpr std::array<uint32_t, 12> digiBlastBits{
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_A, BTN_B, BTN_C, BTN_C,
    BTN_L, BTN_R, BTN_SELECT, BTN_START
};

constexpr std::array<const wchar_t*, 12> actionNames{
    L"Oben", L"Unten", L"Links", L"Rechts",
    L"A", L"B", L"C (X)", L"C (Y)",
    L"L", L"R", L"Select", L"Start"
};

using PadMap = std::array<int, 12>;
constexpr PadMap standardMap{200, 201, 202, 203, 0, 1, 2, 3, 4, 5, 6, 7};

inline bool validPadToken(int token) {
    return (token >= 0 && token < 128) || (token >= 128 && token < 140) || (token >= 200 && token < 204);
}

inline bool hostPressed(const HostPad& p, int token) {
    if (token >= 0 && token < 128) return p.buttons[token];
    if (token >= 128 && token < 140) {
        const int axis = (token - 128) / 2;
        return (token & 1) ? p.axes[axis] > 12000 : p.axes[axis] < -12000;
    }
    if (token >= 200 && token <= 203) {
        const unsigned angle = p.pov;
        if (angle >= 36000) return false;
        if (token == 200) return angle >= 31500 || angle <= 4500;
        if (token == 201) return angle >= 13500 && angle <= 22500;
        if (token == 202) return angle >= 22500 && angle <= 31500;
        return angle >= 4500 && angle <= 13500;
    }
    return false;
}

inline uint32_t mapPad(const HostPad& p, const PadMap& map) {
    uint32_t result = 0;
    for (unsigned i = 0; i < map.size(); ++i) {
        if (hostPressed(p, map[i])) result |= digiBlastBits[i];
    }
    // Left analog stick provides directional input; Y is normalized down-positive.
    if (p.axes[0] < -12000) result |= BTN_LEFT;
    if (p.axes[0] > 12000)  result |= BTN_RIGHT;
    if (p.axes[1] < -12000) result |= BTN_UP;
    if (p.axes[1] > 12000)  result |= BTN_DOWN;
    // Analog triggers also drive media rewind / fast-forward when not otherwise bound.
    if (p.axes[4] > 12000)  result |= BTN_REWIND;
    if (p.axes[5] > 12000)  result |= BTN_FORWARD;
    return result;
}

inline int firstNewControl(const HostPad& now, const HostPad& before) {
    for (int i = 0; i < 128; ++i)
        if (hostPressed(now, i) && !hostPressed(before, i)) return i;
    for (int i = 200; i <= 203; ++i)
        if (hostPressed(now, i) && !hostPressed(before, i)) return i;
    for (int i = 128; i < 140; ++i)
        if (hostPressed(now, i) && !hostPressed(before, i)) return i;
    return -1;
}

} // namespace win
} // namespace oceanblast
