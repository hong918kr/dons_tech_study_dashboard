#!/usr/bin/env python3
"""HWLAB-B05 스코프 대역폭·프로브 부하·그라운드 리드 링잉

측정 체인(스코프 + 프로브 + 그라운드 리드)이 파형을 얼마나 바꾸는지 숫자로 만든다.
  Part A  rise-time 예산: tr_meas = sqrt(tr_sig^2 + tr_scope^2 + tr_probe^2), BW = 0.35/tr.
          1ns 에지를 3% 오차 이내로 보려면 몇 GHz 가 필요한가 + 그 근사의 가정(가우시안 vs 단일 극점) 확인
  Part B  10x 프로브(10MΩ‖12pF) + 그라운드 리드(150nH vs 스프링 10nH) 직렬 RLC 를 RK4 로 시뮬레이션,
          링잉 주파수 1/(2π√LC)·overshoot·감쇠비를 직접 측정, 프로브 C 부하로 느려지는 rise time
  Part C  BRD-A rev0 캡처(captures/*.csv) 3넷 × (15cm 리드, 스프링): overshoot 가 측정 아티팩트인지
          실제 SI 문제인지 판정하는 리포트

  python3 -I solution.py      # "N/M passed", 실패가 있으면 exit 1

측정 도구(상태 레벨·10-90% 교차·overshoot)는 HWLAB-B04 가 다루는 IEEE 181 방식의 축약판을 이 파일 안에
그대로 넣었다 (다른 레슨 디렉터리를 import 하지 않는다). Python 3 표준 라이브러리만 쓴다.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
K_SP = 0.35            # tr*BW, 단일 극점 (2.2*tau * 1/(2*pi*tau) = 0.350)
TR_K = 2.5631031311    # 가우시안 CDF 의 10-90% 폭 = 2*1.2815515655*sigma


# ======================================================== B04-lite 측정 도구 (완성본, 수정 불필요)
def gauss_edge(t, t0, tr, v0=0.0, v1=1.0):
    """10-90% rise time 이 tr 인 가우시안(erf) 에지. 가우시안 응답끼리의 직렬 연결은 sigma^2 가 더해진다."""
    s = tr / TR_K
    return v0 + (v1 - v0) * 0.5 * (1.0 + math.erf((t - t0) / (s * math.sqrt(2.0))))


def load_capture(name):
    """captures/<name>.csv → (t[s], v[V], 주석 줄 목록)"""
    t, v, meta = [], [], []
    with open(os.path.join(HERE, "captures", name + ".csv"), encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line.startswith("#"):
                meta.append(line[1:].strip())
            elif line and line[0] in "0123456789-":
                a, b = line.split(",")
                t.append(float(a) * 1e-9)
                v.append(float(b))
    return t, v, meta


def hist_mode(vals, lo, hi, nbins):
    """[lo, hi) 구간 히스토그램의 최빈 bin → 그 bin ±1 안 샘플들의 중앙값 (IEEE 181 state level 의 축약).
    평균이 아니라 중앙값: 에지 꼬리 샘플 몇 개가 레벨을 끌고 가지 않게."""
    w = (hi - lo) / nbins
    cnt = [0] * nbins
    for x in vals:
        if lo <= x < hi:
            cnt[min(nbins - 1, int((x - lo) / w))] += 1
    b = max(range(nbins), key=lambda k: cnt[k])
    sel = [x for x in vals if lo + (b - 1) * w <= x < lo + (b + 2) * w]
    sel.sort()
    return sel[len(sel) // 2]


def state_levels(v, nbins=100):
    """(base, top): min..mid 와 mid..max 각각의 히스토그램 mode. 링잉 꼭대기(max)가 아니라 '머무는' 레벨."""
    lo, hi = min(v), max(v)
    mid = (lo + hi) / 2.0
    return hist_mode(v, lo, mid, nbins), hist_mode(v, mid, hi + 1e-12, nbins)


def cross_time(t, v, level, start=0):
    """start 이후 처음으로 level 을 위로 지나는 시각 (선형 보간) 과 그 뒤 샘플 인덱스"""
    for i in range(max(1, start), len(v)):
        if v[i - 1] < level <= v[i]:
            return t[i - 1] + (level - v[i - 1]) / (v[i] - v[i - 1]) * (t[i] - t[i - 1]), i
    raise ValueError("no crossing of %.4f" % level)


def edge_measure(t, v):
    """상승 에지 1개짜리 파형 → base/top/10-90 tr/peak/overshoot%. 50% 교차를 먼저 찾고 그 앞뒤로 10%/90%."""
    base, top = state_levels(v)
    amp = top - base
    t50, i50 = cross_time(t, v, base + 0.5 * amp)
    j = i50
    while j > 1 and v[j - 1] > base + 0.1 * amp:   # 50% 지점에서 거꾸로 걸어가 10% 교차 직전으로
        j -= 1
    t10, _ = cross_time(t, v, base + 0.1 * amp, j - 1)
    t90, i90 = cross_time(t, v, base + 0.9 * amp, i50 - 1)
    peak = max(v[i50:])
    return {"base": base, "top": top, "amp": amp, "t10": t10, "t50": t50, "t90": t90,
            "tr": t90 - t10, "i50": i50, "peak": peak, "os_pct": 100.0 * (peak - top) / amp}


def noise_rms(v, n=100):
    """트리거 전 n 샘플의 표준편차 (잡음 바닥)"""
    seg = v[:n]
    m = sum(seg) / len(seg)
    return math.sqrt(sum((x - m) ** 2 for x in seg) / (len(seg) - 1))


def bw_from_tr(tr, k=K_SP):
    return k / tr


def tr_from_bw(bw, k=K_SP):
    return k / bw


# ======================================================== Part A: rise-time 예산
def rss(*xs):
    """root-sum-square: sqrt(x1^2 + x2^2 + ...)"""
    return math.sqrt(sum(x * x for x in xs))


def rise_error(tr_sig, bws, k=K_SP):
    """신호 tr_sig 를 BW 목록(스코프, 프로브 ...) 체인으로 볼 때 측정 tr 의 상대 오차 (0.03 = 3%)"""
    return rss(tr_sig, *[tr_from_bw(b, k) for b in bws]) / tr_sig - 1.0


def required_system_bw(tr_sig, err, k=K_SP):
    """tr_meas <= (1+err)*tr_sig 가 되는 최소 시스템 BW.  tr_sys = tr_sig*sqrt((1+err)^2-1)"""
    tr_sys = tr_sig * math.sqrt((1.0 + err) ** 2 - 1.0)
    return bw_from_tr(tr_sys, k)


def required_scope_bw(tr_sig, err, probe_bw, k=K_SP):
    """프로브 BW 가 정해져 있을 때 스코프에 필요한 최소 BW. 프로브만으로 예산 초과면 None."""
    tr_sys = tr_sig * math.sqrt((1.0 + err) ** 2 - 1.0)
    tr_probe = tr_from_bw(probe_bw, k)
    if tr_probe >= tr_sys:
        return None
    return bw_from_tr(math.sqrt(tr_sys ** 2 - tr_probe ** 2), k)


def single_pole_filter(x, dt, bw):
    """1차 RC 저역통과 (tau = 1/(2*pi*bw)), 구간별 상수 입력에 대한 정확한 이산화: y += (1-e^(-dt/tau))(x-y)"""
    a = 1.0 - math.exp(-dt * 2.0 * math.pi * bw)
    y, out = 0.0, []
    for xi in x:
        y += a * (xi - y)
        out.append(y)
    return out


# ======================================================== Part B: 프로브 부하 + 그라운드 리드 RLC
def series_rlc(R, L, C):
    """직렬 RLC (C 양단 출력) 의 f0, Z0=sqrt(L/C), zeta=R/(2 Z0), 감쇠 주파수 fd, 이상적 계단 overshoot %"""
    f0 = 1.0 / (2.0 * math.pi * math.sqrt(L * C))
    z0 = math.sqrt(L / C)
    zeta = R / (2.0 * z0)
    if zeta < 1.0:
        fd = f0 * math.sqrt(1.0 - zeta * zeta)
        os_pct = 100.0 * math.exp(-math.pi * zeta / math.sqrt(1.0 - zeta * zeta))
    else:
        fd, os_pct = 0.0, 0.0
    return {"f0": f0, "z0": z0, "zeta": zeta, "fd": fd, "os_pct": os_pct}


def simulate_probe(src, Rs, L, C, Rp=10e6, scope_bw=None, dt=5e-12, t_end=100e-9, every=10):
    """Thevenin 소스 src(t)[V], Rs → 그라운드 리드 L → 프로브 입력 C ‖ Rp. 스코프는 v_C 를 본다.
    상태: i (L 전류), v (C 전압), y (스코프 단일 극점 출력; scope_bw=None 이면 y=v).
      L di/dt = src - Rs*i - v,  C dv/dt = i - v/Rp,  tau dy/dt = v - y
    RK4, dt 간격, every 스텝마다 저장 → (ts, v_scope, v_node).  v_node = src - Rs*i (프로브가 부하한 DUT 노드)"""
    tau = 1.0 / (2.0 * math.pi * scope_bw) if scope_bw else None

    def d(t, s):
        i, v, y = s
        di = (src(t) - Rs * i - v) / L
        dv = (i - v / Rp) / C
        dy = (v - y) / tau if tau else 0.0
        return di, dv, dy

    s = (0.0, 0.0, 0.0)
    n = int(round(t_end / dt))
    ts, vs, vn = [], [], []
    for k in range(n + 1):
        t = k * dt
        if k % every == 0:
            ts.append(t)
            vs.append(s[2] if tau else s[1])
            vn.append(src(t) - Rs * s[0])
        k1 = d(t, s)
        k2 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k1)))
        k3 = d(t + dt / 2, tuple(a + dt / 2 * b for a, b in zip(s, k2)))
        k4 = d(t + dt, tuple(a + dt * b for a, b in zip(s, k3)))
        s = tuple(a + dt / 6 * (b + 2 * c + 2 * e + g) for a, b, c, e, g in zip(s, k1, k2, k3, k4))
    return ts, vs, vn


def ring_analysis(t, v, top, i_start, hyst):
    """i_start(50% 교차) 이후 top 기준 편차 d=v-top 의 교대 극값을 히스테리시스 hyst 로 찾는다.
    반환: peaks [(t, d)], zc (극값 사이 0 교차 시각, 선형 보간), f_ring (0 교차 간격), zeta_est (로그 감쇠율)"""
    d = [x - top for x in v]
    peaks = []
    want_max = True
    e = i_start
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
    zeta_est = None
    if len(big) >= 3:
        dl = math.log(big[1] / big[-1]) / (len(big) - 2)   # 반주기당 로그 감쇠 (첫 극값은 에지 영향으로 제외)
        zeta_est = dl / math.sqrt(math.pi ** 2 + dl ** 2)
    return {"peaks": [(t[p], d[p]) for p in peaks], "zc": zc, "f_ring": f_ring, "zeta_est": zeta_est}


def lead_inductance(f_ring, C, zeta=0.0):
    """측정 링잉 주파수(감쇠 fd)와 알려진 프로브 C 로 루프 인덕턴스 역산: f0 = fd/sqrt(1-zeta^2), L = 1/((2*pi*f0)^2*C)"""
    f0 = f_ring / math.sqrt(1.0 - zeta * zeta)
    return 1.0 / ((2.0 * math.pi * f0) ** 2 * C)


# ======================================================== Part C: BRD-A 캡처 판정
def analyze_capture(name):
    """캡처 1개 → edge_measure + ring_analysis 결과 dict"""
    t, v, _ = load_capture(name)
    m = edge_measure(t, v)
    h = max(6.0 * noise_rms(v), 0.02 * m["amp"])
    m.update(ring_analysis(t, v, m["top"], m["i50"], h))
    m["hyst"] = h
    return m


def diagnose(net, vmax=3.6, c_probe=12e-12):
    """같은 넷을 15cm 리드/스프링으로 잰 두 캡처를 비교해 판정.
      lead 링잉 주파수 → L_est (프로브 C 로 역산). 50~300nH 이면 '그라운드 리드 루프로 설명됨'.
      spring peak > vmax              → REAL      (짧은 그라운드에서도 남는다 = 보드 SI 문제)
      lead peak > vmax, spring <= vmax → ARTIFACT  (그라운드 리드가 만든 링잉, 판정은 spring 기준 PASS)
      둘 다 <= vmax                     → PASS
    반환 (verdict, 리포트 한 줄, info dict)"""
    a = analyze_capture(net + "_lead15cm")
    b = analyze_capture(net + "_spring")
    L_est = None
    if a["f_ring"]:
        L_est = lead_inductance(a["f_ring"], c_probe, a["zeta_est"] or 0.0)
    explained = L_est is not None and 50e-9 <= L_est <= 300e-9
    if b["peak"] > vmax:
        verdict = "REAL"
    elif a["peak"] > vmax:
        verdict = "ARTIFACT"
    else:
        verdict = "PASS"

    def ring_txt(m):
        return "ring=%.1fMHz" % (m["f_ring"] / 1e6) if m["f_ring"] else "ring=none"
    lead_txt = "lead15cm peak=%.2fV OS=%.1f%% %s" % (a["peak"], a["os_pct"], ring_txt(a))
    if L_est is not None:
        lead_txt += " -> L_est=%.0fnH%s" % (L_est * 1e9, " (ground-lead loop)" if explained else "")
    spr_txt = "spring peak=%.2fV OS=%.1f%% tr=%.2fns %s" % (b["peak"], b["os_pct"], b["tr"] * 1e9, ring_txt(b))
    tail = {"REAL": "FAIL: overshoot persists with spring tip (board SI, not the probe)",
            "ARTIFACT": "PASS on spring tip; lead ringing is a measurement artifact",
            "PASS": "PASS (edge too slow to excite the lead resonance)"}.get(verdict, "TODO")
    line = "%-8s %-9s | %s | %s | limit %.2fV -> %s" % (verdict, net, lead_txt, spr_txt, vmax, tail)
    return verdict, line, {"lead": a, "spring": b, "L_est": L_est, "explained": explained}


# ======================================================== 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 (ok, 설명) 을 돌려준다. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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


def chain_gauss(tr_sig, bw, dt=2e-12, n=4000, t0=3e-9):
    """가우시안 에지 + 가우시안 스코프: 출력도 가우시안 에지, sigma_tot^2 = sigma_sig^2 + sigma_scope^2.
    가우시안 응답의 tr*BW = 0.3396 (H(f)=exp(-2 pi^2 s^2 f^2) 의 -3dB 와 10-90% 폭에서)."""
    tr_scope = 0.3396 / bw
    tr_tot = math.sqrt(tr_sig ** 2 + tr_scope ** 2)
    t = [k * dt for k in range(n)]
    return t, [gauss_edge(x, t0, tr_tot) for x in t]


def chain_single_pole(tr_sig, bw, dt=1e-12, n=8000, t0=3e-9):
    t = [k * dt for k in range(n)]
    return t, single_pole_filter([gauss_edge(x, t0, tr_sig) for x in t], dt, bw)


def sp_bw_for_error(tr_sig, err):
    """단일 극점 스코프로 tr 오차가 err 가 되는 BW 를 시뮬레이션 + 이분법으로 찾는다."""
    lo, hi = 0.5e9, 5e9
    for _ in range(30):
        mid = math.sqrt(lo * hi)
        t, y = chain_single_pole(tr_sig, mid)
        if edge_measure(t, y)["tr"] / tr_sig - 1.0 > err:
            lo = mid
        else:
            hi = mid
    return hi


def run_tests():
    print("-- B04-lite tool sanity (given) --")

    def t_tool():
        t = [k * 5e-12 for k in range(3000)]
        m = edge_measure(t, [gauss_edge(x, 5e-9, 1e-9, 0.0, 3.3) for x in t])
        return near(m["tr"], 1e-9, 2e-12) and near(m["top"], 3.3, 1e-3), "ideal 1ns edge -> tr %.4f ns, top %.4f V" % (
            m["tr"] * 1e9, m["top"])
    check("edge_measure on an ideal 1ns Gaussian edge", t_tool)

    def t_load():
        n = [len(load_capture(net + "_" + p)[1]) for net in ("SPI0_SCLK", "QSPI_CLK", "FAN_PWM")
             for p in ("lead15cm", "spring")]
        return n == [1601] * 6, "samples per capture %s" % n
    check("6 captures load (1601 samples each)", t_load)

    print("-- Part A: rise-time budget --")
    check("rss(3,4) == 5", lambda: (near(rss(3.0, 4.0), 5.0, 1e-12), "%.6f" % rss(3.0, 4.0)))
    bw3 = [None]

    def a_bw():
        bw3[0] = required_system_bw(1e-9, 0.03)
        return near(bw3[0], 1.4184e9, 2e6), "1ns @3%% -> system BW %.4f GHz (tr_sys %.1f ps)" % (
            bw3[0] / 1e9, tr_from_bw(bw3[0]) * 1e12)
    check("required_system_bw(1ns, 3%) ~ 1.418 GHz", a_bw)
    check("rise_error 1ns via 1GHz / 500MHz",
          lambda: (near(rise_error(1e-9, [1e9]), 0.0595, 5e-4) and near(rise_error(1e-9, [500e6]), 0.2207, 5e-4),
                   "1GHz %.2f%%, 500MHz %.2f%%" % (100 * rise_error(1e-9, [1e9]), 100 * rise_error(1e-9, [500e6]))))
    check("rise_error at required BW == 3.00%",
          lambda: (near(rise_error(1e-9, [required_system_bw(1e-9, 0.03)]), 0.03, 1e-9),
                   "%.4f%%" % (100 * rise_error(1e-9, [required_system_bw(1e-9, 0.03)]))))
    check("required_scope_bw with 2.5GHz active probe ~ 1.72 GHz",
          lambda: (near(required_scope_bw(1e-9, 0.03, 2.5e9), 1.722e9, 5e6),
                   "%.3f GHz" % (required_scope_bw(1e-9, 0.03, 2.5e9) / 1e9)))
    check("500MHz passive probe alone blows the 3% budget -> None",
          lambda: (required_scope_bw(1e-9, 0.03, 500e6) is None,
                   "probe tr %.0f ps > budget %.0f ps" % (tr_from_bw(500e6) * 1e12,
                                                          1e-9 * math.sqrt(1.03 ** 2 - 1) * 1e12)))

    def a_gauss():
        t, y = chain_gauss(1e-9, required_system_bw(1e-9, 0.03, k=0.3396))
        e = edge_measure(t, y)["tr"] / 1e-9 - 1.0
        return near(e, 0.03, 0.002), "Gaussian scope (k=0.3396) measured error %.2f%%" % (100 * e)
    check("simulated Gaussian chain matches RSS (3%)", a_gauss)

    def a_sp():
        t, y = chain_single_pole(1e-9, bw3[0] or 1.4184e9)
        e = edge_measure(t, y)["tr"] / 1e-9 - 1.0
        return e > 0.035, "single-pole scope @1.418GHz measured error %.2f%% (RSS says 3.00%%)" % (100 * e)
    check("single-pole scope: RSS underestimates the error", a_sp)

    def a_bis():
        bw = sp_bw_for_error(1e-9, 0.03)
        return 1.5e9 < bw < 1.8e9, "single-pole BW for 3%% = %.3f GHz" % (bw / 1e9)
    check("bisection: single-pole needs more BW for 3%", a_bis)

    print("-- Part B: probe loading + ground-lead RLC --")
    p150 = series_rlc(33.0, 150e-9, 12e-12)
    p10 = series_rlc(33.0, 10e-9, 12e-12)
    check("150nH+12pF: f0 118.6MHz, Z0 111.8, zeta 0.148",
          lambda: (near(p150["f0"], 118.6e6, 0.1e6) and near(p150["z0"], 111.8, 0.1) and near(p150["zeta"], 0.1476, 5e-4),
                   "f0=%.1fMHz Z0=%.1fohm zeta=%.4f OS_step=%.1f%%" % (p150["f0"] / 1e6, p150["z0"], p150["zeta"], p150["os_pct"])))
    check("10nH spring: f0 459.4MHz, zeta 0.572, OS_step 11.2%",
          lambda: (near(p10["f0"], 459.4e6, 0.1e6) and near(p10["zeta"], 0.5716, 5e-4) and near(p10["os_pct"], 11.2, 0.1),
                   "f0=%.1fMHz Z0=%.1fohm zeta=%.4f OS_step=%.1f%%" % (p10["f0"] / 1e6, p10["z0"], p10["zeta"], p10["os_pct"])))

    def src1(t):
        return gauss_edge(t, 10e-9, 1e-9, 0.0, 3.3)
    sim = {}

    def run(L, C):
        key = (L, C)
        if key not in sim:
            sim[key] = simulate_probe(src1, 33.0, L, C, 10e6, 1e9)
        return sim[key]

    def b_os():
        ts, vs, _ = run(150e-9, 12e-12)
        ma = edge_measure(ts, vs)
        ts, vs, _ = run(10e-9, 12e-12)
        mb = edge_measure(ts, vs)
        ok = near(ma["os_pct"], 59.5, 1.0) and near(mb["os_pct"], 5.1, 0.5) and ma["os_pct"] > 10 * mb["os_pct"]
        return ok, "1ns edge, Rs=33: OS 150nH %.1f%% (peak %.2fV) vs spring %.1f%% (peak %.2fV)" % (
            ma["os_pct"], ma["peak"], mb["os_pct"], mb["peak"])
    check("simulated overshoot: 15cm lead vs spring tip", b_os)

    def b_ring():
        ts, vs, _ = run(150e-9, 12e-12)
        m = edge_measure(ts, vs)
        r = ring_analysis(ts, vs, m["top"], m["i50"], 0.02 * m["amp"])
        ok = near(r["f_ring"], p150["fd"], 0.01 * p150["fd"]) and near(r["zeta_est"], p150["zeta"], 0.1 * p150["zeta"])
        return ok, "f_ring %.2fMHz vs fd %.2fMHz, zeta_est %.4f vs %.4f, %d extrema" % (
            r["f_ring"] / 1e6, p150["fd"] / 1e6, r["zeta_est"], p150["zeta"], len(r["peaks"]))
    check("ring_analysis recovers fd and zeta (150nH)", b_ring)

    def b_L():
        ts, vs, _ = run(150e-9, 12e-12)
        m = edge_measure(ts, vs)
        r = ring_analysis(ts, vs, m["top"], m["i50"], 0.02 * m["amp"])
        L = lead_inductance(r["f_ring"], 12e-12, r["zeta_est"])
        return near(L, 150e-9, 3e-9), "L_est %.1f nH from f_ring + C=12pF" % (L * 1e9)
    check("L back-calculated from ringing ~ 150nH", b_L)

    def b_load():
        out = []
        for C in (1e-12, 12e-12, 100e-12):
            ts, vs, _ = run(10e-9, C)
            out.append(edge_measure(ts, vs)["tr"])
        ok = out[0] < 1.08e-9 and 1.08e-9 < out[1] < 1.2e-9 and out[2] > 5e-9
        return ok, "spring, tip C 1/12/100 pF -> tr %.2f / %.2f / %.2f ns (2.2*Rs*C = %.2f / %.2f / %.2f ns)" % (
            out[0] * 1e9, out[1] * 1e9, out[2] * 1e9, 2.2 * 33 * 1e-12 * 1e9, 2.2 * 33 * 12e-12 * 1e9, 2.2 * 33 * 100e-12 * 1e9)
    check("probe C loading slows the edge", b_load)

    def b_node():
        ts, vs, vn = run(150e-9, 12e-12)
        mn = edge_measure(ts, vn)
        return mn["os_pct"] > 5.0, "DUT node itself (not the scope) overshoots %.1f%% with 15cm lead attached" % mn["os_pct"]
    check("long lead also disturbs the DUT node", b_node)

    print("-- Part C: BRD-A rev0 captures --")
    res = {}

    def diag(net):
        if net not in res:
            res[net] = diagnose(net)
        return res[net]

    def c1():
        v, line, info = diag("SPI0_SCLK")
        print("    " + line)
        return v == "ARTIFACT" and info["explained"] and near(info["L_est"], 150e-9, 10e-9), "L_est %.0fnH" % (info["L_est"] * 1e9)
    check("SPI0_SCLK -> ARTIFACT (ground lead)", c1)

    def c2():
        v, line, info = diag("QSPI_CLK")
        print("    " + line)
        fr = info["spring"]["f_ring"]
        return v == "REAL" and near(fr, 250e6, 25e6), "spring ring %.1f MHz (board resonance)" % (fr / 1e6)
    check("QSPI_CLK -> REAL (planted: series R DNP)", c2)

    def c3():
        v, line, info = diag("FAN_PWM")
        print("    " + line)
        return v == "PASS" and info["lead"]["os_pct"] < 3.0, "20ns edge, lead OS %.2f%%" % info["lead"]["os_pct"]
    check("FAN_PWM -> PASS (slow edge, lead harmless)", c3)

    def c4():
        m = diag("SPI0_SCLK")[2]["spring"]
        return near(m["tr"], 1.12e-9, 0.05e-9), "spring tr %.3f ns (true 1.000 ns; 12pF loading + 1GHz)" % (m["tr"] * 1e9)
    check("SPI0_SCLK spring tr shows probe loading", c4)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
