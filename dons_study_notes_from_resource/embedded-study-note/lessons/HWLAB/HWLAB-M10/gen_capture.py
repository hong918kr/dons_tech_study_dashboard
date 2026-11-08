#!/usr/bin/env python3
"""HWLAB-M10 캡처 생성기 (결정적) — HWLAB-M09 스타일 PRBS7 NRZ + 채널 ISI + 주입 지터(RJ/PJ)

HWLAB-M09(eye diagram)가 병렬로 작성 중이라, 이 레슨은 M09식 캡처를 여기서 축약해 직접 만든다.
  - 데이터: PRBS7 (x^7 + x^6 + 1, 주기 127 비트), NRZ ±0.4 V (800 mVppd 차동을 단일 값으로 표현)
  - 채널 ISI: 1극(RC) 채널, 시정수 tau = tau_ui · UI (M09 는 FIR 탭 — 어느 쪽이든 선형 채널 ISI 라 DDJ 분해는 같다)
    RC 를 고른 이유: 비트 경계 전압이 v[m+1] = b[m] + (v[m] - b[m])·e^(-1/tau_ui) 로 정확히 재귀되어
    연속 시간 파형과 '정답 DDJ'(잡음 없는 교차 시각)를 해석적으로 얻을 수 있다.
  - 지터: 채널 뒤 관측 시간축을 비튼다(time-warp): v_obs(t) = v_clean(t - J(t)).
    J(t) 는 비트 경계 n 의 교차 시각 주변 ±0.4 UI 에서 정확히 j[n] = PJ(n) + RJ(n) 으로 평평하므로,
    에지 n 의 정답 TIE = DDJ[p] + PJ(n) + RJ(n) 이 정확히 성립한다 (p = n mod 127).
    (Tx 지터를 채널 '앞'에 넣으면 ISI가 이웃 에지 지터를 섞어 정답 RJ 가 모호해진다 — md 5장 참고)
  - 진폭 잡음: 샘플마다 가우스 noise_mv (기울기로 나눠 타이밍 지터로 변환됨 — 연습 1)

  python3 -I gen_capture.py   # valbrd_b_serdes.json(스펙+레인 설정) 과 capture_preview_tx2.csv(앞 64 UI) 를 쓴다
Python 3.9+, 표준 라이브러리만.
"""
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
PLEN = 127                        # PRBS7 주기
FLAT = 0.4                        # J(t) 가 j[n] 으로 평평한 반폭 (UI)


def prbs7(seed=0x7F):
    """PRBS7 (x^7 + x^6 + 1) 한 주기 127 비트 (0/1). seed != 0."""
    s, out = seed & 0x7F, []
    for _ in range(PLEN):
        nb = ((s >> 6) ^ (s >> 5)) & 1
        s = ((s << 1) | nb) & 0x7F
        out.append(nb)
    return out


def boundary_levels(bits, tau_ui, amp):
    """주기 정상상태에서 각 비트 시작 시각의 전압 V[m] (m = 0..126). b = ±amp."""
    a = math.exp(-1.0 / tau_ui)
    lev = [amp if b else -amp for b in bits]
    v = 0.0
    for _ in range(4):                               # 4 주기 돌리면 e^(-127·4/τ) 로 완전히 수렴
        for m in range(PLEN):
            v = lev[m] + (v - lev[m]) * a
    V = []
    for m in range(PLEN):
        V.append(v)
        v = lev[m] + (v - lev[m]) * a
    return V, lev


def clean_crossings(bits, tau_ui, amp, thr=0.0):
    """잡음·지터 없는 파형에서 경계 p 의 에지가 thr 를 지나는 시각(UI, 경계 기준). 전이 없는 p 는 None.
    비트 p 구간: v(x) = b + (V[p] - b)·e^(-x/τ) → x = τ·ln((V[p] - b)/(thr - b)). 교차가 다음 비트로 넘어가면 None."""
    V, lev = boundary_levels(bits, tau_ui, amp)
    out = []
    for p in range(PLEN):
        if bits[p] == bits[p - 1]:
            out.append(None)
            continue
        b = lev[p]
        r = (V[p] - b) / (thr - b)
        x = tau_ui * math.log(r) if r > 0 else None
        out.append(x if (x is not None and 0.0 <= x < 1.0) else None)
    return out


