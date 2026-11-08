#!/usr/bin/env python3
"""HWLAB-B08 추가 연습 — 한 번에 검증

  연습 1  스파이크 타이밍 → fsw · duty · Vin 역산
  연습 2  BW limit 필터 모양(1차 단일 극 / 2차 / 4차 Butterworth)에 따라 Vpp(20MHz) 가 달라진다
  연습 3  수직 분해능과 클리핑: 8비트 ADC 의 V/div · offset 설정이 리플 수치를 망치는 경우
  연습 4  Vpp 는 기록 길이에 따라 커진다 — 잡음만 있는 신호의 Vpp vs RMS vs 백분위 pk-pk
  연습 5  DC + AC 합산 윈도우: Vavg 와 Vpp 가 각각 통과해도 순간값이 ±3% 를 넘는가

  python3 -I practice.py      # "N/M passed", 실패 시 exit 1
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_capture as gen                                   # noqa: E402
from solution import (FS, SPECS, bw_limit, find_spikes,     # noqa: E402
                      load_csv, rail_stats)


# ---------------------------------------------------------------- 연습 1
def switching_from_spikes(events, vout):
    """find_spikes 결과 → (fsw, D, vin_est). 양(+) 스파이크 = ON 에지, 음(-) = OFF 에지.
    T = ON 에지 간격 평균, D = (OFF 에지 - 직전 ON 에지) 평균 / T, Vin ≈ Vout / D (이상적 벅)"""
    on = [t for t, p in events if p > 0]
    off = [t for t, p in events if p < 0]
    T = (on[-1] - on[0]) / (len(on) - 1)
    ton = [min((b - a for a in on if a < b), default=None) for b in off]
    ton = [x for x in ton if x is not None]
    D = sum(ton) / len(ton) / T
    return 1.0 / T, D, vout / D


# ---------------------------------------------------------------- 연습 2
def single_pole(v, fs=FS, fc=20e6):
    """1차 RC 저역통과 (임펄스 불변: a = exp(-2π·fc/fs)), 첫 샘플로 초기화"""
    a = math.exp(-2.0 * math.pi * fc / fs)
    y, s = [], v[0]
    for x in v:
        s = a * s + (1.0 - a) * x
        y.append(s)
    return y


# ---------------------------------------------------------------- 연습 3
def scope_quantize(v, vdiv, offset, bits=8, divs=8):
    """화면 = offset ± vdiv·divs/2 를 2^bits 코드로 나눈 ADC. 범위를 넘으면 코드 0 / max 로 클리핑.
    반환 (전압 리스트, 클리핑된 샘플 수, LSB)"""
    full = vdiv * divs
    lo = offset - full / 2.0
    nmax = (1 << bits) - 1
    lsb = full / (1 << bits)
    out, clipped = [], 0
    for x in v:
        c = int(math.floor((x - lo) / lsb))
        if c < 0 or c > nmax:
            clipped += 1
            c = 0 if c < 0 else nmax
        out.append(lo + (c + 0.5) * lsb)
    return out, clipped, lsb


# ---------------------------------------------------------------- 연습 4
def pct_pp(v, p=0.5):
    """백분위 pk-pk: (100-p) 백분위 - p 백분위 (최근접 순위). 이상치 몇 개에 흔들리지 않는다."""
    s = sorted(v)
    n = len(s)
    lo = s[int(math.floor(p / 100.0 * (n - 1)))]
    hi = s[int(math.ceil((100.0 - p) / 100.0 * (n - 1)))]
    return hi - lo


# ---------------------------------------------------------------- 연습 5
def window_check(v_bwl, nominal, tol_pct):
    """BW-limited 순간값의 min/max 가 nominal ± tol 안인가 → (ok, vmin, vmax, lo, hi)"""
    lo, hi = nominal * (1 - tol_pct / 100.0), nominal * (1 + tol_pct / 100.0)
    vmin, vmax = min(v_bwl), max(v_bwl)
    return (lo <= vmin and vmax <= hi), vmin, vmax, lo, hi


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        r = fn()
        ok, detail = r if isinstance(r, tuple) else (bool(r), "")
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("  [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail))


def main():
    t1, v1 = load_csv(os.path.join(HERE, "cap_VDD_1V8.csv"))
    yb = bw_limit(v1)

    print("-- 연습 1: spikes -> fsw / D / Vin --")
    fsw, D, vin = switching_from_spikes(find_spikes(t1, v1, yb), rail_stats(v1)["vavg"])
    check("fsw = 2.000MHz ±0.5%", lambda: (abs(fsw - 2e6) < 0.005 * 2e6, "%.4f MHz" % (fsw / 1e6)))
    check("D = 1.803/5.0 = 0.3606 ±0.005", lambda: (abs(D - 0.3606) < 0.005, "D=%.4f" % D))
    check("Vin estimate = 5.0V ±2%", lambda: (abs(vin - 5.0) < 0.1, "Vin≈%.3fV" % vin))

    print("-- 연습 2: BW-limit filter shape --")
    pp1 = rail_stats(single_pole(v1))["vpp"]
    pp2 = rail_stats(yb)["vpp"]
    pp4 = rail_stats(bw_limit(v1, order=4))["vpp"]
    _, vtrue = gen.synth("VDD_1V8", noise=0, spike_rise=0, spike_fall=0)
    ppt = rail_stats(vtrue)["vpp"]
    det = "1st=%.2f 2nd=%.2f 4th=%.2f truth=%.2f mV" % (pp1 * 1e3, pp2 * 1e3, pp4 * 1e3, ppt * 1e3)
    check("Vpp(20MHz) ordering 1st > 2nd > 4th, all < 30mV", lambda: (pp1 > pp2 > pp4 and pp1 < 0.030, det))
    _, vs = gen.synth("VDD_1V8", noise=0)                    # 스파이크만 남기기: (리플+스파이크) - 리플
    spk = [1.8 + a - b for a, b in zip(vs, vtrue)]
    lk = [max(abs(x - 1.8) for x in f(spk)) for f in (single_pole, bw_limit, lambda v: bw_limit(v, order=4))]
    check("spike leakage pk: single-pole > 2x 2nd-order",
          lambda: (lk[0] > 2 * lk[1], "leak 1st=%.2f 2nd=%.2f 4th=%.2f mV" % tuple(x * 1e3 for x in lk)))
    rng0 = random.Random(1)
    wn0 = [rng0.gauss(0.0, 1e-3) for _ in range(100000)]
    exp1 = math.sqrt((math.pi / 2) * 20e6 / (FS / 2))         # 1차 ENBW = (π/2)·fc
    check("single-pole noise ratio = sqrt((π/2)fc/(fs/2)) = 0.1585 ±6%",
          lambda: (lambda r: (abs(r - exp1) < 0.06 * exp1, "got %.4f exp %.4f" % (r, exp1)))(
              rail_stats(single_pole(wn0))["rms_ac"] / rail_stats(wn0)["rms_ac"]))

    print("-- 연습 3: vertical resolution & clipping (8-bit) --")
    qa, ca, la = scope_quantize(v1, 0.5, 0.0)        # 500 mV/div, offset 0: 1.8V 를 화면에 넣으려는 흔한 설정
    qb, cb, lb = scope_quantize(v1, 0.010, 1.8)      # 10 mV/div, offset 1.8 V
    qc, cc, lc = scope_quantize(v1, 0.002, 1.8)      # 2 mV/div: 스파이크가 화면 밖 → 클리핑
    ppa = rail_stats(bw_limit(qa))["vpp"]
    ppb = rail_stats(bw_limit(qb))["vpp"]
    check("500mV/div: LSB = 15.6mV (ripple < 1 LSB)", lambda: (abs(la - 0.015625) < 1e-12, "LSB=%.3fmV" % (la * 1e3)))
    check("500mV/div: 20MHz Vpp off by > 3mV from unquantized",
          lambda: (abs(ppa - pp2) > 0.003, "quant=%.2fmV ref=%.2fmV" % (ppa * 1e3, pp2 * 1e3)))
    check("10mV/div+offset: LSB 0.31mV, no clip, Vpp within 0.5mV",
          lambda: (cb == 0 and abs(ppb - pp2) < 0.0005, "LSB=%.3fmV Vpp=%.2fmV clip=%d" % (lb * 1e3, ppb * 1e3, cb)))
    check("2mV/div+offset: clipping detected (screen ±8mV)",
          lambda: (cc > 0, "clipped=%d samples LSB=%.4fmV" % (cc, lc * 1e3)))

    print("-- 연습 4: Vpp grows with record length --")
    rng = random.Random(7)
    wn = [rng.gauss(0.0, 1e-3) for _ in range(100000)]
    pps = [rail_stats(wn[:n])["vpp"] for n in (1000, 10000, 100000)]
    check("Vpp(1k) < Vpp(10k) < Vpp(100k) for the same noise",
          lambda: (pps[0] < pps[1] < pps[2], "%s mV" % ["%.2f" % (x * 1e3) for x in pps]))
    rms = [rail_stats(wn[:n])["rms_ac"] for n in (10000, 100000)]
    check("RMS stays at sigma=1mV ±3% (10k, 100k)",
          lambda: (all(abs(r - 1e-3) < 3e-5 for r in rms), "%s mV" % ["%.4f" % (x * 1e3) for x in rms]))
    ref = 2 * 2.5758 * 1e-3                                    # 0.5–99.5 % = ±2.576σ
    pp99 = [pct_pp(wn[:n]) for n in (10000, 100000)]
    check("0.5-99.5 percentile pk-pk ≈ 5.15σ ±5% (stable vs N)",
          lambda: (all(abs(p - ref) < 0.05 * ref for p in pp99), "%s mV" % ["%.3f" % (x * 1e3) for x in pp99]))

    print("-- 연습 5: DC + AC window --")
    t8, v8 = gen.synth("VDD_0V8")
    ok8, mn8, mx8, lo8, hi8 = window_check(bw_limit(v8), SPECS["VDD_0V8"]["nominal"], 3.0)
    ok18, mn18, mx18, lo18, hi18 = window_check(yb, 1.8, 3.0)
    check("VDD_1V8 window PASS (min/max @20MHz inside 1.746..1.854)",
          lambda: (ok18, "%.4f..%.4f" % (mn18, mx18)))
    check("VDD_0V8 window: report min/max vs 0.776..0.824",
          lambda: (lo8 == 0.8 * 0.97 and hi8 == 0.8 * 1.03, "min=%.4f max=%.4f ok=%s" % (mn8, mx8, ok8)))
    check("VDD_0V8 headroom < 5mV on the tighter side (ripple eats the window)",
          lambda: (min(mn8 - lo8, hi8 - mx8) < 0.005, "headroom=%.2fmV" % (min(mn8 - lo8, hi8 - mx8) * 1e3)))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
