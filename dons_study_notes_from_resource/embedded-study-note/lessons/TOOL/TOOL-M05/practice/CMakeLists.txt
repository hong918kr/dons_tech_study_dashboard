# One source tree, two build trees:
#   host  : cmake -S . -B build-host   -G Ninja                       -> lib + unit tests (ctest)
#   target: cmake -S . -B build-target -G Ninja \
#             -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake         -> lib + fw.elf/.bin/.map/.size
cmake_minimum_required(VERSION 3.20)
project(m05fw LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# portable library: identical source for host and target
add_library(dsp STATIC lib/src/crc.c lib/src/filter.c)
target_include_directories(dsp PUBLIC lib/include)
target_compile_options(dsp PRIVATE -Wall -Wextra -Werror)

if(CMAKE_CROSSCOMPILING)
    # ---------------- target build: firmware image ----------------
    set(BOARD_REV "" CACHE STRING "P1: board revision (empty = fw/main.c default)")
    set(FW_FLASH_BUDGET 8192 CACHE STRING "P3: Flash budget in bytes (text + data)")
    set(LD_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/fw/stm32f4.ld)
    add_executable(fw fw/startup.c fw/main.c fw/libc_min.c)
    set_target_properties(fw PROPERTIES SUFFIX .elf)
    target_compile_options(fw PRIVATE -Wall -Wextra -Werror -Os)
    if(BOARD_REV)
        target_compile_definitions(fw PRIVATE BOARD_REV=${BOARD_REV})
    endif()
    # P2: -nostdlib also drops libgcc (__aeabi_idiv on cores without SDIV) -> add it back
    target_link_libraries(fw PRIVATE dsp gcc)
    target_link_options(fw PRIVATE
        -nostdlib -T${LD_SCRIPT} -Wl,--print-memory-usage
        -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fw.map)
    # the link step must re-run when the linker script changes
    set_target_properties(fw PROPERTIES LINK_DEPENDS ${LD_SCRIPT})
    add_custom_command(TARGET fw POST_BUILD
        # P3: budget gate first -> an over-budget image never becomes fw.bin
        COMMAND ${CMAKE_COMMAND} -DSIZE_TOOL=${ARM_SIZE} -DELF=$<TARGET_FILE:fw>
                -DBUDGET=${FW_FLASH_BUDGET} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fw_size_check.cmake
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fw> fw.bin
        COMMAND ${ARM_SIZE} $<TARGET_FILE:fw> > fw.size.txt
        COMMAND ${ARM_SIZE} $<TARGET_FILE:fw>
        BYPRODUCTS fw.bin fw.size.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "fw.elf -> fw.bin + fw.size.txt"
        VERBATIM)
endif()

# host-only unit tests (they use <stdio.h>; the MCU build has no libc).
# CMAKE_SYSTEM_PROCESSOR is "arm64" on this Mac: test the build MODE, not a CPU name.
if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(test_dsp tests/test_dsp.c)
    target_link_libraries(test_dsp PRIVATE dsp)
    foreach(t crc mavg wrap)
        add_test(NAME dsp.${t} COMMAND test_dsp ${t})
        set_tests_properties(dsp.${t} PROPERTIES LABELS unit)
    endforeach()
endif()