def _jitter_seq(n_bits, cfg, rng, c0):
    """경계 n = -1 .. n_bits 의 j[n] (ps): PJ 는 교차 시각 근처 (n + c0)·UI 에서 평가."""
    ui, A, f, ph, sd = cfg["ui_ps"], cfg["pj_ps"], cfg["pj_mhz"] * 1e6, cfg["pj_phase"], cfg["rj_ps"]
    j = {}
    for n in range(-1, n_bits + 2):
        j[n] = A * math.sin(2 * math.pi * f * (n + c0) * ui * 1e-12 + ph) + sd * rng.gauss(0.0, 1.0)
    return j


def make_capture(cfg, n_bits=None):
    """cfg(레인 설정) → dict(dt_ps, v[샘플 V], truth{...}). 결정적 (cfg['seed'])."""
    rng = random.Random(cfg["seed"])
    ui, spp, tau, amp = cfg["ui_ps"], cfg["spp"], cfg["tau_ui"], cfg["amp_v"]
    n_bits = n_bits or cfg["n_bits"]
    bits = prbs7()
    V, lev = boundary_levels(bits, tau, amp)
    c0 = tau * math.log(2.0)                            # 정착된 에지의 50 % 교차 지연 (UI) — J 매듭 위치
    j = _jitter_seq(n_bits, cfg, rng, c0)
    dt = ui / spp
    noise = cfg["noise_mv"] * 1e-3
    v = []
    for i in range(n_bits * spp):
        u = i / spp                                     # UI 단위 시간
        w = u - c0
        n = int(math.floor(w + 0.5))
        e = w - n
        if e > FLAT:
            J = j[n] + (j[n + 1] - j[n]) * (e - FLAT) / (1.0 - 2 * FLAT)
        elif e < -FLAT:
            J = j[n - 1] + (j[n] - j[n - 1]) * (e + 1.0 - FLAT) / (1.0 - 2 * FLAT)
        else:
            J = j[n]
        uw = u - J / ui                                 # 비틀린 시간
        m = int(math.floor(uw))
        p = m % PLEN
        x = uw - m
        val = lev[p] + (V[p] - lev[p]) * math.exp(-x / tau)
        v.append(val + (rng.gauss(0.0, noise) if noise else 0.0))
    cc = clean_crossings(bits, tau, amp)
    ddj = _center([x * ui if x is not None else None for x in cc])
    truth = dict(ddj_ps=ddj, pj_ps=cfg["pj_ps"], pj_mhz=cfg["pj_mhz"], rj_ps=cfg["rj_ps"], c0_ui=c0,
                 jit_ps={n: j[n] for n in range(0, n_bits)}, bits=bits)
    return dict(dt_ps=dt, v=v, ui_ps=ui, truth=truth)


def _center(vals):
    xs = [x for x in vals if x is not None]
    m = sum(xs) / len(xs)
    return [None if x is None else x - m for x in vals]


def true_ddj(cfg, thr=0.0):
    """정답 DDJ (ps, 전이 있는 위치들 평균 0) — 위치 p = 0..126, 전이 없으면 None."""
    cc = clean_crossings(prbs7(), cfg["tau_ui"], cfg["amp_v"], thr)
    return _center([x * cfg["ui_ps"] if x is not None else None for x in cc])


def tie_record(cfg, n_bits, seed):
    """파형 없이 같은 지터 모델로 긴 TIE 기록만 빠르게: (n, tie_ps). tie = DDJ[p] + PJ(n) + RJ(n)."""
    rng = random.Random(seed)
    ddj = true_ddj(cfg)
    ui, A, f, ph, sd = cfg["ui_ps"], cfg["pj_ps"], cfg["pj_mhz"] * 1e6, cfg["pj_phase"], cfg["rj_ps"]
    c0 = cfg["tau_ui"] * math.log(2.0)
    w = 2 * math.pi * f * ui * 1e-12
    ns, tie = [], []
    for n in range(n_bits):
        d = ddj[n % PLEN]
        g = rng.gauss(0.0, 1.0)                         # 전이 여부와 무관하게 난수를 소비 (결정성 단순화)
        if d is None:
            continue
        ns.append(n)
        tie.append(d + A * math.sin(w * (n + c0) + ph) + sd * g)
    return ns, tie


