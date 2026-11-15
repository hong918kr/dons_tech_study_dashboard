#!/bin/bash
# CPP-A05 bloat.sh — fat vs thin static_vector (demo_bloat.cpp, 5 타입 × N) 의 코드 크기를 잰다. 레슨 디렉터리에서 실행.
#   bash bloat.sh obj    (맥) 레시피 (b): Apple clang --target=thumbv7em 오브젝트 .text + 함수별 nm 합계
#   bash bloat.sh link   (Lima VM elinux 안에서) 레시피 (c): arm-none-eabi-g++ 14.2 링크 ELF, gc-sections 유무, ld.lld --icf=all
#     limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-A05 && cp <lesson dir>/* /tmp/CPP-A05/ && bash /tmp/CPP-A05/bloat.sh link'
#   bash bloat.sh place  (맥) demo_global_placement.cpp: 전역 벡터가 .bss / .data / .init_array 중 어디로 가나 (clang)
#   bash bloat.sh host   (맥) ld64 가 같은 기계어 함수를 합치는지(dedup) arm64 실행 파일로 확인
set -e
cd "$(dirname "$0")"
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# 함수 크기 합계 (nm -S): app = prod_/cons_ (사용 코드), lib = 컨테이너 코드 (fat 멤버 / thin 코어),
# mem = memcpy/memmove, init = 전역 동적 초기화 함수, start = _start
sum_nm() {   # sum_nm <nm 명령> <파일들>
    local NM=$1; shift
    $NM -S --defined-only "$@" 2>/dev/null | awk 'function hex(h,  i, v) { v = 0; h = tolower(h)
            for (i = 1; i <= length(h); i++) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1; return v }
        NF==4 && ($3 ~ /^[TtWw]$/) { n = hex($2)
            if ($4 ~ /^(prod|cons)_/) app += n; else if ($4 ~ /^(memcpy|memmove)$/) mem += n
            else if ($4 ~ /_GLOBAL__sub_I/) ini += n; else if ($4 == "_start") st += n; else lib += n }
        END { printf "app %5d  lib %5d  mem %4d  init %3d  start %3d", app, lib, mem, ini, st }'
}
CFGS='fat-3N-1S|-DIMPL=0 -DNS=3 -DSITES=1
thin-3N-1S|-DIMPL=1 -DNS=3 -DSITES=1
fat-1N-1S|-DIMPL=0 -DNS=1 -DSITES=1
thin-1N-1S|-DIMPL=1 -DNS=1 -DSITES=1
fat-3N-3S|-DIMPL=0 -DNS=3 -DSITES=3
thin-3N-3S|-DIMPL=1 -DNS=3 -DSITES=3
fat-1N-3S|-DIMPL=0 -DNS=1 -DSITES=3
thin-1N-3S|-DIMPL=1 -DNS=1 -DSITES=3'
each_cfg() {   # each_cfg <fn> → fn <name> <defines> <srcs>   (이름: 구현-N 종류 수-호출처 수)
    local name D src
    while IFS='|' read -r name D; do
        src="demo_bloat.cpp"; [[ $D == *IMPL=1* ]] && src="$src demo_thin_core.cpp"
        "$1" "$name" "$D" "$src" </dev/null
    done <<< "$CFGS"
}

