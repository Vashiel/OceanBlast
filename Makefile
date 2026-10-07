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

src/main.o: src/display/launcher.h

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: build/cpu_regression.exe build/input_test.exe
	./build/cpu_regression.exe
	./build/input_test.exe

build/cpu_regression.exe: tests/cpu_regression.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/input_test.exe: tests/input_test.cpp src/cpu/arm920t.cpp src/memory/bus.cpp src/cartridge/cart_parser.cpp src/display/display_win32.cpp src/audio/audio_win32.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET) build/cpu_regression.exe build/input_test.exe

.PHONY: all clean test
