#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include "core/types.h"
#include "memory/bus.h"
#include "cpu/arm920t.h"
#include "cartridge/cart_parser.h"

#include "display/display.h"

using namespace oceanblast;


void printBanner() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  OceanBlast - Nikko digiBLAST Emulator (2005)   " << std::endl;
    std::cout << "  Hardware: Samsung OCEAN-L-20 (S3C2410 ARM920T) " << std::endl;
    std::cout << "=================================================" << std::endl;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <cartridge.bin> [--steps <N>] [--gui] [--scale <2|3|4>] [--trace]" << std::endl;
}

int main(int argc, char* argv[]) {
    printBanner();

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string cartPath = argv[1];
    size_t stepLimit = 500000;
    bool trace = false;
    bool gui = false;
    int scale = 3;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) {
            stepLimit = std::stoull(argv[++i]);
        } else if (arg == "--trace") {
            trace = true;
        } else if (arg == "--gui" || arg == "--window") {
            gui = true;
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::stoi(argv[++i]);
        }
    }

    oceanblast::Bus bus;

    std::cout << "[Loader] Opening digiBLAST Cartridge: " << cartPath << std::endl;
    if (!bus.loadCartridge(cartPath)) {
        std::cerr << "[Error] Failed to load cartridge: " << cartPath << std::endl;
        return 1;
    }

    // Inspect Cartridge Header & Boot Structure
    auto cartInfo = oceanblast::CartParser::parse(bus.getCartNand());
    oceanblast::CartParser::printInfo(cartInfo);

    // Initialize ARM920T CPU
    oceanblast::ARM920T cpu(bus);
    cpu.reset(0x00000000); // Boot from Steppingstone SRAM

    oceanblast::Display display(scale);
    if (gui) {
        if (!display.init("OceanBlast - Nikko digiBLAST (2005)")) {
            std::cerr << "[Warning] Failed to initialize display window; falling back to headless mode." << std::endl;
            gui = false;
        }
    }

    std::cout << "\n[OceanBlast] Starting ARM920T Steppingstone execution from 0x00000000..." << std::endl;

    size_t executedSteps = 0;
    bool enteredSdram = false;

    while (!cpu.isHalted() && executedSteps < stepLimit) {
        if (gui && (executedSteps % 50000 == 0)) {
            display.processEvents();
            if (!display.isOpen()) {
                std::cout << "\n[OceanBlast] Display window closed by user." << std::endl;
                break;
            }
            bus.setButtonMask(display.getButtonMask());
            u32 fbPhys = 0x30300000;
            u32 lcdsaddr1 = bus.getMmio(0x4D000014);
            if (lcdsaddr1 != 0) {
                fbPhys = (lcdsaddr1 & 0x1FFFFFFF) << 1;
            } else if (executedSteps > 185000000) {
                fbPhys = 0x302A0000;
            } else if (executedSteps > 4000000) {
                fbPhys = 0x30310000;
            }
            display.updateFrame(bus.getSdramPtr(), fbPhys);
        }

        u32 currentPC = cpu.getPC();

        if (!enteredSdram && currentPC >= 0x30000000) {
            enteredSdram = true;
            std::cout << "\n[OceanBlast] >>> Steppingstone Boot Complete! Jumped to SDRAM at 0x" 
                      << std::hex << currentPC << std::dec << " (Step " << executedSteps << ") <<<\n" << std::endl;
        }

        if (trace && executedSteps < 100) {
            std::cout << "[Trace " << executedSteps << "] PC=0x" << std::hex << currentPC
                      << " CPSR=0x" << cpu.getCPSR() << " " << cpu.disassembleCurrentARM() << std::dec << std::endl;
        }

        static u32 pcHist[32];
        static size_t pcHistIdx = 0;
        pcHist[pcHistIdx++ % 32] = currentPC;

        static bool jumpedToLow = false;
        if (!jumpedToLow && bus.isMmuEnabled() && currentPC < 0x1000) {
            jumpedToLow = true;
            std::cerr << "\n[ALERT] PC jumped to low address: 0x" << std::hex << currentPC
                      << " at step " << std::dec << executedSteps << ". Last 16 PCs:\n";
            for (int k = 16; k >= 1; --k) {
                u32 histPC = pcHist[(pcHistIdx - k) % 32];
                std::cerr << "  [" << (17 - k) << "] 0x" << std::hex << histPC << std::dec << "\n";
            }
            std::cerr << std::endl;
        }

        static int forkVisit = 0;
        static int forkTraceSteps = 0;
        if (currentPC == 0x4012a608) {
            forkVisit++;
            forkTraceSteps = 40;
            std::cout << "\n>>> [HIT 0x4012a608 Visit #" << forkVisit << " at Step " << executedSteps
                      << " TTB=0x" << std::hex << bus.getTtb() << "] <<<\n";
            cpu.dumpState();
            std::cout << "Stack at SP=0x" << std::hex << cpu.getSP() << ":\n";
            for (u32 o = 0; o < 32; o += 4) {
                u32 val = 0;
                bus.peek32(cpu.getSP() + o, val);
                std::cout << "  [SP+" << o << "]=0x" << val;
            }
            std::cout << std::dec << "\n";
        }
        if (forkTraceSteps > 0) {
            forkTraceSteps--;
            std::cout << "[FORK-V" << forkVisit << "-TRACE] PC=0x" << std::hex << currentPC
                      << " CPSR=0x" << cpu.getCPSR() << " " << cpu.disassembleCurrentARM() << std::dec << std::endl;
        }

        static bool hit93540 = false;
        if (!hit93540 && currentPC == 0x93540) {
            hit93540 = true;
            std::cout << "\n>>> [FIRST HIT 0x93540 at Step " << executedSteps << "] <<<\n";
            std::cout << "Last 16 PCs:\n";
            for (int k = 16; k >= 1; --k) {
                u32 histPC = pcHist[(pcHistIdx - k) % 32];
                std::cout << "  [" << (17 - k) << "] 0x" << std::hex << histPC << std::dec << "\n";
            }
            std::cout << "Registers at 0x93540:\n";
            cpu.dumpState();
            std::cout << "--------------------------------------------\n" << std::endl;
        }

        if (executedSteps >= stepLimit - 30) {
            std::cout << "[Step " << executedSteps << "] PC=0x" << std::hex << std::setw(8) << std::setfill('0')
                      << currentPC << " CPSR=0x" << cpu.getCPSR() << " " << cpu.disassembleCurrentARM() << std::dec << std::endl;
        }

        cpu.step();
        executedSteps++;
    }

    std::cout << "\n[OceanBlast] Execution finished after " << executedSteps << " instructions." << std::endl;
    cpu.dumpState();

    if (bus.isMmuEnabled()) {
        std::cout << "\n--- [Linux Kernel dmesg / Log Buffer] ---" << std::endl;
        const u8* sdram = bus.getSdramPtr();
        std::string banner = "Linux version";
        size_t foundPos = std::string::npos;
        for (size_t i = 0; i + banner.size() < ADDR_SDRAM_SIZE; ++i) {
            if (std::memcmp(sdram + i, banner.data(), banner.size()) == 0) {
                if (i >= 3 && sdram[i-3] == '<' && sdram[i-1] == '>') {
                    foundPos = i - 3;
                    break;
                }
            }
        }
        if (foundPos != std::string::npos) {
            std::string log;
            for (size_t i = foundPos; i < foundPos + 32768 && i < ADDR_SDRAM_SIZE; ++i) {
                char ch = static_cast<char>(sdram[i]);
                if (ch == 0) break;
                log += ch;
            }
            std::cout << log << std::endl;
        } else {
            std::cout << "(Kernel log buffer not yet initialized)" << std::endl;
        }
        std::cout << "-----------------------------------------" << std::endl;

        std::cout << "\n--- [Page Table & MMU Inspection] ---" << std::endl;
        std::cout << "Active TTB: 0x" << std::hex << bus.getTtb() << std::dec << std::endl;
        for (u32 ttbBase : {0x30004000u, bus.getTtb() & ~0x3FFFu}) {
            std::cout << "TTB at 0x" << std::hex << ttbBase << ":" << std::dec << std::endl;
            if (ttbBase >= ADDR_SDRAM_BASE && (ttbBase - ADDR_SDRAM_BASE) <= ADDR_SDRAM_SIZE - 16384) {
                for (u32 idx = 0; idx < 4096; ++idx) {
                    u32 descOffset = ttbBase - ADDR_SDRAM_BASE + (idx * 4);
                    u32 desc = 0;
                    std::memcpy(&desc, sdram + descOffset, 4);
                    if (desc != 0) {
                        u32 va = idx << 20;
                        std::cout << "  VA 0x" << std::hex << va << " -> desc 0x" << desc;
                        if ((desc & 3) == 2) {
                            std::cout << " (Section PA 0x" << (desc & 0xFFF00000) << ")";
                        } else if ((desc & 3) == 1) {
                            u32 ptPhys = desc & ~0x3FF;
                            std::cout << " (Coarse PT PA 0x" << ptPhys << ")";
                        }
                        std::cout << std::dec << std::endl;
                    }
                }
            }
        }

        std::cout << "\n--- [Exception Vectors Inspection (0xFFFF0000)] ---" << std::endl;
        for (u32 v = 0xFFFF0000; v <= 0xFFFF0030; v += 4) {
            std::cout << "  [0x" << std::hex << v << "] PA 0x" << bus.translate(v)
                      << " = 0x" << bus.read32(v) << std::dec << std::endl;
        }

        std::cout << "\n--- [S3C2410 LCD Controller Registers] ---" << std::endl;
        std::cout << "LCDCON1:   0x" << std::hex << bus.read32(0x4D000000) << std::endl;
        std::cout << "LCDCON2:   0x" << std::hex << bus.read32(0x4D000004) << std::endl;
        std::cout << "LCDCON3:   0x" << std::hex << bus.read32(0x4D000008) << std::endl;
        std::cout << "LCDCON4:   0x" << std::hex << bus.read32(0x4D00000C) << std::endl;
        std::cout << "LCDCON5:   0x" << std::hex << bus.read32(0x4D000010) << std::endl;
        std::cout << "LCDSADDR1: 0x" << std::hex << bus.read32(0x4D000014) << std::endl;
        std::cout << "LCDSADDR2: 0x" << std::hex << bus.read32(0x4D000018) << std::endl;
        std::cout << "LCDSADDR3: 0x" << std::hex << bus.read32(0x4D00001C) << std::dec << std::endl;

        // Dump Framebuffer memory and full SDRAM
        std::ofstream fb0("fb_30300000.raw", std::ios::binary);
        if (fb0.is_open()) fb0.write(reinterpret_cast<const char*>(sdram + 0x300000), 153600);
        std::ofstream fb1("fb_30310000.raw", std::ios::binary);
        if (fb1.is_open()) fb1.write(reinterpret_cast<const char*>(sdram + 0x310000), 153600);
        std::ofstream sdr("sdram.bin", std::ios::binary);
        if (sdr.is_open()) sdr.write(reinterpret_cast<const char*>(sdram), ADDR_SDRAM_SIZE);
        std::cout << "\n[Debug] Dumped sdram.bin (16 MB)" << std::endl;

        std::cout << "[Debug] Memory at PA 0x30204ba0:" << std::hex;
        for (u32 o = 0; o < 32; o += 4) {
            u32 val = 0;
            std::memcpy(&val, sdram + (0x00204ba0 + o), 4);
            std::cout << "  [+0x" << o << "]=0x" << val;
        }
        std::cout << std::dec << std::endl;
    }

    if (gui && display.isOpen()) {
        u32 fbPhys = 0x30300000;
        u32 lcdsaddr1 = bus.getMmio(0x4D000014);
        if (lcdsaddr1 != 0) fbPhys = (lcdsaddr1 & 0x1FFFFFFF) << 1;
        else if (executedSteps > 185000000) fbPhys = 0x302A0000;
        else if (executedSteps > 4000000) fbPhys = 0x30310000;
        display.updateFrame(bus.getSdramPtr(), fbPhys);
        std::cout << "[Display] Emulation paused. Press ESC or close the window to exit." << std::endl;
        while (display.isOpen()) {
            display.processEvents();
            display.updateFrame(bus.getSdramPtr(), fbPhys);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    return 0;
}
