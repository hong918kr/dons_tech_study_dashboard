#!/usr/bin/env python3
"""SIV-A03 전력 특성화 자동화 — Python 세트: dynamic / leakage 분리 분석

    P = Pleak(T,V) + alpha*C*V^2*f    (alpha*C = C_eff 는 곱으로만 관측된다)

  Part A  최소제곱(직접 구현), 슬라이스 회귀(같은 T,V에서 P vs V^2 f), ln(Pleak) vs T 가중 회귀
  Part B  식별 가능성: 점 부족, f 범위 좁음, V-f 묶임(DVFS표), 자기발열 / 조건수와 복원 오차 측정
  Part C  캠페인: power_campaign.csv (칩 8개, 결함 심음) → 칩별 판정과 리포트

    python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
    python3 -I solution.py --report   # 테스트 + 시나리오 표 + 캠페인 리포트

Python 3.9 표준 라이브러리만 쓴다. 단위: W, V, GHz, C_eff는 nF, 온도 C.
"""
import csv
import json
import math
import os
import random
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_stats = [0, 0]


def check(cond, name, detail=""):
    _stats[0] += 1
    if cond:
        _stats[1] += 1
    print(("PASS " if cond else "FAIL ") + name + (" : " + detail if detail else ""))


class RankDeficient(Exception):
    pass


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def norm(a):
    return math.sqrt(dot(a, a))


# ---------------------------------------------------------------- 합성 데이터 (주어진 코드)
WORKLOADS = ("idle", "mem", "cpu")
TEMPS = (-40, 0, 25, 85, 105)
VOLTS = (0.72, 0.80, 0.88)
FREQS = (0.8, 1.1, 1.4, 1.7, 2.0)
TRUTH = {"L25": 0.150, "a": 0.025, "b": 4.0, "C": {"idle": 0.15, "mem": 0.9, "cpu": 1.6}}


def leak_true(t, v, tr=TRUTH):
    return tr["L25"] * (v / 0.8) * math.exp(tr["a"] * (t - 25) + tr["b"] * (v - 0.8))


def dyn_true(w, v, f, tr=TRUTH):
    return tr["C"][w] * v * v * f


def plan_full(temps=TEMPS, volts=VOLTS, freqs=FREQS, workloads=WORKLOADS):
    return [(w, t, v, f) for w in workloads for t in temps for v in volts for f in freqs]


DVFS_PAIRS = ((0.72, 0.8), (0.76, 1.1), (0.80, 1.4), (0.84, 1.7), (0.88, 2.0))


def plan_dvfs(temps=TEMPS, pairs=DVFS_PAIRS, workloads=WORKLOADS):
    return [(w, t, v, f) for w in workloads for t in temps for v, f in pairs]


def synth(plan, seed, rth=0.0, sigma_abs=0.003, sigma_rel=0.002, tj_offset=0.0, tj_noise=0.3, tr=TRUTH):
    """행: dict(w,t,v,f,tj,p). rth>0이면 자기발열 고정점 반복. t는 라벨(챔버), tj는 온도센서(잡음+오프셋)."""
    rng = random.Random(seed)
    rows = []
    for w, t, v, f in plan:
        tj = t
        p = 0.0
        for _ in range(40):
            p = leak_true(tj, v, tr) + dyn_true(w, v, f, tr)
            if rth == 0.0:
                break
            tj = t + rth * p
            if tj > 175.0:
                raise ValueError("thermal runaway: T=%s V=%s f=%s rth=%s" % (t, v, f, rth))
        meas = p * (1 + rng.gauss(0, sigma_rel)) + rng.gauss(0, sigma_abs)
        tj_meas = tj + tj_offset + (rng.gauss(0, tj_noise) if tj_noise else 0.0)
        rows.append({"w": w, "t": t, "v": v, "f": f, "tj": tj_meas, "tj_true": tj, "p": meas})
    return rows


# ---------------------------------------------------------------- Part A: 최소제곱과 분리
def lstsq(X, y):
    """X(행 리스트, m x n)에 대한 최소제곱. dict(beta, rss, dof, cov_unscaled)를 돌려준다.
    cov_unscaled = (X^T X)^-1 (잡음분산 s^2를 곱하면 계수 공분산).
    열 스케일을 맞춘 뒤 수치적으로 종속이면(스케일된 R_jj < 1e-10) RankDeficient를 던진다.
    m < n 도 RankDeficient."""
    m, n = len(X), len(X[0]) if X else 0
    # TODO A1: Modified Gram-Schmidt QR(재직교화 2회), R 역행렬로 계수와 cov_unscaled
    if m < n or n == 0:
        raise RankDeficient("m<n")
    cols = [[X[i][j] for i in range(m)] for j in range(n)]
    scale = [norm(c) or 1.0 for c in cols]
    A = [[v / scale[j] for v in cols[j]] for j in range(n)]
    Q = []
    R = [[0.0] * n for _ in range(n)]
    for j in range(n):
        v = A[j][:]
        for _ in range(2):
            for i, q in enumerate(Q):
                r = dot(q, v)
                R[i][j] += r
                v = [a - r * b for a, b in zip(v, q)]
        rjj = norm(v)
        if rjj < 1e-10:
            raise RankDeficient("column %d" % j)
        R[j][j] = rjj
        Q.append([a / rjj for a in v])
    qty = [dot(q, y) for q in Q]
    b = [0.0] * n
    for i in range(n - 1, -1, -1):
        b[i] = (qty[i] - sum(R[i][k] * b[k] for k in range(i + 1, n))) / R[i][i]
    beta = [b[j] / scale[j] for j in range(n)]
    # R^-1 (상삼각)
    Rinv = [[0.0] * n for _ in range(n)]
    for j in range(n):
        Rinv[j][j] = 1.0 / R[j][j]
        for i in range(j - 1, -1, -1):
            Rinv[i][j] = -sum(R[i][k] * Rinv[k][j] for k in range(i + 1, j + 1)) / R[i][i]
    cov = [[sum(Rinv[i][k] * Rinv[j][k] for k in range(n)) / (scale[i] * scale[j])
            for j in range(n)] for i in range(n)]
    res = [yi - dot(xi, beta) for xi, yi in zip(X, y)]
    return {"beta": beta, "rss": dot(res, res), "dof": m - n, "cov_unscaled": cov}


