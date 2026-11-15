#!/usr/bin/env bash
# TOOL-B13 demo_size.sh — LOG_LEVEL / 로그 스타일별 오브젝트 크기와 문자열 테이블을 잰다.
#   Mac:  clang --target=thumbv7em-none-eabi (ELF .o) + Mac arm64 (Mach-O .o)
#   VM :  arm-none-eabi-gcc 14 (elinux Lima VM) — 실제 펌웨어 툴체인
# 사용: bash demo_size.sh            (결과는 화면에. 레슨 md 의 표는 이 출력에서 왔다)
set -u
cd "$(dirname "$0")"
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B13_work/size; rm -rf "$W"; mkdir -p "$W"
SIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size     # /usr/local/bin/llvm-size 는 LLVM 3.2 — 쓰지 말 것
OBJDUMP=/usr/bin/objdump
M4="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -Os -c -Wall -Wextra"

# 섹션 하나를 덤프해서 NUL 로 자른 문자열 목록을 만든다 → "전체 n개 (메시지 m개)" 출력
strs() {  # $1=obj $2=section  [$3=show]
  "$OBJDUMP" -s -j "$2" "$1" 2>/dev/null | python3 -c '
import sys, re
data = bytearray()
for ln in sys.stdin:
    m = re.match(r"^ ([0-9a-f]+) ((?:[0-9a-f]{2,8} ?){1,4})", ln)
    if m: data += bytes.fromhex(m.group(2).replace(" ", ""))
s = [x.decode() for x in bytes(data).split(b"\0") if x]
msg = [x for x in s if not re.fullmatch(r"[A-Za-z0-9_./-]+", x)]
print("%d strings (%d messages)" % (len(s), len(msg)))
if len(sys.argv) > 1:
    for x in s: print("    " + repr(x))
' ${3:+show}
}
sec() { "$SIZE" -A "$1" | awk -v n="$2" '$1==n{print $2}'; }

echo "== 1. thumbv7em -Os -c, style IF0 (do { if (0) ... } while (0)), LOG_LEVEL 0..4 =="
printf "%-6s %6s %8s %14s  %s\n" LEVEL .text .ARM.exidx .rodata.str1.1 strings
for L in 0 1 2 3 4; do
  o=$W/if0_$L.o; clang $M4 -DLOG_LEVEL=$L demo_fw.c -o $o || exit 1
  printf "%-6s %6s %8s %14s  %s\n" $L "$(sec $o .text)" "$(sec $o .ARM.exidx)" "$(sec $o .rodata.str1.1)" "$(strs $o .rodata.str1.1)"
done
echo
echo "== 2. 같은 것을 VOID0 (#if 로 ((void)0)) / RUNTIME (런타임 필터만) 으로 =="
for L in 0 1 2 3 4; do
  o=$W/void0_$L.o; clang $M4 -DLOG_STYLE_VOID0 -DLOG_LEVEL=$L demo_fw.c -o $o 2>$W/void0_$L.err || { cat $W/void0_$L.err; exit 1; }
  printf "VOID0 L=%s  .text=%s .rodata.str1.1=%s  same-as-IF0: %s   warnings: %s\n" $L "$(sec $o .text)" "$(sec $o .rodata.str1.1)" \
    "$(cmp -s <("$OBJDUMP" -d $o | tail -n +3) <("$OBJDUMP" -d $W/if0_$L.o | tail -n +3) && echo same-code || echo DIFF)" \
    "$(grep -c 'warning:' $W/void0_$L.err)"
done
o=$W/rt.o; clang $M4 -DLOG_STYLE_RUNTIME demo_fw.c -o $o
printf "RUNTIME     .text=%s .rodata.str1.1=%s  %s\n" "$(sec $o .text)" "$(sec $o .rodata.str1.1)" "$(strs $o .rodata.str1.1)"
echo "-- VOID0 L=1 경고 (로그에서만 쓰던 변수가 남는다):"
sed -n '1,4p' $W/void0_1.err
echo
echo "== 3. 파일 이름: __FILE_NAME__ vs __FILE__ (절대 경로로 컴파일) vs -fmacro-prefix-map, LOG_LEVEL=4 =="
ABS="$PWD/demo_fw.c"
clang $M4 -DLOG_LEVEL=4 "$ABS" -o $W/fn.o
clang $M4 -DLOG_LEVEL=4 -DLOG_FILE_FULL "$ABS" -o $W/ff.o
clang $M4 -DLOG_LEVEL=4 -DLOG_FILE_FULL -fmacro-prefix-map="$PWD/"= "$ABS" -o $W/fm.o
for v in fn ff fm; do
  printf "%s .rodata.str1.1=%-4s  file string: %s\n" $v "$(sec $W/$v.o .rodata.str1.1)" \
    "$(strs $W/$v.o .rodata.str1.1 show | grep -E "demo_fw\.c'$")"
