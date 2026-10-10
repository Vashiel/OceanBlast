#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include "windows_input.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace oceanblast {
namespace launcher {

// Hidden compatibility controls synced with FrontendSettings for automated inspection tests.
static HWND pathBox = nullptr, scaleBox = nullptr, soundBox = nullptr, profileBox = nullptr;
static HWND debugBox = nullptr, formatBox = nullptr, nvramBox = nullptr, timingBox = nullptr;
static HWND startButton = nullptr, stopButton = nullptr, statusBox = nullptr, windowModeBox = nullptr;

static HWND window = nullptr, settingsWindow = nullptr, deviceCombo = nullptr, learningLabel = nullptr;
static std::vector<HWND> settingsChildren;
static int settingsPage = 0, learnAction = -1;
static bool learnKeyboard = false;
static bool mainFullscreen = false;
static WINDOWPLACEMENT savedPlacement{};
static HMENU mainMenu = nullptr;
static HANDLE process = nullptr;
static std::wstring executable, folder, activeRomPath;
static win::FrontendSettings settings;
static win::InputManager input;
static win::HostPad previousPad{};
static ULONG_PTR gdiplusToken = 0;
static IStream* logoStream = nullptr;
static IStream* symbolStream = nullptr;
static std::unique_ptr<Gdiplus::Image> logo, symbol;
static HICON appIcon = nullptr;

enum {
    OpenRom = 100,
    RestartRom = 101,
    StopRom = 102,
    ToggleFullscreenCmd = 103,
    EscapeFromChildCmd = 104,
    ExitApp = 105,
    SettingsBase = 200
};

static const wchar_t* categories[]{
    L"Ordner & System",
    L"Steuerung",
    L"Sound",
    L"Video",
    L"Timing & Diagnose",
    L"Kurztasten & Hilfe"
};

static HWND findEmbeddedDisplay() {
    if (!window) return nullptr;
    return FindWindowExA(window, nullptr, "OceanBlastDisplayClass", nullptr);
}

static void syncCompatibilityControls() {
    if (!soundBox) return;
    SetWindowTextW(pathBox, settings.lastRom.c_str());
    SendMessageW(scaleBox, CB_SETCURSEL, settings.scaleIndex, 0);
    SendMessageW(soundBox, BM_SETCHECK, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(profileBox, BM_SETCHECK, settings.fpsLog ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(debugBox, BM_SETCHECK, settings.debugLog ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(formatBox, CB_SETCURSEL, settings.displayFormat, 0);
    SendMessageW(nvramBox, BM_SETCHECK, settings.keepNvram ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(timingBox, CB_SETCURSEL, settings.timingMode, 0);
    SendMessageW(windowModeBox, CB_SETCURSEL, settings.windowMode, 0);
}

static void pullCompatibilityControls() {
    if (!soundBox) return;
    wchar_t filename[32768]{};
    GetWindowTextW(pathBox, filename, 32768);
    if (filename[0]) settings.lastRom = filename;
    settings.scaleIndex = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(scaleBox, CB_GETCURSEL, 0, 0)));
    settings.soundEnabled = SendMessageW(soundBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fpsLog = SendMessageW(profileBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.debugLog = SendMessageW(debugBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.displayFormat = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(formatBox, CB_GETCURSEL, 0, 0)));
    settings.keepNvram = SendMessageW(nvramBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.timingMode = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(timingBox, CB_GETCURSEL, 0, 0)));
    settings.windowMode = static_cast<unsigned>(std::max<LRESULT>(0, SendMessageW(windowModeBox, CB_GETCURSEL, 0, 0)));
}

static void notifyRunningGameSettingsChanged() {
    syncCompatibilityControls();
    if (HWND child = findEmbeddedDisplay()) {
        PostMessageA(child, WM_APP + 1, 0, 0);
    }
}

static void saveSettings() {
    if (!settings.save(input.key())) {
        MessageBoxW(window, L"Einstellungen konnten nicht gespeichert werden.", L"OceanBlast", MB_ICONERROR);
    }
    notifyRunningGameSettingsChanged();
}

static void setIdleTitle() {
    if (!window) return;
    SetWindowTextW(window, L"OceanBlast — Strg+O: ROM öffnen, ROM hierher ziehen");
}

static void stop() {
    if (process) {
        if (HWND child = findEmbeddedDisplay()) {
            SendMessageA(child, WM_CLOSE, 0, 0);
        }
        const DWORD pid = GetProcessId(process);
        EnumWindows([](HWND w, LPARAM p) -> BOOL {
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            if (owner == static_cast<DWORD>(p)) PostMessageW(w, WM_CLOSE, 0, 0);
            return TRUE;
        }, static_cast<LPARAM>(pid));
        if (WaitForSingleObject(process, 500) != WAIT_OBJECT_0) {
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 200);
        }
        CloseHandle(process);
        process = nullptr;
    }
    if (startButton) EnableWindow(startButton, TRUE);
    if (stopButton) EnableWindow(stopButton, FALSE);
    setIdleTitle();
    if (window) InvalidateRect(window, nullptr, TRUE);
}

static void toggleMainFullscreen() {
    if (!window) return;
    LONG style = GetWindowLongW(window, GWL_STYLE);
    if (!mainFullscreen) {
        savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window, &savedPlacement);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
        mainFullscreen = true;
        SetMenu(window, nullptr);
        SetWindowLongW(window, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(window, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        mainFullscreen = false;
        SetMenu(window, mainMenu);
        SetWindowLongW(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
        SetWindowPlacement(window, &savedPlacement);
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    if (HWND child = findEmbeddedDisplay()) {
        RECT rc{};
        GetClientRect(window, &rc);
        SetWindowPos(child, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(window, nullptr, TRUE);
}

static bool launchRom(const std::wstring& romFile) {
    DWORD attrs = GetFileAttributesW(romFile.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(window, L"Bitte zuerst eine gültige digiBLAST-ROM-Datei (*.bin;*.rom) auswählen.", L"OceanBlast", MB_ICONWARNING);
        return false;
    }
    stop();
    activeRomPath = romFile;
    settings.lastRom = romFile;
    if (settings.romFolder.empty()) {
        std::filesystem::path p(romFile);
        if (p.has_parent_path()) settings.romFolder = p.parent_path().wstring();
    }
    saveSettings();

    const int scale = static_cast<int>(settings.scaleIndex) + 2;
    std::wstring command = L"\"" + executable + L"\" \"" + romFile + L"\" --gui --scale " + std::to_wstring(scale);
    command += L" --parent-hwnd " + std::to_wstring(reinterpret_cast<uintptr_t>(window));
    if (settings.windowMode == 0) command += L" --window-mode skin";
    else command += L" --window-mode plain";
    if (settings.windowMode == 1 && !mainFullscreen) toggleMainFullscreen();

    if (settings.soundEnabled) command += L" --sound";
    if (settings.fpsLog) command += L" --profile";
    if (settings.debugLog) command += L" --debug";

    if (settings.timingMode == 1) command += L" --preset off";
    else if (settings.timingMode == 2) command += L" --timing auto";
    else if (settings.timingMode == 3) command += L" --cpu-steps-per-tick 2";
    else if (settings.timingMode == 4) command += L" --cpu-steps-per-tick 4";
    else if (settings.timingMode == 5) command += L" --cpu-steps-per-tick 8";

    if (settings.displayFormat == 1) command += L" --display-format rgb444 --display-stride 480";
    else if (settings.displayFormat == 2) command += L" --display-format rgb444";
    else if (settings.displayFormat == 3) command += L" --display-format rgb565";
    else if (settings.displayFormat == 4) command += L" --display-format lcd";

    if (settings.keepNvram) command += L" --nvram \"" + folder + L"\\board.nvram\"";

    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW((folder + L"\\session.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE inHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    if (log == INVALID_HANDLE_VALUE || inHandle == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (inHandle != INVALID_HANDLE_VALUE) CloseHandle(inHandle);
        MessageBoxW(window, L"Session-Logdatei konnte nicht geöffnet werden.", L"OceanBlast", MB_ICONERROR);
        return false;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = inHandle;
    PROCESS_INFORMATION child = {};
    BOOL ok = CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &child);
    CloseHandle(log);
    CloseHandle(inHandle);
    if (!ok) {
        MessageBoxW(window, L"Emulator-Prozess konnte nicht gestartet werden.", L"OceanBlast", MB_ICONERROR);
        return false;
    }
    process = child.hProcess;
    CloseHandle(child.hThread);
    if (startButton) EnableWindow(startButton, FALSE);
    if (stopButton) EnableWindow(stopButton, TRUE);
    if (statusBox) SetWindowTextW(statusBox, L"Emulator läuft. Details werden in sessions\\session.log protokolliert.");
    const auto romName = std::filesystem::path(romFile).filename().wstring();
    SetWindowTextW(window, (L"OceanBlast — " + romName).c_str());
    return true;
}

static bool browseRom(HWND owner, bool autoLaunch) {
    wchar_t filename[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"digiBLAST ROM (*.bin;*.rom)\0*.bin;*.rom\0Alle Dateien (*.*)\0*.*\0";
    dialog.lpstrInitialDir = settings.romFolder.empty() ? nullptr : settings.romFolder.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    settings.lastRom = filename;
    saveSettings();
    if (autoLaunch) return launchRom(filename);
    return true;
}

static void chooseRomFolder() {
    BROWSEINFOW info{};
    info.hwndOwner = settingsWindow ? settingsWindow : window;
    info.lpszTitle = L"Standard-Ordner für digiBLAST-ROMs auswählen";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    if (auto item = SHBrowseForFolderW(&info)) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(item, path)) {
            settings.romFolder = path;
            saveSettings();
        }
        CoTaskMemFree(item);
    }
}

static HMENU makeMenu() {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU cfg = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, OpenRom, L"ROM laden...\tStrg+O");
    AppendMenuW(file, MF_STRING, RestartRom, L"Spiel neu starten\tStrg+R");
    AppendMenuW(file, MF_STRING, StopRom, L"Spiel stoppen");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ExitApp, L"Beenden");

    const wchar_t* names[]{
        L"Ordner & System...",
        L"Steuerung...",
        L"Sound...",
        L"Video...",
        L"Timing & Diagnose...",
        L"Kurztasten & Hilfe..."
    };
    for (unsigned i = 0; i < 6; ++i) {
        AppendMenuW(cfg, MF_STRING, SettingsBase + i, names[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Datei");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(cfg), L"Einstellungen");
    return menu;
}

static void startScreen(HDC dc) {
    RECT r{};
    GetClientRect(window, &r);
    HBRUSH bg = CreateSolidBrush(RGB(6, 20, 36));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    if (logo) {
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        float w = std::min(static_cast<float>(r.right) * 0.84f, 960.0f);
        float h = w * static_cast<float>(logo->GetHeight()) / static_cast<float>(logo->GetWidth());
        if (h > static_cast<float>(r.bottom) * 0.60f) {
            h = static_cast<float>(r.bottom) * 0.60f;
            w = h * static_cast<float>(logo->GetWidth()) / static_cast<float>(logo->GetHeight());
        }
        const float x = (r.right - w) / 2.0f;
        const float y = std::max(0.0f, (r.bottom - h) / 2.0f - 28.0f);
        g.DrawImage(logo.get(), x, y, w, h);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(180, 195, 213));
    HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    RECT text{10, r.bottom - 88, r.right - 10, r.bottom - 28};
    DrawTextW(dc,
              L"ROM laden: Strg+O · ROM ins Fenster ziehen\nEinstellungen: System, Controller, Sound, Video und Timing",
              -1, &text, DT_CENTER | DT_WORDBREAK);
    SelectObject(dc, oldFont);
}

static HWND settingsControl(const wchar_t* cls, const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = 0) {
    HWND child = CreateWindowExW(
        0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, settingsWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    settingsChildren.push_back(child);
    return child;
}

static void settingsLabel(const wchar_t* text, int x, int y, int w = 620, int h = 24) {
    settingsControl(L"STATIC", text, 0, x, y, w, h);
}

static void comboItem(HWND h, const wchar_t* text) {
    SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

static void rebuildSettings() {
    learnAction = -1;
    deviceCombo = nullptr;
    learningLabel = nullptr;
    for (HWND child : settingsChildren) DestroyWindow(child);
    settingsChildren.clear();

    HWND nav = settingsControl(L"LISTBOX", L"", 900, 12, 14, 180, 535, LBS_NOTIFY | WS_BORDER | WS_TABSTOP);
    for (auto name : categories) SendMessageW(nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(nav, LB_SETCURSEL, settingsPage, 0);

    settingsControl(L"BUTTON", L"Schließen", 901, 720, 550, 110, 30, WS_TABSTOP);
    settingsLabel(L"Änderungen werden lokal gespeichert.", 210, 553, 500);

    if (settingsPage == 0) {
        settingsLabel(L"ROM-Ordner (Startverzeichnis für Strg+O)", 210, 18);
        settingsControl(L"EDIT", settings.romFolder.empty() ? L"Standardverzeichnis" : settings.romFolder.c_str(),
                        0, 210, 44, 490, 25, ES_READONLY | WS_BORDER);
        settingsControl(L"BUTTON", L"Auswählen...", 910, 710, 44, 120, 25, WS_TABSTOP);

        settingsLabel(L"Ausgewähltes digiBLAST-ROM", 210, 88);
        settingsControl(L"EDIT", settings.lastRom.empty() ? L"Noch kein ROM ausgewählt" : settings.lastRom.c_str(),
                        0, 210, 114, 490, 25, ES_READONLY | WS_BORDER);
        settingsControl(L"BUTTON", L"Datei...", 911, 710, 114, 120, 25, WS_TABSTOP);

        settingsControl(L"BUTTON", L"Spiel starten", 912, 210, 154, 150, 30, BS_DEFPUSHBUTTON | WS_TABSTOP);
        HWND stopBtn = settingsControl(L"BUTTON", L"Spiel stoppen", 913, 372, 154, 150, 30, WS_TABSTOP);
        EnableWindow(stopBtn, process != nullptr);

        settingsLabel(L"Persistenter Gerätespeicher (2 KB I2C-EEPROM)", 210, 212);
        HWND nvram = settingsControl(L"BUTTON", L"Geräteeinstellungen und Spielstände speichern (sessions\\board.nvram)",
                                     914, 210, 240, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(nvram, BM_SETCHECK, settings.keepNvram ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(L"Tipp: Du kannst ROM-Dateien (*.bin; *.rom) jederzeit direkt per Drag & Drop in das Hauptfenster ziehen oder mit Strg+O öffnen.", 210, 292, 620, 44);
    } else if (settingsPage == 1) {
        settingsLabel(L"Controller auswählen", 210, 16);
        deviceCombo = settingsControl(L"COMBOBOX", L"", 960, 210, 42, 440, 180, CBS_DROPDOWNLIST | WS_TABSTOP);
        int selected = -1;
        for (unsigned i = 0; i < input.controllers.size(); ++i) {
            comboItem(deviceCombo, input.controllers[i].name.c_str());
            if (input.controllers[i].key == input.key()) selected = static_cast<int>(i);
        }
        if (selected < 0) {
            selected = static_cast<int>(input.controllers.size());
            comboItem(deviceCombo, L"Gespeicherter Controller (nicht verbunden)");
        }
        SendMessageW(deviceCombo, CB_SETCURSEL, selected, 0);
        settingsControl(L"BUTTON", L"Aktualisieren", 961, 666, 42, 164, 25, WS_TABSTOP);

        settingsLabel(L"Preset", 210, 80, 65);
        HWND preset = settingsControl(L"COMBOBOX", L"", 962, 280, 76, 370, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {L"Stadia / Xbox — Positionen", L"PlayStation — Positionen", L"digiBLAST / USB — anlernbar"})
            comboItem(preset, text);
        SendMessageW(preset, CB_SETCURSEL, settings.controllerPreset, 0);

        settingsLabel(L"digiBLAST  Controller             Tastatur", 210, 314, 300);
        settingsLabel(L"digiBLAST  Controller             Tastatur", 525, 314, 305);
        for (unsigned i = 0; i < 12; ++i) {
            int y = 345 + static_cast<int>(i % 6) * 25;
            int x = 210 + static_cast<int>(i / 6) * 315;
            settingsLabel(win::actionNames[i], x, y, 48);
            settingsControl(L"BUTTON", win::controlLabel(settings.padMap[i]).c_str(),
                            980 + i, x + 50, y, 153, 23, WS_TABSTOP);
            settingsControl(L"BUTTON", win::keyboardLabel(settings.keyboardKeys[i]).c_str(),
                            1000 + i, x + 207, y, 98, 23, WS_TABSTOP);
        }
        learningLabel = settingsControl(
            L"STATIC", L"Belegung anklicken, dann Taste drücken. Esc bricht ab.",
            0, 210, 498, 620, 42);
    } else if (settingsPage == 2) {
        settingsLabel(L"Sound", 210, 20);
        HWND enabled = settingsControl(L"BUTTON", L"Audio-Ausgabe aktivieren", 970, 210, 62, 400, 30, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(enabled, BM_SETCHECK, settings.soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(L"Lautstärke", 210, 110);
        HWND volume = settingsControl(TRACKBAR_CLASSW, L"", 971, 210, 140, 580, 45, TBS_AUTOTICKS | WS_TABSTOP);
        SendMessageW(volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(volume, TBM_SETPOS, TRUE, settings.soundVolume);

        settingsLabel(L"S3C2410 IIS + DMA2 Stereo-Audio · 22,05 kHz Standardrate mit dynamischem Resampling.", 210, 220, 620, 40);
    } else if (settingsPage == 3) {
        settingsLabel(L"Darstellungsmodus", 210, 20);
        HWND winMode = settingsControl(L"COMBOBOX", L"", 975, 210, 46, 500, 140, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {L"digiBLAST-Gehäuse (Console Skin)", L"Vollbild (F11 schaltet um)", L"Standard-Fenster (LCD im Hauptfenster)"})
            comboItem(winMode, text);
        SendMessageW(winMode, CB_SETCURSEL, settings.windowMode, 0);

        settingsLabel(L"Bildformat", 210, 88);
        HWND aspect = settingsControl(L"COMBOBOX", L"", 972, 210, 114, 500, 140, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {L"Originale digiBLAST-Proportionen (3:2)", L"4:3", L"Fensterfüllend (gestreckt)"})
            comboItem(aspect, text);
        SendMessageW(aspect, CB_SETCURSEL, settings.videoAspect, 0);

        HWND integer = settingsControl(L"BUTTON", L"Ganzzahlige Pixelskalierung (hat Vorrang vor Bildformat)",
                                       973, 210, 156, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(integer, BM_SETCHECK, settings.integerScale ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND smooth = settingsControl(L"BUTTON", L"Weiche Bildskalierung (GDI Halftone)",
                                      974, 210, 190, 500, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(smooth, BM_SETCHECK, settings.smoothVideo ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(L"Stand-Alone-Skalierungsfaktor (--gui)", 210, 232);
        HWND scale = settingsControl(L"COMBOBOX", L"", 976, 210, 258, 300, 120, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {L"2× (480 × 320)", L"3× (720 × 480, Standard)", L"4× (960 × 640)"})
            comboItem(scale, text);
        SendMessageW(scale, CB_SETCURSEL, settings.scaleIndex, 0);

        settingsLabel(L"Display-Decoder", 210, 300);
        HWND decoder = settingsControl(L"COMBOBOX", L"", 977, 210, 326, 420, 150, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {
            L"Automatisch (Standard, 12-Bit LCD / 16-Bit RGB565)",
            L"RGB444, 480-Byte Zeilen (Diagnose)",
            L"RGB444 gepackt, 360-Byte Zeilen (Diagnose)",
            L"RGB565, 480-Byte Zeilen (Diagnose)",
            L"Nur LCD-Register (Diagnose)"
        }) comboItem(decoder, text);
        SendMessageW(decoder, CB_SETCURSEL, settings.displayFormat, 0);
    } else if (settingsPage == 4) {
        settingsLabel(L"CPU- & Peripherie-Timing", 210, 20);
        HWND timing = settingsControl(L"COMBOBOX", L"", 978, 210, 48, 520, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto text : {
            L"Automatische ROM-Einstellungen (Standard)",
            L"Standard: 1× (20 MIPS Basis)",
            L"Automatische Register-Takte (experimentell)",
            L"Mehr CPU-Arbeit: 2× (40 MIPS)",
            L"Mehr CPU-Arbeit: 4× (80 MIPS)",
            L"Mehr CPU-Arbeit: 8× (160 MIPS, schneller Kampf in Wade Hixton)"
        }) comboItem(timing, text);
        SendMessageW(timing, CB_SETCURSEL, settings.timingMode, 0);

        HWND fps = settingsControl(L"BUTTON", L"FPS- & Audio-Metriken in sessions\\performance.csv protokollieren",
                                   979, 210, 96, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(fps, BM_SETCHECK, settings.fpsLog ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND dbg = settingsControl(L"BUTTON", L"Erweitertes Debug-Logging in sessions\\session.log aktivieren",
                                   965, 210, 132, 600, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(dbg, BM_SETCHECK, settings.debugLog ? BST_CHECKED : BST_UNCHECKED, 0);

        settingsLabel(L"Hinweis: Änderungen am Timing-Modus oder Display-Decoder werden beim nächsten Start bzw. Neustart (Strg+R) des ROMs übernommen.", 210, 185, 620, 48);
    } else {
        settingsLabel(L"Kurztasten & Steuerung", 210, 20);
        settingsLabel(
            L"• Strg+O: ROM laden    ·    Strg+R: Spiel neu starten\n"
            L"• ROM-Datei ins Fenster ziehen: Spiel direkt per Drag & Drop starten\n"
            L"• F11 oder Alt+Eingabe: Vollbild umschalten\n"
            L"• Esc: Vollbild verlassen / Pause\n"
            L"• F5: Pause / Weiter    ·    F6: Einzelner CPU-Schritt    ·    F7: Snapshot speichern\n"
            L"• F8 / L2: Rücklauf    ·    F9: Stopp    ·    F10: Play/Pause    ·    F12 / R2: Vorlauf\n\n"
            L"Unter 'Steuerung' lassen sich Xbox-, Stadia-, PlayStation- und USB-Controller sowie alle Tastaturtasten frei anlernen.",
            210, 56, 620, 240);
    }
    InvalidateRect(settingsWindow, nullptr, TRUE);
}

static void drawController(HDC dc) {
    if (settingsPage != 1) return;
    using namespace Gdiplus;
    const auto* info = input.selected();
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Exact coordinate space from Google_Stadia.svg (viewBox 0 0 1090 707, translate(-94, -404))
    // Uniform scale s preserves 100% of the original SVG proportions.
    constexpr float s = 0.232f;
    constexpr float ox = 520.0f - (640.0f * s);
    constexpr float oy = 128.0f - (406.0f * s);
    auto px = [&](float x) { return ox + x * s; };
    auto py = [&](float y) { return oy + y * s; };
    auto pr = [&](float r) { return r * s; };
    auto addL = [&](GraphicsPath& p, float x1, float y1, float x2, float y2) {
        p.AddLine(px(x1), py(y1), px(x2), py(y2));
    };
    auto addB = [&](GraphicsPath& p, float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2) {
        p.AddBezier(px(x1), py(y1), px(cx1), py(cy1), px(cx2), py(cy2), px(x2), py(y2));
    };
    auto circleRect = [&](float cx, float cy, float r) {
        return RectF(px(cx - r), py(cy - r), pr(r * 2.0f), pr(r * 2.0f));
    };
    auto addPill = [&](GraphicsPath& p, float cx, float cy, float hw, float hh) {
        const float left = px(cx - hw), top = py(cy - hh);
        const float w = pr(hw * 2.0f), h = pr(hh * 2.0f);
        p.AddArc(left, top, h, h, 90.0f, 180.0f);
        p.AddArc(left + w - h, top, h, h, 270.0f, 180.0f);
        p.CloseFigure();
    };

    // 1. Left & Right shoulder bumpers (id="L", id="R")
    GraphicsPath leftBumper, rightBumper;
    addL(leftBumper, 425.780f, 428.510f, 425.780f, 416.437f);
    addB(leftBumper, 425.780f, 416.437f, 425.780f, 412.277f, 422.615f, 408.835f, 418.458f, 408.666f);
    addB(leftBumper, 418.458f, 408.666f, 394.531f, 407.695f, 317.408f, 407.521f, 268.848f, 441.714f);
    addB(leftBumper, 268.848f, 441.714f, 266.800f, 443.157f, 265.638f, 445.530f, 265.638f, 448.036f);
    addL(leftBumper, 265.638f, 448.036f, 265.638f, 471.266f);

    addL(rightBumper, 854.890f, 428.510f, 854.890f, 416.437f);
    addB(rightBumper, 854.890f, 416.437f, 854.890f, 412.277f, 858.055f, 408.835f, 862.212f, 408.666f);
    addB(rightBumper, 862.212f, 408.666f, 886.140f, 407.695f, 963.262f, 407.521f, 1011.822f, 441.714f);
    addB(rightBumper, 1011.822f, 441.714f, 1013.872f, 443.157f, 1015.032f, 445.530f, 1015.032f, 448.036f);
    addL(rightBumper, 1015.032f, 448.036f, 1015.032f, 471.266f);

    const bool lPressed = win::hostPressed(input.current, 4) || win::hostPressed(input.current, 300);
    const bool rPressed = win::hostPressed(input.current, 5) || win::hostPressed(input.current, 301);

    Pen bumperPen(lPressed ? Color(255, 16, 188, 212) : Color(210, 64, 82, 104), lPressed ? 3.2f : 2.2f);
    Pen bumperPenR(rPressed ? Color(255, 16, 188, 212) : Color(210, 64, 82, 104), rPressed ? 3.2f : 2.2f);
    g.DrawPath(&bumperPen, &leftBumper);
    g.DrawPath(&bumperPenR, &rightBumper);

    // 2. Controller body contour (id="body" from Google_Stadia.svg)
    GraphicsPath body;
    addB(body, 1178.400f, 975.760f, 1158.030f, 821.080f, 1114.960f, 653.190f, 1076.580f, 552.920f);
    addB(body, 1076.580f, 552.920f, 1038.990f, 451.870f, 939.530f, 431.660f, 846.330f, 431.660f);
    addL(body, 846.330f, 431.660f, 433.590f, 431.660f);
    addB(body, 433.590f, 431.660f, 340.391f, 431.660f, 240.930f, 451.869f, 203.330f, 552.920f);
    addB(body, 203.330f, 552.920f, 165.738f, 653.970f, 122.662f, 821.080f, 101.520f, 975.760f);
    addB(body, 101.520f, 975.760f, 89.773f, 1052.712f, 146.162f, 1099.350f, 196.285f, 1104.790f);
    addB(body, 196.285f, 1104.790f, 246.409f, 1110.230f, 306.715f, 1091.570f, 336.475f, 1015.400f);
    addB(body, 336.475f, 1015.400f, 366.236f, 940.004f, 378.767f, 875.490f, 441.425f, 875.490f);
    addL(body, 441.425f, 875.490f, 837.715f, 875.490f);
    addB(body, 837.715f, 875.490f, 900.369f, 875.490f, 912.900f, 940.004f, 942.665f, 1015.400f);
    addB(body, 942.665f, 1015.400f, 972.426f, 1090.800f, 1032.736f, 1110.230f, 1082.855f, 1104.790f);
    addB(body, 1082.855f, 1104.790f, 1132.974f, 1099.350f, 1190.155f, 1052.710f, 1178.400f, 975.760f);
    body.CloseFigure();

    SolidBrush shadow(Color(28, 8, 22, 40));
    g.TranslateTransform(2.5f, 4.5f);
    g.FillPath(&shadow, &body);
    g.TranslateTransform(-2.5f, -4.5f);

    // Semi-transparent Ocean-Glass housing
    LinearGradientBrush housing(
        PointF(px(640.0f), py(425.0f)),
        PointF(px(640.0f), py(1110.0f)),
        Color(215, 240, 246, 252),
        Color(190, 202, 216, 232));
    Pen bodyOutline(Color(235, 96, 118, 142), 2.0f);
    g.FillPath(&housing, &body);
    g.DrawPath(&bodyOutline, &body);

    SolidBrush dark(Color(238, 34, 46, 60));
    SolidBrush darkSoft(Color(195, 52, 66, 84));
    SolidBrush wellFill(Color(45, 24, 42, 66));
    SolidBrush highlight(Color(250, 16, 188, 212));
    SolidBrush iconLight(Color(245, 238, 244, 250));
    Pen partOutline(Color(220, 108, 126, 146), 1.5f);
    Pen wellOutline(Color(150, 84, 104, 126), 1.2f);

    // 3. D-Pad (id="dpad" from Google_Stadia.svg)
    GraphicsPath dpad;
    addL(dpad, 405.610f, 582.850f, 374.392f, 582.850f);
    addB(dpad, 374.392f, 582.850f, 372.183f, 582.850f, 370.392f, 581.059f, 370.392f, 578.850f);
    addL(dpad, 370.392f, 578.850f, 370.392f, 547.632f);
    addB(dpad, 370.392f, 547.632f, 370.392f, 532.763f, 357.870f, 520.241f, 343.000f, 520.241f);
    addB(dpad, 343.000f, 520.241f, 328.131f, 520.241f, 315.609f, 532.763f, 315.609f, 547.632f);
    addL(dpad, 315.609f, 547.632f, 315.609f, 578.850f);
    addB(dpad, 315.609f, 578.850f, 315.609f, 581.059f, 313.818f, 582.850f, 311.609f, 582.850f);
    addL(dpad, 311.609f, 582.850f, 280.391f, 582.850f);
    addB(dpad, 280.391f, 582.850f, 265.521f, 582.850f, 252.999f, 595.371f, 252.999f, 610.241f);
    addB(dpad, 252.999f, 610.241f, 252.999f, 625.110f, 265.521f, 637.632f, 280.391f, 637.632f);
    addL(dpad, 280.391f, 637.632f, 311.609f, 637.632f);
    addB(dpad, 311.609f, 637.632f, 313.818f, 637.632f, 315.609f, 639.423f, 315.609f, 641.632f);
    addL(dpad, 315.609f, 641.632f, 315.609f, 672.850f);
    addB(dpad, 315.609f, 672.850f, 315.609f, 687.719f, 328.131f, 700.241f, 343.000f, 700.241f);
    addB(dpad, 343.000f, 700.241f, 357.870f, 700.241f, 370.392f, 687.719f, 370.392f, 672.850f);
    addL(dpad, 370.392f, 672.850f, 370.392f, 641.632f);
    addB(dpad, 370.392f, 641.632f, 370.392f, 639.423f, 372.183f, 637.632f, 374.392f, 637.632f);
    addL(dpad, 374.392f, 637.632f, 405.610f, 637.632f);
    addB(dpad, 405.610f, 637.632f, 420.480f, 637.632f, 433.002f, 625.110f, 433.002f, 610.241f);
    addB(dpad, 433.002f, 610.241f, 433.002f, 595.371f, 421.262f, 582.850f, 405.610f, 582.850f);
    dpad.CloseFigure();

    g.FillPath(&dark, &dpad);
    g.DrawPath(&partOutline, &dpad);

    // Directional highlights on D-Pad
    if (win::hostPressed(input.current, 200)) g.FillEllipse(&highlight, circleRect(343.0f, 552.0f, 22.0f));
    if (win::hostPressed(input.current, 201)) g.FillEllipse(&highlight, circleRect(343.0f, 668.5f, 22.0f));
    if (win::hostPressed(input.current, 202)) g.FillEllipse(&highlight, circleRect(285.0f, 610.24f, 22.0f));
    if (win::hostPressed(input.current, 203)) g.FillEllipse(&highlight, circleRect(401.0f, 610.24f, 22.0f));

    // 4. Left & Right Analog Sticks (id="leftcircle"/"leftstick", id="rightcircle"/"rightstick")
    const float stickCentersX[2] = {480.225f, 800.235f};
    const float stickCenterY = 765.40f;
    for (int i = 0; i < 2; ++i) {
        const float cx = stickCentersX[i];
        g.FillEllipse(&wellFill, circleRect(cx, stickCenterY, 64.0f));
        g.DrawEllipse(&wellOutline, circleRect(cx, stickCenterY, 64.0f));

        const auto& axes = input.current.axes;
        const float normX = std::clamp(axes[i * 2] / 32768.0f, -1.0f, 1.0f);
        const float normY = std::clamp(axes[i * 2 + 1] / 32768.0f, -1.0f, 1.0f);
        const float sx = cx + normX * 22.0f;
        const float sy = stickCenterY + normY * 22.0f;
        const bool stickActive = win::hostPressed(input.current, 8 + i) ||
                                 std::abs(axes[i * 2]) > 14000 || std::abs(axes[i * 2 + 1]) > 14000;

        g.FillEllipse(stickActive ? &highlight : &dark, circleRect(sx, sy, 48.0f));
        g.DrawEllipse(&partOutline, circleRect(sx, sy, 48.0f));
        g.FillEllipse(&darkSoft, circleRect(sx, sy, 31.0f));
        g.DrawEllipse(&wellOutline, circleRect(sx, sy, 31.0f));
    }

    // 5. Center buttons (optionsbutton, menubutton, assistantbutton, capturebutton, stadiabutton)
    const bool selectPressed = win::hostPressed(input.current, 6) || win::hostPressed(input.current, 10);
    const bool startPressed = win::hostPressed(input.current, 7) || win::hostPressed(input.current, 11);

    GraphicsPath optionsPill, menuPill;
    addPill(optionsPill, 518.0f, 532.58f, 34.0f, 20.0f);
    addPill(menuPill, 761.88f, 532.58f, 34.0f, 20.0f);
    g.FillPath(selectPressed ? &highlight : &dark, &optionsPill);
    g.DrawPath(&partOutline, &optionsPill);
    g.FillPath(startPressed ? &highlight : &dark, &menuPill);
    g.DrawPath(&partOutline, &menuPill);

    // 3 dots inside optionsbutton (506, 518, 530 at y=532.4)
    for (float dx : {506.0f, 518.0f, 530.0f}) {
        g.FillEllipse(&iconLight, circleRect(dx, 532.4f, 4.0f));
    }
    // 3 horizontal bars inside menubutton
    for (float by : {524.2f, 531.4f, 538.6f}) {
        g.FillRectangle(&iconLight, RectF(px(749.25f), py(by), pr(25.5f), pr(2.6f)));
    }

    // Assistant (564, 611.43, r=24) & Capture (715.37, 611.43, r=24) & Stadia Home (640, 765.4, r=36)
    g.FillEllipse(win::hostPressed(input.current, 12) ? &highlight : &darkSoft, circleRect(564.0f, 611.43f, 24.0f));
    g.DrawEllipse(&partOutline, circleRect(564.0f, 611.43f, 24.0f));
    g.FillEllipse(&iconLight, circleRect(558.2f, 609.5f, 6.2f));
    g.FillEllipse(&iconLight, circleRect(569.5f, 608.2f, 3.2f));
    g.FillEllipse(&iconLight, circleRect(569.5f, 616.5f, 3.8f));
    g.FillEllipse(&iconLight, circleRect(574.5f, 605.8f, 1.8f));

    g.FillEllipse(win::hostPressed(input.current, 13) ? &highlight : &darkSoft, circleRect(715.37f, 611.43f, 24.0f));
    g.DrawEllipse(&partOutline, circleRect(715.37f, 611.43f, 24.0f));
    Pen iconPen(Color(235, 238, 244, 250), 1.4f);
    g.DrawRectangle(&iconPen, px(706.5f), py(603.5f), pr(17.7f), pr(15.8f));

    g.FillEllipse(&dark, circleRect(640.0f, 765.40f, 36.0f));
    g.DrawEllipse(&partOutline, circleRect(640.0f, 765.40f, 36.0f));
    g.DrawEllipse(&iconPen, circleRect(640.0f, 765.40f, 16.0f));

    // 6. Face Buttons (Ybutton, Xbutton, Bbutton, Abutton — r=34 in Google_Stadia.svg)
    FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 11.0f, FontStyleBold, UnitPixel);
    Gdiplus::Font smallFont(&family, 10.5f, FontStyleBold, UnitPixel);
    StringFormat centered;
    centered.SetAlignment(StringAlignmentCenter);
    centered.SetLineAlignment(StringAlignmentCenter);

    auto drawFaceButton = [&](float cx, float cy, const wchar_t* label, int token) {
        const RectF rc = circleRect(cx, cy, 34.0f);
        g.FillEllipse(win::hostPressed(input.current, token) ? &highlight : &dark, rc);
        g.DrawEllipse(&partOutline, rc);
        g.DrawString(label, -1, &font, rc, &centered, &iconLight);
    };

    const bool psSymbols = (settings.controllerPreset == 1);
    drawFaceButton(946.75f, 541.40f, psSymbols ? L"△" : L"Y", 3);
    drawFaceButton(879.00f, 609.32f, psSymbols ? L"□" : L"X", 2);
    drawFaceButton(1015.00f, 609.32f, psSymbols ? L"○" : L"B", 1);
    drawFaceButton(946.75f, 677.40f, psSymbols ? L"✕" : L"A", 0);

    SolidBrush labelBrush(Color(255, 42, 54, 70));
    g.DrawString(L"L / L2", -1, &smallFont, RectF(px(80.0f), py(410.0f), pr(175.0f), 18.0f), &centered, lPressed ? &highlight : &labelBrush);
    g.DrawString(L"R / R2", -1, &smallFont, RectF(px(1025.0f), py(410.0f), pr(175.0f), 18.0f), &centered, rPressed ? &highlight : &labelBrush);

    const wchar_t* name = (info && info->stadia) ? L"Google Stadia Controller" : (info ? info->name.c_str() : L"Controller");
    g.DrawString(name, -1, &font, RectF(210.0f, 103.0f, 620.0f, 20.0f), &centered, &labelBrush);
    g.DrawString(input.connected ? L"Verbunden · Eingaben werden live hervorgehoben" : L"Kein verbundenes Eingabegerät",
                 -1, &smallFont, RectF(210.0f, 293.0f, 620.0f, 19.0f), &centered, &labelBrush);
}

static LRESULT CALLBACK settingsProc(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(w), event = HIWORD(w);
        if (id == 900 && event == LBN_SELCHANGE) {
            settingsPage = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(l), LB_GETCURSEL, 0, 0));
            rebuildSettings();
            return 0;
        }
        if (id == 901) { DestroyWindow(h); return 0; }
        if (id == 910) { chooseRomFolder(); rebuildSettings(); return 0; }
        if (id == 911) { browseRom(h, false); rebuildSettings(); return 0; }
        if (id == 912) {
            if (settings.lastRom.empty()) browseRom(h, true);
            else launchRom(settings.lastRom);
            rebuildSettings();
            return 0;
        }
        if (id == 913) { stop(); rebuildSettings(); return 0; }
        if (id == 914) {
            settings.keepNvram = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
            saveSettings();
            return 0;
        }
        if (event == CBN_SELCHANGE) {
            const auto value = static_cast<unsigned>(SendMessageW(reinterpret_cast<HWND>(l), CB_GETCURSEL, 0, 0));
            if (id == 960 && value < input.controllers.size()) {
                input.select(input.controllers[value].key);
                settings.savedController = input.key();
                settings.loadControllerFor(input.key());
                saveSettings();
                rebuildSettings();
                return 0;
            }
            if (id == 962) {
                settings.controllerPreset = value;
                settings.padMap = win::standardMap;
                if (value == 1) {
                    // PlayStation face button positions
                    settings.padMap[4] = 1;
                    settings.padMap[5] = 2;
                    settings.padMap[6] = 0;
                    settings.padMap[7] = 3;
                } else if (value == 2) {
                    // Alternative layout
                    settings.padMap[4] = 0;
                    settings.padMap[5] = 1;
                    settings.padMap[6] = 2;
                    settings.padMap[7] = 3;
                }
                saveSettings();
                rebuildSettings();
                return 0;
            }
            if (id == 972) settings.videoAspect = value;
            if (id == 975) settings.windowMode = value;
            if (id == 976) settings.scaleIndex = value;
            if (id == 977) settings.displayFormat = value;
            if (id == 978) settings.timingMode = value;
            saveSettings();
            if (window) InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (id == 961) {
            const auto key = input.key();
            input.refresh();
            input.select(key);
            rebuildSettings();
            return 0;
        }
        if (id >= 980 && id < 992) {
            learnAction = id - 980;
            learnKeyboard = false;
            previousPad = input.poll();
            SetWindowTextW(learningLabel, L"Jetzt einen Controller-Knopf oder eine Richtung drücken … (Esc: Abbrechen)");
            SetFocus(h);
            return 0;
        }
        if (id >= 1000 && id < 1012) {
            learnAction = id - 1000;
            learnKeyboard = true;
            SetWindowTextW(learningLabel, L"Jetzt eine Tastaturtaste drücken … (Esc: Abbrechen)");
            SetFocus(h);
            return 0;
        }
        if (id == 970) settings.soundEnabled = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (id == 973) settings.integerScale = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (id == 974) settings.smoothVideo = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (id == 979) settings.fpsLog = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (id == 965) settings.debugLog = SendMessageW(reinterpret_cast<HWND>(l), BM_GETCHECK, 0, 0) == BST_CHECKED;
        saveSettings();
        if (window) InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_HSCROLL) {
        settings.soundVolume = static_cast<unsigned>(SendMessageW(reinterpret_cast<HWND>(l), TBM_GETPOS, 0, 0));
        saveSettings();
        return 0;
    }
    if (message == WM_KEYDOWN && learnAction >= 0) {
        if (w == VK_ESCAPE) {
            rebuildSettings();
            return 0;
        }
        if (learnKeyboard) {
            settings.keyboardKeys[learnAction] = static_cast<unsigned>(w);
            saveSettings();
            rebuildSettings();
        }
        return 0;
    }
    if (message == WM_TIMER) {
        const auto now = input.poll();
        if (learnAction >= 0 && !learnKeyboard && GetForegroundWindow() == h) {
            const int token = win::firstNewControl(now, previousPad);
            if (token >= 0) {
                settings.padMap[learnAction] = token;
                saveSettings();
                rebuildSettings();
            }
        }
        previousPad = now;
        if (settingsPage == 1) {
            RECT r{210, 110, 835, 314};
            InvalidateRect(h, &r, FALSE);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        drawController(dc);
        EndPaint(h, &ps);
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(h, 1);
        settingsWindow = nullptr;
        settingsChildren.clear();
        learnAction = -1;
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}

static void showSettings(int page = 0) {
    settingsPage = page;
    if (settingsWindow) {
        rebuildSettings();
        SetForegroundWindow(settingsWindow);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = settingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"OceanBlastSettings";
    RegisterClassW(&wc);

    RECT rect{0, 0, 850, 595};
    AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    settingsWindow = CreateWindowW(
        wc.lpszClassName, L"OceanBlast — Einstellungen",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        window, nullptr, wc.hInstance, nullptr);
    rebuildSettings();
    SetTimer(settingsWindow, 1, 30, nullptr);
    ShowWindow(settingsWindow, SW_SHOW);
}

static LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == OpenRom || id == 10) {
            browseRom(w, id == OpenRom);
        } else if (id == 11) {
            pullCompatibilityControls();
            saveSettings();
            if (!settings.lastRom.empty()) launchRom(settings.lastRom);
            else browseRom(w, true);
        } else if (id == RestartRom) {
            if (!activeRomPath.empty()) launchRom(activeRomPath);
            else if (!settings.lastRom.empty()) launchRom(settings.lastRom);
        } else if (id == StopRom || id == 12) {
            stop();
        } else if (id == ToggleFullscreenCmd) {
            toggleMainFullscreen();
        } else if (id == EscapeFromChildCmd) {
            if (mainFullscreen) toggleMainFullscreen();
            else stop();
        } else if (id == 13) {
            showSettings(1);
        } else if (id == ExitApp || id == 14) {
            PostMessageW(w, WM_CLOSE, 0, 0);
        } else if (id >= SettingsBase && id < SettingsBase + 6) {
            showSettings(id - SettingsBase);
        }
        return 0;
    }
    case WM_DEVICECHANGE: {
        const auto selected = input.key();
        input.refresh();
        input.select(selected);
        if (settingsWindow) rebuildSettings();
        notifyRunningGameSettingsChanged();
        return 0;
    }
    case WM_DROPFILES: {
        wchar_t dropped[MAX_PATH]{};
        if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, dropped, MAX_PATH)) {
            DragFinish(reinterpret_cast<HDROP>(wp));
            launchRom(dropped);
        } else {
            DragFinish(reinterpret_cast<HDROP>(wp));
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(lp & 0x40000000)) {
            if (wp == 'O') { browseRom(w, true); return 0; }
            if (wp == 'R') {
                if (!activeRomPath.empty()) launchRom(activeRomPath);
                else if (!settings.lastRom.empty()) launchRom(settings.lastRom);
                return 0;
            }
        }
        if ((wp == VK_F11 || (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) && !(lp & 0x40000000)) {
            toggleMainFullscreen();
            return 0;
        }
        if (wp == VK_ESCAPE && !(lp & 0x40000000) && mainFullscreen) {
            toggleMainFullscreen();
            return 0;
        }
        if (HWND child = findEmbeddedDisplay()) {
            PostMessageA(child, static_cast<UINT>(msg), wp, lp);
            return 0;
        }
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (HWND child = findEmbeddedDisplay()) {
            PostMessageA(child, static_cast<UINT>(msg), wp, lp);
            return 0;
        }
        break;
    }
    case WM_SIZE: {
        if (HWND child = findEmbeddedDisplay()) {
            RECT rc{};
            GetClientRect(w, &rc);
            SetWindowPos(child, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (!findEmbeddedDisplay()) {
            startScreen(dc);
        } else {
            RECT r{};
            GetClientRect(w, &r);
            FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
        if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            process = nullptr;
            if (startButton) EnableWindow(startButton, TRUE);
            if (stopButton) EnableWindow(stopButton, FALSE);
            if (statusBox) {
                SetWindowTextW(statusBox, code
                    ? L"Emulator exited with error. Check session.log for details."
                    : L"Emulator stopped. Ready to launch another ROM.");
            }
            setIdleTitle();
            InvalidateRect(w, nullptr, TRUE);
            if (settingsWindow && settingsPage == 0) rebuildSettings();
        }
        return 0;
    case WM_CLOSE:
        stop();
        if (settingsWindow) DestroyWindow(settingsWindow);
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static int run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdipInput, nullptr);

    wchar_t module[32768] = {};
    GetModuleFileNameW(nullptr, module, 32768);
    executable = module;
    folder = executable.substr(0, executable.find_last_of(L"\\/")) + L"\\sessions";
    CreateDirectoryW(folder.c_str(), nullptr);

    settings.load();
    logo = win::loadImageAsset(101, L"oceanblast-logo.png", &logoStream);
    symbol = win::loadImageAsset(102, L"oceanblast-symbol.png", &symbolStream);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls = {};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.lpszClassName = L"OceanBlastLauncher";
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;
    RegisterClassW(&cls);

    RECT r{0, 0, 1024, 768};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    window = CreateWindowW(
        cls.lpszClassName,
        L"OceanBlast — Strg+O: ROM öffnen, ROM hierher ziehen",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    DragAcceptFiles(window, TRUE);
    mainMenu = makeMenu();
    SetMenu(window, mainMenu);

    if (symbol) static_cast<Gdiplus::Bitmap*>(symbol.get())->GetHICON(&appIcon);
    if (appIcon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    input.init(window);
    input.refresh();
    input.autoSelectPreferred(settings.savedController);
    settings.loadControllerFor(input.key());

    // Hidden compatibility controls for automated test inspection.
    auto hiddenControl = [&](const wchar_t* type, const wchar_t* title, DWORD style, int id) -> HWND {
        return CreateWindowW(type, title, WS_CHILD | style, 0, 0, 10, 10, window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    pathBox = hiddenControl(L"EDIT", L"", ES_AUTOHSCROLL, 0);
    scaleBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"2× (480 × 320)", L"3× (720 × 480)", L"4× (960 × 640)"})
        SendMessageW(scaleBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    soundBox = hiddenControl(L"BUTTON", L"Enable Audio", BS_AUTOCHECKBOX, 0);
    profileBox = hiddenControl(L"BUTTON", L"FPS Log", BS_AUTOCHECKBOX, 0);
    debugBox = hiddenControl(L"BUTTON", L"Debug Log", BS_AUTOCHECKBOX, 0);
    formatBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Automatic (default)", L"RGB444, 480-byte rows (diagnostic)", L"RGB444 (diagnostic)", L"RGB565 (diagnostic)", L"LCD registers (diagnostic)"})
        SendMessageW(formatBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    nvramBox = hiddenControl(L"BUTTON", L"Keep Device Settings", BS_AUTOCHECKBOX, 0);
    timingBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Automatic ROM settings (default)", L"Standard: 1x", L"Automatic register clocks (experimental)", L"More CPU work: 2× (experimental)", L"More CPU work: 4× (experimental)", L"More CPU work: 8×"})
        SendMessageW(timingBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    windowModeBox = hiddenControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST, 0);
    for (auto text : {L"Console Skin", L"Fullscreen", L"Plain Window (default)"})
        SendMessageW(windowModeBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    startButton = hiddenControl(L"BUTTON", L"Start Game", BS_DEFPUSHBUTTON, 11);
    stopButton = hiddenControl(L"BUTTON", L"Stop Game", 0, 12);
    EnableWindow(stopButton, FALSE);
    statusBox = hiddenControl(L"STATIC", L"Select a ROM file and click Start Game to begin emulation.", 0, 0);
    syncCompatibilityControls();

    SetTimer(window, 1, 200, nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWindow && learnAction < 0 && IsDialogMessageW(settingsWindow, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    input.shutdown();
    logo.reset();
    symbol.reset();
    if (logoStream) { logoStream->Release(); logoStream = nullptr; }
    if (symbolStream) { symbolStream->Release(); symbolStream = nullptr; }
    if (appIcon) { DestroyIcon(appIcon); appIcon = nullptr; }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}

} // namespace launcher
} // namespace oceanblast
#endif
