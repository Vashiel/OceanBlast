#include "display.h"
#include "framebuffer.h"

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

namespace oceanblast {

namespace {

struct SdlDisplayState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_GameController* controller = nullptr;
    SDL_JoystickID controllerId = -1;
    unsigned textureHeight = 0;
    bool syncedPresentation = false;
    uint64_t presentedFrames = 0;
    bool prevAltReturn = false;
};

uint64_t monotonicMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

unsigned keySlotForSdl(SDL_Keycode sym) {
    // Stable slot indices inside Display::m_keyboardButtons (std::array<uint32_t, ...>).
    switch (sym) {
        case SDLK_UP: return 1;
        case SDLK_DOWN: return 2;
        case SDLK_LEFT: return 3;
        case SDLK_RIGHT: return 4;
        case SDLK_z: return 5;
        case SDLK_k: return 6;
        case SDLK_x: return 7;
        case SDLK_j: return 8;
        case SDLK_c: return 9;
        case SDLK_a: return 10;
        case SDLK_q: return 11;
        case SDLK_s: return 12;
        case SDLK_w: return 13;
        case SDLK_RETURN: return 14;
        case SDLK_SPACE: return 15;
        case SDLK_F8: return 16;
        case SDLK_b: return 17;
        case SDLK_PAGEUP: return 18;
        case SDLK_COMMA: return 19;
        case SDLK_F9: return 20;
        case SDLK_BACKSPACE: return 21;
        case SDLK_F10: return 22;
        case SDLK_p: return 23;
        case SDLK_F12: return 24;
        case SDLK_n: return 25;
        case SDLK_PAGEDOWN: return 26;
        case SDLK_PERIOD: return 27;
        default: return 0;
    }
}

uint32_t buttonMaskForSdl(SDL_Keycode sym) {
    switch (sym) {
        case SDLK_UP: return BTN_UP;
        case SDLK_DOWN: return BTN_DOWN;
        case SDLK_LEFT: return BTN_LEFT;
        case SDLK_RIGHT: return BTN_RIGHT;
        case SDLK_z:
        case SDLK_k: return BTN_A;
        case SDLK_x:
        case SDLK_j: return BTN_B;
        case SDLK_c: return BTN_C;
        case SDLK_a:
        case SDLK_q: return BTN_L;
        case SDLK_s:
        case SDLK_w: return BTN_R;
        case SDLK_RETURN: return BTN_START;
        case SDLK_SPACE: return BTN_SELECT;
        case SDLK_F8:
        case SDLK_b:
        case SDLK_PAGEUP:
        case SDLK_COMMA: return BTN_REWIND;
        case SDLK_F9:
        case SDLK_BACKSPACE: return BTN_STOP;
        case SDLK_F10:
        case SDLK_p: return BTN_PLAY;
        case SDLK_F12:
        case SDLK_n:
        case SDLK_PAGEDOWN:
        case SDLK_PERIOD: return BTN_FORWARD;
        default: return 0;
    }
}

uint32_t pollControllerMask(SDL_GameController* controller) {
    if (!controller || !SDL_GameControllerGetAttached(controller)) return 0;
    uint32_t mask = 0;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) mask |= BTN_UP;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) mask |= BTN_DOWN;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) mask |= BTN_LEFT;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) mask |= BTN_RIGHT;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) mask |= BTN_A;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) mask |= BTN_B;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X) ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y)) mask |= BTN_C;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) mask |= BTN_L;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) mask |= BTN_R;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK)) mask |= BTN_SELECT;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) mask |= BTN_START;

    const int axisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const int axisY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    if (axisX < -12000) mask |= BTN_LEFT;
    if (axisX > 12000) mask |= BTN_RIGHT;
    if (axisY < -12000) mask |= BTN_UP;
    if (axisY > 12000) mask |= BTN_DOWN;

    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) mask |= BTN_REWIND;
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) mask |= BTN_FORWARD;
    return mask;
}

void openFirstController(SdlDisplayState& state) {
    if (state.controller) return;
    const int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* ctrl = SDL_GameControllerOpen(i);
        if (ctrl) {
            state.controller = ctrl;
            state.controllerId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
            break;
        }
    }
}

SDL_Rect computeDestinationRect(int outW, int outH, int videoAspect, bool integerScale) {
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    int width = outW;
    int height = outH;
    if (integerScale && outW >= LCD_WIDTH && outH >= LCD_HEIGHT) {
        const int factor = std::max(1, std::min(outW / LCD_WIDTH, outH / LCD_HEIGHT));
        width = LCD_WIDTH * factor;
        height = LCD_HEIGHT * factor;
    } else if (videoAspect == 1) {
        if (outW * 3 > outH * 4) width = outH * 4 / 3;
        else height = outW * 3 / 4;
    } else if (videoAspect != 2) {
        if (outW * 2 > outH * 3) width = outH * 3 / 2;
        else height = outW * 2 / 3;
    }
    return SDL_Rect{(outW - width) / 2, (outH - height) / 2, std::max(1, width), std::max(1, height)};
}

} // namespace

