# 43. Famiko-Style Windows Launcher, Embedded Gameplay & Controller Input

## 1. Overview & Motivation

Following the resolution of the handheld 12-bpp `'LCD'` mode (`GPH8 = 1`) in [docs/42](42_wade_gph8_lcd_mode.md), the Windows graphical frontend (`bin/oceanblast.exe`) was upgraded to match the architecture, visual design, and controller input stack of the `Famiko` Windows frontend:

1. **Unified `1024x768` Main Window with Logo & Single-Window Gameplay:**
   - Launching `bin/oceanblast.exe` without a ROM path opens a `1024x768` resizable window (`OceanBlastLauncherWindow`) styled in dark ocean-navy (`RGB(6, 20, 36)`) with the glossy **OceanBlast** logo (`assets/oceanblast-logo.png`, embedded via `RT_RCDATA` ID `101`) centered on the start screen and the wave emblem (`assets/oceanblast-symbol.png`, ID `102`) set as the window/taskbar icon (`WM_SETICON`).
   - A native Win32 menu bar (**`Datei`** | **`Einstellungen`**) provides **`ROM laden...` (`Strg+O`)**, **`Emulation beenden` (`Esc`)**, **`Beenden`**, and direct jumps into all six settings categories (**`Ordner & System`**, **`Steuerung`**, **`Sound`**, **`Video`**, **`Timing & Diagnose`**, **`Kurztasten & Hilfe`**).
   - Cartridges can be loaded via `Strg+O`, the **`Datei -> ROM laden...`** menu item, or by **dragging and dropping** a `.bin` file directly onto the window (`WM_DROPFILES`).
   - Instead of opening a separate popup window and hiding the launcher, the emulator child process is spawned with `--parent-hwnd <uintptr_t>` and renders directly inside the `1024x768` main window (`Display::setParentWindow`), returning cleanly to the OceanBlast start screen when the game ends (`Esc` or **`Datei -> Emulation beenden`**).

2. **Full XInput, DirectInput8 & Google Stadia Controller Support:**
   - Added `src/display/input_mapping.h` and `src/display/windows_input.h` (`oceanblast::win::InputManager`), polling both **XInput** (`xinput1_4.dll`, `xinput9_1_0.dll`, `xinput1_3.dll`) and **DirectInput8** (`DirectInput8Create` with `DI8DEVCLASS_GAMECTRL` enumeration and `DISCL_NONEXCLUSIVE | DISCL_BACKGROUND` cooperative level).
   - Specifically detects **Google Stadia Controllers** (`VID_18D1` / `"Stadia"` product name) as well as Xbox, PlayStation DualShock/DualSense, and generic USB gamepads.
   - Maps all 12 digiBLAST hardware lines (`GPF2/7/3/6` D-Pad, `GPF0/1/4` A/B/C, `GPG11/8` L/R shoulders, `GPG10` Start/Stop, `GPG9` Select/Play-Pause, and `GPG0/13` Rewind/Fast-Forward), plus automatic 8-way POV hat D-Pad, left analog stick D-Pad (`±14000` deadzone), and `L2`/`R2` analog triggers (`> 64`) for media rewind/fast-forward.

3. **Interactive `OceanBlast — Einstellungen` Dialog (`850x595`) with Live Controller Schematic:**
   - Modeled after `Famiko`'s settings dialog, with a left `LISTBOX` category navigation and a custom-drawn **live GDI+ controller schematic** (`drawController`) on the **`Steuerung`** page that highlights pressed buttons, D-Pad directions, shoulder triggers, and analog stick positions in real time (`25 ms` timer).
   - Includes **click-to-learn interactive remapping** for all 12 digiBLAST controls (both controller buttons/directions and host keyboard keys) and three one-click controller presets (**`Preset: Stadia / Xbox`**, **`Preset: PlayStation`**, **`Preset: digiBLAST / USB`**).
   - Persists all settings and bindings to `%APPDATA%\OceanBlast\windows.ini` (`FrontendSettings`, overridable via `OCEANBLAST_SETTINGS_PATH` for deterministic testing) and live-reloads settings into a running game via `WM_APP + 1`.

