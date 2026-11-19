#pragma once
#ifdef _WIN32

#include "windows_input.h"
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace oceanblast::win {

namespace diag {

inline std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

inline uint32_t crc32Bytes(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
        }
    }
    return ~crc;
}

// RFC 1951 raw Deflate (LZ77 + Fixed Huffman, BTYPE=01) with zero external dependencies.
class BitWriter {
public:
    std::vector<uint8_t> bytes;
    void putBits(uint32_t value, int count) {
        bitBuf |= (value << bitCount);
        bitCount += count;
        while (bitCount >= 8) {
            bytes.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
            bitBuf >>= 8;
            bitCount -= 8;
        }
    }
    void putBitsRev(uint32_t value, int count) {
        uint32_t rev = 0;
        for (int i = 0; i < count; ++i) rev = (rev << 1) | ((value >> i) & 1u);
        putBits(rev, count);
    }
    void flush() {
        if (bitCount > 0) {
            bytes.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
            bitBuf = 0;
            bitCount = 0;
        }
    }
private:
    uint32_t bitBuf = 0;
    int bitCount = 0;
};

inline void emitFixedLitLen(BitWriter& bw, uint32_t sym) {
    if (sym <= 143) bw.putBitsRev(0x30u + sym, 8);
    else if (sym <= 255) bw.putBitsRev(0x190u + (sym - 144u), 9);
    else if (sym <= 279) bw.putBitsRev(sym - 256u, 7);
    else bw.putBitsRev(0xC0u + (sym - 280u), 8);
}

inline std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t size) {
    static constexpr uint16_t kLenBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static constexpr uint8_t kLenExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static constexpr uint16_t kDistBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static constexpr uint8_t kDistExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    BitWriter bw;
    bw.bytes.reserve(size / 2 + 16);
    bw.putBits(1, 1); // BFINAL = 1
    bw.putBits(1, 2); // BTYPE = 01 (Fixed Huffman)

    constexpr size_t kHashBits = 15;
    constexpr size_t kHashSize = 1u << kHashBits;
    constexpr size_t kWindow = 32768;
    std::vector<int32_t> head(kHashSize, -1);
    std::vector<int32_t> prev(size, -1);

    auto hash3 = [&](size_t pos) -> size_t {
        uint32_t v = (uint32_t(data[pos]) << 16) | (uint32_t(data[pos + 1]) << 8) | uint32_t(data[pos + 2]);
        return ((v * 2654435761u) >> (32 - kHashBits)) & (kHashSize - 1);
    };

    size_t pos = 0;
    while (pos < size) {
        size_t bestLen = 0;
        size_t bestDist = 0;
        if (pos + 3 <= size) {
            const size_t h = hash3(pos);
            int32_t cand = head[h];
            prev[pos] = cand;
            head[h] = static_cast<int32_t>(pos);

            int checks = 0;
            const size_t maxLen = std::min<size_t>(258, size - pos);
            while (cand >= 0 && checks < 48) {
                const size_t dist = pos - static_cast<size_t>(cand);
                if (dist == 0 || dist > kWindow) break;
                if (data[cand] == data[pos] && data[cand + bestLen] == data[pos + bestLen]) {
                    size_t len = 0;
                    while (len < maxLen && data[static_cast<size_t>(cand) + len] == data[pos + len]) ++len;
                    if (len >= 3 && len > bestLen) {
                        bestLen = len;
                        bestDist = dist;
                        if (bestLen == maxLen) break;
                    }
                }
                cand = prev[static_cast<size_t>(cand)];
                ++checks;
            }
        }

        if (bestLen >= 3) {
            int lCode = 28;
            for (int i = 0; i < 29; ++i) {
                if (bestLen >= kLenBase[i] && (i == 28 || bestLen < kLenBase[i + 1])) {
                    lCode = i;
                    break;
                }
            }
            emitFixedLitLen(bw, 257u + static_cast<uint32_t>(lCode));
            if (kLenExtra[lCode] > 0) bw.putBits(static_cast<uint32_t>(bestLen - kLenBase[lCode]), kLenExtra[lCode]);

            int dCode = 29;
            for (int i = 0; i < 30; ++i) {
                if (bestDist >= kDistBase[i] && (i == 29 || bestDist < kDistBase[i + 1])) {
                    dCode = i;
                    break;
                }
            }
            bw.putBitsRev(static_cast<uint32_t>(dCode), 5);
            if (kDistExtra[dCode] > 0) bw.putBits(static_cast<uint32_t>(bestDist - kDistBase[dCode]), kDistExtra[dCode]);

            for (size_t k = 1; k < bestLen && pos + k + 3 <= size; ++k) {
                const size_t h = hash3(pos + k);
                prev[pos + k] = head[h];
                head[h] = static_cast<int32_t>(pos + k);
            }
            pos += bestLen;
        } else {
            emitFixedLitLen(bw, data[pos]);
            ++pos;
        }
    }

    emitFixedLitLen(bw, 256); // End of block
    bw.flush();
    return bw.bytes;
}