Display::Display(int scale)
    : m_scale(scale), m_open(false), m_buttonMask(0),
      m_hwnd(nullptr), m_hdc(nullptr), m_bitmapInfo(nullptr) {
    m_pixels.resize(LCD_WIDTH * LCD_HEIGHT, 0);
}

Display::~Display() {
    close();
}

uint32_t Display::mapConfiguredKeyboardKey(unsigned /*key*/) const {
    return 0;
}

void Display::reloadUserSettings() {}

void Display::onDeviceChange() {
    if (auto* state = static_cast<SdlDisplayState*>(m_presenter)) {
        openFirstController(*state);
    }
}

bool Display::init(const char* title) {
    close();

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            std::cerr << "[Display] SDL video initialization failed: " << SDL_GetError() << '\n';
            return false;
        }
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, m_smoothVideo ? "1" : "0");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    const int width = std::max<int>(LCD_WIDTH, LCD_WIDTH * std::max(1, m_scale));
    const int height = std::max<int>(LCD_HEIGHT, LCD_HEIGHT * std::max(1, m_scale));
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (m_startFullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    auto* state = new SdlDisplayState();
    state->window = SDL_CreateWindow(title ? title : "OceanBlast - Nikko digiBLAST Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!state->window) {
        std::cerr << "[Display] Failed to create SDL window: " << SDL_GetError() << '\n';
        delete state;
        return false;
    }

    state->renderer = SDL_CreateRenderer(state->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (state->renderer) {
        state->syncedPresentation = true;
    } else {
        state->renderer = SDL_CreateRenderer(state->window, -1, SDL_RENDERER_SOFTWARE);
        state->syncedPresentation = false;
    }
    if (!state->renderer) {
        std::cerr << "[Display] Failed to create SDL renderer: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(state->window);
        delete state;
        return false;
    }

    state->texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, LCD_HEIGHT);
    if (!state->texture) {
        std::cerr << "[Display] Failed to create SDL texture: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        delete state;
        return false;
    }
    state->textureHeight = LCD_HEIGHT;
    openFirstController(*state);

    m_presenter = state;
    m_hwnd = state->window;
    m_fullscreen = m_startFullscreen;
    m_open = true;

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);
    SDL_RenderPresent(state->renderer);

    std::cout << "[Display] SDL2 display window initialized (" << width << "x" << height << ", "
              << m_scale << "x scale)." << std::endl;
    return true;
}

void Display::processEvents() {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    if (!m_open || !state) return;

    const uint64_t nowMs = monotonicMs();
    const uint32_t previousPulses = m_pulses.mask();
    m_pulses.expire(nowMs);
    if (previousPulses != m_pulses.mask()) updateHostButtons();

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                m_open = false;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    m_open = false;
                } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    refreshLayout();
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                openFirstController(*state);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (state->controller && event.cdevice.which == state->controllerId) {
                    SDL_GameControllerClose(state->controller);
                    state->controller = nullptr;
                    state->controllerId = -1;
                    gamepadButtons(0);
                    openFirstController(*state);
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const bool isDown = (event.type == SDL_KEYDOWN);
                const bool isRepeat = event.key.repeat != 0;
                const SDL_Keycode sym = event.key.keysym.sym;
                const Uint16 mod = event.key.keysym.mod;

                if (sym == SDLK_F11 || (sym == SDLK_RETURN && (mod & KMOD_ALT))) {
                    if (isDown && !isRepeat) toggleFullscreen();
                    break;
                }
                if (sym == SDLK_ESCAPE) {
                    if (isDown && !isRepeat) {
                        if (m_fullscreen) toggleFullscreen();
                        else close();
                    }
                    break;
                }
                if (sym == SDLK_F5) {
                    if (isDown && !isRepeat) paused = !paused;
                    break;
                }
                if (sym == SDLK_F6) {
                    if (isDown) { paused = true; singleStep = true; }
                    break;
                }
                if (sym == SDLK_F7) {
                    if (isDown) snapshot = true;
                    break;
                }
                if (sym == SDLK_AUDIOPREV) {
                    if (isDown && !isRepeat) pulseButton(BTN_REWIND, 180);
                    break;
                }
                if (sym == SDLK_AUDIOSTOP) {
                    if (isDown && !isRepeat) pulseButton(BTN_STOP, 180);
                    break;
                }
                if (sym == SDLK_AUDIOPLAY) {
                    if (isDown && !isRepeat) pulseButton(BTN_PLAY, 180);
                    break;
                }
                if (sym == SDLK_AUDIONEXT) {
                    if (isDown && !isRepeat) pulseButton(BTN_FORWARD, 180);
                    break;
                }

                const unsigned slot = keySlotForSdl(sym);
                const uint32_t mask = buttonMaskForSdl(sym);
                if (slot != 0 && mask != 0) {
                    keyboardButton(slot, mask, isDown);
                }
                break;
            }
            default:
                break;
        }
    }

    if (m_open && state->controller && (paused || nowMs - m_lastInputPollMs >= 8)) {
        m_lastInputPollMs = nowMs;
        gamepadButtons(pollControllerMask(state->controller));
    }
}

