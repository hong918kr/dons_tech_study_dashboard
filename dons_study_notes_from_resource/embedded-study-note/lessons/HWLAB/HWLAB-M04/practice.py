#!/usr/bin/env python3
"""HWLAB-M04 추가 연습 — 본문 모델(solution.py)을 확장한다

  연습 1  dithered_mean      : LSB 보다 작은 sleep 전류를 잡음(dither) + 평균으로 읽을 수 있나? 단방향 ADC 의 함정
  연습 2  multi_frame_error  : 점 샘플 오차는 캡처를 길게 하면 줄어드나? — DUT/계측기 클럭 드리프트가 있을 때만
  연습 3  cp_current         : DC/DC 가 달린 DUT(정전력 부하)는 burden 때문에 전류가 '늘어난다'
  연습 4  ranges_needed / covers : ADC 비트 수와 레인지 개수, 그리고 레인지 표가 전 구간을 덮는지 검사

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def dithered_mean(i_a, lsb_a, fs_a, sigma_lsb, n, seed=4, bipolar=False):
    """i_a 에 가우스 잡음(σ = sigma_lsb·LSB)을 더해 n 번 양자화한 평균.
    bipolar=False 는 본문 quantize 그대로(0 아래 → 0), True 는 [−FS, FS] 양방향 ADC."""
    rng = random.Random(seed)
    sd = sigma_lsb * lsb_a
    acc = 0.0
    for _ in range(n):
        x = i_a + (rng.gauss(0.0, sd) if sd else 0.0)
        if bipolar:
            x = min(max(x, -fs_a), fs_a)
            acc += round(x / lsb_a) * lsb_a
        else:
            acc += S.quantize(x, lsb_a, fs_a)
    return acc / n


# ---------------------------------------------------------------- 연습 2
def multi_frame_error(segs, fs_hz, phase0_us, drift_ppm, frames):
    """같은 1 s 프레임을 frames 번 연속 캡처. DUT 클럭이 계측기보다 drift_ppm 빠르면 프레임마다 샘플 위상이
    frame_us·ppm·1e-6 µs 씩 밀린다(점 샘플). 전체 평균의 오차[%]."""
    T = segs[-1][1]
    i_true = S.average_current(segs)
    Ts = 1e6 / fs_hz
    acc = 0.0
    for f in range(frames):
        ph = (phase0_us + f * T * drift_ppm * 1e-6) % Ts
        s = S.sample_point(segs, fs_hz, ph)
        acc += sum(s) / len(s)
    return (acc / frames / i_true - 1.0) * 100.0


# ---------------------------------------------------------------- 연습 3
def cp_current(p_w, v_src, r_ohm):
    """정전력 부하 P 가 R 뒤에 있을 때 정상 상태 전류: I·(V_src − I·R) = P → R·I² − V_src·I + P = 0 의 작은 근.
    판별식 < 0 이면 None (정전력 부하가 burden 때문에 붕괴 — UVLO/brown-out 까지 전압이 무너진다)."""
    if r_ohm == 0:
        return p_w / v_src
    disc = v_src * v_src - 4.0 * r_ohm * p_w
    if disc < 0:
        return None
    return (v_src - math.sqrt(disc)) / (2.0 * r_ohm)


# ---------------------------------------------------------------- 연습 4
def ranges_needed(i_min, i_max, bits, e):
    """레인지 하나가 'LSB ≤ e·I' 로 덮는 폭 = FS / (FS·2^-bits / e) = e·2^bits 배.
    [i_min, i_max] 를 덮는 최소 레인지 수 (폭이 1 이하면 불가능 → None)."""
    span = e * 2 ** bits
    if span <= 1.0:
        return None
    return max(1, int(math.ceil(math.log(i_max / i_min) / math.log(span) - 1e-12)))


def covers(ranges, i_min, i_max, e, n=400):
    """로그 간격 n 점에서, 어떤 전류든 'FS ≥ I 이고 LSB ≤ e·I' 인 레인지가 하나라도 있는가. 못 덮는 첫 전류도 반환."""
    for k in range(n + 1):
        i = i_min * (i_max / i_min) ** (k / n)
        if not any(r["fs_a"] >= i and r["lsb_a"] <= e * i for r in ranges):
            return False, i
    return True, None


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn()
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as ex:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(ex).__name__, ex)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def main():
    prof = S.load_json("profile_sensnode.json")
    meters = {m["name"]: m for m in S.load_json("instruments.json")["meters"]}
    truth = S.build_profile(prof)
    Is = prof["sleep_a"]

    print("-- 연습 1: dither --")
    lsb, fs = 0.02 / 4096, 0.02
    m0 = dithered_mean(Is, lsb, fs, 0.0, 1000)
    mb = dithered_mean(Is, lsb, fs, 0.5, 200000, bipolar=True)
    mu = dithered_mean(Is, lsb, fs, 0.5, 200000, bipolar=False)
    print("      12-bit/20mA (LSB %.3fuA), sleep 2uA: no noise %.4fuA | 0.5LSB rms bipolar %.4fuA | unipolar %.4fuA"
          % (lsb * 1e6, m0 * 1e6, mb * 1e6, mu * 1e6))
    check("no dither: 2uA < LSB/2 reads 0 forever (averaging cannot help)", lambda: m0 == 0.0)
    check("0.5 LSB rms dither + bipolar ADC + 200k-sample mean -> within 2% of 2uA",
          lambda: (abs(mb / Is - 1) < 0.02, "%+.2f%%" % ((mb / Is - 1) * 100)))
    sd = 0.5 * lsb
    Phi = lambda x: 0.5 * (1 + math.erf((x - Is) / sd / math.sqrt(2)))          # 정규분포 CDF, 평균 2uA
    mu_th = sum(k * lsb * (Phi((k + 0.5) * lsb) - Phi((k - 0.5) * lsb)) for k in range(1, 40))
    check("unipolar ADC clips the noise below 0 -> biased up to E[Q(max(X,0))] = %.3fuA (+%.0f%%), within 3%%"
          % (mu_th * 1e6, (mu_th / Is - 1) * 100),
          lambda: (abs(mu / mu_th - 1) < 0.03 and mu / Is - 1 > 0.05, "%.4fuA" % (mu * 1e6)))

    print("-- 연습 2: capture length vs clock drift (point sampling, 10 kS/s) --")
    ph_worst = max(((j + 0.5) for j in range(100)),
                   key=lambda p: abs(multi_frame_error(truth, 10000, p, 0.0, 1)))
    e1 = multi_frame_error(truth, 10000, ph_worst, 37.0, 1)
    e10 = multi_frame_error(truth, 10000, ph_worst, 37.0, 10)
    e100 = multi_frame_error(truth, 10000, ph_worst, 37.0, 100)
    e100_0 = multi_frame_error(truth, 10000, ph_worst, 0.0, 100)
    print("      worst phase %.1fus: 1 frame %+.2f%% | 10 frames %+.2f%% | 100 frames %+.3f%% (37 ppm) | 100 frames, 0 ppm %+.2f%%"
          % (ph_worst, e1, e10, e100, e100_0))
    check("37 ppm drift: 100 frames (phase walks 37us/frame over the 100us grid) -> |err| < 0.1%",
          lambda: (abs(e1) > 10 and abs(e100) < 0.1, "%+.2f%% -> %+.3f%%" % (e1, e100)))
    check("0 ppm (DUT and meter share a clock / locked): 100 frames == 1 frame error",
          lambda: (abs(e100_0 - e1) < 1e-9, "%+.2f%%" % e100_0))

    print("-- 연습 3: constant-power (DC/DC) load behind a shunt --")
    P, Vs = 0.015 * 3.0, 3.0
    for r in (0.05, 1.0, 10.0, 100.0):
        i = cp_current(P, Vs, r)
        print("      R=%6.2f ohm: I=%s" % (r, "collapse (no solution)" if i is None else "%.4fmA (%+.2f%% vs 15mA)" % (i * 1e3, (i / 0.015 - 1) * 100)))
    check("R=10 ohm: current rises to 15.836mA (+5.6%) - burden changes the quantity you measure",
          lambda: (abs(cp_current(P, Vs, 10.0) - 0.0158359) < 1e-7, "%.4fmA" % (cp_current(P, Vs, 10.0) * 1e3)))
    check("R=0.05 ohm: < +0.03%; R=100 ohm: no solution (collapse)",
          lambda: (cp_current(P, Vs, 0.05) / 0.015 - 1 < 3e-4 and cp_current(P, Vs, 100.0) is None))

    print("-- 연습 4: ADC bits vs number of ranges --")
    need = {b: ranges_needed(Is, 0.015, b, 0.01) for b in (8, 12, 16, 20)}
    print("      ranges needed for 2uA..15mA at LSB <= 1%% of reading: %s" % need)
    check("ranges needed: 8-bit 10, 12-bit 3, 16-bit 2, 20-bit 1",
          lambda: (need == {8: 10, 12: 3, 16: 2, 20: 1}, "%s" % need))
    ok_ar, _ = covers(meters["AR_100K"]["ranges"], Is, 0.015, 0.01)
    ok_sc, gap_sc = covers(meters["SCOPE_10R"]["ranges"], Is, 0.015, 0.01)
    r8 = [dict(fs_a=r["fs_a"], lsb_a=r["fs_a"] / 256) for r in meters["AR_100K"]["ranges"]]
    ok_8, gap_8 = covers(r8, Is, 0.015, 0.01)
    print("      AR_100K covers=%s | SCOPE_10R covers=%s (first gap %.3fuA) | decade ranges @8-bit covers=%s (first gap %.3fuA)"
          % (ok_ar, ok_sc, gap_sc * 1e6, ok_8, gap_8 * 1e6))
    check("AR_100K decade ranges @12-bit cover 2uA..15mA at 1%; scope single range and decade@8-bit do not",
          lambda: ok_ar and not ok_sc and not ok_8)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
