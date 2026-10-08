# Video-and-Game Cartridge Startup Compatibility

Date: 2026-10-08. Baseline: `29391853`.

Scope: Linux boot, launcher startup and active framebuffer output for the three extracted `.bin` cartridges in `roms/video_games`. Movie playback requires successful launcher and player startup; a loading splash is not playback validation.

The [UART0 interrupt correction](15_uart_video_startup.md) subsequently restores the Italian/Spanish language menus. The table below describes the pre-correction baseline.

Each run uses 1.2 billion instructions, `--debug`, snapshots every 100 million instructions and the generic `tests/start_buttons.txt` replay. The emulator runs headlessly without host audio; this does not validate movie playback, controls or sound.

| Cartridge | Final active frame | Guest observation |
| --- | --- | --- |
| Sponge Bob + 5 Atari Games [V+G] (IT ES) | Completely black, 0/76,800 nonzero bytes | Linux boots; launcher/language applications execute; EEPROM-open error present |
| Winx Club + 5 Atari Games [V+G] (IT ES) | Completely black, 0/76,800 nonzero bytes | Linux boots; launcher/language applications execute; EEPROM-open error present |
| Winx Club + 5 Atari Games [V+G] (NL FR DE TR) | Retained digiBLAST loading splash, 61,369/76,800 nonzero bytes | Repeated guest `Segmentation fault` messages following `digiblastlauncher` execution |

All runs finish their instruction budgets without a detected kernel panic or subprocess timeout. A zero host exit code does not imply a successful guest application: the international Winx cartridge visibly crashes its guest launcher repeatedly.

The logs contain `/usr/packages/Launcher/bin/digiblastlauncher` and `/usr/packages/Language/bin/digiblastlanguage`. No RealPlayer execution or opened movie file was observed. The user identifies RealPlayer as the original video player; these runs do not reach a verified player/decoder path. Diagnose launcher startup and guest faults before attributing the black output to a codec or video-output device.

The nine video-only ZIP archives under `roms/videos` were inventoried but not extracted or tested in this probe. Their playback status remains unknown.

The [validation CSV](validation/2026-10-08_video_games.csv) records cartridge hashes, execution budgets and frame observations. Proprietary images, ROMs and full dumps are excluded from source publication.

Reproduction from the repository root, using a new output directory:

```powershell
python tools/audit_games.py --exe bin/oceanblast.exe --roms roms/video_games `
  --output C:/temp/oceanblast-video-new --jobs 2 --timeout 180 `
  --steps 1200000000 --snapshots 100000000 --input-script tests/start_buttons.txt --debug
```
