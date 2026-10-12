// Included inside launcher namespace after its process/session state declarations.
static HWND musicWindow = nullptr, musicList = nullptr, musicName = nullptr, musicCapacity = nullptr;
static bool musicClosing = false;
static std::vector<oceanblast::Mp3Storage::Track> musicTracks;
static std::filesystem::path musicStoragePath() { return std::filesystem::path(folder) / L"mp3-storage.img"; }
static void musicError(HWND owner, const std::exception& error) {
    std::string message = error.what();
    std::wstring wide(message.begin(), message.end());
    MessageBoxW(owner, wide.c_str(), L"MP3 Storage", MB_ICONERROR);
}
static bool isMp3Rom(const std::filesystem::path& rom) {
    std::error_code error;
    if (std::filesystem::file_size(rom, error) != 17301504 || error) return false;
    std::ifstream file(rom, std::ios::binary);
    std::array<char, 65536> block{};
    uint32_t crc = 0xffffffff;
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) { auto c = n; for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); t[n] = c; }
        return t;
    }();
    while (file) {
        file.read(block.data(), block.size());
        for (std::streamsize i = 0; i < file.gcount(); ++i) crc = table[(crc ^ uint8_t(block[i])) & 255] ^ (crc >> 8);
    }
    return file.eof() && (crc ^ 0xffffffff) == 0xb048a789u;
}
static bool createMusicStorage(HWND owner) {
    if (std::filesystem::exists(musicStoragePath())) return true;
    if (MessageBoxW(owner, L"The MP3 cartridge needs a 256 MB storage image.\nCreate it now?",
                    L"MP3 Storage", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1) != IDYES) return false;
    try {
        oceanblast::Mp3Storage::create(musicStoragePath());
        auto message = L"Storage created in:\n" + musicStoragePath().wstring();
        MessageBoxW(owner, message.c_str(), L"MP3 Storage", MB_OK | MB_ICONINFORMATION);
        return true;
    }
    catch (const std::exception& error) { musicError(owner, error); return false; }
}
static bool importMusic(HWND owner) {
    std::vector<wchar_t> selected(65536, 0);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrTitle = L"Add MP3 files to storage";
    dialog.lpstrFilter = L"MP3 audio (*.mp3)\0*.mp3\0\0";
    dialog.lpstrFile = selected.data(); dialog.nMaxFile = DWORD(selected.size());
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError()) MessageBoxW(owner, L"Could not open the file selection. Try selecting fewer files.", L"MP3 Storage", MB_ICONERROR);
        return false;
    }
    std::vector<std::filesystem::path> files;
    const wchar_t* next = selected.data() + wcslen(selected.data()) + 1;
    if (!*next) files.emplace_back(selected.data());
    else for (; *next; next += wcslen(next) + 1) files.push_back(std::filesystem::path(selected.data()) / next);
    try { oceanblast::Mp3Storage(musicStoragePath()).add(files); return true; }
    catch (const std::exception& error) { musicError(owner, error); return false; }
}
static void refreshMusic() {
    SendMessageW(musicList, LB_RESETCONTENT, 0, 0); musicTracks.clear();
    try {
        oceanblast::Mp3Storage storage(musicStoragePath()); musicTracks = storage.tracks();
        for (const auto& track : musicTracks) {
            std::wstring row(track.name.begin(), track.name.end());
            row += L"    (" + std::to_wstring((track.size + 1023ull) / 1024) + L" KB)";
            SendMessageW(musicList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        }
        auto text = L"256 MB storage · " + std::to_wstring(storage.freeBytes() / 1024 / 1024) + L" MB free · " + std::to_wstring(musicTracks.size()) + L" track(s)";
        SetWindowTextW(musicCapacity, text.c_str());
    } catch (const std::exception& error) { musicError(musicWindow, error); }
    SetWindowTextW(musicName, L"");
    EnableWindow(GetDlgItem(musicWindow, 302), FALSE); EnableWindow(GetDlgItem(musicWindow, 303), FALSE);
}
static LRESULT CALLBACK musicProcedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        const int id = LOWORD(wp);
        auto index = SendMessageW(musicList, LB_GETCURSEL, 0, 0);
        if (id == 300 && HIWORD(wp) == LBN_SELCHANGE && index >= 0 && size_t(index) < musicTracks.size()) {
            auto& name = musicTracks[size_t(index)].name;
            std::wstring wide(name.begin(), name.end()); SetWindowTextW(musicName, wide.c_str());
            EnableWindow(GetDlgItem(w, 302), TRUE); EnableWindow(GetDlgItem(w, 303), TRUE);
        } else if (id == 301) { importMusic(w); refreshMusic(); }
        else if ((id == 302 || id == 303) && index >= 0 && size_t(index) < musicTracks.size()) {
            try {
                oceanblast::Mp3Storage storage(musicStoragePath());
                if (id == 302) {
                    wchar_t name[256]{}; GetWindowTextW(musicName, name, 256);
                    storage.rename(size_t(index), std::u16string(name, name + wcslen(name)));
                } else {
                    auto& name = musicTracks[size_t(index)].name;
                    auto prompt = L"Delete \"" + std::wstring(name.begin(), name.end()) + L"\" from MP3 storage?";
                    if (MessageBoxW(w, prompt.c_str(), L"MP3 Storage", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                    storage.remove(size_t(index));
                }
                refreshMusic();
            } catch (const std::exception& error) { musicError(w, error); }
        } else if (id == 307) {
            if (MessageBoxW(w, L"Remove the entire MP3 storage image and all music stored in it?\nYour original MP3 files and cartridge dump will remain unchanged.",
                L"Remove MP3 Storage", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
            try {
                if (!std::filesystem::remove(musicStoragePath())) throw std::runtime_error("Storage was not found.");
                if (statusBox) SetWindowTextW(statusBox, L"MP3 storage removed. The next MP3 launch will offer to create a new store.");
                DestroyWindow(w);
            } catch (const std::exception& error) { musicError(w, error); }
        } else if (id == IDOK || id == IDCANCEL) DestroyWindow(w);
        return 0;
    }
    if (msg == WM_CLOSE) { DestroyWindow(w); return 0; }
    if (msg == WM_DESTROY) { musicClosing = true; musicWindow = nullptr; return 0; }
    return DefWindowProcW(w, msg, wp, lp);
}
static void showMusicManager() {
    if (process && WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
        MessageBoxW(window, L"Stop the emulator before editing MP3 storage.", L"MP3 Storage", MB_ICONINFORMATION); return;
    }
    const bool existed = std::filesystem::exists(musicStoragePath());
    if (!createMusicStorage(window)) return;
    try { oceanblast::Mp3Storage storage(musicStoragePath()); }
    catch (const std::exception& error) { musicError(window, error); return; }
    WNDCLASSW cls{}; cls.lpfnWndProc = musicProcedure; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"OceanBlastMp3Storage"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassW(&cls);
    RECT owner{}; GetWindowRect(window, &owner);
    musicClosing = false;
    musicWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"OceanBlast — MP3 Storage",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, owner.left + 40, owner.top + 40, 640, 460, window, nullptr, cls.hInstance, nullptr);
    if (!musicWindow) return;
    auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
        HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, musicWindow,
            reinterpret_cast<HMENU>(INT_PTR(id)), cls.hInstance, nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return child;
    };
    musicCapacity = control(L"STATIC", L"", 0, 16, 16, 600, 22, 305);
    musicList = control(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                        16, 46, 592, 245, 300);
    control(L"STATIC", L"Filename (including .mp3):", 0, 16, 304, 235, 20, 306);
    musicName = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 16, 327, 360, 25, 304);
    SendMessageW(musicName, EM_SETLIMITTEXT, 255, 0);
    control(L"BUTTON", L"Rename", WS_TABSTOP | BS_PUSHBUTTON, 388, 327, 100, 26, 302);
    control(L"BUTTON", L"Delete", WS_TABSTOP | BS_PUSHBUTTON, 500, 327, 108, 26, 303);
    control(L"BUTTON", L"Add MP3 files...", WS_TABSTOP | BS_PUSHBUTTON, 16, 371, 160, 30, 301);
    control(L"BUTTON", L"Remove Storage...", WS_TABSTOP | BS_PUSHBUTTON, 190, 371, 160, 30, 307);
    control(L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON, 500, 371, 108, 30, IDOK);
    EnableWindow(window, FALSE); if (settingsWindow) EnableWindow(settingsWindow, FALSE);
    ShowWindow(musicWindow, SW_SHOW); refreshMusic();
    if (!existed) { importMusic(musicWindow); refreshMusic(); }
    MSG message{};
    while (!musicClosing && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(musicWindow, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(window, TRUE); if (settingsWindow) EnableWindow(settingsWindow, TRUE); SetForegroundWindow(window);
}
