#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>
#include <iomanip>
#include <thread>
#include <chrono>
#include <sstream>
#include <map>
#include "core/types.h"
#include "core/input_script.h"
#include "core/cartridge_settings.h"
#include "core/emulation_clock.h"
#include "core/execution_batch.h"
#include "core/host_pacer.h"
#include "core/guest_symbols.h"
#include "memory/bus.h"
#include "cpu/arm920t.h"
#include "cartridge/cart_parser.h"

#include "display/display.h"
#include "audio/audio.h"
#include "display/launcher.h"
#include "display/display_profile.h"

using namespace oceanblast;


void printBanner() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  OceanBlast - Nikko digiBLAST Emulator (2005)   " << std::endl;
    std::cout << "  Hardware: Samsung OCEAN-L-20 (S3C2410 ARM920T) " << std::endl;
    std::cout << "=================================================" << std::endl;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <cartridge.bin> [--steps <N>] [--gui] [--scale <2|3|4>] [--sound] [--trace]" << std::endl;
    std::cout << "  --clock-mips <N>  GUI step-rate limit (default 20 times CPU ratio; 0 disables pacing)\n"
              << "  --preset <auto|off> Content-based cartridge settings (default auto); explicit CPU ratio overrides\n"
              << "  --timing <legacy|auto> Register-clock CPU timing with cached cycle estimates (experimental)\n"
              << "  --emulated-seconds <N> Stop after a bounded amount of modeled time\n"
              << "  --cpu-steps-per-tick <N> Diagnostic CPU work per peripheral tick (1..16; overrides cartridge preset)\n"
              << "  --execution-batch <N> Host bookkeeping batch (1..4096; 1 selects scalar execution)\n"
              << "  --simple-alu <on|off> Common ARM ALU execution path (default on)\n"
              << "  --host-wait <timer|sleep> GUI pacing wait (default high-resolution timer when supported)\n"
              << "  --audio-rate <Hz> Host output rate (default 22050)\n"
              << "  --profile          Write GUI performance.csv, including audio rate and dropped samples\n";
    std::cout << "  --snapshot-interval <N> Save framebuffer/state every N instructions\n"
              << "  --exit-on-limit         Close GUI after the instruction budget is exhausted\n"
              << "  --nvram <path>          Load/save a separate 2048-byte EEPROM image\n"
              << "  --pc-profile <N>        Sample execution pages every N instructions\n"
              << "  --i2c-log               Trace I2C register writes and byte completions\n"
              << "  --mmio-profile          Count guest MMIO accesses (excludes host inspection)\n"
              << "  --display-format <auto|lcd|rgb444|rgb565> Display decoder (default auto)\n"
              << "  --frame-sync <auto|raw> Complete video write sweeps (default auto)\n"
              << "  --record-frames <path> Record decoded LCD frames as 240x160 RGB24 at 30 fps\n"
              << "  --window-mode <skin|plain> Window appearance (default plain); F11 toggles fullscreen\n"
              << "  --fullscreen Start with a borderless fullscreen LCD\n"
              << "  --renderer <auto|gdi> Vsynced DXGI output with GDI fallback (default auto)\n"
              << "  --display-stride <bytes> Diagnostic host scanline stride\n"
              << "  --fault-log             Log all exception contexts, including expected page faults\n"
              << "  --trace-pc <start> <end> Trace an inclusive PC range (decimal or 0x addresses)\n"
              << "  --input-script <path>   Replay step/mask events (decimal steps, hex masks)\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    if (argc < 2) {
        FreeConsole();
        return oceanblast::launcher::run();
    }
