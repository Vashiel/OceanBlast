#include "display/launcher.h"
#include <fstream>
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

static void runControllerPreferenceChecks() {
    using oceanblast::win::FrontendSettings;
    FrontendSettings preferred;
    preferred.savedController = L"preferred-pad";
    preferred.padMap[4] = 9;
    check("Preferred controller mapping saved", preferred.save(L"preferred-pad"));

    FrontendSettings fallback;
    fallback.load();
    fallback.loadControllerFor(L"fallback-pad");
    fallback.padMap[4] = 23;
    fallback.soundVolume = 37;
    check("Fallback controller settings saved", fallback.save(L"fallback-pad"));
    FrontendSettings restored;
    restored.load();
    check("Fallback controller does not replace preferred device or mapping",
          restored.savedController == L"preferred-pad" && restored.padMap[4] == 9 && restored.soundVolume == 37);
    restored.loadControllerFor(L"fallback-pad");
    check("Fallback mapping is stored under its own device", restored.padMap[4] == 23);

    fallback.loadControllerFor(L"keyboard");
    check("Keyboard fallback settings saved", fallback.save(L"keyboard"));
    restored.load();
    check("Keyboard fallback preserves disconnected preferred device", restored.savedController == L"preferred-pad");
    fallback.savedController = L"keyboard";
    check("Explicit keyboard selection saved", fallback.save(L"keyboard"));
    restored.load();
    check("Explicit device selection replaces preference", restored.savedController == L"keyboard");

    FrontendSettings firstRun;
    check("First-run active controller saved", firstRun.save(L"fallback-pad"));
    restored.load();
    check("First-run active controller becomes preference", restored.savedController == L"fallback-pad");
}

static std::string settingsBytes() {
    std::ifstream file(std::filesystem::path(oceanblast::win::settingsPath()), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static bool freshProcessReadsAudioSettings() {
    wchar_t executable[32768]{};
    if (!GetModuleFileNameW(nullptr, executable, 32768)) return false;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --check-saved-audio";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &child)) return false;
    CloseHandle(child.hThread);
    const DWORD wait = WaitForSingleObject(child.hProcess, 5000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &exitCode);
    else TerminateProcess(child.hProcess, 2);
    CloseHandle(child.hProcess);
    return wait == WAIT_OBJECT_0 && exitCode == 0;
}

static void runSettingsNotificationChecks() {
    using namespace oceanblast::launcher;
    const auto original = settings;
    auto unchanged = [&](unsigned message, WPARAM w, HWND sender) {
        settings.soundVolume = 37;
        settings.save(input.key());
        const auto before = settingsBytes();
        // An unrelated notification must not persist other pending state.
        settings.soundVolume = 83;
        SendMessageW(settingsWindow, message, w, reinterpret_cast<LPARAM>(sender));
        return !before.empty() && settingsBytes() == before;
    };

    showSettings(0);
    check("Opening a dropdown does not save settings",
          unchanged(WM_COMMAND, MAKEWPARAM(915, CBN_DROPDOWN), GetDlgItem(settingsWindow, 915)));
    check("Navigation focus does not save settings",
          unchanged(WM_COMMAND, MAKEWPARAM(900, LBN_SETFOCUS), GetDlgItem(settingsWindow, 900)));
    check("Read-only field focus does not save settings",
          unchanged(WM_COMMAND, MAKEWPARAM(0, EN_KILLFOCUS), FindWindowExW(settingsWindow, nullptr, L"EDIT", nullptr)));

    showSettings(2);
    check("Checkbox focus does not change or save audio settings",
          unchanged(WM_COMMAND, MAKEWPARAM(970, BN_SETFOCUS), GetDlgItem(settingsWindow, 970)));
    check("Unknown combo notifications do not save settings",
          unchanged(WM_COMMAND, MAKEWPARAM(9999, CBN_SELCHANGE), GetDlgItem(settingsWindow, 971)));
    check("Unrelated horizontal scroll does not change or save volume",
          unchanged(WM_HSCROLL, SB_ENDSCROLL, GetDlgItem(settingsWindow, 970)));

    SendMessageW(GetDlgItem(settingsWindow, 970), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(970, BN_CLICKED),
                 reinterpret_cast<LPARAM>(GetDlgItem(settingsWindow, 970)));
    oceanblast::win::FrontendSettings restored;
    restored.load();
    check("Actual checkbox click changes and persists audio setting", !settings.soundEnabled && !restored.soundEnabled);
    HWND volume = GetDlgItem(settingsWindow, 971);
    SendMessageW(volume, TBM_SETPOS, TRUE, 62);
    SendMessageW(settingsWindow, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(volume));
    restored.load();
    check("Volume slider change persists", settings.soundVolume == 62 && restored.soundVolume == 62);
    check("Fresh process reads audio edits without an extra INI flush", freshProcessReadsAudioSettings());
    settings = original;
    saveSettings();
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
          GetMenuItemCount(GetSubMenu(GetMenu(window), 2)) == 8 &&
          GetMenuState(GetSubMenu(GetMenu(window), 2), ManageMp3Storage, MF_BYCOMMAND) != UINT(-1));

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

    runSettingsNotificationChecks();

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

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--check-saved-audio") {
        oceanblast::win::FrontendSettings saved;
        saved.load();
        return !saved.soundEnabled && saved.soundVolume == 62 ? 0 : 1;
    }
    SetEnvironmentVariableW(L"OCEANBLAST_SETTINGS_PATH", L"build/test_windows_settings.ini");
    DeleteFileW(L"build/test_windows_settings.ini");
    runControllerMappingChecks();
    runControllerPreferenceChecks();
    DeleteFileW(L"build/test_windows_settings.ini");
    SetTimer(nullptr, 0, 200, inspect);
    const int result = oceanblast::launcher::run();
    DeleteFileW(L"build/test_windows_settings.ini");
    return (result || failures) ? 1 : 0;
}
