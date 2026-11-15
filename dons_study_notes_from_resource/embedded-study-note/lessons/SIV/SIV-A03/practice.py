#!/usr/bin/env python3
"""SIV-A03 추가 연습 (Python 세트) — 전부 PASS, exit 0
  P4 DVFS표에 측정점을 어디 더 찍을까: 합법 영역에서 최악 고유방향을 줄이는 탐욕 설계
  P5 잔차 붓스트랩으로 Pleak 표준오차 추정 vs 해석해 비교
  P6 load-line: V_set을 V_die로 잘못 쓰면 생기는 C_eff 편향과 보정

    python3 -I practice.py
"""
import math
import random
import statistics
import sys

_stats = [0, 0]


def check(cond, name, detail=""):
    _stats[0] += 1
    if cond:
        _stats[1] += 1
    print(("PASS " if cond else "FAIL ") + name + (" : " + detail if detail else ""))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def norm(a):
    return math.sqrt(dot(a, a))


def lstsq2(X, y):
    """2열 최소제곱 (Gram 행렬 직접 풀이 — 2x2라 충분). 반환 beta, rss"""
    a, b, d = sum(r[0] * r[0] for r in X), sum(r[0] * r[1] for r in X), sum(r[1] * r[1] for r in X)
    p, q = sum(r[0] * yi for r, yi in zip(X, y)), sum(r[1] * yi for r, yi in zip(X, y))
    det = a * d - b * b
    beta = ((d * p - b * q) / det, (a * q - b * p) / det)
    rss = sum((yi - beta[0] * r[0] - beta[1] * r[1]) ** 2 for r, yi in zip(X, y))
    cov00 = d / det
    return beta, rss, cov00


def scaled_cond2(X):
    """2열 행렬의 스케일된 조건수: 열 단위화 후 smax/smin (2x2 고유값 닫힌 해)"""
    n1, n2 = norm([r[0] for r in X]), norm([r[1] for r in X])
    r12 = sum((r[0] / n1) * (r[1] / n2) for r in X)
    lam_max, lam_min = 1 + abs(r12), 1 - abs(r12)
    return math.sqrt(lam_max / lam_min) if lam_min > 0 else float("inf")


TRUTH = {"L25": 0.150, "a": 0.025, "b": 4.0, "C": 1.6}
DVFS = ((0.72, 0.8), (0.76, 1.1), (0.80, 1.4), (0.84, 1.7), (0.88, 2.0))


def leak(t, v):
    return TRUTH["L25"] * (v / 0.8) * math.exp(TRUTH["a"] * (t - 25) + TRUTH["b"] * (v - 0.8))


def vmin_for(f):
    """DVFS표의 V_min(f) (선형 보간, 0.8 GHz 아래는 0.72V). 표 아래 전압은 불법(타이밍 실패)."""
    pts = DVFS
    if f <= pts[0][1]:
        return pts[0][0]
    for (v0, f0), (v1, f1) in zip(pts, pts[1:]):
        if f <= f1:
            return v0 + (f - f0) / (f1 - f0) * (v1 - v0)
    return pts[-1][0]


def design_row(v, f):
    return [v / 0.8 * math.exp(TRUTH["b"] * (v - 0.8)), v * v * f]


def p4_greedy_point(points, cand):
    """points(현재 (v,f)들)에 후보 중 하나를 더해 스케일된 조건수를 최소화하는 합법 점을 고른다."""
    best = None
    for vf in cand:
        if vf[0] < vmin_for(vf[1]) - 1e-9:  # V_min 미만은 불법
            continue
        k = scaled_cond2([design_row(*p) for p in points + [vf]])
        if best is None or k < best[0]:
            best = (k, vf)
    return best


