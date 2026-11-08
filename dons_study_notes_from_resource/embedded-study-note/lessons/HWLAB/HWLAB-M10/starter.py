#!/usr/bin/env python3
"""HWLAB-M10 지터 분해: RJ/DJ(DDJ·PJ)와 Dual-Dirac TJ@BER

스코프 지터 분석 패키지(Keysight EZJIT+, Tek DPOJET 류)가 TIE 를 성분으로 쪼개는 과정을 직접 구현한다.
  Part A  M09식 캡처(PRBS7 NRZ + RC 채널 ISI + 주입 RJ/PJ, gen_capture.py) → 에지 → TIE
          → 패턴 위치별 평균 = DDJ → 잔차 FFT 피크 + 사인 최소제곱 = PJ → 남은 RMS = RJ
  Part B  히스토그램 꼬리 Q-scale 피팅 → RJδδ·DJδδ → TJ(1e-12) = DJδδ + 2·7.034·RJδδ.
          순진한 방법(std = RJ, pk-pk = TJ, 직사각 FFT 피크, DDJ 제거 전 FFT)이 왜 틀리는지 정답과 비교
  Part C  VALBRD-B 3 레인 판정 리포트 — 어떤 성분이 TJ 를 깎는지로 '무엇을 고칠지' 를 말한다

  python3 -I solution.py            # "N/M passed", 실패 있으면 exit 1
Python 3.9+, 표준 라이브러리만 (math, cmath, json, os, sys, time) + 같은 디렉터리의 gen_capture.py.
"""
import cmath
import json
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import gen_capture as G             # noqa: E402  (캡처 생성기 + 정답 성분 + 컨볼루션 TJ)

PLEN = G.PLEN                       # PRBS7 패턴 길이 127
NFFT = 16384                        # TIE 스펙트럼 길이 (UI 단위 균일 수열, 2^14)


# ---------------------------------------------------------------- 주어짐: 기본 도구
def load_setup():
    with open(os.path.join(HERE, "valbrd_b_serdes.json"), encoding="utf-8") as f:
        return json.load(f)


def find_edges(dt, v, thr=0.0):
    """샘플 파형 → [(교차 시각 ps, +1/-1)]. 이웃 두 샘플 사이 선형 보간 (HWLAB-B04 의 cross_time 과 같다)."""
    out = []
    for i in range(len(v) - 1):
        a, b = v[i] - thr, v[i + 1] - thr
        if (a < 0.0) != (b < 0.0):
            out.append(((i + a / (a - b)) * dt, 1 if b >= 0.0 else -1))
    return out


def fft(x):
    """반복형 radix-2 FFT (len(x) = 2^k). X[k] = Σ x[n]·e^(-j2πkn/N)."""
    n = len(x)
    a = [complex(v) for v in x]
    j = 0
    for i in range(1, n):                          # 비트 역순 재배치
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        w_step = cmath.exp(-2j * math.pi / size)
        half = size // 2
        for start in range(0, n, size):
            w = 1 + 0j
            for k in range(start, start + half):
                t = w * a[k + half]
                a[k + half] = a[k] - t
                a[k] = a[k] + t
                w *= w_step
        size *= 2
    return a


def fill_uniform(n, r, length):
    """에지가 있는 UI(n)에서만 정의된 r 을 UI 마다 균일한 수열로: 사이 UI 는 선형 보간 (스코프 TIE 스펙트럼과 같은 방식)."""
    out, j, n0 = [], 0, n[0]
    for u in range(n0, n0 + length):
        while j + 1 < len(n) and n[j + 1] <= u:
            j += 1
        if n[j] == u or j + 1 >= len(n):
            out.append(r[j])
        else:
            out.append(r[j] + (r[j + 1] - r[j]) * (u - n[j]) / (n[j + 1] - n[j]))
    return out


def ls_sine(n, r, fc):
    """r[k] ≈ a·cos(2π·fc·n[k]) + b·sin(2π·fc·n[k]) 의 최소제곱 (a, b). fc 단위 = cycles/UI. 2×2 정규방정식."""
    w = 2 * math.pi * fc
    scc = sss = scs = rc = rs = 0.0
    for k, x in zip(n, r):
        c, s = math.cos(w * k), math.sin(w * k)
        scc += c * c
        sss += s * s
        scs += c * s
        rc += x * c
        rs += x * s
    det = scc * sss - scs * scs
    return (rc * sss - rs * scs) / det, (rs * scc - rc * scs) / det


