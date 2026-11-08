#!/usr/bin/env python3
"""HWLAB-B05 추가 연습 답 — 한 번에 검증

  python3 -I practice.py      # "N/M passed", 실패가 있으면 exit 1

연습 1  스코프 응답 모양(k=0.35 / 0.45)과 "3배·5배 규칙"의 실제 오차
연습 2  링잉 캡처에서 직렬 저항 R 역산 (zeta, L, C → R = 2 zeta sqrt(L/C))
연습 3  overshoot 10% 이내가 되는 최대 그라운드 리드 길이 (10nH/cm 가정, 시뮬레이션 + 이분법)
연습 4  프로브 부하 허용치: AC(tip C)와 DC(입력 저항) 두 가지
연습 5  그라운드 리드에 직렬 감쇠 저항을 넣으면? (overshoot vs rise time 트레이드오프)
본문(solution.py)의 필요한 부분을 축약해 넣었다 (레슨 디렉터리끼리 import 하지 않는다). 표준 라이브러리만.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TR_K = 2.5631031311


# ---- 본문에서 가져온 도구 (B04-lite 측정 + RLC 시뮬레이션 + 링잉 분석) ----
def gauss_edge(t, t0, tr, v0=0.0, v1=1.0):
    s = tr / TR_K
    return v0 + (v1 - v0) * 0.5 * (1.0 + math.erf((t - t0) / (s * math.sqrt(2.0))))


def load_capture(name):
    t, v = [], []
    with open(os.path.join(HERE, "captures", name + ".csv"), encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line and line[0] in "0123456789-":
                a, b = line.split(",")
                t.append(float(a) * 1e-9)
                v.append(float(b))
    return t, v


def hist_mode(vals, lo, hi, nbins):
    w = (hi - lo) / nbins
    cnt = [0] * nbins
    for x in vals:
        if lo <= x < hi:
            cnt[min(nbins - 1, int((x - lo) / w))] += 1
    b = max(range(nbins), key=lambda k: cnt[k])
    sel = sorted(x for x in vals if lo + (b - 1) * w <= x < lo + (b + 2) * w)
    return sel[len(sel) // 2]


def cross_time(t, v, level, start=0):
    for i in range(max(1, start), len(v)):
        if v[i - 1] < level <= v[i]:
            return t[i - 1] + (level - v[i - 1]) / (v[i] - v[i - 1]) * (t[i] - t[i - 1]), i
    raise ValueError("no crossing")


def edge_measure(t, v):
    lo, hi = min(v), max(v)
    mid = (lo + hi) / 2.0
    base, top = hist_mode(v, lo, mid, 100), hist_mode(v, mid, hi + 1e-12, 100)
    amp = top - base
    t50, i50 = cross_time(t, v, base + 0.5 * amp)
    j = i50
    while j > 1 and v[j - 1] > base + 0.1 * amp:
        j -= 1
    t10, _ = cross_time(t, v, base + 0.1 * amp, j - 1)
    t90, _ = cross_time(t, v, base + 0.9 * amp, i50 - 1)
    peak = max(v[i50:])
    return {"top": top, "amp": amp, "i50": i50, "tr": t90 - t10, "peak": peak, "os_pct": 100.0 * (peak - top) / amp}


def simulate_probe(src, Rs, L, C, Rp=10e6, scope_bw=1e9, dt=5e-12, t_end=60e-9, every=10):
    tau = 1.0 / (2.0 * math.pi * scope_bw)

    def d(t, s):
        i, v, y = s
        return (src(t) - Rs * i - v) / L, (i - v / Rp) / C, (v - y) / tau
    s = (0.0, 0.0, 0.0)
    ts, vs = [], []
    for k in range(int(round(t_end / dt)) + 1):
        t = k * dt
        if k % every == 0:
            ts.append(t)
            vs.append(s[2])
        k1 = d(t, s)
        k2 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k1)))
        k3 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k2)))
        k4 = d(t + dt, tuple(a + dt * b for a, b in zip(s, k3)))
        s = tuple(a + dt / 6 * (b + 2 * c + 2 * e + g) for a, b, c, e, g in zip(s, k1, k2, k3, k4))
    return ts, vs


def ring_analysis(t, v, top, i_start, hyst):
    d = [x - top for x in v]
    peaks, want_max, e = [], True, i_start
    for i in range(i_start, len(d)):
        if want_max:
            if d[i] > d[e]:
                e = i
            elif d[i] < d[e] - hyst:
                if d[e] < hyst:
                    break
                peaks.append(e)
                want_max, e = False, i
        else:
            if d[i] < d[e]:
                e = i
            elif d[i] > d[e] + hyst:
                if d[e] > -hyst:
                    break
                peaks.append(e)
                want_max, e = True, i
    zc = []
    for a, b in zip(peaks, peaks[1:]):
        for i in range(a + 1, b + 1):
            if (d[i - 1] > 0) != (d[i] > 0):
                zc.append(t[i - 1] + (0.0 - d[i - 1]) / (d[i] - d[i - 1]) * (t[i] - t[i - 1]))
                break
    f_ring = (len(zc) - 1) / (2.0 * (zc[-1] - zc[0])) if len(zc) >= 2 else None
    big = [abs(d[p]) for p in peaks if abs(d[p]) >= 2.0 * hyst]
    zeta = None
    if len(big) >= 3:
        dl = math.log(big[1] / big[-1]) / (len(big) - 2)
        zeta = dl / math.sqrt(math.pi ** 2 + dl ** 2)
    return f_ring, zeta


def noise_rms(v, n=100):
    m = sum(v[:n]) / n
    return math.sqrt(sum((x - m) ** 2 for x in v[:n]) / (n - 1))


# ---- 연습 1: 응답 모양과 경험칙 ----
def required_bw(tr_sig, err, k):
    return k / (tr_sig * math.sqrt((1.0 + err) ** 2 - 1.0))


def rule_error(multiple):
    """스코프 BW = multiple * (0.35/tr_sig) 일 때 RSS 오차 (두 쪽 모두 k=0.35 가정) = sqrt(1+1/m^2)-1"""
    return math.sqrt(1.0 + 1.0 / multiple ** 2) - 1.0


# ---- 연습 2: R 역산 ----
def series_r_from_ring(f_ring, zeta, C):
    f0 = f_ring / math.sqrt(1.0 - zeta * zeta)
    L = 1.0 / ((2.0 * math.pi * f0) ** 2 * C)
    return 2.0 * zeta * math.sqrt(L / C), L


# ---- 연습 3: 최대 그라운드 리드 길이 ----
def os_for_L(L, Rs=33.0, C=12e-12, tr=1e-9):
    ts, vs = simulate_probe(lambda t: gauss_edge(t, 5e-9, tr, 0.0, 3.3), Rs, L, C, t_end=50e-9)
    return edge_measure(ts, vs)["os_pct"]


def max_lead_L(os_limit, Rs=33.0, C=12e-12, tr=1e-9):
    lo, hi = 1e-9, 150e-9
    for _ in range(22):
        mid = math.sqrt(lo * hi)
        if os_for_L(mid, Rs, C, tr) > os_limit:
            hi = mid
        else:
            lo = mid
    return lo


def zeta_for_os(os_pct):
    """이상적 계단의 overshoot → zeta 역산: ln(OS) = -pi zeta / sqrt(1-zeta^2)"""
    a = -math.log(os_pct / 100.0)
    return a / math.sqrt(math.pi ** 2 + a * a)


# ---- 연습 4: 프로브 부하 허용치 ----
def max_tip_c(tr_sig, rs, err):
    """RC 근사: tr_load = 2.2 Rs C, RSS 로 err 이내 → C_max"""
    return tr_sig * math.sqrt((1.0 + err) ** 2 - 1.0) / (2.2 * rs)


def dc_loading_error(r_src, r_in):
    """Thevenin r_src 에 r_in 을 물렸을 때 읽힌 전압의 상대 오차 (음수 = 낮게 읽음)"""
    return r_in / (r_src + r_in) - 1.0


# ---- 연습 5: 감쇠 저항 ----
def damping_r(L, C, zeta_target, rs):
    return max(0.0, 2.0 * zeta_target * math.sqrt(L / C) - rs)


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok, info = fn()
    except Exception as e:  # noqa: BLE001
        ok, info = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS %s  [%s]" % (name, info))
    else:
        print("FAIL %s: %s" % (name, info))


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def main():
    print("-- 연습 1 --")
    check("k=0.35 vs 0.45 for 1ns @3%",
          lambda: (near(required_bw(1e-9, 0.03, 0.35), 1.418e9, 2e6) and near(required_bw(1e-9, 0.03, 0.45), 1.824e9, 2e6),
                   "single-pole/Gaussian-ish %.3f GHz, brick-wall-ish %.3f GHz" % (
                       required_bw(1e-9, 0.03, 0.35) / 1e9, required_bw(1e-9, 0.03, 0.45) / 1e9)))
    check("3x / 4x / 5x rule errors",
          lambda: (near(rule_error(3), 0.0541, 2e-4) and near(rule_error(4), 0.0308, 2e-4) and near(rule_error(5), 0.0198, 2e-4),
                   "3x %.2f%%, 4x %.2f%%, 5x %.2f%%" % (100 * rule_error(3), 100 * rule_error(4), 100 * rule_error(5))))

    print("-- 연습 2 --")

    def p2():
        t, v = load_capture("SPI0_SCLK_lead15cm")
        m = edge_measure(t, v)
        h = max(6.0 * noise_rms(v), 0.02 * m["amp"])
        f, z = ring_analysis(t, v, m["top"], m["i50"], h)
        R, L = series_r_from_ring(f, z, 12e-12)
        return near(R, 33.0, 3.3), "f_ring %.1f MHz, zeta %.3f, L %.0f nH -> R_series %.1f ohm (gen used 33)" % (
            f / 1e6, z, L * 1e9, R)
    check("series R from SPI0_SCLK lead capture ~ 33 ohm", p2)

    print("-- 연습 3 --")

    def p3():
        Lmax = max_lead_L(10.0)
        z = zeta_for_os(10.0)
        L_step = (33.0 / (2 * z)) ** 2 * 12e-12
        ok = 10e-9 < Lmax < 30e-9 and L_step < Lmax
        return ok, "1ns edge: L_max %.1f nH (~%.1f cm @10nH/cm); ideal-step bound zeta>=%.3f -> L<=%.1f nH" % (
            Lmax * 1e9, Lmax * 1e9 / 10.0, z, L_step * 1e9)
    check("max ground lead for OS <= 10%", p3)

    print("-- 연습 4 --")
    check("max tip C for <=10% tr growth (Rs=33, 1ns)",
          lambda: (near(max_tip_c(1e-9, 33.0, 0.10), 6.31e-12, 0.02e-12),
                   "C_max %.2f pF -> 12pF passive fails, ~1pF active OK" % (max_tip_c(1e-9, 33.0, 0.10) * 1e12)))
    check("DC loading of a 1Mohm node: 10M (10x) vs 1M (1x)",
          lambda: (near(dc_loading_error(1e6, 10e6), -0.0909, 1e-4) and near(dc_loading_error(1e6, 1e6), -0.5, 1e-9),
                   "10x %.1f%%, 1x %.1f%%" % (100 * dc_loading_error(1e6, 10e6), 100 * dc_loading_error(1e6, 1e6))))

    print("-- 연습 5 --")

    def p5():
        Rd = damping_r(150e-9, 12e-12, 0.7, 33.0)
        src = lambda t: gauss_edge(t, 5e-9, 1e-9, 0.0, 3.3)  # noqa: E731
        ts, vs = simulate_probe(src, 33.0 + Rd, 150e-9, 12e-12, t_end=50e-9)
        m = edge_measure(ts, vs)
        ok = near(Rd, 123.5, 0.5) and m["os_pct"] < 6.0 and m["tr"] > 2e-9
        return ok, "add %.1f ohm -> OS %.1f%% but tr %.2f ns (true 1 ns)" % (Rd, m["os_pct"], m["tr"] * 1e9)
    check("damping resistor in 15cm lead: OS down, tr up", p5)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
