#!/usr/bin/env python3
"""LPERF-A03 mini-c2c: 접근 trace 로 `perf c2c report` 와 같은 모양의 표를 만드는 모델

이 VM/맥에는 PEBS/SPE 가 없어 진짜 `perf c2c` 를 돌릴 수 없다 (레슨 §1 참고).
이 프로그램은 진짜 PMU 샘플이 아니라, demo_stats.c 의 계측 빌드가 남긴 trace 를
"라인 하나 = MESI 상태 하나, 스레드 하나 = 코어 하나" 모델에 넣어서 HITM 을 계산한다.
모든 표에는 LABEL 을 찍는다. 이 숫자는 하드웨어 측정값이 아니다.

  Part A  trace 파서 + 라인 상태 전이 step() + simulate() (라인·오프셋·코드 위치별 집계)
  Part B  Shared Data Cache Line Table (line_table) + Pareto (pareto) + 분류기 (classify)
  Part C  전/후 비교 (compare) + 실제 실행 trace 4개로 HITM -> 0 검증

  python3 -I solution.py                                  # N/M passed, 실패가 있으면 exit 1
  python3 -I solution.py report trace_hidden.txt [--line 64] [--show-all]
  python3 -I solution.py compare trace_hidden.txt trace_tlagg.txt [--line 64]

Python 3.9+, 표준 라이브러리만.
"""
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LABEL = "모델 출력 — perf 결과 아님"
DISPLAY_LINE_LIMIT = 0.001  # perf builtin-c2c.c 의 같은 이름 상수 (6.18.55 소스에서 확인)

Event = collections.namedtuple("Event", "seq tid addr op site")


class LineState:
    """캐시 라인 하나의 모델 상태. 무한 크기 캐시, 축출 없음, 스레드 하나 = 코어 하나."""

    def __init__(self):
        self.state = "I"      # I / E / S / M
        self.holders = set()  # 사본을 가진 tid. E, M 이면 정확히 하나

    def __repr__(self):
        return "LineState(%s,%s)" % (self.state, sorted(self.holders))


class OffsetRow:
    """라인 안의 (offset, code site) 한 줄. perf c2c 기본 coalesce (offset,iaddr) 와 같은 단위."""

    def __init__(self, offset, site):
        self.offset, self.site = offset, site
        self.tids = set()
        self.loads = self.stores = self.hitm = 0
        self.ld_l1 = self.ld_llc = self.ld_dram = 0
        self.st_l1hit = self.st_l1miss = 0


class LineStats:
    def __init__(self, addr):
        self.addr = addr
        self.records = self.loads = self.stores = self.hitm = 0
        self.ld_l1 = self.ld_llc = self.ld_dram = 0
        self.st_l1hit = self.st_l1miss = 0
        self.rows = {}       # (offset, site) -> OffsetRow
        self.writers = collections.defaultdict(set)   # offset -> 쓴 tid
        self.accessors = collections.defaultdict(set)  # offset -> 접근한 tid

    def row(self, offset, site):
        key = (offset, site)
        if key not in self.rows:
            self.rows[key] = OffsetRow(offset, site)
        return self.rows[key]


# ---------------------------------------------------------------- Part A
def parse_trace(text):
    """trace 텍스트 -> seq 오름차순 Event 목록.
    줄 형식: <seq> <tid> <0xaddr> <R|W> <site>. 빈 줄과 # 주석은 건너뛴다.
    형식이 틀리면 ValueError (메시지에 "line <n>" 포함, n 은 1부터), seq 중복도 ValueError."""
    # TODO(Part A)
    return []
    # END TODO


def step(ls, tid, op):
    """LineState 하나를 접근 하나만큼 진행시키고 결과를 돌려준다.
    R: "L1"(내 사본) / "HITM"(다른 코어가 M) / "LLC"(다른 코어에 깨끗한 사본) / "DRAM"(아무도 없음)
    W: "L1HIT"(내가 M 또는 E) / "L1MISS"(그 외: 소유권을 가져온다)
    전이: 읽기 후 사본이 나 혼자면 E, 여럿이면 S. HITM 뒤에는 원래 주인과 나 둘 다 S. 쓰기 후 M, holders={tid}."""
    # TODO(Part A)
    return "L1"
    # END TODO


