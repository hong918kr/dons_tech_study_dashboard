#!/usr/bin/env python3
"""DIST-A05 데모: 같은 seed 집합으로 설정별 위반을 세고 최소 반례를 출력한다.

  python3 -I demo_sweep.py [seed 개수, 기본 1000]
시뮬레이터가 결정적이므로 위반 수는 실행할 때마다 같다. 시간만 달라진다.
"""
import importlib.util
import os
import sys
import time

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "solution.py")
spec = importlib.util.spec_from_file_location("dist_a05", path)
s = importlib.util.module_from_spec(spec)
sys.modules["dist_a05"] = s
spec.loader.exec_module(s)

NOFAULT = dict(slow_p=0, drop_p=0, lag_p=0, crash_at=None)


def row(cfg, seeds):
    t0 = time.perf_counter()
    r = s.sweep(cfg, seeds)
    dt = time.perf_counter() - t0
    print("%-24s %3d+%d=%d  %5d/%-5d  %5.1f%%  %5d  %5.2fs" % (
        cfg.name, cfg.R, cfg.W, cfg.R + cfg.W, len(r.violations), r.seeds,
        100.0 * r.completed / r.total,
        len(r.minimal) if r.minimal else 0, dt))
    return r


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    seeds = range(n)
    print("config                   R+W        viol/seeds  done%   min    time")
    results = []
    for cfg in s.CONFIGS:
        results.append((cfg, row(cfg, seeds)))
    print()
    print("같은 설정, 결함 없음 (지연 1..10 만):")
    for name in ("R1W1 none", "R2W2 none", "R2W2 sync (ABD)"):
        c = s.cfg_named(name)
        row(s.Cfg(name + " nofault", R=c.R, W=c.W, writeback=c.writeback, **NOFAULT), seeds)
    print()
    print("대조군: amnesia 만 뺀 R2W2 sync (crash 도 없음):")
    row(s.Cfg("R2W2 sync no-crash", writeback="sync", crash_at=None), seeds)
    print()
    print("같은 클라이언트가 timeout 뒤 다시 쓸 때의 ts 타이브레이커 (timeout=40):")
    tb = []
    for tie in ("client", "op"):
        c = s.Cfg("R2W2 sync tie=" + tie, writeback="sync", tie=tie, timeout=40)
        tb.append((c, row(c, seeds)))
    print()
    for cfg, r in results + tb:
        if r.minimal:
            print("[%s] seed=%d key=%s  %d개 위반 seed 중 앞 %d개를 줄인 최소 반례 (%d ops)" % (
                cfg.name, r.first_seed, r.first_key, len(r.violations),
                min(len(r.violations), s.SHRINK_LIMIT), len(r.minimal)))
            print(s.render(r.minimal))
            print()


if __name__ == "__main__":
    main()
