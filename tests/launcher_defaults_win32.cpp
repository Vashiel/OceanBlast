#include "display/launcher.h"
#include <iostream>
#include <vector>

static int failures = 0;

static void check(const char* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}

static bool capturePreview(HWND target, const wchar_t* path) {
    RECT rect{};
    GetWindowRect(target, &rect);
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return false;
    HDC dc = GetDC(target);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(memory, bitmap);
    const BOOL printed = PrintWindow(target, memory, 0);
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> data(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());
    Gdiplus::GetImageEncoders(count, size, encoders);
    bool saved = false;
    if (printed) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        for (unsigned i = 0; i < count; ++i) {
            if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
                saved = image.Save(path, &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
                break;
            }
        }
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(target, dc);
    return saved;
}

static void runControllerMappingChecks() {
    using namespace oceanblast::win;
    using namespace oceanblast;
    HostPad idle;
    check("Controller idle state maps to zero", mapPad(idle, standardMap) == 0 && firstNewControl(idle, idle) == -1);
    HostPad held;
    held.buttons[7] = true;
    check("Held controller button does not trigger new capture", firstNewControl(held, held) == -1);
    HostPad chord = held;
    chord.buttons[0] = true;
    check("First newly pressed button is captured", firstNewControl(chord, held) == 0);
    check("Standard map maps A and Start", mapPad(chord, standardMap) == (BTN_A | BTN_START));
    PadMap custom = standardMap;
    custom[4] = 23;
    check("Custom controller button mapping replaces default", mapPad(chord, custom) == BTN_START);
    chord.buttons[23] = true;
    check("Custom controller button activates mapped action", mapPad(chord, custom) == (BTN_A | BTN_START));

    const unsigned angles[]{0, 4500, 9000, 13500, 18000, 22500, 27000, 31500};
    const uint32_t expected[]{
        BTN_UP, BTN_UP | BTN_RIGHT, BTN_RIGHT, BTN_DOWN | BTN_RIGHT,
        BTN_DOWN, BTN_DOWN | BTN_LEFT, BTN_LEFT, BTN_UP | BTN_LEFT
    };
    bool povOk = true;
    for (unsigned i = 0; i < 8; ++i) {
        HostPad p;
        p.pov = angles[i];
        if (mapPad(p, standardMap) != expected[i]) povOk = false;
    }
    check("All 8 POV hat directions (including diagonals) map to digiBLAST D-Pad", povOk);

    HostPad stick;
    stick.axes[0] = 32767;
    stick.axes[1] = -32768;
    check("Left analog stick maps to Up+Right and captures axis token",
          mapPad(stick, standardMap) == (BTN_UP | BTN_RIGHT) && firstNewControl(stick, idle) == 129);
}

static uint64_t controllerImageHash(HWND target) {
    constexpr int width = 625, height = 215;
    HDC screen = GetDC(target), buffer = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(buffer, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ previous = SelectObject(buffer, bitmap);
    BitBlt(buffer, 0, 0, width, height, screen, 210, 100, SRCCOPY);
    GdiFlush();
    uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < width * height; ++i) {
        hash ^= static_cast<const uint32_t*>(pixels)[i] & 0x00ffffff;
        hash *= 1099511628211ull;
    }
    SelectObject(buffer, previous); DeleteObject(bitmap); DeleteDC(buffer); ReleaseDC(target, screen);
    return hash;
}

