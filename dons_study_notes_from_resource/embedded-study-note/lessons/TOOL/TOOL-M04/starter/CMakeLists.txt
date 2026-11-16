# TOOL-M04 — firmware host build (ringbuf library + app + unit tests) in CMake.
# Configure: cmake -S <src> -B <build> -G Ninja [-DFEATURE_CRC=OFF]   Build: cmake --build <build>
cmake_minimum_required(VERSION 3.20)
project(fw_host VERSION 1.4.2 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # <build>/compile_commands.json for clangd / clang-tidy
# TODO(1): flags as one string: clobbers -DCMAKE_C_FLAGS from the user / CI / toolchain file
set(CMAKE_C_FLAGS "-std=c11 -Wall -Wextra -Werror -Wundef")

# TODO(2): directory-wide includes + defines: EVERY target below (and in ringbuf/) gets them
include_directories(ringbuf/include ringbuf/src)
add_definitions(-DRB_HAVE_CRC=1 -DRB_ASSERTS=1)

add_subdirectory(ringbuf)

set(FW_WARNINGS "")                     # (warnings already sit in CMAKE_C_FLAGS above)

add_executable(app app/main.c app/sensor.c)
target_include_directories(app PRIVATE app)
target_compile_options(app PRIVATE ${FW_WARNINGS})
target_link_libraries(app PRIVATE ringbuf)

# TODO(3): tests are created but CTest never hears about them
add_executable(test_ringbuf tests/test_ringbuf.c)
target_link_libraries(test_ringbuf PRIVATE ringbuf)
target_compile_options(test_ringbuf PRIVATE ${FW_WARNINGS})
add_test(NAME test_ringbuf COMMAND test_ringbuf)

add_executable(test_crc tests/test_crc.c)
target_link_libraries(test_crc PRIVATE ringbuf)
target_compile_options(test_crc PRIVATE ${FW_WARNINGS})
add_test(NAME test_crc COMMAND test_crc)

add_test(NAME app_smoke COMMAND app)
