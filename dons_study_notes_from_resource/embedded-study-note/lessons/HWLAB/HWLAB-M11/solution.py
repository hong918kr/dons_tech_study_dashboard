#!/usr/bin/env python3
"""HWLAB-M11 BER Bathtub과 Q-scale 외삽

BERT(bit error ratio tester)가 하는 일을 직접 구현한다: PRBS 를 보내고, 수신 샘플링 위상을 UI 전체에 걸쳐
옮기며 위상마다 비트 오류를 센다 → bathtub. 짧게(1e-6 깊이까지만) 잰 bathtub 을 Q-scale 에서 직선으로 펴서
1e-12 eye opening 을 외삽하고, 그 외삽이 언제 맞고 언제 거짓말하는지 주입한 정답(지터 모델)과 비교한다.

  Part A  PRBS7 생성 + 정렬(lock) + 오류 카운트(SIV-M06 checker 축약), 비트 단위 링크 시뮬레이션의 오류 수
          == 에지 지터만 세는 빠른 bathtub (정확히 같은 수), 해석 bathtub BER(x) = ρT·[P(J>x) + P(J>1−x)]
  Part B  1e7 비트(1e-6 깊이) bathtub → Q-scale 가중 직선 피팅 → EO(1e-12) 외삽 + 표준오차,
          dual-Dirac (RJ, DJδδ, TJ) 환산, scope TIE 1e5 에지 dual-Dirac(HWLAB-M10 축약)과 비교,
          정규화 ρ · 피팅 창 · 비가우시안 DJ(정현 PJ)가 외삽을 어떻게 비트는지
  Part C  VALBRD-B 리타이머 U7 lane0 / lane3 BERT 데이터(bert_sweep.csv) 판정 — 외삽은 PASS 인데
          눈 안쪽 '외톨이 오류'와 마스크 위상 장시간 측정이 드문 큰 지터(low-probability DJ)를 드러내는 lane3

  python3 -I solution.py            # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --table    # 측정 시간·신뢰도 trade-off 표만 출력

단위: 위상·지터는 전부 UI (10 Gb/s 에서 1 UI = 100 ps). Python 3.9+, 표준 라이브러리만.
"""
import csv
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SQ2 = math.sqrt(2.0)
RHO_T = 64.0 / 127.0          # PRBS7 전이 밀도: 한 주기 127 비트에 전이(run 경계) 64 개
RHO_DD = RHO_T / 2.0          # dual-Dirac 한쪽 꼬리의 정규화: 전이 밀도 × Dirac 가중치 1/2


# ---------------------------------------------------------------- 주어진 도구
def tail(z):
    """표준 정규 꼬리 P(N(0,1) > z) = erfc(z/√2)/2."""
    return 0.5 * math.erfc(z / SQ2)


def pdf(z):
    return math.exp(-0.5 * z * z) / math.sqrt(2.0 * math.pi)


def prbs7(n, seed=(1, 1, 1, 1, 1, 1, 1)):
    """PRBS7 = x^7 + x^6 + 1 (탭 7·6): b[t] = b[t-7] XOR b[t-6]. 처음 7 비트 = seed (전부 0 이면 안 됨)."""
    b = list(seed)
    while len(b) < n:
        b.append(b[-7] ^ b[-6])
    return b[:n]


PRBS7_PERIOD = prbs7(127)


def jitter_sampler(model, rng):
    """지터 모델 → 에지 하나의 TIE(UI)를 뽑는 함수. 양수 = 늦은 에지.

    model = {"kind": "dd", "dj": DJδδ, "rj": σ}            dual-Dirac ±DJ/2 (각 1/2) + 가우시안
            {"kind": "pj", "a": 진폭, "rj": σ}             정현 PJ a·sin θ (θ 균일 = 아크사인 분포) + 가우시안
            + 선택 "rare": [p, s]                         확률 p 로 ±s (각 1/2) 가 더해지는 드문 큰 지터
    """
    g, r, s = rng.gauss, rng.random, model["rj"]
    if model["kind"] == "dd":
        d = model["dj"] / 2.0

        def base():
            return (d if r() < 0.5 else -d) + g(0.0, s)
    else:
        a, w = model["a"], 2.0 * math.pi

        def base():
            return a * math.sin(w * r()) + g(0.0, s)
    if "rare" not in model:
        return base
    p, sh = model["rare"]

    def draw():
        j = base()
        if r() < p:
            j += sh if r() < 0.5 else -sh
        return j
    return draw


