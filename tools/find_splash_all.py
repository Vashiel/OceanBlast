import struct

with open("sdram.bin", "rb") as f:
    sdram = f.read()

pos = 0
while True:
    pos = sdram.find(b"splash", pos)
    if pos == -1: break
    va = 0x30000000 + pos
    print(f"Found 'splash' at PA 0x{va:08x}: {sdram[pos:pos+30]}")
    pos += 1
