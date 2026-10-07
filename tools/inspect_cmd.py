import struct

raw = open('roms/games/Rayman 3 [G] (M10).bin', 'rb').read(528 * 512)
clean = bytearray()
for p in range(512):
    clean.extend(raw[p*528 : p*528 + 512])

# Dump __u_boot_cmd table
# Each entry is 24 bytes (6 words): name, maxargs, repeatable, cmd, usage, help
for addr in range(0x30fa1000, 0x30fa15c0, 24):
    off = addr - 0x30f80000
    if off + 24 > len(clean): break
    name_p, maxargs, rep, cmd_p, usage_p, help_p = struct.unpack('<IIIIII', clean[off:off+24])
    def get_str(p):
        if 0x30f80000 <= p < 0x30f80000 + len(clean):
            return clean[p-0x30f80000:].split(b'\0')[0].decode(errors='ignore')
        return '?'
    print(f"0x{addr:08x}: name='{get_str(name_p)}' maxargs={maxargs} cmd=0x{cmd_p:08x} usage='{get_str(usage_p)}'")
