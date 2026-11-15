#!/bin/bash
# ARM-M06 demo_insn_check: 리덕션 명령이 실제로 있나? 어셈블러에 한 줄씩 물어본다.
#   bash demo_insn_check.sh
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
T=/tmp/ARM-M06_insn; mkdir -p $T
while IFS= read -r insn; do
  printf '%s\n' "$insn" > $T/t.s
  printf '%-30s ' "$insn"
  if cc -c $T/t.s -o $T/t.o 2>$T/err.txt; then echo "OK   $(/usr/bin/objdump -d $T/t.o | tail -1 | cut -f2-)"
  else echo "ERR  $(head -1 $T/err.txt | sed 's/.*error: //')"; fi
done <<'LIST'
faddv s0, v0.4s
faddp v0.4s, v0.4s, v0.4s
faddp s0, v0.2s
fmaxv s0, v0.4s
fmaxv d0, v0.2d
fmaxnmv s0, v0.4s
addv s0, v0.4s
addv d0, v0.2d
addp d0, v0.2d
saddlv d0, v0.4s
sdot v0.4s, v1.16b, v2.16b
sdot v0.4s, v1.16b, v2.4b[3]
fmla v1.4s, v2.4s, v0.s[0]
LIST
echo "--- 리눅스 generic armv8-a 타깃에서 SDOT (.arch_extension 없이 / 있이):"
printf 'sdot v0.4s, v1.16b, v2.16b\n' > $T/t.s
cc --target=aarch64-linux-gnu -march=armv8-a -c $T/t.s -o $T/t.o 2>&1 | head -1 | sed 's/.*error: /ERR  /'
printf '.arch_extension dotprod\nsdot v0.4s, v1.16b, v2.16b\n' > $T/t.s
cc --target=aarch64-linux-gnu -march=armv8-a -c $T/t.s -o $T/t.o && echo "OK   with .arch_extension dotprod"
