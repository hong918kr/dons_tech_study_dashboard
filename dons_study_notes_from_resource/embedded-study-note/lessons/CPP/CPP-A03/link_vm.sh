#!/bin/bash
# CPP-A03 link_vm.sh — 맨몸 링크(-nostdlib -nostartfiles)로 "누가 이 심볼을 주나" 를 확인한다. Lima VM elinux (arm-none-eabi-g++ 14.2.1):
#   bash nm_table.sh obj      # (선택, 맥) clang 이 만든 LEGACY 오브젝트 → /tmp/cppa03/app_legacy_clang.o  (TMPDIR 기준)
#   tar -cf - fw_app.cpp fw_start.cpp fw_stubs.cpp link_vm.sh [-C "${TMPDIR:-/tmp}/cppa03" app_legacy_clang.o] \
#     | limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-A03 && tar -xf - -C /tmp/CPP-A03 && bash /tmp/CPP-A03/link_vm.sh'
# (맥 홈은 VM 에서 읽기 전용이라 VM 의 /tmp 로 복사해서 돌린다)
cd "$(dirname "$0")"
F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -ffreestanding -ffunction-sections -fdata-sections"
L="-nostdlib -nostartfiles -Wl,--gc-sections -Wl,-e,_start"
q() { grep -E 'undefined reference|error' | sed -E 's|^.*/ld: ||; s|^[^ ]*\.o: in function||' | sort -u ; }
cc() { arm-none-eabi-g++ $F "$@"; }
ld() { arm-none-eabi-g++ $F $L "$@" 2>&1 | q; return "${PIPESTATUS[0]}"; }
cc -c fw_start.cpp -o start.o && cc -c fw_stubs.cpp -o stubs.o && cc -c fw_app.cpp -o app.o && cc -DLEGACY -c fw_app.cpp -o app_legacy.o || exit 1
printf 'extern "C" { void* __dso_handle = &__dso_handle; void _init() {} void _fini() {} }\n' > crt_min.cpp   # crtbegin.o/crti.o 대역
printf 'void* operator new(unsigned int) { for (;;) { } }\n' > new_trap.cpp
cc -c crt_min.cpp -o crt_min.o && cc -c new_trap.cpp -o new_trap.o || exit 1

echo "### 1. LEGACY, 라이브러리 없이";              ld app_legacy.o start.o -o a1.elf
echo "### 2. LEGACY + fw_stubs.o";                  ld app_legacy.o start.o stubs.o -o a2.elf
echo "### 3. LEGACY + fw_stubs.o + -lgcc -lc";      ld app_legacy.o start.o stubs.o -lgcc -lc -o a3.elf
echo "### 3b. LEGACY + fw_stubs.o + -lstdc++ -lgcc -lc"; ld app_legacy.o start.o stubs.o -lstdc++ -lgcc -lc -o a4.elf
if [ -f app_legacy_clang.o ]; then
echo "### 3c. clang 의 LEGACY 오브젝트 + fw_stubs.o + -lgcc -lc"; ld app_legacy_clang.o start.o stubs.o -lgcc -lc -o a5.elf
fi
echo "### 4. 고친 판, 라이브러리 없이";             ld app.o start.o -o b1.elf
echo "### 5. 고친 판 + -lgcc -lc";                  ld app.o start.o -lgcc -lc -Wl,-Map,b2.map -o b2.elf; echo "rc=$?"
echo "### 5 에서 끌려온 라이브러리 멤버"; grep -E '^/.*\.a\(' b2.map | sed -E 's|^.*/||'
echo "### 크기 비교 (링크 성공한 것만)"
ld app_legacy.o start.o crt_min.o -lsupc++ -lgcc -lc -lnosys -o L_real.elf      # 진짜 런타임 (libsupc++ + newlib)
ld app_legacy.o start.o stubs.o -lsupc++ -lgcc -lc -lnosys -Wl,-Map,L_stub_new.map -o L_stub_new.elf 2>/dev/null   # 스텁 + new 만 libsupc++
ld app_legacy.o start.o stubs.o new_trap.o -lgcc -lc -o L_stub_all.elf          # 스텁 + new 도 trap
arm-none-eabi-size L_real.elf L_stub_new.elf L_stub_all.elf b2.elf
echo "### new 하나가 끌고 온 사슬 (L_stub_new.map, 앞 12개)"
awk '/^Archive member included/{on=1; next} on && /^$/ {if (seen) exit; next} on {seen=1; print}' L_stub_new.map \
    | sed -E 's|/[^ ]*/||g; s|^ +||' | paste -d' ' - - | sed -E 's/ +/ /g' | head -12
echo "### L_real 의 큰 심볼"; arm-none-eabi-nm -S --size-sort -C L_real.elf | grep -v ' N ' | tail -6
echo "### 누가 주나 (툴체인 아카이브 nm)"
for a in "$(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -print-file-name=libsupc++.a)" \
         "$(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -print-libgcc-file-name)" \
         "$(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -print-file-name=libc.a)" \
         "$(dirname "$(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -print-libgcc-file-name)")/crtbegin.o"; do
    arm-none-eabi-nm -A "$a" 2>/dev/null | grep -E ' [TD] (__cxa_guard_acquire|__cxa_pure_virtual|__cxa_atexit|__aeabi_atexit|atexit|__dso_handle|_Znwj|_ZdlPvj|__dynamic_cast|__cxa_throw|__aeabi_uldivmod|__aeabi_memclr8|__aeabi_memcpy4|memset|__aeabi_dmul)$' \
        | sed -E 's|^.*/([^/]+\.a):|\1:|; s|^.*/(crtbegin\.o):|\1:|; s| [0-9a-f]{8} | |'
done
echo "### 스텁 층 (fw_stubs.o)"; arm-none-eabi-size stubs.o; arm-none-eabi-nm -S --size-sort stubs.o | grep -E ' [TtBbDd] '
echo "### __cxa_guard_acquire 디스어셈블"; arm-none-eabi-objdump -d --no-show-raw-insn stubs.o | sed -n '/<__cxa_guard_acquire>:/,/^$/p'
