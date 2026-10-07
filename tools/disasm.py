import struct
import sys

COND_NAMES = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv"]
DP_OPS = ["and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"]

def disasm_arm_word(addr, w):
    cond = COND_NAMES[w >> 28]

    # BX / BLX
    if (w & 0x0FFFFFF0) == 0x012FFF10:
        rm = w & 0xF
        return f"bx{cond} r{rm}"
    if (w & 0x0FFFFFF0) == 0x012FFF30:
        rm = w & 0xF
        return f"blx{cond} r{rm}"

    # Coprocessor MCR / MRC
    if (w & 0x0FE00000) == 0x0E000000:
        cp = (w >> 8) & 0xF
        op1 = (w >> 21) & 7
        crn = (w >> 16) & 0xF
        rd  = (w >> 12) & 0xF
        crm = w & 0xF
        op2 = (w >> 5) & 7
        is_mrc = (w & 0x00100000) != 0
        name = "mrc" if is_mrc else "mcr"
        return f"{name}{cond} p{cp}, {op1}, r{rd}, c{crn}, c{crm}, {op2}"

    # Branch B / BL
    if (w & 0x0E000000) == 0x0A000000:
        link = (w & 0x01000000) != 0
        off = w & 0x00FFFFFF
        if off & 0x00800000:
            off -= 0x01000000
        target = (addr + 8 + (off << 2)) & 0xFFFFFFFF
        op = "bl" if link else "b"
        return f"{op}{cond} 0x{target:08x}"

    # Block Transfer LDM / STM
    if (w & 0x0E000000) == 0x08000000:
        p = (w >> 24) & 1
        u = (w >> 23) & 1
        s = (w >> 22) & 1
        w_bit = (w >> 21) & 1
        l = (w >> 20) & 1
        rn = (w >> 16) & 0xF
        op = "ldm" if l else "stm"
        mode = ("i" if u else "d") + ("b" if p else "a")
        rlist = []
        for r in range(16):
            if w & (1 << r):
                rlist.append(f"r{r}")
        wb = "!" if w_bit else ""
        s_flag = "^" if s else ""
        return f"{op}{cond}{mode} r{rn}{wb}, {{{', '.join(rlist)}}}{s_flag}"

    # Single Data Transfer LDR / STR
    if (w & 0x0C000000) == 0x04000000:
        i = (w >> 25) & 1
        p = (w >> 24) & 1
        u = (w >> 23) & 1
        b = (w >> 22) & 1
        w_bit = (w >> 21) & 1
        l = (w >> 20) & 1
        rn = (w >> 16) & 0xF
        rd = (w >> 12) & 0xF
        op = "ldr" if l else "str"
        if b:
            op += "b"
        op += cond

        if i == 0:
            off = w & 0xFFF
            sign = "+" if u else "-"
            if rn == 15 and p:
                pc = addr + 8
                target = (pc + off if u else pc - off) & 0xFFFFFFFF
                return f"{op} r{rd}, [pc, #{sign}0x{off:x}]  ; =0x{target:08x}"
            wb = "!" if w_bit else ""
            if p:
                return f"{op} r{rd}, [r{rn}, #{sign}0x{off:x}]{wb}"
            else:
                return f"{op} r{rd}, [r{rn}], #{sign}0x{off:x}"

    # Data Processing
    if (w & 0x0C000000) == 0x00000000:
        is_imm = (w >> 25) & 1
        opcode = (w >> 21) & 0xF
        s = (w >> 20) & 1
        rn = (w >> 16) & 0xF
        rd = (w >> 12) & 0xF
        op = DP_OPS[opcode] + cond
        if s and opcode not in (8, 9, 10, 11):
            op += "s"

        if is_imm:
            imm = w & 0xFF
            rot = ((w >> 8) & 0xF) * 2
            val = ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF if rot else imm
            if opcode in (13, 15): # MOV, MVN
                return f"{op} r{rd}, #0x{val:x}"
            elif opcode in (8, 9, 10, 11): # TST, TEQ, CMP, CMN
                return f"{op} r{rn}, #0x{val:x}"
            return f"{op} r{rd}, r{rn}, #0x{val:x}"

    return f"raw 0x{w:08x}"

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "roms/games/Rayman 3 [G] (M10).bin"
    start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x0
    count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 64

    with open(path, "rb") as f:
        f.seek(start)
        raw = f.read(count * 4)

    for i in range(0, len(raw), 4):
        addr = start + i
        w = struct.unpack("<I", raw[i:i+4])[0]
        line = disasm_arm_word(addr, w)
        print(f"0x{addr:08x}:  {w:08x}   {line}")