struct ZipEntry {
    std::string name;
    std::string content;
};

inline bool writeZipArchive(const std::wstring& path, const std::vector<ZipEntry>& entries) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    auto writeU16 = [&](uint16_t v) {
        const uint8_t b[2] = { static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu) };
        out.write(reinterpret_cast<const char*>(b), 2);
    };
    auto writeU32 = [&](uint32_t v) {
        const uint8_t b[4] = {
            static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu),
            static_cast<uint8_t>((v >> 16) & 0xFFu), static_cast<uint8_t>((v >> 24) & 0xFFu)
        };
        out.write(reinterpret_cast<const char*>(b), 4);
    };

    struct CentralRecord {
        std::string name;
        uint16_t method;
        uint32_t crc;
        uint32_t compSize;
        uint32_t uncompSize;
        uint32_t localOffset;
    };
    std::vector<CentralRecord> records;
    records.reserve(entries.size());

    SYSTEMTIME st{};
    GetLocalTime(&st);
    const uint16_t dosTime = static_cast<uint16_t>(((st.wHour & 31) << 11) | ((st.wMinute & 63) << 5) | ((st.wSecond / 2) & 31));
    const uint16_t dosDate = static_cast<uint16_t>((((st.wYear >= 1980 ? st.wYear - 1980 : 0) & 127) << 9) |
                                                   ((st.wMonth & 15) << 5) | (st.wDay & 31));

    for (const auto& entry : entries) {
        const auto* raw = reinterpret_cast<const uint8_t*>(entry.content.data());
        const size_t rawSize = entry.content.size();
        const uint32_t crc = crc32Bytes(raw, rawSize);
        std::vector<uint8_t> compressed = deflateRaw(raw, rawSize);
        const bool useDeflate = compressed.size() < rawSize;
        const uint16_t method = useDeflate ? 8u : 0u;
        const uint8_t* payload = useDeflate ? compressed.data() : raw;
        const uint32_t compSize = static_cast<uint32_t>(useDeflate ? compressed.size() : rawSize);
        const uint32_t uncompSize = static_cast<uint32_t>(rawSize);
        const uint32_t localOffset = static_cast<uint32_t>(out.tellp());

        writeU32(0x04034B50u); // Local file header
        writeU16(20u);         // Version needed (2.0)
        writeU16(0x0800u);     // UTF-8 filename flag
        writeU16(method);
        writeU16(dosTime);
        writeU16(dosDate);
        writeU32(crc);
        writeU32(compSize);
        writeU32(uncompSize);
        writeU16(static_cast<uint16_t>(entry.name.size()));
        writeU16(0u);
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        if (compSize > 0) out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(compSize));

        records.push_back({entry.name, method, crc, compSize, uncompSize, localOffset});
    }

    const uint32_t cdOffset = static_cast<uint32_t>(out.tellp());
    for (const auto& r : records) {
        writeU32(0x02014B50u); // Central directory header
        writeU16(20u);
        writeU16(20u);
        writeU16(0x0800u);
        writeU16(r.method);
        writeU16(dosTime);
        writeU16(dosDate);
        writeU32(r.crc);
        writeU32(r.compSize);
        writeU32(r.uncompSize);
        writeU16(static_cast<uint16_t>(r.name.size()));
        writeU16(0u);
        writeU16(0u);
        writeU16(0u);
        writeU16(0u);
        writeU32(0u);
        writeU32(r.localOffset);
        out.write(r.name.data(), static_cast<std::streamsize>(r.name.size()));
    }

    const uint32_t cdSize = static_cast<uint32_t>(out.tellp()) - cdOffset;
    writeU32(0x06054B50u); // End of central directory
    writeU16(0u);
    writeU16(0u);
    writeU16(static_cast<uint16_t>(records.size()));
    writeU16(static_cast<uint16_t>(records.size()));
    writeU32(cdSize);
    writeU32(cdOffset);
    writeU16(0u);

    return out.good();
}

