#!/usr/bin/env python3
"""HWLAB-B09 추가 연습 — 같은 캡처·저항표로 bring-up 엔지니어가 더 뽑아내는 숫자들

  연습 1  estimate_cin(cap)          : 첫 상승 구간(UVLO 이전) I/(dV/dt) 로 보드 입력 커패시턴스 추정 → BOM 과 비교
  연습 2  inrush_i2t(cap)            : inrush 구간 I²t → 입력 퓨즈의 펄스 내구(derating) 대비 여유
  연습 3  rel_correct(reading, lead) : 2-wire 리드 저항 보정(REL) 이 저옴 레일의 SHORT 판정을 바꾸는 경우
  연습 4  hiccup_period(cap)         : 전류 제한에 묶여 재기동을 반복하는 보드의 hiccup 주기

  python3 -I practice.py    # "N/M passed", 실패 시 exit 1
"""
import math
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OL = math.inf


# ---- 본문(solution.py)에서 필요한 부분만 축약해 넣었다 (레슨 디렉터리끼리 import 하지 않는다) ----
def load_capture(name):
    meta, t, v, i = {}, [], [], []
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            if ln.startswith("#"):
                k, _, val = ln[1:].strip().partition("=")
                meta[k.strip()] = val.strip()
            elif ln[0].isdigit() or ln[0] == "-":
                a, b, c = ln.split(",")
                t.append(int(a) * 1e-6)
                v.append(float(b))
                i.append(float(c))
    return meta, t, v, i


def opening(x, k=3):
    n = len(x)
    h = k // 2
    ero = [min(x[max(0, j - h):min(n, j - h + k)]) for j in range(n)]
    return [max(ero[max(0, j - (k - 1 - h)):min(n, j - (k - 1 - h) + k)]) for j in range(n)]


def classify_rail(golden, dut, k=4.0, short_ratio=0.2, rel_floor=0.05, abs_floor=0.05):
    xs = sorted(x for x in golden if not math.isinf(x))
    med = statistics.median(xs)
    mad = statistics.median(abs(x - med) for x in xs)
    sigma = max(1.4826 * mad, rel_floor * med, abs_floor)
    ratio, z = dut / med, (dut - med) / sigma
    if ratio <= short_ratio and z <= -k:
        return "SHORT", ratio, z
    if z <= -k:
        return "LOW", ratio, z
    if z >= k:
        return "HIGH", ratio, z
    return "OK", ratio, z


# ---------------------------------------------------------------- 연습 1
def estimate_cin(cap, v_lo=0.5, v_hi=4.0):
    """t>=0 부터 V 가 처음 v_hi 를 넘을 때까지(DC/DC 가 아직 꺼져 있는 UVLO 이전),
    v_lo < V 인 샘플마다 C = I / (dV/dt) (중앙차분) → 중앙값. 블리더 전류(V/2k ≤ 2 mA)는 무시한다."""
    _, t, v, i = cap
    dt = t[1] - t[0]
    cs = []
    for j in range(1, len(t) - 1):
        if t[j] < 0:
            continue
        if v[j] > v_hi:
            break
        if v[j] > v_lo:
            dvdt = (v[j + 1] - v[j - 1]) / (2 * dt)
            if dvdt > 0:
                cs.append(i[j] / dvdt)
    return statistics.median(cs), len(cs)


# ---------------------------------------------------------------- 연습 2
def inrush_window(cap, k=3, window=10e-3, th_frac=0.1):
    """본문 measure_inrush 와 같은 정의의 inrush 구간 인덱스 (j0, j1) 와 i_ss"""
    _, t, v, i = cap
    tail = [iv for tv, iv in zip(t, i) if tv >= t[-1] - window + 1e-12]
    i_ss = statistics.median(tail)
    io = opening(i, k)
    i_th = i_ss + th_frac * (max(io) - i_ss)
    hit = [j for j in range(len(t)) if io[j] >= i_th and t[j] < t[-1] - window]
    return hit[0], hit[-1], i_ss


def inrush_i2t(cap):
    """inrush 구간의 ∫ i² dt [A²s] (원시 전류 — 글리치는 구간 밖)"""
    _, t, _, i = cap
    j0, j1, _ = inrush_window(cap)
    dt = t[1] - t[0]
    return sum(i[j] * i[j] for j in range(j0, j1 + 1)) * dt


def fuse_check(i2t_pulse, fuse_melt_i2t, derate):
    """펄스 I²t ≤ derate × 퓨즈 공칭 용단 I²t 이면 OK. (판정, 사용률)"""
    use = i2t_pulse / (derate * fuse_melt_i2t)
    return ("OK" if use <= 1.0 else "FAIL"), use


# ---------------------------------------------------------------- 연습 3
def rel_correct(reading, lead):
    """2-wire 측정값에서 리드(+접촉) 저항을 뺀다. 음수가 되면 0 (분해능 이하)."""
    return max(0.0, reading - lead)


