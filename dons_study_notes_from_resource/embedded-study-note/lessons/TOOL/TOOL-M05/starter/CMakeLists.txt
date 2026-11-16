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
    set(LD_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/fw/stm32f4.ld)
    add_executable(fw fw/startup.c fw/main.c fw/libc_min.c)
    set_target_properties(fw PROPERTIES SUFFIX .elf)
    target_compile_options(fw PRIVATE -Wall -Wextra -Werror -Os)
    target_link_libraries(fw PRIVATE dsp)
    target_link_options(fw PRIVATE
        -nostdlib -T${LD_SCRIPT}
        -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fw.map)
    # TODO(3): edit fw/stm32f4.ld, run ninja -> "no work to do". Make the link depend on it.

    add_custom_command(TARGET fw POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fw> fw.bin
        COMMAND ${ARM_SIZE} $<TARGET_FILE:fw> > fw.size.txt
        COMMAND ${ARM_SIZE} $<TARGET_FILE:fw>
        BYPRODUCTS fw.bin fw.size.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "fw.elf -> fw.bin + fw.size.txt"
        VERBATIM)
endif()

# unit tests: "never on ARM targets" (worked on the Intel CI box...)
# TODO(4): on this Apple-Silicon Mac, ctest says "No tests were found". Why?
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    enable_testing()
    add_executable(test_dsp tests/test_dsp.c)
    target_link_libraries(test_dsp PRIVATE dsp)
    foreach(t crc mavg wrap)
        add_test(NAME dsp.${t} COMMAND test_dsp ${t})
        set_tests_properties(dsp.${t} PROPERTIES LABELS unit)
    endforeach()
endif()
