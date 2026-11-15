#!/usr/bin/env python3
"""SIV-A06 demo: 측정 방법의 한계를 표로 측정. python3 -I demo_limits.py
모두 합성 모델이며 수치는 이 모델에서의 측정값이다 (solution.py 의 CHECK 와 별개)."""
import math
import random


def acquire(width, depth, Ts, phase, mode):
    if mode == "point":
        return depth if phase < width else 0.0
    if mode == "peak":
        return depth if width > 0 else 0.0
    best = 0.0
    for k in range(0, int(math.ceil(width / Ts)) + 2):
        lo, hi = phase + (k - 1) * Ts, phase + k * Ts
        best = max(best, max(0.0, min(width, hi) - max(0.0, lo)))
    return depth * best / Ts


def rate(width, depth, Ts, lim, mode, n=1000):
    return sum(acquire(width, depth, Ts, (j + 0.5) / n * Ts, mode) >= lim for j in range(n)) / n


def table_modes():
    print("== 1. 단발 사각 dip 검출률 (Ts=10us, depth=2xlimit, 위상 1000 격자)")
    print("W/Ts  point  boxcar  peak")
    for r in (0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0):
        w = r * 10.0
        print("%4.2f  %.3f  %.3f   %.3f" % (r, rate(w, .1, 10., .05, "point"), rate(w, .1, 10., .05, "boxcar"), rate(w, .1, 10., .05, "peak")))


def table_vdip():
    print("== 2. V자 dip 깊이 과소평가 (point 샘플, 깊이 D=100%, 위상 평균/최악)")
    print("W/Ts   mean_seen  worst_seen")
    Ts = 10.0
    for r in (1.0, 2.0, 3.0, 4.0, 8.0, 16.0):
        W = r * Ts
        seen = []
        for j in range(2000):
            ph = (j + 0.5) / 2000 * Ts
            best = 0.0
            for k in range(0, int(W / Ts) + 2):
                x = ph + k * Ts
                if 0 <= x <= W:
                    best = max(best, 1.0 - 2.0 * abs(x - W / 2.0) / W)
            seen.append(best)
        print("%5.1f   %.3f      %.3f" % (r, sum(seen) / len(seen), min(seen)))


def table_false_alarm():
    print("== 3. 흰잡음 위 false alarm (한계까지 m sigma, N=500000 샘플)")
    print("m    measured  N*Q(m)   min_samples=2")
    rng = random.Random(55)
    xs = [rng.gauss(0, 1) for _ in range(500000)]
    for m in (2.0, 3.0, 4.0, 5.0):
        ev = two = 0
        i, n = 0, len(xs)
        while i < n:
            if xs[i] < -m:
                j = i
                cnt = 0
                while j < n and xs[j] < -m:
                    cnt += 1
                    j += 1
                ev += 1
                two += cnt >= 2
                i = j
            else:
                i += 1
        q = 0.5 * math.erfc(m / math.sqrt(2.0))
        print("%.1f  %8d  %8.1f  %8d" % (m, ev, 500000 * q, two))


def table_interp():
    print("== 4. 임계 통과 시각 오차: 샘플 시각 그대로 vs 선형 보간 (Ts=10us, ramp 400us, sigma=0.2%nom, 1000회)")
    rng = random.Random(77)
    Ts, T, nom, sig = 10.0, 400.0, 1.8, 0.0036
    level = 0.5 * nom
    err_s, err_i = [], []
    for k in range(1000):
        ton = 1000.0 + rng.random() * Ts
        truth = ton + 0.5 * T
        prev = None
        for i in range(2000):
            t = i * Ts
            y = (0.0 if t < ton else nom * min(1.0, (t - ton) / T)) + rng.gauss(0, sig)
            if prev is not None and prev[1] < level <= y:
                err_s.append(t - truth)
                ti = prev[0] + (level - prev[1]) * Ts / (y - prev[1])
                err_i.append(ti - truth)
                break
            prev = (t, y)

    def rms(a):
        return math.sqrt(sum(x * x for x in a) / len(a))

    print("sample_time_rms=%.2fus  interp_rms=%.2fus  (noise/slope = %.2fus)" % (rms(err_s), rms(err_i), sig / (nom / T)))


def table_gate():
    print("== 5. gated counter ±1 count 분해능 (ppm)")
    print("f_hz         1ms       10ms      100ms     1s")
    for f in (12.288e6, 24e6, 100e6, 1.056e9):
        print("%-11.0f %8.2f  %8.3f  %8.4f  %8.5f" % (f, 1e6 / (f * .001), 1e6 / (f * .01), 1e6 / (f * .1), 1e6 / f))


if __name__ == "__main__":
    table_modes()
    table_vdip()
    table_false_alarm()
    table_interp()
    table_gate()
