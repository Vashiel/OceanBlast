#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "input_mapping.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace oceanblast {
namespace win {

struct Controller {
    std::wstring name, key;
    GUID guid{};
    int xslot = -1;
    bool stadia = false;
};

class InputManager {
    struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
    using GetState = DWORD(WINAPI*)(DWORD, XState*);
    HMODULE xmodule_ = nullptr;
    GetState xget_ = nullptr;
    IDirectInput8W* direct_ = nullptr;
    IDirectInputDevice8W* device_ = nullptr;
    HWND owner_ = nullptr;
    std::wstring selected_ = L"keyboard";

    static BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* info, void* context) {
        auto& self = *static_cast<InputManager*>(context);
        const auto type = LOBYTE(info->dwDevType);
        const bool controller = info->wUsagePage == 1 && (info->wUsage == 4 || info->wUsage == 5 || info->wUsage == 8);
        if (!controller && (type < DI8DEVTYPE_JOYSTICK || type > DI8DEVTYPE_1STPERSON))
            return DIENUM_CONTINUE;
        std::wstring name = info->tszProductName;
        const bool stadia = name.find(L"Stadia") != std::wstring::npos || LOWORD(info->guidProduct.Data1) == 0x18d1;
        if (stadia) name = L"Stadia Controller";
        wchar_t key[64]{};
        StringFromGUID2(info->guidInstance, key, 64);
        self.controllers.push_back({name, key, info->guidInstance, -1, stadia});
        return DIENUM_CONTINUE;
    }

    static BOOL CALLBACK axisRange(const DIDEVICEOBJECTINSTANCEW* axis, void* context) {
        auto* device = static_cast<IDirectInputDevice8W*>(context);
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(range);
        range.diph.dwHeaderSize = sizeof(range.diph);
        range.diph.dwHow = DIPH_BYID;
        range.diph.dwObj = axis->dwType;
        range.lMin = -32768;
        range.lMax = 32767;
        device->SetProperty(DIPROP_RANGE, &range.diph);
        return DIENUM_CONTINUE;
    }

public:
    std::vector<Controller> controllers;
    HostPad current{};
    bool connected = false;
    HRESULT initializationResult = E_FAIL;

    InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    ~InputManager() { shutdown(); }

    void shutdown() {
        closeDevice();
        if (direct_) { direct_->Release(); direct_ = nullptr; }
        if (xmodule_) { FreeLibrary(xmodule_); xmodule_ = nullptr; }
        xget_ = nullptr;
    }

    void closeDevice() {
        if (device_) { device_->Unacquire(); device_->Release(); device_ = nullptr; }
        current = {};
        connected = false;
    }

    void init(HWND owner) {
        shutdown();
        owner_ = owner;
        for (const auto* name : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
            xmodule_ = LoadLibraryW(name);
            if (!xmodule_) continue;
            xget_ = reinterpret_cast<GetState>(reinterpret_cast<void*>(GetProcAddress(xmodule_, "XInputGetState")));
            if (xget_) break;
            FreeLibrary(xmodule_);
            xmodule_ = nullptr;
        }
        initializationResult = DirectInput8Create(
            GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
            reinterpret_cast<void**>(&direct_), nullptr);
    }

    void refresh() {
        controllers.clear();
        controllers.push_back({L"Nur Tastatur", L"keyboard", {}, -1, false});
        if (direct_) direct_->EnumDevices(DI8DEVCLASS_ALL, enumerate, this, DIEDFL_ATTACHEDONLY);
        if (xget_) {
            for (unsigned slot = 0; slot < 4; ++slot) {
                XState state{};
                if (xget_(slot, &state) == ERROR_SUCCESS) {
                    controllers.push_back({
                        L"XInput Controller " + std::to_wstring(slot + 1),
                        L"XInput" + std::to_wstring(slot),
                        {},
                        static_cast<int>(slot),
                        false
                    });
                }
            }
        }
    }

    const Controller* selected() const {
        for (const auto& info : controllers)
            if (info.key == selected_) return &info;
        return nullptr;
    }

    const std::wstring& key() const { return selected_; }

