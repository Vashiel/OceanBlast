# MP3 Cartridge Firmware Probe

## Module Identity and Dump Size

The supplied dump is named `MP3 player 256MB.bin`. Its accompanying README identifies SKU `A1007A`, release `v1.00.00`, build date September 1, 2006, and description `256MB MP3 Cartridge`. The advertised unit is MB, not GB.

The raw image contains 17,301,504 bytes: 32,768 pages of 512 data bytes plus 16 spare bytes, representing 16 MiB of data and 512 KiB of spare bytes. This is substantially smaller than the module's advertised capacity. The image size alone cannot determine whether it represents firmware storage, a partial image, or a different storage layout. No complete 256-MB music store has been established. The earlier [checksum audit](14_rom_integrity.md) found no MP3 entry in the inspected MAME software list; that absence is not evidence of corruption.

## Bounded Boot Observation

A 600-million-step headless run at CPU ratio 3 with the optimized executable reaches U-Boot, the embedded Linux kernel, a mounted SquashFS root, and a framebuffer splash written by the guest. It does not establish a working MP3-player interface or audio playback.

The emulated NAND identifies as a Toshiba 16-MiB device. The guest reports that its rootfs partition extends beyond this device and truncates it. The data partition begins at `0x01770000`, beyond the exposed NAND capacity, and is disabled. The USB OHCI host-controller reset times out, and driver startup fails. Repeated I2C acknowledgement failures also occur. These are observed limitations of this execution, not proof that the source dump is damaged.

## Firmware Storage Topology

Read-only extraction of the SquashFS at data offset `0x170000` identifies the actual music-storage path. `/linuxrc` does not call the shared startup script's `mountall` function. It instead expects a USB/SCSI whole disk at `/dev/scsi/host0/bus0/target0/lun0/disc` and its first partition at `.../part1`. The player uses `/mnt/mass`; `DIGIBLAST_BACKING_FILE` points to the SCSI disk and `DIGIBLAST_LO_OFFSET` is 512 bytes. The startup script also loads `g_file_storage.ko` with removable storage enabled.

If the SCSI disk is missing, `memoryerror.sh` displays an MP3 storage accessibility error. If the partition is absent or its filesystem check fails, `formatusb.sh` creates a partition table and FAT filesystem on that disk. These scripts were inspected as data and were not executed. The extracted player package contains `digiblastmp3` and Helix MP3 decoder/format plugins. The firmware therefore provides a concrete USB/SCSI dependency independent of the disabled NAND `/data` partition.

| Extracted file | SHA-256 |
| --- | --- |
| `/linuxrc` | `60f4c7a6de6c503254d13680fce84cf0397c5bef49750da24507528b9f0b4335` |
| `/usr/packages/startupscripts/startup.sh` | `4bc433d85cd8c499b46503c3996a42cfec9f04791d4eaa5265a23426ea0ddae5` |

## Emulation Scope

The subsequent [MP3 storage implementation](51_mp3_storage_and_player.md) supplies OHCI enumeration, persistent USB/SCSI storage, a launcher storage manager and bounded original-player playback evidence. The boot limitations above describe the earlier unattached-storage probe. Physical USB gadget sharing and general MP3 decoding compatibility remain outside that evidence.

Preserving the original MP3 interface and playback software is useful independently of game compatibility. This probe identified OHCI enumeration and USB mass-storage transport exposing a persistent disk to the guest SCSI stack as the required implementation boundary, followed by filesystem mounting and player startup. The subsequent implementation supplies that path. Gadget-side sharing and I2C failures require separate validation. A virtual disk must isolate guest formatting from host files.

Enlarging the NAND image to the advertised music capacity does not implement the firmware's USB/SCSI topology. A complete original music-storage image has not been established; the subsequent playback evidence uses a separately reconstructed FAT16 store. Game and RealVideo timing remain independent compatibility work.

```powershell
bin/oceanblast.exe "roms/hardware/MP3 player 256MB/MP3 player 256MB.bin" `
  --steps 600000000 --cpu-steps-per-tick 3
```