static void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace oceanblast::launcher;
    KillTimer(nullptr, timer);

    check("Launcher default language is English (0)", settings.language == 0);
    check("Launcher audio enabled", SendMessageW(soundBox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    check("Launcher automatic ROM settings", SendMessageW(timingBox, CB_GETCURSEL, 0, 0) == 0);
    check("Launcher plain window", SendMessageW(windowModeBox, CB_GETCURSEL, 0, 0) == 2);
    check("Embedded OceanBlast logo and symbol loaded", bool(logo) && bool(symbol) && logo->GetWidth() > logo->GetHeight());
    check("Main menu bar has File, Playback, and Settings menus",
          GetMenu(window) != nullptr &&
          GetMenuItemCount(GetSubMenu(GetMenu(window), 0)) == 5 &&
          GetMenuItemCount(GetSubMenu(GetMenu(window), 1)) == 9 &&
          GetMenuItemCount(GetSubMenu(GetMenu(window), 2)) == 6);

    UpdateWindow(window);
    check("Launcher start screen preview rendered", capturePreview(window, L"build/launcher-startup-preview.png"));

    bool allPagesOk = true;
    for (int page = 0; page < 6; ++page) {
        showSettings(page);
        if (!settingsWindow || settingsChildren.empty()) allPagesOk = false;
        if (page == 1) {
            UpdateWindow(settingsWindow);
            check("Launcher controller settings preview rendered",
                  capturePreview(settingsWindow, L"build/launcher-controls-preview.png"));
            const auto savedPad = input.current;
            input.current = {};
            const RECT drawing{210, 100, 835, 315};
            auto repaint = [&] { InvalidateRect(settingsWindow, &drawing, FALSE); UpdateWindow(settingsWindow); return controllerImageHash(settingsWindow); };
            const uint64_t idleImage = repaint();
            bool stable = true;
            for (unsigned i = 0; i < 20; ++i) if (repaint() != idleImage) stable = false;
            input.current.buttons[0] = true;
            const bool highlightChanged = repaint() != idleImage;
            input.current.buttons[0] = false;
            check("Controller repaint is stable and release restores exact idle pixels", stable && highlightChanged && repaint() == idleImage);
            input.current = savedPad;
        }
    }
    check("All 6 settings categories build cleanly", allPagesOk);

    bool allLanguagesOk = true;
    for (unsigned lang : {1u, 2u, 3u, 4u, 5u, 6u, 0u}) {
        showSettings(0);
        HWND langCombo = GetDlgItem(settingsWindow, 915);
        HWND cartLangCombo = GetDlgItem(settingsWindow, 916);
        if (!langCombo || SendMessageW(langCombo, CB_GETCOUNT, 0, 0) != 7 ||
            !cartLangCombo || SendMessageW(cartLangCombo, CB_GETCOUNT, 0, 0) != 10) {
            allLanguagesOk = false;
            break;
        }
        SendMessageW(langCombo, CB_SETCURSEL, lang, 0);
        SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(915, CBN_SELCHANGE), reinterpret_cast<LPARAM>(langCombo));
        if (settings.language != lang) allLanguagesOk = false;
        wchar_t title[256]{};
        GetWindowTextW(settingsWindow, title, 256);
        if (std::wstring(title) != kUiText[lang].settingsWindowTitle) allLanguagesOk = false;
    }
    check("All 7 UI languages and 10 cartridge language options switch live and persist cleanly", allLanguagesOk && settings.language == 0);

    showSettings(0);
    HWND cartridgeLanguage = GetDlgItem(settingsWindow, 916);
    bool cartridgeModesSaved = true;
    for (unsigned mode = 0; mode < 10; ++mode) {
        SendMessageW(cartridgeLanguage, CB_SETCURSEL, mode, 0);
        SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(916, CBN_SELCHANGE), reinterpret_cast<LPARAM>(cartridgeLanguage));
        oceanblast::win::FrontendSettings saved;
        saved.load();
        if (settings.cartLanguage != mode || saved.cartLanguage != mode) cartridgeModesSaved = false;
    }
    check("All 10 cartridge language modes persist independently of UI language", cartridgeModesSaved && settings.language == 0);

    if (settingsWindow) DestroyWindow(settingsWindow);

    PostMessageW(window, WM_CLOSE, 0, 0);
}

int main() {
    SetEnvironmentVariableW(L"OCEANBLAST_SETTINGS_PATH", L"build/test_windows_settings.ini");
    DeleteFileW(L"build/test_windows_settings.ini");
    runControllerMappingChecks();
    SetTimer(nullptr, 0, 200, inspect);
    const int result = oceanblast::launcher::run();
    DeleteFileW(L"build/test_windows_settings.ini");
    return (result || failures) ? 1 : 0;
}