def golden_max(fn, lo, hi, iters=40):
    """[lo, hi] 에서 단봉 함수 fn 의 최대점 (황금분할 탐색)."""
    g = (math.sqrt(5.0) - 1.0) / 2.0
    c1, c2 = hi - g * (hi - lo), lo + g * (hi - lo)
    f1, f2 = fn(c1), fn(c2)
    for _ in range(iters):
        if f1 > f2:
            hi, c2, f2 = c2, c1, f1
            c1 = hi - g * (hi - lo)
            f1 = fn(c1)
        else:
            lo, c1, f1 = c1, c2, f2
            c2 = lo + g * (hi - lo)
            f2 = fn(c2)
    return 0.5 * (lo + hi)


def phi(x):
    """표준정규 CDF Φ(x) = erfc(-x/√2)/2 (math.erfc 는 꼬리에서도 상대 정밀도 유지)."""
    return 0.5 * math.erfc(-x / math.sqrt(2.0))


# ---------------------------------------------------------------- Part A: TIE → DDJ → PJ → RJ
def tie_from_edges(edges, ui_nom):
    """에지 시각 → (n, tie, T). n[k] = 첫 에지 기준 UI 번호, 이상 클럭 t = a + T·n 을 최소제곱으로 맞춰
    tie[k] = t[k] - (a + T·n[k]). (상수 주파수 이상 클럭 = '선형 피팅 CDR'. golden PLL 은 연습 2)"""
    # TODO ① n[k] = round((t[k] - t[0]) / ui_nom)   (지터 << UI/2 라 반올림이 비트 경계를 맞힌다)
    # TODO ② t = a + T·n 최소제곱 (Σn, Σt, Σn², Σnt) → T, a
    # TODO ③ tie[k] = t[k] - (a + T·n[k])
    raise NotImplementedError("TODO: tie_from_edges")


def ddj_by_pattern(n, tie, plen=PLEN):
    """패턴 위치 p = n mod plen 별 TIE 평균 = DDJ[p] (전이 없는 위치는 None, 전이 위치 평균 0 으로 센터링).
    반환 dict(ddj, count, resid, pp): resid[k] = tie[k] - DDJ[p_k]."""
    # TODO ① 위치별 합·개수 → 평균
    # TODO ② 전이 있는 위치들의 평균을 빼서 센터링 (TIE 의 공통 오프셋은 DDJ 가 아니다)
    # TODO ③ resid = tie - DDJ[p], pp = max(DDJ) - min(DDJ)
    raise NotImplementedError("TODO: ddj_by_pattern")


def find_pj(n, r, ui_ps, nfft=NFFT, snr_min=20.0):
    """잔차 r(에지 UI n 에서만 정의)의 PJ: 균일 수열 → Hann 창 FFT 피크 → ±1 빈에서 사인 LS 진폭 최대인 주파수.
    반환 dict(f_mhz, amp, a, b, fc, snr, peak_bin, naive_amp, naive_f_mhz). snr < snr_min 이면 PJ 없음(amp 0)."""
    # TODO ① x = fill_uniform(n, r, nfft) - 평균
    # TODO ② Hann 창(w = 0.5 - 0.5cos(2πi/N)) FFT 크기 |X| (1..N/2-1) → 피크 빈 k, snr = |X_k| / median|X|
    # TODO ③ 비교용 naive: 창 없는(직사각) FFT 피크로 진폭 2|X_k|/N
    # TODO ④ golden_max(LS 진폭(fc), (k-1)/N, (k+1)/N) → fc, ls_sine → a, b, amp = √(a²+b²)
    raise NotImplementedError("TODO: find_pj")


def rj_rms(n, resid, pj, count, plen=PLEN):
    """PJ 를 뺀 나머지의 RMS = RJ. 위치별 평균에 자기 자신이 들어가 분산이 (1 - 1/R) 배로 줄어든 것을 보정."""
    # TODO ① e[k] = resid[k] - (a·cos(2π·fc·n) + b·sin(2π·fc·n))
    # TODO ② rms(e) · √(R/(R-1)), R = 에지가 속한 위치의 평균 반복 수
    raise NotImplementedError("TODO: rj_rms")


