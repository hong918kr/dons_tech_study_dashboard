#!/usr/bin/env python3
"""SIV-A08 생산 연계: 수율·Binning 분석 — 웨이퍼 로트 → 수율·bin·wafer map → 테스트 순서 → excursion 리포트

  Part A  수율, bin 배정(우선순위), ASCII wafer map, 공간 시그니처(edge ring / cluster), p/t 순서 정렬 (독립 가정 + brute force 검증)
  Part B  상관 fail: 독립 예측 vs 실측, 순서를 데이터로 고르기(greedy·DP, 선후 제약), early-stop 이 잃는 것(샘플링), multi-site 한계
  Part C  excursion 4종이 심어진 로트 L7K31 → 찾기(종류·웨이퍼·테스트·위치) → debug 엔지니어용 리포트

  python3 -I solution.py                    # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report           # L7K31 로트 리포트 (기준 로트 L7K12)
  python3 -I solution.py --map L7K31 6      # 웨이퍼 6 의 ASCII map
  python3 -I solution.py --tables           # md 에 붙인 측정표 (순서·마스킹·multi-site·검출력·guardband)

Python 3.9 표준 라이브러리만 쓴다 (csv, json, math, os, random, statistics, sys, zlib, collections, itertools).
데이터는 gen_wafer_lot.py 가 만든 합성 데이터 — 모든 숫자는 교육용 대표값/가정이다.
"""
import collections
import csv
import itertools
import json
import math
import os
import random
import statistics
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
# 테스트 프로그램 P2.1 (CP). t = 다이당 ms (대표값). 이 순서가 프로그램에 적힌 그대로의 순서다.
TEST_MS = collections.OrderedDict([
    ("CONT", 12.0), ("IDDQ", 35.0), ("PLL", 70.0), ("SA", 160.0),
    ("TD", 240.0), ("MBIST", 300.0), ("VMIN", 110.0), ("SERDES", 90.0)])
TESTS = tuple(TEST_MS)
BIT = {t: 1 << i for i, t in enumerate(TESTS)}
ALL_MASK = (1 << len(TESTS)) - 1
PROGRAM_ORDER = list(TESTS)
AREA_CM2 = 2.56
# 선후 제약 (a, b) = a 가 b 보다 먼저: 접촉 확인이 맨 앞, at-speed/PLL 의존 항목은 PLL 뒤
PRECEDENCE = tuple(("CONT", t) for t in TESTS if t != "CONT") + tuple(
    ("PLL", t) for t in ("TD", "MBIST", "VMIN", "SERDES"))
# bin 표 — 위에서부터 첫 일치가 그 다이의 bin (우선순위). 조건은 모두 AND.
#   any_fail: 이 중 하나라도 fail / leak_gt: leak > 값 / fmax_min: fmax >= 값 / leak_max, vmin_max: <= 값
BIN_TABLE = [
    {"name": "SB91_CONT", "hbin": 7, "any_fail": ("CONT",)},
    {"name": "SB92_LOGIC", "hbin": 7, "any_fail": ("SA", "TD", "IDDQ")},
    {"name": "SB93_MBIST", "hbin": 7, "any_fail": ("MBIST",)},
    {"name": "SB94_ANALOG", "hbin": 7, "any_fail": ("PLL", "SERDES")},
    {"name": "SB95_VMIN", "hbin": 7, "any_fail": ("VMIN",)},
    {"name": "SB96_LEAK", "hbin": 5, "leak_gt": 170.0},
    {"name": "S1", "hbin": 1, "fmax_min": 2400.0, "leak_max": 135.0, "vmin_max": 725.0},
    {"name": "S2", "hbin": 1, "fmax_min": 2280.0, "leak_max": 150.0, "vmin_max": 745.0},
    {"name": "S3", "hbin": 2, "fmax_min": 2080.0, "leak_max": 170.0, "vmin_max": 770.0},
    {"name": "SB97_SPEED", "hbin": 5},
]
PRICE = {"S1": 3.0, "S2": 2.0, "S3": 1.0}     # 상대 판매가 (대표값)
RING_RN, RING_Z, RING_RATIO = 0.80, 4.0, 2.0
CL_HALF, CL_ALPHA, CL_MIN_K, CL_MIN_N = 2, 1e-5, 6, 12
RATE_Z, RATE_RATIO, FIRST_Z, PARAM_Z, PARAM_RUN = 5.0, 1.5, 3.0, 4.0, 2

Die = collections.namedtuple("Die", "wafer x y vmin fmax leak mask")


def names_of(mask):
    return [t for t in TESTS if mask & BIT[t]]


def mk(wafer, x, y, vmin=700.0, fmax=2300.0, leak=90.0, fails=""):
    """테스트용 다이 하나. fails = "SA+TD"."""
    mask = 0
    for t in filter(None, fails.split("+")):
        mask |= BIT[t]
    return Die(wafer, x, y, vmin, fmax, leak, mask)


# ---------------------------------------------------------------- 주어진 코드: 로더
def load_lot(path):
    """lot CSV → (meta dict, [Die]). '#' 머리줄의 k=v 를 meta 로, 나머지는 csv 로."""
    meta, dies = {}, []
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0].startswith("#"):
                for tok in " ".join(row).lstrip("# ").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        meta[k] = v
                continue
            if row[0] == "wafer":
                continue
            mask = 0
            for t in filter(None, row[6].split("+")):
                mask |= BIT[t]
            dies.append(Die(int(row[0]), int(row[1]), int(row[2]), float(row[3]), float(row[4]), float(row[5]), mask))
    return meta, dies


def lot_path(lot):
    return os.path.join(HERE, "lot_%s.csv" % lot)


def by_wafer(dies):
    out = collections.OrderedDict()
    for d in dies:
        out.setdefault(d.wafer, []).append(d)
    return out


# ======================================================================== Part A
def yield_summary(dies):
    """hard 수율 (mask == 0 인 다이 / 전체) → {"n", "good", "yield", "by_wafer": {w: (n, good)}}.  빈 입력이면 yield 0.0."""
    # TODO: 웨이퍼별 (n, good) 를 세고 합계와 yield 를 낸다
    raise NotImplementedError("yield_summary")


def d0_from_yield(y, area_cm2):
    """Poisson 수율 모델 Y = exp(-A*D) 를 D 로 푼다 (모델이다 — md Part A). y 가 (0,1] 밖이면 ValueError."""
    # TODO: 범위 확인 후 -ln(y) / A
    raise NotImplementedError("d0_from_yield")


def assign_bin(die, table=BIN_TABLE):
    """table 을 위에서부터 보고 첫 일치 rule 의 name. 조건 키는 AND. 어떤 rule 도 안 맞으면 ValueError (표가 불완전)."""
    # TODO: rule 마다 any_fail / leak_gt / fmax_min / leak_max / vmin_max 를 확인, 키가 없으면 통과
    raise NotImplementedError("assign_bin")


