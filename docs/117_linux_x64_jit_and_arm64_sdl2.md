# Linux x86-64 System-V JIT, ARM64 Interpreter Build and SDL2 Backends

Date: 2026-11-16. Extends the `v0.4.0` engine from [116](116_page_tlb_generation_and_flat_mmio.md) to GNU/Linux `x86_64` (System-V AMD64 ABI JIT + SDL2) and GNU/Linux `aarch64` (ARM920T + FPA11 interpreter + SDL2) while keeping the Windows 7–11 `x64` JIT and `x86` PGO builds intact.

## Delivered behavior

1. **System-V AMD64 ABI & POSIX W^X port (`src/cpu/x64_execution_engine.cpp`, `tests/x64_entry_abi.cpp`):**
   * Parameterized `buildEntryBridge()`, `emitMemory()`, and `checkedMultiple` helper marshalling across Windows x64 (`RCX`/`RDX`/`R8`/`R9`, 40-byte reserve) and System-V AMD64 (`RDI`/`RSI`/`RDX`/`RCX`, 24-byte stack frame).
   * Preserved internal shared callee-saved registers (`R12 = ARM920TState*`, `R13 = NativeRun*`, `R14d = base`, `R15d = cycles`, `EBX = lastCycles`, `EBP = CPSR`) uniformly across both ABIs. On System-V slow-load and store helper paths with post-indexed `writeback`, `esi` is spilled/reloaded at `[rsp]` so the zero-overhead fast inline load path avoids stack traffic.
   * Replaced Windows-only `VirtualQuery` executable span verification in `queryExecutableRegion()` with a cross-platform helper that parses `/proc/self/maps` (`r-x`) on POSIX while keeping the per-slab cached `(verifiedRegionBase, verifiedRegionSize)` fast path.
   * Fixed LP64 `std::initializer_list` type deduction in `tests/x64_clocked_timing.cpp`.
2. **Portable SDL2 Display, Audio & GameController backends (`src/display/display_sdl.cpp`, `src/audio/audio_sdl.cpp`):**
   * Implemented the `Display` contract (`src/display/display.h`) via `SDL_Window`, `SDL_Renderer` (VSync hardware with automatic `SDL_RENDERER_SOFTWARE` fallback), `SDL_PIXELFORMAT_RGB888` streaming texture, window/aspect/integer-scaling layout, `F5`/`F6`/`F7`/`F11`/`Alt+Enter`/`Esc` hotkeys, keyboard bindings matching Windows, and `SDL_GameController` D-pad/analog-stick/trigger polling.
   * Implemented the `Audio` contract (`src/audio/audio.h`) via `SDL_OpenAudioDevice` / `SDL_QueueAudio`, sharing the existing `AudioResampler`, preroll threshold, volume scaling, and non-blocking overflow drop behavior (`kMaxQueuedFrames = 12288`).
3. **CMake & ARM64 cross-compilation support (`jit/CMakeLists.txt`, `CMakeLists.txt`, `cmake/aarch64-linux-gnu.cmake`):**
   * Updated `jit/CMakeLists.txt` so Windows targets link legacy Win7-compatible Win32/dynamic-DXGI libraries (`_WIN32_WINNT=0x0601`) and non-Windows x86-64 hosts link `SDL2::SDL2` + `Threads::Threads` + `-static-libgcc -static-libstdc++` (`OCEANBLAST_STATIC_STDLIB=ON`).
   * Added top-level `CMakeLists.txt` and `cmake/aarch64-linux-gnu.cmake` (with automatic `qemu-aarch64-static` test runner support) to build the portable ARM920T + FPA11 interpreter player (`oceanblast`) and 19 portable CTest suites on `aarch64` and non-JIT hosts.

## Correctness and parity verification