def simulate_link(n_bits, model, phases, seed):
    """비트 단위 링크: PRBS7 TX → 전이마다 지터 → 위상 x(0<x<1, 공칭 에지 기준)에서 샘플한 RX 비트.

    비트 k 구간 = [k + j_k, k+1 + j_{k+1}) (j 는 그 경계에 전이가 있을 때만 존재).
    샘플 시각 k + x 에서: 왼쪽 에지가 x 보다 늦으면 이전 비트, 오른쪽 에지가 x−1 보다 이르면 다음 비트를 본다.
    반환 (tx, {x: rx}, jitters) — jitters 는 전이 순서대로 뽑은 TIE 목록 (빠른 경로와 같은 난수열).
    """
    tx = prbs7(n_bits)
    draw = jitter_sampler(model, random.Random(seed))
    jit = [None] * (n_bits + 1)
    jitters = []
    for k in range(1, n_bits):
        if tx[k] != tx[k - 1]:
            jit[k] = draw()
            jitters.append(jit[k])
    out = {}
    for x in phases:
        rx = []
        for k in range(n_bits):
            v = tx[k]
            jl, jr = jit[k], jit[k + 1]
            if jl is not None and jl > x:
                v = tx[k - 1]
            elif jr is not None and jr < x - 1.0:
                v = tx[k + 1]
            rx.append(v)
        out[x] = rx
    return tx, out, jitters


def edge_jitters(model, n_edges, seed):
    """빠른 경로용: 지터만 스트림으로 (리스트를 만들지 않아 1e7 비트도 메모리 걱정 없음)."""
    draw = jitter_sampler(model, random.Random(seed))
    for _ in range(n_edges):
        yield draw()


def measure_bathtub(model, n_bits, P, seed):
    """BERT bathtub 한 번: 위상 k/P (k=1..P-1) 마다 (x, bits, errors). 전이 수 = round(ρT·n_bits).
    (실제 BERT 는 위상마다 따로 n_bits 를 보낸다. 여기선 지터 레코드 하나를 모든 위상에 재사용한다.)"""
    errs = bathtub_counts(edge_jitters(model, int(round(RHO_T * n_bits)), seed), P)
    return [(k / P, n_bits, errs[k - 1]) for k in range(1, P)]


def ber_analytic(model, x, rho_t=RHO_T):
    """주입한 모델의 정확한 bathtub (무한히 긴 측정과 같다): 왼쪽 에지가 늦거나 오른쪽 에지가 이른 확률."""
    return rho_t * (tail_prob(model, x) + tail_prob(model, 1.0 - x))


def eye_analytic(model, ber, rho_t=RHO_T):
    """BER(x) = ber 가 되는 왼쪽/오른쪽 위상(이분법) → (xL, xR, EO). 눈이 닫혔으면 None."""
    if ber_analytic(model, 0.5, rho_t) >= ber:
        return None

    def root(a, b, left):
        for _ in range(80):
            m = 0.5 * (a + b)
            if (ber_analytic(model, m, rho_t) > ber) == left:
                a = m
            else:
                b = m
        return 0.5 * (a + b)
    xl, xr = root(1e-6, 0.5, True), root(0.5, 1.0 - 1e-6, False)
    return xl, xr, xr - xl


def tie_dual_dirac(tie, ber, P=512, f_max=2e-3, min_cnt=5):
    """HWLAB-M10 축약판 — scope TIE 레코드만으로 dual-Dirac 꼬리 피팅.

    TIE 의 위/아래 꼬리 카운트 #(j > x), #(j < x−1) 은 ρT = 1 인 bathtub 과 똑같은 모양이다.
    그래서 bathtub_counts + qscale_fit(ρ = 1/2: Dirac 하나의 가중치)을 그대로 쓰고, BER 목표는 비트 BER 로
    맞추기 위해 Q 를 ρT/2 정규화로 구한다 (Part B 의 BERT 외삽과 같은 기준)."""
    n = len(tie)
    errs = bathtub_counts(tie, P)
    pts = [(k / P, n, errs[k - 1]) for k in range(1, P)]
    fl = qscale_fit(pts, "L", 0.5, f_max, min_cnt)
    fr = qscale_fit(pts, "R", 0.5, f_max, min_cnt)
    return extrapolate(fl, fr, ber, RHO_DD)


def load_sweep(path):
    """bert_sweep.csv → {lane: {"sweep": [(x, bits, err)], "long": [(x, bits, err)]}}."""
    out = {}
    with open(path, encoding="ascii") as f:
        for row in csv.DictReader(line for line in f if not line.startswith("#")):
            d = out.setdefault(row["lane"], {"sweep": [], "long": []})
            d[row["kind"]].append((float(row["phase_ui"]), int(row["bits"]), int(row["errors"])))
    return out


