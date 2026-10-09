#ifndef OCEANBLAST_DISPLAY_H
#define OCEANBLAST_DISPLAY_H

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <array>

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
    BTN_SELECT = 1 << 9,
    BTN_C = 1 << 10,
    BTN_REWIND = 1 << 11,
    BTN_FORWARD = 1 << 12
};

class Display {
public:
    Display(int scale = 3);
    ~Display();

    bool init(const char* title = "OceanBlast - Nikko digiBLAST Emulator");
    void processEvents();
    void updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr, bool is16bpp = true, size_t stride = 0, unsigned sourceHeight = LCD_HEIGHT);
    void updateFrameData(const uint8_t* source, bool is16bpp, size_t stride, unsigned sourceHeight);
    const std::vector<uint32_t>& framePixels() const { return m_pixels; }
    unsigned frameSourceHeight() const { return m_sourceHeight; }
    void renderToDc(void* targetHdc);
    void configureWindow(bool skin, bool fullscreen = false) { m_skinEnabled = skin; m_startFullscreen = fullscreen; }
    void toggleFullscreen();
    bool fullscreen() const { return m_fullscreen; }
    void refreshLayout();
    void paintShell(void* dc);
    void mouseButton(int x, int y, bool down);
    void refreshControls();
    void keyboardButton(unsigned key, uint32_t mask, bool down);
    void updateHostButtons();
    void useGdiPresentation(bool value) { m_gdiOnly = value; }
    bool usesSyncedPresentation() const;
    uint64_t presentedFrames() const;
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
        m_keyboardButtons.fill(0); m_mouseMask = 0;
        if (m_buttonMask) { m_buttonMask = 0; m_buttonTransitions.push_back(0); }
    }

private:
    bool m_skinEnabled = false, m_startFullscreen = false, m_fullscreen = false;
    void* m_skin = nullptr;
    void* m_lcdWindow = nullptr;
    int m_outputWidth = 0, m_outputHeight = 0;
    int m_savedX = 0, m_savedY = 0, m_savedW = 0, m_savedH = 0;
    uint32_t m_mouseMask = 0;
    std::array<uint32_t,256> m_keyboardButtons{};
    uint64_t m_previousPresented = 0;
    unsigned m_sourceHeight = LCD_HEIGHT;
    int m_scale;
    bool m_open;
    uint32_t m_buttonMask;
    uint32_t m_guestButtonMask = 0;
    std::deque<uint32_t> m_buttonTransitions;

    void* m_hwnd;
    void* m_hdc;
    void* m_bitmapInfo;
    void* m_presenter = nullptr;
    bool m_gdiOnly = false;
    std::vector<uint32_t> m_pixels; // 240x160 32-bit XRGB
};

} // namespace oceanblast

#endif // OCEANBLAST_DISPLAY_H
