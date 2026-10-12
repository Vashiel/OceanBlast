#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace oceanblast {
// The launcher edits an isolated FAT16 image, never a physical disk or cartridge.
class Mp3Storage {
    using Bytes = std::vector<uint8_t>;
    std::filesystem::path path;
    Bytes fat, root;
    uint32_t partition = 0, data = 0, rootSector = 0, fatSector = 0;
    uint32_t clusterBytes = 0, clusters = 0, fatSectors = 0, rootSlots = 0;
    static uint16_t r16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
    static uint32_t r32(const uint8_t* p) { return r16(p) | uint32_t(r16(p + 2)) << 16; }
    static void w16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
    static void w32(uint8_t* p, uint32_t v) { w16(p, uint16_t(v)); w16(p + 2, uint16_t(v >> 16)); }
    static void read(std::istream& f, uint64_t offset, uint8_t* p, size_t n) {
        f.seekg(offset); f.read(reinterpret_cast<char*>(p), n);
        if (!f) throw std::runtime_error("Could not read MP3 storage.");
    }
    static void write(std::ostream& f, uint64_t offset, const uint8_t* p, size_t n) {
        f.seekp(offset); f.write(reinterpret_cast<const char*>(p), n);
        if (!f) throw std::runtime_error("Could not write MP3 storage.");
    }
    std::vector<uint16_t> chain(uint16_t first, uint32_t size) const {
        std::vector<uint16_t> result;
        if (!first && !size) return result;
        std::set<uint16_t> seen;
        for (uint16_t c = first; c < 0xfff8;) {
            if (c < 2 || c >= clusters + 2 || !seen.insert(c).second)
                throw std::runtime_error("Invalid FAT chain. Storage was not modified.");
            result.push_back(c); c = r16(&fat[c * 2]);
        }
        if (uint64_t(result.size()) * clusterBytes < size)
            throw std::runtime_error("Truncated FAT chain. Storage was not modified.");
        return result;
    }
    static uint8_t checksum(const uint8_t* name) {
        uint8_t sum = 0;
        for (int i = 0; i < 11; ++i) sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + name[i]);
        return sum;
    }
    static void validateName(const std::u16string& name) {
        if (name.size() < 5 || name.size() > 255 || name.back() == u' ' || name.back() == u'.')
            throw std::runtime_error("Use a filename ending in .mp3 (up to 255 characters).");
        auto suffix = name.substr(name.size() - 4);
        for (auto& c : suffix) if (c >= u'A' && c <= u'Z') c += 32;
        if (suffix != u".mp3") throw std::runtime_error("The filename must end in .mp3.");
        for (auto c : name) if (c < 32 || std::u16string(u"\\/:*?\"<>|").find(c) != std::u16string::npos)
            throw std::runtime_error("The filename contains an unsupported character.");
    }
