# MP3 Cartridge Firmware Probe

## Module Identity and Dump Size

The supplied dump is named `MP3 player 256MB.bin`. Its accompanying README identifies SKU `A1007A`, release `v1.00.00`, build date September 1, 2006, and description `256MB MP3 Cartridge`. The advertised unit is MB, not GB.

The raw image contains 17,301,504 bytes: 32,768 pages of 512 data bytes plus 16 spare bytes, representing 16 MiB of data and 512 KiB of spare bytes. This is substantially smaller than the module's advertised capacity. The image size alone cannot determine whether it represents firmware storage, a partial image, or a different storage layout. No complete 256-MB music store has been established. The earlier [checksum audit](14_rom_integrity.md) found no MP3 entry in the inspected MAME software list; that absence is not evidence of corruption.

## Bounded Boot Observation

A 600-million-step headless run at CPU ratio 3 with the optimized executable reaches U-Boot, the embedded Linux kernel, a mounted SquashFS root, and a framebuffer splash written by the guest. It does not establish a working MP3-player interface or audio playback.

The emulated NAND identifies as a Toshiba 16-MiB device. The guest reports that its rootfs partition extends beyond this device and truncates it. The data partition begins at `0x01770000`, beyond the exposed NAND capacity, and is disabled. The USB OHCI host-controller reset times out, and driver startup fails. Repeated I2C acknowledgement failures also occur. These are observed limitations of this execution, not proof that the source dump is damaged.

## Emulation Scope

Preserving the original MP3 interface and its playback software is useful independently of game compatibility. A usable implementation needs the original storage topology established, writable persistent music storage, and the peripherals exercised by the firmware. USB transfer support should follow evidence of the relevant host/device path rather than substituting a generic host MP3 player.

The next step is to inspect the firmware's startup scripts, storage driver expectations, and player executable. A virtual music image should only be added after determining how the guest accesses the missing store. Game and RealVideo timing remain independent compatibility work.

```powershell
bin/oceanblast.exe "roms/hardware/MP3 player 256MB/MP3 player 256MB.bin" `
  --steps 600000000 --cpu-steps-per-tick 3
```