def singular_values(X):
    """X의 특이값(내림차순). One-sided Jacobi로 구현 — X^T X의 고유값을 쓰면 조건수가 제곱된다."""
    # TODO A2: one-sided Jacobi 회전으로 열을 직교화, 열 노름 = 특이값
    A = [[row[j] for row in X] for j in range(len(X[0]))]
    n = len(A)
    for _ in range(60):
        rotated = False
        for p in range(n - 1):
            for q in range(p + 1, n):
                al, be, ga = dot(A[p], A[p]), dot(A[q], A[q]), dot(A[p], A[q])
                if abs(ga) <= 1e-15 * math.sqrt(al * be) or ga == 0.0:
                    continue
                rotated = True
                zeta = (be - al) / (2.0 * ga)
                t = math.copysign(1.0, zeta) / (abs(zeta) + math.sqrt(1.0 + zeta * zeta))
                c = 1.0 / math.sqrt(1.0 + t * t)
                s = c * t
                Ap, Aq = A[p], A[q]
                A[p] = [c * x - s * y for x, y in zip(Ap, Aq)]
                A[q] = [s * x + c * y for x, y in zip(Ap, Aq)]
        if not rotated:
            break
    return sorted((norm(col) for col in A), reverse=True)


def scaled_cond(X):
    """열을 단위 노름으로 맞춘 뒤의 조건수 smax/smin. 단위계에 의존하지 않게 하기 위함(smin=0이면 inf)."""
    n = len(X[0])
    cols = [norm([row[j] for row in X]) or 1.0 for j in range(n)]
    Xs = [[row[j] / cols[j] for j in range(n)] for row in X]
    # TODO A2b: singular_values로 조건수 계산
    sv = singular_values(Xs)
    return sv[0] / sv[-1] if sv[-1] > 0 else float("inf")


def fit_slices(rows):
    """(w,t,v)로 묶어 각 슬라이스에서 P = L + C*x, x = V^2*f 를 푼다.
    반환: (slices, info). slices[(w,t,v)] = dict(L, C, cov_LL, cov_CC, dof, rss).
    info = dict(pooled_s2 (dof 합으로 나눈 분산, dof 합이 0이면 None), underdetermined (점이 부족해 건너뛴 슬라이스 수)).
    슬라이스 안의 x가 같아서(또는 1개라서) 풀 수 없으면 건너뛴다(쓰레기를 돌려주지 않는다)."""
    # TODO A3: 슬라이스별 lstsq, RankDeficient는 underdetermined로 집계, 잔차 분산 풀링
    groups = {}
    for r in rows:
        groups.setdefault((r["w"], r["t"], r["v"]), []).append(r)
    slices, under, rss_sum, dof_sum = {}, 0, 0.0, 0
    for key, g in sorted(groups.items()):
        X = [[1.0, r["v"] * r["v"] * r["f"]] for r in g]
        y = [r["p"] for r in g]
        try:
            res = lstsq(X, y)
        except RankDeficient:
            under += 1
            continue
        slices[key] = {"L": res["beta"][0], "C": res["beta"][1], "cov_LL": res["cov_unscaled"][0][0],
                       "cov_CC": res["cov_unscaled"][1][1], "dof": res["dof"], "rss": res["rss"]}
        rss_sum += res["rss"]
        dof_sum += res["dof"]
    info = {"pooled_s2": rss_sum / dof_sum if dof_sum > 0 else None, "underdetermined": under}
    return slices, info


def combine_leak(slices, info):
    """워크로드 간 공통인 누설을 (t,v)별 역분산 가중 평균으로 합친다.
    반환 {(t,v): dict(L, se)}. pooled_s2가 None이면 se = inf."""
    # TODO A4: 역분산 가중 평균 (가중치 1/cov_LL)
    out = {}
    groups = {}
    for (w, t, v), s in slices.items():
        groups.setdefault((t, v), []).append(s)
    s2 = info["pooled_s2"]
    for key, g in groups.items():
        W = sum(1.0 / s["cov_LL"] for s in g)
        L = sum(s["L"] / s["cov_LL"] for s in g) / W
        out[key] = {"L": L, "se": math.sqrt(s2 / W) if s2 is not None else float("inf")}
    return out