#endif
    printBanner();

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string cartPath = argv[1];
    size_t stepLimit = 500000;
    bool customSteps = false;
    bool trace = false;
    bool gui = false;
    bool windowSkin = false, startFullscreen = false;
    int scale = 3;

    bool sound = false;
    int audioRate = 22050; // Native digiBLAST S3C2410 audio rate
    bool debug = false;
    bool faultLog = false;
    bool i2cLog = false;
    bool mmioProfile = false;
    std::string nvramPath;
    bool simpleAluExecution = true;
    int displayFormat = 0; // 0: LCD registers, 1: RGB444, 2: RGB565.
    bool automaticDisplay = true;
    bool coherentFrames = true, gdiPresentation = false;
    size_t displayStride = 0;
    bool exitOnLimit = false;
    bool traceRange = false;
    u32 traceStart = 0, traceEnd = 0;
    bool profile = false;
    double clockMips = 20.0; // Timer/DMA model currently assumes 20M instructions/s.
    bool explicitClock = false;
    bool autoTiming = false;
    bool automaticPreset = true, explicitRatio = false;
    uint64_t tickLimit = UINT64_MAX;
    size_t cpuStepsPerTick = 1;
    size_t executionBatch = 4096;
    bool preciseHostWait = true;
    size_t snapshotInterval = 0;
    size_t pcProfileInterval = 0, nextPcProfile = 0;
    std::map<std::pair<u32, u32>, uint64_t> pcProfile;
    std::string inputScriptPath;
    std::string recordFramesPath;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) {
            stepLimit = std::stoull(argv[++i]);
            customSteps = true;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--fault-log") {
            faultLog = true;
        } else if (arg == "--display-format" && i + 1 < argc) {
            const std::string format = argv[++i];
            automaticDisplay = format == "auto";
            if (format == "auto" || format == "lcd") displayFormat = 0;
            else if (format == "rgb444") displayFormat = 1;
            else if (format == "rgb565") displayFormat = 2;
            else { std::cerr << "[Error] Invalid display format." << std::endl; return 1; }
        } else if (arg == "--frame-sync" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode != "auto" && mode != "raw") { std::cerr << "[Error] Invalid frame sync mode.\n"; return 1; }
            coherentFrames = mode == "auto";
        } else if (arg == "--renderer" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode != "auto" && mode != "gdi") { std::cerr << "[Error] Invalid renderer.\n"; return 1; }
            gdiPresentation = mode == "gdi";
        } else if (arg == "--display-stride" && i + 1 < argc) {
            displayStride = std::stoull(argv[++i]);
        } else if (arg == "--pc-profile" && i + 1 < argc) {
            pcProfileInterval = std::stoull(argv[++i]);
        } else if (arg == "--i2c-log") {
            i2cLog = true;
        } else if (arg == "--mmio-profile") {
            mmioProfile = true;
        } else if (arg == "--nvram" && i + 1 < argc) {
            nvramPath = argv[++i];
        } else if (arg == "--trace-pc" && i + 2 < argc) {
            traceStart = static_cast<u32>(std::stoul(argv[++i], nullptr, 0));
            traceEnd = static_cast<u32>(std::stoul(argv[++i], nullptr, 0));
            traceRange = true;
        } else if (arg == "--exit-on-limit") {
            exitOnLimit = true;
        } else if (arg == "--profile") {
            profile = true;
        } else if (arg == "--snapshot-interval" && i + 1 < argc) {
            snapshotInterval = std::stoull(argv[++i]);
        } else if (arg == "--input-script" && i + 1 < argc) {
            inputScriptPath = argv[++i];
        } else if (arg == "--record-frames" && i + 1 < argc) {
            recordFramesPath = argv[++i];
            gui = true;
        } else if (arg == "--clock-mips" && i + 1 < argc) {
            clockMips = std::stod(argv[++i]);
            explicitClock = true;
        } else if (arg == "--preset" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode != "auto" && mode != "off") { std::cerr << "[Error] Invalid preset mode.\n"; return 1; }
            automaticPreset = mode == "auto";
        } else if (arg == "--timing" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode != "auto" && mode != "legacy") { std::cerr << "[Error] Invalid timing mode.\n"; return 1; }
            autoTiming = mode == "auto";
        } else if (arg == "--emulated-seconds" && i + 1 < argc) {
            const double duration = std::stod(argv[++i]);
            if (!(duration > 0 && duration <= 86400)) { std::cerr << "[Error] Invalid modeled duration.\n"; return 1; }
            tickLimit = uint64_t(duration * EmulationClock::ticksPerSecond);
        } else if (arg == "--cpu-steps-per-tick" && i + 1 < argc) {
            explicitRatio = true;
            try { cpuStepsPerTick = std::stoull(argv[++i]); }
            catch (...) { std::cerr << "[Error] Invalid CPU/peripheral ratio.\n"; return 1; }
        } else if (arg == "--execution-batch" && i + 1 < argc) {
            try { executionBatch = std::stoull(argv[++i]); }
            catch (...) { std::cerr << "[Error] Invalid execution batch.\n"; return 1; }
        } else if (arg == "--simple-alu" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value != "on" && value != "off") { std::cerr << "[Error] Invalid ALU mode.\n"; return 1; }
            simpleAluExecution = value == "on";
        } else if (arg == "--host-wait" && i + 1 < argc) {
            const std::string wait = argv[++i];
            if (wait != "timer" && wait != "sleep") { std::cerr << "[Error] Invalid host wait.\n"; return 1; }
            preciseHostWait = wait == "timer";
        } else if (arg == "--trace") {
            trace = true;
        } else if (arg == "--fullscreen") {
            gui = true; startFullscreen = true;
        } else if (arg == "--window-mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode != "skin" && mode != "plain") { std::cerr << "[Error] Invalid window mode.\n"; return 1; }
            windowSkin = mode == "skin";
        } else if (arg == "--gui" || arg == "--window") {
            gui = true;
        } else if (arg == "--sound" || arg == "--audio") {
            sound = true;
        } else if ((arg == "--audio-rate" || arg == "--rate" || arg == "--samplerate") && i + 1 < argc) {
            audioRate = std::stoi(argv[++i]);
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::stoi(argv[++i]);
        }
    }

    if (!executionBatch || executionBatch > 4096) {
        std::cerr << "[Error] Execution batch must be between 1 and 4096.\n"; return 1;
    }
    if (!cpuStepsPerTick || cpuStepsPerTick > 16) {
        std::cerr << "[Error] CPU steps per tick must be between 1 and 16.\n"; return 1;
    }
    if (autoTiming && (cpuStepsPerTick != 1 || (explicitClock && clockMips != 0))) {
        std::cerr << "[Error] Automatic timing cannot be combined with an instruction ratio or MIPS limit.\n"; return 1;
    }
    if (gui && !customSteps) {
        stepLimit = std::numeric_limits<size_t>::max();
    }
    if (displayStride && (displayStride < (displayFormat == 1 ? 360u : 480u) || displayStride > 8192 || displayStride % 4)) {
        std::cerr << "[Error] Invalid display stride." << std::endl; return 1;
    }
    if (traceRange && traceStart > traceEnd) {
        std::cerr << "[Error] Invalid trace PC range." << std::endl;
        return 1;
    }
    if (!(clockMips >= 0 && clockMips <= 1000) || audioRate < 4000 || audioRate > 192000) {
        std::cerr << "[Error] Invalid clock or audio rate." << std::endl;
        return 1;
    }

    oceanblast::Bus bus;
    if (!nvramPath.empty()) {
        std::error_code error;
        const bool existing = std::filesystem::exists(nvramPath, error);
        if (error || (existing && !bus.loadEeprom(nvramPath))) {
            std::cerr << "[Error] NVRAM file must contain exactly 2048 bytes." << std::endl;
            return 1;
        }
    }
    std::vector<InputEvent> inputEvents;
    if (!inputScriptPath.empty()) {
        std::ifstream input(inputScriptPath);
        std::string error;
        if (!input || !readInputScript(input, inputEvents, error)) {
            std::cerr << "[Error] Input script: " << (error.empty() ? "cannot open file" : error) << std::endl;
            return 1;
        }
    }
    size_t nextInput = 0, nextSnapshot = snapshotInterval;

    std::cout << "[Loader] Opening digiBLAST Cartridge: " << cartPath << std::endl;
    if (!bus.loadCartridge(cartPath)) {
        std::cerr << "[Error] Failed to load cartridge: " << cartPath << std::endl;
        return 1;
    }

    // Inspect Cartridge Header & Boot Structure
    auto cartInfo = oceanblast::CartParser::parse(bus.getCartNand());
    oceanblast::CartParser::printInfo(cartInfo);
    const auto& cartridge = bus.getCartNand();
    const uint32_t cartridgeCrc = cartridgeCrc32(cartridge.data(), cartridge.size());
    const auto cartridgeSettings = resolveCartridgeSettings(cartridge.size(), cartridgeCrc,
        automaticPreset, explicitRatio, cpuStepsPerTick, autoTiming);
    cpuStepsPerTick = cartridgeSettings.cpuStepsPerTick;
    if (!explicitClock) clockMips *= cpuStepsPerTick;
    std::cout << "[Preset] " << cartridgeSettings.name << "; CPU ratio: " << cpuStepsPerTick
              << "; selection: " << (automaticPreset ? "automatic" : "disabled") << '\n';
    const DisplayProfile displayProfile = identifyDisplayProfile(cartridge.size(), cartridgeCrc);
    std::cout << "[Display] Cartridge CRC32: " << std::hex << cartridgeCrc << std::dec
              << "; selection: " << (automaticDisplay ? "automatic" : "explicit")
              << "; profile: " << (displayProfile == DisplayProfile::CrazyJack ? "Crazy Jack" : "LCD registers") << '\n';

    // Initialize ARM920T CPU
    oceanblast::ARM920T cpu(bus);
    cpu.reset(0x00000000); // Boot from Steppingstone SRAM
    cpu.setSimpleAluExecution(simpleAluExecution);
    cpu.setCycleTiming(autoTiming);
    cpu.setDebugLogging(debug || trace);
    cpu.setFaultLogging(faultLog);
    bus.setI2cLogging(i2cLog);
    bus.setMmioProfiling(mmioProfile);

    oceanblast::Display display(scale);
    display.configureWindow(windowSkin, startFullscreen);
    display.useGdiPresentation(gdiPresentation);
    oceanblast::Audio audio;
    if (gui) {
        if (!display.init("OceanBlast - Nikko digiBLAST (2005)")) {
            std::cerr << "[Warning] Failed to initialize display window; falling back to headless mode." << std::endl;
            gui = false;
        } else {
            // The preloaded NAND splash is packed RGB444, before LCD setup.
            display.updateFrame(bus.getSdramPtr(), 0x30300000, false, 360);
        }
    }

    bus.enableHostFrameCapture(gui && coherentFrames && !displayStride && displayFormat == 0);

    if (sound) {
        if (audio.init(audioRate, 2)) {
            bus.setAudioCallback([&](const int16_t* s, size_t n) {
                u32 currentRate = bus.getAudioSampleRate();
                audio.writeSamples(s, n, currentRate);
            });
        }
    }

    std::ofstream recordedFrames;
    std::vector<uint8_t> recordedRgb(static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT * 3);
    uint64_t recordedFrameCount = 0;
    if (!recordFramesPath.empty()) {
        recordedFrames.open(recordFramesPath, std::ios::binary);
        if (!recordedFrames) {
            std::cerr << "[Error] Cannot open LCD frame recording: " << recordFramesPath << '\n';
            return 1;
        }
        std::cout << "[Video] Recording decoded LCD frames as 240x160 RGB24 at 30 fps.\n";
    }

    size_t executedSteps = 0;
    size_t peripheralPhase = 0;
    EmulationClock emulationClock;
    uint64_t guestTicks = 0, idleTicks = 0, guiTicks = 0;
    std::cout << "[Timing] Mode: " << (autoTiming ? "automatic register clocks (experimental)" : "legacy instruction ratio") << '\n';
    if (autoTiming) std::cout << "[Timing] Instruction-ratio and MIPS pacing disabled; using modeled CPU time.\n";
    else std::cout << "[Timing] CPU steps per peripheral tick: " << cpuStepsPerTick
                   << "; GUI instruction-rate limit: " << clockMips << " MIPS\n";
    bool enteredSdram = false;

    auto getActiveFbPhys = [&]() -> u32 {
        u32 lcdsaddr1 = bus.getMmio(0x4D000014);
        if (lcdsaddr1 != 0) {
            return (lcdsaddr1 & 0x1FFFFFFF) << 1;
        }
        if (bus.isMmuEnabled()) {
            return 0x30310000;
        }
        return 0x30300000;
    };

    CrazyJackDisplayTransition displayTransition;
    auto displayLayout = [&]() {
        return resolveFramebufferLayout(displayProfile, automaticDisplay, getActiveFbPhys(),
            bus.getMmio(0x4D000000), bus.isLcd16Bpp(), bus.getFramebufferStride(), displayFormat, displayStride, displayTransition.active());
    };
    auto displayIs16Bpp = [&]() { return displayLayout().rgb565; };
    auto displayRowStride = [&]() { return displayLayout().stride; };
    auto presentFramebuffer = [&](u32 fb, bool rgb565, size_t stride) {
        if (bus.hostFrameCaptureActive()) {
            const auto& capture = bus.getHostFrameCapture();
            if (capture.ready()) display.updateFrameData(capture.data(), rgb565, stride, bus.getFramebufferHeight());
        } else display.updateFrame(bus.getSdramPtr(), fb, rgb565, stride, bus.getFramebufferHeight());
    };

    auto writePcProfile = [&]() {
        if (pcProfile.empty()) return;
        std::ofstream output("pc_profile.csv");
        output << "ttb,pc_page,samples\n";
        for (const auto& entry : pcProfile)
            output << "0x" << std::hex << entry.first.first << ",0x" << entry.first.second
                   << ',' << std::dec << entry.second << '\n';
    };

    auto saveSnapshot = [&]() {
        writePcProfile();
        const std::string stem = "snapshot_" + std::to_string(executedSteps);
        if (mmioProfile) bus.saveMmioProfile(stem + "_mmio.csv");
        const u32 fb = getActiveFbPhys();
        const bool is16bpp = displayIs16Bpp();
        const size_t fbSize = displayRowStride() * bus.getFramebufferHeight();
        if (fb >= ADDR_SDRAM_BASE && uint64_t(fb - ADDR_SDRAM_BASE) + fbSize <= ADDR_SDRAM_SIZE) {
            std::ofstream image(stem + ".raw", std::ios::binary);
            image.write(reinterpret_cast<const char*>(bus.getSdramPtr() + (fb - ADDR_SDRAM_BASE)), fbSize);
        }
        if (bus.getHostFrameCapture().ready()) {
            std::ofstream committed(stem + "_present.raw", std::ios::binary);
            committed.write(reinterpret_cast<const char*>(bus.getHostFrameCapture().data()), bus.getHostFrameCapture().size());
        }
        std::ofstream state(stem + ".txt");
        state << "steps=" << executedSteps << "\nPC=" << std::hex << cpu.getPC()
              << "\nCPSR=" << cpu.getCPSR() << "\nTTB=" << bus.getTtb()
              << "\nframebuffer=" << fb << "\nformat=" << (is16bpp ? "16bpp" : "12bpp")
              << "\nlcd_format=" << (bus.isLcd16Bpp() ? "16bpp" : "12bpp")
              << "\nstride=" << std::dec << displayRowStride()
              << "\nheight=" << bus.getFramebufferHeight()
              << "\ncoherent_frames=" << bus.getHostFrameCapture().completed()
              << "\npartial_sweeps=" << bus.getHostFrameCapture().abandoned()
              << "\nsynced_presentations=" << display.presentedFrames()
              << "\ndisplay_selection=" << (automaticDisplay ? "auto" : "explicit")
              << "\ndisplay_compatibility=" << displayLayout().compatibility
              << "\ncartridge_crc32=" << std::hex << cartridgeCrc << std::dec
              << "\nlcdcon1=" << std::hex << bus.getMmio(0x4D000000) << std::dec
              << "\ntiming=" << (autoTiming ? "auto" : "legacy")
              << "\nguest_ticks=" << guestTicks << "\nidle_ticks=" << idleTicks
              << "\ncartridge_preset=" << cartridgeSettings.name
              << "\ncpu_steps_per_tick=" << cpuStepsPerTick
              << "\ndisplay_override=" << (displayFormat != 0 || displayStride != 0)
              << "\naudio_rate=" << std::dec << bus.getAudioSampleRate()
              << "\nfclk=" << bus.getCpuClock() << "\nhclk=" << bus.getBusClock() << "\npclk=" << bus.getPeripheralClock()
              << "\nexecution_clock=" << cpu.getExecutionClock()
              << "\ncp15_control=" << std::hex << cpu.getControlRegister()
              << "\nmpllcon=" << std::hex << bus.getMmio(0x4c000004)
              << "\nclkslow=" << bus.getMmio(0x4c000010) << "\nclkdivn=" << bus.getMmio(0x4c000014) << std::dec
              << "\ndropped_samples=" << audio.getDroppedSamples()
              << "\naudio_submitted_frames=" << audio.getSubmittedFrames()
              << "\naudio_queued_frames=" << audio.getQueuedFrames()
              << "\naudio_empty_queue_events=" << audio.getEmptyQueueEvents()
              << "\ndma_source=" << std::hex << bus.getMmio(0x4B000098)
              << "\ndma_remaining=" << bus.getMmio(0x4B000094)
              << "\niismod=" << bus.getMmio(0x55000004)
              << "\niiscon=" << bus.getMmio(0x55000000)
              << "\ntcfg0=" << bus.getMmio(0x51000000)
              << "\ntcfg1=" << bus.getMmio(0x51000004)
              << "\ntcon=" << bus.getMmio(0x51000008)
              << "\ntcntb4=" << bus.getMmio(0x5100003c)
              << "\ntcnto4=" << bus.getMmio(0x51000040)
              << "\niispsr=" << bus.getMmio(0x55000008)
              << "\niiccon=" << bus.getMmio(0x54000000)
              << "\niicstat=" << bus.getMmio(0x54000004)
              << "\nucon0=" << bus.getMmio(0x50000004)
              << "\nsrcpnd=" << bus.getMmio(0x4A000000)
              << "\nintpnd=" << bus.getMmio(0x4A000010)
              << "\nintmsk=" << bus.getMmio(0x4A000008)
              << "\nsubsrcpnd=" << bus.getMmio(0x4A000018)
              << "\nintsubmsk=" << bus.getMmio(0x4A00001C) << '\n';
        for (int reg = 0; reg < 16; ++reg) state << 'r' << std::dec << reg << '=' << std::hex << cpu.getReg(reg) << '\n';
    };

    std::cout << "\n[OceanBlast] Starting ARM920T Steppingstone execution from 0x00000000..." << std::endl;

    using Clock = std::chrono::steady_clock;
    HostPacer hostPacer(gui && preciseHostWait);
    if (gui) std::cout << "[Timing] Host wait: " << (hostPacer.highResolution() ? "high-resolution timer" : "standard sleep") << '\n';
    auto lastFrame = Clock::now(), lastStats = lastFrame;
    auto paceStart = lastFrame;
    auto nextRecordedFrame = lastFrame;
    size_t paceSteps = 0;
    uint64_t paceTicks = 0, statsTicks = 0;
    size_t statsSteps = 0, presented = 0, changed = 0;
    uint32_t previousHash = 0;
    u32 previousFb = 0;
    size_t previousStride = 0;
    unsigned previousHeight = 0;
    bool previousIs16bpp = false;
    bool haveHash = false, previousCaptureReady = false;
    size_t nextDisplayProbe = 0;
    size_t nextVideoProbe = 1000000000;
    bool videoDecoderRecognized = false;
    std::ofstream metrics;
    if (profile && gui) { metrics.open("performance.csv"); metrics << "steps,seconds,present_fps,changed_fps,mips,pc,framebuffer,audio_rate,dropped_samples,audio_submitted_frames,audio_queued_frames,audio_empty_queue_events,fclk,pclk,cpu_steps_per_tick,guest_seconds,speed_percent,idle_ticks,coherent_frames,partial_sweeps,synchronized_presentations,frame_capture_active,capture_ready\n"; }
    while (!cpu.isHalted() && executedSteps < stepLimit && guestTicks < tickLimit) {
        if (gui && coherentFrames && displayFormat == 0 && !displayStride &&
            !videoDecoderRecognized && executedSteps >= nextVideoProbe) {
            if (findGuestExport(bus, "RV40toYUV420Transform")) {
                videoDecoderRecognized = true;
                bus.enableHostFrameCapture(true, true);
                std::cout << "[Display] Mapped RealVideo decoder: complete native video sweeps enabled.\n";
            }
            nextVideoProbe = executedSteps + 250000000;
        }
        if (automaticDisplay && displayProfile == DisplayProfile::CrazyJack && executedSteps >= nextDisplayProbe) {
            const u32 fb = getActiveFbPhys();
            const size_t stride = bus.getFramebufferStride();
            const bool fits = fb >= ADDR_SDRAM_BASE && uint64_t(fb - ADDR_SDRAM_BASE) + stride * bus.getFramebufferHeight() <= ADDR_SDRAM_SIZE;
            const bool wasActive = displayTransition.active();
            displayTransition.observe(fb, bus.getMmio(0x4D000000), stride,
                fits ? bus.getSdramPtr() + (fb - ADDR_SDRAM_BASE) : nullptr);
            if (wasActive != displayTransition.active())
                std::cout << "[Display] Crazy Jack game layout " << (displayTransition.active() ? "enabled" : "disabled")
                          << " at step " << executedSteps << '\n';
            nextDisplayProbe = executedSteps + 1000000;
        }
        if (nextInput < inputEvents.size() && executedSteps == inputEvents[nextInput].step) {
            bus.setButtonMask(inputEvents[nextInput].mask);
            ++nextInput;
        }
        if (snapshotInterval && executedSteps == nextSnapshot) {
            saveSnapshot();
            if (nextSnapshot > std::numeric_limits<size_t>::max() - snapshotInterval) snapshotInterval = 0;
            else nextSnapshot += snapshotInterval;
        }
        if (gui && (display.paused || (autoTiming ? guestTicks - guiTicks >= 20000 : executedSteps % 50000 == 0))) {
            guiTicks = guestTicks;
            display.processEvents();
            if (!display.isOpen()) {
                std::cout << "\n[OceanBlast] Display window closed by user." << std::endl;
                break;
            }
            if (inputScriptPath.empty()) {
                if (display.paused) { display.synchronizeButtons(); bus.setButtonMask(display.getButtonMask()); }
                else bus.setButtonMask(display.consumeButtonMask());
            }
            auto now = Clock::now();
            if (now - lastFrame >= std::chrono::milliseconds(16)) {
                const u32 fb = getActiveFbPhys();
                const bool is16bpp = displayIs16Bpp();
                const size_t fbSize = displayRowStride() * bus.getFramebufferHeight();
                if (fb >= ADDR_SDRAM_BASE && uint64_t(fb - ADDR_SDRAM_BASE) + fbSize <= ADDR_SDRAM_SIZE) {
                    uint32_t hash = 2166136261u;
                    const auto& capture = bus.getHostFrameCapture();
                    const bool latched = bus.hostFrameCaptureActive();
                    const uint8_t* source = latched ? capture.data() : bus.getSdramPtr() + (fb - ADDR_SDRAM_BASE);
                    const uint32_t* words = reinterpret_cast<const uint32_t*>(source);
                    for (size_t i = 0; i < fbSize / 4; ++i) hash = (hash ^ words[i]) * 16777619u;
                    const size_t stride = displayRowStride();
                    const bool redraw = !haveHash || hash != previousHash || fb != previousFb ||
                                        stride != previousStride || bus.getFramebufferHeight() != previousHeight || is16bpp != previousIs16bpp ||
                                        (latched && capture.ready() && !previousCaptureReady);
                    if (haveHash && redraw) ++changed;
                    previousHash = hash; previousFb = fb; previousStride = stride;
                    previousCaptureReady = latched && capture.ready();
                    previousHeight = bus.getFramebufferHeight();
                    previousIs16bpp = is16bpp; haveHash = true;
                    if (redraw && (!latched || capture.ready())) { presentFramebuffer(fb, is16bpp, stride); ++presented; }
                }
                lastFrame = now;
            }
            if (recordedFrames.is_open()) {
                constexpr auto framePeriod = std::chrono::nanoseconds(33333333);
                while (now >= nextRecordedFrame) {
                    const auto& pixels = display.framePixels();
                    const unsigned sourceHeight = display.frameSourceHeight();
                    if (!sourceHeight || pixels.size() < static_cast<size_t>(LCD_WIDTH) * sourceHeight) {
                        std::cerr << "[Video] Invalid decoded LCD frame; stopping recording.\n";
                        recordedFrames.close();
                        break;
                    }
                    for (int y = 0; y < LCD_HEIGHT; ++y) {
                        const size_t sourceY = static_cast<size_t>(y) * sourceHeight / LCD_HEIGHT;
                        for (int x = 0; x < LCD_WIDTH; ++x) {
                            const uint32_t pixel = pixels[sourceY * LCD_WIDTH + x];
                            const size_t output = (static_cast<size_t>(y) * LCD_WIDTH + x) * 3;
                            recordedRgb[output] = static_cast<uint8_t>(pixel >> 16);
                            recordedRgb[output + 1] = static_cast<uint8_t>(pixel >> 8);
                            recordedRgb[output + 2] = static_cast<uint8_t>(pixel);
                        }
                    }
                    recordedFrames.write(reinterpret_cast<const char*>(recordedRgb.data()),
                                         static_cast<std::streamsize>(recordedRgb.size()));
                    if (!recordedFrames) {
                        std::cerr << "[Video] Failed while writing LCD frames.\n";
                        recordedFrames.close();
                        break;
                    }
                    ++recordedFrameCount;
                    nextRecordedFrame += framePeriod;
                }
            }
            const double seconds = std::chrono::duration<double>(now - lastStats).count();
            if (seconds >= 1.0) {
                const double mips = (executedSteps - statsSteps) / seconds / 1000000.0;
                const double speed = (guestTicks - statsTicks) * 100.0 / EmulationClock::ticksPerSecond / seconds;
                std::ostringstream title;
                title << "OceanBlast | Display " << std::fixed << std::setprecision(1) << presented / seconds
                      << " FPS | Changes " << changed / seconds << "/s | " << mips << " MIPS"
                      << " | CPU " << cpuStepsPerTick << "x"
                      << " | Speed " << speed << "%" << (autoTiming ? " Auto" : "")
                      << " | PC " << std::hex << cpu.getPC() << " | FB " << getActiveFbPhys()
                      << " | Audio " << std::dec << bus.getAudioSampleRate() << " Hz | Queue " << audio.getQueuedFrames() << "f | Empty " << audio.getEmptyQueueEvents() << " | Drop " << audio.getDroppedSamples()
                      << (display.usesSyncedPresentation() ? " | VSync" : " | GDI")
                      << (display.paused ? " | PAUSE" : "");
                display.setTitle(title.str());
                if (metrics) { metrics << executedSteps << ',' << seconds << ',' << presented / seconds << ',' << changed / seconds << ',' << mips << ',' << cpu.getPC() << ',' << getActiveFbPhys() << ',' << bus.getAudioSampleRate() << ',' << audio.getDroppedSamples() << ',' << audio.getSubmittedFrames() << ',' << audio.getQueuedFrames() << ',' << audio.getEmptyQueueEvents() << ',' << bus.getCpuClock() << ',' << bus.getPeripheralClock() << ',' << cpuStepsPerTick << ',' << double(guestTicks) / EmulationClock::ticksPerSecond << ',' << speed << ',' << idleTicks << ',' << bus.getHostFrameCapture().completed() << ',' << bus.getHostFrameCapture().abandoned() << ',' << display.presentedFrames() << ',' << bus.hostFrameCaptureActive() << ',' << bus.getHostFrameCapture().ready() << '\n'; metrics.flush(); }
                lastStats = now; statsSteps = executedSteps; statsTicks = guestTicks; presented = changed = 0;
            }
            if (display.snapshot) {
                display.snapshot = false;
                saveSnapshot();
            }
            if (display.paused) { paceStart = Clock::now(); paceSteps = executedSteps; paceTicks = guestTicks; }
            if (display.paused && !display.singleStep) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            display.singleStep = false;
            if (!display.paused && clockMips > 0) {
                const auto target = paceStart + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(autoTiming ? double(guestTicks - paceTicks) / EmulationClock::ticksPerSecond : (executedSteps - paceSteps) / (clockMips * 1000000.0)));
                const auto current = Clock::now();
                if (target > current) hostPacer.waitUntil(target);
                else if (current - target > std::chrono::milliseconds(250)) { paceStart = current; paceSteps = executedSteps; paceTicks = guestTicks; }
            }
        }

        if (autoTiming && cpu.isWaitingForInterrupt() && !bus.hasPendingIrq()) {
            const uint64_t amount = std::min(bus.ticksUntilEvent(), tickLimit - guestTicks);
            bus.tick(amount);
            emulationClock.advanceIdle(amount);
            guestTicks += amount; idleTicks += amount;
            continue;
        }
        if (executionBatch > 1 && enteredSdram && !cpu.isWaitingForInterrupt() && !debug && !trace && !traceRange && !faultLog &&
            !pcProfileInterval && !display.paused && stepLimit - executedSteps > 30) {
            size_t budget = std::min<size_t>(executionBatch, stepLimit - executedSteps - 30);
            if (nextInput < inputEvents.size()) budget = std::min(budget, inputEvents[nextInput].step - executedSteps);
            if (snapshotInterval) budget = std::min(budget, nextSnapshot - executedSteps);
            if (automaticDisplay && displayProfile == DisplayProfile::CrazyJack)
                budget = std::min(budget, nextDisplayProbe - executedSteps);
            if (gui && !autoTiming) budget = std::min(budget, size_t(50000 - executedSteps % 50000));
            if (!autoTiming && tickLimit != UINT64_MAX) {
                const uint64_t remaining = tickLimit - guestTicks;
                // Do not batch across the last peripheral-time boundary.
                const uint64_t stepsToLimit = legacyStepsUntilTickLimit(remaining, cpuStepsPerTick, peripheralPhase);
                budget = std::min<uint64_t>(budget, stepsToLimit);
            }
            if (budget) {
                if (autoTiming) {
                    const uint64_t guiDeadline = gui && guiTicks <= UINT64_MAX - 20000 ? guiTicks + 20000 : UINT64_MAX;
                    executedSteps += runClockedInstructions(cpu, bus, emulationClock, budget,
                        std::min(tickLimit, guiDeadline), guestTicks);
                } else {
                    executedSteps += runLegacyInstructions(cpu, budget, cpuStepsPerTick, peripheralPhase, guestTicks);
                }
                continue;
            }
        }
        u32 currentPC = cpu.getPC();
        if (pcProfileInterval && executedSteps == nextPcProfile) {
            ++pcProfile[{bus.getTtb(), currentPC & ~0xfffu}];
            if (nextPcProfile > std::numeric_limits<size_t>::max() - pcProfileInterval) pcProfileInterval = 0;
            else nextPcProfile += pcProfileInterval;
        }

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
        if (debug && currentPC == 0x4012a608) {
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
        if (debug && !hit93540 && currentPC == 0x93540) {
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

        if (traceRange && currentPC >= traceStart && currentPC <= traceEnd) {
            std::cout << "[PC TRACE] step=" << executedSteps << " PC=0x" << std::hex << currentPC
                      << " TTB=0x" << bus.getTtb() << " " << cpu.disassembleCurrentARM()
                      << std::dec << '\n';
            cpu.dumpState();
        }
        if (autoTiming) {
            const u32 fclk = cpu.getExecutionClock();
            cpu.step(0);
            const uint64_t amount = emulationClock.advance(cpu.getLastCycles(), fclk);
            bus.tick(amount);
            guestTicks = emulationClock.elapsedTicks();
        } else {
            const bool advance = peripheralPhase == 0;
            cpu.step(advance ? 1 : 0);
            guestTicks += advance;
            if (++peripheralPhase == cpuStepsPerTick) peripheralPhase = 0;
        }
        executedSteps++;
    }

    if (autoTiming) saveSnapshot();
    std::cout << "[Timing] Modeled seconds: " << double(guestTicks) / EmulationClock::ticksPerSecond << "; idle seconds: " << double(idleTicks) / EmulationClock::ticksPerSecond << '\n';
    std::cout << "\n[OceanBlast] Execution finished after " << executedSteps << " instructions." << std::endl;
    if (recordedFrames.is_open()) recordedFrames.close();
    if (recordedFrameCount) std::cout << "[Video] Recorded " << recordedFrameCount << " LCD frames to " << recordFramesPath << '\n';
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
        // These are physical registers; read32 interprets its argument as a
        // virtual address once the guest enables its MMU.
        std::cout << "LCDCON1:   0x" << std::hex << bus.getMmio(0x4D000000) << std::endl;
        std::cout << "LCDCON2:   0x" << std::hex << bus.getMmio(0x4D000004) << std::endl;
        std::cout << "LCDCON3:   0x" << std::hex << bus.getMmio(0x4D000008) << std::endl;
        std::cout << "LCDCON4:   0x" << std::hex << bus.getMmio(0x4D00000C) << std::endl;
        std::cout << "LCDCON5:   0x" << std::hex << bus.getMmio(0x4D000010) << std::endl;
        std::cout << "LCDSADDR1: 0x" << std::hex << bus.getMmio(0x4D000014) << std::endl;
        std::cout << "LCDSADDR2: 0x" << std::hex << bus.getMmio(0x4D000018) << std::endl;
        std::cout << "LCDSADDR3: 0x" << std::hex << bus.getMmio(0x4D00001C) << std::dec << std::endl;
        const u32 activeFb = getActiveFbPhys();
        std::cout << "Active framebuffer PA: 0x" << std::hex << activeFb << std::dec << std::endl;
        const bool is16bpp = displayIs16Bpp();
        const size_t fbSize = displayRowStride() * bus.getFramebufferHeight();
        if (activeFb >= ADDR_SDRAM_BASE && activeFb - ADDR_SDRAM_BASE <= ADDR_SDRAM_SIZE - fbSize) {
            const u8* frame = sdram + activeFb - ADDR_SDRAM_BASE;
            size_t nonzero = 0;
            for (size_t i = 0; i < fbSize; ++i) nonzero += frame[i] != 0;
            std::cout << "Active framebuffer nonzero bytes: " << nonzero << "/" << fbSize
                      << " (" << (is16bpp ? "16bpp RGB565" : "12bpp packed") << ")" << std::endl;
            std::ofstream active("fb_active.raw", std::ios::binary);
            active.write(reinterpret_cast<const char*>(frame), fbSize);
        }

        const auto& capture = bus.getHostFrameCapture();
        std::cout << "[Display] Complete video sweeps: " << capture.completed()
                  << "; abandoned partial sweeps: " << capture.abandoned()
                  << "; captured bytes: " << capture.size()
                  << "; synchronized presentations: " << display.presentedFrames() << '\n';
        if (capture.ready()) {
            std::ofstream committed("fb_present.raw", std::ios::binary);
            committed.write(reinterpret_cast<const char*>(capture.data()), capture.size());
        }
        // Dump Framebuffer memory and full SDRAM
        std::ofstream fb0("fb_30300000.raw", std::ios::binary);
        if (fb0.is_open()) fb0.write(reinterpret_cast<const char*>(sdram + 0x300000), 153600);
        std::ofstream fb1("fb_30310000.raw", std::ios::binary);
        if (fb1.is_open()) fb1.write(reinterpret_cast<const char*>(sdram + 0x310000), 153600);
        std::ofstream sdr("sdram.bin", std::ios::binary);
        if (sdr.is_open()) sdr.write(reinterpret_cast<const char*>(sdram), ADDR_SDRAM_SIZE);
        std::cout << "\n[Debug] Dumped sdram.bin (" << ADDR_SDRAM_SIZE / (1024 * 1024) << " MB)" << std::endl;

        std::cout << "[Debug] Memory at PA 0x30204ba0:" << std::hex;
        for (u32 o = 0; o < 32; o += 4) {
            u32 val = 0;
            std::memcpy(&val, sdram + (0x00204ba0 + o), 4);
            std::cout << "  [+0x" << o << "]=0x" << val;
        }
        std::cout << std::dec << std::endl;
    }

    if (gui && display.isOpen() && !exitOnLimit) {
        u32 fbPhys = getActiveFbPhys();
        presentFramebuffer(fbPhys, displayIs16Bpp(), displayRowStride());
        std::cout << "[Display] Emulation paused. Press ESC or close the window to exit." << std::endl;
        while (display.isOpen()) {
            display.processEvents();
            presentFramebuffer(getActiveFbPhys(), displayIs16Bpp(), displayRowStride());
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    writePcProfile();
    if (mmioProfile && !bus.saveMmioProfile("mmio_profile.csv")) {
        std::cerr << "[Error] Cannot save MMIO profile.\n"; return 1;
    }
    if (!nvramPath.empty() && !bus.saveEeprom(nvramPath)) {
        std::cerr << "[Error] Cannot save NVRAM file." << std::endl;
        return 1;
    }
    return 0;
}
