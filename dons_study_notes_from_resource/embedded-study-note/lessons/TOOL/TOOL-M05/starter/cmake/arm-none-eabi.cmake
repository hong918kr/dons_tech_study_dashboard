# arm-none-eabi.cmake — CMake toolchain file: bare-metal Cortex-M4 with arm-none-eabi-gcc.
# Use:  cmake -S . -B build-target -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
# A toolchain file says HOW to compile for the target CPU. It is read before project()
# and AGAIN inside every try_compile project, so it must be idempotent and set *_INIT only.

# (1) target system: "Generic" = no OS. Setting it turns CMAKE_CROSSCOMPILING on.
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# (2) compilers (found on PATH; CMake derives objcopy/objdump/size... from the prefix)
set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(ARM_SIZE ${TOOLCHAIN_PREFIX}size REQUIRED)

# (3) compiler checks: CMake builds a tiny test program before project() succeeds.
#     There is no crt0/libc/linker script on this machine (no newlib).
# TODO(1): target configure fails here on this Mac. Read the error, fix with one line.

# (4) CPU flags.  TODO(2): this works for a plain build, but a board engineer's
#     CFLAGS=-DBOARD_REV=3 never reaches the compiler. Why? Fix it.
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS        "${CPU_FLAGS} -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "")   # C flags (with CPU flags) are already on the link line

# (5) find_*(): programs from the HOST, libraries/headers/packages only from the target root
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
