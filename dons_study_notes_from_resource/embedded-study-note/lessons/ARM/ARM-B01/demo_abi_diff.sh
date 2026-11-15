#!/bin/bash
# ARM-B01 demo_abi_diff.sh — demo_abi_diff.c 를 두 ABI 로 -O2 컴파일하고 함수별 asm 을 나란히 뽑는다
# 실행: bash demo_abi_diff.sh            (macOS, clang 만 있으면 됨. 링크는 안 한다)
set -e
cd "$(dirname "$0")"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
for t in arm64-apple-macos14 aarch64-linux-gnu; do
  out=/tmp/ARM-B01_abi_$t.s
  clang --target=$t -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_abi_diff.c -o "$out"
  echo "================ $t ================"
  for f in call_printf take_char take_schar call_char ld_size; do
    # 함수 라벨부터 ret / b 꼬리호출까지, 지시어·주석 줄은 빼고
    awk -v f="$f" '
      $0 ~ "^_?"f":" {on=1; print; next}
      on && /^[ \t]*(\.|;|\/\/)/ {next}
      on && /^L[a-z]*[0-9]*:$/ {next}
      on {sub(/[ \t]+(;|\/\/).*$/, ""); print}
      on && ($1=="ret" || $1=="b") {on=0}
    ' "$out"
  done
  n=$(grep -v '^\s*\.file' "$out" | grep -cwE 'x18|w18' || true)
  echo "pressure(): x18/w18 를 언급한 줄 수 = $n"
done
n=$(clang --target=aarch64-linux-gnu -ffixed-x18 -std=c11 -O2 -S demo_abi_diff.c -o - | grep -v '^\s*\.file' | grep -cwE 'x18|w18' || true)
echo "aarch64-linux-gnu -ffixed-x18: x18/w18 줄 수 = $n"
