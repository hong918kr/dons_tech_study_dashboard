#!/bin/bash
# CPP-A08 doctest_check.sh — solution.cpp 의 테스트 본문이 "진짜" doctest 로도 그대로 컴파일·통과하는지 확인 (맥, 성공 시 exit 0)
# doctest.h 는 레슨에 넣지 않는다: 고정 버전(v2.4.11)을 DOCTEST_DIR(기본 /tmp/cpp_a08_doctest)로 받아 쓴다.
set -eu
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${DOCTEST_DIR:=/tmp/cpp_a08_doctest}"
cd "$(dirname "$0")"
if [ ! -f "$DOCTEST_DIR/doctest.h" ]; then
    mkdir -p "$DOCTEST_DIR"
    curl -sSfL -o "$DOCTEST_DIR/doctest.h" https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
fi
echo "doctest $(grep -m3 -oE 'define DOCTEST_VERSION_(MAJOR|MINOR|PATCH) [0-9]+' "$DOCTEST_DIR/doctest.h" | awk '{printf "%s%s", (NR>1?".":""), $3}')"
OUT=$(mktemp -d /tmp/cpp_a08_dt.XXXXXX); trap 'rm -rf "$OUT"' EXIT
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DUSE_DOCTEST -I"$DOCTEST_DIR" solution.cpp -o "$OUT/sol_doctest"
"$OUT/sol_doctest" | tail -3
# 함정 하나: doctest 의 CHECK 는 식을 분해(decompose)하므로 '&&' / '||' 가 들어간 식은 컴파일 에러다
printf '#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "doctest.h"\nTEST_CASE("x") { int a = 1, b = 2; CHECK(a == 1 && b == 2); }\n' > "$OUT/and.cpp"
if c++ -std=c++20 -fsyntax-only -I"$DOCTEST_DIR" "$OUT/and.cpp" 2>"$OUT/and.log"; then echo "(&& 가 통과했다?)"
else grep -m1 -E 'error' "$OUT/and.log" | sed "s|$DOCTEST_DIR/||"; fi
