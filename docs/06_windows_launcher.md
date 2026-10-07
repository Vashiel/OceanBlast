# Windows launcher

2026-10-07. Double-click `bin/oceanblast.exe` without arguments to open the German launcher. Select a cartridge with **ROM laden**, choose 2x/3x/4x scaling, optionally enable sound, then click **Spiel starten**. **Datei** includes ROM selection and exit; **Hilfe / Steuerung** lists the keyboard controls. The game uses its own display window; click that window for input.

The launcher starts the same executable as a child process with `--gui --scale N` and optional `--sound`. Only one session is allowed at a time. **Spiel beenden** requests normal window closure; after process exit, Start becomes available again. Closing the launcher during a session first requests game closure; close it again after the session ends. ROM selection does not modify the cartridge. No downloads or external dependencies are needed.

Logs are written to `bin/sessions/session.log` and replaced at each launch. Existing emulator diagnostic dumps are confined to that working directory, which is ignored through `bin/`. Preserve a session log before starting another run if it is needed for comparison. Paths with spaces are quoted. Non-ANSI Windows filenames are not yet guaranteed by the emulator's existing narrow-character cartridge loader.

Implementation: `src/display/launcher.h`, no-argument dispatch in `src/main.cpp`, and `-lcomdlg32` in the Makefile. Existing command-line invocation remains available. Audio now initializes only when `--sound` is explicitly selected, so the GUI checkbox can actually disable it.

Validation: optimized Windows build with `-Wall -Wextra` passed. The launcher opened and exposed ROM selection, scale selector, sound checkbox, Start/Stop and help/menu controls through Windows accessibility. A local ROM path was entered; Start opened the child display and disabled duplicate starts. Stop requested its closure. Sustained gameplay and audio acceptance remain separate from launcher validation.

The final visual check confirmed normal session exit, the stopped status message and re-enabled Start button. The tested launcher remains open locally.
