#!/bin/bash
# CPP-A06 steps.sh — clang 의 constexpr step 한도 실측 (맥에서, 레슨 디렉터리 기준으로 실행)
#   bash steps.sh limit   기본 한도가 몇인지: spin(N) 이 실패하기 시작하는 N 을 찾고 -fconstexpr-steps 값과 맞춰 본다
#   bash steps.sh count   pll_search / naive / pll_solve_fast 가 실제로 쓰는 step 수 (통과하는 최소 -fconstexpr-steps 이분 탐색)
#   bash steps.sh time    컴파일 시간 (-fsyntax-only, 3회 중 최소)
# 끝까지 돌면 exit 0.
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
cd "$(dirname "$0")"
CXX="c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only demo_steps.cpp"
ok() { $CXX "$@" >/dev/null 2>&1; }
minpass() {   # minpass <args...> → 통과하는 최소 -fconstexpr-steps
    local lo=1 hi=1
    while ! ok "$@" -fconstexpr-steps=$hi; do lo=$hi; hi=$((hi * 2)); done
    while [ $((hi - lo)) -gt 1 ]; do
        local mid=$(((lo + hi) / 2))
        if ok "$@" -fconstexpr-steps=$mid; then hi=$mid; else lo=$mid; fi
    done
    echo $hi
}
case "${1:-count}" in
limit)
    lo=1; hi=4194304
    while [ $((hi - lo)) -gt 1 ]; do mid=$(((lo + hi) / 2)); if ok -DVARIANT=3 -DSPIN=$mid; then lo=$mid; else hi=$mid; fi; done
    echo "default: spin($lo) passes, spin($hi) fails"
    need=$(minpass -DVARIANT=3 -DSPIN=$lo); echo "spin($lo) needs -fconstexpr-steps >= $need"
    ok -DVARIANT=3 -DSPIN=$hi -fconstexpr-steps=$need || echo "spin($hi) fails with -fconstexpr-steps=$need too"
    ;;
count)
    for c in "8000000u 168000000u 1" "25000000u 168000000u 1" "26000000u 168000000u 1" "12288000u 168000000u 1" "12288000u 168000000u 0"; do
        set -- $c
        printf "HSE=%-10s SYS=%s USB=%s  search=%-8s fast=%-6s naive=%s\n" $1 $2 $3 \
            "$(minpass -DVARIANT=0 -DHSE=$1 -DSYS=$2 -DUSB=$3)" "$(minpass -DVARIANT=2 -DHSE=$1 -DSYS=$2 -DUSB=$3)" \
            "$(minpass -DVARIANT=1 -DHSE=$1 -DSYS=$2 -DUSB=$3)"
    done ;;
time)
    t() { local best=999; for i in 1 2 3; do s=$(python3 -c 'import time;print(time.time())')
          $CXX "$@" >/dev/null 2>&1; e=$(python3 -c 'import time;print(time.time())'); d=$(python3 -c "print(round($e-$s,3))")
          best=$(python3 -c "print(min($best,$d))"); done; echo "$best s"; }
    echo "spin(1) (baseline, solution.cpp 헤더 포함): $(t -DVARIANT=3 -DSPIN=1)"
    echo "pll_search 12.288MHz:                     $(t -DVARIANT=0)"
    echo "pll_solve_fast 12.288MHz:                 $(t -DVARIANT=2)"
    echo "naive 12.288MHz, -fconstexpr-steps=2^31-1: $(t -DVARIANT=1 -fconstexpr-steps=2147483647)"
    ;;
esac
