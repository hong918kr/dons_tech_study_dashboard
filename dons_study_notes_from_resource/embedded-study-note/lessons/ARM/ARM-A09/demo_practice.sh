#!/usr/bin/env bash
# ARM-A09 demo_practice.sh — VM 에서: limactl shell elinux -- bash -lc 'bash <lesson>/demo_practice.sh'
# 추가 연습(ADR / CONDBR19 / TSTBR14 / MOVW_UABS)의 정답 워드와 범위 에러를 실제 링커로 만든다.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ARM-A09_work/practice
mkdir -p "$W" && cd "$W" || exit 2
cp "$SRC/demo_practice.S" . && as demo_practice.S -o p.o || exit 1
readelf -rW p.o | sed -n '/.rela.text.near/,/^$/p'
mk() { printf 'SECTIONS {\n . = %s;\n .text.back : { *(.text.back) }\n . = 0x10000;\n .text.near : { *(.text.near) }\n . = %s;\n .text.mid : { *(.text.mid) }\n . = %s;\n .text.far : { *(.text.far) }\n . = 0x123456789AB0;\n .data.far : { *(.data.far) }\n}\n' "$3" "$1" "$2" > p.ld; }
echo "-- in range (boundaries): far1 = 0x10fffc (b.ne@0x10000: +0xffffc), mid1 = 0x18004 (tbz@0x10008: +0x7ffc), back1 = 0x800c (tbnz@0x1000c: -0x8000)"
mk 0x18004 0x10fffc 0x800c
ld -static -T p.ld -o p.elf p.o && objdump -d p.elf | sed -n '/<_start>:/,/^$/p'
nm p.elf | grep -E ' (_start|far_data|mid1|far1|back1)$' || nm p.elf
echo "-- out of range: far1 = 0x110000 (+0x100000), mid1 = 0x18008 (+0x8000), back1 = 0x8008 (-0x8004)"
mk 0x18008 0x110000 0x8008
ld -static -T p.ld -o p2.elf p.o 2>&1 | sed -n '1,8p'