case "${1:-obj}" in
obj)
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
    cxx() { clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@"; }
    obj_row() {   # name defines srcs
        local objs="" s
        for s in $3; do cxx $O $2 -c "$s" -o "$OUT/${s%.cpp}.o"; objs="$objs $OUT/${s%.cpp}.o"; done
        printf "%-12s .text %5d   %s\n" "$1" "$(xcrun llvm-size -A $objs | awk '$1 ~ /^\.text/ {s+=$2} END {print s}')" \
            "$(sum_nm 'xcrun llvm-nm' $objs)"
    }
    for O in -Os -O2; do
        echo "== clang 21 thumbv7em $O  오브젝트 (링크 전: 안 쓰는 코어 함수도 포함)"
        each_cfg obj_row
        obj_row "thin-inl-3N" "-DIMPL=1 -DNS=3 -DSITES=1 -DTHIN_CORE_INLINE" "demo_bloat.cpp"
    done
    for O in -Os -O2; do
        echo "== 비-trivial T (Counted<1..3> × N 4/16/64) clang $O 오브젝트: fat 전체 템플릿 vs thin ops 표"
        obj_row "fat-nontriv" "-DIMPL=0 -DNONTRIV=1" "demo_bloat.cpp"
        obj_row "thin-ops" "-DIMPL=1 -DNONTRIV=1" "demo_bloat.cpp demo_thin_core.cpp"
    done
    O=-Os
    cxx -Os -DIMPL=1 -DNONTRIV=1 -c demo_bloat.cpp -o "$OUT/n.o"
    echo "== thin-ops -Os: 타입별로 생긴 것 (ops 표는 어느 섹션?)"
    xcrun llvm-objdump -t -C "$OUT/n.o" | grep -E ' (\.text|\.rodata)[^ ]*\s+[0-9a-f]+ (thin::ops_for|void thin::op_)' | grep 'Counted<1>' \
        | awk -F'\t' '{ n = split($1, a, " "); sec = a[n]; sub(/\._Z.*/, "", sec); print sec, $2 }'
    echo "(Counted<2>, <3> 도 같은 모양: $(xcrun llvm-nm -C "$OUT/n.o" | grep -cE 'thin::(ops_for|op_[a-z_]+)<Counted<')개 심볼 = 3 타입 × (표 1 + 함수 5))"
    echo "== thin erase_ops 의 원소 루프 (-Os): 원소마다 간접 호출"
    xcrun llvm-objdump -d --no-show-raw-insn --disassemble-symbols=_ZN4thin9erase_opsERtPSt4bytejjRKNS_8elem_opsE "$OUT/demo_thin_core.o" | grep -E '^\s+[0-9a-f]+:'
    echo "== fat-3N-1S -Os: 함수 크기 (u32 인스턴스)"; cxx -Os -DIMPL=0 -c demo_bloat.cpp -o "$OUT/a.o"
    xcrun llvm-nm -S --size-sort -C "$OUT/a.o" | grep -E ' [TtWw] ' | grep -E 'u32_|unsigned int, (4|16|64)u'
    echo "== thin-3N-1S -Os: 함수 크기 (u32 인스턴스)"; cxx -Os -DIMPL=1 -c demo_bloat.cpp -o "$OUT/t.o"
    xcrun llvm-nm -S --size-sort -C "$OUT/t.o" | grep -E ' [TtWw] ' | grep -E 'u32_'
    echo "== thin 코어 TU -Os"; cxx -Os -c demo_thin_core.cpp -o "$OUT/c.o"
    xcrun llvm-nm -S --size-sort -C "$OUT/c.o" | grep -E ' [TtWw] '
    echo "== 'static_vector<' 가 들어간 함수 심볼 수:  fat $(xcrun llvm-nm -C "$OUT/a.o" | grep -c 'static_vector<')   thin $(xcrun llvm-nm -C "$OUT/t.o" | grep -c 'static_vector<' || true)"
    echo "== asm: prod_u32_16_1 (thin, -Os)"
    xcrun llvm-objdump -d --no-show-raw-insn --disassemble-symbols=prod_u32_16_1 "$OUT/t.o" | grep -E '^\s+[0-9a-f]+:'
    ;;
link)
    F="-std=c++20 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs"
    GC="-ffunction-sections -fdata-sections -Wl,--gc-sections"
    txt() { arm-none-eabi-g++ $F "$@" -o "$OUT/a.elf" 2>"$OUT/err" || { cat "$OUT/err"; exit 1; }
            arm-none-eabi-size -A "$OUT/a.elf" | awk '$1==".text"{print $2}'; }
    link_row() {   # name defines srcs
        printf "%-12s %8s %8s %10s %10s %12s\n" "$1" "$(txt -Os $2 $3)" "$(txt -Os $GC $2 $3)" \
            "$(txt -Os $GC -DKEEP_MEM $2 $3)" "$(txt -O2 $GC -DKEEP_MEM $2 $3)" \
            "$(txt -Os $GC -DKEEP_MEM -fuse-ld=lld -Wl,--icf=all $2 $3)"
    }
    echo "== arm-none-eabi-g++ 14.2.1, 링크된 ELF 의 .text 바이트 (KEEP_MEM = memcpy/memmove 가 이미 링크된 펌웨어 기준)"
    printf "%-12s %8s %8s %10s %10s %12s\n" "" "Os" "Os+gc" "Os+gc+mem" "O2+gc+mem" "Os+gc+mem+icf"
    each_cfg link_row
    link_row "fat-nontriv" "-DIMPL=0 -DNONTRIV=1" "demo_bloat.cpp"
    link_row "thin-ops" "-DIMPL=1 -DNONTRIV=1" "demo_bloat.cpp demo_thin_core.cpp"
    echo "== 함수 합 (nm -S), -Os gc KEEP_MEM"
    nm_row() { arm-none-eabi-g++ $F -Os $GC -DKEEP_MEM $2 $3 -o "$OUT/a.elf" 2>/dev/null; printf "%-12s %s\n" "$1" "$(sum_nm arm-none-eabi-nm "$OUT/a.elf")"; }
    each_cfg nm_row
    echo "== gc 없이 링크하면 남는 안 쓰는 코어 함수 (thin-3N-1S -Os)"
    arm-none-eabi-g++ $F -Os -DIMPL=1 demo_bloat.cpp demo_thin_core.cpp -o "$OUT/t.elf" 2>/dev/null
    arm-none-eabi-nm -C -S --size-sort "$OUT/t.elf" | grep -E '_ops' || true
    echo "== GCC -fipa-icf 끄기: fat-3N-1S -Os gc  기본 $(txt -Os $GC -DIMPL=0 demo_bloat.cpp)  -fno-ipa-icf $(txt -Os $GC -fno-ipa-icf -DIMPL=0 demo_bloat.cpp)"
    echo "== 인라인을 끈 fat (-Os -fno-inline gc): ld $(txt -Os $GC -fno-inline -DIMPL=0 demo_bloat.cpp)  lld $(txt -Os $GC -fno-inline -DIMPL=0 -fuse-ld=lld demo_bloat.cpp)  lld --icf=all $(txt -Os $GC -fno-inline -DIMPL=0 -fuse-ld=lld -Wl,--icf=all demo_bloat.cpp)"
    echo "== 그때 lld 가 접은 섹션 (--print-icf-sections, 앞 8줄)"
    arm-none-eabi-g++ $F -Os $GC -fno-inline -DIMPL=0 -fuse-ld=lld -Wl,--icf=all -Wl,--print-icf-sections demo_bloat.cpp -o "$OUT/i.elf" 2>&1 \
        | grep -E 'selected|removing' | sed -e 's/^ld.lld: //' -e 's#/tmp/[^:]*:##' | head -8 || true
    echo "== 전역 배치 (arm-none-eabi-g++ -c demo_global_placement.cpp)"
    for O in -Os -O2; do
        arm-none-eabi-g++ -std=c++20 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb $O -c demo_global_placement.cpp -o "$OUT/gp.o"
        echo "-- $O"; arm-none-eabi-objdump -t "$OUT/gp.o" | grep -E ' g_|_GLOBAL' | awk '{print $(NF-2), $(NF-1), $NF}'
    done
    echo "== GNU ld --help 에 'icf' 가 나오는 줄 수: $(arm-none-eabi-ld --help | grep -ci icf || true)"
    ;;