def _phi_upper(z):
    """표준정규 위쪽 꼬리 P(Z > z) = erfc(z/√2)/2."""
    return 0.5 * math.erfc(z / math.sqrt(2.0))


def conv_tail(x, ddj_vals, A, sd, K=256):
    """성분 모델의 P(TIE > x): DDJ(전이 위치 균등) ⊗ PJ(위상 균등 사인) ⊗ RJ(가우스).
    사인 위상 적분은 K 점 중점 규칙 (주기 함수라 스펙트럼 정확도)."""
    s = 0.0
    sins = [A * math.sin(2 * math.pi * (k + 0.5) / K) for k in range(K)]
    for d in ddj_vals:
        for a in sins:
            s += _phi_upper((x - d - a) / sd)
    return s / (len(ddj_vals) * K)


def tj_convolution(ddj_vals, A, sd, ber=1e-12):
    """성분 모델의 TJ(BER): 에지가 [xL, xR] 밖으로 나갈 확률 = BER (양쪽 꼬리 각각 BER/2). 이분법.
    주입한 정답 성분을 넣으면 '정답 TJ', 분해로 추정한 성분을 넣으면 '컨볼루션 TJ 추정'이 된다.
    dual-Dirac(가중치 1/2씩)에서는 이 정의가 정확히 DJδδ + 2·Q(BER)·RJ, Q = Φ⁻¹(1-BER) 를 준다."""
    def solve(vals):
        lo, hi = max(vals), max(vals) + A + 12 * sd
        for _ in range(50):
            mid = 0.5 * (lo + hi)
            if conv_tail(mid, vals, A, sd) > ber / 2:
                lo = mid
            else:
                hi = mid
        return 0.5 * (lo + hi)
    xr = solve(ddj_vals)
    xl = -solve([-d for d in ddj_vals])
    return xr - xl


# ---------------------------------------------------------------- 레슨 데이터: VALBRD-B 스펙 + 레인 설정
BASE = dict(ui_ps=100.0, spp=16, amp_v=0.4, noise_mv=1.0, n_bits=PLEN * 130, pj_phase=0.7)

LANES = [
    dict(BASE, name="TX0", tau_ui=0.50, rj_ps=1.00, pj_ps=2.0, pj_mhz=37.3, seed=1100),
    dict(BASE, name="TX1", tau_ui=0.80, rj_ps=1.00, pj_ps=2.0, pj_mhz=37.3, seed=1101),
    dict(BASE, name="TX2", tau_ui=0.50, rj_ps=1.00, pj_ps=9.0, pj_mhz=25.0, seed=1102),
]

PART_A = dict(BASE, name="DEMO", tau_ui=0.55, rj_ps=1.50, pj_ps=6.0, pj_mhz=50.97, seed=1010)

SPEC = {
    "board": "VALBRD-B rev A",
    "link": "SoC SERDES TX -> J5 (SMA breakout), PRBS7 @ 10 Gb/s",
    "note": "사내 TX 지터 예산 (대표값 - 실제 표준/데이터시트 확인 필요)",
    "ber": 1e-12,
    "tj_ui_max": 0.28,
    "dj_dd_ui_max": 0.15,
    "rj_ps_max": 2.0,
}


def write_files():
    data = {"spec": SPEC, "lanes": LANES, "part_a": PART_A}
    with open(os.path.join(HERE, "valbrd_b_serdes.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1, sort_keys=True)
        f.write("\n")
    cap = make_capture(LANES[2], n_bits=64)
    with open(os.path.join(HERE, "capture_preview_tx2.csv"), "w", encoding="ascii") as f:
        f.write("# VALBRD-B TX2 preview: first 64 UI, %g ps/sample (full capture is generated in memory)\n"
                % cap["dt_ps"])
        f.write("time_ps,v\n")
        for i, x in enumerate(cap["v"]):
            f.write("%.3f,%.6f\n" % (i * cap["dt_ps"], x))


if __name__ == "__main__":
    write_files()
    print("wrote valbrd_b_serdes.json, capture_preview_tx2.csv")
