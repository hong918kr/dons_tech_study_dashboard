#!/usr/bin/env python3
"""HWLAB-M12 추가 연습 — 본문 라이브러리(solution.py)를 확장한다

  연습 1  prbs(n)             : PRBS7/9/11 로 eye 를 다시 재서 '패턴이 길수록 eye 가 작아진다'를 확인 (PDA 가 하한)
  연습 2  ffe_grid_best       : eye(@1e-12)를 직접 최대화하는 TX FFE 격자 탐색 vs zero-forcing
  연습 3  ctle_peaking        : CTLE 의 zero 주파수·peak 주파수·peaking[dB] (ADC 를 깊게 해도 peaking < |ADC|)
  연습 4  dfe_sim_unrolled    : tap-1 투기(speculation, loop unrolling) DFE 가 직접 DFE 와 같은 결정을 내는지

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
TAPS = {7: (7, 6), 9: (9, 5), 11: (11, 9)}       # x^7+x^6+1, x^9+x^5+1, x^11+x^9+1


def prbs(order):
    """피보나치 LFSR 로 PRBS(order) 한 주기 (2^order - 1 비트), ±1."""
    a, b = TAPS[order]
    mask = (1 << order) - 1
    r = mask
    out = []
    for _ in range(mask):
        bit = ((r >> (a - 1)) ^ (r >> (b - 1))) & 1
        r = ((r << 1) | bit) & mask
        out.append(1 if bit else -1)
    return out


# ---------------------------------------------------------------- 연습 2
def ffe_grid_best(p, step=0.025, cm1_max=0.2, cp1_max=0.35, sigma_in=S.SIGMA_IN_MV):
    """c-1 ∈ [-cm1_max, 0], c+1 ∈ [-cp1_max, 0] 격자, c0 = 1 - |c-1| - |c+1|. PDA eye @1e-12 최대인 조합."""
    thr = 2 * S.Q_1E12 * S.noise_out_rms(None, sigma_in)
    best = None
    na, nb = int(round(cm1_max / step)), int(round(cp1_max / step))
    for i in range(na + 1):
        for j in range(nb + 1):
            c = (-i * step, 1 - (i + j) * step, -j * step)
            eh, ew, _ = S.eye_metrics(S.pda_curve(S.ffe_apply(p, c)), thr)
            if best is None or (eh, ew) > (best[0], best[1]):
                best = (eh, ew, c)
    return best


# ---------------------------------------------------------------- 연습 3
def ctle_peaking(adc_db, fp1=2e9, fp2=8e9, fmax=20e9, n=2000):
    """→ (fz, f_peak, peaking_dB = 20·log10(|H|max / |H(0)|)). |H| 를 0..fmax 에서 훑는다."""
    fz = 10 ** (adc_db / 20.0) * fp1
    h0 = S.ctle_mag(0.0, adc_db, fp1, fp2)
    fpk, hpk = 0.0, h0
    for i in range(1, n + 1):
        f = fmax * i / n
        m = S.ctle_mag(f, adc_db, fp1, fp2)
        if m > hpk:
            fpk, hpk = f, m
    return fz, fpk, 20 * math.log10(hpk / h0)


# ---------------------------------------------------------------- 연습 4
def dfe_sim_unrolled(cur, ntaps, sigma, nbits=100000, seed=12):
    """tap-1 을 투기(speculative)로: 두 후보 r + w1, r - w1 를 미리 판정해 두고 직전 결정으로 고른다.
    (실리콘에선 tap-1 피드백이 1 UI 안에 닫혀야 하는 타이밍 문제를 비교기 2개 + MUX 로 푼다.)
    난수 소비 순서를 S.dfe_sim 과 똑같이 맞춰 같은 비트·잡음으로 비교한다."""
    rng = random.Random(seed)
    ks = sorted(cur)
    pad, fut = max(ks) + 1, max(0, -min(ks))
    bits = [rng.choice((-1, 1)) for _ in range(pad + nbits + fut)]
    w = [cur[j] for j in range(1, ntaps + 1)]
    hk = [(k, cur[k]) for k in ks]
    d = bits[:pad]
    errs = []
    for n in range(pad, pad + nbits):
        r = rng.gauss(0.0, sigma)
        for k, h in hk:
            r += h * bits[n - k]
        for j in range(2, ntaps + 1):              # tap 2.. 는 여유가 있으니 그대로 빼고
            r -= w[j - 1] * d[n - j]
        if_prev_pos = 1 if r - w[0] >= 0 else -1   # d[n-1] = +1 이라고 가정한 비교기
        if_prev_neg = 1 if r + w[0] >= 0 else -1   # d[n-1] = -1 이라고 가정한 비교기
        dn = if_prev_pos if d[n - 1] > 0 else if_prev_neg     # MUX
        d.append(dn)
        if dn != bits[n]:
            errs.append(n - pad)
    return errs


# ---------------------------------------------------------------- 하네스
def main():
    t0 = time.time()
    p15 = S.pulse_from_step(S.channel_step(15.0))
    q7 = S.ctle_apply(p15, -7)

    print("-- 연습 1: pattern length vs eye (no EQ / CTLE -7 dB) --")
    rows = {}
    for name, q in (("no EQ", p15), ("CTLE -7", q7)):
        ehs = [S.eye_metrics(S.eye_curve(q, prbs(o)))[0] for o in (7, 9, 11)]
        pda = S.eye_metrics(S.pda_curve(q))[0]
        rows[name] = ehs + [pda]
        print("      %-8s PRBS7 %.1f  PRBS9 %.1f  PRBS11 %.1f  PDA %.1f mV" % (name, *rows[name]))
    S.check("prbs(7) == solution.prbs7() and len(prbs(11)) == 2047, balanced (+1 count = 2^(n-1))",
            lambda: prbs(7) == S.prbs7() and len(prbs(11)) == 2047 and prbs(11).count(1) == 1024)
    S.check("no EQ: EH(PRBS7) >= EH(PRBS9) >= EH(PRBS11) >= PDA",
            lambda: all(rows["no EQ"][i] >= rows["no EQ"][i + 1] - 1e-9 for i in range(3)))
    S.check("CTLE -7: same ordering, PRBS11 within 15 mV of PDA (worst case nearly hit)",
            lambda: (all(rows["CTLE -7"][i] >= rows["CTLE -7"][i + 1] - 1e-9 for i in range(3))
                     and rows["CTLE -7"][2] - rows["CTLE -7"][3] < 15,
                     "gap=%.1f" % (rows["CTLE -7"][2] - rows["CTLE -7"][3])))

    print("-- 연습 2: eye-optimal TX FFE grid vs zero-forcing (no CTLE/DFE, @1e-12) --")
    thr = 2 * S.Q_1E12 * S.SIGMA_IN_MV
    zf = S.ffe_zf(S.cursors(p15, 2, 2))
    zf_eh = S.eye_metrics(S.pda_curve(S.ffe_apply(p15, zf)), thr)
    gb = ffe_grid_best(p15)
    print("      ZF   c=(%.3f, %.3f, %.3f) -> EH=%.1fmV EW=%.3fUI" % (zf + zf_eh[:2]))
    print("      grid c=(%.3f, %.3f, %.3f) -> EH=%.1fmV EW=%.3fUI" % (gb[2] + gb[:2]))
    S.check("grid optimum beats ZF on eye height @1e-12 (ZF != eye-optimal)",
            lambda: (gb[0] > zf_eh[0], "%.1f > %.1f" % (gb[0], zf_eh[0])))
    S.check("grid optimum keeps |c|-sum = 1 and both side taps <= 0",
            lambda: abs(sum(abs(c) for c in gb[2]) - 1) < 1e-12 and gb[2][0] <= 0 and gb[2][2] <= 0)

    print("-- 연습 3: CTLE zero / peak / peaking --")
    pk = {}
    for adc in (-6, -9, -12):
        pk[adc] = ctle_peaking(adc)
        print("      ADC %3d dB: fz=%.3f GHz  f_peak=%.2f GHz  peaking=%.2f dB  |H(4GHz)|=%.2f dB"
              % (adc, pk[adc][0] / 1e9, pk[adc][1] / 1e9, pk[adc][2], 20 * math.log10(S.ctle_mag(4e9, adc))))
    S.check("fz = 10^(ADC/20)*fp1: ADC -6 dB -> 1.002 GHz",
            lambda: (abs(pk[-6][0] - 1.0024e9) < 1e6, "%.4f GHz" % (pk[-6][0] / 1e9)))
    S.check("peaking grows with |ADC| but stays below |ADC| (poles cap the HF gain)",
            lambda: pk[-6][2] < pk[-9][2] < pk[-12][2] and all(pk[a][2] < -a for a in pk))
    S.check("peak frequency sits between fp1 and fp2 (2..8 GHz)",
            lambda: all(2e9 <= pk[a][1] <= 8e9 for a in pk))

    print("-- 연습 4: tap-1 speculative (unrolled) DFE == direct DFE --")
    c = S.cursors(p15, 2, 24)
    sig = c[0] / 3.2
    e_dir = S.dfe_sim(c, 3, sig, 50000, 7)
    e_unr = dfe_sim_unrolled(c, 3, sig, 50000, 7)
    print("      direct %d errors, unrolled %d errors" % (len(e_dir), len(e_unr)))
    S.check("unrolled DFE makes exactly the same decisions (same error positions)",
            lambda: (e_dir == e_unr and len(e_dir) > 0, "%d errors" % len(e_dir)))

    print("%d/%d passed  (%.1f s)" % (S.g_pass, S.g_total, time.time() - t0))
    return 0 if S.g_pass == S.g_total else 1


if __name__ == "__main__":
    sys.exit(main())
