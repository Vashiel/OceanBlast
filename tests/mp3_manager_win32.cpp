#include "display/launcher.h"
#include <iostream>
#include <chrono>
static int failures = 0;
static void check(const char* name, bool ok) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static int answer = IDNO;
static bool creationNotice = false;
static bool savePreview(HWND w) {
    RECT rc{}; GetWindowRect(w, &rc); HDC dc = GetDC(w), memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
    auto old = SelectObject(memory, bitmap); bool ok = PrintWindow(w, memory, 0);
    UINT count = 0, size = 0; Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> bytes(size); auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    Gdiplus::GetImageEncoders(count, size, encoders); bool saved = false;
    if (ok) { Gdiplus::Bitmap image(bitmap, nullptr); for (UINT i = 0; i < count; ++i)
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) { saved = image.Save(L"build/mp3-storage-preview.png", &encoders[i].Clsid, nullptr) == Gdiplus::Ok; break; } }
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(w, dc); return saved;
}
static void CALLBACK answerPrompt(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    EnumWindows([](HWND w, LPARAM) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        wchar_t cls[80]{}; GetClassNameW(w, cls, 80);
        if (std::wstring(cls) == L"#32770") {
            if (GetDlgItem(w, IDOK)) {
                PostMessageW(w, WM_COMMAND, IDOK, 0);
                creationNotice = false;
            }
            else SendMessageW(w, WM_COMMAND, answer, 0);
        }
        return TRUE;
    }, 0);
    if (creationNotice) SetTimer(nullptr, 0, 200, answerPrompt);
}
static void CALLBACK inspectManager(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace oceanblast::launcher;
    KillTimer(nullptr, timer);
    check("Storage manager opens and disables its launcher owner", musicWindow && !IsWindowEnabled(window));
    check("Imported MP3 is displayed in the list", SendMessageW(musicList, LB_GETCOUNT, 0, 0) == 1);
    check("Add MP3 files button is available", IsWindowEnabled(GetDlgItem(musicWindow, 301)));
    SendMessageW(musicList, LB_SETCURSEL, 0, 0);
    SendMessageW(musicWindow, WM_COMMAND, MAKEWPARAM(300, LBN_SELCHANGE), reinterpret_cast<LPARAM>(musicList));
    check("Selecting a track enables rename and delete", IsWindowEnabled(GetDlgItem(musicWindow, 302)) && IsWindowEnabled(GetDlgItem(musicWindow, 303)));
    UpdateWindow(musicWindow); check("Storage manager preview rendered", savePreview(musicWindow));
    SetWindowTextW(musicName, L"Renamed music.mp3"); SendMessageW(musicWindow, WM_COMMAND, 302, 0);
    const auto renamedTracks = oceanblast::Mp3Storage(musicStoragePath()).tracks();
    const auto renamed = std::find_if(renamedTracks.begin(), renamedTracks.end(), [](const auto& track) { return track.name == u"Renamed music.mp3"; });
    check("Rename button persists the edited filename", renamed != renamedTracks.end());
    SendMessageW(musicList, LB_SETCURSEL, size_t(renamed - renamedTracks.begin()), 0);
    SendMessageW(musicWindow, WM_COMMAND, MAKEWPARAM(300, LBN_SELCHANGE), reinterpret_cast<LPARAM>(musicList));
    answer = IDNO; SetTimer(nullptr, 0, 100, answerPrompt); SendMessageW(musicWindow, WM_COMMAND, 303, 0);
    check("Canceling deletion preserves the track", oceanblast::Mp3Storage(musicStoragePath()).tracks().size() == 1);
    answer = IDYES; SetTimer(nullptr, 0, 100, answerPrompt); SendMessageW(musicWindow, WM_COMMAND, 303, 0);
    check("Confirmed deletion updates the list and image", oceanblast::Mp3Storage(musicStoragePath()).tracks().empty() && SendMessageW(musicList, LB_GETCOUNT, 0, 0) == 0);
    check("Remove entire storage action is available", IsWindowEnabled(GetDlgItem(musicWindow, 307)));
    answer = IDNO; SetTimer(nullptr, 0, 100, answerPrompt); SendMessageW(musicWindow, WM_COMMAND, 307, 0);
    check("Canceling removal preserves the whole store", musicWindow && std::filesystem::exists(musicStoragePath()));
    answer = IDYES; SetTimer(nullptr, 0, 100, answerPrompt); SendMessageW(musicWindow, WM_COMMAND, 307, 0);
    check("Confirmed removal deletes the image and closes management", !musicWindow && !std::filesystem::exists(musicStoragePath()));
    check("Removing storage preserves the original source MP3", std::filesystem::file_size(musicStoragePath().parent_path() / "Test.mp3") == 16);
}
static std::filesystem::path testFolder;
static void CALLBACK inspect(HWND, UINT, UINT_PTR timer, DWORD) {
    using namespace oceanblast::launcher;
    KillTimer(nullptr, timer); folder = testFolder.wstring();
    answer = IDNO; SetTimer(nullptr, 0, 100, answerPrompt);
    check("No cancels initial storage creation", !createMusicStorage(window) && !std::filesystem::exists(musicStoragePath()));
    answer = IDYES; creationNotice = true; SetTimer(nullptr, 0, 100, answerPrompt);
    check("Yes creates a reusable 256 MB image", createMusicStorage(window) && std::filesystem::file_size(musicStoragePath()) == oceanblast::Mp3Storage::capacity);
    auto track = testFolder / "Test.mp3"; { std::ofstream f(track); f << "test mp3 payload"; }
    oceanblast::Mp3Storage(musicStoragePath()).add({track});
    SetTimer(nullptr, 0, 200, inspectManager);
    SendMessageW(window, WM_COMMAND, ManageMp3Storage, 0);
    check("Closing management restores the launcher", !musicWindow && IsWindowEnabled(window));
    oceanblast::Mp3Storage::create(musicStoragePath());
    // Optional private-ROM integration; the child captures argv instead of booting.
    const auto rom = std::filesystem::absolute("roms/hardware/MP3 player 256MB/MP3 player 256MB.bin");
    if (std::filesystem::exists(rom)) {
        check("MP3 recognition uses the known dump identity", isMp3Rom(rom));
        const auto args = testFolder / "child-arguments.txt";
        SetEnvironmentVariableW(L"OCEANBLAST_TEST_CHILD_ARGS", args.c_str());
        const bool launched = launchRom(rom.wstring());
        const bool exited = launched && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
        std::ifstream f(args); std::string arguments((std::istreambuf_iterator<char>(f)), {}); f.close();
        check("MP3 launch automatically attaches the existing store", exited && arguments.find("--usb-storage\n" + musicStoragePath().string()) != std::string::npos);
        stop(); std::filesystem::remove(musicStoragePath());
        answer = IDNO; SetTimer(nullptr, 0, 100, answerPrompt);
        check("Declining storage cancels MP3 launch before creating a child", !launchRom(rom.wstring()) && !process && !std::filesystem::exists(musicStoragePath()));
    } else std::cout << "SKIP private MP3 dump launch integration (ROM not supplied)\n";
    PostMessageW(window, WM_CLOSE, 0, 0);
}
int main(int argc, char** argv) {
    if (argc > 1) {
        wchar_t capture[32768]{}; GetEnvironmentVariableW(L"OCEANBLAST_TEST_CHILD_ARGS", capture, 32768);
        if (!capture[0]) return 2;
        std::ofstream f{std::filesystem::path(capture)};
        for (int i = 1; i < argc; ++i) f << argv[i] << '\n';
        return f ? 0 : 1;
    }
    std::cout.setf(std::ios::unitbuf);
    testFolder = std::filesystem::temp_directory_path() / ("oceanblast-manager-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(testFolder)) return 2;
    auto config = testFolder / "settings.ini"; SetEnvironmentVariableW(L"OCEANBLAST_SETTINGS_PATH", config.c_str());
    SetTimer(nullptr, 0, 200, inspect);
    const auto result = oceanblast::launcher::run();
    std::filesystem::remove_all(testFolder);
    return failures || result ? 1 : 0;
}
