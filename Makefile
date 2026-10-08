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

src/main.o: src/display/display.h src/display/launcher.h src/core/input_script.h src/audio/audio.h src/audio/resampler.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
src/audio/audio_win32.o: src/audio/audio.h src/audio/resampler.h
src/cpu/arm920t.o: src/cpu/arm920t.h src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h src/core/types.h
src/memory/bus.o: src/memory/bus.h src/memory/i2c_eeprom.h src/memory/timer4.h
src/display/display_win32.o: src/display/display.h src/display/framebuffer.h

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp %.o,$^) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe build/i2c_eeprom.exe build/framebuffer.exe build/timer4.exe build/audio_clock.exe
	./build/cpu_regression.exe
	./build/input_test.exe
	./build/audio_resampler.exe
	./build/uart_interrupt.exe
	./build/i2c_eeprom.exe
	./build/framebuffer.exe
	./build/timer4.exe
	./build/audio_clock.exe

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
	rm -f $(OBJS) $(TARGET) build/cpu_regression.exe build/input_test.exe build/audio_resampler.exe build/uart_interrupt.exe
	rm -f build/i2c_eeprom.exe
	rm -f build/framebuffer.exe build/timer4.exe build/audio_clock.exe

.PHONY: all clean test
