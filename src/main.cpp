#include <iostream>
#include <string>
#include <cstring>
#include <iomanip>
#include "core/types.h"
#include "memory/bus.h"
#include "cpu/arm920t.h"
#include "cartridge/cart_parser.h"

using namespace oceanblast;


void printBanner() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  OceanBlast - Nikko digiBLAST Emulator (2005)   " << std::endl;
    std::cout << "  Hardware: Samsung OCEAN-L-20 (S3C2410 ARM920T) " << std::endl;
    std::cout << "=================================================" << std::endl;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <cartridge.bin> [--steps <N>] [--trace]" << std::endl;
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

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) {
            stepLimit = std::stoull(argv[++i]);
        } else if (arg == "--trace") {
            trace = true;
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

    std::cout << "\n[OceanBlast] Starting ARM920T Steppingstone execution from 0x00000000..." << std::endl;

    size_t executedSteps = 0;
    bool enteredSdram = false;

    while (!cpu.isHalted() && executedSteps < stepLimit) {
        u32 currentPC = cpu.getPC();

        if (!enteredSdram && currentPC >= 0x30000000) {
            enteredSdram = true;
            std::cout << "\n[OceanBlast] >>> Steppingstone Boot Complete! Jumped to SDRAM at 0x" 
                      << std::hex << currentPC << std::dec << " (Step " << executedSteps << ") <<<\n" << std::endl;
        }

        if (trace && executedSteps < 100) {
            std::cout << "[Trace " << executedSteps << "] PC=0x" << std::hex << std::setw(8) << std::setfill('0')
                      << currentPC << " CPSR=0x" << cpu.getCPSR() << std::dec << std::endl;
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
            for (size_t i = foundPos; i < foundPos + 4096 && i < ADDR_SDRAM_SIZE; ++i) {
                char ch = static_cast<char>(sdram[i]);
                if (ch == 0) break;
                log += ch;
            }
            std::cout << log << std::endl;
        } else {
            std::cout << "(Kernel log buffer not yet initialized)" << std::endl;
        }
        std::cout << "-----------------------------------------" << std::endl;
    }

    return 0;
}
