# Automatic Cartridge Settings

## Selection

The Windows launcher defaults to **Plain Window**, enabled audio, and **Automatic ROM settings**. Runtime cartridge profiles are selected from the loaded NAND image's CRC32 and byte count. File names do not participate, so renaming a dump does not change its settings. `session.log` records the selected profile and CPU ratio; the game title shows the active ratio. Snapshot state files include the profile name.

| Cartridge | NAND bytes | CRC32 | CPU steps per peripheral tick |
| --- | --- | --- | --- |
| Pitfall The Lost Expedition, English | 17,301,504 | `d1d6118f` | 2 |
| Superstar Chefs, English | 17,301,504 | `93209877` | 2 |
| Other content | Any | Other | 1 |

The 2x presets reflect listening reports that these configurations sound better than 1x or experimental register-clock execution. They are compatibility settings, not proof of original hardware speed, exact pitch, or complete gameplay accuracy. Superstar Chefs can still slow down in active scenes. Untested revisions and corrupted images receive the standard fallback rather than inheriting settings from their filenames.

The existing automatic display decoder and complete-video-frame capture remain independent of CPU presets. Crazy Jack's specific display-layout transition is still selected by its verified cartridge identity. A CPU profile does not force LCD format, stride, or video playback speed.

## Overrides

`--preset auto` is the runtime default. `--preset off` selects the standard 1x ratio unless an explicit ratio is supplied. `--cpu-steps-per-tick N` overrides the profile, including an explicit value of 1. The launcher exposes Standard, 2x and 4x manual choices alongside automatic settings.

`--timing auto` retains its existing experimental register-clock meaning. It bypasses cartridge CPU presets and still rejects incompatible explicit ratios or nonzero MIPS limits. It is separate from **Automatic ROM settings**. The default GUI limiter is 20 MIPS multiplied by the selected ratio, therefore 40 MIPS for a 2x profile. An explicit `--clock-mips` value is preserved.

Profiles do not dynamically increase CPU work in response to low host FPS or an empty audio queue. Such symptoms can also originate in guest workload, DMA, or incomplete hardware timing; changing the ratio blindly would obscure the cause. New presets require cartridge identity and title-specific validation.

## Validation

ROM-free selection checks cover known checksums, unknown content, incorrect byte counts, disabled profiles, explicit 1x and 4x overrides, and experimental register timing. Bounded cartridge comparisons check automatic and explicit 2x execution using the same input script and instruction budget. These checks establish selection and execution equivalence, not subjective audio quality or full compatibility.

The regression suite passes 257 checks and the Windows presentation/launcher suite passes 20 checks. Five additional bounded CLI starts verify automatic selection, profile disablement, an explicit 1x override, experimental register timing, and the unknown-cartridge fallback. Both cartridges complete 2,400,000,001 instructions in automatic and explicit 2x runs. Their final SDRAM hashes and framebuffer snapshots at instruction 2,400,000,000 match within each pair. See [comparison results](validation/2026-10-09_automatic_cartridge_settings.csv).

The installed executable uses an O3 unity build with profile-guided optimization, trained with 2.4 billion instructions per cartridge on Pitfall, Superstar Chefs, and Wade Hixton at 2x, using `tests/chefs_ratio4_confirm.txt`. Executable SHA256: `526e09c1aa4d0446e25b2e237cbcfb95284b3b491b5bb618b1b803f8d384021c`. Audio quality and sustained host performance require separate listening and gameplay validation.