| Target & Environment | Suite / Run | Result |
| --- | --- | --- |
| **Windows 11 x64 (`w64devkit` GCC 16.2)** | `ctest --test-dir build/jit --output-on-failure` | **13/13 passed** (`15.02 s`) |
| **Linux x86-64 JIT (Ubuntu 22.04 LTS, GLIBC 2.35, GCC 11.4, WSL 1)** | `ctest --test-dir build/linux-x64 --output-on-failure` | **13/13 passed** (`65.83 s`) |
| **Linux x86-64 JIT + SDL2 dummy driver (`Chefs`, 200k GUI+sound steps)** | `--engine jit --gui --sound --steps 200000 --exit-on-limit` (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) | Exit `0`; SDL2 `720x480` window and `22050 Hz` stereo audio initialized |
| **Linux x86-64 binary forward compatibility (Ubuntu 24.04.5 LTS, GLIBC 2.39, `libsdl2-2.0-0`)** | `Chefs` 10M steps (`--engine jit`) | Exit `0`; CPU `33e7097e...`, SDRAM `a29f3c57...` (exact Windows match) |
| **Linux ARM64 (`aarch64-linux-gnu-g++` 11.4, multiarch `libsdl2-dev:arm64`) via `qemu-aarch64-static`** | `ctest --test-dir build/linux-arm64 --output-on-failure` | **19/19 passed** (`15.39 s`) |
| **Linux ARM64 packaged `oceanblast` via `qemu-aarch64-static`** | `Chefs` 10M steps | Exit `0`; CPU `33e7097e...`, SDRAM `a29f3c57...` (exact Windows match) |

### Headless Linux x86-64 JIT benchmarks (Ubuntu 22.04 WSL 1)

| Workload | Steps | Wall seconds | Native coverage | Compilations / Cache hits | Canonical CPU & SDRAM SHA-256 vs Windows ([116](116_page_tlb_generation_and_flat_mmio.md)) |
| --- | ---: | ---: | --- | --- | --- |
| **Wade Hixton's Counter Punch** | 2,400,000,000 | `21.748 s` | `2,156,965,208` (`89.87%`) | `8,008` / `529,199,722` | `da648c0e...` / `5fe4a1db...` (**bit-identical**) |
| **Superstar Chefs** | 600,000,000 | `5.420 s` | `442,608,852` (`73.77%`) | `6,454` / `87,389,730` | `137783ec...` / `29c365d6...` (**bit-identical**) |

## Packaged `v0.4.0` release artifacts (`bin/`, untracked)

* `oceanblast-v0.4.0-windows7-11-x64.zip` (`oceanblast.exe` + `assets/digiblast-skin.png`, SHA-256 `07e813db4002af4ca8eb294038b867f8be23af902e8837d9b0ad3ea0738dcb74`)
* `oceanblast-v0.4.0-windows7-11-x86.zip` (`oceanblast-x86.exe` + `assets/digiblast-skin.png`, SHA-256 `ccde92adcda1425b53a6c0d78062f5fae6858738e6140a51a45bb593b67dcd55`)
* `oceanblast-v0.4.0-linux-x64.tar.gz` (stripped x86-64 ELF, SHA-256 `7cfe968184a028ed07eecdcd38173ad960c0f4fcd19396584074565098c20f2d`)
* `oceanblast-v0.4.0-linux-arm64.tar.gz` (stripped aarch64 ELF, SHA-256 `5899cd7725686ec0699458e8dd523f8403503f34980d96198f8370e89c8ef793`)

## Scope and limitations

Linux tests ran inside Ubuntu 22.04.5 LTS and Ubuntu 24.04.5 LTS on Windows Subsystem for Linux 1 (`4.4.0-22621-Microsoft`), and ARM64 binaries were executed through `qemu-aarch64-static` user-mode emulation. Interactive X11/Wayland window output, hardware ALSA/PulseAudio playback, physical Linux GameController input, and native ARM64 silicon runtime performance remain unverified on dedicated hardware. Linux distributions require the `libSDL2-2.0.so.0` runtime package (`libsdl2-2.0-0` on Debian/Ubuntu). See [validation manifest](validation/2026-11-16_linux_x64_jit_and_arm64_sdl2.json).
