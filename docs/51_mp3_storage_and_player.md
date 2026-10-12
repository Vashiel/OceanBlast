# MP3 Storage and Original Player

## Launcher Workflow

The recognized `MP3 player 256MB` firmware cartridge requires a separate writable music store. On first launch, OceanBlast asks **“The MP3 cartridge needs a 256 MB storage image. Create it now?”** Choosing **No** cancels that launch. Choosing **Yes** creates `sessions/mp3-storage.img` beside the installed executable, displays **“Storage created in:”** with its full path, and then opens the MP3 file picker. Canceling the picker keeps an empty, reusable store; it does not cancel storage creation.

Subsequent launches attach the same image without another creation prompt. Recognition uses the original dump's exact byte count and CRC-32 (`17,301,504`, `b048a789`), rather than its filename. Other cartridges are not automatically given a USB disk.

**Settings → MP3 Storage...** opens the music directory and free-space display. **Add MP3 files...** accepts multiple files. Selecting a track fills the filename field; **Rename** preserves its audio data, and **Delete** asks for confirmation. Names retain the `.mp3` extension. File selection copies data into the virtual store and leaves the computer's source files intact.

**Remove Storage...** deletes the entire music-storage image after a separate confirmation, with **No** selected by default. It leaves the original MP3 source files and cartridge dump intact. The manager closes after successful removal; the next MP3 launch offers to create a new store.

The emulator must be stopped before editing storage. The manager refuses an image held open by a writer. Each operation edits a temporary image copy and replaces the original only after successful writes. Failed reads, duplicate names, insufficient capacity and invalid names therefore preserve the previous store. An existing creation destination or transaction file is not overwritten. The launcher does not expose physical disks as USB storage.

The image contains 256 MiB (268,435,456 bytes); the interface uses the cartridge's advertised **256 MB** wording. Partition, FAT and directory overhead reduce usable music capacity. The original firmware dump remains unchanged. A pre-existing unsupported or inconsistent image is reported as an error rather than silently recreated.

## Emulated Storage Path

The MP3 firmware's Linux USB host driver enumerates an emulated full-speed mass-storage device through OHCI. Bulk-Only Transport provides the SCSI inquiry, capacity, sense and block read/write operations required by this cartridge. Guest writes persist in the explicitly attached image. The launcher initializes an MBR partition and FAT16 filesystem, including VFAT long filenames, which the original startup scripts can mount at `/mnt/mass`.

USB host emulation is enabled only when a storage image is attached. The other cartridges retain their previous USB host register behavior. OHCI descriptor access is bounded to guest SDRAM, and storage transfers are bounded to the image's block range.

This is a focused implementation for the MP3 cartridge. USB gadget mode, access to a physical cartridge over its connector, isochronous devices and general USB peripheral compatibility are not implemented. The PCB photograph shows a USB connector but does not by itself establish the device's internal USB topology; the guest scripts establish the emulated USB/SCSI dependency described in [the firmware analysis](48_mp3_cartridge_firmware_probe.md).

## Playback Evidence and Limits

The original Linux kernel enumerates the device, discovers its 256-MiB SCSI capacity and mounts the prepared partition. The original `digiblastmp3` player reaches its track interface. A generated 20-second, 440-Hz, stereo 128-kbit/s MP3 without ID3 or Xing headers produces a sustained tone in guest IIS PCM capture. A separate MP3 encoded with ID3/Xing headers fails with Helix result `0x80040011`. This difference does not isolate which header or format feature causes the failure; it is not evidence that every tagged MP3 is unsupported.

A subsequent three-billion-step PGO GUI replay attaches an image created and edited by the same FAT16 manager used by the launcher. The guest filesystem check accepts its long filename, `Playback test.mp3`, without reformatting. The original interface displays the title and 20-second duration, with captured progress values of `00:04`, `00:07` and `00:15`. The run terminates normally with no dropped host samples. This verifies the manager-to-player path for that generated file, not general music-library compatibility or audible fidelity.

The v0.3.0-alpha Windows package uses a baseline x86-64 PGO unity build. A three-billion-step GUI replay of the same generated track completes normally at the selected ratio, with no dropped samples. The homepage animation is a 12-second excerpt of its decoded LCD capture, sampled at 10 frames per second without changing playback speed. The storage-manager image shows the regression fixture's test directory. Neither preview establishes general decoder compatibility.

The launcher copies audio unchanged. It does not silently transcode files or claim universal MP3 decoding. Decoder compatibility, playback controls and audible fidelity remain separate acceptance checks. Playback failures should retain the source encoding details and guest log for diagnosis.

`tests/mp3_storage.cpp` checks image creation without overwrite, long filenames, exact imported bytes, rename without changing data allocation, deletion/free-space recovery, transactional rollback and writer exclusion. `tests/mp3_manager_win32.cpp` checks the Yes/No creation dialog, Settings entry, selection, rename, deletion confirmation and return to the launcher. Native file-picker interaction is not automated by that fixture. USB transport and OHCI completion have separate ROM-free coverage in `tests/usb_storage.cpp`.

## Command-Line Use

The launcher requires no Python installation. A separate optional utility can prepare a new image for command-line use:

```powershell
python tools/create_mp3_storage.py build/music.img --music "D:\Music\Track.mp3"
bin/oceanblast.exe "roms/hardware/MP3 player 256MB/MP3 player 256MB.bin" --usb-storage build/music.img --gui --sound
```

The utility creates a new image exclusively and uses short track aliases. The launcher manager preserves long filenames instead. `--usb-storage` requires an existing writable regular file; guest formatting, when requested by the cartridge firmware, affects that image only.

## References

- [Linux OHCI structures](https://github.com/torvalds/linux/blob/v2.6.12/drivers/usb/host/ohci.h), [root hub handling](https://github.com/torvalds/linux/blob/v2.6.12/drivers/usb/host/ohci-hub.c), [S3C2410 interrupt assignments](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/arch-s3c2410/irqs.h), and [USB storage transport](https://github.com/torvalds/linux/blob/v2.6.12/drivers/usb/storage/transport.c).
- [Helix result definitions](https://github.com/muromec/qtopia-ezx/blob/master/src/3rdparty/libraries/helix/src/common/include/hxresult.h): `0x80040011` is shared by missing renderer, file-format and component results; it is not a precise corrupt-file diagnosis.
