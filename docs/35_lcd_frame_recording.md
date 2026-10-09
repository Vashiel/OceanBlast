# LCD Frame Recording

`--record-frames <path>` samples the emulator's decoded LCD buffer at 30 frames per second and writes raw RGB24 pixels. The option opens the game display automatically. The capture is 240×160 pixels; it contains the emulated LCD image without the desktop, window frame, or console skin. Static images are repeated to preserve their time on screen. Audio is not included.

For example:

```powershell
bin/oceanblast.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" --sound --cpu-steps-per-tick 2 --record-frames build/wade.rgb --steps 2400000000 --exit-on-limit
```

Encode the raw stream with FFmpeg:

```powershell
ffmpeg -f rawvideo -pixel_format rgb24 -video_size 240x160 -framerate 30 -i build/wade.rgb -vf scale=720:480:flags=neighbor -c:v libx264 -pix_fmt yuv420p build/wade.mp4
```

The capture records host-time LCD presentation, not a guest frame counter or proof of the console's intended refresh rate. Recording adds frame-copy and file-write work; performance measurements taken during a recording should be identified as such. Enable `--profile` to retain the runtime's audio queue and MIPS measurements alongside the capture.