place)
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then echo "ARM_INC 없음 — 먼저 bash bloat.sh obj" >&2; exit 1; fi
    cxx() { clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@"; }
    for O in -Os -O2 -O0; do
        cxx $O -c demo_global_placement.cpp -o "$OUT/gp.o"
        echo "-- clang $O"; xcrun llvm-objdump -t "$OUT/gp.o" | grep -E ' g_|global_var_init|_GLOBAL' | awk '{print $(NF-2), $(NF-1), $NF}'
    done
    echo "-- clang -DSHOW_ERROR"; cxx -O2 -DSHOW_ERROR -fsyntax-only demo_global_placement.cpp 2>&1 | grep -E 'error|note' | head -8 || true
    ;;
host)
    printf '#include <cstdint>\nextern "C" [[noreturn]] void _start();\nint main(int argc, char**) { if (argc > 99) _start(); return 0; }\n' > "$OUT/m.cpp"
    for X in "-Os" "-Os -fno-inline"; do
        c++ -std=c++20 -fno-exceptions -fno-rtti $X -DIMPL=0 -c demo_bloat.cpp -o "$OUT/a.o"
        c++ $X "$OUT/m.cpp" "$OUT/a.o" -o "$OUT/dd"
        c++ $X "$OUT/m.cpp" "$OUT/a.o" -Wl,-no_deduplicate -o "$OUT/nd"
        echo "== fat-3N-1S $X  ld64 기본 __text $(size -m "$OUT/dd" | awk '/Section __text/ {print $3}')   -no_deduplicate $(size -m "$OUT/nd" | awk '/Section __text/ {print $3}')"
        echo "-- 기본 빌드의 erase 주소 (u32 / i32 × N)"; nm -C "$OUT/dd" | grep -E 'static_vector<(unsigned int|int), (4|16|64)ul>::erase' || echo "(전부 인라인됨)"
    done
    ;;
esac