done
echo
echo "== 4. -O0 에서도 꺼진 레벨이 사라지나? (thumbv7em, LOG_LEVEL=1) =="
for st in IF0 BOTH; do for O in -O0 -Os; do
  clang ${M4/-Os/$O} -DLOG_STYLE_$st -DLOG_LEVEL=1 demo_fw.c -o $W/o_$st$O.o
  printf "clang %-4s %-3s .text=%-5s %s\n" $st $O "$(sec $W/o_$st$O.o .text)" "$(strs $W/o_$st$O.o .rodata.str1.1)"
done; done
clang $M4 -DLOG_STYLE_BOTH -DLOG_LEVEL=4 demo_fw.c -o $W/both4.o
printf "clang BOTH -Os L=4 .text=%s .rodata.str1.1=%s (런타임 검사 비용 = IF0 L=4 대비)\n" "$(sec $W/both4.o .text)" "$(sec $W/both4.o .rodata.str1.1)"
echo
echo "== 5. Mac arm64 Mach-O (-Os -c), LOG_LEVEL 0..4 =="
for L in 0 1 2 3 4; do
  o=$W/mac_$L.o; cc -std=c11 -Os -c -Wall -Wextra -DLOG_LEVEL=$L demo_fw.c -o $o
  printf "L=%s  " $L
  "$OBJDUMP" -h $o | awk '$2=="__text"||$2=="__cstring"{print $2, $3}' | while read n h; do printf "%s=%d " $n $((16#$h)); done
  printf "%s\n" "$(strs $o __cstring)"
done
echo "-- size -m (L=4):"; size -m $W/mac_4.o
echo "-- L=3 문자열 목록 (thumbv7em):"; strs $W/if0_3.o .rodata.str1.1 show
echo
echo "== 6. VM arm-none-eabi-gcc 14 (-mcpu=cortex-m4 -mthumb -Os -c) =="
limactl shell elinux -- bash -lc "
set -u; V=/tmp/TOOL-B13_work/vm; rm -rf \$V; mkdir -p \$V; cp '$PWD/demo_fw.c' \$V/; cd \$V
G='arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -ffreestanding -Wall -Wextra -c'
rostr() { for x in \$(readelf -S -W \$1 | grep -o '\.rodata[.a-z0-9]*'); do readelf -p \$x \$1; done | grep '^ *\['; }
nstr() { rostr \$1 | grep -c .; }
printf '%-8s %6s %8s %6s\n' build .text .rodata* nstr
for L in 0 1 2 3 4; do
  \$G -Os -DLOG_LEVEL=\$L demo_fw.c -o g\$L.o
  t=\$(arm-none-eabi-size -A g\$L.o | awk '\$1==\".text\"{print \$2}')
  r=\$(arm-none-eabi-size -A g\$L.o | awk '\$1~/^.rodata/{s+=\$2} END{print s+0}')
  printf '%-8s %6s %8s %6s\n' Os_L\$L \$t \$r \$(nstr g\$L.o)
done
for O in -O0 -Og; do
  \$G \$O -DLOG_LEVEL=1 demo_fw.c -o go.o
  printf 'gcc %-3s L=1: .text=%s rodata strings=%s  (DBG/INF text present? %s)\n' \$O \
    \$(arm-none-eabi-size -A go.o | awk '\$1==\".text\"{print \$2}') \$(nstr go.o) \
    \$(rostr go.o | grep -c -E 'uart_send done|erased in')
done
echo '-- arm-none-eabi-size (Berkeley) L=0 vs L=4:'; arm-none-eabi-size g0.o g4.o
echo '-- readelf -S (L=3) rodata sections:'; readelf -S -W g3.o | grep -E 'rodata|Name'
echo '-- readelf -p .rodata (L=3): GCC 는 __func__ 를 여기 둔다'; readelf -p .rodata g3.o | grep '\['
"
echo
echo "== 7. VM 링크 (demo_link.c, -Os, nano/nosys specs): 포맷하는 로그 vs 토큰화 로그 =="
echo "   (nosys 의 '_write is not implemented' 류 링커 경고는 2>/dev/null 로 숨김)"
limactl shell elinux -- bash -lc "
V=/tmp/TOOL-B13_work/link; rm -rf \$V; mkdir -p \$V; cp '$PWD/demo_link.c' \$V/; cd \$V
G='arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os demo_link.c'
\$G --specs=nano.specs --specs=nosys.specs -o fmt_nano.elf 2>/dev/null
\$G --specs=nano.specs --specs=nosys.specs -DTOKENIZED -o tok_nano.elf 2>/dev/null
\$G --specs=nosys.specs -o fmt_full.elf 2>/dev/null
arm-none-eabi-size fmt_nano.elf tok_nano.elf fmt_full.elf
"
