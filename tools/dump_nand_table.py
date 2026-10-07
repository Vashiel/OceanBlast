import struct

raw = open('roms/games/Rayman 3 [G] (M10).bin', 'rb').read(528 * 512)
clean = bytearray()
for p in range(512):
    clean.extend(raw[p*528 : p*528 + 512])

ptr = struct.unpack('<I', clean[0xbc0c:0xbc10])[0]
print(f'Table pointer: 0x{ptr:08x}')
table_off = ptr - 0x30f80000

for i in range(12):
    row = table_off + i * 32
    if row + 32 > len(clean):
        break
    name_ptr, mfr, dev, page_size = struct.unpack('<IIII', clean[row:row+16])
    if name_ptr == 0:
        break
    name = ''
    if name_ptr >= 0x30f80000 and (name_ptr - 0x30f80000) < len(clean):
        no = name_ptr - 0x30f80000
        name = clean[no:no+30].split(b'\0')[0].decode(errors='ignore')
    print(f'Chip {i}: Name="{name}" Mfr=0x{mfr:02x} Dev=0x{dev:02x} PageSize={page_size}')