public:
    static constexpr uint64_t capacity = 256ull * 1024 * 1024;
    struct Track { std::u16string name; uint32_t size; uint16_t first; size_t slot; std::vector<size_t> slots; };
    explicit Mp3Storage(const std::filesystem::path& image) : path(image) { reload(); }
    void reload() {
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != capacity)
            throw std::runtime_error("Expected a 256 MB regular storage image.");
        std::ifstream f(path, std::ios::binary);
        std::array<uint8_t, 512> mbr{}, boot{};
        read(f, 0, mbr.data(), 512);
        partition = r32(&mbr[454]);
        const auto sectors = r32(&mbr[458]);
        if (r16(&mbr[510]) != 0xaa55 || !partition || uint64_t(partition) + sectors > capacity / 512)
            throw std::runtime_error("Unsupported storage partition. Storage was not modified.");
        read(f, uint64_t(partition) * 512, boot.data(), 512);
        auto reserved = r16(&boot[14]);
        auto spc = boot[13];
        fatSectors = r16(&boot[22]); rootSlots = r16(&boot[17]);
        uint32_t total = r16(&boot[19]); if (!total) total = r32(&boot[32]);
        const uint32_t rootSectors = (rootSlots * 32 + 511) / 512;
        const uint32_t overhead = reserved + 2 * fatSectors + rootSectors;
        if (r16(&boot[510]) != 0xaa55 || r16(&boot[11]) != 512 || boot[16] != 2 ||
            !reserved || !spc || (spc & (spc - 1)) || !fatSectors || !rootSlots ||
            total > sectors || total <= overhead)
            throw std::runtime_error("Unsupported FAT16 geometry. Storage was not modified.");
        clusters = (total - overhead) / spc;
        if (clusters < 4085 || clusters >= 65525 || (clusters + 2) * 2 > fatSectors * 512)
            throw std::runtime_error("Unsupported FAT16 capacity. Storage was not modified.");
        clusterBytes = spc * 512;
        fatSector = partition + reserved; rootSector = fatSector + 2 * fatSectors;
        data = rootSector + rootSectors;
        fat.resize(fatSectors * 512); root.resize(rootSlots * 32);
        read(f, uint64_t(fatSector) * 512, fat.data(), fat.size());
        Bytes other(fat.size()); read(f, uint64_t(fatSector + fatSectors) * 512, other.data(), other.size());
        if (other != fat) throw std::runtime_error("FAT copies differ. Storage was not modified.");
        read(f, uint64_t(rootSector) * 512, root.data(), root.size());
        // Refuse cross-linked or malformed root-file chains before any edit.
        std::set<uint16_t> owned;
        for (size_t s = 0; s < rootSlots; ++s) {
            const auto* p = &root[s * 32];
            if (!p[0]) break;
            if (p[0] == 0xe5 || p[11] == 15 || (p[11] & 8)) continue;
            for (auto c : chain(r16(p + 26), r32(p + 28)))
                if (!owned.insert(c).second) throw std::runtime_error("Cross-linked FAT chains. Storage was not modified.");
        }
    }
    std::vector<Track> tracks() const {
        std::vector<Track> result;
        std::vector<size_t> pending;
        for (size_t s = 0; s < rootSlots; ++s) {
            auto* p = &root[s * 32];
            if (!p[0]) break;
            if (p[0] == 0xe5) { pending.clear(); continue; }
            if (p[11] == 0x0f) { pending.push_back(s); continue; }
            if (p[11] & 0x18) { pending.clear(); continue; }
            std::u16string name;
            bool valid = !pending.empty() && pending.size() <= 20 &&
                root[pending.front() * 32] == (0x40 | pending.size());
            if (valid) {
                name.resize(pending.size() * 13, u'\xffff');
                constexpr int positions[]{1,3,5,7,9,14,16,18,20,22,24,28,30};
                for (size_t i = 0; i < pending.size(); ++i) {
                    auto* l = &root[pending[i] * 32];
                    size_t ordinal = l[0] & 31;
                    if (ordinal != pending.size() - i || l[13] != checksum(p) || l[12] || r16(l + 26)) { valid = false; break; }
                    for (int j = 0; j < 13; ++j) name[(ordinal - 1) * 13 + j] = r16(l + positions[j]);
                }
                const auto end = name.find_first_of(std::u16string{u'\0', u'\xffff'});
                if (end != std::u16string::npos) name.resize(end);
            }
            if (!valid) {
                pending.clear(); name.clear();
                for (int j = 0; j < 8 && p[j] != ' '; ++j) name += char16_t(p[j]);
                if (p[8] != ' ') { name += u'.'; for (int j = 8; j < 11 && p[j] != ' '; ++j) name += char16_t(p[j]); }
            }
            auto extension = name.size() >= 4 ? name.substr(name.size() - 4) : u"";
            for (auto& c : extension) if (c >= u'A' && c <= u'Z') c += 32;
            if (extension == u".mp3") { auto slots = pending; slots.push_back(s); result.push_back({name, r32(p + 28), r16(p + 26), s, slots}); }
            pending.clear();
        }
        return result;
    }
    uint64_t freeBytes() const {
        uint64_t count = 0;
        for (uint32_t c = 2; c < clusters + 2; ++c) count += r16(&fat[c * 2]) == 0;
        return count * clusterBytes;
    }
    void erase(const Track& t) {
        for (auto c : chain(t.first, t.size)) w16(&fat[c * 2], 0);
        for (auto s : t.slots) root.at(s * 32) = 0xe5;
    }
    void entry(const std::u16string& name, uint16_t first, uint32_t size) {
        validateName(name);
#ifndef _WIN32
        auto folded = [](std::u16string s) { for (auto& c : s) if (c >= u'A' && c <= u'Z') c += 32; return s; };
#endif
        for (const auto& t : tracks()) {
#ifdef _WIN32
            const bool same = CompareStringOrdinal(reinterpret_cast<const wchar_t*>(t.name.data()), int(t.name.size()),
                reinterpret_cast<const wchar_t*>(name.data()), int(name.size()), TRUE) == CSTR_EQUAL;
#else
            const bool same = folded(t.name) == folded(name);
#endif
            if (same) throw std::runtime_error("A track with this name already exists.");
        }
        std::array<uint8_t, 11> alias{};
        bool available = false;
        for (unsigned number = 1; number <= 99999; ++number) {
            char text[12]; std::snprintf(text, sizeof(text), "OB%06uMP3", number);
            std::copy_n(text, 11, alias.data()); available = true;
            for (size_t s = 0; s < rootSlots; ++s) if (root[s * 32] != 0xe5 && root[s * 32] && root[s * 32 + 11] != 15 &&
                std::equal(alias.begin(), alias.end(), &root[s * 32])) { available = false; break; }
            if (available) break;
        }
        if (!available) throw std::runtime_error("No available short filename.");
        const size_t longSlots = (name.size() + 12) / 13, count = longSlots + 1;
        size_t start = rootSlots, run = 0; bool end = false;
        for (size_t s = 0; s < rootSlots; ++s) {
            if (!root[s * 32]) end = true;
            if (end || root[s * 32] == 0xe5) { if (++run == count) { start = s + 1 - count; break; } }
            else run = 0;
        }
        if (start == rootSlots) throw std::runtime_error("The storage directory is full.");
        // All bytes after the first end marker are logically unused.
        for (size_t s = 0; s < rootSlots; ++s) if (!root[s * 32]) { std::fill(root.begin() + s * 32, root.end(), 0); break; }
        constexpr int positions[]{1,3,5,7,9,14,16,18,20,22,24,28,30};
        for (size_t i = 0; i < longSlots; ++i) {
            auto* l = &root[(start + i) * 32]; std::fill_n(l, 32, 0xff);
            size_t ordinal = longSlots - i; l[0] = uint8_t(ordinal | (i == 0 ? 0x40 : 0));
            l[11] = 15; l[12] = 0; l[13] = checksum(alias.data()); w16(l + 26, 0);
            for (int j = 0; j < 13; ++j) {
                size_t n = (ordinal - 1) * 13 + j;
                w16(l + positions[j], n < name.size() ? name[n] : n == name.size() ? 0 : 0xffff);
            }
        }
        auto* p = &root[(start + longSlots) * 32]; std::fill_n(p, 32, 0);
        std::copy(alias.begin(), alias.end(), p); p[11] = 0x20; w16(p + 24, 0x21); w16(p + 26, first); w32(p + 28, size);
    }
    // Commit a complete temporary copy. A failed import never replaces the original.
    template<class Edit> void transaction(Edit edit) {
        auto temporary = path; temporary += ".editing";
#ifdef _WIN32
        HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Storage is in use. Stop the emulator before editing it.");
#endif
        bool owned = false;
        try {
            reload();
            if (!std::filesystem::copy_file(path, temporary)) throw std::runtime_error("Could not prepare storage copy.");
            owned = true;
            { std::fstream f(temporary, std::ios::binary | std::ios::in | std::ios::out);
              edit(f); write(f, uint64_t(fatSector) * 512, fat.data(), fat.size());
              write(f, uint64_t(fatSector + fatSectors) * 512, fat.data(), fat.size());
              write(f, uint64_t(rootSector) * 512, root.data(), root.size()); f.flush();
              if (!f) throw std::runtime_error("Could not flush storage copy."); }
#ifdef _WIN32
            if (!ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
                throw std::runtime_error("Could not replace storage (Windows error " + std::to_string(GetLastError()) + "). The previous image is unchanged.");
            CloseHandle(lock); lock = INVALID_HANDLE_VALUE;
#else
            std::filesystem::rename(temporary, path);
#endif
            owned = false; reload();
        } catch (...) {
#ifdef _WIN32
            if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
#endif
            if (owned) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
            throw;
        }
    }
    void add(const std::vector<std::filesystem::path>& sources) {
        transaction([&](std::fstream& out) {
            for (const auto& source : sources) {
                if (!std::filesystem::is_regular_file(source)) throw std::runtime_error("Select regular MP3 files.");
                auto size = std::filesystem::file_size(source); auto name = source.filename().u16string(); validateName(name);
                if (size > freeBytes()) throw std::runtime_error("There is not enough free space.");
                std::vector<uint16_t> allocated;
                for (uint32_t c = 2; c < clusters + 2 && uint64_t(allocated.size()) * clusterBytes < size; ++c)
                    if (!r16(&fat[c * 2])) allocated.push_back(uint16_t(c));
                std::ifstream in(source, std::ios::binary); std::array<uint8_t, 65536> buffer{};
                if (clusterBytes > buffer.size()) throw std::runtime_error("Unsupported cluster size.");
                uint64_t remaining = size;
                for (size_t i = 0; i < allocated.size(); ++i) {
                    size_t n = size_t(std::min<uint64_t>(remaining, clusterBytes)); std::fill_n(buffer.data(), clusterBytes, uint8_t(0));
                    in.read(reinterpret_cast<char*>(buffer.data()), n);
                    if (!in) throw std::runtime_error("Could not read MP3 file. Storage was not modified.");
                    write(out, uint64_t(data) * 512 + uint64_t(allocated[i] - 2) * clusterBytes, buffer.data(), clusterBytes);
                    w16(&fat[allocated[i] * 2], i + 1 < allocated.size() ? allocated[i + 1] : 0xffff); remaining -= n;
                }
                entry(name, allocated.empty() ? 0 : allocated[0], uint32_t(size));
            }
        });
    }
    void remove(size_t index) { transaction([&](std::fstream&) { auto list = tracks(); erase(list.at(index)); }); }
    void rename(size_t index, const std::u16string& name) {
        transaction([&](std::fstream&) { auto t = tracks().at(index); for (auto s : t.slots) root[s * 32] = 0xe5; entry(name, t.first, t.size); });
    }
    static void create(const std::filesystem::path& path) {
        // Exclusively reserve the destination; do not truncate an existing image.
#ifdef _WIN32
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not create storage (the file may already exist).");
        LARGE_INTEGER end{}; end.QuadPart = capacity;
        bool ok = SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
        if (!ok) { DeleteFileW(path.c_str()); throw std::runtime_error("Could not allocate storage."); }
#else
        if (std::filesystem::exists(path)) throw std::runtime_error("Storage already exists.");
        { std::ofstream f(path, std::ios::binary); f.seekp(capacity - 1); f.put(0); if (!f) throw std::runtime_error("Could not allocate storage."); }
#endif
        try {
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            Bytes mbr(512), boot(512), fat(256 * 512), root(512 * 32);
            mbr[450] = 6; w32(&mbr[454], 1); w32(&mbr[458], uint32_t(capacity / 512 - 1)); w16(&mbr[510], 0xaa55);
            boot[0] = 0xeb; boot[1] = 0x3c; boot[2] = 0x90;
            std::copy_n("OCNBLAST", 8, &boot[3]); w16(&boot[11], 512); boot[13] = 8;
            w16(&boot[14], 1); boot[16] = 2; w16(&boot[17], 512); boot[21] = 0xf8;
            w16(&boot[22], 256); w16(&boot[24], 56); w16(&boot[26], 9); w32(&boot[28], 1);
            w32(&boot[32], uint32_t(capacity / 512 - 1)); boot[36] = 128; boot[38] = 0x29;
            w32(&boot[39], 0x4f434e42); std::copy_n("OCEANBLAST FAT16   ", 19, &boot[43]); w16(&boot[510], 0xaa55);
            w16(fat.data(), 0xfff8); w16(&fat[2], 0xffff);
            write(f, 0, mbr.data(), mbr.size()); write(f, 512, boot.data(), boot.size());
            write(f, 1024, fat.data(), fat.size()); write(f, 258 * 512, fat.data(), fat.size());
            write(f, 514 * 512, root.data(), root.size()); f.flush(); if (!f) throw std::runtime_error("Could not initialize storage.");
        } catch (...) { std::error_code ignored; std::filesystem::remove(path, ignored); throw; }
    }
};
}
