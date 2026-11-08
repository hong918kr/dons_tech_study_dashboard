#!/usr/bin/env python3
"""HWLAB-B04 파형 기본 측정 알고리즘: 상태 레벨·Rise/Fall·Overshoot (IEEE 181)

스코프의 자동 측정(Top/Base, Rise/Fall, Overshoot, Freq, Duty, +Width)이 내부에서 하는 일을 직접 구현한다.
  Part A  히스토그램 mode 로 상태 레벨(base/top) → 10–90 % 기준 레벨 → 선형 보간 교차 → rise/fall,
          주기·주파수·펄스폭·duty (이상적인 사다리꼴 펄스 / RC 에지로 정답 확인)
  Part B  링잉·잡음이 있는 2차(RLC) 응답 펄스에서 min/max 기반 순진한 방법과 오차 비교
          (해석해: overshoot = exp(-πζ/√(1-ζ²)), 10–90 % rise 는 해석식을 이분법으로)
  Part C  VALBRD-A 두 장의 MCO_8M 캡처(CSV)로 bring-up 판정 리포트 — R47 미실장 보드를 잡아낸다

  python3 -I solution.py        # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --report SN0042_tp31_mco8m.csv   # 캡처 하나에 대한 측정 리포트만 출력

Python 3.9+, 표준 라이브러리만 (math, random, os, sys).
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- 파형 모델 (주어짐)
def step2(t, zeta, wn):
    """2차 저역 시스템(직렬 RLC 등)의 단위 계단 응답, 0 <= zeta < 1. t <= 0 이면 0."""
    if t <= 0.0:
        return 0.0
    wd = wn * math.sqrt(1.0 - zeta * zeta)
    k = zeta / math.sqrt(1.0 - zeta * zeta)
    return 1.0 - math.exp(-zeta * wn * t) * (math.cos(wd * t) + k * math.sin(wd * t))


def step_rc(t, tau):
    """1차(RC) 단위 계단 응답."""
    return 0.0 if t <= 0.0 else 1.0 - math.exp(-t / tau)


def ramp(t, tr):
    """선형 램프 에지: t=0 이 50 % 지점, 0→1 까지 tr 초 (10–90 % = 0.8·tr)."""
    return min(1.0, max(0.0, t / tr + 0.5))


def synth(dt, n, edges, base, top, resp, noise_sd=0.0, seed=1):
    """edges = [(시각, +1/-1)], resp(τ) = 0→1 단위 응답. 선형 중첩 + 백색 잡음."""
    rng = random.Random(seed)
    t = [i * dt for i in range(n)]
    v = []
    for ti in t:
        x = 0.0
        for te, sgn in edges:
            x += sgn * resp(ti - te)
        v.append(base + (top - base) * x + (rng.gauss(0.0, noise_sd) if noise_sd else 0.0))
    return t, v


def pulse_edges(t_first, period, width, count):
    out = []
    for k in range(count):
        out.append((t_first + k * period, +1))
        out.append((t_first + k * period + width, -1))
    return out


def analytic_rise2(zeta, wn, lo=0.1, hi=0.9):
    """2차 계단 응답의 lo→hi 도달 시간 (첫 피크 π/ωd 전까지 단조 증가 → 이분법)."""
    def first_time(level):
        a, b = 0.0, math.pi / (wn * math.sqrt(1 - zeta * zeta))
        for _ in range(100):
            m = 0.5 * (a + b)
            if step2(m, zeta, wn) < level:
                a = m
            else:
                b = m
        return 0.5 * (a + b)
    return first_time(hi) - first_time(lo)


def analytic_os2(zeta):
    """2차 계단 응답의 overshoot 비율 (0.1 = 10 %)."""
    return math.exp(-math.pi * zeta / math.sqrt(1 - zeta * zeta))


def load_csv(path):
    """스코프 CSV → (t, v). '#' 주석 줄과 숫자가 아닌 헤더 줄은 건너뛴다."""
    t, v = [], []
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.strip()
            if not s or s[0] == "#":
                continue
            a, b = s.split(",")[:2]
            try:
                t.append(float(a))
                v.append(float(b))
            except ValueError:
                continue                     # "time_s,ch1_v" 같은 헤더
    return t, v


# ---------------------------------------------------------------- Part A: 상태 레벨·교차·전이
def state_levels(v, nbins=200):
    """히스토그램 mode 로 (base, top).

    min..max 를 nbins 칸으로 나누고, 아래 절반 칸들 중 최빈 칸 → base, 위 절반 중 최빈 칸 → top.
    칸 중심값은 칸 폭/2 만큼 틀릴 수 있으므로, 최빈 칸 ±1 칸에 든 샘플들의 평균으로 다듬는다.
    """
    # TODO ① min..max 를 nbins 칸으로 나눠 칸마다 샘플 수를 센다 (x == vmax 는 마지막 칸으로)
    # TODO ② 아래 절반 칸 중 최빈 칸 → base, 위 절반 칸 중 최빈 칸 → top (동률이면 앞쪽 칸)
    # TODO ③ 최빈 칸 ±1 칸(3칸 폭)에 든 샘플들의 평균으로 다듬는다
    return min(v), max(v)  # 지금은 naive(min/max) 와 같다 — 이걸 바꾸는 게 이 레슨의 핵심


def cross_time(t, v, i, level, interp=True):
    """v[i] 와 v[i+1] 사이에서 level 을 지나는 시각. interp=False 면 '넘어선 첫 샘플' 시각."""
    # TODO: interp=False 면 t[i+1]. 아니면 (t[i],v[i])–(t[i+1],v[i+1]) 직선이 level 을 지나는 시각 (dv == 0 이면 t[i])
    return t[i + 1]


def transitions(t, v, base, top, lo=0.10, hi=0.90, interp=True):
    """히스테리시스 상태기계로 전이(에지)를 찾는다.

    L = base + lo·A, M = base + 0.5·A, H = base + hi·A  (A = top - base)
    상태는 v <= L 이면 LOW, v >= H 이면 HIGH. LOW→HIGH 가 되면 상승 전이 하나:
      t_lo  = 마지막 LOW 샘플과 다음 샘플 사이의 L 교차 (L 을 *마지막으로* 지난 시각)
      t_hi  = HIGH 가 된 샘플과 그 앞 샘플 사이의 H 교차 (H 를 *처음* 지난 시각)
      t_mid = 그 구간에서 M 을 처음 지난 시각
    하강은 대칭 (H 를 마지막으로, L 을 처음으로). 링잉이 H 아래로 잠깐 내려와도 L 까지 안 가면 새 전이가 아니다.
    반환: dict(dir=+1/-1, t_lo, t_mid, t_hi, i0, i1) 목록 (i0..i1 = 전이 구간 샘플 인덱스)
    """
    A = top - base
    L, M, H = base + lo * A, base + 0.5 * A, base + hi * A
    out = []
    # TODO: state(None/"L"/"H"), last_low, last_high 를 들고 샘플을 앞에서부터 훑는다.
    #   v <= L : 직전 state 가 "H" 면 하강 전이 하나 추가 → state="L", last_low=i
    #   v >= H : 직전 state 가 "L" 면 상승 전이 하나 추가 → state="H", last_high=i
    #   전이 dict(dir, t_lo, t_mid, t_hi, i0, i1) — 교차 시각은 cross_time(t, v, j, level, interp)
    return out


def edge_durations(trs, direction):
    """상승(+1)이면 t_hi - t_lo, 하강(-1)이면 t_lo - t_hi — 둘 다 양수 (10→90 / 90→10)."""
    return [(tr["t_hi"] - tr["t_lo"]) * direction for tr in trs if tr["dir"] == direction]


def pulse_stats(trs):
    """50 % 교차 시각으로 주기·주파수·양(+)펄스폭·duty.

    period = 첫·마지막 상승 t_mid 사이 / (상승 수 - 1)   (= 연속 주기들의 평균)
    width  = 각 상승 t_mid 부터 바로 다음 하강 t_mid 까지의 평균
    duty   = width / period · 100 [%]
    상승 에지가 2개 미만이면 None.
    """
    # TODO: 상승/하강 t_mid 목록 → period, freq, width(각 상승 → 바로 다음 하강, 평균), duty[%]
    return None


# ---------------------------------------------------------------- Part B: aberration, 두 방법 비교
def aberrations(v, trs, base, top):
    """에지별 post-transition 구간(전이 끝 i1 → 다음 전이 시작 i0, 없으면 레코드 끝)에서
    상승 후 overshoot  = (max - top) / A · 100,   하강 후 undershoot = (base - min) / A · 100.
    반환 (최악 overshoot %, 최악 undershoot %, 레코드 전체 vmax, vmin). 해당 에지가 없으면 0.0.
    """
    # TODO: 전이 k 의 post-transition 구간 = v[trs[k]["i1"] : (다음 전이 i0, 없으면 len(v)-1) + 1]
    #       상승 → (max - top)/A·100,  하강 → (base - min)/A·100, 각각 에지들 중 최악값
    return 0.0, 0.0, max(v), min(v)


def measure(t, v, method="ieee", interp=True, nbins=200):
    """스코프 '자동 측정' 한 벌. method='ieee' → 히스토그램 상태 레벨, 'naive' → base=min, top=max."""
    if method == "ieee":
        base, top = state_levels(v, nbins)
    else:
        base, top = min(v), max(v)
    trs = transitions(t, v, base, top, interp=interp)
    rises, falls = edge_durations(trs, +1), edge_durations(trs, -1)
    os_pct, us_pct, vmax, vmin = aberrations(v, trs, base, top)
    ps = pulse_stats(trs) or {}
    return dict(method=method, base=base, top=top, amp=top - base, n_edges=len(trs),
                rise=sum(rises) / len(rises) if rises else None,
                fall=sum(falls) / len(falls) if falls else None,
                overshoot=os_pct, undershoot=us_pct, vmax=vmax, vmin=vmin,
                freq=ps.get("freq"), period=ps.get("period"), width=ps.get("width"), duty=ps.get("duty"))


# ---------------------------------------------------------------- Part C: bring-up 판정
SPEC_MCO8M = dict(
    rise_max=5e-9, fall_max=5e-9,          # 10–90 % ≤ 5 ns (수신 칩 클럭 입력 요구)
    os_max=10.0, us_max=10.0,              # 진폭 대비 ≤ 10 %
    vmax_abs=3.6, vmin_abs=-0.3,           # 수신 칩 절대 최대 정격 VDDIO+0.3 / VSS-0.3
    freq=(8e6 * 0.99, 8e6 * 1.01),         # 스코프 타임베이스 정확도 수준의 확인 (ppm 은 카운터로)
    duty=(45.0, 55.0),
)


def judge(m, spec):
    """측정 dict + 스펙 → 항목별 (이름, 측정값 문자열, 한계 문자열, ok) 목록과 전체 verdict('PASS'/'FAIL')."""
    # TODO: rise, fall, overshoot, undershoot, Vmax, Vmin, freq, duty 8 항목을 이 순서로
    #       (이름, 측정값 문자열, 한계 문자열, ok)  예: ("overshoot", "23.9%", "<= 10.0%", False)
    #       verdict = 모든 항목이 ok 면 "PASS", 아니면 "FAIL"
    items = []
    return items, "TODO"


def report(label, t, v, spec):
    """bring-up 리포트 문자열: IEEE 방식 판정 + 같은 캡처를 naive(min/max)로 판정했으면 어땠을지."""
    m = measure(t, v, "ieee")
    nv = measure(t, v, "naive")
    items, verdict = judge(m, spec)
    nitems, nverdict = judge(nv, spec)
    lines = ["%s -> %s  base=%.3fV top=%.3fV amp=%.3fV edges=%d"
             % (label, verdict, m["base"], m["top"], m["amp"], m["n_edges"])]
    for name, val, lim, ok in items:
        lines.append("  %-4s %-10s = %-10s %s" % ("ok" if ok else "BAD", name, val, lim))
    lines.append("  naive(min/max): top=%.3fV overshoot=%.1f%% rise=%.2fns -> %s, BAD=%s"
                 % (nv["top"], nv["overshoot"], nv["rise"] * 1e9, nverdict,
                    ",".join(n for n, _, _, ok in nitems if not ok) or "-"))
    return "\n".join(lines), verdict, m, nv


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


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def ns(x):
    return "None" if x is None else "%.4fns" % (x * 1e9)


def safe(fn, default=None):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def run_tests():
    # ---- Part A: 이상적인 사다리꼴 펄스. 0→3.3 V, 램프 5 ns(10–90 = 4 ns), 주기 100 ns, 폭 40 ns, 5 펄스
    print("-- Part A: state levels / crossings / transitions --")
    dt = 0.1e-9
    tA, vA = synth(dt, 5200, pulse_edges(10e-9, 100e-9, 40e-9, 5), 0.0, 3.3, lambda x: ramp(x, 5e-9))

    def lv():
        b, tp = state_levels(vA)
        return near(b, 0.0, 1e-3) and near(tp, 3.3, 1e-3), "base=%.4f top=%.4f exp 0/3.3" % (b, tp)
    check("state_levels(trapezoid) == (0, 3.3) +-1mV", lv)
    check("cross_time interpolates (0V@0ns, 2V@1ns, level 0.5V -> 0.25ns)",
          lambda: near(cross_time([0.0, 1e-9], [0.0, 2.0], 0, 0.5), 0.25e-9, 1e-15))
    check("cross_time(interp=False) -> first sample past the level",
          lambda: cross_time([0.0, 1e-9], [0.0, 2.0], 0, 0.5, interp=False) == 1e-9)
    trA = safe(lambda: transitions(tA, vA, 0.0, 3.3), [])
    check("transitions: 5 rising + 5 falling, alternating",
          lambda: ([tr["dir"] for tr in trA] == [1, -1] * 5, "dirs=%s" % [tr["dir"] for tr in trA]))
    mA = safe(lambda: measure(tA, vA), {})
    check("rise 10-90 == 0.8 * 5ns = 4.000ns", lambda: (near(mA["rise"], 4e-9, 1e-12), "rise=" + ns(mA["rise"])))
    check("fall 90-10 == 4.000ns", lambda: (near(mA["fall"], 4e-9, 1e-12), "fall=" + ns(mA["fall"])))
    check("period 100ns / freq 10MHz", lambda: (near(mA["period"], 100e-9, 1e-12) and near(mA["freq"], 10e6, 1.0),
                                                "period=" + ns(mA["period"])))
    check("+width 40ns, duty 40%", lambda: (near(mA["width"], 40e-9, 1e-12) and near(mA["duty"], 40.0, 1e-3),
                                            "width=%s duty=%.4f%%" % (ns(mA["width"]), mA["duty"])))
    tau = 1e-9
    tR, vR = synth(0.01e-9, 2000, [(2e-9, +1)], 0.0, 1.0, lambda x: step_rc(x, tau))
    rR = safe(lambda: edge_durations(transitions(tR, vR, 0.0, 1.0), +1), [])
    check("RC edge: 10-90 == tau*ln9 = 2.1972ns (+-0.2%)",
          lambda: (len(rR) == 1 and near(rR[0], tau * math.log(9), 0.002 * tau * math.log(9)),
                   "rise=%s" % (ns(rR[0]) if rR else rR)))

    # ---- Part B: 2차 응답(ζ=0.6, fn=150 MHz) 펄스 + 10 mV rms 잡음 — 해석해와 두 방법 비교
    print("-- Part B: ringing + noise, IEEE-181 histogram vs naive min/max --")
    zeta, wn = 0.6, 2 * math.pi * 150e6
    tB, vB = synth(dt, 5200, pulse_edges(10e-9, 100e-9, 50e-9, 5), 0.0, 3.3,
                   lambda x: step2(x, zeta, wn), noise_sd=0.010, seed=181)
    true_rise, true_os = analytic_rise2(zeta, wn), analytic_os2(zeta) * 100
    mi, mn = safe(lambda: measure(tB, vB, "ieee"), {}), safe(lambda: measure(tB, vB, "naive"), {})
    safe(lambda: print("      truth: rise=%s overshoot=%.2f%% | ieee: rise=%s os=%.2f%% top=%.4f | naive: rise=%s os=%.2f%% top=%.4f"
          % (ns(true_rise), true_os, ns(mi["rise"]), mi["overshoot"], mi["top"],
             ns(mn["rise"]), mn["overshoot"], mn["top"])))
    check("ieee state levels within 10mV of 0 / 3.3V",
          lambda: (near(mi["base"], 0.0, 0.010) and near(mi["top"], 3.3, 0.010),
                   "base=%.4f top=%.4f" % (mi["base"], mi["top"])))
    check("ieee rise within 3% of analytic", lambda: (near(mi["rise"], true_rise, 0.03 * true_rise),
                                                      "err=%+.2f%%" % ((mi["rise"] / true_rise - 1) * 100)))
    check("ieee overshoot within 1 pct-pt of exp(-pi*z/sqrt(1-z^2))",
          lambda: (near(mi["overshoot"], true_os, 1.0), "%.2f%% vs %.2f%%" % (mi["overshoot"], true_os)))
    check("ieee undershoot (falling) within 1 pct-pt (linear: same as overshoot)",
          lambda: (near(mi["undershoot"], true_os, 1.0), "%.2f%%" % mi["undershoot"]))
    check("naive overshoot is 0 by construction (top == max)",
          lambda: (mn["overshoot"] <= 1e-9, "%.3f%%" % mn["overshoot"]))
    check("naive amplitude inflated > 15%",
          lambda: (mn["amp"] / 3.3 - 1 > 0.15, "amp=%.3fV (%+.1f%%)" % (mn["amp"], (mn["amp"] / 3.3 - 1) * 100)))
    check("naive rise error > 10% vs analytic",
          lambda: (mn["rise"] / true_rise - 1 > 0.10, "err=%+.1f%%" % ((mn["rise"] / true_rise - 1) * 100)))

    z2 = 0.45                                           # overshoot ≈ 20.5 % > 12.5 % → naive 10 % 레벨이 base 아래
    tC, vC = synth(dt, 5200, pulse_edges(10e-9, 100e-9, 50e-9, 5), 0.0, 3.3,
                   lambda x: step2(x, z2, wn), noise_sd=0.010, seed=182)
    tr2 = analytic_rise2(z2, wn)
    mi2, mn2 = safe(lambda: measure(tC, vC, "ieee"), {}), safe(lambda: measure(tC, vC, "naive"), {})
    check("zeta=0.45 (OS 20%): ieee rise still within 3%",
          lambda: (near(mi2["rise"], tr2, 0.03 * tr2), "ieee=%s truth=%s" % (ns(mi2["rise"]), ns(tr2))))
    check("zeta=0.45: naive rise > 5x truth (10% level fell below base -> previous edge ringing)",
          lambda: (mn2["rise"] > 5 * tr2, "naive=%s truth=%s" % (ns(mn2["rise"]), ns(tr2))))

    tS, vS = synth(0.5e-9, 60, [(5e-9, +1)], 0.0, 1.0, lambda x: step_rc(x, tau))   # 2 GS/s, RC τ=1 ns
    truth = tau * math.log(9)

    def interp_vs_not():
        a = edge_durations(transitions(tS, vS, 0.0, 1.0, interp=True), +1)[0]
        b = edge_durations(transitions(tS, vS, 0.0, 1.0, interp=False), +1)[0]
        ea, eb = abs(a - truth), abs(b - truth)
        return ea < 0.05 * truth and eb > 2 * ea, "interp err=%.1fps, no-interp err=%.1fps" % (ea * 1e12, eb * 1e12)
    check("coarse sampling (0.5ns, RC 2.2ns): interpolation error << sample-snap error", interp_vs_not)

    tN, vN = synth(dt, 5200, pulse_edges(10e-9, 100e-9, 50e-9, 5), 0.0, 3.3, lambda x: ramp(x, 5e-9),
                   noise_sd=0.020, seed=7)

    def noise_only():
        b, tp = state_levels(vN)
        return (abs(tp - 3.3) < 0.005 and max(vN) - 3.3 > 0.050,
                "ieee top=%.4f, naive top(max)=%.4f (noise 20mV rms)" % (tp, max(vN)))
    check("no ringing, 20mV noise: ieee top +-5mV, naive max > +50mV", noise_only)

    # ---- Part C: VALBRD-A rev B, MCO_8M @ TP31, 두 보드
    print("-- Part C: bring-up report (VALBRD-A MCO_8M @ TP31) --")
    caps = {}
    for sn in ("SN0041", "SN0042"):
        caps[sn] = load_csv(os.path.join(HERE, "%s_tp31_mco8m.csv" % sn))
    check("captures loaded: 5000 samples @ 0.2ns each",
          lambda: all(len(c[1]) == 5000 and near(c[0][1] - c[0][0], 0.2e-9, 1e-15) for c in caps.values()))
    res = {}
    for sn, (t, v) in caps.items():
        res[sn] = safe(lambda: report("VALBRD-A %s MCO_8M@TP31" % sn, t, v, SPEC_MCO8M))
        if res[sn]:
            print(res[sn][0])
    check("SN0041 (R47=33R) -> PASS", lambda: res["SN0041"][1] == "PASS")
    check("SN0042 (R47=0R) -> FAIL", lambda: res["SN0042"][1] == "FAIL")

    def failed_items():
        items, _ = judge(res["SN0042"][2], SPEC_MCO8M)
        bad = [n for n, _, _, ok in items if not ok]
        return bad == ["overshoot", "undershoot", "Vmax", "Vmin"], "failed=%s" % bad
    check("SN0042 fails exactly overshoot/undershoot/Vmax/Vmin (timing OK)", failed_items)
    check("SN0042 naive(min/max) overshoot reads 0% (would hide the defect)",
          lambda: (res["SN0042"][3]["overshoot"] <= 1e-9, "%.2f%%" % res["SN0042"][3]["overshoot"]))
    check("SN0042 freq 8MHz +-0.1%, duty 49 +-0.5%",
          lambda: (near(res["SN0042"][2]["freq"], 8e6, 8e3) and near(res["SN0042"][2]["duty"], 49.0, 0.5),
                   "freq=%.5fMHz duty=%.2f%%" % (res["SN0042"][2]["freq"] / 1e6, res["SN0042"][2]["duty"])))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 3 and argv[1] == "--report":
        t, v = load_csv(argv[2])
        print(report(os.path.basename(argv[2]), t, v, SPEC_MCO8M)[0])
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
