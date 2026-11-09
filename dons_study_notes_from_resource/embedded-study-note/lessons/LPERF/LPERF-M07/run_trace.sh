#!/bin/bash
# LPERF-M07 run_trace.sh <mode> <workers> <trace_secs>  — 서버를 띄우고 그 위에 futex_wait.bt 를 건다 (VM 안에서 실행)
# 서버는 trace 보다 3 초 오래 산다: ustack 심볼 해석은 출력 시점에 프로세스가 살아 있어야 한다.
set -u
mode=${1:-global}; nw=${2:-5}; secs=${3:-8}
out=$(mktemp -d /tmp/LPERF-M07_trace.XXXXXX)
./server "$mode" "$nw" $((secs + 3)) > "$out/server.txt" &
srv=$!
for _ in $(seq 1 100); do grep -q READY "$out/server.txt" 2>/dev/null && break; sleep 0.05; done
timeout $((secs + 20)) sudo bpftrace futex_wait.bt "$srv" "$secs" > "$out/bt.txt" 2>&1
wait "$srv"
echo "== server =="; cat "$out/server.txt"; echo "== bpftrace =="; cat "$out/bt.txt"