---

## 2. Architecture & New Components

### 2.1 Embedded Win32 Resources (`src/display/oceanblast.rc`)

To ensure `bin/oceanblast.exe` remains self-contained even when launched outside the repository root, `src/display/oceanblast.rc` embeds both PNG assets as `RCDATA` resources compiled via `windres`:

| Resource ID | Macro | Asset Path | Purpose |
| :---: | :--- | :--- | :--- |
| `101` | `OCEANBLAST_IDR_LOGO_PNG` | `assets/oceanblast-logo.png` | Full glossy OceanBlast wordmark rendered on the start screen and in the settings sidebar |
| `102` | `OCEANBLAST_IDR_SYMBOL_PNG` | `assets/oceanblast-symbol.png` | `256x256` RGBA wave + `'O'` emblem converted via `Gdiplus::Bitmap::GetHICON` for `ICON_BIG` and `ICON_SMALL` |

`oceanblast::win::loadImageAsset()` loads the PNG byte stream from the executable resource via `FindResourceW` / `CreateStreamOnHGlobal`, with a transparent fallback to `assets/` on disk.

### 2.2 Controller Mapping & Input Stack (`src/display/input_mapping.h`, `src/display/windows_input.h`)

`oceanblast::win::HostPad` normalizes controller state across XInput and DirectInput8:
- **`buttons` (`uint32_t`):** Up to 16 digital buttons (`B1`..`B16`). For XInput pads, `B1..B10` map to `A, B, X, Y, LB, RB, Back, Start, L3, R3`.
- **`hat` (`uint8_t`):** 8-way POV hat (`0..7`, `0xFF` centered), synthesized from `XINPUT_GAMEPAD_DPAD_*` on XInput or read from `DIJOYSTATE2::rgdwPOV[0]` on DirectInput8 (`0..35999` hundredths of a degree).
- **`lx, ly, rx, ry` (`int16_t`):** Normalized signed 16-bit thumbstick axes (`-32768..32767`).
- **`lt, rt` (`uint8_t`):** Normalized 8-bit triggers (`0..255`), read from `bLeftTrigger`/`bRightTrigger` on XInput, axes `rglSlider` / `lRx` / `lRy` on DirectInput, or `B7`/`B8` digital trigger buttons (`L2`/`R2`) on DirectInput Stadia/PlayStation pads.

#### Default Controller Mapping (`standardMap()`)

| digiBLAST Control | GPIO / EINT Line | Default Controller Binding | Additional Automatic Input | Default Keyboard Key |
| :--- | :--- | :--- | :--- | :--- |
| **Steuerkreuz Oben** | `GPF2` (`EINT2`) | `Hat Up` (`0x100`) | Left Stick Up (`ly < -14000`) | `Up` (`VK_UP`) |
| **Steuerkreuz Unten** | `GPF7` (`EINT7`) | `Hat Down` (`0x102`) | Left Stick Down (`ly > +14000`) | `Down` (`VK_DOWN`) |
| **Steuerkreuz Links** | `GPF3` (`EINT3`) | `Hat Left` (`0x103`) | Left Stick Left (`lx < -14000`) | `Left` (`VK_LEFT`) |
| **Steuerkreuz Rechts** | `GPF6` (`EINT6`) | `Hat Right` (`0x101`) | Left Stick Right (`lx > +14000`) | `Right` (`VK_RIGHT`) |
| **A-Knopf (Unten)** | `GPF0` (`EINT0`) | `B1` (`A` / Cross) | — | `Z` (`K` alias) |
| **B-Knopf (Rechts)** | `GPF1` (`EINT1`) | `B2` (`B` / Circle) | — | `X` (`J` alias) |
| **C-Knopf (Links/Oben)** | `GPF4` (`EINT4`) | `B3` (`X` / Square) | `B4` (`Y` / Triangle in Stadia/Xbox preset) | `C` |
| **L-Schulter** | `GPG11` (`EINT19`) | `B5` (`L1` / `LB`) | — | `A` (`Q` alias) |
| **R-Schulter** | `GPG8` (`EINT16`) | `B6` (`R1` / `RB`) | — | `S` (`W` alias) |
| **Start / Stop (`KEY_S`)** | `GPG10` (`EINT18`) | `B8` (`Start` / Options) | `B12` on Stadia DirectInput | `Enter` (`F9` alias) |
| **Select / Play (`KEY_P`)** | `GPG9` (`EINT17`) | `B7` (`Select` / Back) | `B11` on Stadia DirectInput | `Space` (`F10` alias) |
| **Rücklauf (`KEY_B`)** | `GPG0` (`EINT8`) | `B9` (`L3` / L2) | Left Trigger `LT > 64` | `F8` |
| **Vorlauf (`KEY_N`)** | `GPG13` (`EINT21`) | `B10` (`R3` / R2) | Right Trigger `RT > 64` | `F12` |

