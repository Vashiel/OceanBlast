from PIL import Image

with open("roms/test.bin", "rb") as f:
    rom = f.read()

# Extract splash0 (page 384 .. 384+128)
data = bytearray()
for p in range(384, 384 + 128):
    p_off = p * 528
    data.extend(rom[p_off:p_off + 512])

# Try different widths/strides:
# In 12-bit packed (3 bytes = 2 pixels):
# If width = W, row_bytes = W * 1.5
for w in [240, 256, 320]:
    row_bytes = int(w * 1.5)
    h = min(160, len(data) // row_bytes)
    
    img = Image.new("RGB", (w, h))
    for y in range(h):
        row_start = y * row_bytes
        for x in range(0, w, 2):
            idx = row_start + (x * 3) // 2
            if idx + 2 < len(data):
                b0 = data[idx]
                b1 = data[idx+1]
                b2 = data[idx+2]
                
                # Try Scheme A: b0: R0(7:4) G0(3:0), b1: B0(7:4) R1(3:0), b2: G1(7:4) B1(3:0)
                # (Standard RGB RGB nibbles)
                r0 = ((b0 >> 4) & 0xF) * 17
                g0 = (b0 & 0xF) * 17
                b0_val = ((b1 >> 4) & 0xF) * 17
                
                r1 = (b1 & 0xF) * 17
                g1 = ((b2 >> 4) & 0xF) * 17
                b1_val = (b2 & 0xF) * 17
                
                img.putpixel((x, y), (r0, g0, b0_val))
                if x + 1 < w:
                    img.putpixel((x + 1, y), (r1, g1, b1_val))
    img.save(f"splash0_w{w}_schemeA.png")

print("Saved candidate images for Scheme A.")