def combine_ceff(slices, info):
    """워크로드별 C_eff를 (t,v) 전체에 대해 역분산 가중 평균. 반환 {w: dict(C, se)}."""
    # TODO A5: 역분산 가중 평균 (가중치 1/cov_CC)
    out = {}
    groups = {}
    for (w, t, v), s in slices.items():
        groups.setdefault(w, []).append(s)
    s2 = info["pooled_s2"]
    for w, g in groups.items():
        W = sum(1.0 / s["cov_CC"] for s in g)
        C = sum(s["C"] / s["cov_CC"] for s in g) / W
        out[w] = {"C": C, "se": math.sqrt(s2 / W) if s2 is not None else float("inf")}
    return out


def fit_temp_law(temps, L, se):
    """ln L = ln L25 + a*(T-25) 가중 회귀. L<=0 또는 se가 inf인 점은 버리고 dropped에 T를 기록.
    가중치 w = (L/se)^2 (델타법: Var[ln L] ~ se^2/L^2).
    반환 dict(a, a_se, L25, L25_se, dropped). 남은 점이 2개 미만이면 RankDeficient."""
    # TODO A6: 로그 변환 + 가중 최소제곱 (행에 sqrt(w)=L/se 곱하기)
    X, y, dropped = [], [], []
    for t, l, s in zip(temps, L, se):
        if l <= 0 or not math.isfinite(s) or s <= 0:
            dropped.append(t)
            continue
        w = l / s
        X.append([w, w * (t - 25.0)])
        y.append(w * math.log(l))
    res = lstsq(X, y)
    a = res["beta"][1]
    L25 = math.exp(res["beta"][0])
    cov = res["cov_unscaled"]
    return {"a": a, "a_se": math.sqrt(cov[1][1]), "L25": L25, "L25_se": L25 * math.sqrt(cov[0][0]), "dropped": dropped}


def fit_temp_law_unweighted(temps, L):
    """비교용(주어진 코드): 가중치 없는 ln 회귀."""
    pts = [(t, math.log(l)) for t, l in zip(temps, L) if l > 0]
    res = lstsq([[1.0, t - 25.0] for t, _ in pts], [y for _, y in pts])
    return res["beta"][1]


# ---------------------------------------------------------------- Part B: 식별 가능성
def pooled_per_t(rows):
    """'온도별로 P vs V^2 f' 를 V 구분 없이 푼다: (w,t)로만 묶음. 반환 {(w,t): dict(L, C)}.
    (V에 따라 누설이 변하면 편향됨 — Part B의 측정 대상)"""
    # TODO B1: (w,t)로 묶어 lstsq (x = V^2 f)
    groups = {}
    for r in rows:
        groups.setdefault((r["w"], r["t"]), []).append(r)
    out = {}
    for key, g in groups.items():
        res = lstsq([[1.0, r["v"] * r["v"] * r["f"]] for r in g], [r["p"] for r in g])
        out[key] = {"L": res["beta"][0], "C": res["beta"][1]}
    return out


def fit_varpro(rows):
    """자기발열 보정: 측정 Tj를 공변량으로 쓰는 전역 모델 (variable projection).
        P = L25[v] * exp(a*(Tj-25)) + C[w] * v^2 * f
    주어진 a마다 나머지(L25[v], C[w])는 선형 → lstsq의 rss를 a에 대해 최소화.
    a 격자 0.000..0.080 (0.005 간격) 후 최적 부근에서 황금분할 20회.
    반환 dict(a, L25 {v: ..}, C {w: ..}, rss, n, tj_span). Tj 범위가 5 C 미만이면 a = None(식별 불가)."""
    volts = sorted({r["v"] for r in rows})
    works = sorted({r["w"] for r in rows})
    # TODO B2: a에 대한 1-D 최적화 + 내부 선형 lstsq, 결과 조립
    vidx = {v: i for i, v in enumerate(volts)}
    widx = {w: len(volts) + i for i, w in enumerate(works)}
    y = [r["p"] for r in rows]

    def design(a):
        X = []
        for r in rows:
            row = [0.0] * (len(volts) + len(works))
            row[vidx[r["v"]]] = math.exp(a * (r["tj"] - 25.0))
            row[widx[r["w"]]] = r["v"] * r["v"] * r["f"]
            X.append(row)
        return X

    def rss(a):
        return lstsq(design(a), y)["rss"]

    tjs = [r["tj"] for r in rows]
    span = max(tjs) - min(tjs)
    if span < 5.0:
        best_a = 0.0
    else:
        grid = [i * 0.005 for i in range(17)]
        vals = [rss(a) for a in grid]
        k = vals.index(min(vals))
        lo, hi = grid[max(k - 1, 0)], grid[min(k + 1, 16)]
        g = (math.sqrt(5) - 1) / 2
        c1, c2 = hi - g * (hi - lo), lo + g * (hi - lo)
        f1, f2 = rss(c1), rss(c2)
        for _ in range(20):
            if f1 < f2:
                hi, c2, f2 = c2, c1, f1
                c1 = hi - g * (hi - lo)
                f1 = rss(c1)
            else:
                lo, c1, f1 = c1, c2, f2
                c2 = lo + g * (hi - lo)
                f2 = rss(c2)
        best_a = 0.5 * (lo + hi)
    res = lstsq(design(best_a), y)
    return {"a": best_a if span >= 5.0 else None, "tj_span": span,
            "L25": {v: res["beta"][vidx[v]] for v in volts},
            "C": {w: res["beta"][widx[w]] for w in works},
            "rss": res["rss"], "n": len(rows), "resid": [yi - dot(xi, res["beta"]) for xi, yi in zip(design(best_a), y)]}


