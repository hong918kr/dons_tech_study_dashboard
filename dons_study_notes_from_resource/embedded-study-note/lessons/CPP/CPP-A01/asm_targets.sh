#!/bin/bash
# CPP-A01 asm_targets.sh — typestate 실측 (맥에서 실행, 레슨 폴더 기준). 모두 성공하면 exit 0.
#   bash asm_targets.sh asm     demo_target.cpp → Cortex-M4 (thumbv7em) 디스어셈블 + 함수별 명령 수 + size/nm
#   bash asm_targets.sh errors  demo_errors.cpp 를 -DSHOW_ERROR=1..9 로 각각 컴파일 (실패해야 정상) → 첫 에러 줄들
#   bash asm_targets.sh tidy    demo_moved_from.cpp → clang-tidy bugprone-use-after-move + clang -Wconsumed
#   bash asm_targets.sh gcc     Lima VM elinux 의 arm-none-eabi-g++ 14.2.1 로 같은 비교 + GCC 에러 메시지 + consumed 속성 처리
#   bash asm_targets.sh moveassign  Spi/토큰의 이동 대입을 = default 로 바꾸면 std::visit 의 abort 경로가 사라지는지 (clang -Os)
#   bash asm_targets.sh all     위 다섯 개
# 맥 clang 은 thumb 코드를 내지만 bare-metal C++ 헤더가 없으므로 arm-none-eabi 헤더를 쓴다 (CPP-A04 asm_targets.sh 와 같은 방식).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=/tmp/cpp_a01_out}"; mkdir -p "$OUT"
: "${TIDY:=$HOME/.venvs/clangtidy/bin/clang-tidy}"
cd "$(dirname "$0")"
fetch_inc() {
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
}
m4() {
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@"
}
count() {   # objdump -d 출력 → "함수: 명령 수" (리터럴 풀 .word 제외)
    awk '/^[0-9a-f]+ <.*>:$/ { if (f) printf "%-48s %3d\n", f, n; f=$2; n=0; next }
         /^ +[0-9a-f]+:/ { if ($0 !~ /\.word|\.short/) n++ }
         END { if (f) printf "%-48s %3d\n", f, n }'
}
do_asm() {
    fetch_inc
    for O in -Os -O2; do
        m4 $O -c demo_target.cpp -o "$OUT/t$O.o"
        echo "== 명령 수 (clang $(clang++ --version | head -1 | sed 's/.*version //;s/ (.*//'), thumbv7em $O)"
        xcrun llvm-objdump -d --no-show-raw-insn "$OUT/t$O.o" | count | sed 's/[<>:]//g'
    done
    echo "== 디스어셈블 -Os (demangled)"
    xcrun llvm-objdump -d -C --no-show-raw-insn "$OUT/t-Os.o" | grep -v '^$'
    echo "== size / 정의된 심볼 / 미정의 심볼 (-Os)"
    xcrun llvm-size "$OUT/t-Os.o"; xcrun llvm-nm -C --size-sort "$OUT/t-Os.o"; echo "-- undefined:"; xcrun llvm-nm -u -C "$OUT/t-Os.o" || true
}
do_errors() {
    for n in 1 2 3 4 5 6 7 8 9; do
        echo "== SHOW_ERROR=$n"
        if c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp \
               > "$OUT/err$n.txt" 2>&1; then
            echo "UNEXPECTED: compiled"; exit 1
        fi
        grep -E 'error:|note: because|note: candidate|evaluated to false' "$OUT/err$n.txt" | head -4
    done
    echo "== SHOW_ERROR 없이: 통과해야 한다"
    c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only demo_errors.cpp && echo "OK"
}
do_tidy() {
    echo "== clang-tidy bugprone-use-after-move ($("$TIDY" --version | grep -o 'version [0-9.]*'))"
    "$TIDY" --checks='-*,bugprone-use-after-move' demo_moved_from.cpp -- -std=c++20 -isysroot "$SDKROOT" 2>&1 \
        | grep -E 'warning:|note:' | sed 's|.*/demo_moved_from|demo_moved_from|' || true
    echo "== clang -Wconsumed (typestate 속성)"
    c++ -std=c++20 -Wconsumed -DUSE_CONSUMED -fsyntax-only demo_moved_from.cpp 2>&1 | grep -E 'warning:' || true
    echo "== 기본 빌드 (-Wall -Wextra): 경고 없이 통과 — 컴파일러는 이 구멍을 기본으로는 안 본다"
    c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only demo_moved_from.cpp && echo "OK"
}
do_gcc() {
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-A01 && mkdir -p /tmp/CPP-A01"
    tar -cf - spi_typestate.hpp demo_target.cpp demo_errors.cpp demo_moved_from.cpp | limactl shell elinux -- tar -xf - -C /tmp/CPP-A01
    limactl shell elinux -- bash -lc '
        cd /tmp/CPP-A01
        G="arm-none-eabi-g++ -std=c++20 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -mfloat-abi=hard"
        for O in -Os -O2; do
            $G $O -c demo_target.cpp -o t$O.o
            echo "== 명령 수 ($(arm-none-eabi-g++ --version | head -1), $O)"
            arm-none-eabi-objdump -d --no-show-raw-insn t$O.o | awk "/^[0-9a-f]+ <.*>:\$/ { if (f) printf \"%-48s %3d\n\", f, n; f=\$2; n=0; next } /^ +[0-9a-f]+:/ { if (\$0 !~ /\.word/) n++ } END { if (f) printf \"%-48s %3d\n\", f, n }" | sed "s/[<>:]//g"
            arm-none-eabi-size t$O.o; echo "-- undefined:"; arm-none-eabi-nm -u -C t$O.o || true
        done
        echo "== GCC: SHOW_ERROR=1 (Configured 에 transfer)"
        $G -Os -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp 2>&1 | grep -E "error:|note:" | head -4 || true
        echo "== GCC: consumed 속성"
        $G -Os -Wall -fsyntax-only -DUSE_CONSUMED demo_moved_from.cpp 2>&1 | grep -E "warning:" | sort | uniq -c | head -4 || true'
}
do_moveassign() {
    fetch_inc
    mkdir -p "$OUT/ma"
    sed 's/Spi& operator=(Spi&&) = delete;/Spi\& operator=(Spi\&\&) noexcept = default;/; s/PeriphToken& operator=(PeriphToken&&) = delete;/PeriphToken\& operator=(PeriphToken\&\&) noexcept = default;/' \
        spi_typestate.hpp > "$OUT/ma/spi_typestate.hpp"
    cp demo_target.cpp "$OUT/ma/"
    for v in deleted defaulted; do
        if [ $v = deleted ]; then src=demo_target.cpp; else src="$OUT/ma/demo_target.cpp"; fi
        m4 -Os -c "$src" -o "$OUT/ma/$v.o"
        echo "== move-assign $v: 미정의 심볼"; xcrun llvm-nm -u "$OUT/ma/$v.o" || true
        xcrun llvm-nm --size-sort "$OUT/ma/$v.o" | grep -E 'normalize|__do_visit|__invoke_impl' | cut -c1-60
    done
}
case "${1:-asm}" in
asm) do_asm ;;
errors) do_errors ;;
tidy) do_tidy ;;
gcc) do_gcc ;;
moveassign) do_moveassign ;;
all) do_asm >/dev/null; do_errors >/dev/null; do_tidy >/dev/null; do_gcc >/dev/null; do_moveassign >/dev/null; echo "all OK" ;;
*) echo "usage: $0 asm|errors|tidy|gcc|moveassign|all" >&2; exit 2 ;;
esac
