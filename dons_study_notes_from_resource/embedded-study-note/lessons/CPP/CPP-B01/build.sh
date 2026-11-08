#!/bin/sh
# CPP-B01 demo: 같은 드라이버의 C 빌드 vs C++ 빌드 비교 (size / objdump / nm)
#   sh build.sh
set -u
cd "$(dirname "$0")"
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
O=${TMPDIR:-/tmp}/cppb01_build; mkdir -p "$O"

echo "== 1) C 소스를 그대로 C++ 로 컴파일 (실패가 정상)"
c++ -x c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti \
    -c ring_gpio.c -o "$O/fail.o" 2>&1 | grep -E 'error|generated' | sed 's/^/    /'

echo "== 2) 빌드"
cc  -std=c11   -Wall -Wextra -Werror -O2 -c ring_gpio.c        -o "$O/c.o"   || exit 1
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti \
    -c ring_gpio_port.cpp -o "$O/cpp.o" || exit 1
c++ -std=c++20 -Wall -Wextra -Werror -O2 \
    -c ring_gpio_port.cpp -o "$O/cpp_exc.o" || exit 1         # 예외/RTTI 켠 기본값
echo "    (NAIVE_CONST: extern 없이 빌드 — -Werror 빼고 경고만 본다)"
c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti -DNAIVE_CONST \
    -c ring_gpio_port.cpp -o "$O/cpp_naive.o" 2>&1 | grep -E 'warning' | sed 's/^/    /'

echo "== 3) size (__TEXT / __DATA 합계, others = __LD,__compact_unwind)"
size "$O/c.o" "$O/cpp.o" "$O/cpp_exc.o" "$O/cpp_naive.o"

echo "== 4) size -m (섹션별)"
for f in c cpp; do echo "--- $f.o"; size -m "$O/$f.o" | grep -E 'Section|total'; done

echo "== 5) objdump -d: 심볼 이름만 빼고 명령어 비교"
# 분기 주석 <_rb_push+0x24> 같은 심볼 이름은 맹글링 때문에 당연히 다르므로 지운다
dis() { objdump -d --no-show-raw-insn --no-leading-addr "$1" | grep -E '^\s' | sed 's/ <[^>]*>//' ; }
dis "$O/c.o"   > "$O/c.s"
dis "$O/cpp.o" > "$O/cpp.s"
wc -l < "$O/c.s" | sed 's/^/    C   instructions: /'
wc -l < "$O/cpp.s" | sed 's/^/    C++ instructions: /'
if diff "$O/c.s" "$O/cpp.s" > "$O/diff.txt"; then echo "    IDENTICAL"; else echo "    DIFF:"; cat "$O/diff.txt"; fi

echo "== 6) nm: 함수 이름 (C++ 은 맹글링 — CPP-B02)"
nm "$O/c.o"   | grep -E ' [TSD] ' | sed 's/^/    C   /'
nm "$O/cpp.o" | grep -E ' [TSD] ' | sed 's/^/    C++ /'
echo "== 7) NAIVE_CONST: extern 없이 const 전역을 두면?"
nm "$O/cpp_naive.o" | grep -E 'led_cfg|mode_names' || echo "    led_cfg / mode_names 심볼 없음 (내부 링크 → 미사용 → 제거)"
