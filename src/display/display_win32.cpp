#include "display.h"
#include "framebuffer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include "presenter_win32.h"
#include "console_skin_win32.h"
#include "windows_input.h"
#include <windowsx.h>

namespace oceanblast {

static Display* g_currentDisplay = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_currentDisplay) {
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            g_currentDisplay->paintShell(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        } else if (msg == WM_ERASEBKGND) {
            return 1;
        } else if (msg == WM_SIZE) {
            g_currentDisplay->refreshLayout();
            return 0;
        } else if (msg == WM_DEVICECHANGE) {
            g_currentDisplay->onDeviceChange();
            return 0;
        } else if (msg == WM_APP + 1) {
            g_currentDisplay->reloadUserSettings();
            g_currentDisplay->refreshLayout();
            return 0;
        } else if (msg == WM_KILLFOCUS) {
            g_currentDisplay->mouseButton(0, 0, false);
            g_currentDisplay->releaseButtons();
            g_currentDisplay->refreshControls();
            return 0;
        } else if (msg == WM_LBUTTONDOWN) {
            SetFocus(hwnd); SetCapture(hwnd);
            g_currentDisplay->mouseButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true); return 0;
        } else if (msg == WM_MOUSEMOVE && (wParam & MK_LBUTTON) && GetCapture() == hwnd) {
            g_currentDisplay->mouseButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true); return 0;
        } else if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) {
            g_currentDisplay->mouseButton(0, 0, false);
            if (msg == WM_LBUTTONUP) ReleaseCapture();
            return 0;
        } else if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
            bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            if (isDown && (GetKeyState(VK_CONTROL) & 0x8000) && g_currentDisplay->parentWindow()) {
                if (wParam == 'O') {
                    PostMessageW(reinterpret_cast<HWND>(g_currentDisplay->parentWindow()), WM_COMMAND, 100, 0);
                    return 0;
                }
                if (wParam == 'R') {
                    PostMessageW(reinterpret_cast<HWND>(g_currentDisplay->parentWindow()), WM_COMMAND, 101, 0);
                    return 0;
                }
            }
            if ((wParam == VK_F11 || (wParam == VK_RETURN && (lParam & (1L << 29))))) {
                if (isDown && !(lParam & (1L << 30))) {
                    if (g_currentDisplay->parentWindow())
                        PostMessageW(reinterpret_cast<HWND>(g_currentDisplay->parentWindow()), WM_COMMAND, 103, 0);
                    else
                        g_currentDisplay->toggleFullscreen();
                }
                return 0;
            }
            uint32_t mask = g_currentDisplay->mapConfiguredKeyboardKey(static_cast<unsigned>(wParam));
            switch (wParam) {
                case VK_F5: if (isDown && !(lParam & (1L << 30))) g_currentDisplay->paused = !g_currentDisplay->paused; return 0;
                case VK_F6: if (isDown) { g_currentDisplay->paused = true; g_currentDisplay->singleStep = true; } return 0;
                case VK_F7: if (isDown) g_currentDisplay->snapshot = true; return 0;
                case VK_UP:     mask |= BTN_UP; break;
                case VK_DOWN:   mask |= BTN_DOWN; break;
                case VK_LEFT:   mask |= BTN_LEFT; break;
                case VK_RIGHT:  mask |= BTN_RIGHT; break;
                case 'Z': case 'K': mask |= BTN_A; break;
                case 'X': case 'J': mask |= BTN_B; break;
                case 'C': mask |= BTN_C; break;
                case VK_F8: mask |= BTN_REWIND; break;
                case VK_F9: mask |= BTN_START; break;
                case VK_F10: mask |= BTN_SELECT; break;
                case VK_F12: mask |= BTN_FORWARD; break;
                case 'A': case 'Q': mask |= BTN_L; break;
                case 'S': case 'W': mask |= BTN_R; break;
                case VK_RETURN: mask |= BTN_START; break;
                case VK_SPACE:  mask |= BTN_SELECT; break;
                case VK_ESCAPE:
                    if (isDown && !(lParam & (1L << 30))) {
                        if (g_currentDisplay->parentWindow()) {
                            PostMessageW(reinterpret_cast<HWND>(g_currentDisplay->parentWindow()), WM_COMMAND, 104, 0);
                        } else if (g_currentDisplay->fullscreen()) {
                            g_currentDisplay->toggleFullscreen();
                        } else {
                            g_currentDisplay->close();
                        }
                    }
                    return 0;
                default: break;
            }
            if (mask != 0) {
                g_currentDisplay->keyboardButton(static_cast<unsigned>(wParam), mask, isDown);
                return 0;
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

static LRESULT CALLBACK LcdProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; auto dc = BeginPaint(hwnd, &ps);
        if (g_currentDisplay && !g_currentDisplay->usesSyncedPresentation()) g_currentDisplay->renderToDc(dc);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_DESTROY || msg == WM_CLOSE) return 0;
    if (msg == WM_LBUTTONDOWN) { SetFocus(GetParent(hwnd)); return 0; }
    if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) return 0;
    return WndProc(hwnd, msg, wp, lp);
}

