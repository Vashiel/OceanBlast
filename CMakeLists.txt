cmake_minimum_required(VERSION 3.20)
project(oceanblast CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OCEANBLAST_BUILD_TESTS "Build core interpreter and peripheral tests" ON)
option(OCEANBLAST_STATIC_STDLIB "Link libgcc and libstdc++ statically on GNU/Linux" ON)

add_library(oceanblast_core STATIC
  src/cpu/arm920t.cpp
  src/memory/bus.cpp
  src/cartridge/cart_parser.cpp
)
target_include_directories(oceanblast_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oceanblast_core PRIVATE -O2)
endif()

find_package(Threads REQUIRED)

if(WIN32)
  enable_language(RC)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets" OCEANBLAST_ASSETS)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/oceanblast.rc"
    "101 RCDATA \"${OCEANBLAST_ASSETS}/oceanblast-logo.png\"\n102 RCDATA \"${OCEANBLAST_ASSETS}/oceanblast-symbol.png\"\n")
  add_executable(oceanblast
    "${CMAKE_CURRENT_BINARY_DIR}/oceanblast.rc"
    src/main.cpp
    src/display/display_win32.cpp
    src/audio/audio_win32.cpp
  )
  target_compile_definitions(oceanblast PRIVATE OCEANBLAST_LEGACY_WINDOWS=1 _WIN32_WINNT=0x0601 WINVER=0x0601)
  target_link_libraries(oceanblast PRIVATE oceanblast_core Threads::Threads
    gdi32 user32 winmm comdlg32 comctl32 shell32 ole32 dinput8 dxguid gdiplus)
else()
  find_package(SDL2 REQUIRED)
  add_executable(oceanblast
    src/main.cpp
    src/display/display_sdl.cpp
    src/audio/audio_sdl.cpp
  )
  if(TARGET SDL2::SDL2)
    target_link_libraries(oceanblast PRIVATE oceanblast_core SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS})
  else()
    target_include_directories(oceanblast PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(oceanblast PRIVATE oceanblast_core ${SDL2_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS})
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oceanblast PRIVATE -O2)
  if(OCEANBLAST_STATIC_STDLIB AND UNIX AND NOT APPLE)
    target_link_options(oceanblast PRIVATE -static-libgcc -static-libstdc++)
  endif()
endif()

if(OCEANBLAST_BUILD_TESTS)
  enable_testing()
  set(OCEANBLAST_PORTABLE_TESTS
    audio_clock
    audio_resampler
    cartridge_loading
    cartridge_nvram
    cartridge_settings
    cpu_regression
    dma_audio_stream
    emulation_timing
    execution_boundary
    frame_latch
    framebuffer
    i2c_eeprom
    input_test
    machine
    mmu_translation
    mp3_storage
    timer4
    uart_interrupt
    usb_storage
  )
  foreach(test_name IN LISTS OCEANBLAST_PORTABLE_TESTS)
    add_executable(${test_name} "tests/${test_name}.cpp")
    target_link_libraries(${test_name} PRIVATE oceanblast_core Threads::Threads)
    if(test_name STREQUAL "input_test")
      if(WIN32)
        target_sources(input_test PRIVATE src/display/display_win32.cpp)
        target_compile_definitions(input_test PRIVATE OCEANBLAST_LEGACY_WINDOWS=1 _WIN32_WINNT=0x0601 WINVER=0x0601)
        target_link_libraries(input_test PRIVATE gdi32 user32 winmm comdlg32 comctl32 shell32 ole32 dinput8 dxguid gdiplus)
      else()
        target_sources(input_test PRIVATE src/display/display_sdl.cpp)
        if(TARGET SDL2::SDL2)
          target_link_libraries(input_test PRIVATE SDL2::SDL2 ${CMAKE_DL_LIBS})
        else()
          target_include_directories(input_test PRIVATE ${SDL2_INCLUDE_DIRS})
          target_link_libraries(input_test PRIVATE ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
        endif()
      endif()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${test_name} PRIVATE -O2)
      if(OCEANBLAST_STATIC_STDLIB AND UNIX AND NOT APPLE)
        target_link_options(${test_name} PRIVATE -static-libgcc -static-libstdc++)
      endif()
    endif()
    add_test(NAME ${test_name}
      COMMAND ${test_name}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endforeach()
endif()