    void select(const std::wstring& key) {
        closeDevice();
        selected_ = key;
        const auto* info = selected();
        if (!info || key == L"keyboard" || info->xslot >= 0 || !direct_) return;
        if (FAILED(direct_->CreateDevice(info->guid, &device_, nullptr))) {
            device_ = nullptr;
            return;
        }
        if (FAILED(device_->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(device_->SetCooperativeLevel(owner_, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
            closeDevice();
            return;
        }
        device_->EnumObjects(axisRange, device_, DIDFT_AXIS);
        device_->Acquire();
    }

    void autoSelectPreferred(std::wstring& savedController) {
        if (savedController.empty()) {
            for (const auto& c : controllers) {
                if (c.stadia) { savedController = c.key; break; }
            }
            if (savedController.empty() && controllers.size() > 1) {
                savedController = controllers[1].key;
            }
            if (savedController.empty()) {
                savedController = L"keyboard";
            }
        }
        select(savedController);
        // If a previously saved device is unplugged, fall back to any connected controller while keeping the setting.
        if (!selected() && controllers.size() > 1) {
            for (const auto& c : controllers) {
                if (c.stadia) { select(c.key); return; }
            }
            select(controllers[1].key);
        }
    }

    HostPad poll() {
        HostPad out{};
        connected = false;
        const auto* info = selected();
        if (!info || selected_ == L"keyboard") {
            current = out;
            return out;
        }
        if (info->xslot >= 0 && xget_) {
            XState state{};
            if (xget_(info->xslot, &state) == ERROR_SUCCESS) {
                connected = true;
                constexpr WORD buttons[]{0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};
                for (unsigned i = 0; i < 10; ++i)
                    out.buttons[i] = (state.buttons & buttons[i]) != 0;
                out.axes = {
                    state.lx, -static_cast<int>(state.ly),
                    state.rx, -static_cast<int>(state.ry),
                    static_cast<int>(state.lt) * 32767 / 255,
                    static_cast<int>(state.rt) * 32767 / 255
                };
                const unsigned dpad = state.buttons & 15;
                if (dpad & 1)      out.pov = (dpad & 4) ? 31500 : (dpad & 8) ? 4500 : 0;
                else if (dpad & 2) out.pov = (dpad & 4) ? 22500 : (dpad & 8) ? 13500 : 18000;
                else if (dpad & 4) out.pov = 27000;
                else if (dpad & 8) out.pov = 9000;
            }
        } else if (device_) {
            DIJOYSTATE2 state{};
            HRESULT result = device_->Poll();
            if (FAILED(result)) device_->Acquire();
            result = device_->GetDeviceState(sizeof(state), &state);
            if (SUCCEEDED(result)) {
                connected = true;
                for (unsigned i = 0; i < 128; ++i)
                    out.buttons[i] = (state.rgbButtons[i] & 128) != 0;
                out.axes = {
                    static_cast<int>(state.lX), static_cast<int>(state.lY),
                    static_cast<int>(state.lRx), static_cast<int>(state.lRy),
                    static_cast<int>(state.lZ), static_cast<int>(state.lRz)
                };
                out.pov = state.rgdwPOV[0];
            }
        }
        current = out;
        return out;
    }
};

inline std::wstring controlLabel(int token) {
    if (token >= 0 && token < 128) return L"Taste " + std::to_wstring(token + 1);
    if (token >= 128 && token < 140)
        return L"Achse " + std::to_wstring((token - 128) / 2 + 1) + (token & 1 ? L" +" : L" -");
    if (token == 200) return L"D-Pad oben";
    if (token == 201) return L"D-Pad unten";
    if (token == 202) return L"D-Pad links";
    if (token == 203) return L"D-Pad rechts";
    return L"Nicht belegt";
}

inline std::wstring keyboardLabel(unsigned key) {
    wchar_t text[64]{};
    auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (key >= VK_PRIOR && key <= VK_DOWN) scan |= 0x100;
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), text, 64)) return text;
    return L"Taste " + std::to_wstring(key);
}

