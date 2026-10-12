#include "core/mp3_storage.h"
#include <iostream>
#include <chrono>
using oceanblast::Mp3Storage;
static int failures = 0;
static void check(const char* name, bool ok) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static uint64_t hash(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary); std::array<char, 65536> bytes{}; uint64_t h = 1469598103934665603ull;
    while (f) { f.read(bytes.data(), bytes.size()); for (std::streamsize i = 0; i < f.gcount(); ++i) h = (h ^ uint8_t(bytes[i])) * 1099511628211ull; }
    return h;
}
int main() {
    auto dir = std::filesystem::temp_directory_path() / ("oceanblast-mp3-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(dir)) return 2;
    auto image = dir / "music.img";
    try {
        Mp3Storage::create(image); Mp3Storage storage(image); const auto free = storage.freeBytes();
        check("New image has 256 MiB and an empty FAT16 directory", std::filesystem::file_size(image) == Mp3Storage::capacity && storage.tracks().empty());
        auto emptyHash = hash(image); bool refused = false;
        try { Mp3Storage::create(image); } catch (...) { refused = true; }
        check("Creation never overwrites existing storage", refused && hash(image) == emptyHash);
        auto a = dir / std::filesystem::path(u"A long track title \u00e4.mp3"), b = dir / "Second.mp3";
        { std::ofstream f(a, std::ios::binary); for (int n = 0; n < 9000; ++n) f.put(char(n)); }
        { std::ofstream f(b, std::ios::binary); f << "second track"; }
        storage.add({a, b}); auto list = storage.tracks();
        check("Multi-file import preserves long Unicode filenames and lengths", list.size() == 2 && list[0].name == a.filename().u16string() && list[0].size == 9000 && list[1].size == 12);
        check("Import accounts for allocated clusters", storage.freeBytes() == free - 4 * 4096);
        std::ifstream disk(image, std::ios::binary); disk.seekg(546 * 512);
        std::vector<char> payload(9000); disk.read(payload.data(), payload.size()); bool exact = true;
        for (int n = 0; n < 9000; ++n) exact &= payload[n] == char(n);
        disk.close(); check("Imported payload bytes match the source", exact);
        storage.rename(0, u"Renamed track.mp3"); list = storage.tracks();
        auto renamed = std::find_if(list.begin(), list.end(), [](const auto& t) { return t.name == u"Renamed track.mp3"; });
        check("Rename keeps the original cluster chain and file length", renamed != list.end() && renamed->first == 2 && renamed->size == 9000);
        auto before = hash(image); refused = false;
        try { storage.add({b}); } catch (...) { refused = true; }
        check("Duplicate import rolls back the entire transaction", refused && hash(image) == before);
        refused = false;
        try { storage.add({a, dir / "missing.mp3"}); } catch (...) { refused = true; }
        check("Failed batch import preserves existing storage", refused && hash(image) == before);
        refused = false;
        try { storage.rename(0, u"../escape.mp3"); } catch (...) { refused = true; }
        check("Invalid rename leaves storage unchanged", refused && hash(image) == before);
        { std::ofstream f(image.string() + ".editing"); f << "preserve"; }
        refused = false; try { storage.add({a}); } catch (...) { refused = true; }
        check("Existing transaction file is preserved", refused && std::filesystem::file_size(image.string() + ".editing") == 8 && hash(image) == before);
        std::filesystem::remove(image.string() + ".editing");
#ifdef _WIN32
        HANDLE guest = CreateFileW(image.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        refused = false; try { storage.add({a}); } catch (...) { refused = true; }
        check("Editing refuses an image opened by a guest writer", guest != INVALID_HANDLE_VALUE && refused && hash(image) == before);
        if (guest != INVALID_HANDLE_VALUE) CloseHandle(guest);
#endif
        storage.reload(); list = storage.tracks();
        auto it = std::find_if(list.begin(), list.end(), [](const auto& t) { return t.name == u"Renamed track.mp3"; });
        storage.remove(size_t(it - list.begin()));
        check("Deletion frees only the selected track's clusters", storage.tracks().size() == 1 && storage.freeBytes() == free - 4096);
        storage.remove(0); check("Deleting all tracks restores full free space", storage.tracks().empty() && storage.freeBytes() == free);
        auto zero = dir / "Empty.mp3"; { std::ofstream f(zero); }
        storage.add({zero}); storage.remove(0);
        check("Zero-length files can be removed", storage.tracks().empty() && storage.freeBytes() == free);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    std::filesystem::remove_all(dir); return failures ? 1 : 0;
}