Display::Display(int scale)
    : m_scale(scale), m_open(false), m_buttonMask(0),
      m_hwnd(nullptr), m_hdc(nullptr), m_bitmapInfo(nullptr) {
    m_pixels.resize(LCD_WIDTH * LCD_HEIGHT, 0);
}

Display::~Display() {
    close();
}

uint32_t Display::mapConfiguredKeyboardKey(unsigned key) const {
    uint32_t mask = 0;
    for (unsigned i = 0; i < 12; ++i) {
        if (key == m_keyboardKeys[i]) mask |= win::digiBlastBits[i];
    }
    return mask;
}

void Display::reloadUserSettings() {
    win::FrontendSettings settings;
    settings.load();
    m_videoAspect = settings.videoAspect;
    m_integerScale = settings.integerScale;
    m_smoothVideo = settings.smoothVideo;
    m_audioVolume = settings.soundEnabled ? settings.soundVolume : 0;
    m_audioEnabled = settings.soundEnabled;
    m_keyboardKeys = settings.keyboardKeys;
    if (m_inputManager) {
        auto* mgr = static_cast<win::InputManager*>(m_inputManager);
        mgr->refresh();
        std::wstring preferred = settings.savedController;
        mgr->autoSelectPreferred(preferred);
        settings.loadControllerFor(mgr->key());
        m_padMap = settings.padMap;
    } else {
        m_padMap = settings.padMap;
    }
    if (m_parentHwnd) {
        const bool wantSkin = (settings.windowMode == 0);
        if (wantSkin && !m_skin) {
            auto* skin = new ConsoleSkin();
            if (skin->image) { m_skin = skin; m_skinEnabled = true; }
            else delete skin;
        } else if (!wantSkin && m_skin) {
            delete static_cast<ConsoleSkin*>(m_skin);
            m_skin = nullptr;
            m_skinEnabled = false;
        }
    }
}

