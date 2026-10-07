import struct

raw = open('roms/games/Rayman 3 [G] (M10).bin', 'rb').read(528 * 512)
clean = bytearray()
for p in range(512):
    clean.extend(raw[p*528 : p*528 + 512])

for addr in range(0x30fa1584, 0x30fa15c0, 20):
    off = addr - 0x30f80000
    w0, w1, w2, w3, w4 = struct.unpack('<IIIII', clean[off:off+20])
    def s(p):
        if 0x30f80000 <= p < 0x30f80000 + len(clean):
            return clean[p-0x30f80000:].split(b'\0')[0].decode(errors='ignore')
        return hex(p)
    print(f"0x{addr:08x}: name='{s(w0)}' maxargs={w1} rep={w2} fn=0x{w3:08x} usage='{s(w4)}'")
