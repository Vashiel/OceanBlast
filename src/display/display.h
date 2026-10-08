#ifndef OCEANBLAST_DISPLAY_H
#define OCEANBLAST_DISPLAY_H

#include <cstdint>
#include <string>
#include <vector>
#include <deque>

namespace oceanblast {

// digiBLAST native resolution
constexpr int LCD_WIDTH = 240;
constexpr int LCD_HEIGHT = 160;

// Button flags
enum Button {
    BTN_UP     = 1 << 0,
    BTN_DOWN   = 1 << 1,
    BTN_LEFT   = 1 << 2,
    BTN_RIGHT  = 1 << 3,
    BTN_A      = 1 << 4,
    BTN_B      = 1 << 5,
    BTN_L      = 1 << 6,
    BTN_R      = 1 << 7,
    BTN_START  = 1 << 8,
    BTN_SELECT = 1 << 9
};

class Display {
public:
    Display(int scale = 3);
    ~Display();

    bool init(const char* title = "OceanBlast - Nikko digiBLAST Emulator");
    void processEvents();
    void updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr, bool is16bpp = true, size_t stride = 0);
    void renderToDc(void* targetHdc);
    bool isOpen() const { return m_open; }
    void close();
    void setTitle(const std::string& title);
    bool paused = false;
    bool singleStep = false;
    bool snapshot = false;

    uint32_t getButtonMask() const { return m_buttonMask; }
    void setButtonState(uint32_t mask, bool down) {
        const uint32_t next = down ? m_buttonMask | mask : m_buttonMask & ~mask;
        if (next != m_buttonMask) { m_buttonMask = next; m_buttonTransitions.push_back(next); }
    }
    uint32_t consumeButtonMask() {
        if (!m_buttonTransitions.empty()) {
            m_guestButtonMask = m_buttonTransitions.front(); m_buttonTransitions.pop_front();
        }
        return m_guestButtonMask;
    }
    void synchronizeButtons() { m_buttonTransitions.clear(); m_guestButtonMask = m_buttonMask; }
    void releaseButtons() {
        if (m_buttonMask) { m_buttonMask = 0; m_buttonTransitions.push_back(0); }
    }

private:
    int m_scale;
    bool m_open;
    uint32_t m_buttonMask;
    uint32_t m_guestButtonMask = 0;
    std::deque<uint32_t> m_buttonTransitions;

    void* m_hwnd;
    void* m_hdc;
    void* m_bitmapInfo;
    std::vector<uint32_t> m_pixels; // 240x160 32-bit XRGB
};

} // namespace oceanblast

#endif // OCEANBLAST_DISPLAY_H