inline std::string queryWindowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) {
            RTL_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) {
                std::ostringstream oss;
                const char* label = "Windows";
                if (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000) label = "Windows 11";
                else if (vi.dwMajorVersion == 10) label = "Windows 10";
                else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 3) label = "Windows 8.1";
                else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 2) label = "Windows 8";
                else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1) label = "Windows 7";
                oss << label << " (" << vi.dwMajorVersion << "." << vi.dwMinorVersion
                    << " Build " << vi.dwBuildNumber << ")";
                return oss.str();
            }
        }
    }
    return "Windows (version unknown)";
}

inline std::string queryCpuName() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[256]{};
        DWORD bytes = sizeof(buf) - sizeof(wchar_t);
        DWORD type = 0;
        const LONG rc = RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buf), &bytes);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            std::string s = wideToUtf8(buf);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
            size_t first = s.find_first_not_of(' ');
            if (first != std::string::npos) s.erase(0, first);
            if (!s.empty()) return s;
        }
    }
    return "Unknown CPU";
}

inline std::string queryAudioDeviceSummary() {
    const UINT count = waveOutGetNumDevs();
    std::ostringstream oss;
    WAVEOUTCAPSW caps{};
    if (count > 0 && waveOutGetDevCapsW(0, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
        oss << wideToUtf8(caps.szPname) << " (devices=" << count << ")";
    } else if (waveOutGetDevCapsW(WAVE_MAPPER, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
        oss << wideToUtf8(caps.szPname) << " (devices=" << count << ")";
    } else {
        oss << "Unavailable (devices=" << count << ")";
    }
    return oss.str();
}

inline std::string sanitizePaths(std::string text) {
    // Strip Windows user profile directory (e.g., C:\Users\Alice) for privacy.
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
        const std::string needle = wideToUtf8(profile);
        if (!needle.empty()) {
            size_t pos = 0;
            while ((pos = text.find(needle, pos)) != std::string::npos) {
                text.replace(pos, needle.size(), "<USERPROFILE>");
                pos += 13;
            }
        }
    }
    // Ensure clean UTF-8 text (strip NUL/control bytes and non-UTF-8 raw binary bytes from --debug logs).
    std::string clean;
    clean.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\r' || c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7F)) {
            clean.push_back(static_cast<char>(c));
            ++i;
        } else if ((c & 0xE0u) == 0xC0u && i + 1 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0u) == 0x80u) {
            clean.append(text, i, 2);
            i += 2;
        } else if ((c & 0xF0u) == 0xE0u && i + 2 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0u) == 0x80u &&
                   (static_cast<unsigned char>(text[i + 2]) & 0xC0u) == 0x80u) {
            clean.append(text, i, 3);
            i += 3;
        } else if ((c & 0xF8u) == 0xF0u && i + 3 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0u) == 0x80u &&
                   (static_cast<unsigned char>(text[i + 2]) & 0xC0u) == 0x80u &&
                   (static_cast<unsigned char>(text[i + 3]) & 0xC0u) == 0x80u) {
            clean.append(text, i, 4);
            i += 4;
        } else {
            clean.push_back('.');
            ++i;
        }
    }
    return clean;
}

inline std::string readFileUtf8(const std::filesystem::path& path, size_t maxTailBytes = 0) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sizeLi{};
    if (!GetFileSizeEx(h, &sizeLi) || sizeLi.QuadPart <= 0) {
        CloseHandle(h);
        return {};
    }
    const uint64_t total = static_cast<uint64_t>(sizeLi.QuadPart);
    uint64_t start = 0;
    std::string prefix;
    if (maxTailBytes > 0 && total > maxTailBytes) {
        start = total - maxTailBytes;
        prefix = "[... truncated to last " + std::to_string(maxTailBytes / 1024) + " KB ...]\n";
        LARGE_INTEGER seekPos{};
        seekPos.QuadPart = static_cast<LONGLONG>(start);
        SetFilePointerEx(h, seekPos, nullptr, FILE_BEGIN);
    }
    const size_t toRead = static_cast<size_t>(total - start);
    std::string data(toRead, '\0');
    size_t offset = 0;
    while (offset < toRead) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(toRead - offset, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(h, data.data() + offset, chunk, &got, nullptr) || got == 0) break;
        offset += got;
    }
    CloseHandle(h);
    data.resize(offset);
    return prefix + data;
}