Because Google Stadia controllers expose `Options`/`Menu` and `Assistant`/`Capture` on higher DirectInput button indices (`B11`/`B12`) when used over generic DirectInput without an XInput wrapper, the **`Preset: Stadia / Xbox`** button automatically binds both `B1/B2/B3/B4` face buttons and `B7/B8`/`B11/B12` menu buttons when a Stadia controller is active, while **`Click-to-Learn`** allows any physical button, hat direction, stick axis, or trigger to be assigned interactively with a single click.

### 2.3 Single-Window Embedded Gameplay (`--parent-hwnd`)

In `src/display/display_win32.cpp`:
- `Display::setParentWindow(uintptr_t hwnd)` attaches the emulator directly to the launcher's `1024x768` window handle rather than creating a second top-level window.
- When embedded in the launcher (`m_embeddedParent = true`), `Display` subclasses the parent `HWND` via `SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, ...)` for the duration of the emulation session, handles `WM_SIZE`, `WM_KEYDOWN`/`WM_KEYUP`, `WM_DEVICECHANGE` (controller hot-plug), and `WM_APP + 1` (live settings reload from the launcher's `Einstellungen` dialog), and restores the original `WndProc` cleanly in `Display::shutdown()`.
- In every `Display::processEvents()` call, `m_inputManager->poll()` updates `m_gamepadMask = win::mapPad(pad, m_padMap)`, and `Display::refreshButtons()` combines `m_keyboardButtons | m_mouseMask | m_gamepadMask` onto the S3C2410 GPIO bus.

---

## 3. Validation & Verification

1. **Automated Unit & Win32 GUI Tests (`make test`, `make test-windows`):**
   - Extended `tests/launcher_defaults_win32.cpp` to verify:
     - `oceanblast::win::standardMap()` default bindings, 8-way POV hat mapping, analog stick deadzones (`±14000`), and `L2`/`R2` trigger mapping (`lt/rt > 64`).
     - `oceanblast::win::firstNewControl()` edge-detection for interactive controller button learning.
     - `FrontendSettings` INI round-trip persistence (`saveFrontendSettings` / `loadFrontendSettings`).
     - Non-blocking preview rendering of both the main `OceanBlast` start screen (`--preview-launcher build/launcher-startup-preview.png`) and the `Steuerung` controller settings page (`--preview-controls build/launcher-controls-preview.png`).
     - Command-line synthesis (`buildCommandLine`) including `--parent-hwnd`, `--scale`, `--window-mode`, `--sound`, and `--clock-mips`.
2. **All Regression Suites:**
   - `make`, `make test`, `make test-windows`, and `python tests/guest_function_attribution.py` pass without warnings or errors.
