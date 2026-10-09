CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g -Isrc

TARGET = bin/oceanblast.exe

SRCS = src/main.cpp \
       src/memory/bus.cpp \
       src/cpu/arm920t.cpp \
       src/cartridge/cart_parser.cpp \
       src/display/display_win32.cpp \
       src/audio/audio_win32.cpp

OBJS = $(SRCS:.cpp=.o)
LDFLAGS = -lgdi32 -luser32 -lwinmm -lcomdlg32

all: $(TARGET)

src/main.o: src/cpu/arm920t.h src/display/display.h src/display/launcher.h src/core/input_script.h src/audio/audio.h src/audio/resampler.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
src/audio/audio_win32.o: src/audio/audio.h src/audio/resampler.h
src/cpu/arm920t.o: src/cpu/arm920t.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/core/types.h
src/memory/bus.o: src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
src/display/display_win32.o: src/display/display.h src/display/framebuffer.h
src/main.o build/framebuffer.exe: src/display/display_profile.h

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp %.o,$^) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: build/emulation_timing.exe build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe
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

build/cpu_regression.exe: tests/cpu_regression.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp src/core/input_script.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/display/framebuffer.h src/display/display.h src/audio/audio.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^) $(LDFLAGS)

build/input_test.exe: tests/input_test.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/display/framebuffer.h src/display/display.h src/audio/audio.h src/audio/resampler.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp %.o,$^) $(LDFLAGS)

clean:
	rm -f build/emulation_timing.exe
	rm -f build/runtime_probe.exe
	rm -f build/capture_audio.exe
	rm -f $(OBJS) $(TARGET) build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe
	rm -f build/i2c_eeprom.exe
	rm -f build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe

.PHONY: all clean test

build/sample_native.exe: tools/sample_native.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/probe_host_wait.exe: tools/probe_host_wait.cpp src/core/host_pacer.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/scene_work_probe.exe: tools/scene_work_probe.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/core/input_script.h src/memory/clock_tree.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

build/codec_probe.exe: tools/codec_probe.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/core/input_script.h src/memory/clock_tree.h src/memory/timer4.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

src/main.o src/cpu/arm920t.o src/memory/bus.o build/cpu_regression.exe build/input_test.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe build/dma_audio_stream.exe build/runtime_probe.exe build/capture_audio.exe: src/memory/clock_tree.h

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
src/main.o build/emulation_timing.exe: src/core/emulation_clock.h
src/main.o build/emulation_timing.exe: src/core/execution_batch.h
src/main.o: src/core/host_pacer.h
build/cpu_regression.exe build/input_test.exe: src/cpu/arm920t.h
build/emulation_timing.exe: tests/emulation_timing.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cpu/arm920t.h src/memory/bus.h src/memory/clock_tree.h src/memory/timer4.h src/memory/i2c_eeprom.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)