void Display::onDeviceChange() {
    if (!m_inputManager) return;
    reloadUserSettings();
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
    if (m_skinEnabled) {
        auto* skin = new ConsoleSkin();
        if (skin->image) {
            m_skin = skin;
            clientW = std::min(1536, LCD_WIDTH * m_scale * 2);
            clientH = clientW * 2 / 3;
            RECT work{};
            SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
            const int maximum = std::min((work.right - work.left) * 9 / 10, (work.bottom - work.top - 60) * 3 / 2);
            clientW = std::min(clientW, maximum);
            clientH = clientW * 2 / 3;
        } else {
            delete skin;
            m_skinEnabled = false;
            std::cout << "[Display] Skin asset unavailable; using plain window.\n";
        }
    }

    HWND parent = reinterpret_cast<HWND>(m_parentHwnd);
    HWND hwnd = nullptr;
    if (parent && IsWindow(parent)) {
        RECT parentRect{};
        GetClientRect(parent, &parentRect);
        clientW = std::max<int>(240, parentRect.right - parentRect.left);
        clientH = std::max<int>(160, parentRect.bottom - parentRect.top);
        DWORD childStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
        hwnd = CreateWindowExA(
            0,
            "OceanBlastDisplayClass",
            title,
            childStyle,
            0, 0,
            clientW, clientH,
            parent, nullptr, hInstance, nullptr
        );
    } else {
        m_parentHwnd = 0;
        RECT rect = { 0, 0, clientW, clientH };
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_CLIPCHILDREN;
        AdjustWindowRect(&rect, style, FALSE);

        hwnd = CreateWindowExA(
            0,
            "OceanBlastDisplayClass",
            title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr, nullptr, hInstance, nullptr
        );
    }

    if (!hwnd) {
        std::cerr << "[Display] Failed to create Win32 window." << std::endl;
        return false;
    }

    m_hwnd = static_cast<void*>(hwnd);
    if (!m_parentHwnd) {
        Gdiplus::GdiplusStartupInput gdipInput;
        ULONG_PTR gdipToken = 0;
        if (Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr) == Gdiplus::Ok) {
            IStream* symStream = nullptr;
            auto sym = win::loadImageAsset(102, L"oceanblast-symbol.png", &symStream);
            if (sym) {
                HICON icon = nullptr;
                if (static_cast<Gdiplus::Bitmap*>(sym.get())->GetHICON(&icon) == Gdiplus::Ok && icon) {
                    m_appIcon = static_cast<void*>(icon);
                    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                }
            }
            sym.reset();
            if (symStream) symStream->Release();
            Gdiplus::GdiplusShutdown(gdipToken);
        }
    }

    WNDCLASSA lcdClass{};
    lcdClass.lpfnWndProc = LcdProc;
    lcdClass.hInstance = hInstance;
    lcdClass.lpszClassName = "OceanBlastLcdClass";
    lcdClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&lcdClass);
    m_lcdWindow = CreateWindowA(lcdClass.lpszClassName, "", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr, hInstance, nullptr);
    if (!m_lcdWindow) { close(); return false; }
    m_hdc = static_cast<void*>(GetDC(static_cast<HWND>(m_lcdWindow)));

    BITMAPINFO* bmi = new BITMAPINFO();
    ZeroMemory(bmi, sizeof(BITMAPINFO));
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = LCD_WIDTH;
    bmi->bmiHeader.biHeight = -LCD_HEIGHT; // Top-down
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_RGB;
    m_bitmapInfo = static_cast<void*>(bmi);

    auto* inputMgr = new win::InputManager();
    inputMgr->init(m_parentHwnd ? reinterpret_cast<HWND>(m_parentHwnd) : hwnd);
    m_inputManager = inputMgr;
    reloadUserSettings();

    m_open = true;
    refreshLayout();
    if (m_startFullscreen && !m_parentHwnd) toggleFullscreen();
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

    if (m_open && m_inputManager) {
        auto* mgr = static_cast<win::InputManager*>(m_inputManager);
        const auto pad = mgr->poll();
        gamepadButtons(win::mapPad(pad, m_padMap));
    }
}

void Display::setTitle(const std::string& title) {
    if (m_parentHwnd && IsWindow(reinterpret_cast<HWND>(m_parentHwnd))) {
        SetWindowTextA(reinterpret_cast<HWND>(m_parentHwnd), title.c_str());
    } else if (m_hwnd) {
        SetWindowTextA(static_cast<HWND>(m_hwnd), title.c_str());
    }
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
        m_previousPresented += static_cast<SyncedPresenter*>(m_presenter)->presented();
        delete static_cast<SyncedPresenter*>(m_presenter); m_presenter = nullptr;
    }
    if (usesSyncedPresentation()) static_cast<SyncedPresenter*>(m_presenter)->submit(m_pixels, sourceHeight);
    else renderToDc(m_hdc);
}

bool Display::usesSyncedPresentation() const {
    return m_presenter && static_cast<SyncedPresenter*>(m_presenter)->healthy();
}
uint64_t Display::presentedFrames() const {
    return m_previousPresented + (m_presenter ? static_cast<SyncedPresenter*>(m_presenter)->presented() : 0);
}