def true_kappa(rows):
    """모델의 참 형태 열 [L형상(V), V^2 f]의 조건수 (a, b는 참값 사용). 주어진 코드."""
    X = [[r["v"] / 0.8 * math.exp(TRUTH["a"] * (r["t"] - 25) + TRUTH["b"] * (r["v"] - 0.8)),
          r["v"] * r["v"] * r["f"]] for r in rows]
    return scaled_cond(X)


def _err_pct(est, truth):
    return abs(est - truth) / truth * 100.0


def scenario_table(n_mc):
    """주어진 코드: 설계/모델을 바꿔 가며 L(25C,0.80V)와 C_cpu 복원 RMS 오차 측정."""
    rows_out = []

    def slice_stats(plan_fn, rth=0.0, t=25):
        el, ec = [], []
        for k in range(n_mc):
            rows = synth(plan_fn(), 1000 + k, rth=rth)
            sl, info = fit_slices(rows)
            L = combine_leak(sl, info)[(t, 0.80)]["L"]
            C = combine_ceff(sl, info)["cpu"]["C"]
            el.append(L)
            ec.append(C)
        return el, ec

    def rms(xs, truth):
        return math.sqrt(sum(((x - truth) / truth * 100.0) ** 2 for x in xs) / len(xs))

    def bias(xs, truth):
        return (statistics.mean(xs) - truth) / truth * 100.0

    Ctrue = TRUTH["C"]["cpu"]

    def plan_full25(fs=FREQS, t=25):
        return plan_full(temps=(t,), freqs=fs)

    rows0 = synth(plan_full25(), 1)

    def add(name, kappa, el, ec, t=25):
        Ltrue = leak_true(t, 0.80)
        rows_out.append({"name": name, "kappa": kappa, "L_rms": rms(el, Ltrue), "L_bias": bias(el, Ltrue),
                         "C_rms": rms(ec, Ctrue), "C_bias": bias(ec, Ctrue)})

    # S0 정상: 3V x 5f
    el, ec = slice_stats(lambda: plan_full25())
    add("S0 full 3Vx5f", scaled_cond([[1.0, r["v"] ** 2 * r["f"]] for r in rows0 if r["v"] == 0.80]), el, ec)
    # S1 f 2점 (0.8, 2.0)
    el, ec = slice_stats(lambda: plan_full25((0.8, 2.0)))
    add("S1 2 f points", scaled_cond([[1.0, r["v"] ** 2 * r["f"]] for r in synth(plan_full25((0.8, 2.0)), 1) if r["v"] == 0.80]), el, ec)
    # S2 좁은 f (1.90~2.00 GHz, 외삽)
    narrow = (1.90, 1.925, 1.95, 1.975, 2.00)
    el, ec = slice_stats(lambda: plan_full25(narrow))
    add("S2 narrow f 1.9-2.0", scaled_cond([[1.0, r["v"] ** 2 * r["f"]] for r in synth(plan_full25(narrow), 1) if r["v"] == 0.80]), el, ec)
    # S3 V-f 묶임(DVFS표) + 온도별 pooled
    el, ec = [], []
    for k in range(n_mc):
        rows = synth(plan_dvfs(temps=(25,)), 2000 + k)
        p = pooled_per_t(rows)
        el.append(sum(p[(w, 25)]["L"] for w in WORKLOADS) / len(WORKLOADS))
        ec.append(p[("cpu", 25)]["C"])
    add("S3 DVFS-locked pooled", true_kappa(synth(plan_dvfs(temps=(25,)), 1)), el, ec)
    # S4 자기발열 + 챔버 온도 라벨 슬라이스 (25C는 누설이 작아 잘 안 보이고, 85C에서 커진다)
    k0 = scaled_cond([[1.0, r["v"] ** 2 * r["f"]] for r in rows0 if r["v"] == 0.80])
    el, ec = slice_stats(lambda: plan_full25(), rth=6.0)
    add("S4a self-heat 25C, slice", k0, el, ec)
    el, ec = slice_stats(lambda: plan_full25(t=85), rth=6.0, t=85)
    add("S4b self-heat 85C, slice", k0, el, ec, t=85)
    # S5 자기발열 + Tj 공변량 varpro, S6 Tj 센서 오프셋 +5C
    for name, off in (("S5 self-heat, varpro(Tj)", 0.0), ("S6 varpro, Tj offset +5C", 5.0)):
        el, ec = [], []
        for k in range(n_mc):
            rows = synth(plan_full(temps=(0, 25, 85), workloads=("mem", "cpu")), 3000 + k, rth=6.0, tj_offset=off)
            res = fit_varpro(rows)
            el.append(res["L25"][0.80])
            ec.append(res["C"]["cpu"])
        add(name, float("nan"), el, ec)
    return rows_out


