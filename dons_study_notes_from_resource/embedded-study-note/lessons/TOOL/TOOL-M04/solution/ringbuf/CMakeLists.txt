# ringbuf/CMakeLists.txt — the library. Must also work when another project
# (e.g. the bootloader) pulls it in with add_subdirectory().
option(FEATURE_CRC "Running CRC-16 over the ring buffer stream" ON)

# (4) the option decides the sources AND a PUBLIC definition (it changes rb_t's layout)
add_library(ringbuf STATIC src/ringbuf.c $<$<BOOL:${FEATURE_CRC}>:src/rb_crc.c>)
target_compile_definitions(ringbuf PUBLIC RB_HAVE_CRC=$<BOOL:${FEATURE_CRC}> PRIVATE RB_ASSERTS=1)

# (5) PUBLIC include/ for consumers, PRIVATE src/ for rb_internal.h
target_include_directories(ringbuf PUBLIC include PRIVATE src)
target_compile_features(ringbuf PUBLIC c_std_11)
target_compile_options(ringbuf PRIVATE "$<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Werror;-Wundef>")