def bin_counts(dies, table=BIN_TABLE):
    """{bin 이름: 개수} — table 의 모든 bin 을 0 포함해 표 순서대로."""
    # TODO: OrderedDict 를 rule 이름으로 0 초기화 후 assign_bin 으로 센다
    raise NotImplementedError("bin_counts")


def guardband_table(table, gb_mv):
    """모든 speed bin 의 vmin_max 를 gb_mv 만큼 조인(작게 한) 새 표. 원본은 건드리지 않는다."""
    # TODO: rule 을 복사해 vmin_max 가 있으면 빼고, 없으면 그대로
    raise NotImplementedError("guardband_table")


def die_glyph(d):
    """wafer map 글자: 1/2/3 = S1/S2/S3, x = hard fail(HBIN7), - = parametric reject(HBIN5)."""
    b = assign_bin(d)
    if b in ("S1", "S2", "S3"):
        return b[1]
    return "x" if d.mask else "-"


def wafer_map(dies, wafer, glyph=die_glyph):
    """한 웨이퍼의 ASCII map. y 큰 쪽이 위, x 작은 쪽이 왼쪽. 글자 사이 공백 1칸 (터미널 글자 비율 보정), 빈 자리는 공백.
    각 줄 오른쪽 공백은 지운다. 그 웨이퍼 다이가 없으면 ""."""
    # TODO: (x,y) → 글자 dict, min/max 격자를 y 내림차순으로 돌며 " ".join
    raise NotImplementedError("wafer_map")


def edge_ring(dies_w, bit, rn_min=RING_RN):
    """한 웨이퍼·한 테스트의 edge ring 통계. rn = hypot(x,y)/rmax (rmax = 그 웨이퍼 다이의 최대 거리).
    rn >= rn_min 을 edge, 나머지를 inner 로 두고 두 비율 z = (edge-inner)/sqrt(pbar(1-pbar)(1/ne+1/ni)).
    → {"edge", "inner", "ne", "ni", "z"}. 한쪽 집합이 비었거나 분산 0 이면 z = 0.0."""
    # TODO: 위 정의대로
    raise NotImplementedError("edge_ring")


def is_ring(s):
    """edge_ring 결과 판정: z >= RING_Z 이고 edge >= RING_RATIO * inner."""
    return s["z"] >= RING_Z and s["edge"] >= RING_RATIO * s["inner"]


def binom_tail(k, n, p):
    """P(X >= k), X ~ Binomial(n, p).  정확 계산 (math.comb)."""
    if k <= 0:
        return 1.0
    return min(1.0, sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(k, n + 1)))


def find_clusters(dies_w, bit, half=CL_HALF, alpha=CL_ALPHA):
    """(2*half+1)^2 창을 모든 다이 중심에 놓고, 창 안 fail k/n 이 창 밖 비율 bg 로 설명되는지 scan.
    조정 p = binom_tail(k, n, max(bg, 0.5/N)) * M  (M = 시작 때 다이 수, Bonferroni).
    n >= CL_MIN_N, k >= CL_MIN_K 인 창 중 조정 p 최소를 고르고, p < alpha 면 기록 후 그 창 다이를 지우고 반복.
    → [{"cx","cy" (창 안 fail 의 무게중심), "k","n","p","bg"}]  (찾은 순서)."""
    # TODO: 위 정의대로 (창 다이 제거 후 N, F 도 다시 계산)
    raise NotImplementedError("find_clusters")


def expected_time(order, p, t):
    """stop-on-first-fail + 항목 독립 가정의 기대 시간 = Σ t[order[k]] · Π_{j<k} (1 - p[order[j]])."""
    # TODO: 위 식을 앞에서부터 누적
    raise NotImplementedError("expected_time")


def ratio_order(p, t):
    """p/t 내림차순 (동률은 이름 오름차순) → 이름 리스트."""
    # TODO: sorted 한 줄
    raise NotImplementedError("ratio_order")


# ======================================================================== Part B
def marginals(dies):
    """테스트별 true fail 비율 {t: p} (full datalog 기준)."""
    # TODO: 비트별로 세고 len 으로 나눈다
    raise NotImplementedError("marginals")


def die_time(d, order, t):
    """이 다이가 stop-on-first-fail 로 쓰는 시간: 첫 fail 항목까지 (그 항목 포함) 시간 합."""
    # TODO: 순서대로 더하다가 fail 이면 멈춘다
    raise NotImplementedError("die_time")


def empirical_time(dies, order, t):
    """실측 평균 시간 (독립 가정 없음): die_time 의 평균."""
    # TODO: die_time 평균
    raise NotImplementedError("empirical_time")


def greedy_order(dies, t, prec=(), conditional=True):
    """선후 제약 prec = [(a,b): a 먼저] 를 지키는 greedy.  매 단계 "지금 시작할 수 있는" 항목 중 rate/t 최대를 고른다.
    conditional=True: rate = 아직 살아 있는 다이 안에서의 fail 비율 / False: 전체 marginal (= 제약 있는 p/t 정렬).
    동률은 이름 오름차순. 제약에 순환이 있으면 ValueError."""
    # TODO: 남은 집합을 줄여 가며 고르고, conditional 이면 고른 항목의 fail 다이를 살아 있는 목록에서 뺀다
    raise NotImplementedError("greedy_order")


def dp_order(dies, t, prec=()):
    """정확 최적: 부분집합 DP.  cost[S] = S 를 이미 끝낸 뒤의 남은 기대 시간
    = min_{i ∉ S, i 시작 가능} alive(S)*t[i] + cost[S ∪ {i}],  alive(S) = S 의 어떤 항목도 fail 안 한 비율.
    동률은 이름 오름차순.  → (order, 기대 시간).  t 의 키(≤ 8개) 위에서만 순서를 정한다."""
    # TODO: 마스크별 개수(Counter) → alive(S) 표 → S 를 큰 쪽부터 DP → 복원
    raise NotImplementedError("dp_order")


def first_fail_counts(dies, order):
    """stop-on-first-fail 로 돌렸을 때 테스트별 '첫 fail' 다이 수 {t: count} (order 의 모든 항목, 0 포함)."""
    # TODO: 다이마다 order 를 보며 첫 fail 항목에 +1
    raise NotImplementedError("first_fail_counts")


def masked_table(dies, order):
    """테스트별 {"test","true","first","masked"} 목록(order 순). true = marginal, first = 첫 fail 비율,
    masked = (true-first)/true (true==0 이면 0.0) — 앞 항목 뒤에 가려져 datalog 에 안 보이는 비율."""
    # TODO: marginals + first_fail_counts 조합
    raise NotImplementedError("masked_table")


