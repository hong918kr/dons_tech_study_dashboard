#!/usr/bin/env python3
"""DIST-A05 데모: 검사기 실행 시간과 탐색 노드 수.

  표 1  최악 입력: 쓰기 n 개가 전부 겹치고 마지막 읽기가 불가능한 값 → 메모 있음/없음
  표 2  키 분할의 효과: 키 k 개 × 키당 동시 쓰기 w 개를 통째로 vs 키별로
  표 3  시뮬레이터 히스토리 길이를 늘릴 때 (동시성 폭은 클라이언트 수로 제한됨)
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


def timed(ops, **kw):
    t0 = time.perf_counter()
    try:
        r = s.check_linearizable(ops, **kw)
        return r.nodes, time.perf_counter() - t0, r.ok
    except s.SearchBudget:
        return None, time.perf_counter() - t0, None


def table1():
    print("표 1: 최악 입력 (쓰기 n 개 전부 겹침 + 불가능한 읽기 1개)")
    print("   n    memo nodes   memo sec    no-memo nodes  no-memo sec")
    for n in (4, 6, 8, 9, 10, 12, 14, 16):
        ops = s.worst_case(n)
        mn, mt, _ = timed(ops)
        if n <= 9:
            nn, nt, _ = timed(ops, memo=False, max_nodes=8000000)
            tail = "%14s  %10.3f" % (nn if nn is not None else ">limit", nt)
        else:
            tail = "%14s  %10s" % ("(skip)", "-")
        print("%4d  %12d  %9.3f  %s" % (n, mn, mt, tail))


def table2():
    print()
    print("표 2: 키 분할 (키 k 개 × 키당 겹친 쓰기 w 개, 키마다 불가능한 읽기)")
    print("   k   w  whole nodes   whole sec   per-key nodes  per-key sec")
    for k, w in ((2, 6), (3, 5), (2, 8), (3, 6)):
        ops = []
        for j in range(k):
            key = "k%d" % j
            ops += [s.Op(i, key, "w", (j + 1) * 100 + i, i, w + i) for i in range(w)]
            ops.append(s.Op(w, key, "r", -1 - j, 2 * w + 1, 2 * w + 2))
        wn, wt, _ = timed(ops, model=s.KvModel, max_nodes=4000000)
        t0 = time.perf_counter()
        _, _, res = s.check_kv(ops)
        pt = time.perf_counter() - t0
        pn = sum(r.nodes for r in res.values())
        print("%4d %3d  %11s  %10.3f  %14d  %11.4f" % (k, w, wn if wn is not None else ">limit", wt, pn, pt))


def table3():
    print()
    print("표 3: 시뮬레이터 히스토리 (R2W2 sync, 클라이언트 4 → 겹침 폭이 작다). 결함 켬 vs 결함 끔(미완료 0개)")
    print(" faults  ops/client   ops total  pending   check sec     nodes")
    quiet = dict(slow_p=0, drop_p=0, lag_p=0, crash_at=None)
    for label, kw in (("on", {}), ("off", quiet)):
        for per in (12, 50, 200, 800):
            cfg = s.Cfg("x", writeback="sync", ops_per_client=per, **kw)
            hist = s.QuorumStore(cfg, 1).run()
            pend = sum(1 for op in hist if op.t_ret is None)
            t0 = time.perf_counter()
            ok, _, res = s.check_kv(hist)
            dt = time.perf_counter() - t0
            print("%7s  %10d  %10d  %7d  %10.4f  %8d  ok=%s" % (
                label, per, len(hist), pend, dt, sum(r.nodes for r in res.values()), ok))


if __name__ == "__main__":
    table1()
    table2()
    table3()
