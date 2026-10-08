# Cartridge Dump Integrity & MAME Checksum Validation

Date: 2026-10-08.

## 1. Reference & Method

The reference is MAME's CC0-1.0 [digiBLAST cartridge software list](https://github.com/mamedev/mame/blob/954def46685cd0276671138fbd032036b1a771fb/hash/digiblast_cart.xml), revision `954def46685cd0276671138fbd032036b1a771fb`. The XML SHA-256 is `522e05d8a6c8d07b64709e566cedecfe7a92b706b98f37a3d1d2e1a49efb5f04`.

Each local BIN file and each BIN member of a ZIP archive is compared using **file size, CRC32 and SHA-1 together**. ZIP members are streamed without extraction or modification. Hashes cover the original dump, including its 16 OOB bytes per 512-byte NAND data page. Comparing an OOB-stripped image against a raw-dump hash would be invalid.

The [complete checksum report](validation/2026-10-08_mame_checksums.csv) uses repository-relative filenames and records the software-list identifier, dump status and support status.

## 2. Results

| Input group | Checked images | Exact MAME matches | No catalog match |
| --- | ---: | ---: | ---: |
| Game cartridges | 11 | 10 | 1 |
| Combined video/game cartridges | 3 | 3 | 0 |
| Video-only ZIP members | 9 | 7 | 2 |
| MP3 hardware ZIP member | 1 | 0 | 1 |
| `test.bin` | 1 | 1 | 0 |
| **Total** | **25** | **21** | **4** |

`test.bin` duplicates the Rayman 3 dump; there are 24 distinct images. No checksum mismatch against an identified catalog entry was found.

The following variants have no entry in the checked catalog. This is **not** evidence that their dumps are corrupt:

| Local cartridge | CRC32 |
| --- | --- |
| Gormiti Agguato nella Valle [G] (IT) | `ddeec7ea` |
| Gormiti Dalle Origini all'Eclissi [V] (IT) | `8bf52908` |
| Winx Club 1 [V] (NL FR EN TR) | `ee696172` |
| MP3 player 256MB | `b048a789` |

Relevant exact matches:

| Cartridge | MAME identifier | CRC32 |
| --- | --- | --- |
| Crazy Jack [G] (EN) | `crzyjack` | `9bce041a` |
| Superstar Chefs [G] (EN) | `sschefs` | `93209877` |
| Sponge Bob + 5 Atari Games [V+G] (IT ES) | `sbatari` | `69ab74c0` |
| Winx Club + 5 Atari Games [V+G] (IT ES) | `wxatari` | `6b5ca82e` |
| Winx Club + 5 Atari Games [V+G] (NL FR DE TR) | `wxatarib` | `eb1d8fda` |

All matching software-list entries have `supported="no"`. The catalog also notes blank ECC data. A matching hash establishes identity with the reference dump; it does not certify complete cartridge contents, valid compressed files or successful emulation. The XML does not mark the international Winx dump as `baddump`.

## 3. Invalid Compressed Data in the International Winx Reference

The international Winx combined cartridge has SHA-1 `f192ffe38eadcc8e3b9b59db76379806e400892f`, matching MAME exactly. Its rootfs begins at NAND **data-stream** offset `0x170000` after removing OOB bytes.

Linux reports two SquashFS page-read failures. Independent host-side zlib checks reproduce both failures directly from the unchanged ROM:

| Rootfs-relative block offset | Compressed size | NAND data address | Independent result |
| --- | --- | --- | --- |
| `0x1bfbf56` | `0x6c28` | `0x1d6bf56` | Incomplete/truncated zlib stream (`-5`) |
| `0x1c02b7e` | `0x7c5c` | `0x1d72b7e` | Invalid zlib header (`-3`) |

Both ranges intersect the entirely erased 512-byte data page at `0x1d72a00`, raw-image offset `0x1e5e350`. That page's 16 OOB bytes are also all `0xff`; no stored ECC is available to reconstruct its contents. Generating ECC for the erased bytes cannot recover missing original data.

The launcher trace calls unmapped code at `0x41cb0`, then encounters the SquashFS failures. Subsequent execution traverses zero-filled code and faults at `0x580b0` while reading address `0x5`. These observations connect the launcher failure to an unsuccessful executable-page read. They do not identify every affected file or prove that this is the only compatibility issue in this cartridge.

The invalid block contents already exist in the checksum-matching reference. Emulator instruction fixes cannot recreate those bytes. Further work requires an independently verified cartridge dump, or independent evidence explaining how the original hardware supplies the missing page. Preserve the source image and compare page/OOB data before changing the NAND model. Do not patch guest instructions or substitute arbitrary bytes to suppress the crash.

## 4. Reproduction

Download only the public software-list XML, then run from the repository root:

```powershell
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/mamedev/mame/954def46685cd0276671138fbd032036b1a771fb/hash/digiblast_cart.xml' -OutFile digiblast_cart.xml
python tools/verify_roms.py --catalog digiblast_cart.xml --roms roms --output checksum-results-new.csv
python tools/check_nand_zlib.py 'roms/video_games/Winx Club + 5 Atari Games [V+G] (NL FR DE TR).bin' `
  --raw-528 --base 0x170000 --block 0x1bfbf56:0x6c28 --block 0x1c02b7e:0x7c5c
```

The checksum tool refuses to replace an existing report. `MATCH` requires all three identifiers; `NO_CATALOG_MATCH` leaves integrity unclassified. `CHECKSUM_MISMATCH` means a catalog filename matches but its identifiers differ, which still requires checking for an unlisted revision. The zlib checker exits with code 1 for the two expected failures above. It checks only the specified blocks, not the entire filesystem.