def pick_hash(die, den, salt="P2.1"):
    """결과를 보기 전에 정하는 샘플: crc32("wafer,x,y,salt") % den == 0.  (Python hash() 는 실행마다 달라 안 쓴다.)"""
    # TODO: zlib.crc32 로
    raise NotImplementedError("pick_hash")


def sampled_cost(dies, order, t, picker):
    """picker(die) 가 True 인 다이는 끝까지(전 항목) 돌리고, 나머지는 stop-on-first-fail → 다이당 평균 ms."""
    # TODO: 전 항목 시간 합 vs die_time
    raise NotImplementedError("sampled_cost")


def estimate_rates(dies, picker):
    """샘플(picker True)만으로 추정한 marginal. → ({t: p̂}, n_sample). 샘플이 비면 ({t: 0.0}, 0)."""
    # TODO: 샘플에 marginals
    raise NotImplementedError("estimate_rates")


def touchdown_time(dies, order, t, sites):
    """x{sites} 병렬 CP: 다이를 파일 순서대로 sites 개씩 묶어 한 touchdown. 그 시간 = 묶음 안 die_time 의 최댓값.
    → 다이당 평균 ms (= Σ touchdown 시간 / 다이 수)."""
    # TODO: 슬라이스 묶음 max 의 합 / len(dies)
    raise NotImplementedError("touchdown_time")


# ======================================================================== Part C
def wafer_rates(dies, order):
    """{wafer: {"n", "true":{t:rate}, "first":{t:rate}, "fmax","vmin","leak"(중앙값), "yield"}}"""
    out = collections.OrderedDict()
    for w, ds in by_wafer(dies).items():
        n = len(ds)
        ff = first_fail_counts(ds, order)
        m = marginals(ds)
        out[w] = {"n": n, "true": m, "first": {k: ff[k] / n for k in order},
                  "fmax": statistics.median(d.fmax for d in ds), "vmin": statistics.median(d.vmin for d in ds),
                  "leak": statistics.median(d.leak for d in ds),
                  "yield": sum(1 for d in ds if d.mask == 0) / n}
    return out


def baseline_model(base_dies, order):
    """기준 로트의 웨이퍼별 분포 → {"true":{t:(mean,sd)}, "first":{...}, "fmax":(m,s), "vmin":.., "leak":.., "yield":..}."""
    wr = wafer_rates(base_dies, order)
    def ms(vals):
        vals = list(vals)
        return (statistics.mean(vals), statistics.pstdev(vals))
    out = {"true": {}, "first": {}}
    for t in order:
        out["true"][t] = ms(r["true"][t] for r in wr.values())
        out["first"][t] = ms(r["first"][t] for r in wr.values())
    for k in ("fmax", "vmin", "leak", "yield"):
        out[k] = ms(r[k] for r in wr.values())
    out["order"] = list(order)
    return out


ACTIONS = {
    "EDGE_RING": "에지 영역 공정(베벨 etch, 에지 노광)·프로브 카드 에지 사이트 확인 → 같은 chamber/run 웨이퍼 추적",
    "CLUSTER": "입자/스크래치 후보 → 해당 웨이퍼 인라인 결함 맵과 좌표 대조, 물리 FA 후보 다이 선정",
    "PARAM_SHIFT": "hard 수율로는 안 보이는 공정 이동 → 해당 웨이퍼 그룹의 Vt/PSRO/공정 레시피 로그 확인, speed bin 재예측",
    "MASKED": "먼저 fail 하는 항목 뒤에 원인이 가려졌다 → 해당 웨이퍼 full-datalog 재측정, 순서 임시 변경으로 진짜 Pareto 확인",
    "RATE": "공간 패턴 없는 항목별 비율 상승 → 해당 웨이퍼 프로브 카드/테스터 사이트·공정 스텝 상관 조사",
}


def merge_runs(items):
    """[(wafer, 키, payload)] → 같은 키의 연속 웨이퍼를 묶어 {"wafers":[...], "key":키, "payloads":[...]} 목록 (첫 웨이퍼 순)."""
    out, cur = [], {}
    for w, key, pl in sorted(items, key=lambda it: (it[1], it[0])):
        c = cur.get(key)
        if c is not None and c["wafers"][-1] == w - 1:
            c["wafers"].append(w)
            c["payloads"].append(pl)
        else:
            c = {"wafers": [w], "key": key, "payloads": [pl]}
            cur[key] = c
            out.append(c)
    return sorted(out, key=lambda c: (c["wafers"][0], str(c["key"])))


def detect_findings(lot_dies, base, order):
    """excursion 탐지 → finding 목록 (첫 웨이퍼 순). 각 finding = {"kind","wafers","tests","detail","action"}.
    순서: ① 웨이퍼·테스트별 edge_ring ② find_clusters (ring 웨이퍼면 edge 다이 제외) ③ 공간으로 설명된 다이를 빼고
    항목별 비율 z ④ true 는 튀는데 first 가 안 튀면 MASKED ⑤ fmax/vmin/leak 중앙값의 run-length PARAM_SHIFT.
    임계값은 파일 위쪽 상수. kind: EDGE_RING / CLUSTER / RATE / MASKED / PARAM_SHIFT."""
    # TODO ① 웨이퍼별로 ring_hits = [(w, test, stats)], cluster_hits, explained_dies 를 모은다
    # ② explained_dies 를 뺀 다이로 wafer_rates → 항목별 z = (true-mean)/max(sd, binomial sd)
    # ③ z>=RATE_Z and true>=RATE_RATIO*mean → first z<FIRST_Z 면 MASKED, 아니면 RATE
    # ④ fmax 등 |z|>=PARAM_Z 가 PARAM_RUN 웨이퍼 이상 연속 → PARAM_SHIFT
    # ⑤ merge_runs 로 묶고 MASKED 의 behind 항목은 같은 웨이퍼의 RATE 에서 뺀다, tests/detail/action 채우기
    raise NotImplementedError("detect_findings")


def value_index(dies, table=BIN_TABLE):
    """다이당 평균 상대 판매가 Σ price(bin) / n (PRICE 는 대표값)."""
    # TODO: bin_counts + PRICE
    raise NotImplementedError("value_index")


def fmt_wafers(ws):
    return ("W%d" % ws[0]) if len(ws) == 1 else ("W%d..%d" % (ws[0], ws[-1]))


