# Windows Launcher

**CPU Timing Test** defaults to Standard. Experimental 2x/4x options increase CPU work per peripheral tick and automatically raise the GUI instruction limit. Host throughput may be insufficient, causing audio starvation. See [CPU budget and runtime timing](22_cpu_budget_and_runtime_timing.md) before using these diagnostic settings.

2026-10-07. Double-click `bin/oceanblast.exe` without arguments to open the graphical launcher. Select a cartridge with **Browse ROM…**, choose 2x/3x/4x scaling, optionally enable sound, then click **Start Game**. The **File** menu includes ROM selection and exit; **Help / Controls** lists the keyboard controls. The game uses its own display window; click that window for input.

The launcher starts the same executable as a child process with `--gui --scale N` and optional `--sound`. Only one session is allowed at a time. **Stop Game** requests normal window closure; after process exit, Start becomes available again. Closing the launcher during a session first requests game closure; close it again after the session ends. ROM selection does not modify the cartridge. No downloads or external dependencies are needed.

Logs are written to `bin/sessions/session.log` and replaced at each launch. Existing emulator diagnostic dumps are confined to that working directory, which is ignored through `bin/`. Preserve a session log before starting another run if it is needed for comparison. Paths with spaces are quoted. Non-ANSI Windows filenames are not yet guaranteed by the emulator's existing narrow-character cartridge loader.

Implementation: `src/display/launcher.h`, no-argument dispatch in `src/main.cpp`, and `-lcomdlg32` in the Makefile. Existing command-line invocation remains available. Audio now initializes only when `--sound` is explicitly selected, so the GUI checkbox can actually disable it.

Validation: optimized Windows build with `-Wall -Wextra` passed. The launcher opened and exposed ROM selection, scale selector, sound checkbox, Start/Stop and help/menu controls through Windows accessibility. A local ROM path was entered; Start opened the child display and disabled duplicate starts. Stop requested its closure. Sustained gameplay and audio acceptance remain separate from launcher validation.

Validation confirms normal process exit, the stopped status message and re-enabled Start button.

## Device Settings and Display Diagnostics

**Keep Device Settings** is enabled by default. The launcher passes `--nvram` with a shared `bin/sessions/board.nvram` image. EEPROM changes are saved on normal emulator exit; a new image starts erased. Disable the checkbox for a fresh device state without replacing the saved image. This is a device EEPROM, not a game save-state system.

**Display Decoder** defaults to **Automatic**. Recognized cartridge content can select a compatibility decoder when a verified LCD/framebuffer configuration is active. Crazy Jack automatically uses packed RGB444 with 480-byte rows for its game buffer, while boot splash buffers retain native 360-byte rows. **LCD registers (diagnostic)** disables compatibility selection; the explicit RGB444/RGB565 options remain available. Selection changes host interpretation and does not repair the unresolved guest LCD-mode discrepancy. See [automatic display selection](25_automatic_display_selection.md).

The updated launcher exposes both controls through Windows accessibility, starts the selected cartridge with the chosen decoder, and saves a 2048-byte device image on normal exit. English control labels and keyboard shortcuts are retained.
