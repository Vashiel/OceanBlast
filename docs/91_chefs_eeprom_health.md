# Chefs Immediate Life Loss: Invalid Persisted Difficulty

## Reproduction and cause

The reported Superstar Chefs failure is reproduced with the installed executable and the existing shared `sessions/board.nvram`: the player starts with five lives, quickly enters the death sequence, respawns and loses more lives. Releasing all buttons does not prevent it. Scripted left/right input reaches both SDL's key state and the game's private input mask, excluding a simple stuck-key or focus explanation in this fixture.

The installed executable before this fix has SHA-256 `1b40a54ec29c5d3529834ff2cb0353e3774c0b9ce1e6cb9a3a63732eaf3c92e9`. The affected EEPROM has SHA-256 `dca61f6648053ebb409fbd569a45ec33a0dc245c9169e29aec24706d044c58d8`. Its byte at offset 4 is **80**. Chefs loads it as difficulty; the observed game code initializes player health only for difficulty **0, 1 or 2**. With 80, health remains zero, the death path is entered and a life is deducted after the falling death animation crosses the level boundary. The repeated life loss is a consequence of the invalid setting, not evidence that every level platform lacks collision.

An isolated write trace identifies the first death-state write around instruction 1.20466 billion and the first life decrement around 1.30532 billion. The death-state write is at guest PC `0x24cac`; the life decrement store is at `0x24b4c`. Inspection of the surrounding code shows the health gate and difficulty-specific health initialization. Guest addresses apply to this cartridge and mapped process only. No guest game code is patched.

The launcher previously supplied the same EEPROM path to every cartridge. Consequently cartridge-specific bytes can be interpreted as unrelated settings by another title. The exact writer of the invalid legacy bytes is not established. Earlier local captures labelled as older builds also show immediate life loss, but their labels alone do not establish executable provenance. An FPA-disabled diagnostic reaches only the level-selection menu under its original instruction/input schedule and is inconclusive; it is not a successful gameplay comparison.

## Fix

The launcher now stores a separate 2,048-byte EEPROM under `sessions/nvram/<size-hex>-<crc32>.nvram`. Identity follows cartridge contents, so renaming the ROM preserves its settings. Existing per-cartridge files are reused rather than overwritten.

On first launch, the legacy board image is copied into the cartridge's new file. The old `sessions/board.nvram` remains untouched. For the exact supported Chefs cartridge (size 17,301,504; CRC32 `93209877`), an unsupported difficulty byte is changed to 1 in that new copy. Valid difficulties and all other bytes remain unchanged. No generalized save reset or deletion is performed. For the affected local image, **only byte 4 changes, from 80 to 1**. Explicit command-line `--nvram` paths retain their existing meaning; isolation is launcher policy.

Save preparation fails visibly if a source/destination image has an invalid length or cannot be read/written. New images are written through a temporary file and then renamed. With no legacy image, an erased EEPROM is supplied for the cartridge's normal initialization.

The existing local title-blit implementation is preserved. This fix changes save selection and legacy Chefs settings migration, not its rendering, alpha blend or guest timing. Those earlier local CPU/main/bus edits are separate from the save-isolation source commit.

## Bounded checks

The original 20-minute diagnosis allowance was exceeded. Further runs were stopped, the overrun was reported, and an additional 15-minute block was explicitly approved for the identified death path. The decisive comparison uses the same installed emulator and input sequence with a separate repaired EEPROM: a preliminary copy restores valid settings, and the retained migration changes only the unsupported difficulty. The player then initializes with two health units and retains five lives in the short first-level check. This establishes the immediate-life-loss cause, not complete game or controller acceptance.

`tests/cartridge_nvram.cpp` checks content identity against a known CRC, renamed ROMs, independent writes for different contents, preservation of the legacy image, reuse of existing per-cartridge saves, rejection of a malformed save, selective migration, and preservation of all valid difficulties. The test creates and removes only its own temporary fixtures. It is included in `make test`.

The application is rebuilt using the existing conservative x86-64 profile data. New/changed launcher functions may lack matching profile counters and fall back to ordinary optimization; no retraining or new performance gain is claimed. The executable retains the existing local title-blit changes. Validation records distinguish the old diagnostic executable from the rebuilt candidate and installed copy. ROMs, EEPROM images, RAM dumps and game screenshots remain outside Git.

The rebuilt candidate is checked with the migrated image and a short left/jump replay, stopping at 1.6 billion instructions. The inspected 1.5-billion snapshot shows movement, 100 points and five lives. Final guest state retains five lives and one remaining health unit after a normal hit. The executable is then installed, with the preceding executable backed up locally. This is a short scene/self-test, not a new listening or whole-game acceptance. [The validation record](validation/2026-10-17_chefs_eeprom_fix.json) identifies inputs, settings, binaries and acceptance limits.