def lot_report(meta, dies, base_dies, order):
    """debug 엔지니어용 텍스트 리포트. 줄 형식은 md §2 Part C 참고:
      LOT <lot> product=<p> program=<p> wafers=<n> dies=<n>
      yield <x.x>% (baseline <y.y>%, <+d.d>pp)  D0(Poisson model) <z.zzz>/cm2
      bins S1=.. S2=.. S3=.. reject=.. value_index=<v.vv> (baseline <w.ww>)
      FINDINGS <k>
      F<i> <KIND> <wafers> tests=<a,b> <근거 한 토막>
         action: <조치>
      ORDER program=<a>ms dp=<b>ms save=<c.c>%  (x16 touchdown save=<d.d>%)"""
    # TODO: 위 줄 형식으로. base_model, findings, yield_summary, bin_counts, value_index, dp_order, touchdown_time 재사용
    raise NotImplementedError("lot_report")


# ======================================================================== 테스트 하네스 (starter/solution 동일)
g_pass = g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        r = fn()
        ok, detail = r if isinstance(r, tuple) else (bool(r), "")
    except NotImplementedError as e:
        ok, detail = False, "TODO: %s" % e
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("  [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s  (%s)" % (name, detail))
    return ok


class Lazy:
    """fixture: TODO 가 예외를 던져도 하네스가 안 죽게, 처음 불릴 때 계산하고 결과/예외를 기억한다."""

    def __init__(self, fn):
        self.fn, self.done, self.val, self.err = fn, False, None, None

    def __call__(self):
        if not self.done:
            self.done = True
            try:
                self.val = self.fn()
            except Exception as e:  # noqa: BLE001
                self.err = e
        if self.err is not None:
            raise self.err
        return self.val


def synth_wafer(rng, sites, bg, ring_p=0.0, ring_rn=0.85, blob=None):
    """통제 실험용 웨이퍼 한 장 (MBIST bit 하나만). blob = (cx, cy, radius, p)."""
    rmax = max(math.hypot(x, y) for x, y in sites)
    out = []
    for x, y in sites:
        p = bg
        if ring_p and math.hypot(x, y) / rmax >= ring_rn:
            p = max(p, ring_p)
        if blob and math.hypot(x - blob[0], y - blob[1]) <= blob[2]:
            p = max(p, blob[3])
        out.append(Die(1, x, y, 700.0, 2300.0, 90.0, BIT["MBIST"] if rng.random() < p else 0))
    return out


