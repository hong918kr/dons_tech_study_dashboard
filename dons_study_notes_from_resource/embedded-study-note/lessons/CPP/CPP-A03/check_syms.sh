#!/bin/bash
# CPP-A03 check_syms.sh — fw_app.cpp 를 프리스탠딩 Cortex-M4 오브젝트로 만들고, 허용 목록(allow_syms.txt) 밖의
# 미정의 심볼이 하나라도 있으면 exit 1. 깨끗하면 exit 0. CI 에 그대로 넣는 용도.
#   bash check_syms.sh                 고친 판  → exit 0
#   bash check_syms.sh -DLEGACY        흔한 습관 판 → exit 1 (위반 목록 출력)
#   SRC=other.cpp bash check_syms.sh   다른 TU 검사
# 컴파일: 맥 clang --target=thumbv7em-none-eabi -ffreestanding + arm-none-eabi 헤더 (CPP-A04 asm_targets.sh 와 같은 ARM_INC 방식)
cd "$(dirname "$0")"
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
SRC=${SRC:-fw_app.cpp}
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
OUT=${TMPDIR:-/tmp}/cppa03; mkdir -p "$OUT"; OBJ="$OUT/check_$$.o"; trap 'rm -f "$OBJ"' EXIT
clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti \
    -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m/nofp" \
    -isystem "$ARM_INC" "$@" -c "$SRC" -o "$OBJ" || { echo "compile failed"; exit 2; }
PATTERNS=$(sed -E 's/[[:space:]]+#.*$//; /^[[:space:]]*(#|$)/d' allow_syms.txt)
bad=0
while read -r name; do
    [ -z "$name" ] && continue
    if grep -q -E -x -f <(printf '%s\n' "$PATTERNS") <<<"$name"; then echo "ok    $name"; else echo "DENY  $name"; bad=$((bad + 1)); fi
done < <(xcrun llvm-nm -u "$OBJ" | awk '{print $NF}')
echo "$SRC $*: $bad symbol(s) outside allow_syms.txt"
[ "$bad" -eq 0 ]