def selfheat_a_bias(n_mc):
    """주어진 코드: 챔버 온도로 푼 온도계수(슬라이스+ln회귀) vs Tj로 푼 온도계수(varpro)의 평균."""
    a_slice, a_vp = [], []
    for k in range(n_mc):
        rows = synth(plan_full(temps=TEMPS, workloads=("mem", "cpu")), 4000 + k, rth=6.0)
        sl, info = fit_slices(rows)
        le = combine_leak(sl, info)
        law = fit_temp_law(list(TEMPS), [le[(t, 0.80)]["L"] for t in TEMPS], [le[(t, 0.80)]["se"] for t in TEMPS])
        a_slice.append(law["a"])
        a_vp.append(fit_varpro(rows)["a"])
    return statistics.mean(a_slice), statistics.mean(a_vp)


def print_scenarios(tbl):
    print("%-28s %10s %9s %9s %9s %9s" % ("scenario", "kappa", "L_rms%", "L_bias%", "C_rms%", "C_bias%"))
    for r in tbl:
        print("%-28s %10.1f %9.2f %9.2f %9.2f %9.2f" % (r["name"], r["kappa"], r["L_rms"], r["L_bias"], r["C_rms"], r["C_bias"]))


# ---------------------------------------------------------------- Part C: 캠페인 (주어진 입력, TODO는 판정)
def load_campaign():
    rows = []
    with open(os.path.join(HERE, "power_campaign.csv"), encoding="utf-8", newline="") as fh:
        for r in csv.DictReader(fh):
            rows.append({"chip": r["chip"], "w": r["w"], "t": int(r["t"]), "v": float(r["v"]), "f": float(r["f"]),
                         "tj": float(r["tj"]), "i_a": float(r["i_a"]), "vbus_v": float(r["vbus_v"]), "p": float(r["p_w"])})
    return rows


def load_truth():
    with open(os.path.join(HERE, "power_campaign_truth.json"), encoding="utf-8") as fh:
        return json.load(fh)


def precheck_chip(rows):
    """데이터 유효성 관문. Tj가 챔버 온도를 따라가지 않으면(범위 < 챔버 범위의 30%) 'TJ_STUCK' 반환, 정상이면 None.
    (분석 전에 막아야 한다: Tj 고착이면 온도 보정 피팅이 '성공처럼' 보인다)"""
    # TODO C1: Tj 범위 vs 챔버 범위 비교
    tj = [r["tj"] for r in rows]
    t = [r["t"] for r in rows]
    if max(tj) - min(tj) < 0.3 * (max(t) - min(t)):
        return "TJ_STUCK"
    return None


