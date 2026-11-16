# ringbuf/CMakeLists.txt — the library. Must also work when another project
# (e.g. the bootloader) pulls it in with add_subdirectory().
option(FEATURE_CRC "Running CRC-16 over the ring buffer stream" ON)

# TODO(4): the option is never used: rb_crc.c is always built, RB_HAVE_CRC comes from the top level
add_library(ringbuf STATIC src/ringbuf.c src/rb_crc.c)

# TODO(5): PRIVATE only -> a consumer that links ringbuf can't find ringbuf.h
target_include_directories(ringbuf PRIVATE include src)
