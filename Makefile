CXX ?= g++
WINDRES ?= windres
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -g -Isrc

TARGET = build/oceanblast-dev.exe

SRCS = src/main.cpp \
       src/memory/bus.cpp \
       src/cpu/arm920t.cpp \
       src/cartridge/cart_parser.cpp \
       src/display/display_win32.cpp \
       src/audio/audio_win32.cpp

HEADERS = src/core/types.h \
          src/core/guest_symbols.h \
          src/core/input_script.h \
          src/core/emulation_clock.h \
          src/core/execution_batch.h \
          src/core/host_pacer.h \
          src/core/cartridge_settings.h \
          src/core/cartridge_language.h \
          src/core/button_pulses.h \
          src/core/mp3_storage.h \
          src/cpu/arm920t.h \
          src/memory/bus.h \
          src/memory/i2c_eeprom.h \
          src/memory/timer4.h \
          src/memory/clock_tree.h \
          src/memory/usb_mass_storage.h \
          src/memory/usb_ohci.h \
          src/cartridge/cart_parser.h \
          src/display/display.h \
          src/display/framebuffer.h \
          src/display/frame_latch.h \
          src/display/display_profile.h \
          src/display/presenter_win32.h \
          src/display/console_skin_win32.h \
          src/display/launcher.h \
          src/display/mp3_manager_win32.h \
          src/display/windows_input.h \
          src/display/input_mapping.h \
          src/audio/audio.h \
          src/audio/resampler.h

OBJS = build/unity.o
RES_OBJ = build/oceanblast_res.o
LDFLAGS = -lgdi32 -luser32 -lwinmm -lcomdlg32 -lcomctl32 -lshell32 -lole32 -ldinput8 -ldxguid -ld3d11 -ldxgi -ld3dcompiler -lgdiplus

all: $(TARGET)

build/unity.cpp: Makefile
	@mkdir -p build
	@printf '#include "../src/memory/bus.cpp"\n#include "../src/cpu/arm920t.cpp"\n#include "../src/cartridge/cart_parser.cpp"\n#include "../src/display/display_win32.cpp"\n#include "../src/audio/audio_win32.cpp"\n#include "../src/main.cpp"\n' > $@

build/unity.o: build/unity.cpp $(SRCS) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) --param inline-unit-growth=400 --param large-unit-insns=100000 --param max-inline-insns-single=1000 --param max-inline-insns-auto=200 -c $< -o $@

build/framebuffer.exe: src/display/display_profile.h

$(RES_OBJ): src/display/oceanblast.rc assets/oceanblast-logo.png assets/oceanblast-symbol.png
	@mkdir -p build
	$(WINDRES) src/display/oceanblast.rc -O coff -o $@

$(TARGET): $(OBJS) $(RES_OBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(RES_OBJ) $(LDFLAGS)

test: build/frame_latch.exe build/emulation_timing.exe build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe
	./build/mp3_storage.exe
	./build/usb_storage.exe
	./build/cartridge_settings.exe
	./build/cartridge_loading.exe
	./build/frame_latch.exe
	./build/emulation_timing.exe
	./build/cpu_regression.exe
	./build/input_test.exe
	./build/audio_resampler.exe
	./build/uart_interrupt.exe
	./build/i2c_eeprom.exe
	./build/framebuffer.exe
	./build/timer4.exe
	./build/audio_clock.exe
	./build/dma_audio_stream.exe

build/audio_clock.exe: tests/audio_clock.cpp src/memory/bus.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/timer4.exe: tests/timer4.cpp src/memory/bus.cpp src/memory/bus.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/framebuffer.exe: tests/framebuffer.cpp src/display/framebuffer.h src/memory/bus.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/i2c_eeprom.exe: tests/i2c_eeprom.cpp src/memory/bus.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/uart_interrupt.exe: tests/uart_interrupt.cpp src/memory/bus.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/audio_resampler.exe: tests/audio_resampler.cpp src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/cpu_regression.exe: tests/cpu_regression.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp src/core/input_script.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/display/framebuffer.h src/display/display.h src/display/windows_input.h src/display/input_mapping.h src/audio/audio.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^) $(LDFLAGS)

build/input_test.exe: tests/input_test.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/display/framebuffer.h src/display/display.h src/display/windows_input.h src/display/input_mapping.h src/audio/audio.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp %.o,$^) $(LDFLAGS)

