#!/usr/bin/env bash
# BM-B02 Part C — 같은 드라이버 소스(solution.c)를 Cortex-M4 타깃으로 컴파일해 진짜 ldr/str 를 본다.
# 맥에서 실행 (Apple clang 의 ARM 백엔드 + xcrun llvm-objdump). 레슨 디렉터리에서: bash target_build.sh
set -euo pipefail
cd "$(dirname "$0")"
W=/tmp/BM-B02_work; mkdir -p "$W"
CF=(--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -std=c11 -Wall -Wextra -Werror)

echo "== 1. solution.c -DTARGET_BUILD (UNIT_TEST 꺼짐 → REG_* = volatile MMIO)"
clang "${CF[@]}" -DTARGET_BUILD -c solution.c -o "$W/drv_m4.o"
xcrun llvm-objdump -d "$W/drv_m4.o" | sed -n '/<uart_init>:/,/<uart_write>:/p' | sed '$d'
echo "== 2. 심볼: mock/테스트 코드가 오브젝트에 남았나?"
xcrun llvm-nm "$W/drv_m4.o"
xcrun llvm-size "$W/drv_m4.o"

echo "== 3. 같은 파일 호스트 빌드(UNIT_TEST) 의 심볼 — 드라이버가 mock_read32/mock_write32 를 부른다"
cc -std=c11 -Wall -Wextra -Werror -Os -c solution.c -o "$W/drv_host.o"
nm "$W/drv_host.o" | grep -E ' (T|t) _(uart_|mock_(read32|write32|peek|poke|add_reg))' | sort -k3

echo "== 4. demo_novolatile.c — volatile 이 빠지면"
clang "${CF[@]}" -c demo_novolatile.c -o "$W/nv.o"
xcrun llvm-objdump -d "$W/nv.o" | sed -n '/<putc_volatile>:/,$p'
echo "== 5. demo_seam_fp.c — 함수 포인터 이음매는 타깃에서도 간접 호출(blx)로 남는다"
clang "${CF[@]}" -c demo_seam_fp.c -o "$W/fp.o"
xcrun llvm-objdump -d "$W/fp.o" | sed -n '/<uart_putc_ops>:/,$p'
xcrun llvm-size "$W/fp.o"
echo "target_build.sh: OK"