# ---------------------------------------------------------------- 연습 4
def hiccup_period(cap, t_from=3e-3, hyst=0.2):
    """t >= t_from 에서 V 가 (min+max)/2 를 히스테리시스 ±hyst/2 로 위로 지나는 시각들 → 간격 중앙값, 횟수"""
    _, t, v, _ = cap
    seg = [(tv, vv) for tv, vv in zip(t, v) if tv >= t_from]
    mid = (min(vv for _, vv in seg) + max(vv for _, vv in seg)) / 2
    hi, lo = mid + hyst / 2, mid - hyst / 2
    state, cross = 0, []
    for tv, vv in seg:
        if state == 0 and vv > hi:
            cross.append(tv)
            state = 1
        elif state == 1 and vv < lo:
            state = 0
    gaps = [b - a for a, b in zip(cross, cross[1:])]
    return statistics.median(gaps), len(cross)


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def near(x, ref, tol):
    print("      measured %s  expected %s ± %s" % (x, ref, tol))
    return abs(x - ref) <= tol


def main():
    gold = load_capture("cap_golden_G1_3A0.csv")
    d50 = load_capture("cap_dut_0A50.csv")
    d90 = load_capture("cap_dut_0A90.csv")

    print("-- 연습 1: C_in from inrush --")
    cg, ng = estimate_cin(gold)
    c50, n50 = estimate_cin(d50)
    c90, _ = estimate_cin(d90)
    check("golden C_in ~= 220 uF (BOM) from %d samples" % ng, lambda: near(cg * 1e6, 220.0, 8.0))
    check("SN0007 C_in ~= 215 uF from the CC-clipped 0.50 A capture (%d samples)" % n50,
          lambda: near(c50 * 1e6, 215.0, 8.0))
    check("SN0007 C_in agrees between 0.50 A and 0.90 A captures (< 3%)", lambda: abs(c50 / c90 - 1) < 0.03)
    check("within BOM 220 uF ±20% -> no missing bulk cap", lambda: all(0.8 * 220e-6 <= c <= 1.2 * 220e-6
                                                                    for c in (cg, c50, c90)))

    print("-- 연습 2: fuse I2t --")
    i2t = inrush_i2t(gold)
    check("golden inrush I2t ~= 2.7^2 A^2 * 1 ms = 7.3e-3 A^2s", lambda: near(i2t * 1e3, 7.3, 0.4))
    verdict, use = fuse_check(i2t, fuse_melt_i2t=0.10, derate=0.22)   # 가정 값 (본문 설명 참고)
    print("      F1: pulse %.2e A^2s vs 22%% x 0.10 = %.3f A^2s -> %s (%.0f%% used)" % (i2t, 0.022, verdict, 100 * use))
    check("F1 (assumed 0.10 A^2s, 22% derate) -> OK, about 33% used", lambda: verdict == "OK" and near(use, 0.33, 0.03))
    check("same pulse vs a smaller fuse (0.02 A^2s) -> FAIL",
          lambda: fuse_check(i2t, 0.02, 0.22)[0] == "FAIL")

    print("-- 연습 3: lead resistance (REL) on a low-ohm rail --")
    core_golden = [1.82, 1.91, 1.97, 1.86, 1.94]
    raw = 0.42                       # REL 없이 잰 DUT 값 (리드+접촉 0.25 ohm 포함)
    v_raw = classify_rail(core_golden, raw)
    v_rel = classify_rail(core_golden, rel_correct(raw, 0.25))
    print("      raw %.2f -> %s ratio %.3f ; REL %.2f -> %s ratio %.3f"
          % (raw, v_raw[0], v_raw[1], rel_correct(raw, 0.25), v_rel[0], v_rel[1]))
    check("without REL: 0.42 ohm vs golden 1.91 -> LOW (ratio 0.22 hides the short)", lambda: v_raw[0] == "LOW")
    check("with REL 0.25: 0.17 ohm -> SHORT (ratio 0.089)", lambda: v_rel[0] == "SHORT")
    check("rel_correct never goes negative", lambda: rel_correct(0.1, 0.25) == 0.0)

    print("-- 연습 4: hiccup period --")
    per, n = hiccup_period(d50)
    print("      %d restarts, median period %.2f ms (%.0f Hz)" % (n, per * 1e3, 1 / per))
    check("0.50 A capture hiccups: >= 10 restarts in 37 ms", lambda: n >= 10)
    check("hiccup period 1.5-3.5 ms (recharge 4.0->4.5 V + soft-start until P/V hits 0.5 A)",
          lambda: 1.5e-3 <= per <= 3.5e-3)
    check("0.90 A capture: no hiccup (V flat after start)",
          lambda: max(v for tv, v in zip(d90[1], d90[2]) if tv > 10e-3)
          - min(v for tv, v in zip(d90[1], d90[2]) if tv > 10e-3) < 0.1)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
