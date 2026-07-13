cmake_minimum_required(VERSION 3.20)
project(famicom_rf_decoder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math")

option(FAMITV_TOOLS_ONLY "Build only the offline DSP tools; skip famidec if libhackrf/SDL2 are missing" OFF)

find_package(PkgConfig REQUIRED)

# Optional deps -- the DSP library and the offline tools build without them.
pkg_check_modules(HACKRF IMPORTED_TARGET libhackrf)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
endif()

# One interface target for "whichever SDL2 we found" (CMake config or pkg-config).
add_library(sdl2_iface INTERFACE)
if(SDL2_FOUND)
    if(TARGET SDL2::SDL2)
        target_link_libraries(sdl2_iface INTERFACE SDL2::SDL2)
    elseif(TARGET PkgConfig::SDL2)
        target_link_libraries(sdl2_iface INTERFACE PkgConfig::SDL2)
        # Let `#include <SDL2/SDL.h>` resolve by adding the parent include dir.
        foreach(d ${SDL2_INCLUDE_DIRS})
            get_filename_component(_parent "${d}" DIRECTORY)
            target_include_directories(sdl2_iface INTERFACE "${_parent}")
        endforeach()
    else()
        # SDL2_FOUND is set but neither expected target exists (e.g. a config
        # package exporting a differently-named target) -- fall back to the
        # classic SDL2_LIBRARIES/SDL2_INCLUDE_DIRS variables.
        message(WARNING "SDL2_FOUND but no SDL2::SDL2 or PkgConfig::SDL2 target; "
            "falling back to SDL2_LIBRARIES/SDL2_INCLUDE_DIRS")
        if(SDL2_LIBRARIES)
            target_link_libraries(sdl2_iface INTERFACE ${SDL2_LIBRARIES})
            target_include_directories(sdl2_iface INTERFACE ${SDL2_INCLUDE_DIRS})
            # Let `#include <SDL2/SDL.h>` resolve by adding the parent include dir.
            foreach(d ${SDL2_INCLUDE_DIRS})
                get_filename_component(_parent "${d}" DIRECTORY)
                target_include_directories(sdl2_iface INTERFACE "${_parent}")
            endforeach()
        else()
            message(WARNING "No usable SDL2 libraries found either; disabling SDL2")
            set(SDL2_FOUND FALSE)
        endif()
    endif()
endif()

# Where the Nestopia libretro core + libretro.h live (override with -D).
set(NESTOPIA_DIR "${CMAKE_SOURCE_DIR}/../third_party/nestopia"
    CACHE PATH "Path to the built nestopia libretro checkout")
set(LIBRETRO_INC "${NESTOPIA_DIR}/libretro/libretro-common/include")

# --- Core DSP library (no external deps) ---
add_library(fam_dsp STATIC src/dsp/ntsc_decoder.cpp)
target_include_directories(fam_dsp PUBLIC src)

# --- Golden test (DSP only) ---
enable_testing()
add_executable(synth_ntsc tests/synth_ntsc.cpp)
target_link_libraries(synth_ntsc PRIVATE fam_dsp)
add_test(NAME synth_ntsc COMMAND synth_ntsc)

# --- Clean-room engines (no external deps; see docs/PLATFORM.md) ---
add_library(famemu_nes STATIC
    engines/nes/apu.cpp
    engines/nes/cpu.cpp engines/nes/ppu.cpp engines/nes/famemu_nes_core.cpp)
target_include_directories(famemu_nes PUBLIC engines engines/include)

add_library(famemu_snes STATIC engines/snes/cpu65816.cpp engines/snes/sppu.cpp
    engines/snes/famemu_snes_core.cpp
    engines/snes/spc700.cpp engines/snes/sdsp.cpp)
target_include_directories(famemu_snes PUBLIC engines engines/include)

add_executable(snes_dump engines/snes/tools/snes_dump.cpp)
target_link_libraries(snes_dump PRIVATE famemu_snes)

add_executable(nestest_harness engines/nes/tests/nestest_main.cpp)
target_link_libraries(nestest_harness PRIVATE famemu_nes)

add_executable(famemu_dump engines/nes/tools/famemu_dump.cpp)
target_link_libraries(famemu_dump PRIVATE famemu_nes)

add_executable(blargg_harness engines/nes/tests/blargg_main.cpp)
target_link_libraries(blargg_harness PRIVATE famemu_nes)

add_executable(state_test engines/nes/tests/state_test.cpp)
target_link_libraries(state_test PRIVATE famemu_nes)
if(EXISTS "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/nestest.nes")
    add_test(NAME state_roundtrip COMMAND state_test
        "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/nestest.nes")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/nestest.nes")
    add_test(NAME nestest COMMAND nestest_harness
        "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/nestest.nes"
        "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/nestest.log")
endif()
# blargg $6000-protocol test ROMs (exit code = ROM's own verdict).
foreach(_rom ppu_vbl_basics apu_1-len_ctr apu_2-len_table apu_3-irq_flag apu_4-jitter)
    if(EXISTS "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/${_rom}.nes")
        add_test(NAME ${_rom} COMMAND blargg_harness
            "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/${_rom}.nes" 2400)
    endif()
endforeach()
if(EXISTS "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/instr_test_official.nes")
    add_test(NAME instr_test_official COMMAND blargg_harness
        "${CMAKE_SOURCE_DIR}/engines/nes/tests/data/instr_test_official.nes" 14400)
endif()

# --- Loopback demo: synthetic frames -> RF decoder -> BMP (DSP only) ---
add_executable(famitv_loopback tools/famitv_loopback.cpp)
target_link_libraries(famitv_loopback PRIVATE fam_dsp)

# --- Live HackRF app (needs libhackrf + SDL2) ---
if(HACKRF_FOUND AND SDL2_FOUND)
    add_executable(famidec
        src/main.cpp src/source/hackrf_source.cpp src/ui/sdl_display.cpp)
    target_link_libraries(famidec PRIVATE fam_dsp PkgConfig::HACKRF sdl2_iface)
elseif(FAMITV_TOOLS_ONLY)
    message(STATUS "famidec skipped (needs libhackrf + SDL2; FAMITV_TOOLS_ONLY=ON)")
else()
    set(_famidec_missing "")
    if(NOT HACKRF_FOUND)
        string(APPEND _famidec_missing " libhackrf")
    endif()
    if(NOT SDL2_FOUND)
        string(APPEND _famidec_missing " SDL2")
    endif()
    message(FATAL_ERROR "famidec (primary app) cannot be built: missing${_famidec_missing}. "
        "Install the missing dependency, or pass -DFAMITV_TOOLS_ONLY=ON to build only "
        "the offline DSP tools (famitv_loopback, synth_ntsc) without famidec.")
endif()

# --- ROM player: NES core -> RF decoder -> window (needs SDL2 + libretro.h) ---
if(SDL2_FOUND AND EXISTS "${LIBRETRO_INC}/libretro.h")
    add_executable(famitv_play tools/famitv_play.cpp src/ui/sdl_display.cpp)
    target_include_directories(famitv_play PRIVATE "${LIBRETRO_INC}")
    target_link_libraries(famitv_play PRIVATE fam_dsp sdl2_iface ${CMAKE_DL_LIBS})
else()
    message(STATUS "famitv_play skipped (needs SDL2 and ${LIBRETRO_INC}/libretro.h)")
endif()
