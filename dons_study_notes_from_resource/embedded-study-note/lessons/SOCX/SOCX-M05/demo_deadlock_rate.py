#!/usr/bin/env python3
"""SOCX-M05 demo: 랜덤 트래픽에서 데드락은 '언제' 나는가 — 4×4 torus, DOR(VC 1) vs dateline(VC 2) vs mesh XY.

  각 노드가 매 사이클 확률 rate 로 4-flit 패킷(목적지 균등 랜덤) 생성, INJECT_CYCLES 동안 → 그 뒤 비우기.
  seed 고정 (random.Random(seed)) → 같은 숫자가 다시 나온다. 런타임·load 는 기계 상태에 따라 다르다.

  python3 -I demo_deadlock_rate.py
"""
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True
import solution as S                # noqa: E402

INJECT_CYCLES = 300
SEEDS = range(10)
RATES = (0.02, 0.06, 0.10, 0.13, 0.16, 0.24)
LENGTH, DEPTH = 4, 2


def one(topo, route, rate, seed):
    rng = random.Random(seed)
    sim = S.WormholeSim(topo, route, depth=DEPTH, stall_window=16, max_cycles=4000)
    pid = 0
    for t in range(INJECT_CYCLES):
        for n in topo.nodes:
            if rng.random() < rate:
                d = n
                while d == n:
                    d = rng.choice(topo.nodes)
                sim.add(pid, n, d, LENGTH, at=t)
                pid += 1
    res = sim.run()
    return res, pid


def main():
    k = 4
    configs = [
        ("torus DOR, 1 VC", S.Topo(k, wrap=True, nvc=1), S.route_torus_dor),
        ("torus dateline, 2 VC", S.Topo(k, wrap=True, nvc=2), S.route_torus_dateline),
        ("mesh XY, 1 VC", S.Topo(k, wrap=False, nvc=1), S.route_xy),
    ]
    t0 = time.time()
    print("4x4, %d-flit packets, VC depth %d, inject %d cycles, %d seeds per cell"
          % (LENGTH, DEPTH, INJECT_CYCLES, len(SEEDS)))
    print("%-22s %5s %9s %15s %17s %14s" % ("config", "rate", "deadlocks", "DL @cycle", "delivered (all)",
                                            "drain cycle*"))
    example = None
    for name, topo, route in configs:
        for rate in RATES:
            dl, first, deliv, tot, drain = 0, [], 0, 0, []
            for seed in SEEDS:
                res, _ = one(topo, route, rate, seed)
                deliv += res["delivered"]
                tot += res["total"]
                if res["deadlock"]:
                    dl += 1
                    first.append(res["cycle"])
                    if example is None:
                        example = (name, rate, seed, res)
                elif res["done"]:
                    drain.append(res["cycle"])
                else:
                    print("  !! %s rate %.2f seed %d: TIMEOUT without a cycle" % (name, rate, seed))
            fc = "%d..%d" % (min(first), max(first)) if first else "-"
            dc = "%d" % sorted(drain)[len(drain) // 2] if drain else "-"
            print("%-22s %5.2f %6d/%-2d %15s %10d/%-6d %14s" % (name, rate, dl, len(SEEDS), fc, deliv, tot, dc))
    if example:
        name, rate, seed, res = example
        print("\nfirst deadlock found: %s rate %.2f seed %d @cycle %d, cycle of %d buffers:"
              % (name, rate, seed, res["cycle"], len(res["deadlock"])))
        print("  " + " -> ".join(S.cname(c) for c in res["deadlock"]) + " -> back")
    print("* drain cycle = median cycle at which the last flit arrived (runs that completed); injection stops at %d"
          % INJECT_CYCLES)
    print("runtime %.1f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
