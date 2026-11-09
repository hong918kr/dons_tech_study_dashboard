#!/usr/bin/env python3
"""LPERF-A03 추가 연습: mini-c2c 모델 확장. 모든 숫자는 모델 출력이며 perf 결과가 아니다.

  python3 -I practice.py    # N/M passed, 실패가 있으면 exit 1
"""
import collections
import importlib.util
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("lperf_a03_solution", os.path.join(HERE, "solution.py"))
sol = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sol)  # solution.py 의 Event/simulate/classify/parse_trace 를 재사용


# 연습 1. HITM 을 "누가 만들었나" (load site, 마지막으로 쓴 site)
def who_caused(events, line_size=64):
    last_writer = {}  # 라인 주소 -> (tid, site)
    states = collections.defaultdict(sol.LineState)
    pairs = collections.Counter()
    for e in events:
        base = e.addr & ~(line_size - 1)
        out = sol.step(states[base], e.tid, e.op)
        if out == "HITM":
            pairs[(e.site, last_writer[base][1])] += 1
        if e.op == "W":
            last_writer[base] = (e.tid, e.site)
    return pairs


# 연습 2. 샘플링을 더 성기게 해도 맨 위 라인은 그대로인가
def downsample(events, keep_one_in, seed):
    rng = random.Random(seed)
    return [e for e in events if rng.randrange(keep_one_in) == 0]


def top_line(events, line_size=64):
    lines = sol.simulate(events, line_size)
    table = sol.line_table(lines, show_all=True)
    return table[0]["addr"] if table and table[0]["hitm"] else None


# 연습 3. NUMA: 주인이 다른 노드에 있으면 RmtHitm
def node_hitm(events, tid_node, line_size=64):
    states = collections.defaultdict(sol.LineState)
    lcl = rmt = 0
    for e in events:
        base = e.addr & ~(line_size - 1)
        st = states[base]
        owner = next(iter(st.holders)) if st.state == "M" and st.holders else None
        out = sol.step(st, e.tid, e.op)
        if out == "HITM":
            if tid_node[owner] == tid_node[e.tid]:
                lcl += 1
            else:
                rmt += 1
    return lcl, rmt


# 연습 4. 코드를 고치기 전에 레이아웃을 바꿔 보기 (what-if)
def remap(events, mapping):
    return [e._replace(addr=mapping(e.addr)) for e in events]


def hidden_to_padded(base, slot0_off=0x10, slot_size=24, new_line=128):
    """demo_stats.c hidden 의 주소를 "라인마다 한 주체" 배치로 옮기는 함수를 돌려준다."""
    def mapping(addr):
        off = addr - base
        if off < 8:
            return base + off               # limit, flags: 읽기 위주 라인
        if off < slot0_off:
            return base + new_line          # total_ops: 자기 라인
        slot, field = divmod(off - slot0_off, slot_size)
        return base + new_line * (2 + slot) + field
    return mapping


# 연습 5. 라인 크기 스캔 (perf c2c --double-cl 의 직접 판)
def granule_scan(events, sizes):
    return {s: sum(l.hitm for l in sol.simulate(events, s).values()) for s in sizes}


# ---------------------------------------------------------------- 하네스
def main():
    passed = failed = 0

    def CHECK(name, cond, detail=""):
        nonlocal passed, failed
        if cond:
            passed += 1
            print("PASS  " + name)
        else:
            failed += 1
            print("FAIL  %s  %s" % (name, detail))

    pp = sol.parse_trace(sol.G_PINGPONG)
    p = who_caused(pp)
    CHECK("연습 1: 쌍 (b<-a)=2, (a<-b)=1", p == {("site_b", "site_a"): 2, ("site_a", "site_b"): 1}, repr(p))
    tr = sol.parse_trace(sol.G_TRUE)
    CHECK("연습 1: true sharing 은 자기 site 끼리", who_caused(tr) == {("inc", "inc"): 2}, repr(who_caused(tr)))

    with open(os.path.join(HERE, "trace_hidden.txt")) as f:
        text = f.read()
    ev = sol.parse_trace(text)
    full_top = top_line(ev)
    CHECK("연습 2: 전체 trace 의 맨 위 라인", full_top is not None, full_top)
    thin2 = downsample(ev, 2, seed=7)
    thin4 = downsample(ev, 4, seed=7)
    CHECK("연습 2: 1/2 로 줄임", 0 < len(thin2) < len(ev), len(thin2))
    CHECK("연습 2: 1/4 로 줄임", 0 < len(thin4) < len(thin2), len(thin4))
    CHECK("연습 2: 1/2 에서도 맨 위 라인 같음", top_line(thin2) == full_top, (top_line(thin2), full_top))
    CHECK("연습 2: 1/4 에서도 맨 위 라인 같음", top_line(thin4) == full_top, (top_line(thin4), full_top))

    # 0(n0) W, 1(n0) R = lcl / 2(n1) R: 이미 S 라서 HITM 아님 / 2(n1) W / 0(n0) R = rmt
    ev3 = sol.parse_trace("0 0 0x100 W a\n1 1 0x108 R b\n2 2 0x110 R c\n3 2 0x110 W c\n4 0 0x100 R a\n")
    CHECK("연습 3: Lcl 1 / Rmt 1", node_hitm(ev3, {0: 0, 1: 0, 2: 1}) == (1, 1), node_hitm(ev3, {0: 0, 1: 0, 2: 1}))
    CHECK("연습 3: 한 노드면 전부 Lcl", node_hitm(ev3, {0: 0, 1: 0, 2: 0}) == (2, 0), node_hitm(ev3, {0: 0, 1: 0, 2: 0}))

    base = int(re.search(r"base=(0x[0-9a-f]+)", text).group(1), 16)
    moved = sol.simulate(remap(ev, hidden_to_padded(base)), 64)
    hot = [l for l in moved.values() if l.hitm]
    CHECK("연습 4: what-if 후 HITM 라인 1개", len(hot) == 1, [hex(l.addr) for l in hot])
    CHECK("연습 4: 남은 건 total_ops (TRUE)", hot and sol.classify(hot[0])[0] == "TRUE" and hot[0].addr == base + 128, hot and hex(hot[0].addr))

    adj = sol.parse_trace(sol.G_ADJACENT)
    scan = granule_scan(adj, [32, 64, 128, 256])
    CHECK("연습 5: 32/64 는 0, 128/256 은 3", scan == {32: 0, 64: 0, 128: 3, 256: 3}, repr(scan))
    with open(os.path.join(HERE, "trace_padded64.txt")) as f:
        p64 = sol.parse_trace(f.read())
    s64 = granule_scan(p64, [64, 128])
    CHECK("연습 5: CL=64 패딩은 128B 관점에서 HITM 이 늘어난다", s64[128] > s64[64], repr(s64))

    print("%d/%d passed" % (passed, passed + failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
