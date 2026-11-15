#!/usr/bin/env bash
# TOOL-B13 demo_fmt.sh — 포맷 검사가 언제 살아 있고 언제 죽는지 (컴파일 경고만 본다)
set -u
cd "$(dirname "$0")"
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B13_work/fmt; rm -rf "$W"; mkdir -p "$W"
M4="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -Os -c -Wall -Wextra"
warn() { grep -E 'warning:|error:' | sed -E 's|^.*/||'; }

echo "== 1. solution.c / starter.c + -DLOG_DEMO_FMT_BUG  (LOG_D 는 꺼진 레벨, LOG_I 는 켜진 레벨) =="
for v in solution starter; do
  echo "-- $v.c:"; cc -std=c11 -Wall -Wextra -O2 -DLOG_DEMO_FMT_BUG $v.c -o $W/$v 2>&1 | warn
done
echo
echo "== 2. demo_fw.c fmt_bug(): LOG_D(\"%s\", 42) at LOG_LEVEL=3 — IF0 vs VOID0 =="
for st in IF0 VOID0; do
  echo "-- $st:"; clang $M4 -DLOG_STYLE_$st -DLOG_LEVEL=3 -DLOG_DEMO_FMT_BUG demo_fw.c -o $W/fw_$st.o 2>&1 | warn
done
echo
echo "== 3. demo_fmt.c (Mac clang) =="
for p in 1 2 3; do echo "-- PART=$p -Wall"; cc -std=c11 -Wall -c -DPART=$p demo_fmt.c -o $W/f$p.o 2>&1 | warn; done
echo "-- PART=3 -Wall -Wformat=2"; cc -std=c11 -Wall -Wformat=2 -c -DPART=3 demo_fmt.c -o $W/f3.o 2>&1 | warn
echo "-- PART=2 clang --target=thumbv7em-none-eabi"; clang $M4 -std=c11 -DPART=2 demo_fmt.c -o $W/f2m4.o 2>&1 | warn
echo "-- clang thumbv7em uint32_t ="; echo | clang --target=thumbv7em-none-eabi -dM -E - | grep '__UINT32_TYPE__'
echo
echo "== 4. demo_variadic.c (Mac clang, -Wall -Wextra -Wpedantic) =="
for std in c11 c23; do for s in 1 2 3 4; do
  printf -- "-- -std=%s STYLE=%s: " $std $s
  out=$(cc -std=$std -Wall -Wextra -Wpedantic -c -DSTYLE=$s demo_variadic.c -o $W/v.o 2>&1 | warn | sort -u)
  [ -z "$out" ] && echo "(경고 없음)" || { echo; echo "$out"; }
done; done
echo
echo "== 5. VM: arm-none-eabi-gcc 14 / gcc 15 =="
limactl shell elinux -- bash -lc "
V=/tmp/TOOL-B13_work/fmt; rm -rf \$V; mkdir -p \$V; cp '$PWD'/demo_fmt.c '$PWD'/demo_variadic.c \$V/; cd \$V
warn() { grep -E 'warning:|error:'; }
for p in 1 2; do echo \"-- arm-none-eabi-gcc PART=\$p\"; arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -std=c11 -Wall -c -DPART=\$p demo_fmt.c -o f.o 2>&1 | warn; done
echo '-- arm-none-eabi-gcc PART=2 + PRIu32 version only (-DHAVE_INTTYPES, report() 경고는 위와 같음):'
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -std=c11 -Wall -c -DPART=2 -DHAVE_INTTYPES demo_fmt.c -o f.o 2>&1 | warn | grep -c report_fixed | sed 's/^/   warnings in report_fixed: /'
echo -n '-- arm-none-eabi-gcc uint32_t = '; echo | arm-none-eabi-gcc -dM -E - | grep '__UINT32_TYPE__'
echo -n '-- gcc (aarch64 linux) PART=2: '; gcc -std=c11 -Wall -c -DPART=2 demo_fmt.c -o f.o 2>&1 | warn | wc -l | sed 's/\$/ warnings/'
for std in c11 c23; do for s in 1 2 3 4; do
  echo \"-- gcc -std=\$std -Wpedantic STYLE=\$s: \"; gcc -std=\$std -Wall -Wextra -Wpedantic -c -DSTYLE=\$s demo_variadic.c -o v.o 2>&1 | warn
done; done
"
