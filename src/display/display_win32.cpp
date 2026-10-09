#include "display.h"
#include "framebuffer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include "presenter_win32.h"

namespace oceanblast {

static Display* g_currentDisplay = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_currentDisplay) {
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!g_currentDisplay->usesSyncedPresentation()) g_currentDisplay->renderToDc(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        } else if (msg == WM_ERASEBKGND) {
            return 1;
        } else if (msg == WM_KILLFOCUS) {
            g_currentDisplay->releaseButtons();
            return 0;
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

    if (!m_gdiOnly) {
        auto* presenter = new SyncedPresenter();
        if (presenter->init(hwnd, LCD_WIDTH * m_scale, LCD_HEIGHT * m_scale)) {
            m_presenter = presenter;
            std::cout << "[Display] DXGI flip presentation with vertical synchronization.\n";
        } else {
            delete presenter;
            std::cout << "[Display] DXGI unavailable; using GDI fallback.\n";
        }
    }
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

void Display::updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr, bool is16bpp, size_t stride, unsigned sourceHeight) {
    if (!m_open || !m_hwnd || !m_hdc || !sdram) return;

    constexpr uint32_t SDRAM_BASE = 0x30000000;
    constexpr uint32_t SDRAM_SIZE = 32 * 1024 * 1024;

    const size_t rowBytes = is16bpp ? 480 : 360;
    if (!stride) stride = rowBytes;
    if (!sourceHeight || sourceHeight > 1024 || stride < rowBytes || stride > SDRAM_SIZE / sourceHeight) return;
    const size_t fbSize = stride * sourceHeight;
    if (fbPhysAddr < SDRAM_BASE || (fbPhysAddr - SDRAM_BASE) + fbSize > SDRAM_SIZE) {
        return;
    }

    uint32_t offset = fbPhysAddr - SDRAM_BASE;
    const uint8_t* src = sdram + offset;
    updateFrameData(src, is16bpp, stride, sourceHeight);
}

void Display::updateFrameData(const uint8_t* src, bool is16bpp, size_t stride, unsigned sourceHeight) {
    if (!m_open || !src || !sourceHeight || sourceHeight > 1024 || stride < (is16bpp ? 480u : 360u)) return;
    m_sourceHeight = sourceHeight;
    m_pixels.resize(LCD_WIDTH * sourceHeight);
    static_cast<BITMAPINFO*>(m_bitmapInfo)->bmiHeader.biHeight = -static_cast<LONG>(sourceHeight);
    decodeFramebuffer(src, m_pixels.data(), is16bpp, stride, LCD_WIDTH, sourceHeight);

    if (m_presenter && !usesSyncedPresentation()) {
        std::cout << "[Display] DXGI presentation failed; using GDI fallback.\n";
        delete static_cast<SyncedPresenter*>(m_presenter); m_presenter = nullptr;
    }
    if (usesSyncedPresentation()) static_cast<SyncedPresenter*>(m_presenter)->submit(m_pixels, sourceHeight);
    else renderToDc(m_hdc);
}

bool Display::usesSyncedPresentation() const {
    return m_presenter && static_cast<SyncedPresenter*>(m_presenter)->healthy();
}
uint64_t Display::presentedFrames() const {
    return m_presenter ? static_cast<SyncedPresenter*>(m_presenter)->presented() : 0;
}

void Display::renderToDc(void* targetHdc) {
    if (!targetHdc || !m_bitmapInfo) return;

    HDC hdc = static_cast<HDC>(targetHdc);
    BITMAPINFO* bmi = static_cast<BITMAPINFO*>(m_bitmapInfo);

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(
        hdc,
        0, 0, LCD_WIDTH * m_scale, LCD_HEIGHT * m_scale,
        0, 0, LCD_WIDTH, m_sourceHeight,
        m_pixels.data(),
        bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void Display::close() {
    if (!m_open && !m_hwnd && !m_presenter) return;
    m_open = false;
    delete static_cast<SyncedPresenter*>(m_presenter); m_presenter = nullptr;

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
void Display::updateFrame(const uint8_t*, uint32_t, bool, size_t, unsigned) {}
void Display::updateFrameData(const uint8_t*, bool, size_t, unsigned) {}
bool Display::usesSyncedPresentation() const { return false; }
uint64_t Display::presentedFrames() const { return 0; }
void Display::close() {}
}

#endif
