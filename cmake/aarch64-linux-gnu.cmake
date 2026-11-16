set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

if(NOT DEFINED SDL2_DIR)
  set(SDL2_DIR "/usr/lib/aarch64-linux-gnu/cmake/SDL2" CACHE PATH "ARM64 SDL2 CMake config")
endif()

if(NOT DEFINED CMAKE_CROSSCOMPILING_EMULATOR)
  find_program(OCEANBLAST_QEMU_AARCH64 qemu-aarch64-static)
  if(OCEANBLAST_QEMU_AARCH64)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${OCEANBLAST_QEMU_AARCH64}" CACHE FILEPATH "Emulator for aarch64 tests")
  endif()
endif()