def simulate(events, line_size=64):
    """events 를 순서대로 모델에 넣어 {라인 주소: LineStats} 를 만든다.
    line_size 는 2 의 거듭제곱 (아니면 ValueError). 라인 주소 = addr & ~(line_size-1).
    각 접근마다 LineStats 합계, row(offset, site) 합계와 tids, writers/accessors 를 갱신한다."""
    # TODO(Part A)
    return {}
    # END TODO


# ---------------------------------------------------------------- Part B
def line_table(lines, show_all=False):
    """Shared Data Cache Line Table 의 행 목록. HITM 내림차순, 같으면 주소 오름차순.
    각 행: dict(index, addr, hitm, hitm_pct, records, loads, stores, st_l1hit, st_l1miss, ld_l1, ld_llc, ld_dram).
    hitm_pct 는 전체 HITM 대비 %. show_all=False 면 hitm/전체 < DISPLAY_LINE_LIMIT 인 라인을 뺀다
    (전체 HITM 이 0 이면 빈 표). show_all=True 면 모든 라인. index 는 0 부터 (보여 주는 순서)."""
    # TODO(Part B)
    return []
    # END TODO


def classify(line):
    """라인 하나의 공유 유형을 판정한다. 반환: (verdict, {offset: label})
    offset 의 label:
      "TRUE"    그 offset 에 쓰는 스레드가 있고, 접근한 스레드가 2개 이상
      "FALSE-W" TRUE 가 아니고 쓰는 스레드가 있으며, 다른 offset 에 나 아닌 스레드의 쓰기가 있다
      "FALSE-R" 쓰는 스레드가 없고(읽기 전용), 다른 offset 에 그 offset 을 읽는 스레드 아닌 스레드의 쓰기가 있다
      "-"       그 외
    verdict: TRUE 와 FALSE-* 가 둘 다 있으면 "MIXED", TRUE 만 "TRUE", FALSE-* 만 "FALSE", 없으면 "NONE"."""
    # TODO(Part B)
    return "NONE", {}
    # END TODO


def pareto(line):
    """Pareto 행 목록: offset 오름차순, 같은 offset 안에서는 hitm 내림차순, 그다음 site 오름차순.
    각 행: dict(offset, site, hitm, hitm_pct, st_l1hit_pct, st_l1miss_pct, records, tids, cpu_cnt, label).
    퍼센트의 분모는 그 라인의 합계(0 이면 0.0). tids 는 정렬된 list, cpu_cnt = len(tids),
    label 은 classify(line) 의 offset label, records = loads + stores."""
    # TODO(Part B)
    return []
    # END TODO


def render_report(lines, title="", show_all=False, max_lines=8):
    """perf c2c report --stdio 와 같은 구성의 텍스트. 표마다 LABEL 을 찍는다 (주어짐)."""
    out = []
    table = line_table(lines, show_all=show_all)
    total_hitm = sum(l.hitm for l in lines.values())
    out.append("=================================================")
    out.append("           Shared Data Cache Line Table          ")
    out.append("           [%s]" % LABEL)
    out.append("=================================================")
    if title:
        out.append("# " + title)
    out.append("#                      Tot   --- Load Hitm ----    Total   Total   Total  - Stores -   Load   LLC  Load")
    out.append("# Index        Address Hitm    Total LclHit RmtHit records   Loads  Stores L1Hit L1Miss L1Hit LclHit  Dram")
    for r in table[:max_lines]:
        out.append("  %5d %#14x %5.1f%% %7d %6d %6d %7d %7d %7d %5d %6d %5d %6d %5d" % (
            r["index"], r["addr"], r["hitm_pct"], r["hitm"], r["hitm"], 0, r["records"],
            r["loads"], r["stores"], r["st_l1hit"], r["st_l1miss"], r["ld_l1"], r["ld_llc"], r["ld_dram"]))
    out.append("# total HITM (model) = %d, lines with HITM = %d" % (
        total_hitm, sum(1 for l in lines.values() if l.hitm)))
    out.append("")
    out.append("=================================================")
    out.append("      Shared Cache Line Distribution Pareto      ")
    out.append("      [%s]" % LABEL)
    out.append("=================================================")
    for r in table[:max_lines]:
        l = lines[r["addr"]]
        verdict, _ = classify(l)
        out.append("  ---- Cacheline %d  addr %#x  HITM %d  verdict %s ----" % (r["index"], r["addr"], l.hitm, verdict))
        out.append("#   HITM%  L1Hit%  L1Miss%   Offset   recs cpu  label    tids      Code address")
        for p in pareto(l):
            out.append("  %6.1f %7.1f %7.1f %#8x %6d %3d  %-8s %-9s %s" % (
                p["hitm_pct"], p["st_l1hit_pct"], p["st_l1miss_pct"], p["offset"], p["records"], p["cpu_cnt"],
                p["label"], ",".join(str(t) for t in p["tids"]), p["site"]))
    return "\n".join(out)


