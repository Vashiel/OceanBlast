CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g -Isrc

TARGET = bin/oceanblast.exe

SRCS = src/main.cpp \
       src/memory/bus.cpp \
       src/cpu/arm920t.cpp \
       src/cartridge/cart_parser.cpp \
       src/display/display_win32.cpp

OBJS = $(SRCS:.cpp=.o)
LDFLAGS = -lgdi32 -luser32

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
