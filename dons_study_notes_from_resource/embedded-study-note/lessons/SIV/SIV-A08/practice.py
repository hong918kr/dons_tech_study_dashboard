#!/usr/bin/env python3
"""SIV-A08 추가 연습 — 한 번에 검증 (자기완결: 로더만 solution 에서 축약해 넣었다)

  연습 1  GDBN (Good Die in Bad Neighborhood) 스크린
  연습 2  Static PAT — robust 평균 ± 6 robust sigma 이상치
  연습 3  guardband: overkill vs escape (측정 잡음 Monte Carlo)
  연습 4  Poisson 모델 검사: 2-다이 창으로 Y(2A) vs Y(A)^2 → 군집
  연습 5  순서를 샘플 datalog 로 고르면 얼마나 손해인가

  python3 -I practice.py     # "N/M passed", 실패 시 exit 1.  Python 3.9 표준 라이브러리만.
"""
import collections
import csv
import itertools
import math
import os
import random
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TEST_MS = collections.OrderedDict([("CONT", 12.0), ("IDDQ", 35.0), ("PLL", 70.0), ("SA", 160.0),
                                   ("TD", 240.0), ("MBIST", 300.0), ("VMIN", 110.0), ("SERDES", 90.0)])
TESTS = tuple(TEST_MS)
BIT = {t: 1 << i for i, t in enumerate(TESTS)}
Die = collections.namedtuple("Die", "wafer x y vmin fmax leak mask")


def load_lot(lot):
    dies = []
    with open(os.path.join(HERE, "lot_%s.csv" % lot), encoding="utf-8", newline="") as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "wafer":
                continue
            mask = 0
            for t in filter(None, row[6].split("+")):
                mask |= BIT[t]
            dies.append(Die(int(row[0]), int(row[1]), int(row[2]), float(row[3]), float(row[4]), float(row[5]), mask))
    return dies


g_pass = g_total = 0


def check(name, ok, detail=""):
    global g_pass, g_total
    g_total += 1
    g_pass += bool(ok)
    print("%s  %s%s" % ("PASS" if ok else "FAIL", name, "  [%s]" % detail if detail else ""))


# ---------------------------------------------------------------- 연습 1: GDBN
def gdbn(dies_w, min_bad=4):
    """passing 다이 중 8-이웃의 fail 수 >= min_bad 인 좌표 집합 (차량용 품질 관행: 나쁜 동네의 좋은 다이를 버린다)."""
    fail = {(d.x, d.y) for d in dies_w if d.mask}
    out = set()
    for d in dies_w:
        if d.mask:
            continue
        bad = sum((d.x + dx, d.y + dy) in fail for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy)
        if bad >= min_bad:
            out.add((d.x, d.y))
    return out


# ---------------------------------------------------------------- 연습 2: Static PAT
def quantile(sorted_vals, q):
    """선형 보간 분위수 (q in [0,1])."""
    i = q * (len(sorted_vals) - 1)
    lo = math.floor(i)
    hi = math.ceil(i)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (i - lo)


def pat_limits(values, k=6.0):
    """robust mean = 중앙값, robust sigma = IQR / 1.35. → (LPL, UPL)."""
    v = sorted(values)
    med = quantile(v, 0.5)
    sigma = (quantile(v, 0.75) - quantile(v, 0.25)) / 1.35
    return med - k * sigma, med + k * sigma


# ---------------------------------------------------------------- 연습 3: guardband
def overkill_escape(n, sigma_true, sigma_meas, spec, gb, seed):
    """true Vmin ~ N(spec-20, sigma_true), 측정 = true + N(0, sigma_meas), 테스트 리밋 = spec - gb.
    overkill = 실제 양품인데 버린 비율(양품 대비), escape = 실제 불량인데 통과한 비율(불량 대비)."""
    rng = random.Random(seed)
    good_rej = bad_acc = good = bad = 0
    for _ in range(n):
        tv = rng.gauss(spec - 20.0, sigma_true)
        m = tv + rng.gauss(0.0, sigma_meas)
        accept = m <= spec - gb
        if tv <= spec:
            good += 1
            good_rej += not accept
        else:
            bad += 1
            bad_acc += accept
    return good_rej / good, bad_acc / bad


# ---------------------------------------------------------------- 연습 4: 창으로 Poisson 검사
def window_yields(dies):
    """Y1 = 다이 수율, Y2 = x 방향으로 겹치지 않는 2-다이 쌍(x 짝수, x+1)이 둘 다 pass 인 비율."""
    pos = {(d.wafer, d.x, d.y): d for d in dies}
    y1 = sum(1 for d in dies if d.mask == 0) / len(dies)
    both = total = 0
    for (w, x, y), d in pos.items():
        if x % 2 != 0:
            continue
        o = pos.get((w, x + 1, y))
        if o is None:
            continue
        total += 1
        both += d.mask == 0 and o.mask == 0
    return y1, both / total, total


# ---------------------------------------------------------------- 연습 5: 샘플로 순서 고르기
def die_time(d, order, t):
    total = 0.0
    for n in order:
        total += t[n]
        if d.mask & BIT[n]:
            break
    return total


def mean_time(dies, order, t):
    return sum(die_time(d, order, t) for d in dies) / len(dies)


