# consumer/ — a SECOND product (bootloader) that reuses ringbuf as-is.
# It knows nothing about the app's top-level CMakeLists.txt: only the library's own file.
cmake_minimum_required(VERSION 3.20)
project(bootloader LANGUAGES C)
add_subdirectory(${RINGBUF_DIR} ringbuf)        # -DRINGBUF_DIR=<src>/ringbuf
add_executable(boot boot.c)
target_link_libraries(boot PRIVATE ringbuf)     # the ONLY line a consumer should need
