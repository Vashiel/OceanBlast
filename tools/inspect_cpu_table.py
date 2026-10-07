import gzip, struct

raw = open('roms/games/Rayman 3 [G] (M10).bin', 'rb').read()
clean = bytearray()
for p in range(len(raw) // 528):
    clean.extend(raw[p*528 : p*528 + 512])
uimage_data = clean[0x50000 : 0x50000 + 0x1c0000]
uncompressed = gzip.decompress(uimage_data[64 : 64 + struct.unpack('>I', uimage_data[12:16])[0]])

start_off = 0xc0018148 - 0xc0008000
for i in range(4):
    o = start_off + i * 28
    code, mask, name_p = struct.unpack('<III', uncompressed[o:o+12])
    name = ''
    if 0xc0008000 <= name_p < 0xc0008000 + len(uncompressed):
        name = uncompressed[name_p - 0xc0008000:].split(b'\0')[0].decode(errors='ignore')
    print(f'CPU {i}: ID=0x{code:08x}, Mask=0x{mask:08x}, Name="{name}"')
