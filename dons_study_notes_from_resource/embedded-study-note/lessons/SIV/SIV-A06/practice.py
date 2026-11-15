#!/usr/bin/env python3
"""SIV-A06 추가 연습 — 한 번에 검증. python3 -I practice.py (Python 3.9 stdlib only)"""
import math
import random
import sys

_pass = _fail = 0


def CHECK(name, cond, detail=""):
    global _pass, _fail
    if cond:
        _pass += 1
        print("PASS %s" % name)
    else:
        _fail += 1
        print("FAIL %s: %s" % (name, detail))


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def first_crossing(t, v, level, rising=True, hold=1):
    sign = 1 if rising else -1
    for i in range(1, len(v)):
        if sign * (v[i - 1] - level) < 0 <= sign * (v[i] - level):
            if all(sign * (x - level) >= 0 for x in v[i:i + hold]) and i + hold <= len(v):
                return t[i - 1] + (level - v[i - 1]) * (t[i] - t[i - 1]) / (v[i] - v[i - 1])
    return None


# ------------------------------------------------------------ 연습 1
def repeats_to_catch(width, Ts, conf):
    """폭 width의 단발 glitch를 point 샘플링으로 신뢰도 conf 이상 한 번은 잡는 데 필요한 반복 횟수.
    반복마다 샘플 위상이 독립이라고 가정 (전원 사이클마다 위상이 무작위인 모델)."""
    if width >= Ts:
        return 1
    p = width / Ts
    return int(math.ceil(math.log(1.0 - conf) / math.log(1.0 - p)))


# ------------------------------------------------------------ 연습 2
def deskew(rows_t, channel_index, t_conv):
    """다중화 ADC: 스캔 타임스탬프 t, 채널 k는 t + k*t_conv 에 실제 샘플."""
    return [x + channel_index * t_conv for x in rows_t]


def mux_sample(wave, t_scan, channel_index, t_conv):
    return [wave(t + channel_index * t_conv) for t in t_scan]


def ramp_fn(t_on, T, nom):
    def f(t):
        return 0.0 if t < t_on else nom * min(1.0, (t - t_on) / T)
    return f


# ------------------------------------------------------------ 연습 3
def check_reset_release(t_rails_ok, t_reset, reset_v, vih, min_us):
    """모든 레일이 대역 안에 들어온 시각들 t_rails_ok(dict) 이후 min_us 뒤에 RESET_N이 VIH를 넘어야 한다."""
    last = max(t_rails_ok.values())
    tr = first_crossing(t_reset, reset_v, vih, True, hold=2)
    if tr is None:
        return ("FAIL", None, "RESET_N never released")
    dt = tr - last
    return ("PASS" if dt >= min_us else "FAIL", dt, "last rail ok at %.1f" % last)


# ------------------------------------------------------------ 연습 4
def lint_rules(rules, rails):
    """rules=[(a,b)...] 위상 정렬. 사이클이면 ValueError, 없는 레일이면 ValueError. 반환: 결정적 순서(동률은 이름순)."""
    if any(a not in rails or b not in rails for a, b in rules):
        raise ValueError("unknown rail")
    indeg = {r: 0 for r in rails}
    succ = {r: [] for r in rails}
    for a, b in rules:
        succ[a].append(b)
        indeg[b] += 1
    ready = sorted(r for r in rails if indeg[r] == 0)
    order = []
    while ready:
        r = ready.pop(0)
        order.append(r)
        for s in succ[r]:
            indeg[s] -= 1
            if indeg[s] == 0:
                ready.append(s)
        ready.sort()
    if len(order) != len(rails):
        raise ValueError("cycle in rules")
    return order


# ------------------------------------------------------------ 연습 5
def ssc_gate_error_ppm(spread_ppm, mod_hz, gate_s, phase=0.0, steps=20000):
    """삼각 다운스프레드(0 → -S → 0, 주기 1/mod_hz)를 gate_s 동안 평균했을 때의 평균 오차(ppm)와 -S/2의 차.
    게이트가 정수 주기면 0, 부분 주기면 잔차가 남는다."""
    period = 1.0 / mod_hz
    total = 0.0
    for i in range(steps):
        x = ((i + 0.5) / steps * gate_s / period + phase) % 1.0
        tri = 2.0 * x if x < 0.5 else 2.0 * (1.0 - x)
        total += -spread_ppm * tri
    mean = total / steps
    return mean - (-spread_ppm / 2.0)


def expected_gate_error(spread_ppm, n_periods):
    """부분 주기 잔차의 대략적 상한: S / (4 * N) (N = 게이트 안의 변조 주기 수)."""
    return spread_ppm / (4.0 * n_periods)