def decompose(cap):
    """캡처 dict(dt_ps, v, ui_ps) → 성분 분해 결과 dict (주어짐: 위 TODO 함수들을 순서대로 부른다)."""
    edges = find_edges(cap["dt_ps"], cap["v"])
    n, tie, T = tie_from_edges(edges, cap["ui_ps"])
    dd = ddj_by_pattern(n, tie)
    pj = find_pj(n, dd["resid"], T)
    rj = rj_rms(n, dd["resid"], pj, dd["count"])
    return dict(edges=edges, n=n, tie=tie, T=T, ddj=dd["ddj"], ddj_pp=dd["pp"], count=dd["count"],
                resid=dd["resid"], pj=pj, rj=rj)


# ---------------------------------------------------------------- Part B: Q-scale 꼬리 피팅 · dual-Dirac
def inv_phi(p):
    """Φ⁻¹(p), 0 < p < 1 — Φ 가 단조 증가이므로 [-40, 40] 이분법 100 회 (정밀도 ~1e-28 폭)."""
    # TODO: phi(m) < p 면 lo = m, 아니면 hi = m
    raise NotImplementedError("TODO: inv_phi")


def tail_fit(tie, p_lo, p_hi, per_dec=40):
    """TIE 히스토그램 양쪽 꼬리를 Q-scale 에서 직선으로: x = μ + σ·Q.
    정렬값 s[i] 의 경험 CDF F = (i + 0.5)/N. dual-Dirac 가중치 1/2 → 왼쪽 Q = Φ⁻¹(2F), 오른쪽 Q = -Φ⁻¹(2F)
    (오른쪽은 s[N-1-i]). 2F ∈ [p_lo, p_hi] 를 로그 간격(per_dec 점/decade)으로 뽑아 각 꼬리 LS.
    반환 dict(muL, sL, muR, sR, nL, nR)."""
    # TODO ① s = sorted(tie); 로그 간격 확률 → 인덱스 i = round(p/2·N - 0.5) (중복 제거, 범위 확인)
    # TODO ② 왼쪽 점 (Φ⁻¹(2F), s[i]), 오른쪽 점 (-Φ⁻¹(2F), s[N-1-i])
    # TODO ③ 각 꼬리 x = μ + σ·Q 직선 최소제곱 → (μ, σ)
    raise NotImplementedError("TODO: tail_fit")


def dual_dirac(fit, ber=1e-12):
    """dual-Dirac: RJδδ = (σL + σR)/2, DJδδ = μR - μL, Q = Φ⁻¹(1 - BER), TJ = DJδδ + 2·Q·RJδδ."""
    # TODO: Q 는 inv_phi(ber) 의 부호를 바꾼 것 (Φ⁻¹(1-BER) 를 직접 쓰면 1-1e-12 가 반올림된다)
    raise NotImplementedError("TODO: dual_dirac")


# ---------------------------------------------------------------- Part C: 레인 판정
FIT_CAPTURE = (5e-4, 2e-2)           # 캡처(에지 ~8.3k)용 꼬리 피팅 창 (2F 범위)
FIT_LONG = (1e-5, 1e-3)              # 긴 TIE 기록(에지 ~256k)용


def judge_lane(name, dec, dd, spec, ui_ps):
    """분해 결과 dec + dual-Dirac dd → (verdict, dominant, 한 줄 리포트).
    판정: TJ ≤ tj_ui_max·UI, DJδδ ≤ dj_dd_ui_max·UI, RJδδ ≤ rj_ps_max.
    dominant: 스펙트럼 성분 DDJ pk-pk / PJ pk-pk(2A) / RJ 항(2·Q·RJ) 중 가장 큰 것 → 조치 문구."""
    # TODO ① 세 항목 판정 → 하나라도 넘으면 FAIL
    # TODO ② 성분 기여 비교 → dominant ∈ {"DDJ", "PJ", "RJ"} 와 조치 문구
    # TODO ③ "PASS/FAIL lane=... TJ(1e-12)=...UI(...ps) ..." 한 줄
    raise NotImplementedError("TODO: judge_lane")


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
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def truth_ddj_per_edge(dec, cap):
    """각 검출 에지의 정답 DDJ (정답 위치 = 교차 시각을 c0 기준 UI 로 반올림)."""
    tr = cap["truth"]
    return [tr["ddj_ps"][int(round(t / cap["ui_ps"] - tr["c0_ui"])) % PLEN] for t, _ in dec["edges"]]


