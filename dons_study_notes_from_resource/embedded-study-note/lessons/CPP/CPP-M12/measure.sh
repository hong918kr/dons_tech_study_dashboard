#!/bin/bash
# CPP-M12 measure.sh — 맥에서 실행
#   bash measure.sh asm    demo_asm.cpp 를 arm64 / thumbv7em 로 컴파일해 호출부 디스어셈블 + sizeof
#   bash measure.sh size   Lima VM elinux 에서 demo_size.cpp 를 Delegate/std::function 으로 링크해 크기 비교
#   bash measure.sh alloc  demo_alloc.cpp 를 맥(libc++) + VM(libstdc++) 에서 실행
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=$(mktemp -d)}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
F="-std=c++20 -O2 -fno-exceptions -fno-rtti"
THUMB="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -nostdinc++ -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC"
OD="xcrun llvm-objdump -d --no-show-raw-insn"
syms="call_raw call_delegate call_ref call_function known_delegate known_function bind_motor"
case "${1:-asm}" in
asm)
    c++ $F -c demo_asm.cpp -o $OUT/host.o 2>/dev/null
    clang++ $THUMB $F -c demo_asm.cpp -o $OUT/thumb.o 2>/dev/null
    for t in thumb host; do
        echo "=================== $t"
        pfx=""; [ $t = host ] && pfx="_"
        for s in $syms; do
            $OD --disassemble-symbols=$pfx$s $OUT/$t.o | sed -n '/>:$/,$p'
        done
        stub=$(xcrun llvm-nm --defined-only $OUT/$t.o | grep obj_stub | grep Motor | awk '{print $3}' | head -1)
        echo "--- stub: $(echo "$stub" | sed 's/^_//' | xcrun c++filt -n 2>/dev/null | cut -c1-90)"
        [ -n "$stub" ] && $OD --disassemble-symbols=$stub $OUT/$t.o | sed -n '/>:$/,$p'
    done
    echo "=================== thumb sizeof (from .rodata constants)"
    clang++ $THUMB $F -S demo_asm.cpp -o - 2>/dev/null | grep -A3 -E '^sz_[a-z]+:' | grep -E 'sz_|\.long' ;;
size)
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M12 && mkdir -p /tmp/CPP-M12 && cp '$PWD'/solution.cpp '$PWD'/demo_size.cpp /tmp/CPP-M12/ && cd /tmp/CPP-M12 && \
      F='-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles -ffunction-sections -fdata-sections -Wl,--gc-sections'; \
      for spec in nosys.specs nano.specs; do for v in delegate function1 function2; do \
        D=''; [ \$v = function1 ] && D=-DUSE_FUNCTION=1; [ \$v = function2 ] && D=-DUSE_FUNCTION=2; \
        extra=''; [ \$spec = nano.specs ] && extra='--specs=nosys.specs'; \
        arm-none-eabi-g++ \$F --specs=\$spec \$extra \$D demo_size.cpp -o \$v-\$spec.elf 2>/dev/null; \
        arm-none-eabi-size \$v-\$spec.elf | tail -1 | sed \"s/\$/  [\$spec]/\"; \
      done; done; \
      for v in delegate function1 function2; do echo \"--- \$v .init_array:\"; arm-none-eabi-objdump -h \$v-nosys.specs.elf | grep -E 'init_array' || echo '(없음)'; done; \
      echo '--- function1 (full libstdc++): top 8 by size'; arm-none-eabi-nm -C --size-sort -S function1-nosys.specs.elf | tail -8 | cut -c1-90; \
      echo '--- function2 (full libstdc++): heap/throw related'; arm-none-eabi-nm -C function2-nosys.specs.elf | grep -E 'operator new|_malloc_r|__throw_bad_function|__cxa_throw|__verbose_terminate' | cut -c1-90 | head -12; \
      echo '--- function1 (nano): heap/throw related'; arm-none-eabi-nm -C function1-nano.specs.elf | grep -E 'operator new|_malloc_r|__throw_bad_function|__cxa_throw|__verbose_terminate' | cut -c1-90 | head -12" ;;
alloc)
    c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_alloc.cpp -o $OUT/alloc 2>/dev/null && $OUT/alloc
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M12a && mkdir -p /tmp/CPP-M12a && cp '$PWD'/solution.cpp '$PWD'/demo_alloc.cpp /tmp/CPP-M12a/ && cd /tmp/CPP-M12a && g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -fno-assume-sane-operators-new-delete demo_alloc.cpp -o a 2>/dev/null && ./a" ;;
esac