def best_order(dies, t, must_first=("CONT",)):
    """전수 탐색 (앞에 must_first 고정, 나머지 7! = 5040)."""
    rest = [n for n in t if n not in must_first]
    best, best_v = None, float("inf")
    masks = collections.Counter(d.mask for d in dies)
    for p in itertools.permutations(rest):
        o = list(must_first) + list(p)
        v = 0.0
        for m, c in masks.items():
            tt = 0.0
            for n in o:
                tt += t[n]
                if m & BIT[n]:
                    break
            v += c * tt
        v /= len(dies)
        if v < best_v:
            best, best_v = o, v
    return best, best_v


def run():
    base = load_lot("L7K12")
    lot = load_lot("L7K31")
    print("-- 연습 1: GDBN --")
    tiny = [Die(1, x, y, 700.0, 2300.0, 90.0, 1 if (x, y) in {(-1, 0), (0, 1), (1, 0), (0, -1)} else 0)
            for x in range(-1, 2) for y in range(-1, 2)]
    check("center die with 4 failing neighbours flagged, corners are not", gdbn(tiny) == {(0, 0)})
    w3 = [d for d in lot if d.wafer == 3]
    w3_gd = gdbn(w3)
    clean = [d for d in lot if d.wafer == 2]
    check("wafer 3 (cluster) has GDBN dies inside the blob, more than a clean wafer",
          len(w3_gd) > len(gdbn(clean)) and any(math.hypot(x + 4, y - 3) <= 3 for x, y in w3_gd),
          "W3=%d W2=%d" % (len(w3_gd), len(gdbn(clean))))

    print("-- 연습 2: Static PAT --")
    lo, hi = pat_limits([100.0, 101.0, 102.0, 103.0, 104.0])
    check("pat_limits hand value: median 102, IQR 2 -> 102 +- 6*2/1.35",
          abs(lo - (102 - 6 * 2 / 1.35)) < 1e-9 and abs(hi - (102 + 6 * 2 / 1.35)) < 1e-9)
    good = [d for d in base if d.mask == 0]
    lpl, upl = pat_limits([d.leak for d in good])
    flagged = [d for d in good if not lpl <= d.leak <= upl]
    check("L7K12 clean dies: nothing outside PAT, and the symmetric LPL is negative (skewed leak -> lower limit is useless)",
          flagged == [] and lpl < 0.0, "LPL=%.1f UPL=%.1f flagged=%d/%d" % (lpl, upl, len(flagged), len(good)))
    planted = [d.leak for d in good] + [400.0]
    lpl2, upl2 = pat_limits(planted)
    check("planted 400 mA outlier is outside PAT limits even though median/IQR barely move", 400.0 > upl2 and abs(upl2 - upl) < 1.0,
          "UPL %.1f -> %.1f" % (upl, upl2))

    print("-- 연습 3: guardband overkill vs escape --")
    rows = [(g, overkill_escape(200000, 10.0, 4.0, 745.0, g, 8)) for g in (0.0, 4.0, 8.0, 12.0)]
    for g, (ok, es) in rows:
        print("  gb=%4.1f mV  overkill=%6.2f%%  escape=%6.2f%%" % (g, ok * 100, es * 100))
    check("overkill rises and escape falls monotonically with guardband",
          all(rows[i][1][0] < rows[i + 1][1][0] and rows[i][1][1] > rows[i + 1][1][1] for i in range(3)))
    check("gb = 0 lets a real share of bad dies through (escape > 10%)", rows[0][1][1] > 0.10, "%.1f%%" % (rows[0][1][1] * 100))

    print("-- 연습 4: 창으로 Poisson 검사 --")
    y1, y2, n = window_yields(base)
    check("L7K12: Y(2A) > Y(A)^2 by > 1 pp -> defects cluster (Poisson under-predicts big-die yield)",
          y2 > y1 * y1 + 0.01, "Y1=%.4f Y1^2=%.4f Y2=%.4f pairs=%d" % (y1, y1 * y1, y2, n))
    rng = random.Random(44)
    pois = [Die(1, x, y, 700.0, 2300.0, 90.0, 1 if rng.random() < 1 - y1 else 0) for x in range(-60, 60) for y in range(-60, 60)]
    py1, py2, pn = window_yields(pois)
    se = math.sqrt(py2 * (1 - py2) / pn)
    check("control: independent fails -> Y2 == Y1^2 within 3 sigma of the pair sample", abs(py2 - py1 * py1) <= 3 * se,
          "Y2=%.4f Y1^2=%.4f se=%.4f" % (py2, py1 * py1, se))

    print("-- 연습 5: 샘플로 순서 고르기 --")
    t = dict(TEST_MS)
    pick = lambda d, den: zlib.crc32(("%d,%d,%d,salt" % (d.wafer, d.x, d.y)).encode()) % den == 0
    full_o, full_v = best_order(base, t)
    samp = [d for d in base if pick(d, 8)]
    samp_o, _ = best_order(samp, t)
    samp_v = mean_time(base, samp_o, t)
    check("order learned from a 1/8 full-datalog sample is within 1% of the full-data optimum on the full lot",
          (samp_v - full_v) / full_v <= 0.01, "full=%.1f sampled-order=%.1f n=%d" % (full_v, samp_v, len(samp)))
    check("and both beat the as-written program order", samp_v < mean_time(base, list(TESTS), t))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run() else 1)
