#!/usr/bin/env python3
"""HWLAB-M02 load-transient 캡처 생성기 (결정적 — 다시 돌려도 CSV 가 바이트 단위로 같다)

레귤레이터 폐루프 출력 임피던스를 2차 시스템(병렬 RLC 탱크)으로 모델링한다.
    Zout(s) = R_LL + (1/C) · s / (s² + 2ζωn·s + ωn²)
      R_LL : load-line(AVP) 저항, 일반 POL 레귤레이터는 0 (적분기 → DC 오차 0)
      C    : 유효 출력 용량, ωn ≈ 루프 교차 주파수 근처, ζ : 폐루프 감쇠비
부하 전류 i(t) = i0 → i1 사다리꼴(slew A/s) 에 대한 출력:
    v(t) = vnom − R_LL·i(t) − S·[H(t−t0) − H(t−t0−tr)]          (S = slew, tr = (i1−i0)/S)
    H(t) = ∫0^t h(τ)dτ,  h(t) = e^{−αt}·sin(ωd·t) / (C·ωd)       (전류 계단 1 A 에 대한 전압 응답)
여기에 스위칭 리플(삼각파, fsw), 백색 잡음, 12-bit hi-res 양자화(0.1 mV)를 더한다.

  python3 -I gen_capture.py      # VALBRD-A U12 VDD_CORE_0V8 보상 변경 전/후 CSV 두 장을 쓴다

Python 3.9+, 표준 라이브러리만 (math, random, os, sys).
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- 2차 출력 임피던스 모델
def h_step(t, zeta, wn, C):
    """전류 계단 1 A 에 대한 탱크 전압 응답 [V/A] (−|ζ| < 1). t <= 0 이면 0."""
    if t <= 0.0:
        return 0.0
    wd = wn * math.sqrt(1.0 - zeta * zeta)
    return math.exp(-zeta * wn * t) * math.sin(wd * t) / (C * wd)


def h_int(t, zeta, wn, C):
    """H(t) = ∫0^t h_step — 기울기 1 A/s 램프 전류에 대한 응답 [V·s/A]. H(∞) = 1/(C·ωn²)."""
    if t <= 0.0:
        return 0.0
    a = zeta * wn
    wd = wn * math.sqrt(1.0 - zeta * zeta)
    return (wd - math.exp(-a * t) * (a * math.sin(wd * t) + wd * math.cos(wd * t))) / (wn * wn * C * wd)


def load_current(t, p):
    """사다리꼴 부하 전류: t0 에서 i0 → i1 로 slew [A/s] 로 변한다 (i1 < i0 이면 부하 해제)."""
    di = p["i1"] - p["i0"]
    tr = abs(di) / p["slew"]
    x = min(max((t - p["t0"]) / tr, 0.0), 1.0)
    return p["i0"] + di * x


def v_model(t, p):
    """잡음·리플 없는 출력 전압 [V]."""
    di = p["i1"] - p["i0"]
    tr = abs(di) / p["slew"]
    s = math.copysign(p["slew"], di)
    wn = 2.0 * math.pi * p["fn"]
    tank = s * (h_int(t - p["t0"], p["zeta"], wn, p["C"]) - h_int(t - p["t0"] - tr, p["zeta"], wn, p["C"]))
    return p["vnom"] - p.get("r_ll", 0.0) * load_current(t, p) - tank


def synth(p, dt, t_start, n, ripple_pp=0.0, fsw=1e6, noise_v=0.0, noise_i=0.0, lsb_v=0.0, seed=1):
    """스코프 캡처 흉내: (t, v, i) 리스트. 리플 = 평균 0 삼각파(위상 랜덤), 잡음 = 가우스."""
    rng = random.Random(seed)
    ph = rng.random()
    t, v, i = [], [], []
    for k in range(n):
        tk = t_start + k * dt
        x = (tk * fsw + ph) % 1.0
        rip = ripple_pp * (2.0 * abs(x - 0.5) - 0.5)          # −pp/2 … +pp/2, 한 주기 평균 0
        vk = v_model(tk, p) + rip + (rng.gauss(0.0, noise_v) if noise_v else 0.0)
        if lsb_v:
            vk = round(vk / lsb_v) * lsb_v
        t.append(tk)
        v.append(vk)
        i.append(load_current(tk, p) + (rng.gauss(0.0, noise_i) if noise_i else 0.0))
    return t, v, i


def truth(p, band, t_end_scan=None, step=5e-9):
    """잡음 없는 모델을 촘촘히 훑은 '정답': droop, t_droop(t0 기준), recovery(t0 기준), Td, ζ.
    recovery = |v − v_final| > band 인 마지막 시각 − t0 (밴드 경계는 이분법으로 다듬음)."""
    di = p["i1"] - p["i0"]
    v_pre = v_model(p["t0"] - 1e-6, p)
    v_fin = p["vnom"] - p.get("r_ll", 0.0) * p["i1"]
    wn = 2.0 * math.pi * p["fn"]
    wd = wn * math.sqrt(1.0 - p["zeta"] ** 2)
    if t_end_scan is None:
        t_end_scan = p["t0"] + min(40.0 / (p["zeta"] * wn), 400e-6)   # 포락선 e^-40 또는 400 µs
    sgn = 1.0 if di > 0 else -1.0                                # 부하 증가 → 전압이 내려간다
    worst, t_w, last_out = 0.0, p["t0"], None
    t = p["t0"]
    while t <= t_end_scan:
        dv = v_model(t, p)
        if sgn * (v_pre - dv) > worst:
            worst, t_w = sgn * (v_pre - dv), t
        if abs(dv - v_fin) > band:
            last_out = t
        t += step
    rec = None
    if last_out is not None:
        a, b = last_out, last_out + step
        f = lambda x: abs(v_model(x, p) - v_fin) - band          # noqa: E731  a: 밖(+), b: 안(−)
        for _ in range(60):
            m = 0.5 * (a + b)
            if f(m) > 0:
                a = m
            else:
                b = m
        rec = 0.5 * (a + b) - p["t0"]
    return {"droop": worst, "t_droop": t_w - p["t0"], "recovery": rec, "Td": 2.0 * math.pi / wd,
            "zeta": p["zeta"], "v_pre": v_pre, "v_final": v_fin}


# ---------------------------------------------------------------- Part C 보드 데이터
# VALBRD-A rev B, VDD_CORE_0V8 (벅 U12, fsw 1 MHz, Cout_eff ≈ 220 µF: 2×100 µF 폴리머 + MLCC 의 DC-bias 후 유효값)
# 부하 스텝: 보드 위 load-step FET(Q9) 로 0.5 → 3 A, 1 A/µs. 프로브: TP40 (SoC 볼 근처 via), 스프링 팁.
BOARD_BASE = {"vnom": 0.800, "i0": 0.5, "i1": 3.0, "slew": 1.0e6, "t0": -0.25e-6, "C": 220e-6, "r_ll": 0.0}
CAPTURES = {
    # 보상 변경 전: Rcomp 가 커서 교차 주파수가 높고 위상 마진이 부족 (주입한 참값 ζ=0.15, fn=48 kHz)
    "U12_before_comp.csv": dict(BOARD_BASE, zeta=0.15, fn=48e3, seed=4101),
    # 보상 변경 후: Rcomp 10k → 6.8k, Ccomp 추가 조정 (주입한 참값 ζ=0.55, fn=36 kHz)
    "U12_after_comp.csv": dict(BOARD_BASE, zeta=0.55, fn=36e3, seed=4102),
}
CAP_DT, CAP_T0, CAP_N = 50e-9, -20e-6, 5000            # 20 MS/s, −20 µs … +230 µs
CAP_RIPPLE_PP, CAP_FSW, CAP_NOISE_V, CAP_NOISE_I, CAP_LSB = 8e-3, 1e6, 0.8e-3, 0.015, 0.1e-3


def write_capture(name, p):
    t, v, i = synth(p, CAP_DT, CAP_T0, CAP_N, ripple_pp=CAP_RIPPLE_PP, fsw=CAP_FSW, noise_v=CAP_NOISE_V,
                    noise_i=CAP_NOISE_I, lsb_v=CAP_LSB, seed=p["seed"])
    path = os.path.join(HERE, name)
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("# VALBRD-A rev B  VDD_CORE_0V8 @TP40 (spring tip)  load step Q9 0.5->3A 1A/us  %s\n" % name)
        f.write("# CH1 vout DC-coupled 10mV/div offset 0.8V hi-res; CH4 current probe 1A/div; 20MS/s\n")
        f.write("time_us,vout_V,iload_A\n")
        for tk, vk, ik in zip(t, v, i):
            f.write("%.2f,%.5f,%.3f\n" % (tk * 1e6, vk, ik))
    return path


def main():
    for name, p in CAPTURES.items():
        path = write_capture(name, p)
        print("wrote %s (%d bytes)" % (os.path.basename(path), os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
