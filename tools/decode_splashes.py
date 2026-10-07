import struct

with open("sdram.bin", "rb") as f:
    sdram = f.read()

def decode_and_save(offset, filename):
    src = sdram[offset:offset + 57600]
    pixels_rgb24 = bytearray()
    for i in range(0, 57600, 3):
        b0 = src[i]
        b1 = src[i+1]
        b2 = src[i+2]
        
        p0 = (b0 << 4) | (b1 >> 4)
        p1 = ((b1 & 0x0F) << 8) | b2
        
        r0 = ((p0 >> 8) & 0x0F) * 17
        g0 = ((p0 >> 4) & 0x0F) * 17
        b0_val = (p0 & 0x0F) * 17
        
        r1 = ((p1 >> 8) & 0x0F) * 17
        g1 = ((p1 >> 4) & 0x0F) * 17
        b1_val = (p1 & 0x0F) * 17
        
        pixels_rgb24.extend([b0_val, g0, r0, b1_val, g1, r1])

    width, height = 240, 160
    row_stride = width * 3
    image_size = row_stride * height
    file_size = 54 + image_size

    bmp = bytearray(b'BM')
    bmp.extend(struct.pack('<IHHI', file_size, 0, 0, 54))
    bmp.extend(struct.pack('<IIIHHIIIIII', 40, width, height, 1, 24, 0, image_size, 2835, 2835, 0, 0))

    for y in range(height - 1, -1, -1):
        row = pixels_rgb24[y * row_stride:(y + 1) * row_stride]
        bmp.extend(row)

    with open(filename, "wb") as f:
        f.write(bmp)
    print(f"Saved {filename}")

# Splash0 at 0x30300000 -> offset 0x300000
decode_and_save(0x300000, "splash0.bmp")
# Splash1 at 0x30310000 -> offset 0x310000
decode_and_save(0x310000, "splash1.bmp")
