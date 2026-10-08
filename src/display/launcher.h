#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>

namespace oceanblast {
namespace launcher {
static HWND pathBox, scaleBox, soundBox, profileBox, debugBox, formatBox, nvramBox, startButton, stopButton, statusBox;
static HANDLE process = nullptr;
static std::wstring executable, folder;
static void stop() {
    if (process) {
        // Ask the emulator's display to close, allowing normal cleanup.
        const DWORD pid = GetProcessId(process);
        EnumWindows([](HWND w, LPARAM p)->BOOL {
            DWORD owner = 0; GetWindowThreadProcessId(w, &owner);
            if (owner == static_cast<DWORD>(p)) PostMessageW(w, WM_CLOSE, 0, 0);
            return TRUE;
        }, static_cast<LPARAM>(pid));
    }
}
static LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case 10: {
            wchar_t filename[32768] = {};
            OPENFILENAMEW dialog = {}; dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = w; dialog.lpstrFile = filename; dialog.nMaxFile = 32768;
            dialog.lpstrFilter = L"ROM Files (*.bin;*.rom)\0*.bin;*.rom\0All Files (*.*)\0*.*\0";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&dialog)) SetWindowTextW(pathBox, filename);
            break;
        }
        case 11: {
            wchar_t filename[32768] = {}; GetWindowTextW(pathBox, filename, 32768);
            DWORD attrs = GetFileAttributesW(filename);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxW(w, L"Please select an existing ROM file first.", L"OceanBlast", MB_ICONWARNING); break;
            }
            const int scale = static_cast<int>(SendMessageW(scaleBox, CB_GETCURSEL, 0, 0)) + 2;
            std::wstring command = L"\"" + executable + L"\" \"" + filename + L"\" --gui --scale " + std::to_wstring(scale);
            if (SendMessageW(soundBox, BM_GETCHECK, 0, 0) == BST_CHECKED) command += L" --sound";
            if (SendMessageW(profileBox, BM_GETCHECK, 0, 0) == BST_CHECKED) command += L" --profile";
            if (SendMessageW(debugBox, BM_GETCHECK, 0, 0) == BST_CHECKED) command += L" --debug";
            const int format = static_cast<int>(SendMessageW(formatBox, CB_GETCURSEL, 0, 0));
            if (format == 1) command += L" --display-format rgb444 --display-stride 480";
            else if (format == 2) command += L" --display-format rgb444";
            else if (format == 3) command += L" --display-format rgb565";
            if (SendMessageW(nvramBox, BM_GETCHECK, 0, 0) == BST_CHECKED)
                command += L" --nvram \"" + folder + L"\\board.nvram\"";
            SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
            HANDLE log = CreateFileW((folder + L"\\session.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
            if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
                if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
                if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
                MessageBoxW(w, L"Failed to open session log file.", L"OceanBlast", MB_ICONERROR); break;
            }
            STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = log; startup.hStdError = log; startup.hStdInput = input;
            PROCESS_INFORMATION child = {};
            BOOL ok = CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &child);
            CloseHandle(log); CloseHandle(input);
            if (!ok) { MessageBoxW(w, L"Failed to start emulator process.", L"OceanBlast", MB_ICONERROR); break; }
            process = child.hProcess; CloseHandle(child.hThread);
            EnableWindow(startButton, FALSE); EnableWindow(stopButton, TRUE);
            SetWindowTextW(statusBox, L"Emulator is running. Game display opened in its own window.");
            break;
        }
        case 12: stop(); break;
        case 13: MessageBoxW(w, L"Arrow Keys: D-Pad\nZ / K: Button A    X / J: Button B\nA / Q: Shoulder L    S / W: Shoulder R\nEnter: Start    Space: Select\nEsc: Close Game\n\nF5: Pause / Resume\nF6: Single CPU Step\nF7: Save Framebuffer & CPU State\n\nTitle Bar: Display FPS, Frame Flips/s, MIPS, PC, FB.\nClick the game window to focus controls.", L"Controls & Diagnostics", MB_OK); break;
        case 14: PostMessageW(w, WM_CLOSE, 0, 0); break;
        }
        return 0;
    case WM_TIMER:
        if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0; GetExitCodeProcess(process, &code); CloseHandle(process); process = nullptr;
            EnableWindow(startButton, TRUE); EnableWindow(stopButton, FALSE);
            SetWindowTextW(statusBox, code ? L"Emulator exited with error. Check session.log for details." : L"Emulator stopped. Ready to launch another ROM.");
        }
        return 0;
    case WM_CLOSE:
        if (process) { stop(); SetWindowTextW(statusBox, L"Stopping game. Launcher window will close shortly."); return 0; }
        DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