def load_spec():
    with open(os.path.join(HERE, "spec.json"), encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------- Part A: checker · bathtub 카운트 · 해석 모델
def prbs7_align(rx, nverify=64):
    """수신 비트열을 PRBS7 기준 패턴에 정렬(lock): rx[t] == PRBS7_PERIOD[(t + a) % 127] 가 되는 a.

    rx[w : w+nverify] 창이 오류 없이 맞는 a 를 0..126 에서 찾는다. 창에 오류가 있어 어떤 a 도 안 맞으면
    창을 nverify 만큼 옮겨 다시 시도. 끝까지 못 찾으면 None (= 동기 실패, BERT 의 'no sync')."""
    n = len(rx)
    w = 0
    while w + nverify <= n:
        for a in range(127):
            if all(rx[t] == PRBS7_PERIOD[(t + a) % 127] for t in range(w, w + nverify)):
                return a
        w += nverify
    return None


def prbs7_errors(rx, a):
    """정렬 a 로 잠긴(locked) checker: 기준 = PRBS7_PERIOD[(t+a)%127], 받은 비트와 다른 개수.
    기준을 받은 비트가 아니라 로컬 패턴으로 만들므로 오류 하나는 한 번만 센다 (오류 증식 없음)."""
    return sum(1 for t, b in enumerate(rx) if b != PRBS7_PERIOD[(t + a) % 127])


def bathtub_counts(jitters, P):
    """에지 지터만으로 위상 k/P (k=1..P-1) 의 오류 수 → 길이 P-1 리스트 (인덱스 k-1).

    errors(x) = #(j > x) [왼쪽 에지가 늦음 → 그 비트 오류] + #(j < x − 1) [오른쪽 에지가 이름 → 앞 비트 오류].
    j 를 b = floor(j·P) 칸에 한 번 넣으면 (j > k/P ⟺ b ≥ k), (j < k/P − 1 ⟺ b ≤ k − P − 1) 이므로
    누적합 두 번으로 모든 위상을 한꺼번에 센다 (지터 하나당 O(1), 위상 수와 무관)."""
    hist = [0] * (2 * P + 1)          # b = -P .. P  → 인덱스 b + P
    for j in jitters:
        b = int(math.floor(j * P))
        hist[min(2 * P, max(0, b + P))] += 1
    ge = [0] * (2 * P + 2)            # ge[i] = Σ hist[i:]
    for i in range(2 * P, -1, -1):
        ge[i] = ge[i + 1] + hist[i]
    total = ge[0]
    out = []
    for k in range(1, P):
        late = ge[k + P]                          # b ≥ k        ⟺ 인덱스 ≥ k + P
        early = total - ge[k]                     # b ≤ k − P − 1 ⟺ 인덱스 ≤ k − 1
        out.append(late + early)
    return out


def tail_prob(model, x):
    """주입한 지터 모델의 P(J > x) (해석식, 모델이 0 대칭이므로 P(J < −x) 도 같다).

    dd : ½·Q((x−DJ/2)/σ) + ½·Q((x+DJ/2)/σ)
    pj : (1/M)·Σ_i Q((x − a·sin θ_i)/σ),  θ_i = 2π(i+½)/M, M = 720 (주기 함수 → 중점 규칙이 지수적으로 수렴)
    rare [p, s] : (1−p)·P0(x) + p/2·P0(x−s) + p/2·P0(x+s)   (P0 = rare 없는 모델의 꼬리)
    """
    s = model["rj"]

    def p0(x0):
        if model["kind"] == "dd":
            d = model["dj"] / 2.0
            return 0.5 * tail((x0 - d) / s) + 0.5 * tail((x0 + d) / s)
        a, m = model["a"], 720
        return sum(tail((x0 - a * math.sin(2.0 * math.pi * (i + 0.5) / m)) / s) for i in range(m)) / m
    if "rare" not in model:
        return p0(x)
    p, sh = model["rare"]
    return (1.0 - p) * p0(x) + 0.5 * p * p0(x - sh) + 0.5 * p * p0(x + sh)


# ---------------------------------------------------------------- Part B: Q-scale 피팅 · 외삽
def q_of_ber(ber, rho):
    """Q = √2·erfc⁻¹(2·BER/ρ)  (⟺ BER = ρ·P(N>Q)). erfc 는 단조 감소 → 이분법 (math.erfc 만 사용)."""
    target = ber / rho
    lo, hi = -10.0, 40.0
    for _ in range(200):
        m = 0.5 * (lo + hi)
        if tail(m) > target:
            lo = m
        else:
            hi = m
    return 0.5 * (lo + hi)


def qscale_fit(points, side, rho, ber_max, min_err, center=0.5):
    """한쪽 꼬리를 Q-scale 에서 가중 최소제곱 직선 Q = m·x + c 로 피팅.

    쓰는 점: side 'L' 이면 x < center, 'R' 이면 x > center 이고 errors ≥ min_err, BER ≤ ber_max, BER/ρ < 1/2.
    가중치 w = 1/Var(Q) = n_err·(φ(Q)/Q̄(Q))²  (Poisson: δBER/BER = 1/√n, δQ = δBER/(ρφ(Q))).
    반환 dict(m, c, var_m, var_c, cov, sigma=1/|m|, mu=−c/m, n=점 수, xs=[...]) — 점이 3개 미만이면 None.
    """
    rows = []
    for x, n, e in points:
        if (x < center) != (side == "L") or e < min_err:
            continue
        ber = e / n
        if ber > ber_max or ber / rho >= 0.5:
            continue
        q = q_of_ber(ber, rho)
        rows.append((x, q, e * (pdf(q) / tail(q)) ** 2))
    if len(rows) < 3:
        return None
    S = sum(w for _, _, w in rows)
    Sx = sum(w * x for x, _, w in rows)
    Sy = sum(w * q for _, q, w in rows)
    Sxx = sum(w * x * x for x, _, w in rows)
    Sxy = sum(w * x * q for x, q, w in rows)
    D = S * Sxx - Sx * Sx
    m = (S * Sxy - Sx * Sy) / D
    c = (Sxx * Sy - Sx * Sxy) / D
    return dict(m=m, c=c, var_m=S / D, var_c=Sxx / D, cov=-Sx / D,
                sigma=1.0 / abs(m), mu=-c / m, n=len(rows), xs=[x for x, _, _ in rows])


def extrapolate(fl, fr, ber, rho):
    """두 꼬리 직선을 Q_t = q_of_ber(ber, ρ) 까지 연장 → eye opening 과 dual-Dirac 성분.

    x(Q_t) = (Q_t − c)/m,  Var x = (x²·Var m + Var c + 2x·Cov)/m²  (델타 방법),  EO = xR − xL, se = √(Var xL + Var xR)
    RJ = (σL + σR)/2,  DJδδ = 1 − (μR − μL),  TJ = 1 − EO = DJδδ + Q_t·(σL + σR)
    반환 dict(xl, xr, eo, se, q, rj, djdd, tj) — 피팅이 없으면 None."""
    if fl is None or fr is None:
        return None
    q = q_of_ber(ber, rho)

    def at(f):
        x = (q - f["c"]) / f["m"]
        var = (x * x * f["var_m"] + f["var_c"] + 2.0 * x * f["cov"]) / (f["m"] ** 2)
        return x, var
    xl, vl = at(fl)
    xr, vr = at(fr)
    eo = xr - xl
    return dict(xl=xl, xr=xr, eo=eo, se=math.sqrt(vl + vr), q=q,
                rj=0.5 * (fl["sigma"] + fr["sigma"]), djdd=1.0 - (fr["mu"] - fl["mu"]), tj=1.0 - eo)


# ---------------------------------------------------------------- Part C: 신뢰도 · 외톨이 오류 · 판정
def bits_for_cl(ber, cl):
    """오류 0 개로 'BER < ber' 를 신뢰수준 cl 로 주장하는 데 필요한 비트 수: N = −ln(1−cl)/ber (Poisson)."""
    return -math.log(1.0 - cl) / ber


def find_outliers(points, fl, fr, rho, max_expected=0.01):
    """피팅 직선이 '오류가 거의 없어야 한다'고 말하는 위상(기대 오류 < max_expected)에서 오류가 난 점들.

    기대 오류 = n·ρ·Q̄(Q_side(x)),  Q_side = 그 위상에서 더 가까운 꼬리 직선(L 은 x<0.5, R 은 x>0.5)의 Q.
    반환 [(x, errors, expected)]. 이게 있으면 꼬리가 피팅한 가우시안이 아니다 → 외삽 무효."""
    out = []
    for x, n, e in points:
        if e == 0:
            continue
        f = fl if x < 0.5 else fr
        q = f["m"] * x + f["c"]
        exp_e = n * rho * tail(q)
        if exp_e < max_expected:
            out.append((x, e, exp_e))
    return out


def judge_lane(name, data, spec):
    """lane 하나 판정. 반환 (verdict, 한 줄 리포트, 세부 dict).

    순서: ① 피팅 실패 → 'NO_FIT'  ② 마스크 위상 장시간 측정에서 오류 > 0 → 'FAIL'
          ③ 외톨이 오류 있음 → 'INCONCLUSIVE' (외삽 무효, 1e-12 직접 측정 필요)
          ④ EO − 2·se ≥ eo_min → 'PASS',  아니면 'FAIL'"""
    fs = spec["fit"]
    fl = qscale_fit(data["sweep"], "L", RHO_DD, fs["ber_max"], fs["min_err"])
    fr = qscale_fit(data["sweep"], "R", RHO_DD, fs["ber_max"], fs["min_err"])
    ex = extrapolate(fl, fr, spec["target_ber"], RHO_DD)
    if ex is None:
        return "NO_FIT", "%s NO_FIT (fewer than 3 tail points per side)" % name, {}
    outl = find_outliers(data["sweep"], fl, fr, RHO_DD, spec["outlier_expected_max"])
    lerr = sum(e for _, _, e in data["long"])
    lbits = sum(n for _, n, _ in data["long"])
    ui_ps = 1e3 / spec["line_rate_gbps"]
    head = "%s EO_fit(%.0e)=%.3fUI±%.3f (%.1fps) RJ=%.4f DJdd=%.3f" % (
        name, spec["target_ber"], ex["eo"], ex["se"], ex["eo"] * ui_ps, ex["rj"], ex["djdd"])
    if lerr > 0:
        verdict = "FAIL"
        why = "mask long-run %d err / %.1e bits (BER=%.1e >> %.0e) ; %d outlier phase(s) inside fitted eye" % (
            lerr, lbits, lerr / lbits, spec["target_ber"], len(outl))
    elif outl:
        verdict = "INCONCLUSIVE"
        why = "%d outlier phase(s) -> tail not Gaussian, extrapolation invalid; need %.1e bits/point" % (
            len(outl), bits_for_cl(spec["target_ber"], 0.95))
    elif ex["eo"] - 2.0 * ex["se"] >= spec["eo_min_ui"]:
        verdict = "PASS"
        why = "EO-2se=%.3f >= %.2fUI, no outliers, mask long-run 0 err / %.1e bits" % (
            ex["eo"] - 2.0 * ex["se"], spec["eo_min_ui"], lbits)
    else:
        verdict = "FAIL"
        why = "EO-2se=%.3f < %.2fUI" % (ex["eo"] - 2.0 * ex["se"], spec["eo_min_ui"])
    return verdict, "%s %s | %s" % (verdict, head, why), dict(ex=ex, fl=fl, fr=fr, outliers=outl,
                                                               long_err=lerr, long_bits=lbits)


def tradeoff_table(rates_gbps=(10.0, 25.0), cl=0.95):
    """측정 깊이·신뢰수준·시간 표 (문자열)."""
    lines = ["%-34s %10s %12s %12s" % ("method / depth (95% CL, 0 err)", "bits/pt", "t@%.0fG" % rates_gbps[0],
                                         "t@%.0fG" % rates_gbps[1])]

    def fmt_t(s):
        if s < 1e-3:
            return "%.1fus" % (s * 1e6)
        if s < 1:
            return "%.1fms" % (s * 1e3)
        if s < 3600:
            return "%.1fs" % s
        if s < 86400 * 2:
            return "%.1fh" % (s / 3600)
        return "%.1fd" % (s / 86400)
    for label, bits in (("Q-scale sweep, 1e-6 depth (~10 err)", 1e7),
                        ("direct, BER<1e-9 claim", bits_for_cl(1e-9, cl)),
                        ("direct, BER<1e-12 claim", bits_for_cl(1e-12, cl)),
                        ("direct, BER<1e-15 claim", bits_for_cl(1e-15, cl))):
        lines.append("%-34s %10.2e %12s %12s" % (label, bits, fmt_t(bits / (rates_gbps[0] * 1e9)),
                                                 fmt_t(bits / (rates_gbps[1] * 1e9))))
    b64 = 64 * 1e7
    lines.append("%-34s %10.2e %12s %12s" % ("  full bathtub 64 pts @1e-6", b64, fmt_t(b64 / (rates_gbps[0] * 1e9)),
                                             fmt_t(b64 / (rates_gbps[1] * 1e9))))
    b64d = 64 * bits_for_cl(1e-12, cl)
    lines.append("%-34s %10.2e %12s %12s" % ("  full bathtub 64 pts @1e-12", b64d, fmt_t(b64d / (rates_gbps[0] * 1e9)),
                                             fmt_t(b64d / (rates_gbps[1] * 1e9))))
    return "\n".join(lines)


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() → bool 또는 (bool, 설명). 예외도 FAIL (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn()
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def safe(fn, default=None):
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


DD = {"kind": "dd", "dj": 0.20, "rj": 0.020}       # Part A/B 기준 모델: DJδδ 0.20 UI, RJ 0.020 UI (2 ps @10G)
PJ = {"kind": "pj", "a": 0.10, "rj": 0.020}        # 정현 PJ 0.20 UIpp + 같은 RJ — dual-Dirac 이 아닌 DJ


def run_tests():
    P = 256
    # ---- Part A
    print("-- Part A: PRBS7 checker / bit-level == edge count / analytic bathtub --")
    seq = prbs7(254)
    check("PRBS7 period 127, 64 ones, 64 transitions per period",
          lambda: (seq[:127] == seq[127:] and sum(seq[:127]) == 64
                   and sum(seq[i] != seq[(i + 1) % 127] for i in range(127)) == 64))
    rx = prbs7(500 + 31)[31:]                      # 31 비트 늦게 시작한 수신 → 정렬 a = 31
    check("prbs7_align finds the offset (rx started 31 bits late -> a=31)", lambda: (prbs7_align(rx) == 31,
                                                                                  "a=%s" % prbs7_align(rx)))
    bad = list(rx)
    for t in (3, 100, 101, 400):                   # 정렬 창(0..63) 안 오류 1개 포함 4개 주입
        bad[t] ^= 1
    a_bad = safe(lambda: prbs7_align(bad))
    check("align skips an errored window; locked checker counts 4 injected errors exactly once",
          lambda: (a_bad == 31 and prbs7_errors(bad, 31) == 4, "a=%s errors=%s" % (a_bad, safe(lambda: prbs7_errors(bad, 31)))))

    nA, phases = 1 << 14, [k / 32 for k in (1, 2, 3, 4, 5, 6, 16, 26, 27, 28, 29, 30, 31)]
    tx, rxs, jit = simulate_link(nA, DD, phases, seed=11)
    lock = safe(lambda: prbs7_align(rxs[0.5]))
    chk = safe(lambda: [prbs7_errors(rxs[x], lock) for x in phases], [])
    fast = safe(lambda: bathtub_counts(jit, 32), [])
    fast_sel = [fast[round(x * 32) - 1] for x in phases] if fast else []
    print("      bit-level %d bits, %d edges (rho_T=%.4f), lock a=%s" % (nA, len(jit), len(jit) / nA, lock))
    print("      phase : " + " ".join("%6.4f" % x for x in phases))
    print("      checker: " + " ".join("%6s" % e for e in chk))
    print("      edges  : " + " ".join("%6s" % e for e in fast_sel))
    check("checker errors == edge-jitter count at every phase (exact)",
          lambda: (len(chk) == len(phases) and chk == fast_sel and sum(chk) > 0, "sum=%d" % sum(chk)))
    check("eye center (x=0.5) error-free, x=1/32 near rho_T/2 + early-tail",
          lambda: (chk[phases.index(0.5)] == 0 and 0.2 < chk[0] / nA < 0.3, "BER(1/32)=%.4f" % (chk[0] / nA)))

    check("tail_prob(dd) symmetric pieces: P(J>DJ/2) = 1/4 + tiny",
          lambda: (abs(tail_prob(DD, 0.10) - (0.25 + 0.5 * tail(10.0))) < 1e-12, "%.6f" % tail_prob(DD, 0.10)))
    check("tail_prob(pj) with rj->tiny == arcsine CDF: P(a sin th > a/2) = 1/3",
          lambda: (abs(tail_prob({"kind": "pj", "a": 0.1, "rj": 1e-6}, 0.05) - 1.0 / 3.0) < 2e-3,
                   "%.5f" % tail_prob({"kind": "pj", "a": 0.1, "rj": 1e-6}, 0.05)))
    rare = dict(DD, rare=[1e-3, 0.25])
    check("tail_prob(rare) mixture: (1-p)P0(x)+p/2 P0(x-s)+p/2 P0(x+s)",
          lambda: abs(tail_prob(rare, 0.3) - (0.999 * tail_prob(DD, 0.3) + 0.0005 * tail_prob(DD, 0.05)
                                              + 0.0005 * tail_prob(DD, 0.55))) < 1e-15)

    nB = 10_000_000
    ptsDD = safe(lambda: measure_bathtub(DD, nB, P, seed=2026), [])

    def mc_vs_analytic():
        worst, used = 0.0, 0
        for x, n, e in ptsDD:
            if e >= 100:
                lam = n * ber_analytic(DD, x)
                z = (e - lam) / math.sqrt(lam)
                worst, used = max(worst, abs(z)), used + 1
        return worst < 4.5 and used > 30, "%d phases with >=100 err, worst |z|=%.2f (Poisson)" % (used, worst)
    check("1e7-bit bathtub vs analytic: every phase with >=100 errors within 4.5 sigma", mc_vs_analytic)

    # ---- Part B
    print("-- Part B: Q-scale fit to 1e-6, extrapolate to 1e-12, model limits --")
    truth = safe(lambda: eye_analytic(DD, 1e-12))
    fs = dict(ber_max=1e-3, min_err=5)
    fl = safe(lambda: qscale_fit(ptsDD, "L", RHO_DD, **fs))
    fr = safe(lambda: qscale_fit(ptsDD, "R", RHO_DD, **fs))
    ex = safe(lambda: extrapolate(fl, fr, 1e-12, RHO_DD))
    if ex and truth:
        print("      DD truth: EO(1e-12)=%.4f UI | fit: EO=%.4f +-%.4f (1se), RJ=%.4f DJdd=%.4f, pts L/R=%d/%d, depth=%.1e"
              % (truth[2], ex["eo"], ex["se"], ex["rj"], ex["djdd"], fl["n"], fr["n"], fs["min_err"] / nB))
    check("q_of_ber: Q(1e-12, rho=1)=7.0345, Q(1e-12, rho_T/2)=6.8397",
          lambda: (abs(q_of_ber(1e-12, 1.0) - 7.0345) < 1e-3 and abs(q_of_ber(1e-12, RHO_DD) - 6.8397) < 1e-3,
                   "%.4f / %.4f" % (q_of_ber(1e-12, 1.0), q_of_ber(1e-12, RHO_DD))))
    check("fit only uses tail points (BER<=1e-3, err>=5): >=6 per side, all x in tail region",
          lambda: (fl["n"] >= 6 and fr["n"] >= 6 and max(fl["xs"]) < 0.3 and min(fr["xs"]) > 0.7,
                   "L x=%.3f..%.3f R x=%.3f..%.3f" % (min(fl["xs"]), max(fl["xs"]), min(fr["xs"]), max(fr["xs"]))))
    check("fit recovers injected RJ 0.020 (+-10%) and Dirac positions 0.10 / 0.90 (+-0.005)",
          lambda: (abs(ex["rj"] - 0.02) < 0.002 and abs(fl["mu"] - 0.10) < 0.005 and abs(fr["mu"] - 0.90) < 0.005,
                   "RJ=%.4f muL=%.4f muR=%.4f" % (ex["rj"], fl["mu"], fr["mu"])))
    check("EO(1e-12) extrapolated from 1e-6 depth within 3 se of analytic truth",
          lambda: (abs(ex["eo"] - truth[2]) < 3 * ex["se"], "err=%+.4f UI, 3se=%.4f" % (ex["eo"] - truth[2], 3 * ex["se"])))
    check("dual-Dirac TJ = DJdd + 2*Q*RJ (rho_T/2 Q) equals 1 - EO",
          lambda: abs(ex["djdd"] + 2 * ex["q"] * ex["rj"] - ex["tj"]) < 0.002)

    exr1 = safe(lambda: extrapolate(qscale_fit(ptsDD, "L", 1.0, **fs), qscale_fit(ptsDD, "R", 1.0, **fs), 1e-12, 1.0))
    if exr1 and ex:
        print("      rho=1 (no rho_T, no Dirac 1/2): RJ=%.4f DJdd=%.4f EO=%.4f  vs rho_T/2: RJ=%.4f DJdd=%.4f EO=%.4f"
              % (exr1["rj"], exr1["djdd"], exr1["eo"], ex["rj"], ex["djdd"], ex["eo"]))
    check("wrong normalization rho=1: RJ/DJ split shifts (DJdd off by >0.02 UI) but EO moves < 0.01 UI",
          lambda: (abs(exr1["djdd"] - 0.20) > 0.02 and abs(exr1["eo"] - ex["eo"]) < 0.01,
                   "DJdd=%.4f (true 0.20), dEO=%+.4f" % (exr1["djdd"], exr1["eo"] - ex["eo"])))

    tie = list(edge_jitters(DD, 100_000, seed=77))
    tdd = safe(lambda: tie_dual_dirac(tie, 1e-12))
    if tdd:
        print("      M10-style TIE (1e5 edges): RJ=%.4f DJdd=%.4f EO=%.4f +-%.4f" % (tdd["rj"], tdd["djdd"], tdd["eo"], tdd["se"]))
    check("TIE dual-Dirac (1e5 edges, CDF depth ~5e-5): EO within 3 se of truth, se larger than BERT fit",
          lambda: (abs(tdd["eo"] - truth[2]) < 3 * tdd["se"] and tdd["se"] > 1.5 * ex["se"],
                   "err=%+.4f se_tie=%.4f se_bert=%.4f" % (tdd["eo"] - truth[2], tdd["se"], ex["se"])))

    ptsPJ = safe(lambda: measure_bathtub(PJ, nB, P, seed=2027), [])
    tPJ = safe(lambda: eye_analytic(PJ, 1e-12))

    def pj_fit(bmax):
        return extrapolate(qscale_fit(ptsPJ, "L", RHO_DD, bmax, 5), qscale_fit(ptsPJ, "R", RHO_DD, bmax, 5), 1e-12, RHO_DD)
    pjn, pjw = safe(lambda: pj_fit(1e-3)), safe(lambda: pj_fit(0.1))
    if pjn and pjw and tPJ:
        print("      PJ truth EO(1e-12)=%.4f | window 1e-3: EO=%.4f DJdd=%.4f RJ=%.4f | window 0.1: EO=%.4f DJdd=%.4f RJ=%.4f"
              % (tPJ[2], pjn["eo"], pjn["djdd"], pjn["rj"], pjw["eo"], pjw["djdd"], pjw["rj"]))
    check("sinusoidal PJ 0.20 UIpp: dual-Dirac DJdd < 0.9 * DJpp (dual-Dirac underestimates DJpp)",
          lambda: (pjn["djdd"] < 0.18, "DJdd=%.4f vs PJpp=0.20" % pjn["djdd"]))
    check("PJ: tail-window fit EO within 0.02 UI of truth; window up to 0.1 (DJ region) is >2x worse",
          lambda: (abs(pjn["eo"] - tPJ[2]) < 0.02 and abs(pjw["eo"] - tPJ[2]) > 2 * abs(pjn["eo"] - tPJ[2]),
                   "err tail=%+.4f wide=%+.4f" % (pjn["eo"] - tPJ[2], pjw["eo"] - tPJ[2])))

    # ---- Part C
    print("-- Part C: VALBRD-B U7 retimer lanes, BERT sweep to 1e-6 + mask long-run --")
    spec = load_spec()
    data = load_sweep(os.path.join(HERE, "bert_sweep.csv"))
    check("bert_sweep.csv: 2 lanes x 255 phases, 1e7 bits each, + 2 mask points x 2.5e7 bits",
          lambda: all(len(data[ln]["sweep"]) == 255 and all(n == 10_000_000 for _, n, _ in data[ln]["sweep"])
                      and len(data[ln]["long"]) == 2 for ln in ("L0", "L3")))
    check("bits_for_cl: 0 errors, 95% CL, 1e-12 -> 3.0e12 bits (300 s @ 10 Gb/s)",
          lambda: (abs(bits_for_cl(1e-12, 0.95) - 2.9957e12) < 1e9, "%.4e" % bits_for_cl(1e-12, 0.95)))
    res = {}
    for ln in ("L0", "L3"):
        res[ln] = safe(lambda: judge_lane("lane=%s(%s)" % (ln, spec["lanes"][ln]["path"]), data[ln], spec))
        if res[ln]:
            print("      " + res[ln][1])
    truth_eo = safe(lambda: {ln: eye_analytic(spec["lanes"][ln]["injected"], spec["target_ber"]) for ln in ("L0", "L3")})
    if truth_eo:
        print("      analytic truth EO(1e-12): L0=%.4f L3=%.4f (spec >= %.2f)"
              % (truth_eo["L0"][2], truth_eo["L3"][2], spec["eo_min_ui"]))
    check("L0 -> PASS, extrapolated EO within 3 se of analytic truth",
          lambda: (res["L0"][0] == "PASS" and abs(res["L0"][2]["ex"]["eo"] - truth_eo["L0"][2]) < 3 * res["L0"][2]["ex"]["se"],
                   "fit %.4f truth %.4f" % (res["L0"][2]["ex"]["eo"], truth_eo["L0"][2])))
    check("L3: extrapolation alone would PASS (EO_fit - 2se >= spec) but truth EO < spec (planted rare DJ)",
          lambda: (res["L3"][2]["ex"]["eo"] - 2 * res["L3"][2]["ex"]["se"] >= spec["eo_min_ui"] > truth_eo["L3"][2],
                   "fit %.4f truth %.4f" % (res["L3"][2]["ex"]["eo"], truth_eo["L3"][2])))
    check("L3: outlier errors inside fitted eye detected, L0 has none",
          lambda: (len(res["L3"][2]["outliers"]) > 0 and not res["L0"][2]["outliers"],
                   "L3 outliers at x=%s" % ",".join("%.3f" % x for x, _, _ in res["L3"][2]["outliers"][:6])))
    check("L3 -> FAIL from mask long-run errors (BER at mask >> 1e-12)",
          lambda: (res["L3"][0] == "FAIL" and res["L3"][2]["long_err"] > 0, "long_err=%d" % res["L3"][2]["long_err"]))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 2 and argv[1] == "--table":
        print(tradeoff_table())
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