# ---------------------------------------------------------------- Part C
def compare(before, after):
    """수정 전/후 simulate 결과를 비교. 반환 dict:
    hitm_before, hitm_after, lines_before, lines_after (HITM>0 라인 수),
    drop_pct (hitm_before 가 0 이면 0.0), remaining (after 에서 HITM>0 인 행의 site 정렬 목록, 중복 없음)."""
    # TODO(Part C)
    return {}
    # END TODO


def suggest_fixes(line):
    """HITM 이 있는 라인에 어떤 수정이 맞는지 (offset, action) 목록으로. 없으면 [].
    action: label 이 "TRUE" → "thread-local-aggregate", "FALSE-W" → "pad-per-writer",
    "FALSE-R" → "move-to-read-mostly-line". offset 오름차순, label "-" 는 뺀다."""
    # TODO(Part C)
    return []
    # END TODO


# ---------------------------------------------------------------- 하네스 (주어짐)
_passed = 0
_failed = 0


def CHECK(name, cond, detail=""):
    global _passed, _failed
    if cond:
        _passed += 1
        print("PASS  " + name)
    else:
        _failed += 1
        print("FAIL  %s  %s" % (name, detail))


def section(title, fn):
    print("-- %s --" % title)
    try:
        fn()
    except Exception as exc:  # 미구현 TODO 는 해당 파트를 FAIL 로
        CHECK(title + " ran without exception", False, repr(exc))


def load_trace(name):
    with open(os.path.join(HERE, name)) as f:
        return parse_trace(f.read())


G_PINGPONG = """
# 두 스레드, 같은 64B 라인의 서로 다른 8B
0 0 0x1000 R site_a
1 0 0x1000 W site_a
2 1 0x1008 R site_b
3 1 0x1008 W site_b
4 0 0x1000 R site_a
5 0 0x1000 W site_a
6 1 0x1008 R site_b
7 1 0x1008 W site_b
"""

G_TRUE = """
0 0 0x2000 R inc
1 0 0x2000 W inc
2 1 0x2000 R inc
3 1 0x2000 W inc
4 0 0x2000 R inc
5 0 0x2000 W inc
"""

G_READERS = """
0 0 0x3000 R cfg
1 1 0x3000 R cfg
2 2 0x3000 R cfg
3 0 0x3000 R cfg
4 1 0x3000 R cfg
5 2 0x3000 R cfg
"""

G_PRIVATE = """
0 0 0x4000 R a
1 0 0x4000 W a
2 1 0x4040 R b
3 1 0x4040 W b
4 0 0x4000 R a
5 0 0x4000 W a
6 1 0x4040 R b
7 1 0x4040 W b
"""

G_ADJACENT = """
0 0 0x5000 W a
1 1 0x5040 W b
2 0 0x5000 R a
3 0 0x5000 W a
4 1 0x5040 R b
5 1 0x5040 W b
6 0 0x5000 R a
"""

