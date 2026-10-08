// Controlled CPU/peripheral ratio probe. No host pacing, audio device or input.
#include "cpu/arm920t.h"
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <algorithm>
using namespace oceanblast;

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "Usage: runtime_probe ROM TICK_BUDGET CPU_STEPS_PER_TICK OUTPUT.csv [EEPROM]\n";
        return 1;
    }
    uint64_t budget = 0, ratio = 0;
    try { budget = std::stoull(argv[2]); ratio = std::stoull(argv[3]); }
    catch (...) { return 1; }
    if (!budget || !ratio || ratio > 16 || budget > std::numeric_limits<uint64_t>::max() / ratio) return 1;
    Bus bus;
    if (argc == 6 && !bus.loadEeprom(argv[5])) return 1;
    if (!bus.loadCartridge(argv[1])) return 1;
    ARM920T cpu(bus); cpu.reset();
    uint64_t samples = 0, changes = 0;
    bus.setAudioCallback([&](const int16_t*, size_t count) { samples += count; });
    std::ofstream out(argv[4]);
    if (!out) return 1;
    out << "scheduled_ticks,cpu_steps,pc,lcd_control,framebuffer,stride,hash,audio_samples\n";
    std::map<uint32_t, uint64_t> pages;
    uint32_t previousHash = 0, previousAddress = 0;
    size_t previousStride = 0;
    bool havePrevious = false;
    const uint64_t period = 200000; // 100 polls/s of the existing 20M-tick model.
    for (uint64_t step = 0; step < budget * ratio && !cpu.isHalted(); ++step) {
        cpu.step(step % ratio == 0 ? 1 : 0);
        if ((step + 1) % (period * ratio)) continue;
        const uint32_t address = (bus.getMmio(0x4d000014) & 0x1fffffff) << 1;
        const size_t stride = std::max(bus.getFramebufferStride(), bus.isLcd16Bpp() ? size_t(480) : size_t(360));
        uint32_t hash = 2166136261u;
        if (address >= ADDR_SDRAM_BASE && uint64_t(address - ADDR_SDRAM_BASE) + stride * 160 <= ADDR_SDRAM_SIZE) {
            const auto* data = bus.getSdramPtr() + address - ADDR_SDRAM_BASE;
            for (size_t byte = 0; byte < stride * 160; ++byte) hash = (hash ^ data[byte]) * 16777619u;
        } else hash = 0;
        if ((step + 1) % (100000000 * ratio) == 0 && hash) {
            std::ofstream image(std::string(argv[4]) + ".frame_" + std::to_string((step + 1) / ratio) + ".raw", std::ios::binary);
            image.write(reinterpret_cast<const char*>(bus.getSdramPtr() + address - ADDR_SDRAM_BASE), stride * 160);
            if (!image) return 1;
        }
        if (havePrevious && (hash != previousHash || address != previousAddress || stride != previousStride)) ++changes;
        havePrevious = true; previousHash = hash; previousAddress = address; previousStride = stride;
        ++pages[cpu.getPC() & ~0xfffu];
        out << (step + 1) / ratio << ',' << step + 1 << ',' << cpu.getPC() << ','
            << bus.getMmio(0x4d000000) << ',' << address << ',' << stride << ',' << hash << ',' << samples << '\n';
    }
    std::ofstream profile(std::string(argv[4]) + ".pc.csv");
    profile << "pc_page,samples\n";
    for (const auto& page : pages) profile << page.first << ',' << page.second << '\n';
    std::cout << "Observed framebuffer changes: " << changes << "; PCM samples: " << samples << '\n';
    std::cout << "Tick budget is scheduled by the caller; exception-entry steps can return before ticking.\n";
    return !out || !profile ? 1 : 0;
}
