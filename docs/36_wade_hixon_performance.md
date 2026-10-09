# Wade Hixton Performance Observation

The emulator recorded 150 modeled seconds of Wade Hixton's Counter Punch at a manually selected 2x CPU ratio and a 40 MIPS GUI limiter. The decoded LCD stream contains 4,520 frames at 30 fps. The sequence reaches the title and story introduction; it does not reach a boxing match, so it is not full gameplay acceptance.

Across 150 one-second intervals, the run averaged 39.83 MIPS and 99.58% modeled speed. Sampled framebuffer changes averaged 5.54 per second, ranged from 0 to 39.99, and were zero in 66 intervals. The emulator reported 835 synchronized presentations. Audio reported 12 empty-queue events and no dropped samples. The video stream contains no audio; sound was enabled during the run.

These observations show uneven output during this bounded sequence. They do not identify the underlying CPU, LCD, or guest-software cause, and do not establish original hardware speed. [CSV measurements](validation/2026-10-09_wade_video_capture.csv) retain the run parameters and per-second samples.

The ROM's CRC32 is `eba2a2c3`, matching the known 17,301,504-byte dump. Automatic settings previously treated it as unknown and selected 1x, despite prior controlled startup comparisons showing more guest progress at 2x for the same peripheral-time budget. The automatic profile now selects 2x for this exact size and checksum; a bounded run confirmed the selection ([record](validation/2026-10-09_wade_auto_profile.csv)). This is a provisional compatibility setting; verify sustained speed, controls, and audio in an actual boxing match before treating it as an accepted title profile.