G_MIXED = """
0 0 0x6000 R limit
1 0 0x6008 R total
2 0 0x6008 W total
3 1 0x6000 R limit
4 1 0x6008 R total
5 1 0x6008 W total
6 0 0x6010 R slot0
7 0 0x6010 W slot0
8 1 0x6018 R slot1
9 1 0x6018 W slot1
"""


def part_a():
    ev = parse_trace("# c\n\n5 1 0x10 W s\n2 0 0x1F R t\n")
    CHECK("parse: 주석·빈 줄 건너뜀, seq 정렬", [e.seq for e in ev] == [2, 5], repr(ev))
    CHECK("parse: 16진 주소", ev and ev[0].addr == 0x1f, repr(ev))
    CHECK("parse: tid/op/site", ev and ev[1].tid == 1 and ev[1].op == "W" and ev[1].site == "s", repr(ev))
    for bad, why in (("1 0 0x1 X s\n", "op"), ("1 0 0x1 R\n", "fields"), ("1 0 zz R s\n", "addr"),
                     ("1 0 0x1 R s\n1 0 0x2 R s\n", "dup seq")):
        raised = None
        try:
            parse_trace(bad)
        except ValueError as exc:
            raised = str(exc)
        CHECK("parse: 잘못된 입력 ValueError (%s)" % why, raised is not None and "line" in raised, repr(raised))

    ls = LineState()
    CHECK("step: 빈 라인 읽기 DRAM → E", step(ls, 0, "R") == "DRAM" and ls.state == "E" and ls.holders == {0}, repr(ls))
    CHECK("step: E 주인이 쓰기 → L1HIT, M", step(ls, 0, "W") == "L1HIT" and ls.state == "M", repr(ls))
    CHECK("step: 다른 코어가 M → HITM, 둘 다 S", step(ls, 1, "R") == "HITM" and ls.state == "S" and ls.holders == {0, 1}, repr(ls))
    CHECK("step: S 에서 쓰기 → L1MISS, 나만 M", step(ls, 0, "W") == "L1MISS" and ls.state == "M" and ls.holders == {0}, repr(ls))
    CHECK("step: 내 M 을 읽기 → L1", step(ls, 0, "R") == "L1" and ls.state == "M", repr(ls))
    ls2 = LineState()
    step(ls2, 0, "R")
    CHECK("step: E 를 남이 읽음 → LLC, S", step(ls2, 1, "R") == "LLC" and ls2.state == "S" and ls2.holders == {0, 1}, repr(ls2))
    CHECK("step: 무효 상태에서 쓰기 → L1MISS", step(LineState(), 3, "W") == "L1MISS")

    lines = simulate(parse_trace(G_PINGPONG), 64)
    l = lines.get(0x1000)
    CHECK("ping-pong: 라인 1개", list(lines) == [0x1000], repr(list(lines)))
    CHECK("ping-pong: records/loads/stores 8/4/4", l and (l.records, l.loads, l.stores) == (8, 4, 4))
    CHECK("ping-pong: HITM 3 (첫 읽기는 DRAM)", l and l.hitm == 3, l and l.hitm)
    CHECK("ping-pong: dram 1", l and l.ld_dram == 1, l and l.ld_dram)
    CHECK("ping-pong: store L1Hit 1, L1Miss 3", l and (l.st_l1hit, l.st_l1miss) == (1, 3), l and (l.st_l1hit, l.st_l1miss))
    CHECK("ping-pong: offset 0x8 HITM 2", l and l.rows[(8, "site_b")].hitm == 2)
    CHECK("ping-pong: offset 0 HITM 1", l and l.rows[(0, "site_a")].hitm == 1)
    CHECK("ping-pong: writers", l and l.writers[0] == {0} and l.writers[8] == {1})

    l = simulate(parse_trace(G_TRUE), 64).get(0x2000)
    CHECK("true sharing: HITM 2, L1Miss 2", l and (l.hitm, l.st_l1miss, l.st_l1hit) == (2, 2, 1), l and (l.hitm, l.st_l1miss, l.st_l1hit))
    l = simulate(parse_trace(G_READERS), 64).get(0x3000)
    CHECK("읽기만: HITM 0 / DRAM 1 / LLC 2 / L1 3", l and (l.hitm, l.ld_dram, l.ld_llc, l.ld_l1) == (0, 1, 2, 3), l and (l.hitm, l.ld_dram, l.ld_llc, l.ld_l1))
    lines = simulate(parse_trace(G_PRIVATE), 64)
    CHECK("자기 라인만: 2 라인, HITM 0, L1Miss 0", len(lines) == 2 and sum(x.hitm for x in lines.values()) == 0 and sum(x.st_l1miss for x in lines.values()) == 0)

    bad = None
    try:
        simulate([], 100)
    except ValueError as exc:
        bad = str(exc)
    CHECK("simulate: 2 의 거듭제곱이 아닌 line_size → ValueError", bad is not None, repr(bad))