struct PerfSummary {
    bool valid = false;
    size_t rows = 0;
    double minMips = 0, avgMips = 0, maxMips = 0;
    double minSpeed = 0, avgSpeed = 0, maxSpeed = 0;
    double avgPresentFps = 0;
    std::string audioRate, audioOutRate, engine;
    uint64_t maxDropped = 0, maxEmpty = 0;
};

inline PerfSummary summarizePerformanceCsv(const std::string& csv) {
    PerfSummary s;
    if (csv.empty()) return s;
    std::istringstream in(csv);
    std::string line;
    if (!std::getline(in, line)) return s; // header

    double sumMips = 0, sumSpeed = 0, sumFps = 0;
    s.minMips = std::numeric_limits<double>::max();
    s.minSpeed = std::numeric_limits<double>::max();

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols;
        std::istringstream ls(line);
        std::string cell;
        while (std::getline(ls, cell, ',')) cols.push_back(cell);
        if (cols.size() < 31) continue;
        try {
            const double fps = std::stod(cols[2]);
            const double mips = std::stod(cols[4]);
            const uint64_t dropped = std::stoull(cols[8]);
            const uint64_t empty = std::stoull(cols[11]);
            const double speed = std::stod(cols[16]);
            s.minMips = std::min(s.minMips, mips);
            s.maxMips = std::max(s.maxMips, mips);
            sumMips += mips;
            s.minSpeed = std::min(s.minSpeed, speed);
            s.maxSpeed = std::max(s.maxSpeed, speed);
            sumSpeed += speed;
            sumFps += fps;
            s.maxDropped = std::max(s.maxDropped, dropped);
            s.maxEmpty = std::max(s.maxEmpty, empty);
            s.audioRate = cols[7];
            s.audioOutRate = cols[26];
            s.engine = cols[30];
            ++s.rows;
        } catch (...) {}
    }
    if (s.rows > 0) {
        s.valid = true;
        s.avgMips = sumMips / static_cast<double>(s.rows);
        s.avgSpeed = sumSpeed / static_cast<double>(s.rows);
        s.avgPresentFps = sumFps / static_cast<double>(s.rows);
    }
    return s;
}

struct DiagnosticBundle {
    std::string shortSummary;
    std::string fullSummary;
    std::string sessionLog;
    std::string perfCsv;
};

inline DiagnosticBundle collectDiagnostics(const std::wstring& appFolder,
                                           const FrontendSettings& settings,
                                           const std::wstring& activeRomPath) {
    DiagnosticBundle bundle;
    std::filesystem::path sessionsDir(appFolder);
    if (!std::filesystem::exists(sessionsDir / L"session.log") &&
        !std::filesystem::exists(sessionsDir / L"performance.csv") &&
        std::filesystem::exists(sessionsDir / L"sessions")) {
        sessionsDir /= L"sessions";
    }
    bundle.sessionLog = sanitizePaths(readFileUtf8(sessionsDir / L"session.log", 128 * 1024));
    bundle.perfCsv = readFileUtf8(sessionsDir / L"performance.csv", 192 * 1024);
    const PerfSummary perf = summarizePerformanceCsv(bundle.perfCsv);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    const double ramGb = static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);

    const char* archBuild =
#if defined(_WIN64) && defined(OCEANBLAST_WITH_JIT)
        "v0.4.1 (Windows x64 JIT)";
#elif defined(_WIN64)
        "v0.4.1 (Windows x64 Interpreter)";
#else
        "v0.4.1 (Windows x86 Interpreter)";
