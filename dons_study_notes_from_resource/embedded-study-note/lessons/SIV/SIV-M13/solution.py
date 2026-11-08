#!/usr/bin/env python3
"""SIV-M13 검증 커버리지 모델 (functional coverage)

DMA0 블록의 '무엇을 얼마나 테스트했는가' 를 SystemVerilog covergroup 과 같은 의미로 계산한다.
  Part A  coverpoint / bin(값·범위·집합·조건) / ignore·illegal 우선순위 / at_least / FAIL 샘플 분리
  Part B  bin lint(겹침·빈틈), cross 와 ignore 와일드카드, 가중 평균 vs flat 커버리지, DB merge
  Part C  야간 회귀 로그(runlog_v1/v2.jsonl) 샘플링 → hole 리포트 · illegal 리포트 · 수렴 곡선(seed 수 vs 커버리지)

  python3 -I solution.py              # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report     # 테스트 + v1/v2 hole 리포트·수렴 표·ASCII 곡선 출력

Python 3.9 표준 라이브러리만 쓴다. 로그는 gen_runlog.py 로 결정적으로 다시 만들 수 있다.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DONE, HALF, ERR, TIMEOUT = 0x1, 0x2, 0x4, 0x8
MODE_NAMES = ("M2M", "M2P", "P2M", "SG")
BURST_NAMES = ("B1", "B4", "B8", "B16")


def load_log(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def get_field(word, lsb, width):
    return (word >> lsb) & ((1 << width) - 1)


# ---------------------------------------------------------------- Part A: coverpoint / bin
def bin_match(spec, v):
    """bin 스펙 하나가 값 v 를 포함하는가.
    int → 같은 값 · (lo, hi) tuple → lo <= v <= hi (양끝 포함) · set/frozenset/list → 원소 · callable → spec(v)"""
    if callable(spec):
        return bool(spec(v))
    if isinstance(spec, tuple):
        lo, hi = spec
        return lo <= v <= hi
    if isinstance(spec, (set, frozenset, list)):
        return v in spec
    return v == spec


def field_bins(names):
    """레지스터 필드의 auto bin: 값 0..len(names)-1 마다 bin 하나 (SV 의 auto_bin 과 같은 발상)"""
    return [(n, i) for i, n in enumerate(names)]


class Coverpoint:
    def __init__(self, name, fn, bins, ignore=None, illegal=None, at_least=1, weight=1):
        self.name, self.fn = name, fn
        self.bins = list(bins)                   # [(bin 이름, 스펙)] — 커버리지 분모
        self.ignore = list(ignore or [])         # 분모에서 빠진다, 샘플돼도 무시
        self.illegal = list(illegal or [])       # 샘플되면 에러로 기록 (커버리지에 안 셈)
        self.at_least, self.weight = at_least, weight
        self.hits = {n: 0 for n, _ in self.bins}         # PASS 트랜잭션에서 맞은 횟수
        self.fail_hits = {n: 0 for n, _ in self.bins}    # FAIL 트랜잭션에서 맞은 횟수 (커버리지 아님)
        self.illegal_hits = []                           # [(illegal bin 이름, 값, 레코드)]

    def bin_names(self):
        return [n for n, _ in self.bins]

    def sample(self, value, passed=True, rec=None):
        """값 하나를 샘플한다 → 이번에 카운트된 regular bin 이름 목록.
        우선순위: illegal(기록 후 []) > ignore([]) > regular(겹치면 맞은 bin 전부 +1).
        passed=False 면 hits 대신 fail_hits 를 올린다 (돌려주는 목록은 같다)."""
        bad = [n for n, s in self.illegal if bin_match(s, value)]
        if bad:
            for n in bad:
                self.illegal_hits.append((n, value, rec))
            return []
        if any(bin_match(s, value) for _, s in self.ignore):
            return []
        got = [n for n, s in self.bins if bin_match(s, value)]
        table = self.hits if passed else self.fail_hits
        for n in got:
            table[n] += 1
        return got

    def coverage(self):
        """→ (covered, total): hits >= at_least 인 regular bin 수 / regular bin 수"""
        return sum(1 for n in self.bin_names() if self.hits[n] >= self.at_least), len(self.bins)


# ---------------------------------------------------------------- Part B: cross / 집계 / lint
class Cross:
    def __init__(self, name, a, b, ignore=None, at_least=1, weight=1):
        self.name, self.a, self.b = name, a, b
        self.ignore = list(ignore or [])         # [(a bin 이름 또는 '*', b bin 이름 또는 '*')]
        self.at_least, self.weight = at_least, weight
        self.hits = {k: 0 for k in self.bins()}
        self.fail_hits = {k: 0 for k in self.bins()}

    def is_ignored(self, an, bn):
        """(an, bn) 이 ignore 목록의 어느 패턴과 맞는가. '*' 는 아무 bin 과 맞는다."""
        return any((pa == "*" or pa == an) and (pb == "*" or pb == bn) for pa, pb in self.ignore)

    def bins(self):
        return [(an, bn) for an in self.a.bin_names() for bn in self.b.bin_names()
                if not self.is_ignored(an, bn)]

    def sample(self, a_bins, b_bins, passed=True):
        """두 coverpoint 가 이번 샘플에서 카운트한 bin 목록의 곱집합을 센다 (ignore 조합은 건너뜀)."""
        table = self.hits if passed else self.fail_hits
        for an in a_bins:
            for bn in b_bins:
                if (an, bn) in table:
                    table[(an, bn)] += 1

    def coverage(self):
        return sum(1 for k in self.hits if self.hits[k] >= self.at_least), len(self.hits)


class Covergroup:
    def __init__(self, name, coverpoints, crosses):
        self.name = name
        self.coverpoints = list(coverpoints)
        self.crosses = list(crosses)
        self.samples = 0

    def items(self):
        return self.coverpoints + self.crosses

    def sample(self, rec):
        """레코드 하나 샘플 (주어진 코드). status 가 PASS 가 아니면 fail_hits 로 간다."""
        passed = rec.get("status") == "PASS"
        got = {}
        for cp in self.coverpoints:
            got[cp.name] = cp.sample(cp.fn(rec), passed, rec)
        for x in self.crosses:
            x.sample(got[x.a.name], got[x.b.name], passed)
        self.samples += 1

    def flat(self):
        """→ (covered, total): 모든 coverpoint·cross 의 bin 을 한 줄로 센 커버리지"""
        c = t = 0
        for it in self.items():
            ci, ti = it.coverage()
            c, t = c + ci, t + ti
        return c, t

    def coverage(self):
        """SV get_coverage() 와 같은 가중 평균 (%): Σ weight_i × (covered_i / total_i × 100) / Σ weight_i.
        bin 이 0개인 항목은 계산에서 뺀다."""
        num = den = 0.0
        for it in self.items():
            ci, ti = it.coverage()
            if ti == 0 or it.weight == 0:
                continue
            num += it.weight * 100.0 * ci / ti
            den += it.weight
        return num / den if den else 0.0

    def covered_set(self):
        """덮인 bin 의 이름 집합 (주어진 코드) — 수렴 곡선의 '새로 덮인 bin' 계산용"""
        s = set()
        for it in self.items():
            for k, h in it.hits.items():
                if h >= it.at_least:
                    s.add("%s.%s" % (it.name, k if isinstance(k, str) else "%s*%s" % k))
        return s


def lint_bins(cp, domain):
    """coverpoint 의 bin 정의 lint. domain = 이 값들이 전부 어딘가(regular/ignore/illegal)에 속해야 한다.
    → ['W_OVERLAP <cp> <a>/<b> at <lo>..<hi>', 'W_GAP <cp> <lo>..<hi>'] (연속 구간으로 묶어서, 값 순서대로)"""
    out = []
    every = cp.bins + cp.ignore + cp.illegal
    overlap = {}                 # (a, b) → [값...]
    gaps = []
    for v in domain:
        names = [n for n, s in cp.bins if bin_match(s, v)]
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                overlap.setdefault((names[i], names[j]), []).append(v)
        if not any(bin_match(s, v) for _, s in every):
            gaps.append(v)
    for (a, b), vals in overlap.items():
        for lo, hi in _runs(vals):
            out.append("W_OVERLAP %s %s/%s at %d..%d" % (cp.name, a, b, lo, hi))
    for lo, hi in _runs(gaps):
        out.append("W_GAP %s %d..%d" % (cp.name, lo, hi))
    return out


def _runs(vals):
    """정렬된 정수 목록 → 연속 구간 [(lo, hi)] (주어진 코드)"""
    runs = []
    for v in vals:
        if runs and v == runs[-1][1] + 1:
            runs[-1][1] = v
        else:
            runs.append([v, v])
    return [(lo, hi) for lo, hi in runs]


def merge_into(dst, src):
    """같은 모델로 만든 covergroup src 의 hits·fail_hits·illegal_hits·samples 를 dst 에 더한다 (seed/밤 단위 DB merge)."""
    for d, s in zip(dst.items(), src.items()):
        for k, v in s.hits.items():
            d.hits[k] += v
        for k, v in s.fail_hits.items():
            d.fail_hits[k] += v
    for d, s in zip(dst.coverpoints, src.coverpoints):
        d.illegal_hits.extend(s.illegal_hits)
    dst.samples += src.samples
    return dst


# ---------------------------------------------------------------- DMA0 커버리지 모델 (테스트 플랜의 코드판)
LEN_BINS = [("zero", 0), ("one", 1), ("small", (2, 63)), ("medium", (64, 4095)),
            ("page", 4096), ("large", (4097, 65534)), ("max", 65535)]
IRQ_BINS = [("done", DONE), ("half_done", DONE | HALF), ("err", ERR), ("half_err", HALF | ERR),
            ("timeout", {TIMEOUT, TIMEOUT | HALF})]
IRQ_ILLEGAL = [("none", 0),
               ("done_with_err", lambda v: bool(v & DONE) and bool(v & ERR)),
               ("done_with_timeout", lambda v: bool(v & DONE) and bool(v & TIMEOUT))]


def ctrl_of(rec):
    return int(rec["ctrl"], 0)


def build_dma_cg():
    """DMA0 커버리지 모델 (주어진 코드). 모든 bin 이름·순서는 테스트가 기대하는 그대로다."""
    cp_mode = Coverpoint("cp_mode", lambda r: get_field(ctrl_of(r), 0, 2), field_bins(MODE_NAMES))
    cp_burst = Coverpoint("cp_burst", lambda r: get_field(ctrl_of(r), 2, 2), field_bins(BURST_NAMES))
    cp_len = Coverpoint("cp_len", lambda r: r["len"], LEN_BINS)
    cp_irq = Coverpoint("cp_irq", lambda r: int(r["int_status"], 0), IRQ_BINS, illegal=IRQ_ILLEGAL)
    x_mode_len = Cross("x_mode_len", cp_mode, cp_len, ignore=[("SG", "zero")])
    x_burst_irq = Cross("x_burst_irq", cp_burst, cp_irq)
    return Covergroup("dma0_cg", [cp_mode, cp_burst, cp_len, cp_irq], [x_mode_len, x_burst_irq])


# ---------------------------------------------------------------- Part C: 리포트 / 수렴
def _key(k):
    return k if isinstance(k, str) else "%s*%s" % k


def holes(cg):
    """hole 리포트 줄 목록. 순서: coverpoint 들(모델 순서, bin 순서) → cross 들.
    coverpoint hole:   'HOLE <cp>.<bin> hits=<h>/<at_least> fail=<f> <이유>'
    cross hole 중 성분 bin 이 이미 coverpoint hole 인 것은 개별로 쓰지 않고 한 줄로 묶는다:
                       'IMPLIED <cross> <n> bins <- <cp>.<bin>(<개수>) ...'   (원인 bin 은 처음 나온 순서)
    나머지(양쪽 성분이 다 덮였는데 조합만 빈) cross hole:
                       'HOLE <cross>.<a>*<b> hits=<h>/<at_least> fail=<f> <이유> (real cross hole)'
    이유: hits>0 → 'under goal' · fail>0 → 'only in FAILED tests' · 그 외 → 'never sampled'"""
    out = []
    cp_hole = set()

    def why(h, f):
        return "under goal" if h > 0 else ("only in FAILED tests" if f > 0 else "never sampled")

    for cp in cg.coverpoints:
        for n in cp.bin_names():
            h, f = cp.hits[n], cp.fail_hits[n]
            if h < cp.at_least:
                cp_hole.add((cp.name, n))
                out.append("HOLE %s.%s hits=%d/%d fail=%d %s" % (cp.name, n, h, cp.at_least, f, why(h, f)))
    for x in cg.crosses:
        implied = {}
        real = []
        for k in x.bins():
            h, f = x.hits[k], x.fail_hits[k]
            if h >= x.at_least:
                continue
            causes = [c for c in ((x.a.name, k[0]), (x.b.name, k[1])) if c in cp_hole]
            if causes:
                c = causes[0]
                implied[c] = implied.get(c, 0) + 1
            else:
                real.append("HOLE %s.%s hits=%d/%d fail=%d %s (real cross hole)"
                            % (x.name, _key(k), h, x.at_least, f, why(h, f)))
        if implied:
            out.append("IMPLIED %s %d bins <- %s" % (
                x.name, sum(implied.values()),
                " ".join("%s.%s(%d)" % (c[0], c[1], n) for c, n in implied.items())))
        out.extend(real)
    return out


def illegal_report(cg):
    """illegal bin 이 맞은 레코드마다 한 줄 (주어진 코드) — 커버리지가 아니라 '버그 후보' 다."""
    out = []
    for cp in cg.coverpoints:
        for n, v, r in cp.illegal_hits:
            out.append("ILLEGAL %s.%s value=0x%X seed=%s step=%s test=%s ctrl=%s len=%s status=%s"
                       % (cp.name, n, v, r.get("seed"), r.get("step"), r.get("test"),
                          r.get("ctrl"), r.get("len"), r.get("status")))
    return out


def convergence(records, factory=build_dma_cg):
    """seed 를 0,1,2,... 순서로 하나씩 늘려 가며 누적 커버리지를 잰다.
    → [{'seeds': k, 'covered': c, 'total': t, 'pct': 가중%, 'new': 이번 seed 가 새로 덮은 bin 수}]  (k = 1..seed 개수)"""
    by_seed = {}
    for r in records:
        by_seed.setdefault(r["seed"], []).append(r)
    cg = factory()
    seen = set()
    curve = []
    for k, s in enumerate(sorted(by_seed), 1):
        for r in by_seed[s]:
            cg.sample(r)
        cov = cg.covered_set()
        c, t = cg.flat()
        curve.append({"seeds": k, "covered": c, "total": t, "pct": cg.coverage(), "new": len(cov - seen)})
        seen = cov
    return curve


def plateau_at(curve):
    """마지막으로 새 bin 이 덮인 시점의 seed 수 (그 뒤로는 seed 를 늘려도 그대로). 아무것도 없으면 0."""
    last = 0
    for row in curve:
        if row["new"] > 0:
            last = row["seeds"]
    return last


def ascii_plot(curves, lo=50, hi=100, step=5):
    """flat 커버리지(%) 곡선을 ASCII 로 (주어진 코드). curves = [(표식 문자, curve)] — 뒤에 온 것이 위에 그려진다."""
    width = max(len(c) for _, c in curves)
    rows = []
    for y in range(hi, lo - 1, -step):
        line = [" "] * width
        for mark, c in curves:
            for i, row in enumerate(c):
                pct = 100.0 * row["covered"] / row["total"]
                if y <= pct < y + step or (y == hi and pct >= hi):
                    line[i] = mark
        rows.append("%3d%% |%s" % (y, "".join(line)))
    rows.append("     +" + "-" * width)
    rows.append("      " + "".join(str((i + 1) // 10) if (i + 1) % 10 == 0 else " " for i in range(width))
                + "  seeds (x10)")
    return "\n".join(rows)


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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


def has(lines, prefix, *words):
    return any(ln.startswith(prefix) and all(w in ln for w in words) for ln in lines)


def run_cg(records):
    cg = build_dma_cg()
    for r in records:
        cg.sample(r)
    return cg


def item(cg, name):
    return next(it for it in cg.items() if it.name == name)


def mkrec(ctrl, length, st, status="PASS", seed=0, step=0):
    return {"seed": seed, "step": step, "test": "t", "status": status,
            "ctrl": "0x%02X" % ctrl, "len": length, "int_status": "0x%X" % st}


def run_tests():
    print("-- Part A: coverpoint / bin --")
    check("bin_match int / range inclusive both ends",
          lambda: bin_match(4096, 4096) and bin_match((64, 4095), 64) and bin_match((64, 4095), 4095)
          and not bin_match((64, 4095), 4096))
    check("bin_match set / callable",
          lambda: bin_match({8, 10}, 10) and not bin_match({8, 10}, 9) and bin_match(lambda v: v & 1, 3))
    cp = Coverpoint("c", lambda r: r, [("lo", (0, 9)), ("mid", (5, 14)), ("hi", (15, 20))],
                    ignore=[("skip", 20)], illegal=[("bad", 7)], at_least=2)
    check("overlapping value 5 counts in BOTH lo and mid", lambda: cp.sample(5) == ["lo", "mid"]
          and cp.hits["lo"] == 1 and cp.hits["mid"] == 1)
    check("illegal beats regular: 7 -> [] and recorded, lo/mid unchanged",
          lambda: cp.sample(7, rec={"step": 9}) == [] and cp.hits["lo"] == 1
          and cp.illegal_hits == [("bad", 7, {"step": 9})])
    check("ignore beats regular: 20 -> [] and hi not counted", lambda: cp.sample(20) == [] and cp.hits["hi"] == 0)
    check("at_least=2: lo hit once -> coverage (0, 3)", lambda: cp.coverage() == (0, 3))
    cp.sample(3)
    cp.sample(16, passed=False)
    check("2nd hit covers lo -> (1, 3); FAILED sample goes to fail_hits only",
          lambda: cp.coverage() == (1, 3) and cp.hits["hi"] == 0 and cp.fail_hits["hi"] == 1)
    g = build_dma_cg()
    g.sample(mkrec(0x1D, 4096, DONE | HALF))          # MODE=M2P BURST=B16 page half_done
    check("ctrl 0x1D decodes to M2P / B16; len 4096 -> page",
          lambda: item(g, "cp_mode").hits["M2P"] == 1 and item(g, "cp_burst").hits["B16"] == 1
          and item(g, "cp_len").hits["page"] == 1 and item(g, "x_mode_len").hits[("M2P", "page")] == 1)
    check("one record covers 1 bin in each of 6 items -> flat (6, 67)", lambda: g.flat() == (6, 67))

    print("-- Part B: cross / aggregate / lint --")
    g = build_dma_cg()
    xml = item(g, "x_mode_len")
    check("x_mode_len has 4x7-1 = 27 bins (SG*zero ignored)",
          lambda: len(xml.bins()) == 27 and ("SG", "zero") not in xml.hits)
    xw = Cross("xw", item(g, "cp_mode"), item(g, "cp_len"), ignore=[("*", "max"), ("SG", "*")])
    check("wildcard ignore ('*',max)+('SG','*') -> 3x6 = 18 bins",
          lambda: len(xw.bins()) == 18 and xw.is_ignored("M2M", "max") and xw.is_ignored("SG", "one")
          and not xw.is_ignored("M2M", "one"))
    g.sample(mkrec(0x10, 1, DONE | ERR))             # illegal irq: cp_irq 와 x_burst_irq 에 안 들어가야 함
    check("illegal irq sample: no x_burst_irq hit, but x_mode_len still counted",
          lambda: sum(item(g, "x_burst_irq").hits.values()) == 0 and xml.hits[("M2M", "one")] == 1
          and len(item(g, "cp_irq").illegal_hits) == 1)
    g.sample(mkrec(0x10, 0, DONE))                   # SG 아님 → (M2M, zero) 셈
    g.sample(mkrec(0x13, 0, DONE))                   # SG*zero 는 ignore → 어디에도 안 셈
    check("SG*zero sample is dropped by the cross, M2M*zero counted",
          lambda: xml.hits[("M2M", "zero")] == 1 and sum(xml.hits.values()) == 2)
    a = Coverpoint("a", lambda r: r, [("x", 0), ("y", 1)])
    b = Coverpoint("b", lambda r: r, [("p", 0), ("q", 1)], weight=3)
    a.sample(0)
    b.sample(0)
    b.sample(1)
    small = Covergroup("s", [a, b], [Cross("ab", a, b)])
    small.crosses[0].hits[("x", "p")] = 1
    check("weighted = (1*50 + 3*100 + 1*25)/5 = 75.0 %, flat = 4/8",
          lambda: abs(small.coverage() - 75.0) < 1e-9 and small.flat() == (4, 8))
    check("good LEN_BINS over 0..65535: no overlap, no gap",
          lambda: lint_bins(Coverpoint("cp_len", None, LEN_BINS), range(65536)) == [])
    bad = [("zero", 0), ("one", 1), ("small", (2, 63)), ("medium", (64, 4096)),
           ("page", 4096), ("large", (4101, 65534)), ("max", 65535)]
    lb = lint_bins(Coverpoint("cp_len", None, bad), range(65536))
    check("bad bins: W_OVERLAP medium/page at 4096..4096 and W_GAP 4097..4100 (exactly 2)",
          lambda: lb == ["W_OVERLAP cp_len medium/page at 4096..4096", "W_GAP cp_len 4097..4100"])
    li = lint_bins(item(build_dma_cg(), "cp_irq"), range(16))
    check("cp_irq lint over 4-bit INT_STATUS: gaps are the unplanned combos",
          lambda: li == ["W_GAP cp_irq 2..2", "W_GAP cp_irq 12..12", "W_GAP cp_irq 14..14"])
    v1 = load_log("runlog_v1.jsonl")
    whole = run_cg(v1)
    parts = [run_cg([r for r in v1 if r["seed"] % 4 == m]) for m in range(4)]
    merged = build_dma_cg()
    for p in parts:
        merge_into(merged, p)
    check("merge(4 shards) == sample(all): hits, fail_hits, illegal, samples",
          lambda: all(d.hits == s.hits and d.fail_hits == s.fail_hits for d, s in zip(merged.items(), whole.items()))
          and merged.samples == whole.samples == 642
          and len(item(merged, "cp_irq").illegal_hits) == len(item(whole, "cp_irq").illegal_hits) == 2)

    print("-- Part C: nightly DMA0 regression (runlog_v1 -> v2) --")
    check("v1 flat coverage 56/67, weighted 89.25 %",
          lambda: whole.flat() == (56, 67) and abs(whole.coverage() - 89.25) < 0.01)
    h1 = holes(whole)
    check("v1 hole: cp_len.zero never sampled (G1: 0 missing from interesting list)",
          lambda: has(h1, "HOLE cp_len.zero ", "hits=0/1", "fail=0", "never sampled"))
    check("v1 hole: cp_len.max only in FAILED tests (D1 directed test)",
          lambda: has(h1, "HOLE cp_len.max ", "fail=1", "only in FAILED tests"))
    check("v1: x_mode_len 7 holes collapsed into one IMPLIED line",
          lambda: "IMPLIED x_mode_len 7 bins <- cp_len.zero(3) cp_len.max(4)" in h1)
    check("v1 real cross holes: B16*err and B16*half_err (G3)",
          lambda: has(h1, "HOLE x_burst_irq.B16*err ", "never sampled", "real cross hole")
          and has(h1, "HOLE x_burst_irq.B16*half_err ", "real cross hole"))
    check("v1 hole report is exactly 5 lines", lambda: len(h1) == 5)
    il = illegal_report(whole)
    check("v1 illegal: 2x done_with_err (D2), first at seed=0 step=8 len=1",
          lambda: len(il) == 2 and il[0].startswith("ILLEGAL cp_irq.done_with_err value=0x5 seed=0 step=8 ")
          and "len=1 " in il[0])
    c1 = convergence(v1)
    check("v1 curve: 32 points, non-decreasing, ends at 56/67",
          lambda: len(c1) == 32 and all(c1[i]["covered"] <= c1[i + 1]["covered"] for i in range(31))
          and (c1[-1]["covered"], c1[-1]["total"]) == (56, 67))
    check("v1 curve: sum(new) == 56 (every covered bin counted once)",
          lambda: sum(r["new"] for r in c1) == 56)
    check("v1 plateau: last new bin at seed 16 (then 16 more seeds add nothing)", lambda: plateau_at(c1) == 16)
    v2 = load_log("runlog_v2.jsonl")
    g2 = run_cg(v2)
    h2 = holes(g2)
    check("v2 (generator fixed): 66/67, only hole M2P*max blocked by D1",
          lambda: g2.flat() == (66, 67)
          and h2 == ["HOLE x_mode_len.M2P*max hits=0/1 fail=3 only in FAILED tests (real cross hole)"])
    c2 = convergence(v2)
    check("v2 plateau at seed 16, illegal still 1 (D2 is a DUT bug, not a generator bug)",
          lambda: plateau_at(c2) == 16 and len(illegal_report(g2)) == 1)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def report():
    """--report: 야간 회귀 리포트 (md 에 붙인 실제 출력)"""
    curves = []
    for name, mark in (("runlog_v1.jsonl", "*"), ("runlog_v2.jsonl", "o")):
        recs = load_log(name)
        cg = run_cg(recs)
        c, t = cg.flat()
        fails = sum(1 for r in recs if r["status"] != "PASS")
        print("=== %s: %d records, %d FAIL | flat %d/%d = %.1f%% | weighted %.2f%%"
              % (name, len(recs), fails, c, t, 100.0 * c / t, cg.coverage()))
        for it in cg.items():
            ci, ti = it.coverage()
            print("  %-12s %2d/%-2d %6.1f%%" % (it.name, ci, ti, 100.0 * ci / ti))
        for ln in holes(cg) + illegal_report(cg):
            print("  " + ln)
        curve = convergence(recs)
        print("  seeds: " + " ".join("%d:%d" % (r["seeds"], r["covered"]) for r in curve
                                     if r["seeds"] in (1, 2, 3, 4, 6, 8, 10, 12, 16, 19, 24, 32)))
        print("  plateau_at = %d" % plateau_at(curve))
        curves.append((mark, curve))
    print(ascii_plot(curves))


def main(argv):
    ok = run_tests()
    if "--report" in argv[1:]:
        report()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
