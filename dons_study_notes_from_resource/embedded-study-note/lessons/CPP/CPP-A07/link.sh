#!/bin/bash
# CPP-A07 link.sh — Lima VM elinux 안에서 실행 (arm-none-eabi-g++ 14.2.1). 맥 홈은 VM 에서 읽기 전용이라 /tmp 로 복사해 쓴다:
#   limactl shell elinux -- bash -lc 'mkdir -p /tmp/CPP-A07 && cp <lesson dir>/{solution.cpp,demo_target.cpp,link.sh} /tmp/CPP-A07 && cd /tmp/CPP-A07 && bash link.sh'
# 링크된 ELF 에 operator new / malloc / free / __cxa_* 가 하나라도 있으면 exit 1.
set -e
cd "$(dirname "$0")"
F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -nostartfiles --specs=nosys.specs"
arm-none-eabi-g++ $F -DAO_LINK -ffunction-sections -fdata-sections -Wl,--gc-sections demo_target.cpp -o ao.elf
echo "== arm-none-eabi-size"; arm-none-eabi-size ao.elf
echo "== 큰 심볼 (nm -C --size-sort, 상위 10)"; arm-none-eabi-nm -C --size-sort -S ao.elf | grep -v ' _stack$' | tail -10
echo "== 힙/런타임 심볼 검사"
if arm-none-eabi-nm -C ao.elf | grep -E 'operator new|operator delete|malloc|free|__cxa_|_sbrk'; then
    echo "FAIL: heap/runtime symbol linked"; exit 1
fi
echo "  없음 (operator new / malloc / free / __cxa_* / _sbrk)"
