#!/bin/bash
# N06G: build the mini test runner, run "nightly" campaigns on FPGA and SILICON,
# run negative controls (fault injection + checker mutation), then classify.
# macOS: default SDK link broken -> explicit -isysroot.
set -u
cd "$(dirname "$0")"
OUT=outputs
BIN="${TMPDIR:-/tmp}/n06g_runner"
mkdir -p "$OUT"
rm -f "$OUT"/*.log
exec > >(tee "$OUT/run_output.txt") 2>&1

cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined test_runner.c -o "$BIN" || exit 1

# 3 "nights" x 2 platforms, same build, different base seeds, 4 repeats each
for night in 1 2 3; do
  for plat in fpga silicon; do
    "$BIN" --plat "$plat" --build g1a2b3c --seed "$((0x1000 * night))" --repeat 4 \
      > "$OUT/${plat}_night${night}.log"
    echo "[$plat night$night] exit=$? $(grep MAILBOX "$OUT/${plat}_night${night}.log")"
  done
done

# negative control 1: campaign-wide DMA bit flip -> DMA tests MUST FAIL
"$BIN" --plat silicon --build g1a2b3c --seed 0x77 --inject dma_bitflip > "$OUT/negctrl_bitflip.log"
# negative control 2: same injection, but the checker is mutated to be blind
"$BIN" --plat silicon --build g1a2b3c --seed 0x77 --inject dma_bitflip --mutate skip_compare \
  > "$OUT/negctrl_mutated.log"
# timeout-scaling demo: silicon-us timeouts used unscaled on a 20x slower FPGA
"$BIN" --plat fpga --build g1a2b3c --seed 0x77 --no-timeout-scale > "$OUT/fpga_noscale.log"

echo; echo "################ classify: 6 nightly logs ################"
python3 -I classify_results.py "$OUT"/fpga_night*.log "$OUT"/silicon_night*.log

echo; echo "################ negative control 1: bit flip ################"
grep -E "^RESULT (SELFTEST|DMA)" "$OUT/negctrl_bitflip.log"
python3 -I classify_results.py --negctrl "$OUT/negctrl_bitflip.log" --expect-fail DMA_COPY_4K,DMA_RANDOM_SIZES
echo "negctrl1 rc=$?"

echo; echo "################ negative control 2: bit flip + mutated checker ################"
grep -E "^RESULT (SELFTEST|DMA)" "$OUT/negctrl_mutated.log"
python3 -I classify_results.py --negctrl "$OUT/negctrl_mutated.log" --expect-fail DMA_COPY_4K,DMA_RANDOM_SIZES
echo "negctrl2 rc=$? (expected 1: the mutation must be detected)"

echo; echo "################ FPGA without timeout scaling ################"
grep -E "^(RUN|RESULT)" "$OUT/fpga_noscale.log" | grep -E "RUN|TIMEOUT"

echo; echo "################ repro one flaky failure by seed ################"
seed=$(grep -h "ETH_PHY_LINK_UP TIMEOUT" "$OUT"/silicon_night*.log | head -1 | sed -E 's/.* seed=(0x[0-9a-f]+).*/\1/')
for k in 1 2 3; do "$BIN" --plat silicon --build g1a2b3c --only ETH_PHY_LINK_UP --seed "$seed" --repeat 1 | grep RESULT; done

echo; echo "################ auto-bisect demo (git bisect run, 0/1/125) ################"
./bisect_demo.sh
exit 0