def part_b():
    a = LineStats(0x100)
    a.hitm = 3
    b = LineStats(0x40)
    b.hitm = 1
    c = LineStats(0x200)
    t = line_table({0x100: a, 0x40: b, 0x200: c})
    CHECK("line_table: HITM 내림차순, HITM 0 라인 제외", [r["addr"] for r in t] == [0x100, 0x40], repr(t))
    CHECK("line_table: hitm_pct", len(t) == 2 and abs(t[0]["hitm_pct"] - 75.0) < 1e-9 and abs(t[1]["hitm_pct"] - 25.0) < 1e-9, repr(t))
    CHECK("line_table: index 0 부터", [r.get("index") for r in t] == [0, 1], repr(t))
    big = LineStats(0x1000)
    big.hitm = 5000
    tiny = LineStats(0x2000)
    tiny.hitm = 4
    t2 = line_table({0x1000: big, 0x2000: tiny})
    CHECK("line_table: 0.1% 미만 라인 숨김", [r["addr"] for r in t2] == [0x1000], repr(t2))
    t3 = line_table({0x1000: big, 0x2000: tiny}, show_all=True)
    CHECK("line_table: show_all 이면 모두", [r["addr"] for r in t3] == [0x1000, 0x2000], repr(t3))
    CHECK("line_table: HITM 전체 0 이면 빈 표", line_table({0x200: c}) == [])
    CHECK("line_table: HITM 0 + show_all 이면 표시", len(line_table({0x200: c}, show_all=True)) == 1)

    l = simulate(parse_trace(G_PINGPONG), 64)[0x1000]
    v, lab = classify(l)
    CHECK("classify ping-pong: FALSE", v == "FALSE", repr((v, lab)))
    CHECK("classify ping-pong: 라벨 FALSE-W", lab == {0: "FALSE-W", 8: "FALSE-W"}, repr(lab))
    v, lab = classify(simulate(parse_trace(G_TRUE), 64)[0x2000])
    CHECK("classify 같은 주소 RMW: TRUE", v == "TRUE" and lab == {0: "TRUE"}, repr((v, lab)))
    v, lab = classify(simulate(parse_trace(G_READERS), 64)[0x3000])
    CHECK("classify 읽기만: NONE", v == "NONE" and lab == {0: "-"}, repr((v, lab)))
    v, lab = classify(simulate(parse_trace(G_MIXED), 64)[0x6000])
    CHECK("classify limit/total/slot: MIXED", v == "MIXED", repr((v, lab)))
    CHECK("classify: limit FALSE-R, total TRUE, slot FALSE-W", lab == {0: "FALSE-R", 8: "TRUE", 0x10: "FALSE-W", 0x18: "FALSE-W"}, repr(lab))

    lines = simulate(parse_trace(G_PINGPONG), 64)
    p = pareto(lines[0x1000])
    CHECK("pareto: 행 2개, offset 순", [r["offset"] for r in p] == [0, 8], repr(p))
    CHECK("pareto: hitm_pct 33.3 / 66.7", len(p) == 2 and abs(p[0]["hitm_pct"] - 100 / 3) < 0.01 and abs(p[1]["hitm_pct"] - 200 / 3) < 0.01, repr(p))
    CHECK("pareto: L1Miss% 분모는 라인 합계", len(p) == 2 and abs(p[0]["st_l1miss_pct"] + p[1]["st_l1miss_pct"] - 100.0) < 1e-9, repr(p))
    CHECK("pareto: tids·cpu_cnt·records", len(p) == 2 and p[1]["tids"] == [1] and p[1]["cpu_cnt"] == 1 and p[1]["records"] == 4, repr(p))
    CHECK("pareto: label 전달", len(p) == 2 and p[0]["label"] == "FALSE-W", repr(p))
    multi = LineStats(0x10)
    a1 = multi.row(0, "b_site")
    a1.hitm = 1
    a1.tids = {0}
    a2 = multi.row(0, "a_site")
    a2.hitm = 5
    a2.tids = {1}
    a3 = multi.row(0, "c_site")
    a3.hitm = 1
    a3.tids = {0}
    multi.hitm = 7
    multi.accessors[0] = {0, 1}
    CHECK("pareto: 같은 offset 은 HITM 내림차순, 그다음 site", [r["site"] for r in pareto(multi)] == ["a_site", "b_site", "c_site"], repr(pareto(multi)))

    txt = render_report(simulate(parse_trace(G_MIXED), 64), title="golden")
    CHECK("render: 표마다 라벨 (2번)", txt.count(LABEL) == 2, txt[:200])
    CHECK("render: 두 표 제목", "Shared Data Cache Line Table" in txt and "Shared Cache Line Distribution Pareto" in txt)
    CHECK("render: verdict 출력", "verdict MIXED" in txt, txt)