def run_tests():
    meta_a, base = load_lot(lot_path("L7K12"))
    meta_b, lot = load_lot(lot_path("L7K31"))
    with open(os.path.join(HERE, "lot_L7K31_truth.json"), encoding="utf-8") as f:
        truth = json.load(f)
    t = dict(TEST_MS)
    sites = sorted({(d.x, d.y) for d in base if d.wafer == 1})
    print("-- Part A: 수율 · bin · map · 공간 시그니처 · p/t --")
    check("loader: 5525 dies, 25 wafers, 221 sites each",
          lambda: len(base) == 5525 and len(by_wafer(base)) == 25 and len(sites) == 221
          and all(len(v) == 221 for v in by_wafer(lot).values()))
    # 표 기반 수율: 손으로 센 값
    tiny = [mk(1, 0, 0), mk(1, 1, 0, fails="SA"), mk(1, 2, 0, fails="SA+TD"), mk(2, 0, 0), mk(2, 1, 0)]
    def t_y():
        s = yield_summary(tiny)
        return (s["n"], s["good"], round(s["yield"], 6), s["by_wafer"][1], s["by_wafer"][2]) == (5, 3, 0.6, (3, 1), (2, 2))
    check("yield_summary: table-driven (5 dies, 3 good, W1 1/3, W2 2/2)", t_y)
    check("yield_summary: empty input -> yield 0.0", lambda: yield_summary([])["yield"] == 0.0)
    check("yield_summary: L7K12 good count == dies with empty fail field in CSV", lambda: (
        lambda a, b: (a == b, "%d" % a))(yield_summary(base)["good"], sum(
            1 for line in open(lot_path("L7K12"), encoding="utf-8")
            if line[0].isdigit() and line.rstrip("\n").endswith(","))))
    check("d0_from_yield: Y = exp(-A*D) round trip", lambda: abs(d0_from_yield(math.exp(-2.56 * 0.11), 2.56) - 0.11) < 1e-9)
    # 모델 회복: 실제 평균 결함 수 lambda / A 와 비교. 군집·비결함 fail 때문에 편향이 있어 15% 까지 허용 (md Part A)
    def t_d0():
        y = yield_summary(base)["yield"]
        d = d0_from_yield(y, AREA_CM2)
        truth_d = truth["lambda_mean_L7K12"] / AREA_CM2
        return abs(d - truth_d) / truth_d <= 0.15, "D0_hat=%.4f truth=%.4f" % (d, truth_d)
    check("d0_from_yield on L7K12 within 15% of injected mean density (model!)", t_d0)
    cases = [
        (mk(1, 0, 0, fails="CONT+SA+MBIST"), "SB91_CONT"),
        (mk(1, 0, 0, fails="TD+MBIST"), "SB92_LOGIC"),
        (mk(1, 0, 0, fails="MBIST+SERDES"), "SB93_MBIST"),
        (mk(1, 0, 0, fails="SERDES+VMIN"), "SB94_ANALOG"),
        (mk(1, 0, 0, fails="VMIN"), "SB95_VMIN"),
        (mk(1, 0, 0, leak=170.01, fmax=2500, vmin=700), "SB96_LEAK"),
        (mk(1, 0, 0, leak=170.0, fmax=2100, vmin=760), "S3"),
        (mk(1, 0, 0, fmax=2400.0, leak=135.0, vmin=725.0), "S1"),
        (mk(1, 0, 0, fmax=2399.9, leak=135.0, vmin=725.0), "S2"),
        (mk(1, 0, 0, fmax=2500.0, leak=135.1, vmin=700.0), "S2"),
        (mk(1, 0, 0, fmax=2500.0, leak=100.0, vmin=725.1), "S2"),
        (mk(1, 0, 0, fmax=2500.0, leak=150.1, vmin=700.0), "S3"),
        (mk(1, 0, 0, fmax=2079.9, leak=100.0, vmin=700.0), "SB97_SPEED"),
    ]
    check("assign_bin: 13 table cases (priority, inclusive/exclusive boundaries)",
          lambda: all(assign_bin(d) == b for d, b in cases))
    def t_incomplete():
        try:
            assign_bin(mk(1, 0, 0), [{"name": "x", "any_fail": ("SA",)}])
            return False
        except ValueError:
            return True
    check("assign_bin: incomplete table -> ValueError", t_incomplete)
    hist = Lazy(lambda: bin_counts(base))
    check("bin_counts: covers all bins and sums to n", lambda: sum(hist().values()) == len(base)
          and list(hist()) == [r["name"] for r in BIN_TABLE], )
    def t_cap():
        fast = [d for d in base if d.mask == 0 and d.fmax >= 2400.0]
        capped = [d for d in fast if assign_bin(d) != "S1"]
        return 0 < len(capped) < len(fast), "fmax>=2400 clean=%d, not S1=%d" % (len(fast), len(capped))
    check("leakage/Vmin caps: some fast dies are NOT S1 (correlated with speed)", t_cap)
    def t_gb():
        tbl = guardband_table(BIN_TABLE, 20.0)
        return (BIN_TABLE[6]["vmin_max"] == 725.0 and tbl[6]["vmin_max"] == 705.0 and tbl[0] == BIN_TABLE[0]
                and assign_bin(mk(1, 0, 0, fmax=2500, leak=100, vmin=710), tbl) == "S2")
    check("guardband_table: copies, tightens vmin only, moves a die S1->S2", t_gb)
    def t_gb_mono():
        s = [bin_counts(base, guardband_table(BIN_TABLE, g))["S1"] for g in (0, 5, 10, 20, 40)]
        return all(a >= b for a, b in zip(s, s[1:])) and s[0] > s[-1], str(s)
    check("guardband: S1 count monotone non-increasing in guardband (L7K12)", t_gb_mono)
    mapd = [mk(1, -1, 1, fmax=2500, leak=100, vmin=700), mk(1, 0, 1, fmax=2300, leak=100, vmin=740),
            mk(1, 1, 1, fails="SA"), mk(1, -1, 0, fmax=2100, leak=100, vmin=760),
            mk(1, 1, 0, fmax=2000, leak=100, vmin=700), mk(2, 0, 0)]
    check("wafer_map: exact text (y up, x right, gaps as blanks, trailing spaces stripped)",
          lambda: wafer_map(mapd, 1) == "1 2 x\n3   -")
    check("wafer_map: unknown wafer -> empty string", lambda: wafer_map(mapd, 99) == "")
    def t_edge_hand():
        ds = [mk(1, 0, 0)] * 3 + [mk(1, 4, 0, fails="MBIST")] * 2 + [mk(1, 4, 0)] * 2
        ds = [Die(1, 0, 0, 700.0, 2300.0, 90.0, 0)] * 8 + [Die(1, 10, 0, 700.0, 2300.0, 90.0, BIT["MBIST"])] * 4 + [
            Die(1, 10, 0, 700.0, 2300.0, 90.0, 0)] * 4
        s = edge_ring(ds, BIT["MBIST"], 0.8)
        pbar = 4 / 16
        z = (0.5 - 0.0) / math.sqrt(pbar * 0.75 * (1 / 8 + 1 / 8))
        return (s["ne"], s["ni"], s["edge"], s["inner"]) == (8, 8, 0.5, 0.0) and abs(s["z"] - z) < 1e-9
    check("edge_ring: hand-computed two-proportion z", t_edge_hand)
    W = {w: ds for w, ds in by_wafer(lot).items()}
    check("edge_ring: planted ring wafer 6 MBIST detected", lambda: is_ring(edge_ring(W[6], BIT["MBIST"])))
    check("edge_ring: clean wafer 2 MBIST not flagged", lambda: not is_ring(edge_ring(W[2], BIT["MBIST"])))
    def t_cluster():
        c = find_clusters(W[3], BIT["SA"])
        if len(c) != 1:
            return False, "found %d" % len(c)
        dist = math.hypot(c[0]["cx"] - truth["cluster"]["cx"], c[0]["cy"] - truth["cluster"]["cy"])
        # 허용 1.5 다이: 창이 5x5 라 무게중심이 배경 fail 에 끌린다
        return dist <= 1.5, "center=(%.1f,%.1f) k=%d/%d dist=%.2f" % (c[0]["cx"], c[0]["cy"], c[0]["k"], c[0]["n"], dist)
    check("find_clusters: planted cluster on W3 (SA) found within 1.5 die of truth", t_cluster)
    check("find_clusters: clean wafer 2 (SA) finds nothing", lambda: find_clusters(W[2], BIT["SA"]) == [])
    tm_h = {"A": 2.0, "B": 8.0}
    p_h = {"A": 0.1, "B": 0.5}
    check("expected_time: hand value A then B = 2 + 0.9*8 = 9.2", lambda: abs(expected_time(["A", "B"], p_h, tm_h) - 9.2) < 1e-9)
    check("ratio_order: B (0.0625) before A (0.05)", lambda: ratio_order(p_h, tm_h) == ["B", "A"])
    def t_brute():
        rng = random.Random(0xA08)
        for _ in range(400):
            n = rng.randint(2, 7)
            names = ["t%d" % i for i in range(n)]
            p = {x: rng.choice((0.0, 1.0, rng.random() * 0.5, rng.random())) for x in names}
            tt = {x: rng.uniform(1.0, 500.0) for x in names}
            best = min(expected_time(list(o), p, tt) for o in itertools.permutations(names))
            mine = expected_time(ratio_order(p, tt), p, tt)
            if abs(mine - best) > 1e-9:
                return False, "n=%d mine=%f best=%f" % (n, mine, best)
        return True, "400 random instances, n=2..7"
    check("ratio_order == brute-force optimum (includes p=0, p=1)", t_brute)
    def t_naive():
        p = {"F": 0.05, "S": 0.6}
        tt = {"F": 10.0, "S": 100.0}
        by_time = sorted(p, key=lambda n: tt[n])
        by_prob = sorted(p, key=lambda n: -p[n])
        p2 = {"F": 0.02, "S": 0.5, "M": 0.3}
        tt2 = {"F": 5.0, "S": 400.0, "M": 30.0}
        a = expected_time(sorted(p2, key=lambda n: -p2[n]), p2, tt2)
        b = expected_time(ratio_order(p2, tt2), p2, tt2)
        return b < a and expected_time(by_time, p, tt) > expected_time(ratio_order(p, tt), p, tt) - 1e-9
    check("naive sort by p alone is worse than p/t on a concrete case", t_naive)

    print("-- Part B: 상관 · 순서 · early-stop · multi-site --")
    m_b = Lazy(lambda: marginals(base))
    def t_indep():
        rng = random.Random(0xA08 + 1)
        ps = dict(m_b())
        indep = []
        for _ in range(40000):
            mask = 0
            for n in TESTS:
                if rng.random() < ps[n]:
                    mask |= BIT[n]
            indep.append(Die(1, 0, 0, 700.0, 2300.0, 90.0, mask))
        pm = marginals(indep)
        pred, emp = expected_time(PROGRAM_ORDER, pm, t), empirical_time(indep, PROGRAM_ORDER, t)
        # 허용 6 ms: die_time 표준편차 ~300 ms / sqrt(40000) = 1.5 ms 의 4σ
        return abs(pred - emp) <= 6.0, "pred=%.1f emp=%.1f" % (pred, emp)
    check("independent control data: predicted == empirical within 6 ms", t_indep)
    def t_gap():
        pred = expected_time(PROGRAM_ORDER, m_b(), t)
        emp = empirical_time(base, PROGRAM_ORDER, t)
        return emp - pred >= 20.0, "pred=%.1f emp=%.1f gap=%.1f ms" % (pred, emp, emp - pred)
    check("correlated L7K12: independence underestimates time by >= 20 ms", t_gap)
    orders = Lazy(lambda: {
        "program": PROGRAM_ORDER,
        "ratio": greedy_order(base, t, PRECEDENCE, conditional=False),
        "greedy": greedy_order(base, t, PRECEDENCE, conditional=True),
        "dp": dp_order(base, t, PRECEDENCE)[0],
    })
    def t_prec():
        o = orders()
        for nm in ("ratio", "greedy", "dp"):
            pos = {n: i for i, n in enumerate(o[nm])}
            if sorted(o[nm]) != sorted(TESTS) or any(pos[a] > pos[b] for a, b in PRECEDENCE):
                return False, nm
        return True, "dp=" + " ".join(o["dp"])
    check("all orders are permutations and obey precedence", t_prec)
    def t_cycle():
        try:
            greedy_order(base[:10], {"SA": 1.0, "TD": 1.0}, [("SA", "TD"), ("TD", "SA")])
            return False
        except ValueError:
            return True
    check("greedy_order: precedence cycle -> ValueError", t_cycle)
    def t_dp_opt():
        sub = {k: t[k] for k in ("CONT", "IDDQ", "SA", "TD", "MBIST", "SERDES")}
        pre = [("CONT", k) for k in sub if k != "CONT"]
        o, c = dp_order(base, sub, pre)
        bp = 0
        best = float("inf")
        for perm in itertools.permutations(sub):
            if perm[0] != "CONT":
                continue
            v = empirical_time(base, list(perm), sub)
            bp += 1
            best = min(best, v)
        return abs(c - best) < 1e-6 and abs(empirical_time(base, o, sub) - best) < 1e-6, "%d perms, best=%.2f ms" % (bp, best)
    check("dp_order == brute force over legal orders (6 tests on L7K12)", t_dp_opt)
    def t_order_rank():
        o = orders()
        e = {k: empirical_time(base, v, t) for k, v in o.items()}
        d = dp_order(base, t, PRECEDENCE)[1]
        ok = abs(e["dp"] - d) < 1e-6 and e["dp"] <= e["greedy"] + 1e-9 and e["dp"] <= e["ratio"] + 1e-9 and e["dp"] < e["program"]
        return ok, " ".join("%s=%.1f" % (k, v) for k, v in e.items())
    check("L7K12: dp <= greedy/ratio and beats program order", t_order_rank)
    def t_prec_hurts():
        free_dp = dp_order(base, t, ())[1]
        return free_dp <= dp_order(base, t, PRECEDENCE)[1] + 1e-9, "free=%.1f" % free_dp
    check("removing constraints can only help", t_prec_hurts)
    tbl_m = Lazy(lambda: {r["test"]: r for r in masked_table(base, PROGRAM_ORDER)})
    check("masked_table: first test (CONT) is never masked", lambda: tbl_m()["CONT"]["masked"] == 0.0)
    check("masked_table: TD mostly hidden behind SA (>= 50%)", lambda: tbl_m()["TD"]["masked"] >= 0.50 and abs(tbl_m()["TD"]["true"] - marginals(base)["TD"]) < 1e-12)
    def t_pareto_moves():
        a = first_fail_counts(base, PROGRAM_ORDER)
        o = orders()["dp"]
        b = first_fail_counts(base, o)
        return sum(a.values()) == sum(b.values()) and a != b, "fails=%d, TD %d -> %d" % (sum(a.values()), a["TD"], b["TD"])
    check("reordering moves first-fail Pareto but not the total fail count", t_pareto_moves)
    def t_sample():
        pick = lambda d: pick_hash(d, 8)
        est, n = estimate_rates(base, pick)
        # 다이 표본오차 3σ: 항목 간 독립이 아니어도 항목 하나의 추정량은 이항이다
        ok = all(abs(est[k] - m_b()[k]) <= 3 * math.sqrt(m_b()[k] * (1 - m_b()[k]) / n) for k in TESTS)
        return ok, "n=%d/%d" % (n, len(base))
    check("hash sampling 1/8: every marginal within 3 sigma of full datalog", t_sample)
    def t_sample_cost():
        stop = empirical_time(base, PROGRAM_ORDER, t)
        full = sum(t.values())
        c = sampled_cost(base, PROGRAM_ORDER, t, lambda d: pick_hash(d, 8))
        extra, full_extra = c - stop, full - stop
        # 1/8 샘플의 추가 시간은 평균적으로 전체 추가 시간의 1/8. ±25% 는 샘플의 fail 다이 수 흔들림 때문
        return abs(extra / full_extra - 1 / 8) <= 0.25 / 8, "extra=%.1f ms of full-extra %.1f ms" % (extra, full_extra)
    check("sampled_cost: extra time ~ 1/8 of full-datalog extra", t_sample_cost)
    def t_alias():
        # 레티클 반복 결함: 필드 1x2 다이 중 y 짝수 쪽 다이에서만 fail (레티클 필드 16x32 mm)
        rng = random.Random(0xA0A)
        ds = []
        for w in range(1, 26):
            for x, y in sites:
                bad = (y % 2 == 0 and rng.random() < 0.4)
                ds.append(Die(w, x, y, 700.0, 2300.0, 90.0, BIT["SERDES"] if bad else 0))
        tr = marginals(ds)["SERDES"]
        sys_rows, n1 = estimate_rates(ds, lambda d: d.y % 2 == 1)    # 한 줄 건너 샘플 → 전부 놓침
        sys_rows2, n2 = estimate_rates(ds, lambda d: d.y % 2 == 0)   # 반대 위상 → 2배 과대
        hs, n3 = estimate_rates(ds, lambda d: pick_hash(d, 2))
        ok = sys_rows["SERDES"] == 0.0 and abs(sys_rows2["SERDES"] - 2 * tr) < 0.01 and abs(hs["SERDES"] - tr) < 0.03
        return ok, "true=%.3f row-skip odd=%.3f even=%.3f hash=%.3f" % (tr, sys_rows["SERDES"], sys_rows2["SERDES"], hs["SERDES"])
    check("limit: systematic row-skip sampling aliases with a reticle repeater; hash does not", t_alias)
    def t_first_k():
        # 웨이퍼 윗 절반에만 fail 이 있고 파일은 아래(y 작은 쪽)부터 → '웨이퍼당 처음 K fail 만' 은 대표성이 없다
        ds = []
        rng = random.Random(0xA0B)
        for x, y in sorted(sites, key=lambda s: (s[1], s[0])):     # 프로브/파일 순서: y 작은 줄부터
            mask = BIT["MBIST"] if y > 0 and rng.random() < 0.5 else (BIT["SA"] if rng.random() < 0.1 else 0)
            ds.append(Die(1, x, y, 700.0, 2300.0, 90.0, mask))
        seen = 0
        kept = []
        for d in ds:
            if d.mask and seen < 8:
                kept.append(d)
                seen += 1
        share = sum(1 for d in kept if d.mask & BIT["MBIST"]) / len(kept)
        real = sum(1 for d in ds if d.mask & BIT["MBIST"]) / sum(1 for d in ds if d.mask)
        return share < real / 2, "first-8 MBIST share=%.2f, real=%.2f" % (share, real)
    check("limit: 'first K fails' sampling is spatially biased", t_first_k)
    def t_multi():
        o = orders()["dp"]
        s = {}
        for k in (1, 4, 16):
            a, b = touchdown_time(base, PROGRAM_ORDER, t, k), touchdown_time(base, o, t, k)
            s[k] = (a - b) / a * 100
        return s[4] < s[1] and s[16] < 0.25 * s[1], "save%% x1=%.2f x4=%.2f x16=%.2f" % (s[1], s[4], s[16])
    check("limit: order saving collapses with parallel sites (max over the touchdown)", t_multi)

    print("-- Part C: L7K31 excursion --")
    bm = Lazy(lambda: baseline_model(base, PROGRAM_ORDER))
    fs = Lazy(lambda: detect_findings(lot, bm(), PROGRAM_ORDER))
    def find(kind):
        return [f for f in fs() if f["kind"] == kind]
    check("finding: 4 findings total, no false alarm on the 11 clean wafers", lambda: (
        lambda s: (len(fs()) == 4, s))([f["kind"] + fmt_wafers(f["wafers"]) for f in fs()].__repr__()))
    def t_ring():
        r = find("EDGE_RING")
        return len(r) == 1 and r[0]["wafers"] == truth["ring"]["wafers"] and r[0]["tests"] == [truth["ring"]["test"]]
    check("finding: EDGE_RING W5..8 on MBIST", t_ring)
    def t_clu():
        c = find("CLUSTER")
        if len(c) != 1:
            return False, str(c)
        d = c[0]["detail"]
        dist = math.hypot(d["cx"] - truth["cluster"]["cx"], d["cy"] - truth["cluster"]["cy"])
        # 허용 1.5 다이 (위와 같은 이유), SA·TD 는 같은 finding 으로 묶여야 한다
        return c[0]["wafers"] == [3] and {"SA", "TD"} <= set(c[0]["tests"]) and dist <= 1.5, "dist=%.2f tests=%s" % (dist, c[0]["tests"])
    check("finding: CLUSTER W3 center within 1.5 die, SA+TD merged", t_clu)
    def t_mask():
        m = find("MASKED")
        ok = len(m) == 1 and m[0]["wafers"] == truth["masked"]["wafers"] and m[0]["tests"] == ["MBIST"] and m[0]["detail"]["behind"] == "SA"
        return ok, "first=%.3f true=%.3f" % (m[0]["detail"]["first"], m[0]["detail"]["true"]) if m else "none"
    check("finding: MASKED W13..16 MBIST behind SA (SA RATE finding absorbed)", t_mask)
    def t_rate():
        return len(find("RATE")) == 0
    check("finding: no extra RATE findings", t_rate)
    def t_shift():
        p = find("PARAM_SHIFT")
        fm = [f for f in p if "fmax" in f["tests"]]
        ok = len(p) == 1 and len(fm) == 1 and fm[0]["wafers"] == truth["shift"]["wafers"] and fm[0]["detail"]["delta"]["fmax"] < 0
        # 170 MHz per z * dz -0.7 = -119 MHz 주입. 허용 20 MHz: 웨이퍼 간 변동 sd 17 MHz + 기준 중앙값 오차
        return ok and abs(fm[0]["detail"]["delta"]["fmax"] + 119.0) <= 20.0, "delta=%.1f MHz wafers=%s tests=%s" % (
            fm[0]["detail"]["delta"]["fmax"] if fm else 0.0, fm[0]["wafers"] if fm else [], fm[0]["tests"] if fm else [])
    check("finding: PARAM_SHIFT W19..24 fmax about -119 MHz, one merged finding", t_shift)
    def t_clean():
        # 기준 로트를 절반씩 나눠 서로 돌려도 아무것도 안 나와야 한다 (같은 공정의 정상 변동)
        early = [d for d in base if d.wafer <= 12]
        late = [d for d in base if d.wafer > 12]
        a = detect_findings(late, baseline_model(early, PROGRAM_ORDER), PROGRAM_ORDER)
        b = detect_findings(early, baseline_model(late, PROGRAM_ORDER), PROGRAM_ORDER)
        return a == [] and b == [], "findings %d/%d" % (len(a), len(b))
    check("good model: L7K12 halves against each other -> 0 findings", t_clean)
    rpt = Lazy(lambda: lot_report(meta_b, lot, base, PROGRAM_ORDER))
    def t_rpt():
        text = rpt()
        need = ("LOT L7K31 product=RX7 program=P2.1 wafers=25 dies=5525", "FINDINGS 4", "F1 CLUSTER W3 tests=SA,TD",
                "EDGE_RING W5..8 tests=MBIST", "MASKED W13..16 tests=MBIST behind=SA", "PARAM_SHIFT W19..24 tests=fmax", "action:", "ORDER program=")
        miss = [x for x in need if x not in text]
        return not miss, "missing %s" % miss if miss else ""
    check("lot_report: key lines present", t_rpt)
    def t_value():
        w_shift = [d for d in lot if d.wafer in truth["shift"]["wafers"]]
        dy = yield_summary(w_shift)["yield"] - yield_summary(base)["yield"]
        v_s, v_b = value_index(w_shift), value_index(base)
        return abs(dy) <= 0.04 and v_s < 0.75 * v_b, "dyield=%+.1fpp value %.2f vs %.2f" % (dy * 100, v_s, v_b)
    check("param-only excursion: hard yield flat (±4pp) but value falls > 25%", t_value)
    def t_null():
        rng = random.Random(0xA0C)
        hits = 0
        for _ in range(300):
            ds = synth_wafer(rng, sites, 0.08)
            if find_clusters(ds, BIT["MBIST"]) or is_ring(edge_ring(ds, BIT["MBIST"])):
                hits += 1
        return hits <= 3, "false alarms %d/300" % hits
    check("null wafers: false-alarm rate <= 1% (ring or cluster)", t_null)
    def t_power():
        rng = random.Random(0xA0D)
        weak_c = strong_c = weak_r = strong_r = 0
        n = 60
        for _ in range(n):
            if find_clusters(synth_wafer(rng, sites, 0.08, blob=(-3, 2, 1.1, 0.5)), BIT["MBIST"]):
                weak_c += 1
            if find_clusters(synth_wafer(rng, sites, 0.08, blob=(-3, 2, 3.0, 0.8)), BIT["MBIST"]):
                strong_c += 1
            if is_ring(edge_ring(synth_wafer(rng, sites, 0.08, ring_p=0.15), BIT["MBIST"])):
                weak_r += 1
            if is_ring(edge_ring(synth_wafer(rng, sites, 0.08, ring_p=0.55), BIT["MBIST"])):
                strong_r += 1
        return strong_c >= 0.9 * n and strong_r >= 0.9 * n and weak_c <= 0.2 * n and weak_r <= 0.3 * n, (
            "cluster weak %d/%d strong %d/%d; ring weak %d/%d strong %d/%d" % (weak_c, n, strong_c, n, weak_r, n, strong_r, n))
    check("limit: strong signatures found >= 90%, weak ones mostly missed", t_power)
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