#endif

    const std::wstring romNameW = !activeRomPath.empty()
        ? std::filesystem::path(activeRomPath).filename().wstring()
        : (!settings.lastRom.empty() ? std::filesystem::path(settings.lastRom).filename().wstring() : L"(none)");

    const std::string osVer = queryWindowsVersion();
    const std::string cpu = queryCpuName();
    const std::string audioDev = queryAudioDeviceSummary();

    std::ostringstream shortOut;
    shortOut << "OceanBlast: " << archBuild << "\n"
             << "OS: " << osVer << "\n"
             << "CPU: " << cpu << " (" << si.dwNumberOfProcessors << " threads, "
             << std::fixed << std::setprecision(1) << ramGb << " GB RAM)\n"
             << "Audio Device: " << audioDev << "\n"
             << "Last ROM: " << wideToUtf8(romNameW) << "\n"
             << "Settings: sound=" << (settings.soundEnabled ? "on" : "off")
             << " vol=" << settings.soundVolume << "%"
             << " scale=" << (settings.scaleIndex + 2) << "x"
             << " windowMode=" << settings.windowMode
             << " timing=" << settings.timingMode
             << " fpsLog=" << (settings.fpsLog ? "on" : "off")
             << " debugLog=" << (settings.debugLog ? "on" : "off") << "\n";

    if (perf.valid) {
        shortOut << "Performance (" << perf.rows << "s): engine=" << perf.engine
                 << " | MIPS avg=" << std::setprecision(1) << perf.avgMips
                 << " (min=" << perf.minMips << ", max=" << perf.maxMips << ")"
                 << " | Model avg=" << perf.avgSpeed << "%"
                 << " | Display FPS avg=" << perf.avgPresentFps
                 << " | Audio=" << perf.audioRate << "->" << perf.audioOutRate << " Hz"
                 << " | Drop=" << perf.maxDropped
                 << " | Empty=" << perf.maxEmpty << "\n";
    } else {
        shortOut << "Performance: no sessions\\performance.csv recorded yet.\n";
    }
    bundle.shortSummary = shortOut.str();

    std::ostringstream fullOut;
    fullOut << "=== OceanBlast Diagnostic Report ===\n"
            << bundle.shortSummary;
    if (!bundle.sessionLog.empty()) {
        fullOut << "\n=== sessions\\session.log ===\n" << bundle.sessionLog << "\n";
    } else {
        fullOut << "\n=== sessions\\session.log ===\n(not found)\n";
    }
    if (!bundle.perfCsv.empty()) {
        fullOut << "\n=== sessions\\performance.csv ===\n" << bundle.perfCsv << "\n";
    }
    bundle.fullSummary = fullOut.str();
    return bundle;
}

inline bool exportDiagnosticsZip(HWND owner, const std::wstring& appFolder,
                                 const FrontendSettings& settings,
                                 const std::wstring& activeRomPath,
                                 std::wstring& savedPath) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t defaultName[128]{};
    std::swprintf(defaultName, 128, L"oceanblast-log-%04u%02u%02u-%02u%02u%02u.zip",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t fileBuf[MAX_PATH]{};
    wcsncpy(fileBuf, defaultName, MAX_PATH - 1);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"ZIP Archive (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;

    const DiagnosticBundle bundle = collectDiagnostics(appFolder, settings, activeRomPath);
    std::vector<ZipEntry> entries;
    entries.push_back({"diagnostic_summary.txt", bundle.shortSummary});
    if (!bundle.sessionLog.empty()) entries.push_back({"session.log", bundle.sessionLog});
    if (!bundle.perfCsv.empty()) entries.push_back({"performance.csv", bundle.perfCsv});

    if (!writeZipArchive(fileBuf, entries)) return false;
    savedPath = fileBuf;
    return true;
}

inline bool exportDiagnosticsTxt(HWND owner, const std::wstring& appFolder,
                                 const FrontendSettings& settings,
                                 const std::wstring& activeRomPath,
                                 std::wstring& savedPath) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t defaultName[128]{};
    std::swprintf(defaultName, 128, L"oceanblast-log-%04u%02u%02u-%02u%02u%02u.txt",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t fileBuf[MAX_PATH]{};
    wcsncpy(fileBuf, defaultName, MAX_PATH - 1);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text File (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;

    const DiagnosticBundle bundle = collectDiagnostics(appFolder, settings, activeRomPath);
    std::ofstream out(std::filesystem::path(fileBuf), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bundle.fullSummary.data(), static_cast<std::streamsize>(bundle.fullSummary.size()));
    if (!out.good()) return false;
    savedPath = fileBuf;
    return true;
}

inline bool copyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* dst = GlobalLock(mem);
    if (dst) {
        std::memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    } else {
        GlobalFree(mem);
    }
    CloseClipboard();
    return dst != nullptr;
}

