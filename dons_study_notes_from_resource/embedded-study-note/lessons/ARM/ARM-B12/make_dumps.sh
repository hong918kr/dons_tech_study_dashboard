#!/bin/bash
# ARM-B12: firmware.c 를 4개 타깃으로 -Os 컴파일 → objdump/nm/size 텍스트를 이 디렉터리에 저장.
#          + Thumb 첫 halfword 65536개 전수 스윕(objdump 가 2/4 바이트 중 무엇으로 읽나) 요약.
# 사용: bash make_dumps.sh            (맥에서, 실행 불필요 — 오브젝트만 만든다)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-B12_work
mkdir -p "$W"
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
TOOLS=/Library/Developer/CommandLineTools/usr/bin       # Apple LLVM 21 (/usr/local/bin/llvm-objdump 는 LLVM 3.2!)
OD="$TOOLS/llvm-objdump"; NM="$TOOLS/llvm-nm"; SZ="$TOOLS/llvm-size"
CFLAGS="-ffreestanding -Wall -Wextra -Werror -c"

# 이름  : 타깃 플래그
TARGETS=(
  "thumbv7em:--target=thumbv7em-none-eabi -mthumb"
  "thumbv6m:--target=thumbv6m-none-eabi"
  "armv7a:--target=armv7a-none-eabi -marm"
  "aarch64:--target=aarch64-none-elf"
)

cp "$HERE/firmware.c" "$W/firmware.c"
cd "$W"
: > sizes_tmp.txt
for t in "${TARGETS[@]}"; do
  name="${t%%:*}"; flags="${t#*:}"
  # shellcheck disable=SC2086
  clang $flags -Os $CFLAGS firmware.c -o "fw_$name.o"
  "$OD" -dr "fw_$name.o"            > "$HERE/dump_$name.txt"
  "$NM" -S -n --defined-only "fw_$name.o" > "$HERE/syms_$name.txt"
done

{
  echo "# llvm-size -A (SysV, 섹션별) — firmware.c -Os"
  for t in "${TARGETS[@]}"; do "$SZ" -A "fw_${t%%:*}.o"; done
  echo "# llvm-size (Berkeley) — text 열에는 .ARM.exidx 등 실행 전용(alloc+exec/ro) 섹션도 합쳐진다"
  "$SZ" fw_thumbv7em.o fw_thumbv6m.o fw_armv7a.o fw_aarch64.o
  echo "# .text bytes by optimisation level (Os = 위 덤프와 같은 빌드)"
  printf "%-10s %6s %6s %6s\n" target O2 Os Oz
  for t in "${TARGETS[@]}"; do
    name="${t%%:*}"; flags="${t#*:}"; row=""
    for o in O2 Os Oz; do
      # shellcheck disable=SC2086
      clang $flags -$o $CFLAGS firmware.c -o "opt_$name.o"
      row="$row $(printf %6s "$("$SZ" -A "opt_$name.o" | awk '$1==".text"{print $2}')")"
    done
    printf "%-10s%s\n" "$name" "$row"
  done
} > "$HERE/sizes.txt"

# ---- Thumb 길이 스윕: 슬롯마다 [hw1][0xbf00 nop] 을 .inst.n 으로 박고(코드 매핑 심볼 $t),
#      objdump 가 슬롯 시작에서 몇 바이트를 한 명령으로 묶는지 본다.
python3 -I - <<'EOF'
with open('sweep.S', 'w') as f:
    f.write('.syntax unified\n.thumb\n.text\n.globl sweep\nsweep:\n')
    for h in range(65536):
        f.write(f'.inst.n 0x{h:04x}\n.inst.n 0xbf00\n')
EOF
{
  echo "# Thumb 첫 halfword 전수 스윕: llvm-objdump(Apple LLVM 21) 이 슬롯 시작 hw1 을 몇 바이트로 디코드했나"
  echo "# 형식: <target> <hw1_lo> <hw1_hi> <len>   (구간 요약; 65536 슬롯 모두 동기 유지 확인)"
  for tt in thumbv7em thumbv6m; do
    clang --target=$tt-none-eabi -c sweep.S -o "sweep_$tt.o"
    "$OD" -d "sweep_$tt.o" > "sweep_$tt.txt"
    python3 -I - "$tt" "sweep_$tt.txt" <<'EOF'
import re, sys
tt, path = sys.argv[1], sys.argv[2]
lens, unk = {}, {2: 0, 4: 0}
for line in open(path):
    m = re.match(r'\s*([0-9a-f]+): ((?:[0-9a-f]{4} ?)+)\s*\t(.*)', line)
    if not m:
        continue
    a, n = int(m.group(1), 16), len(m.group(2).split()) * 2
    if a % 4 == 0:
        lens[a // 4] = n
    if '<unknown>' in m.group(3):
        unk[n] += 1
assert len(lens) == 65536, len(lens)
runs, prev = [], None
for h in range(65536):
    if lens[h] != prev:
        runs.append([h, h, lens[h]]); prev = lens[h]
    else:
        runs[-1][1] = h
for lo, hi, n in runs:
    print(f'{tt} {lo:04x} {hi:04x} {n}')
print(f'# {tt}: <unknown> 으로 찍힌 명령 수 — 2바이트 {unk[2]}, 4바이트 {unk[4]}')
EOF
  done
  echo "# 경계 부분 원문 (thumbv7em):"
  grep -E '^ +(39ff8|39ffa|39ffc|3a000|3a002|3a004|3a008):' sweep_thumbv7em.txt
} > "$HERE/thumb_sweep.txt"

ls -l "$HERE"/dump_*.txt "$HERE"/syms_*.txt "$HERE"/sizes.txt "$HERE"/thumb_sweep.txt | awk '{print $5, $NF}'