void Display::refreshLayout() {
    if (!m_hwnd || !m_lcdWindow) return;
    if (m_presenter) m_previousPresented += static_cast<SyncedPresenter*>(m_presenter)->presented();
    delete static_cast<SyncedPresenter*>(m_presenter); m_presenter = nullptr;
    RECT area{}; GetClientRect(static_cast<HWND>(m_hwnd), &area);
    RECT screen = area;
    if (m_skin && !m_fullscreen) {
        screen = ConsoleSkin::lcd(area.right, area.bottom);
        int width = screen.right - screen.left, height = screen.bottom - screen.top;
        if (width * 2 > height * 3) { int fit = height * 3 / 2; screen.left += (width - fit) / 2; width = fit; }
        else { int fit = width * 2 / 3; screen.top += (height - fit) / 2; height = fit; }
        m_outputWidth = std::max(1, width); m_outputHeight = std::max(1, height);
    } else {
        int cw = std::max<int>(1, area.right - area.left);
        int ch = std::max<int>(1, area.bottom - area.top);
        int width = cw, height = ch;
        if (m_integerScale && cw >= LCD_WIDTH && ch >= LCD_HEIGHT) {
            const int s = std::max(1, std::min(cw / LCD_WIDTH, ch / LCD_HEIGHT));
            width = LCD_WIDTH * s;
            height = LCD_HEIGHT * s;
        } else if (m_videoAspect == 1) {
            // 4:3 aspect
            if (cw * 3 > ch * 4) width = ch * 4 / 3;
            else height = cw * 3 / 4;
        } else if (m_videoAspect != 2) {
            // Preserve the native 3:2 LCD aspect by default.
            if (cw * 2 > ch * 3) width = ch * 3 / 2;
            else height = cw * 2 / 3;
        }
        screen.left = (cw - width) / 2;
        screen.top = (ch - height) / 2;
        m_outputWidth = std::max(1, width);
        m_outputHeight = std::max(1, height);
    }
    SetWindowPos(static_cast<HWND>(m_lcdWindow), nullptr, screen.left, screen.top, m_outputWidth, m_outputHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!m_gdiOnly && !m_smoothVideo) {
        auto* presenter = new SyncedPresenter();
        if (presenter->init(static_cast<HWND>(m_lcdWindow), m_outputWidth, m_outputHeight)) m_presenter = presenter;
        else delete presenter;
    }
    if (usesSyncedPresentation()) static_cast<SyncedPresenter*>(m_presenter)->submit(m_pixels, m_sourceHeight);
    InvalidateRect(static_cast<HWND>(m_hwnd), nullptr, FALSE);
    InvalidateRect(static_cast<HWND>(m_lcdWindow), nullptr, FALSE);
}
void Display::toggleFullscreen() {
    if (!m_hwnd) return;
    HWND window = static_cast<HWND>(m_hwnd);
    if (!m_fullscreen) {
        RECT old{}; GetWindowRect(window, &old);
        m_savedX = old.left; m_savedY = old.top; m_savedW = old.right - old.left; m_savedH = old.bottom - old.top;
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        m_fullscreen = true;
        SetWindowLongPtr(window, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        m_fullscreen = false;
        SetWindowLongPtr(window, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(window, HWND_NOTOPMOST, m_savedX, m_savedY, m_savedW, m_savedH, SWP_FRAMECHANGED);
    }
    refreshLayout();
}
void Display::paintShell(void* target) {
    if (!target || !m_hwnd) return;
    RECT area{}; GetClientRect(static_cast<HWND>(m_hwnd), &area);
    if (m_skin && !m_fullscreen) static_cast<ConsoleSkin*>(m_skin)->paint(static_cast<HDC>(target), area.right, area.bottom, m_buttonMask);
    else FillRect(static_cast<HDC>(target), &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
}
void Display::refreshControls() {
    if (m_hwnd && m_skin && !m_fullscreen) InvalidateRect(static_cast<HWND>(m_hwnd), nullptr, FALSE);
}
void Display::keyboardButton(unsigned key, uint32_t mask, bool down) {
    if (key >= m_keyboardButtons.size()) return;
    m_keyboardButtons[key] = down ? mask : 0;
    updateHostButtons();
}
void Display::gamepadButtons(uint32_t mask) {
    if (m_gamepadMask != mask) {
        m_gamepadMask = mask;
        updateHostButtons();
    }
}
void Display::updateHostButtons() {
    uint32_t next = m_mouseMask | m_gamepadMask;
    for (auto key : m_keyboardButtons) next |= key;
    if (next != m_buttonMask) { m_buttonMask = next; m_buttonTransitions.push_back(next); refreshControls(); }
}
void Display::mouseButton(int x, int y, bool down) {
    uint32_t next = 0;
    if (down && m_skin && !m_fullscreen) {
        RECT area{}; GetClientRect(static_cast<HWND>(m_hwnd), &area);
        next = static_cast<ConsoleSkin*>(m_skin)->hit(x, y, area.right, area.bottom);
    }
    m_mouseMask = next;
    updateHostButtons();
}

void Display::renderToDc(void* targetHdc) {
    if (!targetHdc || !m_bitmapInfo) return;

    HDC hdc = static_cast<HDC>(targetHdc);
    BITMAPINFO* bmi = static_cast<BITMAPINFO*>(m_bitmapInfo);

    SetStretchBltMode(hdc, m_smoothVideo ? HALFTONE : COLORONCOLOR);
    if (m_smoothVideo) SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(
        hdc,
        0, 0, m_outputWidth, m_outputHeight,
        0, 0, LCD_WIDTH, m_sourceHeight,
        m_pixels.data(),
        bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void Display::close() {
    if (!m_open && !m_hwnd && !m_presenter && !m_skin && !m_bitmapInfo && !m_inputManager) return;
    m_open = false;
    delete static_cast<SyncedPresenter*>(m_presenter); m_presenter = nullptr;
    delete static_cast<win::InputManager*>(m_inputManager); m_inputManager = nullptr;

    if (m_hdc && m_hwnd) {
        ReleaseDC(static_cast<HWND>(m_lcdWindow), static_cast<HDC>(m_hdc));
        m_hdc = nullptr;
    }
    if (m_lcdWindow) { DestroyWindow(static_cast<HWND>(m_lcdWindow)); m_lcdWindow = nullptr; }
    if (m_hwnd) {
        DestroyWindow(static_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }
    if (m_appIcon) {
        DestroyIcon(static_cast<HICON>(m_appIcon));
        m_appIcon = nullptr;
    }
    delete static_cast<ConsoleSkin*>(m_skin); m_skin = nullptr;
    if (g_currentDisplay == this) g_currentDisplay = nullptr;
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
void Display::reloadUserSettings() {}
uint32_t Display::mapConfiguredKeyboardKey(unsigned key) const { (void)key; return 0; }
void Display::onDeviceChange() {}
void Display::toggleFullscreen() {}
void Display::refreshLayout() {}
void Display::paintShell(void*) {}
void Display::mouseButton(int, int, bool) {}
void Display::refreshControls() {}
void Display::keyboardButton(unsigned key, uint32_t mask, bool down) { if (key < m_keyboardButtons.size()) { m_keyboardButtons[key] = down ? mask : 0; updateHostButtons(); } }
void Display::gamepadButtons(uint32_t mask) { m_gamepadMask = mask; updateHostButtons(); }
void Display::updateHostButtons() { uint32_t next = m_mouseMask | m_gamepadMask; for (auto key : m_keyboardButtons) next |= key; if (next != m_buttonMask) { m_buttonMask = next; m_buttonTransitions.push_back(next); } }
void Display::processEvents() {}
void Display::updateFrame(const uint8_t*, uint32_t, bool, size_t, unsigned) {}
void Display::updateFrameData(const uint8_t*, bool, size_t, unsigned) {}
bool Display::usesSyncedPresentation() const { return false; }
uint64_t Display::presentedFrames() const { return 0; }
void Display::close() {}
}

#endif