// Uploads text to https://paste.rs/ via dynamically loaded winhttp.dll (no link-time dependency).
inline bool uploadToPasteRs(const std::string& payload, std::string& outUrl, std::string& outError) {
    outUrl.clear();
    outError.clear();

    HMODULE winhttp = LoadLibraryW(L"winhttp.dll");
    if (!winhttp) {
        outError = "winhttp.dll is unavailable on this system.";
        return false;
    }

    using OpenFn = HINTERNET(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    using ConnectFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
    using OpenReqFn = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    using SendReqFn = BOOL(WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    using RecvRespFn = BOOL(WINAPI*)(HINTERNET, LPVOID);
    using QueryHdrFn = BOOL(WINAPI*)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
    using QueryDataFn = BOOL(WINAPI*)(HINTERNET, LPDWORD);
    using ReadDataFn = BOOL(WINAPI*)(HINTERNET, LPVOID, DWORD, LPDWORD);
    using SetTimeoutsFn = BOOL(WINAPI*)(HINTERNET, int, int, int, int);
    using CloseFn = BOOL(WINAPI*)(HINTERNET);

    auto pOpen = reinterpret_cast<OpenFn>(GetProcAddress(winhttp, "WinHttpOpen"));
    auto pConnect = reinterpret_cast<ConnectFn>(GetProcAddress(winhttp, "WinHttpConnect"));
    auto pOpenReq = reinterpret_cast<OpenReqFn>(GetProcAddress(winhttp, "WinHttpOpenRequest"));
    auto pSendReq = reinterpret_cast<SendReqFn>(GetProcAddress(winhttp, "WinHttpSendRequest"));
    auto pRecvResp = reinterpret_cast<RecvRespFn>(GetProcAddress(winhttp, "WinHttpReceiveResponse"));
    auto pQueryHdr = reinterpret_cast<QueryHdrFn>(GetProcAddress(winhttp, "WinHttpQueryHeaders"));
    auto pQueryData = reinterpret_cast<QueryDataFn>(GetProcAddress(winhttp, "WinHttpQueryDataAvailable"));
    auto pReadData = reinterpret_cast<ReadDataFn>(GetProcAddress(winhttp, "WinHttpReadData"));
    auto pSetTimeouts = reinterpret_cast<SetTimeoutsFn>(GetProcAddress(winhttp, "WinHttpSetTimeouts"));
    auto pClose = reinterpret_cast<CloseFn>(GetProcAddress(winhttp, "WinHttpCloseHandle"));

    if (!pOpen || !pConnect || !pOpenReq || !pSendReq || !pRecvResp || !pQueryHdr || !pQueryData || !pReadData || !pClose) {
        FreeLibrary(winhttp);
        outError = "Failed to resolve WinHTTP functions.";
        return false;
    }

    // Keep payload comfortably under paste.rs ~1 MB limit.
    constexpr size_t kMaxUploadBytes = 384 * 1024;
    std::string body = payload.size() > kMaxUploadBytes
        ? payload.substr(0, kMaxUploadBytes) + "\n[... truncated for paste.rs upload ...]\n"
        : payload;

    HINTERNET hSession = pOpen(L"OceanBlast/0.4.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        FreeLibrary(winhttp);
        outError = "WinHttpOpen failed.";
        return false;
    }
    if (pSetTimeouts) pSetTimeouts(hSession, 5000, 8000, 10000, 10000);

    HINTERNET hConnect = pConnect(hSession, L"paste.rs", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = hConnect ? pOpenReq(hConnect, L"POST", L"/", nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (hRequest) {
        const wchar_t* headers = L"Content-Type: text/plain; charset=utf-8\r\n";
        if (pSendReq(hRequest, headers, static_cast<DWORD>(-1),
                     body.data(), static_cast<DWORD>(body.size()),
                     static_cast<DWORD>(body.size()), 0) &&
            pRecvResp(hRequest, nullptr)) {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            if (pQueryHdr(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
                (statusCode == 200 || statusCode == 201 || statusCode == 206)) {
                std::string response;
                DWORD avail = 0;
                while (pQueryData(hRequest, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!pReadData(hRequest, chunk.data(), avail, &read) || read == 0) break;
                    response.append(chunk.data(), read);
                    if (response.size() > 1024) break;
                }
                while (!response.empty() && (response.back() == '\r' || response.back() == '\n' || response.back() == ' ')) {
                    response.pop_back();
                }
                if (response.rfind("https://paste.rs/", 0) == 0 || response.rfind("http://paste.rs/", 0) == 0) {
                    outUrl = response;
                    ok = true;
                } else {
                    outError = "Unexpected response from paste.rs.";
                }
            } else {
                outError = "paste.rs returned HTTP status " + std::to_string(statusCode) + ".";
            }
        } else {
            outError = "HTTPS request to paste.rs failed (error " + std::to_string(GetLastError()) + ").";
        }
    } else {
        outError = "Could not connect to paste.rs.";
    }

    if (hRequest) pClose(hRequest);
    if (hConnect) pClose(hConnect);
    pClose(hSession);
    FreeLibrary(winhttp);
    return ok;
}

struct PasteDialogState {
    std::wstring url;
    HWND statusLabel = nullptr;
    HWND urlEdit = nullptr;
    HFONT titleFont = nullptr;
    HFONT urlFont = nullptr;
    HFONT uiFont = nullptr;
    bool done = false;
};

inline LRESULT CALLBACK pasteResultWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<PasteDialogState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        st = static_cast<PasteDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->titleFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        st->urlFont = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
        st->uiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        HWND header = CreateWindowW(L"STATIC", L"Log uploaded to paste.rs:",
                                    WS_CHILD | WS_VISIBLE, 20, 16, 510, 24, h, nullptr, nullptr, nullptr);
        SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(st->titleFont), TRUE);

        st->urlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->url.c_str(),
                                      WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER | ES_AUTOHSCROLL,
                                      20, 48, 510, 40, h, reinterpret_cast<HMENU>(100), nullptr, nullptr);
        SendMessageW(st->urlEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->urlFont), TRUE);

        st->statusLabel = CreateWindowW(L"STATIC", L"Link automatically copied to clipboard.",
                                        WS_CHILD | WS_VISIBLE, 20, 96, 510, 22, h, nullptr, nullptr, nullptr);
        SendMessageW(st->statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);

        HWND btnCopy = CreateWindowW(L"BUTTON", L"Copy to Clipboard",
                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                     20, 128, 175, 34, h, reinterpret_cast<HMENU>(101), nullptr, nullptr);
        HWND btnOpen = CreateWindowW(L"BUTTON", L"Open in Browser",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                     205, 128, 165, 34, h, reinterpret_cast<HMENU>(102), nullptr, nullptr);
        HWND btnClose = CreateWindowW(L"BUTTON", L"Close",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                      380, 128, 150, 34, h, reinterpret_cast<HMENU>(103), nullptr, nullptr);
        SendMessageW(btnCopy, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
        SendMessageW(btnOpen, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
        SendMessageW(btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(st->uiFont), TRUE);
        return 0;
    }
    if (msg == WM_COMMAND && st) {
        const int id = LOWORD(w);
        if (id == 101) {
            if (copyTextToClipboard(h, st->url)) {
                SetWindowTextW(st->statusLabel, L"Copied to clipboard!");
            }
            SendMessageW(st->urlEdit, EM_SETSEL, 0, -1);
            return 0;
        }
        if (id == 102) {
            ShellExecuteW(h, L"open", st->url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == 103 || id == IDCANCEL) {
            st->done = true;
            DestroyWindow(h);
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && w == VK_ESCAPE && st) {
        st->done = true;
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_CLOSE && st) {
        st->done = true;
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY && st) {
        if (st->titleFont) DeleteObject(st->titleFont);
        if (st->urlFont) DeleteObject(st->urlFont);
        if (st->uiFont) DeleteObject(st->uiFont);
        st->done = true;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

inline void showPasteResultDialog(HWND owner, const std::wstring& url) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = pasteResultWndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"OceanBlastPasteResult";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    PasteDialogState state;
    state.url = url;
    RECT rc{0, 0, 550, 178};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rc, style, FALSE);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT parentRc{};
    if (owner && GetWindowRect(owner, &parentRc)) {
        x = parentRc.left + ((parentRc.right - parentRc.left) - winW) / 2;
        y = parentRc.top + ((parentRc.bottom - parentRc.top) - winH) / 2;
    }

    if (owner) EnableWindow(owner, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OceanBlastPasteResult", L"paste.rs Upload",
                               style, x, y, winW, winH, owner, nullptr, inst, &state);
    if (dlg) {
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        MSG m{};
        while (!state.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
                PostMessageW(dlg, WM_CLOSE, 0, 0);
                continue;
            }
            if (!IsDialogMessageW(dlg, &m)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

} // namespace diag
} // namespace oceanblast::win

#endif // _WIN32