def main():
    # ---- 연습 1
    CHECK("p1.W_ge_Ts", repeats_to_catch(10.0, 10.0, 0.99) == 1)
    n = repeats_to_catch(1.0, 10.0, 0.95)
    CHECK("p1.formula_N", n == 29, str(n))  # ln(0.05)/ln(0.9) = 28.43 → 29
    rng = random.Random(31)
    for (w, ts, conf) in [(1.0, 10.0, 0.95), (2.5, 10.0, 0.99), (0.5, 10.0, 0.9)]:
        N = repeats_to_catch(w, ts, conf)
        trials = 3000
        hit = sum(1 for _ in range(trials) if any(rng.random() * ts < w for _ in range(N)))
        # 몬테카를로 표준오차 ≈ sqrt(p(1-p)/3000) < 0.01, 여유 0.02
        CHECK("p1.mc w=%.1f conf=%.2f" % (w, conf), hit / trials >= conf - 0.02, "%.4f" % (hit / trials))

    # ---- 연습 2: 다중화 ADC skew — 동시에 올라오는 두 레일
    t_conv = 3.5
    wa = ramp_fn(1000.0, 300.0, 1.8)
    wb = ramp_fn(1000.0, 300.0, 1.8)
    ts = [k * 10.0 for k in range(300)]
    va = mux_sample(wa, ts, 0, t_conv)
    vb = mux_sample(wb, ts, 3, t_conv)      # 채널 3: 10.5us 늦게 샘플
    t_a = first_crossing(ts, va, 0.9, True, 3)
    t_b = first_crossing(ts, vb, 0.9, True, 3)
    # 같은 파형을 늦게 샘플하면 스캔 시각 기준으로는 먼저 통과한 것처럼 보인다 (참값 차이 0)
    CHECK("p2.skew_misleads", t_b < t_a - 3.0, "t_a=%.2f t_b=%.2f" % (t_a, t_b))
    t_b_fixed = first_crossing(deskew(ts, 3, t_conv), vb, 0.9, True, 3)
    CHECK("p2.deskew_fixes", near(t_b_fixed, t_a, 0.5), "t_a=%.2f t_b_fixed=%.2f" % (t_a, t_b_fixed))

    # ---- 연습 3: RESET_N 해제 시점
    tr = [k * 1.0 for k in range(500)]
    late = [0.0 if x < 300.0 else 1.8 for x in tr]
    early = [0.0 if x < 120.0 else 1.8 for x in tr]
    rails = {"VDD_0V8": 90.0, "VDD_1V8": 180.0, "VDD_3V3": 240.0}
    verdict, dt, _ = check_reset_release(rails, tr, late, 1.17, 50.0)
    CHECK("p3.good", verdict == "PASS" and near(dt, 299.65 - 240.0, 0.5), "%s %s" % (verdict, dt))
    verdict, dt, _ = check_reset_release(rails, tr, early, 1.17, 50.0)
    CHECK("p3.early_reset_detected", verdict == "FAIL" and dt < 0, "%s %s" % (verdict, dt))
    CHECK("p3.never_released", check_reset_release(rails, tr, [0.0] * 500, 1.17, 50.0)[0] == "FAIL")

    # ---- 연습 4: 규칙 lint
    rails = ["VIN", "A", "B", "C"]
    CHECK("p4.order", lint_rules([("VIN", "A"), ("A", "B"), ("VIN", "C")], rails) == ["VIN", "A", "B", "C"])
    try:
        lint_rules([("A", "B"), ("B", "A")], rails)
        cyc = False
    except ValueError:
        cyc = True
    CHECK("p4.cycle_rejected", cyc)
    try:
        lint_rules([("A", "Z")], rails)
        unk = False
    except ValueError:
        unk = True
    CHECK("p4.unknown_rail_rejected", unk)
    CHECK("p4.deterministic_tie_by_name", lint_rules([("VIN", "C"), ("VIN", "A")], ["A", "C", "VIN"]) == ["VIN", "A", "C"])

    # ---- 연습 5: SSC 평균
    S, mod = 5000.0, 32000.0
    e = ssc_gate_error_ppm(S, mod, 100.0 / mod)
    CHECK("p5.integer_periods_mean_half_S", abs(e) < 1.0, str(e))
    e = ssc_gate_error_ppm(S, mod, 0.25 / mod)
    CHECK("p5.quarter_period_big_error", abs(e) > 1000.0, str(e))
    for np_ in (10.25, 100.25, 1000.25):
        e = abs(ssc_gate_error_ppm(S, mod, np_ / mod))
        bound = expected_gate_error(S, np_)
        CHECK("p5.partial_period_N=%.2f" % np_, e <= bound * 1.05, "e=%.2f bound=%.2f" % (e, bound))

    print("%d/%d passed" % (_pass, _pass + _fail))
    return 0 if _fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
