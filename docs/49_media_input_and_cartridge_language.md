# Media Input, Cartridge Language and Image Preservation

## Playback Controls

The launcher exposes Play/Pause, Stop, Previous, Next and held seek commands through the Playback menu. Dedicated Play and Stop inputs drive only `GPG9/EINT17` and `GPG10/EINT18`. Game Start and Select retain their existing additional control-key lines. This separation prevents a video transport action from unintentionally asserting a game control modifier.

| Action | Keyboard | Guest input |
| --- | --- | --- |
| Play/Pause | P, F10, media Play/Pause | GPG9 (`KEY_P`) |
| Stop | Backspace, F9, media Stop | GPG10 (`KEY_S`) |
| Previous/Rewind | B, F8, PageUp, comma, media Previous | GPG0 (`KEY_B`) |
| Next/Forward | N, F12, PageDown, period, media Next | GPG13 (`KEY_N`) |

Menu and media-key inputs use timed pulses: 180 ms for transport and 1,500 ms for held menu seek. Each button has its own expiry; a seek no longer extends an unrelated Play pulse. Focus loss clears temporary inputs. Releasing a pulse preserves another physical key that holds the same emulated button. Skin transport buttons use the dedicated media inputs, and scripted replay accepts all 15 input bits.

Grouped external interrupts are reevaluated after child pending bits, child masks or main source-pending bits change. An unmasked pending child retains its group request until serviced. Interrupt edges follow actual GPIO pin transitions, including shared host aliases, rather than changes to the host button mask alone. This is a grouped-pending correction, not a complete implementation of the S3C2410 EXTINT trigger modes.

## Cartridge Language

The cartridge language setting is persisted independently of the interface language. It is applied to EEPROM byte zero before guest boot, after loading saved EEPROM state. The ten choices are Auto, Ask, seven explicit languages, and Keep Saved EEPROM Value. The Playback language menu restarts a running cartridge; changing the settings combo applies on the next launch.

| Language | EEPROM byte |
| --- | --- |
| English | 1 |
| German | 3 |
| Spanish | 9 |
| Italian | 8 |
| Dutch | 16 |
| French | 2 |
| Turkish | 15 |
| Ask | 255 |
| Keep | Unchanged |

Auto follows the interface language. If the filename contains a recognized explicit language list excluding that language, Auto requests the cartridge's selection menu. Generic tags such as `(M10)` and `(EU)` do not exclude a language. Filename tags are naming hints, not detection of the cartridge's actual language content.

```powershell
bin/oceanblast.exe "path/to/cartridge.bin" --gui --sound --cart-language ask
```

## Presets and Source Images

Video and combined cartridges receive presets only when both raw image size and CRC32 match a known image. Manual CPU ratio and register-clock settings retain precedence. The [local-image checksum audit](validation/2026-10-12_cartridge_preset_audit.csv) records identification independently of compatibility; matching a preset is not proof of a correct dump or working gameplay.

Cartridge loading preserves the supplied NAND bytes. An implicit damaged-page replacement was excluded from the reviewed implementation: two neighboring signatures do not establish that a replacement page belongs to the same image. The international Winx combined cartridge remains a known damaged-image limitation. Any externally repaired variant is a distinct input with its own checksum, not a verified original dump.

## Regression Validation

`make test` and `make test-windows` pass, including CPU/MMU, peripheral and ROM-free input checks. A synthetic damaged-page pattern verifies that loading preserves every NAND byte. Language tests cover explicit choices, saved state, unsupported explicit lists and generic multilingual tags. Windows checks cover all ten persisted language choices, independent pulse expiry, held aliases, focus loss, fullscreen transitions and skin input under both DXGI and GDI.

The Controls illustration remains pixel-identical over 20 repeated repaints; a button press changes the image and release restores it exactly. Live cartridge validation and remaining playback limitations are recorded separately in [Runtime Acceptance](50_runtime_acceptance.md).
