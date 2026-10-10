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

static void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace oceanblast::launcher;
    KillTimer(nullptr, timer);

    check("Launcher audio enabled", SendMessageW(soundBox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    check("Launcher automatic ROM settings", SendMessageW(timingBox, CB_GETCURSEL, 0, 0) == 0);
    check("Launcher plain window", SendMessageW(windowModeBox, CB_GETCURSEL, 0, 0) == 2);
    check("Embedded OceanBlast logo and symbol loaded", bool(logo) && bool(symbol) && logo->GetWidth() > logo->GetHeight());
    check("Main menu bar has File and Settings menus",
          GetMenu(window) != nullptr &&
          GetMenuItemCount(GetSubMenu(GetMenu(window), 0)) == 5 &&
          GetMenuItemCount(GetSubMenu(GetMenu(window), 1)) == 6);

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
        }
    }
    check("All 6 settings categories build cleanly", allPagesOk);
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
