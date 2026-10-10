# Photographed Cartridge Hardware

## Evidence and Identification

Three supplied photographs show cartridge boards; a fourth shows the right-hand portion of an opened console. The reported cartridge group contains a SpongeBob/Atari combination, Gormiti, and an MP3-player module. The USB-equipped board is provisionally associated with the MP3-player module. The two other boards are the reported game cartridges, but their individual assignments and original publication links are not established. Board appearance alone is insufficient to assign a game title, language, or storage capacity to a particular dump.

| Photograph | Direct observations | Identification limits |
| --- | --- | --- |
| A | U1 marked `SAMSUNG`, `K9F1G08U0M`, `PCB0`; `0003.38-3L-PCB1` board marking; Grey Innovation branding; populated J2 USB connector; U2 footprint unpopulated on the visible side | USB connector suggests a writable/storage accessory, but does not establish a particular MP3-player model or advertised capacity |
| B | Same readable board-family marking; Hawkeye Global branding; U1 marked `Mask Memory`; populated eight-pin U2 with a marking consistent with `24C08AN`; J2 connector footprint unpopulated | Exact U1 ordering code and cartridge title are not reliably readable |
| C | Same readable board-family marking; Grey Innovation branding; U1 marked `Mask Memory`; populated eight-pin U2; J2 connector footprint unpopulated | Exact U1/U2 ordering codes and cartridge title remain unconfirmed |
| D | Partial console board with display, button assembly, wire harness, inductors, diodes and capacitors; eight-pin IC near U4 marked `34063`; a coil appears detached or damaged | Consistent with a power-converter region; insufficient view to identify the CPU, RAM, audio codec, clock routing or complete circuit |

The populated and unpopulated footprints establish board assembly differences. An absent component on the photographed side does not establish the absence of equivalent storage elsewhere in the cartridge or console. Branding and printed numbers are observations, not a complete revision chronology.

## Samsung Large-page NAND

Samsung's [K9F1GXXX0M manufacturer datasheet](https://dtsheet.com/doc/304494/samsung-k9f1g08q0m-pcb0), including `K9F1G08U0M-PCB0`, specifies an 8-bit NAND device with 128 MiB data capacity, 2,048 data bytes plus 64 spare bytes per page, and 64 pages per erase block. A full device therefore contains:

| Representation | Size |
| --- | ---: |
| Data only | 134,217,728 bytes |
| Data and spare area | 138,412,032 bytes |
| Physical page | 2,112 bytes |
| Erase block, data only | 131,072 bytes |

This is evidence that the photographed cartridge hardware includes large-page NAND. It does not establish the geometry of any unpaired dump or the capacity of every MP3-player cartridge. The unverified `Mask Memory` components must not be assigned this Samsung geometry merely because they occupy a related board layout.

OceanBlast currently interprets cartridge data as 512-byte data pages, optionally with 16-byte OOB, and synthesizes Toshiba-family IDs. It does not implement this photographed Samsung device's native large-page command/address behavior. File-size divisibility cannot identify the geometry: 2,112 is also four times 528. A large-page raw image can therefore pass the current size test while being interpreted incorrectly. Supporting this device requires explicit, verified geometry, matching ID responses, command/address sequencing, spare-area handling and boot behavior; changing the page-size constant alone is insufficient.

## Cartridge EEPROM

Photograph B's U2 marking is consistent with an Atmel `24C08AN`, which belongs to the 8-Kbit serial EEPROM family. The [Atmel AT24C02A/04A/08A/16A datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/doc0976.pdf) specifies 1,024 bytes for the 24C08A and bank-select bits in the I2C address. The suffix reading remains a photographic identification, not an electrical measurement.

The existing [EEPROM model](16_i2c_eeprom_and_player_startup.md) exposes 2,048 bytes through the guest-supported 24C16 probe path. That behavior restored tested startup sequences; it is not proof that every physical cartridge has a 24C16. Cartridge-local EEPROM and console device settings may require distinct models. Verify wiring, slave-address behavior, capacity and the guest-selected probe path before altering the currently working default.

## Preservation and Further Verification

For each hardware sample, useful follow-up evidence includes a label/board pairing, photographs of both board sides, readable chip codes, NAND READ-ID bytes, a complete dump retaining spare bytes, and any separate EEPROM image. This would connect physical geometry to the existing [dump integrity records](14_rom_integrity.md). Photographs alone do not diagnose corrupted ROM contents, software timing, missing VSync or audio distortion.

Image contents are not redistributed here. SHA-256 identities refer to the supplied originals and allow later source attribution without exposing local storage paths:

| Photograph | SHA-256 |
| --- | --- |
| A | `d19c6d4a65f677663cfd96eab1b70dd1a77efd940d8624228a6d9028724086c3` |
| B | `eb2d68e9822a7d612071bb8dca279f41f5f3292b42e4588f8e92614050e5485f` |
| C | `06d5d620e0529536589bd97a66e502c3e1fbb5cfaf0b45be4964d97a3fb258da` |
| D | `e2fa601bc5e931ffad6564370c80cc689d0c33a65f1d13826c0155d15d0d31c9` |
