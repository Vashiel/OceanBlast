with open("roms/test.bin", "rb") as f:
    rom = f.read()

data = bytearray()
for p in range(384, 384 + 128):
    p_off = p * 528
    data.extend(rom[p_off:p_off + 512])

first_nz = next((i for i, b in enumerate(data) if b != 0), -1)
last_nz = len(data) - 1 - next((i for i, b in enumerate(reversed(data)) if b != 0), -1)
total_nz = sum(1 for b in data if b != 0)
print(f"First non-zero at {first_nz}, last non-zero at {last_nz}, total non-zero = {total_nz}")
print(f"Non-zero span = {last_nz - first_nz + 1} bytes")