def characterize_chip(rows):
    """칩 하나: fit_varpro → 측정 전력을 4개 구간으로 나눠 구간별 잔차 MAD로 강건 시그마(1.4826*MAD)
    → |잔차| > 8 시그마인 행을 outlier로 빼고 1회 재피팅. 시그마를 구간별로 두는 이유: 잡음이 P에 따라 달라서
    (Tj 센서 잡음 x dL/dT, 이득 잡음). 반환 dict(chip, L25 {v:..}, C {w:..}, a, sigma = 전체 MAD 시그마,
    outliers = 빠진 행의 list of (w,t,v,f,resid))."""
    # TODO C2: 피팅, MAD 기반 이상치 제거, 재피팅
    chip = rows[0]["chip"]
    fit = fit_varpro(rows)
    resid = fit["resid"]
    n = len(rows)
    order = sorted(range(n), key=lambda i: rows[i]["p"])
    bin_sigma = [0.0] * n
    for b in range(4):
        idx = order[b * n // 4:(b + 1) * n // 4]
        vals = [resid[i] for i in idx]
        med = statistics.median(vals)
        mad = statistics.median(abs(v - med) for v in vals) or 1e-9
        for i in idx:
            bin_sigma[i] = 1.4826 * mad
    med_all = statistics.median(resid)
    sigma = 1.4826 * (statistics.median(abs(e - med_all) for e in resid) or 1e-9)
    outliers, keep = [], []
    for r, e, s in zip(rows, resid, bin_sigma):
        if abs(e) > 8.0 * s:
            outliers.append((r["w"], r["t"], r["v"], r["f"], e))
        else:
            keep.append(r)
    if outliers:
        fit = fit_varpro(keep)
    return {"chip": chip, "L25": fit["L25"], "C": fit["C"], "a": fit["a"], "sigma": sigma, "outliers": outliers}


def classify_fleet(results):
    """results = characterize_chip 결과 list (blocked 칩은 dict(chip, blocked=이유)).
    fleet 중앙값(L25@0.80, C별) 대비 비율로 칩별 판정 dict(chip, verdict, ...)을 낸다. 우선순위:
      BLOCKED (precheck 실패)
      SENSOR_GAIN: 칩의 모든 C 비율과 L 비율이 같은 값(서로 15% 이내)이고 1에서 20% 이상 벗어남 → 측정 이득 문제
      IDLE_CEFF_HIGH: C_idle 비율 > 1.8 이고 C_cpu 비율이 1에서 10% 이내
      LEAKAGE_EXCESS: L 비율 > 1.5 이고 C_cpu 비율이 1에서 10% 이내
      그 외 PASS
    verdict dict 키: chip, verdict, r_L, r_C (C별 비율 dict), line(한 줄 리포트; 아래 형식):
      "FAIL chip=U3 verdict=LEAKAGE_EXCESS L25=0.331W fleet=0.150W r_L=2.21 r_Ccpu=1.00 a=0.0304"
      "BLOCKED chip=U7 reason=TJ_STUCK"
      "PASS chip=U1 ..."  (L25, r_L, r_Ccpu 포함)"""
    # TODO C3: fleet 중앙값, 비율 계산, 우선순위 판정, 리포트 줄
    done = [r for r in results if "blocked" not in r]
    med_L = statistics.median(r["L25"][0.80] for r in done)
    works = sorted(done[0]["C"].keys())
    med_C = {w: statistics.median(r["C"][w] for r in done) for w in works}
    out = []
    for r in results:
        if "blocked" in r:
            out.append({"chip": r["chip"], "verdict": "BLOCKED", "line": "BLOCKED chip=%s reason=%s" % (r["chip"], r["blocked"])})
            continue
        r_L = r["L25"][0.80] / med_L
        r_C = {w: r["C"][w] / med_C[w] for w in works}
        rcs = list(r_C.values()) + [r_L]
        if max(rcs) / min(rcs) < 1.15 and abs(r_C["cpu"] - 1.0) > 0.20:
            v = "SENSOR_GAIN"
        elif r_C["idle"] > 1.8 and abs(r_C["cpu"] - 1.0) < 0.10:
            v = "IDLE_CEFF_HIGH"
        elif r_L > 1.5 and abs(r_C["cpu"] - 1.0) < 0.10:
            v = "LEAKAGE_EXCESS"
        else:
            v = "PASS"
        head = "PASS" if v == "PASS" else "FAIL"
        line = "%s chip=%s verdict=%s L25=%.3fW fleet=%.3fW r_L=%.2f r_Ccpu=%.2f r_Cidle=%.2f a=%.4f" % (
            head, r["chip"], v, r["L25"][0.80], med_L, r_L, r_C["cpu"], r_C["idle"], r["a"])
        out.append({"chip": r["chip"], "verdict": v, "r_L": r_L, "r_C": r_C, "line": line})
    return out


def run_campaign():
    rows = load_campaign()
    by_chip = {}
    for r in rows:
        by_chip.setdefault(r["chip"], []).append(r)
    results = []
    for chip, g in sorted(by_chip.items()):
        reason = precheck_chip(g)
        if reason:
            results.append({"chip": chip, "blocked": reason})
        else:
            results.append(characterize_chip(g))
    return results, classify_fleet(results)


# ---------------------------------------------------------------- 테스트
def within(x, ref, tol):
    return abs(x - ref) <= tol


def test_part_a():
    print("--- Part A: 최소제곱과 분리")
    r = lstsq([[1, 1], [1, 2], [1, 3], [1, 4]], [3, 5, 7, 9])
    check(within(r["beta"][0], 1, 1e-9) and within(r["beta"][1], 2, 1e-9) and r["dof"] == 2, "lstsq exact line y=1+2x", str(r["beta"]))
    check(within(r["cov_unscaled"][1][1], 0.2, 1e-9), "cov_unscaled[1][1]=0.2 for x=1..4", str(r["cov_unscaled"][1][1]))
    try:
        lstsq([[1, 2], [2, 4], [3, 6]], [1, 2, 3])
        check(False, "rank-deficient columns raise")
    except RankDeficient:
        check(True, "rank-deficient columns raise")
    sv = singular_values([[3, 0], [0, 4], [0, 0]])
    check(within(sv[0], 4, 1e-9) and within(sv[1], 3, 1e-9), "singular_values diag (4,3)", str(sv))
    # 알려진 값: X=[[1,1],[1,1+e]] 근사 대신 2x2 회전 포함 행렬
    sv2 = singular_values([[1, 2], [3, 4], [5, 6]])
    check(within(sv2[0], 9.5255, 1e-3) and within(sv2[1], 0.5143, 1e-3), "singular_values vs known [[1,2],[3,4],[5,6]]", str(sv2))
    check(within(scaled_cond([[2, 0], [0, 1000]]), 1.0, 1e-9), "scaled_cond ignores units/column scale")

    rows = synth(plan_full(), seed=11)
    slices, info = fit_slices(rows)
    check(len(slices) == 45 and info["underdetermined"] == 0, "45 slices (3 wl x 5 T x 3 V), none underdetermined", str(len(slices)))
    check(all(s["dof"] == 3 for s in slices.values()), "each slice has dof=3")
    check(info["pooled_s2"] is not None and 0.0030 ** 2 < info["pooled_s2"] < 0.010 ** 2,
          "pooled noise sigma between 3 mW and 10 mW", "%.2f mW" % (math.sqrt(info["pooled_s2"]) * 1e3))
    leak = combine_leak(slices, info)
    bad = [(k, v["L"], v["se"]) for k, v in leak.items() if abs(v["L"] - leak_true(k[0], k[1])) > 4.0 * v["se"]]
    check(len(leak) == 15 and not bad, "all 15 Pleak(T,V) within 4 SE of truth", "bad=%s" % bad)
    c = combine_ceff(slices, info)
    bad = [(w, v["C"], TRUTH["C"][w], v["se"]) for w, v in c.items() if abs(v["C"] - TRUTH["C"][w]) > 4.0 * v["se"]]
    check(len(c) == 3 and not bad, "C_eff per workload within 4 SE of truth", "bad=%s" % bad)
    ts = [t for t in TEMPS if (t, 0.80) in leak]
    law = fit_temp_law(ts, [leak[(t, 0.80)]["L"] for t in ts], [leak[(t, 0.80)]["se"] for t in ts])
    check(within(law["a"], TRUTH["a"], 4.0 * law["a_se"]), "temperature exponent a within 4 SE", "a=%.5f se=%.5f" % (law["a"], law["a_se"]))
    check(within(law["L25"], TRUTH["L25"], 4.0 * law["L25_se"]), "L25 within 4 SE", "L25=%.4f se=%.4f" % (law["L25"], law["L25_se"]))
    check(law["dropped"] == [], "no dropped temps at seed 11", str(law["dropped"]))
    drop = fit_temp_law([-40, 25, 85], [-0.001, 0.15, 0.9], [0.01, 0.01, 0.02])
    check(drop["dropped"] == [-40], "non-positive leakage estimate dropped (not log'd)", str(drop["dropped"]))


def test_part_b():
    print("--- Part B: 식별 가능성")
    # 점이 부족: f 하나
    rows = synth(plan_full(temps=(25,), freqs=(1.4,)), seed=3)
    slices, info = fit_slices(rows)
    check(not slices and info["underdetermined"] == 9, "single f per slice: underdetermined, no fabricated result", str(info))
    # f 두 점: 풀리지만 dof 0 → 잡음 추정 불가
    rows = synth(plan_full(temps=(25,), freqs=(0.8, 2.0)), seed=3)
    slices, info = fit_slices(rows)
    check(slices and all(s["dof"] == 0 for s in slices.values()) and info["pooled_s2"] is None,
          "two f points: exact fit, dof=0, noise unknown", str(info["pooled_s2"]))
    # V-f가 묶이면 슬라이스별 f가 1개라 슬라이스 회귀 자체가 안 된다
    rows = synth(plan_dvfs(temps=(25,)), seed=3)
    slices, info = fit_slices(rows)
    check(not slices and info["underdetermined"] == 15, "DVFS-locked: every (w,t,v) slice has one f", str(info))
    # 조건수: f 폭, V-f 묶임
    wide = [[1.0, 0.64 * f] for f in FREQS]
    nar = [[1.0, 0.64 * f] for f in (1.90, 1.925, 1.95, 1.975, 2.00)]
    kw, kn = scaled_cond(wide), scaled_cond(nar)
    check(kn > 8.0 * kw, "narrow f range inflates kappa (>8x)", "wide=%.1f narrow=%.1f" % (kw, kn))
    kf = true_kappa(synth(plan_full(temps=(25,)), 1))
    kd = true_kappa(synth(plan_dvfs(temps=(25,)), 1))
    check(kd > 2.0 * kf, "V-f coupling inflates true-model kappa (>2x)", "full=%.1f dvfs=%.1f" % (kf, kd))
    # alpha*C는 곱으로만 관측: (alpha,C)=(0.5,3.2)와 (1.0,1.6)은 데이터가 같다
    tr1 = {"L25": 0.15, "a": 0.025, "b": 4.0, "C": {"cpu": 0.5 * 3.2}}
    tr2 = {"L25": 0.15, "a": 0.025, "b": 4.0, "C": {"cpu": 1.0 * 1.6}}
    p = plan_full(temps=(25,), workloads=("cpu",))
    r1, r2 = synth(p, 5, tr=tr1), synth(p, 5, tr=tr2)
    check(all(a["p"] == b["p"] for a, b in zip(r1, r2)), "alpha and C identifiable only as product (identical data)")
    # 저온 ln 회귀에서 가중치의 효과 (시드 고정 측정)
    werr, uerr = [], []
    for k in range(120):
        ts = [-40, 0, 25, 85, 105]
        rows = synth(plan_full(temps=ts, workloads=("cpu",)), 7000 + k)
        sl, info = fit_slices(rows)
        le = combine_leak(sl, info)
        L = [le[(t, 0.80)]["L"] for t in ts]
        se = [le[(t, 0.80)]["se"] for t in ts]
        if min(L) <= 0:
            continue
        werr.append(abs(fit_temp_law(ts, L, se)["a"] - TRUTH["a"]))
        uerr.append(abs(fit_temp_law_unweighted(ts, L) - TRUTH["a"]))
    mw, mu = statistics.mean(werr), statistics.mean(uerr)
    print("  [measure] mean|a error| weighted=%.5f unweighted=%.5f over %d runs" % (mw, mu, len(werr)))
    check(mw < 0.8 * mu, "weighted ln-fit beats unweighted (<0.8x error)", "weighted=%.5f unweighted=%.5f" % (mw, mu))
    # 시나리오 표
    tbl = scenario_table(n_mc=25)
    print_scenarios(tbl)
    d = {r["name"].split()[0]: r for r in tbl}
    check(d["S0"]["L_rms"] < 5.0 and d["S0"]["C_rms"] < 1.0, "S0 full design recovers L,C", "%.2f%% %.2f%%" % (d["S0"]["L_rms"], d["S0"]["C_rms"]))
    check(d["S2"]["L_rms"] > 4.0 * d["S0"]["L_rms"], "S2 narrow f amplifies leakage error >4x", "%.2f vs %.2f" % (d["S2"]["L_rms"], d["S0"]["L_rms"]))
    check(abs(d["S3"]["L_bias"]) > 15.0, "S3 DVFS-locked pooled: leakage biased >15% (not noise)", "bias %.1f%%" % d["S3"]["L_bias"])
    check(abs(d["S4a"]["L_bias"]) < 5.0 and d["S4a"]["C_bias"] > 2.0,
          "S4a self-heat at 25C: L looks fine, C_eff inflated >2%", "L %.1f%% C %.1f%%" % (d["S4a"]["L_bias"], d["S4a"]["C_bias"]))
    check(abs(d["S4b"]["L_bias"]) > 10.0, "S4b self-heat at 85C: leakage biased >10%", "bias %.1f%%" % d["S4b"]["L_bias"])
    a_sl, a_vp = selfheat_a_bias(10)
    print("  [measure] mean a: slice(chamber T)=%.5f varpro(Tj)=%.5f truth=%.5f" % (a_sl, a_vp, TRUTH["a"]))
    check(abs(a_vp - TRUTH["a"]) < 0.0015, "varpro temperature coefficient within 0.0015 of truth", "%.5f" % a_vp)
    check(abs(a_sl - TRUTH["a"]) > 2.0 * abs(a_vp - TRUTH["a"]), "chamber-labelled a is worse than Tj-based a", "%.5f vs %.5f" % (a_sl, a_vp))
    check(d["S5"]["L_rms"] < 5.0 and d["S5"]["C_rms"] < 1.0, "S5 Tj-covariate varpro removes the bias", "%.2f%% %.2f%%" % (d["S5"]["L_rms"], d["S5"]["C_rms"]))
    expect_bias = (math.exp(-5.0 * TRUTH["a"]) - 1.0) * 100.0
    check(abs(d["S6"]["L_bias"] - expect_bias) < 3.0 and d["S6"]["C_rms"] < 1.0,
          "S6 Tj offset +5C: L25 shifts by exp(-5a)-1 = %.1f%%; C untouched (absorbed, undetectable from P alone)" % expect_bias, "bias %.1f%%" % d["S6"]["L_bias"])


def test_part_c():
    print("--- Part C: 캠페인 (SENSE-1 VDD_CPU, 8 chips)")
    truth = load_truth()
    results, verdicts = run_campaign()
    by = {v["chip"]: v for v in verdicts}
    res = {r["chip"]: r for r in results}
    for v in verdicts:
        print("  " + v["line"])
    expect = {"U1": "PASS", "U2": "PASS", "U3": "LEAKAGE_EXCESS", "U4": "PASS", "U5": "IDLE_CEFF_HIGH",
              "U6": "SENSOR_GAIN", "U7": "BLOCKED", "U8": "PASS"}
    for chip, ex in sorted(expect.items()):
        check(by[chip]["verdict"] == ex, "chip %s verdict %s" % (chip, ex), by[chip]["verdict"])
    out = res["U2"]["outliers"]
    check(len(out) == 1 and out[0][:4] == ("cpu", 85, 0.88, 1.7) and out[0][4] > 0.3,
          "U2 outlier located exactly (cpu,85C,0.88V,1.7GHz)", str(out))
    extra = sum(len(r.get("outliers", [])) for r in results) - len(out)
    check(extra == 0, "no false outliers on the other chips", str(extra))
    # 양호 칩 복원 vs 주입 참값: 허용치는 Part B S5 측정 RMS(L ~1.6%, C ~0.17%)보다 넉넉하게(L 약 5배, C 약 9배), 칩별 잡음 실현값을 흡수
    for chip in ("U1", "U4", "U8"):
        L_true = truth[chip]["L25"]
        c_true = truth[chip]["C"]["cpu"]
        L_est, C_est = res[chip]["L25"][0.80], res[chip]["C"]["cpu"]
        check(_err_pct(L_est, L_true) < 8.0, "chip %s L25@0.80V within 8%% of truth" % chip, "%.4f vs %.4f" % (L_est, L_true))
        check(_err_pct(C_est, c_true) < 1.5, "chip %s C_cpu within 1.5%% of truth" % chip, "%.3f vs %.3f" % (C_est, c_true))
    check(_err_pct(res["U3"]["L25"][0.80], truth["U3"]["L25"]) < 8.0, "U3 leaky L25 recovered", "%.3f vs %.3f" % (res["U3"]["L25"][0.80], truth["U3"]["L25"]))
    check(within(res["U6"]["L25"][0.80] / truth["U6"]["L25"], 0.5, 0.05), "U6 sensor gain ~0.5 visible on L too", "%.3f" % (res["U6"]["L25"][0.80] / truth["U6"]["L25"]))


def run_part(fn):
    try:
        fn()
    except Exception as e:
        check(False, fn.__name__, "exception: %r" % (e,))


def main():
    for fn in (test_part_a, test_part_b, test_part_c):
        run_part(fn)
    if "--report" in sys.argv:
        print("=== scenario table (n=200)")
        print_scenarios(scenario_table(n_mc=200))
    print("%d/%d passed" % (_stats[1], _stats[0]))
    sys.exit(0 if _stats[1] == _stats[0] else 1)


if __name__ == "__main__":
    main()