static int run() {
    wchar_t module[32768] = {}; GetModuleFileNameW(nullptr, module, 32768);
    executable = module; folder = executable.substr(0, executable.find_last_of(L"\\/")) + L"\\sessions";
    CreateDirectoryW(folder.c_str(), nullptr);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls = {}; cls.lpfnWndProc = procedure; cls.hInstance = instance;
    cls.lpszClassName = L"OceanBlastLauncher"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    HWND window = CreateWindowW(cls.lpszClassName, L"OceanBlast – digiBLAST Launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 640, 350, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    auto control = [&](const wchar_t* type, const wchar_t* title, DWORD style, int x, int y, int width, int height, int id)->HWND {
        HWND c = CreateWindowW(type, title, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return c;
    };
    control(L"STATIC", L"Nikko digiBLAST Emulator", 0, 20, 15, 580, 25, 0);
    pathBox = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 20, 50, 455, 26, 0);
    control(L"BUTTON", L"Browse ROM…", WS_TABSTOP, 485, 50, 120, 26, 10);
    control(L"STATIC", L"Window Scale", 0, 20, 95, 100, 24, 0);
    scaleBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 125, 90, 145, 120, 0);
    for (auto text : {L"2× (480 × 320)", L"3× (720 × 480)", L"4× (960 × 640)"}) SendMessageW(scaleBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(scaleBox, CB_SETCURSEL, 1, 0);
    soundBox = control(L"BUTTON", L"Enable Audio", BS_AUTOCHECKBOX | WS_TABSTOP, 270, 90, 130, 28, 0);
    profileBox = control(L"BUTTON", L"FPS Log", BS_AUTOCHECKBOX | WS_TABSTOP, 405, 90, 95, 28, 0);
    SendMessageW(profileBox, BM_SETCHECK, BST_CHECKED, 0);
    debugBox = control(L"BUTTON", L"Debug Log", BS_AUTOCHECKBOX | WS_TABSTOP, 505, 90, 100, 28, 0);
    control(L"STATIC", L"Display Decoder", 0, 20, 132, 105, 24, 0);
    formatBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 125, 128, 260, 150, 0);
    for (auto text : {L"LCD registers (default)", L"RGB444, 480-byte rows (diagnostic)", L"RGB444 (diagnostic)", L"RGB565 (diagnostic)"}) SendMessageW(formatBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(formatBox, CB_SETCURSEL, 0, 0);
    nvramBox = control(L"BUTTON", L"Keep Device Settings", BS_AUTOCHECKBOX | WS_TABSTOP, 400, 128, 205, 28, 0);
    SendMessageW(nvramBox, BM_SETCHECK, BST_CHECKED, 0);
    startButton = control(L"BUTTON", L"Start Game", BS_DEFPUSHBUTTON | WS_TABSTOP, 20, 175, 140, 32, 11);
    stopButton = control(L"BUTTON", L"Stop Game", WS_TABSTOP, 175, 175, 140, 32, 12); EnableWindow(stopButton, FALSE);
    control(L"BUTTON", L"Controls", WS_TABSTOP, 330, 175, 125, 32, 13);
    statusBox = control(L"STATIC", L"Select a ROM file and click Start Game to begin emulation.", 0, 20, 225, 580, 45, 0);
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, 10, L"Load ROM…"); AppendMenuW(file, MF_STRING, 14, L"Exit");
    AppendMenuW(help, MF_STRING, 13, L"Controls"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help"); SetMenu(window, menu);
    SetTimer(window, 1, 250, nullptr); ShowWindow(window, SW_SHOW);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
} // namespace launcher
} // namespace oceanblast
#endif