def part_c():
    cmp0 = compare(simulate(parse_trace(G_PINGPONG), 64), simulate(parse_trace(G_PRIVATE), 64))
    CHECK("compare: 3 → 0", cmp0.get("hitm_before") == 3 and cmp0.get("hitm_after") == 0, repr(cmp0))
    CHECK("compare: drop 100%, lines 1 → 0", abs(cmp0.get("drop_pct", -1) - 100.0) < 1e-9 and cmp0.get("lines_before") == 1 and cmp0.get("lines_after") == 0, repr(cmp0))
    CHECK("compare: 남은 site 없음", cmp0.get("remaining") == [], repr(cmp0))
    cmp1 = compare(simulate(parse_trace(G_MIXED), 64), simulate(parse_trace(G_TRUE), 64))
    CHECK("compare: 남은 site", cmp1.get("remaining") == ["inc"], repr(cmp1))
    cmp2 = compare(simulate(parse_trace(G_PRIVATE), 64), simulate(parse_trace(G_PRIVATE), 64))
    CHECK("compare: 전 HITM 0 이면 drop 0", cmp2.get("drop_pct") == 0.0, repr(cmp2))

    fixes = suggest_fixes(simulate(parse_trace(G_MIXED), 64)[0x6000])
    CHECK("suggest_fixes: 오프셋별 처방", fixes == [(0, "move-to-read-mostly-line"), (8, "thread-local-aggregate"), (0x10, "pad-per-writer"), (0x18, "pad-per-writer")], repr(fixes))
    CHECK("suggest_fixes: HITM 없는 라인은 []", suggest_fixes(simulate(parse_trace(G_READERS), 64)[0x3000]) == [])

    # 라인 크기가 답을 바꾼다
    ev = parse_trace(G_ADJACENT)
    h64 = sum(l.hitm for l in simulate(ev, 64).values())
    h128 = sum(l.hitm for l in simulate(ev, 128).values())
    CHECK("granule: 64B 면 HITM 0", h64 == 0, h64)
    CHECK("granule: 128B 면 HITM 생김", h128 == 3, h128)
    CHECK("granule: 128B 판정 FALSE", classify(simulate(ev, 128)[0x5000])[0] == "FALSE")

    # 실제 실행에서 나온 trace (demo_stats.c, Lima VM). 파일이 고정이라 결과도 결정적이다
    hid, pad, tl, pad64 = (load_trace("trace_%s.txt" % n) for n in ("hidden", "padded", "tlagg", "padded64"))
    CHECK("trace 파일 4개 읽음", min(len(hid), len(pad), len(tl), len(pad64)) >= 300, (len(hid), len(pad), len(tl), len(pad64)))
    h = simulate(hid, 64)
    verdicts = sorted(classify(l)[0] for l in h.values() if l.hitm)
    CHECK("hidden@64: HITM 라인 2개", len(verdicts) == 2, repr(verdicts))
    CHECK("hidden@64: MIXED + FALSE", verdicts == ["FALSE", "MIXED"], repr(verdicts))
    top = line_table(h)
    first = h[top[0]["addr"]] if top else None
    CHECK("hidden@64: 맨 위 라인이 total_ops 를 포함 (offset 8, TRUE)", first is not None and classify(first)[1].get(8) == "TRUE", repr(first and classify(first)[1]))
    CHECK("hidden@64: limit 는 FALSE-R", first is not None and classify(first)[1].get(0) == "FALSE-R")

    p = simulate(pad, 64)
    plines = [l for l in p.values() if l.hitm]
    CHECK("padded@64: HITM 라인 1개 (total_ops)", len(plines) == 1, len(plines))
    CHECK("padded@64: 그 라인은 TRUE", plines and classify(plines[0])[0] == "TRUE", repr(plines and classify(plines[0])))
    CHECK("padded@128: 패딩 128 이면 그대로", sum(1 for l in simulate(pad, 128).values() if l.hitm) == 1)

    t = simulate(tl, 64)
    CHECK("tlagg@64: HITM 0", sum(l.hitm for l in t.values()) == 0)
    CHECK("tlagg@128: HITM 0", sum(l.hitm for l in simulate(tl, 128).values()) == 0)
    CHECK("tlagg: 쓰기가 trace 에 있다 (샘플 없어서 0 이 아님)", sum(l.stores for l in t.values()) >= 200, sum(l.stores for l in t.values()))
    c = compare(h, t)
    CHECK("compare hidden→tlagg: after 0", c.get("hitm_before", 0) > 0 and c.get("hitm_after") == 0 and c.get("remaining") == [], repr(c))
    c = compare(h, p)
    CHECK("compare hidden→padded: total_ops 사이트만 남음", len(c.get("remaining", [])) >= 1 and c.get("lines_after") == 1, repr(c))

    p64_64 = simulate(pad64, 64)
    p64_128 = simulate(pad64, 128)
    CHECK("padded(CL=64)@64: HITM 라인 1개", sum(1 for l in p64_64.values() if l.hitm) == 1)
    CHECK("padded(CL=64)@128: FALSE 라인이 다시 나타남", any(classify(l)[0] in ("FALSE", "MIXED") for l in p64_128.values() if l.hitm), repr({hex(a): classify(l)[0] for a, l in p64_128.items() if l.hitm}))


def main(argv):
    if len(argv) >= 2 and argv[1] in ("report", "compare"):
        line_size = 64
        show_all = "--show-all" in argv
        if "--line" in argv:
            line_size = int(argv[argv.index("--line") + 1])
        names = [a for a in argv[2:] if not a.startswith("--") and not a.isdigit()]
        if argv[1] == "report":
            print(render_report(simulate(load_trace(names[0]), line_size), title="%s line=%d" % (names[0], line_size), show_all=show_all))
        else:
            c = compare(simulate(load_trace(names[0]), line_size), simulate(load_trace(names[1]), line_size))
            print("[%s]" % LABEL)
            for k in ("hitm_before", "hitm_after", "lines_before", "lines_after", "drop_pct", "remaining"):
                print("  %-13s %s" % (k, c.get(k)))
        return 0
    section("Part A: 파서와 모델", part_a)
    section("Part B: 표와 분류", part_b)
    section("Part C: 전/후 비교와 실제 trace", part_c)
    print("%d/%d passed" % (_passed, _passed + _failed))
    return 1 if _failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
