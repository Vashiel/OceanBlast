# Console Skin and Fullscreen Display

## Window modes

The Windows launcher offers **Console Skin**, **Fullscreen**, and **Plain Window**. Console Skin is the default. The reconstructed silver console surrounds the live LCD output; it does not replace cartridge framebuffer data. The companion loading illustration is an asset preview, not a simulated guest loading progress indicator.

- `--window-mode skin`: console frame and interactive controls.
- `--window-mode plain`: LCD-only window.
- `--fullscreen`: borderless fullscreen LCD on the window's current monitor.
- **F11** or **Alt+Enter**: switch between the selected window appearance and fullscreen.
- **Escape**: return from fullscreen; close the game when already windowed.

Scale presets specify native LCD multiples in Plain Window. In Console Skin they size the overall console window, which is capped to fit the available desktop work area; the LCD occupies only its inset region. The LCD retains its native 3:2 display aspect ratio. Fullscreen uses black letterboxing rather than stretching the image. Source framebuffers with 240 decoded rows still fit the complete image into the LCD; source height does not change the physical LCD aspect. The former window position and size are restored on return from fullscreen.

The skin artwork is a high-resolution reconstruction of the cartridge loading illustration, not a pixel-identical enlargement. Skin and loading assets are stored in `assets/`; the original cartridge image is not distributed. Keep the assets directory beside the executable or one level above its directory. Missing skin artwork falls back to Plain Window.

## Input feedback

Keyboard input highlights the corresponding console controls. The D-pad center shifts toward the held direction. Skin controls also accept mouse presses; dragging a held mouse button between D-pad directions changes the active direction. Releasing mouse capture or losing focus releases mouse input. Losing focus releases keyboard input as well. Keyboard aliases and mouse input are combined, so releasing one source does not release a button still held by another source.

| Control | Keyboard | Guest input |
| --- | --- | --- |
| D-pad | Arrow keys | Existing directional GPIO inputs |
| A | Z / K | GPF0 |
| B | X / J | GPF1 |
| Third action / C | C | GPF4, Linux KEY_5; physical C correspondence remains provisional |
| Select / Play-Pause | Space / F10 | GPG9, Linux KEY_P |
| Start / Stop | Enter / F9 | GPG10, Linux KEY_S |
| Rewind | F8 | GPG0, Linux KEY_B |
| Forward | F12 | GPG13, Linux KEY_N |
| Existing L / R inputs | A / Q and S / W | GPG11 / GPG8 |

Play-Pause and Select share a guest line, as do Stop and Start, so both matching skin controls highlight together. Media actions are sent to the guest player, not implemented as host-side file seeking. Cartridge software decides how it handles them. The existing F5/F6/F7 emulator debugging controls remain separate. Input scripts retain the existing bits 0–9 and add C at bit 10, Rewind at bit 11, and Forward at bit 12; valid masks range from `0x0000` to `0x1fff`.

## Player mapping evidence

The checked Player executable has SHA-256 `b0f8d327596f604b34d5cf22ecc062ae9ba90e793d0372b7b6dd9afeeebe36ac8`. Its keypad press dispatch at guest executable addresses `0xe614`–`0xe6d4` recognizes Linux keycodes `0x19`, `0x1f`, `0x30`, and `0x31`, converts them to `p`, `s`, `b`, and `n`, and dispatches player signals 12, 8, 16, and 14 respectively. Releases dispatch the paired release signals. This supplies Player input evidence independently of the console rendering.

The kernel keypad table observed at SDRAM offset `0x180f18` maps KEY_P to GPG9, KEY_S to GPG10, KEY_B to GPG0, and KEY_N to GPG13. The third action input exposes the table's GPF4 / KEY_5 line; the physical C label still requires independent verification. No other button wiring is inferred from artwork.

The bounded Sonic X 1 Italian/Spanish replay in `tests/video_media_controls.txt` starts from erased EEPROM and confirms the language/menu before sending media inputs. The guest log records KEY_P presses with PAUSED and PLAYING transitions, KEY_B with backward seek timer start/stop, KEY_N with forward seek timer start/stop, and KEY_S with STOP PLAYING. The final optimized replay exits normally after 5.5 billion instructions, captures 280 complete video sweeps, and submits 310 synchronized images. [Player media-control results](validation/2026-10-09_player_media_controls.csv) record the executable hash. These transitions establish that all four controls reach this Player implementation; music cartridges and other player versions remain unvalidated.

## Presentation architecture

The parent window draws the console shell through GDI+. A separate child window owns the LCD swap chain. Skin repainting does not rebuild or copy guest framebuffers. The resized shell is cached until the window dimensions change; button feedback draws over that cached shell. The existing complete-frame capture and vertically synchronized DXGI output remain active. GDI remains the fallback.

Switching window mode recreates the LCD swap chain at the new output size, submits the current decoded image, and retains the cumulative presentation count. The window thread continues servicing synchronous presentation messages during worker shutdown. Guest CPU, peripheral clocks, DMA and cartridge memory remain independent of window dimensions.

## Optimized Windows build

The installed candidate uses the existing O3 unity/profile-guided compilation path. Training includes Superstar Chefs, Wade Hixton's Counter Punch and DigiQUAD, 2.4 billion instructions each, the 4x diagnostic allowance, `tests/chefs_ratio4_confirm.txt`, and GUI presentation. The builder options are `-Unity -Optimization O3 -Gui -Steps 2400000000 -CpuStepsPerTick 4 -InputScript tests/chefs_ratio4_confirm.txt`. Profile artifacts and cartridge captures remain under ignored `build/`. These diagnostic timing options describe validation and do not replace the launcher's standard timing default.

## Validation

`make test` runs the CPU, peripheral, framebuffer, audio and input regressions. `make test-windows` opens bounded presentation test windows and checks complete source-height output, press feedback, all 13 clickable controls, focus-loss release, combined mouse/keyboard holds, and repeated fullscreen/escape transitions with restoration of the prior window geometry.

The warning-enabled regression build passes 249 regression checks and 17 Windows presentation/control checks, including both DXGI and forced GDI modes. Bounded 600-million-instruction starts of Superstar Chefs in Console Skin, Wade Hixton's Counter Punch in Plain Window, and Crazy Jack in Fullscreen exit normally and submit synchronized images. [Window-mode startup results](validation/2026-10-09_window_modes.csv) record the tested executable hash. These starts do not establish sustained gameplay acceptance.

A [paired headless build comparison](validation/2026-10-09_skin_build_comparison.csv) runs Superstar Chefs with 2.4 billion instructions, erased EEPROM and the same input script in baseline/candidate/candidate/baseline order. All four complete SDRAM hashes match. Throughput is 44.20 and 45.06 MIPS for the preceding executable, versus 34.97 and 47.96 MIPS for the skin candidate. The candidate varies substantially between runs; these measurements do not isolate compiler-profile effects or establish an unchanged gameplay speed. Startup and shutdown are included.

These checks establish host window and input behavior. They do not establish hardware-equivalent audio timing, physical C-button identity, or complete video/music cartridge seek compatibility.
