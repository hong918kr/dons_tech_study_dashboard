# TOOL-M04 — firmware host build (ringbuf library + app + unit tests) in CMake.
# Configure: cmake -S <src> -B <build> -G Ninja [-DFEATURE_CRC=OFF]   Build: cmake --build <build>
cmake_minimum_required(VERSION 3.20)
project(fw_host VERSION 1.4.2 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # <build>/compile_commands.json for clangd / clang-tidy
# (1) no CMAKE_C_FLAGS edits: the C standard via target properties, warnings per target (below)
set(CMAKE_C_STANDARD 11)                # defaults for targets created below -> -std=c11
set(CMAKE_C_EXTENSIONS OFF)             # (ON would give -std=gnu11)

# (2) no directory-wide state: the library carries its own usage requirements
#     (include dirs + RB_HAVE_CRC flow to whoever links it; rb_internal.h + RB_ASSERTS don't)

add_subdirectory(ringbuf)

set(FW_WARNINGS "$<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Werror;-Wundef>" "$<$<C_COMPILER_ID:MSVC>:/W4;/WX>")

add_executable(app app/main.c app/sensor.c)
target_include_directories(app PRIVATE app)
target_compile_options(app PRIVATE ${FW_WARNINGS})
target_link_libraries(app PRIVATE ringbuf)

# (3) enable_testing() BEFORE add_test, labels for "ctest -L unit"
enable_testing()
add_executable(test_ringbuf tests/test_ringbuf.c)
target_link_libraries(test_ringbuf PRIVATE ringbuf)
target_include_directories(test_ringbuf PRIVATE ringbuf/src)   # white-box test: the one explicit exception
target_compile_options(test_ringbuf PRIVATE ${FW_WARNINGS})
add_test(NAME test_ringbuf COMMAND test_ringbuf)
if(FEATURE_CRC)                                                # no CRC code -> no CRC test
  add_executable(test_crc tests/test_crc.c)
  target_link_libraries(test_crc PRIVATE ringbuf)
  target_compile_options(test_crc PRIVATE ${FW_WARNINGS})
  add_test(NAME test_crc COMMAND test_crc)
  set_tests_properties(test_crc PROPERTIES LABELS unit)
endif()
set_tests_properties(test_ringbuf PROPERTIES LABELS unit)
add_test(NAME app_smoke COMMAND app)
set_tests_properties(app_smoke PROPERTIES LABELS smoke PASS_REGULAR_EXPRESSION "samples=64 ")
