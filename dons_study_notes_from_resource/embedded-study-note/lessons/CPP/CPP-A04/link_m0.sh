#!/bin/bash
# CPP-A04 link_m0.sh — "누가 __atomic_* 를 구현하나" 링크 실험. Lima VM elinux 의 arm-none-eabi-g++ 14.2 로 실행:
#   limactl shell elinux -- bash -lc 'mkdir -p /tmp/a04 && cp <lesson dir>/{app_m0.cpp,demo_libatomic_m0.c,link_m0.sh} /tmp/a04 && cd /tmp/a04 && bash link_m0.sh'
# (맥 홈은 VM 에서 읽기 전용이라 /tmp 로 복사해서 돌린다)
cd "$(dirname "$0")"
F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mthumb --specs=nosys.specs"
q() { grep -E 'undefined reference|error' | sed 's|.*/ld: ||' ; }
echo "### libgcc 에 있는 atomic/sync 심볼 (v6-m)"
arm-none-eabi-nm "$(arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -print-libgcc-file-name)" | grep -E ' T .*(atomic|sync)'
echo "### M0+ : 라이브러리 없이";  arm-none-eabi-g++ $F -mcpu=cortex-m0plus app_m0.cpp -o app0.elf 2>&1 | q
echo "### M3  : 라이브러리 없이";  arm-none-eabi-g++ $F -mcpu=cortex-m3    app_m0.cpp -o app3.elf 2>&1 | q
echo "### M0+ : demo_libatomic_m0.c 와 함께"
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -std=c11 -Wall -Wextra -Werror -O2 -c demo_libatomic_m0.c -o la.o
arm-none-eabi-g++ $F -mcpu=cortex-m0plus app_m0.cpp la.o -o app0.elf 2>&1 | q; echo "link rc=${PIPESTATUS[0]}"
arm-none-eabi-objdump -d app0.elf | sed -n '/<__atomic_fetch_add_4>:/,/^$/p;/<UART0_IRQHandler>:/,/^$/p'