def ddj_err_rms(dec, cap):
    est = [dec["ddj"][k % PLEN] for k in dec["n"]]
    tru = truth_ddj_per_edge(dec, cap)
    d = [a - b for a, b in zip(est, tru)]
    m = sum(d) / len(d)
    return math.sqrt(sum((x - m) ** 2 for x in d) / len(d))


def run_tests():
    t_start = time.time()
    setup = load_setup()
    spec, lanes, cfg_a = setup["spec"], setup["lanes"], setup["part_a"]
    ber = spec["ber"]

    # ---- Part A
    print("-- Part A: capture -> TIE -> DDJ (pattern avg) -> PJ (FFT + LS) -> RJ (residual rms) --")
    cap = G.make_capture(cfg_a)
    tr = cap["truth"]
    n_trans = sum(1 for p in range(PLEN) if tr["bits"][p] != tr["bits"][p - 1])
    print("      capture %s: %d bits x %d samples/UI = %d samples, UI=%.0fps, PRBS7 transitions/pattern=%d"
          % (cfg_a["name"], cfg_a["n_bits"], cfg_a["spp"], len(cap["v"]), cap["ui_ps"], n_trans))
    edges = find_edges(cap["dt_ps"], cap["v"])
    check("find_edges: one crossing per data transition (8320 = 64/127 x 16510 bits)",
          lambda: (len(edges) == 8320, "edges=%d" % len(edges)))
    dec = safe(lambda: decompose(cap), {})
    check("TIE: fitted UI within 0.001ps of 100ps, mean TIE ~ 0",
          lambda: (near(dec["T"], 100.0, 1e-3) and abs(sum(dec["tie"]) / len(dec["tie"])) < 1e-6,
                   "T=%.6fps" % dec["T"]))
    tru_vals = [x for x in tr["ddj_ps"] if x is not None]
    tru_pp = max(tru_vals) - min(tru_vals)
    check("DDJ per pattern position: rms error vs injected-channel truth < 0.30ps (RJ/sqrt(130)=0.13)",
          lambda: (ddj_err_rms(dec, cap) < 0.30, "rms err=%.3fps" % ddj_err_rms(dec, cap)))
    check("DDJ pk-pk within 0.6ps of truth (max/min of noisy means biases it up)",
          lambda: (near(dec["ddj_pp"], tru_pp, 0.6), "est=%.3fps truth=%.3fps" % (dec["ddj_pp"], tru_pp)))
    check("PJ frequency within 0.05MHz of injected (FFT bin = %.3fMHz)" % (1e4 / NFFT),
          lambda: (near(dec["pj"]["f_mhz"], tr["pj_mhz"], 0.05),
                   "est=%.3fMHz truth=%.3fMHz" % (dec["pj"]["f_mhz"], tr["pj_mhz"])))
    check("PJ amplitude within 0.2ps of injected",
          lambda: (near(dec["pj"]["amp"], tr["pj_ps"], 0.2),
                   "est=%.3fps truth=%.3fps (snr=%.0f)" % (dec["pj"]["amp"], tr["pj_ps"], dec["pj"]["snr"])))
    check("RJ (residual rms) within 0.08ps of injected 1.5ps (+1mV noise -> +0.11ps in quadrature)",
          lambda: (near(dec["rj"], tr["rj_ps"], 0.08), "est=%.4fps truth=%.2fps" % (dec["rj"], tr["rj_ps"])))

    # ---- Part B
    print("-- Part B: Q-scale tail fit, dual-Dirac TJ, and why the naive numbers lie --")
    check("inv_phi: Q(1e-12) = -inv_phi(1e-12) = 7.0345 +-0.0005, inv_phi(0.5) = 0",
          lambda: (near(-inv_phi(1e-12), 7.0345, 5e-4) and near(inv_phi(0.5), 0.0, 1e-9),
                   "Q=%.5f" % -inv_phi(1e-12)))
    tj_true = G.tj_convolution(tru_vals, tr["pj_ps"], tr["rj_ps"], ber)
    dj_pp_true = tru_pp + 2 * tr["pj_ps"]
    print("      truth (injected components, convolution): TJ(1e-12)=%.2fps  DJpp=%.2fps (DDJ %.2f + PJ 2x%.1f)"
          "  RJ=%.2fps" % (tj_true, dj_pp_true, tru_pp, tr["pj_ps"], tr["rj_ps"]))
    tie = dec.get("tie", [0.0])
    std_tie = math.sqrt(sum(x * x for x in tie) / len(tie))
    pkpk = max(tie) - min(tie)
    check("naive 'RJ = std(TIE)' is >3x the true RJ (DJ is not Gaussian noise)",
          lambda: (std_tie > 3 * tr["rj_ps"], "std(TIE)=%.2fps vs RJ %.2fps" % (std_tie, tr["rj_ps"])))
    check("naive 'TJ = observed pk-pk' (8320 edges) under-reports TJ(1e-12) by >20%",
          lambda: (len(tie) > 1 and pkpk < 0.8 * tj_true, "pk-pk=%.2fps vs TJ=%.2fps (%+.0f%%)" % (pkpk, tj_true, (pkpk / tj_true - 1) * 100)))
    check("PJ by rectangular-window FFT peak: scalloping loses >25%% (f is %.2f bins)"
          % (tr["pj_mhz"] / (1e4 / NFFT)),
          lambda: (dec["pj"]["naive_amp"] < 0.75 * tr["pj_ps"],
                   "naive=%.2fps, refined LS=%.3fps, truth=%.1fps" % (dec["pj"]["naive_amp"], dec["pj"]["amp"], tr["pj_ps"])))
    fit_c = safe(lambda: tail_fit(dec["tie"], *FIT_CAPTURE), {})
    dd_c = safe(lambda: dual_dirac(fit_c, ber), {})
    check("tail fit on capture TIE: TJ_dd within -3%..+12% of truth (shallow fit -> conservative)",
          lambda: (-0.03 <= dd_c["tj"] / tj_true - 1 <= 0.12,
                   "RJdd=%.3f DJdd=%.2f TJ=%.2fps (%+.1f%%), %d+%d pts"
                   % (dd_c["rj"], dd_c["dj"], dd_c["tj"], (dd_c["tj"] / tj_true - 1) * 100, fit_c["nL"], fit_c["nR"])))
    ns_long, tie_long = G.tie_record(cfg_a, PLEN * 4000, seed=4242)
    fit_l = safe(lambda: tail_fit(tie_long, *FIT_LONG), {})
    dd_l = safe(lambda: dual_dirac(fit_l, ber), {})
    check("tail fit on long TIE (%d edges, 2F in [1e-5,1e-3]): TJ_dd within 3%% of truth" % len(tie_long),
          lambda: (abs(dd_l["tj"] / tj_true - 1) <= 0.03,
                   "RJdd=%.3f DJdd=%.2f TJ=%.2fps (%+.1f%%)" % (dd_l["rj"], dd_l["dj"], dd_l["tj"], (dd_l["tj"] / tj_true - 1) * 100)))
    check("model check: RJdd > true RJ and DJdd < DJpp (dual-Dirac is a tail model, not a decomposition)",
          lambda: (dd_l["rj"] > tr["rj_ps"] and dd_l["dj"] < dj_pp_true,
                   "RJdd=%.3f>%.2f, DJdd=%.2f<%.2f" % (dd_l["rj"], tr["rj_ps"], dd_l["dj"], dj_pp_true)))
    tj_sum = dj_pp_true + 2 * dd_l.get("q", 7.034) * tr["rj_ps"]
    check("component sum DJpp + 2Q*RJ over-estimates TJ (conservative, 0..+8%)",
          lambda: (0.0 < tj_sum / tj_true - 1 < 0.08, "sum=%.2fps vs %.2fps (%+.1f%%)" % (tj_sum, tj_true, (tj_sum / tj_true - 1) * 100)))
    est_vals = [x for x in dec.get("ddj", []) if x is not None]
    tj_conv = safe(lambda: G.tj_convolution(est_vals, dec["pj"]["amp"], dec["rj"], ber), 0.0)
    check("convolution TJ from the decomposed components (A) within 2% of truth",
          lambda: (abs(tj_conv / tj_true - 1) < 0.02, "TJconv=%.2fps (%+.2f%%)" % (tj_conv, (tj_conv / tj_true - 1) * 100)))
    cap1 = G.make_capture(lanes[1])
    dec1 = safe(lambda: decompose(cap1), {})
    raw = safe(lambda: find_pj(dec1["n"], dec1["tie"], dec1["T"]), {})
    f_pat = 1e6 / cfg_a["ui_ps"] / PLEN

    def order_matters():
        h = raw["f_mhz"] / f_pat
        return (abs(h - round(h)) * f_pat < 0.05 and near(dec1["pj"]["f_mhz"], lanes[1]["pj_mhz"], 0.05),
                "FFT before DDJ removal -> %.3fMHz = %d x f_pat(%.3fMHz), after -> %.3fMHz"
                % (raw["f_mhz"], round(h), f_pat, dec1["pj"]["f_mhz"]))
    check("lane TX1 (big ISI): PJ search BEFORE DDJ removal locks on a pattern harmonic", order_matters)

    # ---- Part C
    print("-- Part C: %s, %s --" % (spec["board"], spec["link"]))
    res = {}
    for cfg in lanes:
        c = cap1 if cfg["name"] == "TX1" else G.make_capture(cfg)
        d = dec1 if cfg["name"] == "TX1" else safe(lambda: decompose(c), {})
        dd = safe(lambda: dual_dirac(tail_fit(d["tie"], *FIT_CAPTURE), ber), {})
        tv = [x for x in G.true_ddj(cfg) if x is not None]
        tjt = G.tj_convolution(tv, cfg["pj_ps"], cfg["rj_ps"], ber)
        out = safe(lambda: judge_lane(cfg["name"], d, dd, spec, cfg["ui_ps"]), (None, None, ""))
        res[cfg["name"]] = (d, dd, out, tjt, max(tv) - min(tv), cfg)
        if out[2]:
            print("      " + out[2])
        if d and dd:
            print("        truth: DDJ=%.1fps PJ=%.1fps@%.2fMHz RJ=%.2fps TJ=%.1fps | est err: DDJ %+.2f PJ %+.2f RJ %+.3f TJdd %+.1f%%"
                  % (max(tv) - min(tv), cfg["pj_ps"], cfg["pj_mhz"], cfg["rj_ps"], tjt, d["ddj_pp"] - (max(tv) - min(tv)),
                     d["pj"]["amp"] - cfg["pj_ps"], d["rj"] - cfg["rj_ps"], (dd["tj"] / tjt - 1) * 100))
    check("TX0 -> PASS", lambda: res["TX0"][2][0] == "PASS")
    check("TX1 -> FAIL, dominant DDJ (long lossy lane)", lambda: res["TX1"][2][:2] == ("FAIL", "DDJ"))
    check("TX2 -> FAIL, dominant PJ at 25.00 +-0.05MHz",
          lambda: (res["TX2"][2][:2] == ("FAIL", "PJ") and near(res["TX2"][0]["pj"]["f_mhz"], 25.0, 0.05),
                   "f=%.3fMHz" % res["TX2"][0]["pj"]["f_mhz"]))

    def comp_err():
        worst = []
        for name, (d, _, _, _, ddjpp, cfg) in res.items():
            e = (abs(d["ddj_pp"] - ddjpp), abs(d["pj"]["amp"] - cfg["pj_ps"]), abs(d["rj"] - cfg["rj_ps"]))
            worst.append(e)
        w = [max(x[i] for x in worst) for i in range(3)]
        return w[0] < 0.6 and w[1] < 0.2 and w[2] < 0.05, "worst |err| DDJpp=%.2f PJ=%.3f RJ=%.3f ps" % tuple(w)
    check("all lanes: decomposition error DDJpp<0.6ps, PJ<0.2ps, RJ<0.05ps vs injected", comp_err)
    check("all lanes: dual-Dirac TJ within -3%..+12% of truth",
          lambda: (all(-0.03 <= r[1]["tj"] / r[3] - 1 <= 0.12 for r in res.values()),
                   ", ".join("%s %+.1f%%" % (k, (r[1]["tj"] / r[3] - 1) * 100) for k, r in res.items())))

    print("%d/%d passed  (%.1fs)" % (g_pass, g_total, time.time() - t_start))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
