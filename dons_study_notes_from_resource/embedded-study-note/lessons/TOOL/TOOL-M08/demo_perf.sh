#!/usr/bin/env bash
# TOOL-M08 demo — how noisy is `app bench`, and what did c088 really trade?
#   bash demo_perf.sh > demo_perf_out.txt 2>&1
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
D=/tmp/TOOL-M08_work/demo_perf
rm -rf "$D" && mkdir -p "$D" || exit 2
bash gen_repo.sh "$D/repo" >/dev/null; cd "$D/repo" || exit 2
rev() { git rev-list -1 --grep "^\[c$1\]" main; }
for c in 087 088; do
	mkdir -p "$D/c$c"; git archive "$(rev $c)" | tar -x -C "$D/c$c"; make -s -C "$D/c$c" app >/dev/null
done
ns() { "$1" bench | sed -n 's/.* ns=\([0-9]*\)$/\1/p'; }
echo "### 9 interleaved runs (ms): c087 (table) vs c088 (bitwise)"
for k in 1 2 3 4 5 6 7 8 9; do
	a=$(ns "$D/c087/build/app"); b=$(ns "$D/c088/build/app")
	echo "$a $b" >> "$D/pairs.txt"
	awk -v a="$a" -v b="$b" -v k="$k" 'BEGIN { printf "run %d  c087 %6.1f  c088 %6.1f  ratio %.2f\n", k, a/1e6, b/1e6, b/a }'
done
sort -n -k1 "$D/pairs.txt" | awk '{ print $1 }' > "$D/a.txt"; sort -n -k1 "$D/pairs.txt" | awk '{ print $2 }' | sort -n > "$D/b.txt"
awk 'NR==FNR { a[NR] = $1; na = NR; next } { b[FNR] = $1; nb = FNR }
     END { printf "c087 min/median/max %.1f / %.1f / %.1f ms\n", a[1]/1e6, a[5]/1e6, a[na]/1e6;
           printf "c088 min/median/max %.1f / %.1f / %.1f ms\n", b[1]/1e6, b[5]/1e6, b[nb]/1e6;
           printf "ratio of medians %.2f   worst single-run ratio (c088 min / c087 max) %.2f\n", b[5]/a[5], b[1]/a[na] }' "$D/a.txt" "$D/b.txt"

echo; echo "### what c088 saves on a Cortex-M4 (arm-none-eabi-gcc -Os, object only)"
for c in 087 088; do
	arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -std=c11 -ffreestanding -I"$D/c$c/include" -c "$D/c$c/src/crc16.c" -o "$D/crc_c$c.o"
	echo "c$c: $(arm-none-eabi-size -A "$D/crc_c$c.o" | awk '$1 ~ /^\.(text|data|bss|rodata)/ { printf "%s=%s ", $1, $2 }')"
done
echo "(the table is filled at run time by crc16_init() -> it lives in .bss = RAM, not flash)"
