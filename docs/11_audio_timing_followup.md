# Audio timing and unresolved blank frames

Date: 2026-10-08. Baseline: `1c74bb3`. Report: some cartridges still display no gameplay image; others have stutter, changing playback speed or audio/video drift. The English launcher and the existing RGB565 support are preserved.

## Confirmed audio issues

The host resampler only implemented 22,050-to-44,100 and 44,100-to-22,050 Hz conversions. The bus can report 8,000, 11,025 and other rates. Those inputs previously passed unchanged to a device opened at a different rate, causing incorrect speed and pitch. The replacement handles arbitrary positive rates with stereo/mono frame interpolation, preserving phase and the last frame between DMA chunks. It is a basic linear resampler; it is not a high-quality anti-aliasing filter for downsampling. A rate change resets the interpolation state.

Audio queues remain nonblocking: a full waveOut queue drops remaining samples. A new cumulative dropped-sample counter appears in the game title and profile CSV, alongside the inferred input sample rate. Submission failures also contribute to the counter. It counts interleaved output samples, not stereo frames. A zero count does not prove absence of host underruns or correct synchronization.

Small DMA fragments formerly occupied an entire waveOut header each. With only 16 headers, the queue could become full despite containing little audio. The backend now accumulates fragments into 4,096-byte submissions and retains the partial tail for the next callback. This introduces up to one buffer of collection latency (about 46 ms at 22,050 Hz stereo). The existing maximum queue capacity remains unchanged.

## DMA current-position registers

Crazy Jack repeatedly reported `s3c24xx_snd_pcm_pointer: 0,0 (cfcb0000)` and `res cfcb0000 >= 800`: reads of DCSRC2 returned zero. ALSA calculates playback position from this register. DCSRC2 and DCDST2 now expose the latched transfer addresses plus item-aligned progress; fixed-address endpoints remain fixed. DSTAT2 decreases with progress instead of holding the full count until completion. Updating the next source while a transfer is active does not alter the current source. Completion exposes end addresses, and the existing autoreload path latches the next buffer before its interrupt.

## Emulated time

DMA uses 20 million instructions per emulated second; previously, GUI execution ran as fast as the host allowed. DMA arrival times therefore varied with host CPU speed. The GUI now defaults to a wall-clock limit of 20 MIPS, matching that existing timing model. `--clock-mips 0` disables the limit for performance investigation; another positive value changes the limit. Headless execution remains unrestricted. Pause resets the limiter anchor. A lag greater than 250 ms resets it too, preventing a long burst of catch-up after host stalls.

This is a practical pacing model, not accurate ARM920T instruction-cycle timing. A host unable to sustain the target runs slow and may still starve audio. The limiter cannot solve that. Timer 4, IIS clock detection, PCM stereo/16-bit assumptions and host buffering still need per-cartridge validation. DMA interval division now uses a 64-bit numerator before division, avoiding truncation of cycles per byte.

## Verification

The optimized Windows build completed with `-Wall -Wextra`. Five ROM-free resampler cases passed: 8,000→22,050, 11,025→22,050, 22,050→44,100, 44,100→22,050 and 32,000→22,050 Hz. Each verifies output duration and consistency between one large input and multiple DMA-sized chunks. The suite is included in `make test`.

The rebuilt executable is installed locally as `bin/oceanblast.exe`, so the existing launcher uses these changes. The previous binary is preserved at `C:/temp/oceanblast-artifacts/`. The source, tests and documentation are included together in the follow-up commit. Executables, ROMs and full memory dumps are local artifacts, not part of that source publication; other users need to build the executable from source.

Runtime artifacts are kept locally in `C:/temp/oceanblast-artifacts/`. The short Spider-Man run checks the limiter and new CSV columns; boot-only measurements do not establish audible gameplay acceptance.

Artifact mapping (folder names alone do not identify the tested cartridge):

- Root `performance.csv`: initial Spider-Man GUI/audio run, 200 million instructions, before fragment aggregation and DMA position support. At 120–180 million instructions, cumulative dropped output samples increased from 3,082 to 5,138.
- `crazy-jack/performance.csv` and `audio-check.log`: despite the folder name, these are the later **Spider-Man** GUI/audio run at the same 200-million-instruction limit. It maintained approximately 20 MIPS and reported zero dropped output samples. The inferred 87,890 Hz rate in this boot interval still requires investigation; this is not a gameplay audio assessment.
- Root `run.log` and framebuffer dumps: Crazy Jack, 1.2 billion instructions, before DMA current-position support. This later run replaced the root boot log; it does not correspond to the root performance CSV.
- `crazy-jack-fixed/run.log` and dumps: Crazy Jack, 1.2 billion instructions, with DMA position support. The invalid ALSA pointer errors are absent, but the framebuffer remains almost empty.
- `crazy-jack-debug/run.log`: the same Crazy Jack instruction limit with syscall/scheduler diagnostics.

The CPU/DMA and input suites also pass. Four new DMA checks cover the initial source, latched-source progress/remaining count, fixed destination and completion address. Snapshot text now includes audio rate, dropped samples, current DMA source/count and I2C controller state.

## Blank images

The user identified `Crazy Jack [G] (EN).bin`. At 1.2 billion instructions, both comparison runs have LCDSADDR1 selecting `0x302a0000`, 16-bit mode, and only 283 nonzero bytes of its 76,800-byte active buffer. Fixing the DMA pointer errors did not restore gameplay images. A near-empty guest framebuffer needs investigation of the guest game/driver path; RGB565 decoding alone does not establish the cause.

The debug run loads the game assets, attempts to open `/dev/misc/eeprom` twice and logs `No I2C adapter attached`. It subsequently calls signal-mask/signal-suspend syscalls and spends the end of the run in kernel idle code. The I2C model currently NACKs every transfer. This is an incomplete peripheral model and a candidate for investigation, not a demonstrated cause of the blank image. Do not ACK arbitrary device addresses or invent EEPROM contents to hide the error. Identify the cartridge's expected board devices, address/protocol and initialization data before implementing them. Blank-frame compatibility is still unresolved.

Reproduce from a separate working directory so dumps do not overwrite other runs:

```powershell
& 'C:/path/to/OceanBlast/bin/oceanblast.exe' `
  'C:/path/to/Crazy Jack [G] (EN).bin' --steps 1200000000 --debug *> run.log
```

For GUI/audio comparisons, use `--gui --sound --profile`; the default GUI limit is 20 MIPS. `--clock-mips 0` is an unpaced diagnostic, not a synchronization fix. Compare `changed_fps` (observed framebuffer changes) separately from `present_fps` (host presentations).

The complete-pitch and uninterrupted-playback claims in `09_audio_sample_rate_fix.md` are superseded by these observed limitations. No acceptance claim is made for all cartridges.
