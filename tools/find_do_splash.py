import struct

with open("sdram.bin", "rb") as f:
    sdram = f.read()

# U-Boot is at 0x30F80000 -> SDRAM offset 0xF80000
uboot = sdram[0xF80000:0xFA5000]

# Search for "splash" command table entry
pos = 0
while True:
    pos = uboot.find(b"splash\x00", pos)
    if pos == -1: break
    va = 0x30F80000 + pos
    print(f"Found 'splash' string at 0x{va:08x}")
    # Check if a cmd_tbl_t points here
    pos += 1

# Search for pointers to "splash" string in U-Boot
splash_str_va = 0x30f80000 + uboot.find(b"splash\x00")
print(f"splash_str_va: 0x{splash_str_va:08x}")
for off in range(0, len(uboot) - 4, 4):
    val = struct.unpack_from("<I", uboot, off)[0]
    if val == splash_str_va:
        va = 0x30F80000 + off
        print(f"  cmd_tbl_t at 0x{va:08x}: name=0x{val:08x}")
        # cmd_tbl_t: char *name, int maxargs, int repeatable, int (*cmd)(cmd_tbl_s*, int, int, char*[]), char *usage, char *help
        cmd_func = struct.unpack_from("<I", uboot, off + 12)[0]
        print(f"  do_splash function at 0x{cmd_func:08x}")
