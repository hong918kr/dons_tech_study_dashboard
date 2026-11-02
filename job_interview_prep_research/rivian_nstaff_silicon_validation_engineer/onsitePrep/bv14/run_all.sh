#!/usr/bin/env bash
# Reproduce every output pasted into BV14 §6. Writes outputs/run_log.txt.
cd "$(dirname "$0")"
run() { echo "\$ python3 -I farm_orchestrator.py $*"; python3 -I farm_orchestrator.py "$@"; echo "exit=$?"; echo; }
{
  run --selftest
  echo '# negative control: FW heartbeat (5 s) slower than orchestrator idle timeout (3 s)'
  echo "\$ python3 -I farm_orchestrator.py --heartbeat 5 | sed -n '/PER-TEST/,/^\$/p'"
  python3 -I farm_orchestrator.py --heartbeat 5 | sed -n '/PER-TEST/,/^$/p'
  echo '# same farm, 6 nightly seeds: what B04 (true p_fail=0.2 on ddr_march_c) looks like'
  for s in 1 2 3 4 5 6; do echo "seed $s: $(python3 -I farm_orchestrator.py --seed $s | grep '^B04')"; done
  echo
  run --loop B04 ddr_march_c 500
  run --loop B01 ddr_march_c 300
  run --loop B08 ddr_march_c 50 --temp 85
  run --loop B08 ddr_march_c 50 --temp 25
  run --loop B06 pcie_link 20 --temp 25
  run --loop B07 pll_lock 5
  echo '# final default run (seed 1) - writes outputs/results.json, summary.txt, repro/'
  run
} > outputs/run_log.txt 2>&1
rm -rf outputs/loop_scratch