def p5_bootstrap_se(seed, n_boot=300):
    """슬라이스(같은 V) 고정 x에서 잔차 붓스트랩으로 intercept 표준오차를 낸다."""
    rng = random.Random(seed)
    v = 0.80
    xs = [v * v * f for f in (0.8, 1.1, 1.4, 1.7, 2.0)]
    # 공통 잔차 풀을 키우려고 9개 슬라이스의 잔차를 모음 (주제: 슬라이스당 dof 3만으로는 잔차 분포를 못 본다)
    pool = []
    fits = []
    for k in range(9):
        y = [leak(25, v) + TRUTH["C"] * x + rng.gauss(0, 0.004) for x in xs]
        (b0, b1), rss, c00 = lstsq2([[1.0, x] for x in xs], y)
        pool += [yi - b0 - b1 * x for x, yi in zip(xs, y)]
        fits.append((b0, b1, y, c00, rss))
    b0, b1, y, c00, rss = fits[0]
    s2 = sum(e * e for e in pool) / (len(pool) - 2)
    analytic = math.sqrt(s2 * c00)
    # 붓스트랩: 첫 슬라이스의 적합값 + 풀에서 뽑은 잔차로 재적합
    samples = []
    for _ in range(n_boot):
        ys = [b0 + b1 * x + rng.choice(pool) for x in xs]
        (bb0, _), _, _ = lstsq2([[1.0, x] for x in xs], ys)
        samples.append(bb0)
    return statistics.pstdev(samples), analytic


def p6_loadline(seed):
    """이 슬라이스는 부하에 따라 V_die가 처진다: V_die = V_set - I*R_LL, I = P/V_die.
    반환 (C_with_vset, C_with_vdie) 두 추정."""
    rng = random.Random(seed)
    Rll = 0.015  # 15 mOhm (대표값)
    vset = 0.88
    fs = (0.8, 1.1, 1.4, 1.7, 2.0)
    pts = []
    for f in fs:
        vd = vset
        for _ in range(30):
            p = leak(25, vd) + TRUTH["C"] * vd * vd * f
            vd = vset - (p / vd) * Rll
        pts.append((f, vd, p + rng.gauss(0, 0.002)))
    Xset = [[1.0, vset * vset * f] for f, _, _ in pts]
    Xdie = [[1.0, vd * vd * f] for f, vd, _ in pts]
    y = [p for _, _, p in pts]
    return lstsq2(Xset, y)[0][1], lstsq2(Xdie, y)[0][1]


def main():
    print("--- P4 greedy extra point on a DVFS-locked plan")
    points = [(v, f) for v, f in DVFS]
    k0 = scaled_cond2([design_row(*p) for p in points])
    cand = [(v, f) for v in (0.72, 0.76, 0.80, 0.84, 0.88) for f in (0.8, 1.1, 1.4, 1.7, 2.0)]
    k1, chosen = p4_greedy_point(points, cand)
    print("  base kappa %.1f, best extra point (V=%.2f, f=%.1f) -> kappa %.1f" % (k0, chosen[0], chosen[1], k1))
    check(k0 / k1 > 1.15, "adding one legal point reduces kappa", "%.1f -> %.1f" % (k0, k1))
    check(chosen[0] >= vmin_for(chosen[1]) - 1e-9, "chosen point respects V_min(f)", str(chosen))
    check(chosen[1] <= 1.1, "best extra point is low-f (over-volted region breaks V-f coupling)", str(chosen))

    print("--- P5 residual bootstrap vs analytic SE")
    ratios = [p5_bootstrap_se(50 + k) for k in range(5)]
    for boot, ana in ratios:
        print("  boot SE=%.5f analytic SE=%.5f ratio=%.2f" % (boot, ana, boot / ana))
    check(all(0.75 < b / a < 1.25 for b, a in ratios), "bootstrap SE within 25% of analytic SE (5 seeds)")

    print("--- P6 load-line correction")
    es, ed = [], []
    for k in range(10):
        cs, cd = p6_loadline(90 + k)
        es.append(cs)
        ed.append(cd)
    ms, md = statistics.mean(es), statistics.mean(ed)
    print("  mean C_eff with V_set=%.4f, with V_die=%.4f, truth=%.4f" % (ms, md, TRUTH["C"]))
    check(ms < TRUTH["C"] * 0.96, "using V_set biases C_eff down (>4%)", "%.3f" % ms)
    check(abs(md - TRUTH["C"]) < 0.03, "using V_die recovers C_eff within 0.03 nF", "%.3f" % md)

    print("%d/%d passed" % (_stats[1], _stats[0]))
    sys.exit(0 if _stats[1] == _stats[0] else 1)


if __name__ == "__main__":
    main()
