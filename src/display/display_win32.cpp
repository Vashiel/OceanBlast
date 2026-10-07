#include "display.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>

namespace oceanblast {

static Display* g_currentDisplay = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_currentDisplay) {
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            g_currentDisplay->renderToDc(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        } else if (msg == WM_ERASEBKGND) {
            return 1;
        } else if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
            bool isDown = (msg == WM_KEYDOWN);
            uint32_t mask = 0;
            switch (wParam) {
                case VK_F5: if (isDown && !(lParam & (1L << 30))) g_currentDisplay->paused = !g_currentDisplay->paused; return 0;
                case VK_F6: if (isDown) { g_currentDisplay->paused = true; g_currentDisplay->singleStep = true; } return 0;
                case VK_F7: if (isDown) g_currentDisplay->snapshot = true; return 0;
                case VK_UP:     mask = BTN_UP; break;
                case VK_DOWN:   mask = BTN_DOWN; break;
                case VK_LEFT:   mask = BTN_LEFT; break;
                case VK_RIGHT:  mask = BTN_RIGHT; break;
                case 'Z': case 'K': mask = BTN_A; break;
                case 'X': case 'J': mask = BTN_B; break;
                case 'A': case 'Q': mask = BTN_L; break;
                case 'S': case 'W': mask = BTN_R; break;
                case VK_RETURN: mask = BTN_START; break;
                case VK_SPACE:  mask = BTN_SELECT; break;
                case VK_ESCAPE:
                    if (isDown) g_currentDisplay->close();
                    return 0;
                default: break;
            }
            if (mask != 0) {
                g_currentDisplay->setButtonState(mask, isDown);
            }
        } else if (msg == WM_CLOSE) {
            g_currentDisplay->close();
            return 0;
        } else if (msg == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

Display::Display(int scale)
    : m_scale(scale), m_open(false), m_buttonMask(0),
      m_hwnd(nullptr), m_hdc(nullptr), m_bitmapInfo(nullptr) {
    m_pixels.resize(LCD_WIDTH * LCD_HEIGHT, 0);
}

Display::~Display() {
    close();
}

bool Display::init(const char* title) {
    g_currentDisplay = this;

    HINSTANCE hInstance = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = "OceanBlastDisplayClass";

    RegisterClassExA(&wc);

    int clientW = LCD_WIDTH * m_scale;
    int clientH = LCD_HEIGHT * m_scale;

    RECT rect = { 0, 0, clientW, clientH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        "OceanBlastDisplayClass",
        title,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        std::cerr << "[Display] Failed to create Win32 window." << std::endl;
        return false;
    }

    m_hwnd = static_cast<void*>(hwnd);
    m_hdc = static_cast<void*>(GetDC(hwnd));

    BITMAPINFO* bmi = new BITMAPINFO();
    ZeroMemory(bmi, sizeof(BITMAPINFO));
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = LCD_WIDTH;
    bmi->bmiHeader.biHeight = -LCD_HEIGHT; // Top-down
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_RGB;
    m_bitmapInfo = static_cast<void*>(bmi);

    m_open = true;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    std::cout << "[Display] Live display window initialized (" << clientW << "x" << clientH << ", "
              << m_scale << "x scale)." << std::endl;
    return true;
}

void Display::processEvents() {
    if (!m_open || !m_hwnd) return;

    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_open = false;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void Display::setTitle(const std::string& title) {
    if (m_hwnd) SetWindowTextA(static_cast<HWND>(m_hwnd), title.c_str());
}

void Display::updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr) {
    if (!m_open || !m_hwnd || !m_hdc || !sdram) return;

    constexpr uint32_t SDRAM_BASE = 0x30000000;
    constexpr uint32_t SDRAM_SIZE = 32 * 1024 * 1024;

    if (fbPhysAddr < SDRAM_BASE || (fbPhysAddr - SDRAM_BASE) + 57600 > SDRAM_SIZE) {
        return;
    }

    uint32_t offset = fbPhysAddr - SDRAM_BASE;
    const uint8_t* src = sdram + offset;

    // Decode 240x160 12-bit packed LCD444 (3 bytes -> 2 pixels)
    int pixelIdx = 0;
    for (int i = 0; i < 57600; i += 3) {
        uint8_t b0 = src[i];
        uint8_t b1 = src[i + 1];
        uint8_t b2 = src[i + 2];

        uint32_t p0 = (b0 << 4) | (b1 >> 4);
        uint32_t p1 = ((b1 & 0x0F) << 8) | b2;

        uint32_t r0 = ((p0 >> 8) & 0x0F) * 17;
        uint32_t g0 = ((p0 >> 4) & 0x0F) * 17;
        uint32_t b0_val = (p0 & 0x0F) * 17;

        uint32_t r1 = ((p1 >> 8) & 0x0F) * 17;
        uint32_t g1 = ((p1 >> 4) & 0x0F) * 17;
        uint32_t b1_val = (p1 & 0x0F) * 17;

        m_pixels[pixelIdx++] = (r0 << 16) | (g0 << 8) | b0_val;
        m_pixels[pixelIdx++] = (r1 << 16) | (g1 << 8) | b1_val;
    }

    renderToDc(m_hdc);
}

void Display::renderToDc(void* targetHdc) {
    if (!targetHdc || !m_bitmapInfo) return;

    HDC hdc = static_cast<HDC>(targetHdc);
    BITMAPINFO* bmi = static_cast<BITMAPINFO*>(m_bitmapInfo);

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(
        hdc,
        0, 0, LCD_WIDTH * m_scale, LCD_HEIGHT * m_scale,
        0, 0, LCD_WIDTH, LCD_HEIGHT,
        m_pixels.data(),
        bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void Display::close() {
    if (!m_open) return;
    m_open = false;

    if (m_hdc && m_hwnd) {
        ReleaseDC(static_cast<HWND>(m_hwnd), static_cast<HDC>(m_hdc));
        m_hdc = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(static_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }
    if (m_bitmapInfo) {
        delete static_cast<BITMAPINFO*>(m_bitmapInfo);
        m_bitmapInfo = nullptr;
    }
}

} // namespace oceanblast

#else

// Fallback stub for non-Windows platforms
namespace oceanblast {
Display::Display(int scale) : m_scale(scale), m_open(false), m_buttonMask(0), m_hwnd(nullptr), m_hdc(nullptr), m_bitmapInfo(nullptr) {}
Display::~Display() {}
bool Display::init(const char*) { return false; }
void Display::setTitle(const std::string&) {}
void Display::processEvents() {}
void Display::updateFrame(const uint8_t*, uint32_t) {}
void Display::close() {}
}

#endif