clean:
	rm -f build/emulation_timing.exe
	rm -f build/runtime_probe.exe
	rm -f build/capture_audio.exe
	rm -f build/unity.cpp $(OBJS) $(RES_OBJ) $(TARGET) build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe
	rm -f build/i2c_eeprom.exe
	rm -f build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe

.PHONY: all clean test

test: build/usb_storage.exe

test: build/mp3_storage.exe

build/mp3_storage.exe: tests/mp3_storage.cpp src/core/mp3_storage.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/usb_storage.exe: tests/usb_storage.cpp src/memory/usb_ohci.h src/memory/usb_mass_storage.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<


build/sample_native.exe: tools/sample_native.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/sdl_surface_probe.exe: tools/sdl_surface_probe.cpp src/cpu/arm920t.cpp src/memory/bus.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/probe_host_wait.exe: tools/probe_host_wait.cpp src/core/host_pacer.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/scene_work_probe.exe: tools/scene_work_probe.cpp tools/kernel_jiffies.h tools/guest_ram.h src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/core/input_script.h src/memory/clock_tree.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/codec_probe.exe: tools/codec_probe.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/core/input_script.h src/memory/clock_tree.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/cpu_regression.exe build/input_test.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe build/runtime_probe.exe build/capture_audio.exe: src/memory/clock_tree.h
build/emulation_timing.exe: tools/kernel_jiffies.h tools/guest_ram.h

build/runtime_probe.exe: tools/runtime_probe.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/capture_audio.exe: tools/capture_audio.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/dma_audio_stream.exe: tests/dma_audio_stream.cpp src/memory/bus.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

# Register-clock execution and bounded CPU idle advancement.
build/emulation_timing.exe: src/core/emulation_clock.h src/core/execution_batch.h
build/cpu_regression.exe build/input_test.exe: src/cpu/arm920t.h
build/emulation_timing.exe: tests/emulation_timing.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/memory/clock_tree.h src/memory/timer4.h src/memory/i2c_eeprom.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

# Windows/GDI presentation check; opens and closes its own bounded test window.
build/display_height_win32.exe: src/display/console_skin_win32.h src/display/presenter_win32.h src/display/windows_input.h src/display/input_mapping.h tests/display_height_win32.cpp src/display/display_win32.cpp src/display/display.h src/display/framebuffer.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^) $(LDFLAGS)

build/frame_latch.exe: tests/frame_latch.cpp src/memory/bus.cpp src/memory/bus.h src/display/frame_latch.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

# Window modes and controls; opens and closes its own test window.
build/console_skin_win32.exe: tests/console_skin_win32.cpp src/display/display_win32.cpp src/display/display.h src/display/console_skin_win32.h src/display/presenter_win32.h src/display/windows_input.h src/display/input_mapping.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^) $(LDFLAGS)

test-windows: build/display_height_win32.exe build/console_skin_win32.exe build/launcher_defaults_win32.exe build/audio_output_win32.exe build/mp3_manager_win32.exe
	./build/mp3_manager_win32.exe
	./build/audio_output_win32.exe
	./build/launcher_defaults_win32.exe
	./build/display_height_win32.exe
	./build/console_skin_win32.exe
	./build/console_skin_win32.exe --gdi

.PHONY: test-windows

build/audio_output_win32.exe: tests/audio_output_win32.cpp src/audio/audio_win32.cpp src/audio/audio.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^) -lwinmm

# USB bus integration participates in every memory-backed regression target.
build/cpu_regression.exe build/input_test.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe build/runtime_probe.exe build/capture_audio.exe build/frame_latch.exe build/emulation_timing.exe: src/memory/usb_ohci.h src/memory/usb_mass_storage.h

build/cartridge_settings.exe: tests/cartridge_settings.cpp src/core/cartridge_settings.h src/core/cartridge_language.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<
test: build/cartridge_settings.exe
test: build/cartridge_loading.exe

build/cartridge_loading.exe: tests/cartridge_loading.cpp src/memory/bus.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/launcher_defaults_win32.exe: tests/launcher_defaults_win32.cpp src/display/launcher.h src/display/mp3_manager_win32.h src/core/mp3_storage.h src/display/windows_input.h src/display/input_mapping.h $(RES_OBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/launcher_defaults_win32.cpp $(RES_OBJ) $(LDFLAGS)

build/mp3_manager_win32.exe: tests/mp3_manager_win32.cpp src/display/launcher.h src/display/mp3_manager_win32.h src/core/mp3_storage.h src/display/windows_input.h src/display/input_mapping.h $(RES_OBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ tests/mp3_manager_win32.cpp $(RES_OBJ) $(LDFLAGS)