void Display::setTitle(const std::string& title) {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    if (state && state->window) SDL_SetWindowTitle(state->window, title.c_str());
}

void Display::updateFrame(const uint8_t* sdram, uint32_t fbPhysAddr, bool is16bpp, size_t stride, unsigned sourceHeight) {
    if (!m_open || !sdram) return;

    constexpr uint32_t SDRAM_BASE = 0x30000000;
    constexpr uint32_t SDRAM_SIZE = 32 * 1024 * 1024;
    const size_t rowBytes = is16bpp ? 480 : 360;
    if (!stride) stride = rowBytes;
    if (!sourceHeight || sourceHeight > 1024 || stride < rowBytes || stride > SDRAM_SIZE / sourceHeight) return;
    const size_t fbSize = stride * sourceHeight;
    if (fbPhysAddr < SDRAM_BASE || (fbPhysAddr - SDRAM_BASE) + fbSize > SDRAM_SIZE) return;

    updateFrameData(sdram + (fbPhysAddr - SDRAM_BASE), is16bpp, stride, sourceHeight);
}

void Display::updateFrameData(const uint8_t* src, bool is16bpp, size_t stride, unsigned sourceHeight) {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    if (!m_open || !state || !state->renderer || !src || !sourceHeight || sourceHeight > 1024 ||
        stride < (is16bpp ? 480u : 360u)) {
        return;
    }

    m_sourceHeight = sourceHeight;
    m_pixels.resize(static_cast<size_t>(LCD_WIDTH) * sourceHeight);
    decodeFramebuffer(src, m_pixels.data(), is16bpp, stride, LCD_WIDTH, sourceHeight);

    if (!state->texture || state->textureHeight != sourceHeight) {
        if (state->texture) SDL_DestroyTexture(state->texture);
        state->texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGB888,
            SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, static_cast<int>(sourceHeight));
        state->textureHeight = state->texture ? sourceHeight : 0;
    }
    if (!state->texture) return;

    SDL_UpdateTexture(state->texture, nullptr, m_pixels.data(), LCD_WIDTH * static_cast<int>(sizeof(uint32_t)));
    refreshLayout();
    ++state->presentedFrames;
}

bool Display::usesSyncedPresentation() const {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    return state && state->syncedPresentation;
}

uint64_t Display::presentedFrames() const {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    return state ? state->presentedFrames : 0;
}

void Display::refreshLayout() {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    if (!state || !state->renderer || !state->texture) return;
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(state->renderer, &outW, &outH);
    const SDL_Rect dst = computeDestinationRect(outW, outH, m_videoAspect, m_integerScale);
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);
    SDL_RenderCopy(state->renderer, state->texture, nullptr, &dst);
    SDL_RenderPresent(state->renderer);
}

void Display::toggleFullscreen() {
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    if (!state || !state->window) return;
    m_fullscreen = !m_fullscreen;
    SDL_SetWindowFullscreen(state->window, m_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    refreshLayout();
}

void Display::paintShell(void* /*target*/) {}
void Display::refreshControls() {}
void Display::renderToDc(void* /*targetHdc*/) {}
void Display::mouseButton(int /*x*/, int /*y*/, bool /*down*/) {}

void Display::keyboardButton(unsigned key, uint32_t mask, bool down) {
    if (key >= m_keyboardButtons.size()) return;
    m_keyboardButtons[key] = down ? mask : 0;
    updateHostButtons();
}

void Display::gamepadButtons(uint32_t mask) {
    if (m_gamepadMask != mask) {
        m_gamepadMask = mask;
        updateHostButtons();
    }
}

void Display::pulseButton(uint32_t mask, uint32_t durationMs) {
    if (!mask) return;
    if (m_buttonMask & mask) {
        m_pulses.release(mask);
        updateHostButtons();
    }
    m_pulses.press(mask, monotonicMs(), durationMs);
    updateHostButtons();
}

void Display::updateHostButtons() {
    uint32_t next = m_mouseMask | m_gamepadMask | m_pulses.mask();
    for (auto key : m_keyboardButtons) next |= key;
    if (next != m_buttonMask) {
        m_buttonMask = next;
        m_buttonTransitions.push_back(next);
    }
}

void Display::close() {
    m_open = false;
    auto* state = static_cast<SdlDisplayState*>(m_presenter);
    m_presenter = nullptr;
    m_hwnd = nullptr;
    if (!state) return;
    if (state->controller) SDL_GameControllerClose(state->controller);
    if (state->texture) SDL_DestroyTexture(state->texture);
    if (state->renderer) SDL_DestroyRenderer(state->renderer);
    if (state->window) SDL_DestroyWindow(state->window);
    delete state;
    if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

} // namespace oceanblast