# ======================================================================== 측정표 (md 에 붙인 숫자)
def print_tables():
    _, base = load_lot(lot_path("L7K12"))
    _, lot = load_lot(lot_path("L7K31"))
    t = dict(TEST_MS)
    print("== marginals L7K12 ==")
    m = marginals(base)
    print(" ".join("%s=%.3f" % (k, m[k]) for k in TESTS))
    print("== order table L7K12 (ms per die, single site) ==")
    ords = [("program", PROGRAM_ORDER), ("ratio(prec)", greedy_order(base, t, PRECEDENCE, conditional=False)),
            ("greedy(cond)", greedy_order(base, t, PRECEDENCE)), ("dp(prec)", dp_order(base, t, PRECEDENCE)[0]),
            ("dp(free)", dp_order(base, t, ())[0])]
    full = sum(t.values())
    y = yield_summary(base)["yield"]
    print("full_test=%.0f ms  yield=%.4f  bound: pass dies always cost %.1f ms of the mean" % (full, y, y * full))
    for name, o in ords:
        print("%-13s pred_indep=%7.1f  empirical=%7.1f  %s" % (name, expected_time(o, m, t), empirical_time(base, o, t), " ".join(o)))
    dpb = dp_order(base, t, PRECEDENCE)[0]
    print("== same order applied to L7K31 (excursion lot) ==")
    print("program=%.1f  dp-of-L7K12=%.1f  dp-of-L7K31=%.1f" % (empirical_time(lot, PROGRAM_ORDER, t), empirical_time(lot, dpb, t), dp_order(lot, t, PRECEDENCE)[1]))
    print("== masked table L7K12, program order ==")
    for r in masked_table(base, PROGRAM_ORDER):
        print("%-7s true=%.3f first=%.3f masked=%5.1f%%" % (r["test"], r["true"], r["first"], r["masked"] * 100))
    print("== sampling cost (L7K12, program order) ==")
    stop = empirical_time(base, PROGRAM_ORDER, t)
    for den in (1, 2, 8, 32, 128):
        c = sampled_cost(base, PROGRAM_ORDER, t, lambda d: pick_hash(d, den))
        est, n = estimate_rates(base, lambda d: pick_hash(d, den))
        err = max(abs(est[k] - m[k]) for k in TESTS)
        print("1/%-4d n=%-5d mean=%.1f ms (+%.1f ms)  max|p_hat-p|=%.4f" % (den, n, c, c - stop, err))
    print("== multi-site saving, program -> dp(prec), L7K12 ==")
    for k in (1, 2, 4, 8, 16, 32):
        a, b = touchdown_time(base, PROGRAM_ORDER, t, k), touchdown_time(base, dpb, t, k)
        print("x%-3d program=%7.2f dp=%7.2f ms/die  save=%5.2f%%" % (k, a, b, (a - b) / a * 100))
    print("== guardband (L7K12), counts per bin ==")
    print("gb_mv  " + "  ".join("%5s" % n for n in ("S1", "S2", "S3", "rej")) + "  value")
    for gb in (0, 5, 10, 20, 30):
        tb = guardband_table(BIN_TABLE, gb)
        c = bin_counts(base, tb)
        rej = sum(v for k, v in c.items() if k not in PRICE)
        print("%5d  %5d  %5d  %5d  %5d  %.3f" % (gb, c["S1"], c["S2"], c["S3"], rej, value_index(base, tb)))
    print("== detection power (synthetic bg 8%, 200 wafers each) ==")
    sites = sorted({(d.x, d.y) for d in base if d.wafer == 1})
    rng = random.Random(0xA0E)
    print("cluster radius \\ peak p   0.30   0.50   0.80")
    for rad in (1.1, 1.5, 2.0, 3.0, 4.0):
        row = []
        for p in (0.30, 0.50, 0.80):
            hit = sum(1 for _ in range(200) if find_clusters(synth_wafer(rng, sites, 0.08, blob=(-3, 2, rad, p)), BIT["MBIST"]))
            row.append("%5.1f%%" % (hit / 2.0))
        print("%14.1f           %s" % (rad, " ".join(row)))
    print("ring p (edge zone r>=0.85)  detected")
    for p in (0.12, 0.15, 0.20, 0.30, 0.45, 0.55):
        hit = sum(1 for _ in range(200) if is_ring(edge_ring(synth_wafer(rng, sites, 0.08, ring_p=p), BIT["MBIST"])))
        print("%26.2f  %5.1f%%" % (p, hit / 2.0))
    nul = sum(1 for _ in range(1000) if (lambda ds: find_clusters(ds, BIT["MBIST"]) or is_ring(edge_ring(ds, BIT["MBIST"])))(synth_wafer(rng, sites, 0.08)))
    print("null wafers flagged: %d/1000" % nul)


def main(argv):
    try:
        if len(argv) > 1 and argv[1] == "--report":
            meta, lot = load_lot(lot_path("L7K31"))
            _, base = load_lot(lot_path("L7K12"))
            sys.stdout.write(lot_report(meta, lot, base, PROGRAM_ORDER))
            return 0
        if len(argv) > 3 and argv[1] == "--map":
            _, lot = load_lot(lot_path(argv[2]))
            print(wafer_map(lot, int(argv[3])))
            return 0
        if len(argv) > 1 and argv[1] == "--tables":
            print_tables()
            return 0
    except NotImplementedError as e:
        print("TODO not implemented: %s" % e, file=sys.stderr)
        return 2
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