inline std::wstring settingsPath() {
    wchar_t overridePath[32768]{};
    if (GetEnvironmentVariableW(L"OCEANBLAST_SETTINGS_PATH", overridePath, 32768) > 0) {
        std::filesystem::path p(overridePath);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        return p.wstring();
    }
    wchar_t directory[32768]{};
    GetEnvironmentVariableW(L"APPDATA", directory, 32768);
    auto folder = std::filesystem::path(directory) / L"OceanBlast";
    std::filesystem::create_directories(folder);
    return (folder / L"windows.ini").wstring();
}

struct FrontendSettings {
    std::wstring romFolder;
    std::wstring lastRom;
    std::wstring savedController;
    unsigned controllerPreset = 0;
    PadMap padMap = standardMap;
    std::array<unsigned, 12> keyboardKeys{
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        'Z', 'X', 'C', 'V',
        'A', 'S', VK_SPACE, VK_RETURN
    };
    bool soundEnabled = true;
    unsigned soundVolume = 100;
    unsigned scaleIndex = 1;       // 0: 2x, 1: 3x (default), 2: 4x
    unsigned windowMode = 2;       // 0: Console Skin, 1: Fullscreen, 2: Plain Window (default)
    unsigned videoAspect = 0;      // 0: 3:2 native, 1: 4:3, 2: stretched
    bool integerScale = false;
    bool smoothVideo = false;
    unsigned displayFormat = 0;    // 0: Automatic, 1: RGB444 480B, 2: RGB444, 3: RGB565, 4: LCD registers
    unsigned timingMode = 0;       // 0: Auto ROM preset, 1: 1x, 2: Register clocks, 3: 2x, 4: 4x, 5: 8x
    bool keepNvram = true;
    bool fpsLog = true;
    bool debugLog = false;

    void loadControllerFor(const std::wstring& deviceKey) {
        const auto path = settingsPath();
        const auto section = L"controller-" + (deviceKey.empty() ? std::wstring(L"keyboard") : deviceKey);
        controllerPreset = std::min(2u, GetPrivateProfileIntW(section.c_str(), L"preset", 0, path.c_str()));
        for (unsigned i = 0; i < 12; ++i) {
            const int token = static_cast<int>(GetPrivateProfileIntW(
                section.c_str(), std::to_wstring(i).c_str(), standardMap[i], path.c_str()));
            padMap[i] = validPadToken(token) ? token : standardMap[i];
        }
    }

    void load() {
        const auto path = settingsPath();
        wchar_t value[32768]{};
        auto get = [&](const wchar_t* section, const wchar_t* key) {
            GetPrivateProfileStringW(section, key, L"", value, 32768, path.c_str());
            return std::wstring(value);
        };
        romFolder = get(L"folders", L"rom_folder");
        lastRom = get(L"folders", L"last_rom");
        savedController = get(L"input", L"device");
        soundEnabled = GetPrivateProfileIntW(L"audio", L"enabled", 1, path.c_str()) != 0;
        soundVolume = std::min(100u, GetPrivateProfileIntW(L"audio", L"volume", 100, path.c_str()));
        scaleIndex = std::min(2u, GetPrivateProfileIntW(L"video", L"scale", 1, path.c_str()));
        windowMode = std::min(2u, GetPrivateProfileIntW(L"video", L"window_mode", 2, path.c_str()));
        videoAspect = std::min(2u, GetPrivateProfileIntW(L"video", L"aspect", 0, path.c_str()));
        integerScale = GetPrivateProfileIntW(L"video", L"integer", 0, path.c_str()) != 0;
        smoothVideo = GetPrivateProfileIntW(L"video", L"smooth", 0, path.c_str()) != 0;
        displayFormat = std::min(4u, GetPrivateProfileIntW(L"video", L"decoder", 0, path.c_str()));
        timingMode = std::min(5u, GetPrivateProfileIntW(L"system", L"timing", 0, path.c_str()));
        keepNvram = GetPrivateProfileIntW(L"system", L"nvram", 1, path.c_str()) != 0;
        fpsLog = GetPrivateProfileIntW(L"system", L"fps_log", 1, path.c_str()) != 0;
        debugLog = GetPrivateProfileIntW(L"system", L"debug_log", 0, path.c_str()) != 0;
        for (unsigned i = 0; i < 12; ++i) {
            keyboardKeys[i] = std::min(255u, GetPrivateProfileIntW(
                L"keyboard", std::to_wstring(i).c_str(), keyboardKeys[i], path.c_str()));
        }
        loadControllerFor(savedController);
    }

