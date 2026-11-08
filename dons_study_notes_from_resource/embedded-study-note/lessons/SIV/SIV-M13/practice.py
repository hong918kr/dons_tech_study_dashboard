#!/usr/bin/env python3
"""SIV-M13 추가 연습 — 같은 DMA0 커버리지 모델로 회귀 운영에 쓰는 도구 만들기

  연습 1  rank_seeds(records)            : 회귀 압축 — 전체와 같은 커버리지를 내는 최소(그리디) seed 집합
  연습 2  txns_for_confidence(p, cl)     : bin 하나를 cl 확률로 한 번 이상 맞히려면 트랜잭션 몇 개가 필요한가
  연습 3  recovery_cover(records)        : 전이(transition) 커버리지 — 에러/타임아웃 직후 다음 트랜잭션의 MODE
  연습 4  to_json / from_json / merge     : 커버리지 DB 저장·복원·밤 사이 병합 (UCIS 의 축소판)

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DONE, HALF, ERR, TIMEOUT = 0x1, 0x2, 0x4, 0x8
MODE_NAMES = ("M2M", "M2P", "P2M", "SG")
BURST_NAMES = ("B1", "B4", "B8", "B16")


def load_log(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


# ---- 본문(solution.py)의 커버리지 엔진을 축약해 넣었다 (python3 -I 는 같은 폴더 import 도 막는다) ----
def bin_match(spec, v):
    if callable(spec):
        return bool(spec(v))
    if isinstance(spec, tuple):
        return spec[0] <= v <= spec[1]
    if isinstance(spec, (set, frozenset, list)):
        return v in spec
    return v == spec


class Coverpoint:
    def __init__(self, name, fn, bins, ignore=None, illegal=None, at_least=1, weight=1):
        self.name, self.fn, self.bins = name, fn, list(bins)
        self.ignore, self.illegal = list(ignore or []), list(illegal or [])
        self.at_least, self.weight = at_least, weight
        self.hits = {n: 0 for n, _ in self.bins}
        self.fail_hits = {n: 0 for n, _ in self.bins}
        self.illegal_hits = []

    def bin_names(self):
        return [n for n, _ in self.bins]

    def sample(self, value, passed=True, rec=None):
        bad = [n for n, s in self.illegal if bin_match(s, value)]
        if bad:
            self.illegal_hits.extend((n, value, rec) for n in bad)
            return []
        if any(bin_match(s, value) for _, s in self.ignore):
            return []
        got = [n for n, s in self.bins if bin_match(s, value)]
        t = self.hits if passed else self.fail_hits
        for n in got:
            t[n] += 1
        return got

    def coverage(self):
        return sum(1 for h in self.hits.values() if h >= self.at_least), len(self.hits)


class Cross(Coverpoint):
    def __init__(self, name, a, b, ignore=None, at_least=1, weight=1):
        self.name, self.a, self.b = name, a, b
        self.ignore, self.at_least, self.weight = list(ignore or []), at_least, weight
        keys = [(x, y) for x in a.bin_names() for y in b.bin_names()
                if not any((p == "*" or p == x) and (q == "*" or q == y) for p, q in self.ignore)]
        self.hits = {k: 0 for k in keys}
        self.fail_hits = {k: 0 for k in keys}

    def sample_pair(self, a_bins, b_bins, passed=True):
        t = self.hits if passed else self.fail_hits
        for x in a_bins:
            for y in b_bins:
                if (x, y) in t:
                    t[(x, y)] += 1


class Covergroup:
    def __init__(self, coverpoints, crosses):
        self.coverpoints, self.crosses, self.samples = list(coverpoints), list(crosses), 0

    def items(self):
        return self.coverpoints + self.crosses

    def sample(self, rec):
        passed = rec.get("status") == "PASS"
        got = {cp.name: cp.sample(cp.fn(rec), passed, rec) for cp in self.coverpoints}
        for x in self.crosses:
            x.sample_pair(got[x.a.name], got[x.b.name], passed)
        self.samples += 1

    def flat(self):
        c = [it.coverage() for it in self.items()]
        return sum(a for a, _ in c), sum(b for _, b in c)

    def covered_set(self):
        return {"%s.%s" % (it.name, k if isinstance(k, str) else "%s*%s" % k)
                for it in self.items() for k, h in it.hits.items() if h >= it.at_least}


LEN_BINS = [("zero", 0), ("one", 1), ("small", (2, 63)), ("medium", (64, 4095)),
            ("page", 4096), ("large", (4097, 65534)), ("max", 65535)]
IRQ_BINS = [("done", DONE), ("half_done", DONE | HALF), ("err", ERR), ("half_err", HALF | ERR),
            ("timeout", {TIMEOUT, TIMEOUT | HALF})]
IRQ_ILLEGAL = [("none", 0), ("done_with_err", lambda v: bool(v & DONE) and bool(v & ERR)),
               ("done_with_timeout", lambda v: bool(v & DONE) and bool(v & TIMEOUT))]


def build_dma_cg():
    ctrl = lambda r: int(r["ctrl"], 0)  # noqa: E731
    cp_mode = Coverpoint("cp_mode", lambda r: ctrl(r) & 3, [(n, i) for i, n in enumerate(MODE_NAMES)])
    cp_burst = Coverpoint("cp_burst", lambda r: (ctrl(r) >> 2) & 3, [(n, i) for i, n in enumerate(BURST_NAMES)])
    cp_len = Coverpoint("cp_len", lambda r: r["len"], LEN_BINS)
    cp_irq = Coverpoint("cp_irq", lambda r: int(r["int_status"], 0), IRQ_BINS, illegal=IRQ_ILLEGAL)
    return Covergroup([cp_mode, cp_burst, cp_len, cp_irq],
                      [Cross("x_mode_len", cp_mode, cp_len, ignore=[("SG", "zero")]),
                       Cross("x_burst_irq", cp_burst, cp_irq)])


def run_cg(records):
    cg = build_dma_cg()
    for r in records:
        cg.sample(r)
    return cg


def by_seed(records):
    d = {}
    for r in records:
        d.setdefault(r["seed"], []).append(r)
    return d


# ---------------------------------------------------------------- 연습 1: 회귀 압축 (그리디 set cover)
def rank_seeds(records):
    """전체 로그와 같은 covered_set 을 내는 seed 목록을 그리디로 고른다.
    매 단계 '아직 안 덮인 bin 을 가장 많이 새로 덮는 seed' (동률이면 작은 seed 번호).
    → [(seed, 새로 덮은 bin 수)]"""
    sets = {s: run_cg(rs).covered_set() for s, rs in by_seed(records).items()}
    goal = set().union(*sets.values())
    have, out = set(), []
    while have != goal:
        s = min(sets, key=lambda k: (-len(sets[k] - have), k))
        gain = len(sets[s] - have)
        if gain == 0:
            break
        out.append((s, gain))
        have |= sets[s]
    return out


# ---------------------------------------------------------------- 연습 2: 필요한 트랜잭션 수
def txns_for_confidence(p, cl=0.95):
    """트랜잭션마다 독립적으로 확률 p 로 bin 을 맞힐 때, P(n 개 안에 한 번 이상) >= cl 인 최소 n.
    1 - (1-p)^n >= cl  ⇔  n >= ln(1-cl) / ln(1-p)"""
    if p <= 0:
        return math.inf
    if p >= 1:
        return 1
    return math.ceil(math.log(1.0 - cl) / math.log1p(-p))


def p_len_bin(ver, value):
    """gen_runlog.py 의 길이 생성 확률 (30% interesting 목록에서 균등, 70% randint(1, 65535))"""
    lst = {"v1": [1, 2, 63, 64, 4095, 4096, 4097],
           "v2": [0, 1, 2, 63, 64, 4095, 4096, 4097, 65534, 65535]}[ver]
    return 0.3 * lst.count(value) / len(lst) + (0.7 / 65535 if 1 <= value <= 65535 else 0.0)


# ---------------------------------------------------------------- 연습 3: 에러 복구 전이 커버리지
def irq_class(st):
    if st & ERR:
        return "err"
    if st & TIMEOUT:
        return "timeout"
    return "ok"


def recovery_cover(records):
    """같은 seed 안에서 연속한 PASS 트랜잭션 (prev, next) 쌍마다, prev 가 err/timeout 이면
    bin (prev 클래스, next 의 MODE) 를 센다. → {('err'|'timeout', MODE): 횟수} (8개 bin 전부 키로 존재)"""
    bins = {(c, m): 0 for c in ("err", "timeout") for m in MODE_NAMES}
    for rs in by_seed(records).values():
        rs = sorted(rs, key=lambda r: r["step"])
        for prev, nxt in zip(rs, rs[1:]):
            if prev["status"] != "PASS" or nxt["status"] != "PASS":
                continue
            c = irq_class(int(prev["int_status"], 0))
            if c != "ok":
                bins[(c, MODE_NAMES[int(nxt["ctrl"], 0) & 3])] += 1
    return bins


# ---------------------------------------------------------------- 연습 4: 커버리지 DB 저장·병합
def to_json(cg):
    """hits / fail_hits / samples 만 저장 (모델 정의는 코드가 단일 소스). cross 키는 'a*b' 문자열."""
    db = {"samples": cg.samples, "items": {}}
    for it in cg.items():
        k = lambda x: x if isinstance(x, str) else "%s*%s" % x  # noqa: E731
        db["items"][it.name] = {"hits": {k(a): v for a, v in it.hits.items()},
                                "fail_hits": {k(a): v for a, v in it.fail_hits.items()}}
    return json.dumps(db, sort_keys=True)


def from_json(text):
    db = json.loads(text)
    cg = build_dma_cg()
    for it in cg.items():
        d = db["items"][it.name]
        for a in it.hits:
            key = a if isinstance(a, str) else "%s*%s" % a
            it.hits[a] = d["hits"][key]
            it.fail_hits[a] = d["fail_hits"][key]
    cg.samples = db["samples"]
    return cg


def merge(*cgs):
    out = build_dma_cg()
    for cg in cgs:
        for d, s in zip(out.items(), cg.items()):
            for a in s.hits:
                d.hits[a] += s.hits[a]
                d.fail_hits[a] += s.fail_hits[a]
        out.samples += cg.samples
    return out


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def main():
    v1, v2 = load_log("runlog_v1.jsonl"), load_log("runlog_v2.jsonl")

    print("-- 연습 1: regression compaction --")
    r2 = rank_seeds(v2)
    print("   v2 greedy: %s" % r2)
    chosen = {s for s, _ in r2}
    check("v2: greedy seeds reproduce the full covered set",
          lambda: run_cg([r for r in v2 if r["seed"] in chosen]).covered_set() == run_cg(v2).covered_set())
    check("v2: gains sum to 66 and are non-increasing",
          lambda: sum(g for _, g in r2) == 66 and all(r2[i][1] >= r2[i + 1][1] for i in range(len(r2) - 1)))
    check("v2: 32 seeds compact to 8", lambda: len(r2) == 8)

    print("-- 연습 2: transactions for 95% confidence --")
    check("p=0.5 -> n=5 (1-0.5^4=0.9375 < 0.95 <= 0.96875)", lambda: txns_for_confidence(0.5) == 5)
    check("p=1/65535 (pure uniform) -> 196,324 txns", lambda: txns_for_confidence(1 / 65535) == 196324)
    pv1, pv2 = p_len_bin("v1", 65535), p_len_bin("v2", 65535)
    print("   len=65535: v1 p=%.3g -> n95=%d ; v2 p=%.4f -> n95=%d ; v2 M2P*max p=%.5f -> n95=%d"
          % (pv1, txns_for_confidence(pv1), pv2, txns_for_confidence(pv2),
             pv2 / 4, txns_for_confidence(pv2 / 4)))
    check("v1 len=65535 needs ~280k txns (log has 642)", lambda: 280000 < txns_for_confidence(pv1) < 281000)
    check("v2 len=65535 needs 99 txns", lambda: txns_for_confidence(pv2) == 99)
    check("v2 log: observed count of len=65535 within 3 sigma of 640*p",
          lambda: abs(sum(1 for r in v2 if r["len"] == 65535 and r["test"] == "dma_rand") - 640 * pv2)
          <= 3 * math.sqrt(640 * pv2 * (1 - pv2)))

    print("-- 연습 3: error-recovery transition coverage --")
    rc1, rc2 = recovery_cover(v1), recovery_cover(v2)
    print("   v1: %s" % {"%s>%s" % k: v for k, v in rc1.items()})
    print("   v2: %s" % {"%s>%s" % k: v for k, v in rc2.items()})
    check("8 bins defined", lambda: len(rc1) == 8)
    check("v1 recovery: all 8 (err|timeout) -> MODE bins hit",
          lambda: all(v >= 1 for v in rc1.values()))
    seq = [{"seed": 0, "step": i, "status": "PASS", "ctrl": c, "int_status": s}
           for i, (c, s) in enumerate([("0x10", "0x4"), ("0x13", "0x1"), ("0x11", "0x8"), ("0x11", "0x1")])]
    check("hand sequence: err->SG and timeout->M2P only",
          lambda: {k: v for k, v in recovery_cover(seq).items() if v} == {("err", "SG"): 1, ("timeout", "M2P"): 1})

    print("-- 연습 4: coverage DB save / restore / merge --")
    c1, c2 = run_cg(v1), run_cg(v2)
    j1 = to_json(c1)
    check("json round-trip keeps flat 56/67 and samples", lambda: from_json(j1).flat() == (56, 67)
          and from_json(j1).samples == 642)
    m = merge(from_json(j1), from_json(to_json(c2)))
    check("merge(v1, v2) == sample(v1 + v2)",
          lambda: m.covered_set() == run_cg(v1 + v2).covered_set() and m.samples == 1284)
    check("merged nights: 66/67, M2P*max still blocked (fail=4)",
          lambda: m.flat() == (66, 67) and m.crosses[0].fail_hits[("M2P", "max")] == 4)
    check("json is deterministic (sort_keys)", lambda: to_json(from_json(j1)) == j1)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
