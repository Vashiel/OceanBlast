# Video-and-game cartridge probe

Date: 2026-10-08. Executable source baseline: `41cf707`. The previous Games-folder matrix did not cover `roms/video_games`. This follow-up tests all three extracted `.bin` cartridges in that directory.

Each run uses 1.2 billion instructions, `--debug`, snapshots every 100 million instructions and the generic `tests/start_buttons.txt` replay. The emulator runs headlessly without host audio; this does not validate movie playback, controls or sound.

| Cartridge | Final active frame | Guest observation |
| --- | --- | --- |
| Sponge Bob + 5 Atari Games [V+G] (IT ES) | Completely black, 0/76,800 nonzero bytes | Linux boots; launcher/language applications execute; EEPROM-open error present |
| Winx Club + 5 Atari Games [V+G] (IT ES) | Completely black, 0/76,800 nonzero bytes | Linux boots; launcher/language applications execute; EEPROM-open error present |
| Winx Club + 5 Atari Games [V+G] (NL FR DE TR) | Retained digiBLAST loading splash, 61,369/76,800 nonzero bytes | Repeated guest `Segmentation fault` messages following `digiblastlauncher` execution |

All runs finish their instruction budgets without a detected kernel panic or subprocess timeout. A zero host exit code does not imply a successful guest application: the international Winx cartridge visibly crashes its guest launcher repeatedly.

The logs contain `/usr/packages/Launcher/bin/digiblastlauncher` and `/usr/packages/Language/bin/digiblastlanguage`. No RealPlayer execution or opened movie file was observed. The user identifies RealPlayer as the original video player; these runs do not reach a verified player/decoder path. Diagnose launcher startup and guest faults before attributing the black output to a codec or video-output device.

The nine video-only ZIP archives under `roms/videos` were inventoried but not extracted or tested in this probe. Their playback status remains unknown.

Local logs, snapshots and memory dumps: `C:/temp/oceanblast-artifacts/`. The summary is published in [the validation CSV](validation/2026-10-08_video_games.csv); cartridge hashes identify the exact local inputs. Proprietary images, ROMs and full dumps remain local.

Reproduction from the repository root, using a new output directory:

```powershell
python tools/audit_games.py --exe bin/oceanblast.exe --roms roms/video_games `
  --output C:/temp/oceanblast-video-new --jobs 2 --timeout 180 `
  --steps 1200000000 --snapshots 100000000 --input-script tests/start_buttons.txt --debug
```