    bool save(const std::wstring& activeControllerKey) const {
        const auto path = settingsPath();
        bool ok = true;
        auto put = [&](const wchar_t* section, const wchar_t* key, const std::wstring& val) {
            if (!WritePrivateProfileStringW(section, key, val.c_str(), path.c_str())) ok = false;
        };
        put(L"folders", L"rom_folder", romFolder);
        put(L"folders", L"last_rom", lastRom);
        put(L"audio", L"enabled", std::to_wstring(soundEnabled ? 1 : 0));
        put(L"audio", L"volume", std::to_wstring(soundVolume));
        put(L"video", L"scale", std::to_wstring(scaleIndex));
        put(L"video", L"window_mode", std::to_wstring(windowMode));
        put(L"video", L"aspect", std::to_wstring(videoAspect));
        put(L"video", L"integer", std::to_wstring(integerScale ? 1 : 0));
        put(L"video", L"smooth", std::to_wstring(smoothVideo ? 1 : 0));
        put(L"video", L"decoder", std::to_wstring(displayFormat));
        put(L"system", L"timing", std::to_wstring(timingMode));
        put(L"system", L"nvram", std::to_wstring(keepNvram ? 1 : 0));
        put(L"system", L"fps_log", std::to_wstring(fpsLog ? 1 : 0));
        put(L"system", L"debug_log", std::to_wstring(debugLog ? 1 : 0));
        const std::wstring devKey = activeControllerKey.empty() ? std::wstring(L"keyboard") : activeControllerKey;
        put(L"input", L"device", devKey);
        const auto section = L"controller-" + devKey;
        put(section.c_str(), L"preset", std::to_wstring(controllerPreset));
        for (unsigned i = 0; i < 12; ++i) {
            put(section.c_str(), std::to_wstring(i).c_str(), std::to_wstring(padMap[i]));
            put(L"keyboard", std::to_wstring(i).c_str(), std::to_wstring(keyboardKeys[i]));
        }
        return ok;
    }
};

inline std::unique_ptr<Gdiplus::Image> loadImageAsset(unsigned resourceId, const wchar_t* filename, IStream** streamOut) {
    if (streamOut) *streamOut = nullptr;
    HMODULE module = GetModuleHandleW(nullptr);
    if (HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10))) {
        DWORD size = SizeofResource(module, res);
        const void* data = LockResource(LoadResource(module, res));
        if (data && size > 0) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
            if (mem) {
                void* dst = GlobalLock(mem);
                if (dst) {
                    std::memcpy(dst, data, size);
                    GlobalUnlock(mem);
                    IStream* stream = nullptr;
                    if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
                        auto img = std::unique_ptr<Gdiplus::Image>(Gdiplus::Bitmap::FromStream(stream));
                        if (img && img->GetLastStatus() == Gdiplus::Ok) {
                            if (streamOut) *streamOut = stream;
                            return img;
                        }
                        img.reset();
                        stream->Release();
                    } else {
                        GlobalFree(mem);
                    }
                } else {
                    GlobalFree(mem);
                }
            }
        }
    }
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring base(exe);
    const auto slash = base.find_last_of(L"\\/");
    if (slash != std::wstring::npos) base.resize(slash);
    const std::wstring rel = std::wstring(L"assets/") + filename;
    for (const auto& candidatePath : {
        base + L"/" + rel,
        base + L"/../" + rel,
        base + L"/../../" + rel,
        rel
    }) {
        auto* bmp = Gdiplus::Bitmap::FromFile(candidatePath.c_str());
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            return std::unique_ptr<Gdiplus::Image>(bmp);
        }
        delete bmp;
    }
    return {};
}

} // namespace win
} // namespace oceanblast
#endif
