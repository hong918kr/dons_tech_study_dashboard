# TOOL-M04 practice — solution + P1 ALIAS, P2 INTERFACE warnings target, P3 configure_file, P4 test properties.
# (check.sh lays practice/ over solution/, so ringbuf/CMakeLists.txt is the solution's.)
cmake_minimum_required(VERSION 3.20)
project(fw_host VERSION 1.4.2 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(FW_BOARD "nucleo-sim" CACHE STRING "Board name baked into build_info.h")

add_subdirectory(ringbuf)
# P1: namespaced alias. A typo in "fw::..." is a CONFIGURE error, a typo in a plain name is a LINK error.
add_library(fw::ringbuf ALIAS ringbuf)      # (normally right after add_library in ringbuf/)

# P2: warnings as an INTERFACE target: no sources, only usage requirements, linked PRIVATE
add_library(fw_warnings INTERFACE)
add_library(fw::warnings ALIAS fw_warnings)
target_compile_options(fw_warnings INTERFACE
  "$<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Werror;-Wundef;-Wshadow;-Wconversion>"
  "$<$<C_COMPILER_ID:MSVC>:/W4;/WX>")

# P3: generated header goes to the BUILD tree; only the app sees it
configure_file(app/build_info.h.in ${PROJECT_BINARY_DIR}/generated/build_info.h @ONLY)

add_executable(app app/main.c app/sensor.c)
target_include_directories(app PRIVATE app ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(app PRIVATE fw::ringbuf fw::warnings)

enable_testing()
add_executable(test_ringbuf tests/test_ringbuf.c)
target_link_libraries(test_ringbuf PRIVATE fw::ringbuf fw::warnings)
target_include_directories(test_ringbuf PRIVATE ringbuf/src)
add_test(NAME test_ringbuf COMMAND test_ringbuf)
set(unit_tests test_ringbuf)
if(FEATURE_CRC)
  add_executable(test_crc tests/test_crc.c)
  target_link_libraries(test_crc PRIVATE fw::ringbuf fw::warnings)
  add_test(NAME test_crc COMMAND test_crc)
  list(APPEND unit_tests test_crc)
endif()
add_test(NAME app_smoke COMMAND app)
# P4: negative test (exit != 0 is the PASS condition) + a timeout on everything
add_test(NAME app_rejects_args COMMAND app --bogus)
set_tests_properties(${unit_tests} PROPERTIES LABELS unit TIMEOUT 10)
set_tests_properties(app_smoke PROPERTIES LABELS smoke TIMEOUT 10 PASS_REGULAR_EXPRESSION "samples=64 ")
set_tests_properties(app_rejects_args PROPERTIES LABELS negative TIMEOUT 10 WILL_FAIL TRUE)
