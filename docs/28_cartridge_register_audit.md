# Cartridge Register Audit

## Scope and Reproduction

Eleven game cartridges run for two billion instructions with `tests/start_buttons.txt`. Nine extracted video cartridges run for 2.5 billion instructions with `tests/video_language_confirm.txt`. Each run uses automatic display selection, legacy ratio 1, default volatile device initialization, snapshots every 500 million instructions and guest MMIO counters. No sound device or GUI is used. Two workers per audit provide functional coverage; these runs are not isolated throughput measurements.

Executable SHA-256: `e296a90c1dfb916772c880dd262911fbcf3a5f3bf67d7c701c34116594a4ee59`, including bounded execution and guest-only register profiling. The subsequent Windows host wait change affects GUI pacing and is validated separately in [Windows Host Pacing](27_windows_host_pacing.md).

[Game results](validation/2026-10-09_games_register_audit.csv) and [video results](validation/2026-10-09_videos_register_audit.csv) record cartridge identity, budgets, return status, final CPU address, framebuffer and guest error observations. Elapsed wall time is omitted because some sessions include suspended execution. Cartridge contents, memory dumps, extracted programs and screenshots are not published.

```powershell
python tools/audit_games.py --exe bin/oceanblast.exe --roms roms/games `
  --output build/game-register-audit --steps 2000000000 --jobs 2 `
  --timeout 600 --snapshots 500000000 --mmio-profile `
  --input-script tests/start_buttons.txt
python tools/audit_games.py --exe bin/oceanblast.exe --roms extracted-video-roms `
  --output build/video-register-audit --steps 2500000000 --jobs 2 `
  --timeout 600 --snapshots 500000000 --mmio-profile `
  --input-script tests/video_language_confirm.txt
```

## Game Observations

All eleven runs exit normally without guest segmentation faults or kernel panics. Ten final buffers contain image data. Gormiti Agguato ends with a black buffer in the generic multi-button replay. Its separately established title, menu, character-selection and bracket sequences are recorded in [Timer and Runtime Validation](17_timer_and_runtime_validation.md); a black generic-replay result alone does not mean initial rendering is broken.

Crazy Jack retains automatic game-layout selection. Cuccioli, DigiQUAD, the other two Gormiti games, Pitfall, Rayman 3, Spider-Man, Superstar Chefs and Wade have nonempty final buffers. These observations establish bounded execution and output memory, not sustained gameplay, responsive input or synchronized audio acceptance.

## Video Output and Register Activity

All nine video runs exit normally without guest segmentation faults or kernel panics. Decoded final captures show Gormiti intro/episode imagery, SpongeBob water imagery and Yu-Gi-Oh output. Sonic and both Totally Spies variants remain black or near-black in their content areas. Both video-only Winx variants end with completely zeroed active buffers at this budget. These are distinct from the damaged international Winx game/video combination; this audit does not rediagnose their source data as corrupt.

Sonic and the Totally Spies logs reach the player's playing state. The Netherlands Totally Spies log also completes a pause/resume transition before returning to playing. Player creation and those state transitions therefore do not establish that decoded frames arrive at the display.

[Guest LCD access counts](validation/2026-10-09_video_lcd_access.csv) range from 10 to 14 reads and 41 to 80 writes across each complete video execution. Every video has zero additional LCD reads or writes between two billion instructions and the final 2.5-billion limit. Host display/snapshot inspection is excluded from these counts by the tested register profiler. The observed black output is therefore not accompanied by sustained guest polling of the LCD register block during that final interval. This narrows investigation but does not establish complete LCD emulation or exclude interrupt-related faults.

The remaining targets are decoded-frame delivery, player thread wakeups, audio-clock interaction and the point where each player stops updating its active buffer. Retain media-player state transitions, decoder mappings, guest PC profiles, framebuffer hashes and guest-only register counts when testing those paths. Changing image offsets or guessing a new display format cannot explain an entirely zeroed active framebuffer.
