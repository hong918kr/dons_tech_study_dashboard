#!/usr/bin/env python3
"""HWLAB-B05 데모: 공진(peaking)이 있는 프로브 체인은 rise time 을 '실제보다 빠르게' 보여줄 수 있다.

  python3 -I demo_peaking.py

1ns 가우시안 에지(진짜 tr = 1.000ns), Rs=33Ω, 그라운드 스프링 10nH 를 고정하고
tip C 와 스코프 필터 유무를 바꿔 측정 tr / overshoot 를 출력한다. 측정은 B04-lite (solution.py 와 같은 코드).
"""
import math

TR_K = 2.5631031311


def gauss_edge(t, t0, tr, v0=0.0, v1=1.0):
    s = tr / TR_K
    return v0 + (v1 - v0) * 0.5 * (1.0 + math.erf((t - t0) / (s * math.sqrt(2.0))))


def hist_mode(vals, lo, hi, nbins):
    w = (hi - lo) / nbins
    cnt = [0] * nbins
    for x in vals:
        if lo <= x < hi:
            cnt[min(nbins - 1, int((x - lo) / w))] += 1
    b = max(range(nbins), key=lambda k: cnt[k])
    sel = sorted(x for x in vals if lo + (b - 1) * w <= x < lo + (b + 2) * w)
    return sel[len(sel) // 2]


def cross(t, v, level, start=1):
    for i in range(max(1, start), len(v)):
        if v[i - 1] < level <= v[i]:
            return t[i - 1] + (level - v[i - 1]) / (v[i] - v[i - 1]) * (t[i] - t[i - 1]), i
    raise ValueError("no crossing")


def measure(t, v):
    lo, hi = min(v), max(v)
    mid = (lo + hi) / 2
    base, top = hist_mode(v, lo, mid, 100), hist_mode(v, mid, hi + 1e-12, 100)
    a = top - base
    _, i50 = cross(t, v, base + 0.5 * a)
    j = i50
    while j > 1 and v[j - 1] > base + 0.1 * a:
        j -= 1
    t10, _ = cross(t, v, base + 0.1 * a, j - 1)
    t90, _ = cross(t, v, base + 0.9 * a, i50 - 1)
    return t90 - t10, 100.0 * (max(v[i50:]) - top) / a


def simulate(Rs, L, C, bw, dt=5e-12, t_end=60e-9, every=10):
    tau = 1.0 / (2 * math.pi * bw) if bw else None

    def d(t, s):
        i, v, y = s
        return ((gauss_edge(t, 10e-9, 1e-9) - Rs * i - v) / L, (i - v / 10e6) / C,
                (v - y) / tau if tau else 0.0)
    s = (0.0, 0.0, 0.0)
    ts, vs = [], []
    for k in range(int(round(t_end / dt)) + 1):
        t = k * dt
        if k % every == 0:
            ts.append(t)
            vs.append(s[2] if tau else s[1])
        k1 = d(t, s)
        k2 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k1)))
        k3 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k2)))
        k4 = d(t + dt, tuple(a + dt * b for a, b in zip(s, k3)))
        s = tuple(a + dt / 6 * (b + 2 * c + 2 * e + g) for a, b, c, e, g in zip(s, k1, k2, k3, k4))
    return ts, vs


def main():
    # 스코프만 (프로브 영향 없음): 단일 극점 1GHz 를 1ps 간격으로 직접 적용
    dt = 1e-12
    a = 1.0 - math.exp(-dt * 2 * math.pi * 1e9)
    y, t, out = 0.0, [], []
    for k in range(8000):
        y += a * (gauss_edge(k * dt, 3e-9, 1e-9) - y)
        t.append(k * dt)
        out.append(y)
    tr, os_ = measure(t, out)
    print("%-34s tr=%.3f ns  OS=%.2f%%" % ("scope 1GHz only (ideal probe)", tr * 1e9, os_))
    for C in (1e-12, 12e-12):
        for bw in (None, 1e9):
            zeta = 33.0 / (2 * math.sqrt(10e-9 / C))
            f0 = 1 / (2 * math.pi * math.sqrt(10e-9 * C))
            tr, os_ = measure(*simulate(33.0, 10e-9, C, bw))
            label = "tip %2.0fpF + 10nH, %s" % (C * 1e12, "scope 1GHz" if bw else "no scope filter")
            print("%-34s tr=%.3f ns  OS=%.2f%%   (f0=%.2fGHz zeta=%.3f)" % (label, tr * 1e9, os_, f0 / 1e9, zeta))


if __name__ == "__main__":
    main()
