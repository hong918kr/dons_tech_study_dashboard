#!/usr/bin/env python3
"""step_response_oracle.py — R07 예제: 제어 품질 / 지연 시간 판정 오라클 (표준 라이브러리만).

무엇을 하나
  1. step_metrics(): 샘플링된 계단 응답(setpoint, measured)에서
     rise time(10%→90%), overshoot(%), settling time(±band), steady-state error 계산
  2. latency_summary(): 지연 샘플 목록 → n, min, mean, stdev, p50, p95, p99, max
  3. judge(): 측정값 dict vs 한계(limits) dict → PASS/FAIL 줄 (기계가 읽는 한 줄 형식)
  4. flaky_check(): 같은 테스트 N번 결과 → 통과율과 Wilson 하한 (통계적 판정)

쓰는 곳 (R07 본문)
  - SITL / HIL / 비행 로그에서 roll rate setpoint vs gyro 를 뽑아 회귀 판정
  - 로직 분석기로 잰 CRSF→DShot 지연 수천 개를 분포로 요약

실행:  python3 step_response_oracle.py      (파일 끝 self-test, 성공 시 "SELF-TEST PASS")
"""
import math
import random
import statistics


# ---------------------------------------------------------------- step response
def step_metrics(t, sp, y, band=0.05, ss_window=0.1):
    """t[s], sp(setpoint), y(measured) 같은 길이 리스트. 계단은 sp가 처음 바뀌는 시점.

    band      : settling 허용폭 (최종 계단 크기의 비율, 0.05 = ±5%)
    ss_window : steady-state 평균을 낼 끝부분 길이 (전체 시간 대비 비율)
    반환 dict : step_time, rise_time, overshoot_pct, settling_time, ss_error, ss_error_pct
    시간 값은 계단 시점 기준 상대 시간. 해당 사건이 없으면 None.
    """
    if not (len(t) == len(sp) == len(y)) or len(t) < 3:
        raise ValueError("t, sp, y must have same length >= 3")
    # 1) 계단 시점 찾기
    k0 = None
    for i in range(1, len(sp)):
        if sp[i] != sp[i - 1]:
            k0 = i
            break
    if k0 is None:
        raise ValueError("no step found in setpoint")
    t0 = t[k0]
    y_init = y[k0 - 1]
    sp_final = sp[-1]
    amp = sp_final - y_init
    if amp == 0:
        raise ValueError("zero step amplitude")
    sign = 1.0 if amp > 0 else -1.0

    def norm(v):  # 0 = 시작 값, 1 = 목표 값 (방향 무관)
        return (v - y_init) / amp

    # 2) rise time 10%→90%
    t10 = t90 = None
    for i in range(k0, len(t)):
        n = norm(y[i])
        if t10 is None and n >= 0.10:
            t10 = _interp_cross(t, y, i, y_init + 0.10 * amp)
        if t90 is None and n >= 0.90:
            t90 = _interp_cross(t, y, i, y_init + 0.90 * amp)
            break
    rise = (t90 - t10) if (t10 is not None and t90 is not None) else None

    # 3) overshoot: 목표를 넘어간 최대량 / 계단 크기
    peak = max((sign * (v - sp_final) for v in y[k0:]), default=0.0)
    overshoot_pct = max(0.0, peak / abs(amp) * 100.0)

    # 4) settling: 마지막으로 band 밖에 있던 샘플 다음 시각
    tol = band * abs(amp)
    last_out = None
    for i in range(k0, len(t)):
        if abs(y[i] - sp_final) > tol:
            last_out = i
    if last_out is None:
        settling = 0.0
    elif last_out == len(t) - 1:
        settling = None  # 끝까지 안 들어옴
    else:
        settling = t[last_out + 1] - t0

    # 5) steady-state error: 끝 ss_window 구간 평균
    n_ss = max(1, int(len(t) * ss_window))
    y_ss = statistics.fmean(y[-n_ss:])
    ss_err = sp_final - y_ss
    return {
        "step_time": t0,
        "rise_time": rise,
        "overshoot_pct": overshoot_pct,
        "settling_time": settling,
        "ss_error": ss_err,
        "ss_error_pct": abs(ss_err) / abs(amp) * 100.0,
    }


def _interp_cross(t, y, i, level):
    """y[i-1] < level <= y[i] (방향 무관) 구간에서 선형 보간한 교차 시각."""
    if i == 0 or y[i] == y[i - 1]:
        return t[i]
    frac = (level - y[i - 1]) / (y[i] - y[i - 1])
    frac = min(1.0, max(0.0, frac))
    return t[i - 1] + frac * (t[i] - t[i - 1])


# ---------------------------------------------------------------- latency
def percentile(sorted_vals, p):
    """선형 보간 백분위 (p: 0~100). numpy 'linear' 방식과 같다."""
    if not sorted_vals:
        raise ValueError("empty")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * p / 100.0
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (pos - lo)


def latency_summary(samples):
    """지연 샘플(단위 자유, 보통 ms 또는 us) → 분포 요약 dict."""
    s = sorted(samples)
    return {
        "n": len(s),
        "min": s[0],
        "mean": statistics.fmean(s),
        "stdev": statistics.pstdev(s) if len(s) > 1 else 0.0,
        "p50": percentile(s, 50),
        "p95": percentile(s, 95),
        "p99": percentile(s, 99),
        "max": s[-1],
    }


def histogram_text(samples, bins=10, width=40):
    """터미널용 ASCII 히스토그램 (리포트에 그대로 붙이기)."""
    lo, hi = min(samples), max(samples)
    if hi == lo:
        return f"{lo:.3f} | {'#' * width} {len(samples)}"
    step = (hi - lo) / bins
    counts = [0] * bins
    for v in samples:
        k = min(bins - 1, int((v - lo) / step))
        counts[k] += 1
    top = max(counts)
    lines = []
    for k, c in enumerate(counts):
        bar = "#" * int(round(c / top * width)) if top else ""
        lines.append(f"{lo + k * step:8.3f} | {bar} {c}")
    return "\n".join(lines)


# ---------------------------------------------------------------- judge
def judge(test_id, measured, limits):
    """limits = {"name": (lo, hi)}  (lo/hi 중 None 은 한쪽 한계 없음).

    반환: (overall_pass: bool, lines: list[str])
    줄 형식: RESULT <test_id>.<name> PASS|FAIL value=<v> lo=<lo> hi=<hi>
    측정값이 None(사건 없음)이면 FAIL — '측정 못 함'을 통과로 치지 않는다.
    """
    ok_all = True
    lines = []
    for name, (lo, hi) in limits.items():
        v = measured.get(name)
        if v is None:
            ok = False
        else:
            ok = (lo is None or v >= lo) and (hi is None or v <= hi)
        ok_all &= ok
        vs = "None" if v is None else f"{v:.4g}"
        lines.append(f"RESULT {test_id}.{name} {'PASS' if ok else 'FAIL'} value={vs} lo={lo} hi={hi}")
    return ok_all, lines


# ---------------------------------------------------------------- flaky / statistics
def wilson_lower(passes, n, z=1.96):
    """통과율의 Wilson score 95% 하한. n=0 이면 0."""
    if n == 0:
        return 0.0
    p = passes / n
    denom = 1 + z * z / n
    centre = p + z * z / (2 * n)
    margin = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return (centre - margin) / denom


def flaky_check(results, required_rate=0.99):
    """results: bool 리스트 (같은 빌드, 같은 테스트 N번).

    verdict:
      STABLE_PASS : 전부 통과 그리고 단측 정확 하한 (1-0.95)^(1/N) >= required_rate
      STABLE_FAIL : 전부 실패
      FLAKY       : 섞여 있음 → 원인 조사 대상 (재시도로 덮지 말 것)
      NEED_MORE   : 전부 통과지만 N이 작아 하한이 기준 미달
    """
    n = len(results)
    k = sum(1 for r in results if r)
    lb = wilson_lower(k, n)
    if n == 0:
        verdict = "NEED_MORE"
    elif k == 0:
        verdict = "STABLE_FAIL"
    elif k < n:
        verdict = "FLAKY"
    elif all_pass_lower(n) >= required_rate:
        verdict = "STABLE_PASS"
    else:
        verdict = "NEED_MORE"
    return {"n": n, "passes": k, "rate": k / n if n else 0.0, "wilson_lb": round(lb, 4),
            "exact_lb_if_all_pass": round(all_pass_lower(n), 4) if n and k == n else None,
            "verdict": verdict}


def all_pass_lower(n, confidence=0.95):
    """N번 모두 통과했을 때 통과율의 단측 정확(Clopper-Pearson) 하한 = (1-conf)^(1/N).

    Wilson(양측)보다 덜 보수적이다. 0 실패 판정은 이 값으로 한다 (rule of three 와 같은 근거).
    """
    return (1 - confidence) ** (1.0 / n) if n else 0.0


def runs_needed_zero_fail(required_rate, confidence=0.95):
    """'0번 실패'로 통과율 >= required_rate 를 confidence 로 말하려면 몇 번? (rule of three 일반형)"""
    return math.ceil(math.log(1 - confidence) / math.log(required_rate))


# ---------------------------------------------------------------- synthetic plant
def simulate_second_order(wn=40.0, zeta=0.5, dt=0.000125, t_end=0.5, t_step=0.05,
                          amp=1.0, noise=0.0, seed=1, gain=1.0):
    """2차 시스템 계단 응답 (오일러 적분, 8 kHz 샘플 = 125 us). 테스트용 가짜 '드론 축'."""
    rnd = random.Random(seed)
    n = int(t_end / dt)
    t, sp, y = [], [], []
    x, v = 0.0, 0.0
    for i in range(n):
        ti = i * dt
        r = amp if ti >= t_step else 0.0
        a = wn * wn * (gain * r - x) - 2 * zeta * wn * v
        v += a * dt
        x += v * dt
        t.append(ti)
        sp.append(r)
        y.append(x + (rnd.gauss(0, noise) if noise else 0.0))
    return t, sp, y


# ---------------------------------------------------------------- self-test
def _self_test():
    fails = 0

    def check(cond, msg):
        nonlocal fails
        print(("  ok   " if cond else "  FAIL ") + msg)
        if not cond:
            fails += 1

    print("[1] 2차 시스템 zeta=0.5: 이론 overshoot = exp(-pi*z/sqrt(1-z^2)) = 16.3%")
    t, sp, y = simulate_second_order(wn=40.0, zeta=0.5)
    m = step_metrics(t, sp, y, band=0.02)
    theo = math.exp(-math.pi * 0.5 / math.sqrt(1 - 0.25)) * 100
    print("     ", {k: (round(v, 4) if isinstance(v, float) else v) for k, v in m.items()})
    check(abs(m["overshoot_pct"] - theo) < 1.0, f"overshoot {m['overshoot_pct']:.2f}% ~= {theo:.2f}%")
    # 2% settling 근사 4/(zeta*wn) = 0.2 s (근사식이므로 넉넉히)
    check(m["settling_time"] is not None and 0.1 < m["settling_time"] < 0.3, "settling ~ 4/(zeta*wn)")
    check(m["ss_error_pct"] < 0.5, "steady-state error ~ 0")
    check(m["rise_time"] is not None and 0.02 < m["rise_time"] < 0.06, "rise time 범위")

    print("[2] 음의 계단 (방향 무관하게 같은 overshoot)")
    t2, sp2, y2 = simulate_second_order(wn=40.0, zeta=0.5, amp=-1.0)
    m2 = step_metrics(t2, sp2, y2, band=0.02)
    check(abs(m2["overshoot_pct"] - m["overshoot_pct"]) < 1e-6, "대칭")

    print("[3] 이득 부족 플랜트 (gain=0.9) → steady-state error 10% → judge FAIL")
    t3, sp3, y3 = simulate_second_order(wn=40.0, zeta=0.7, gain=0.9)
    m3 = step_metrics(t3, sp3, y3)
    limits = {"overshoot_pct": (None, 25.0), "settling_time": (None, 0.25), "ss_error_pct": (None, 2.0)}
    ok3, lines3 = judge("ctl.roll_step", m3, limits)
    for ln in lines3:
        print("     ", ln)
    check(not ok3 and abs(m3["ss_error_pct"] - 10.0) < 0.5, "ss error 10% 검출, overall FAIL")

    print("[4] 정상 플랜트 + 노이즈 → judge PASS")
    t4, sp4, y4 = simulate_second_order(wn=40.0, zeta=0.6, noise=0.005, seed=7)
    m4 = step_metrics(t4, sp4, y4, band=0.05)
    ok4, lines4 = judge("ctl.roll_step", m4, limits)
    for ln in lines4:
        print("     ", ln)
    check(ok4, "overall PASS")

    print("[5] 끝까지 settle 안 함 → settling None → judge FAIL ('측정 못 함' != 통과)")
    t5, sp5, y5 = simulate_second_order(wn=40.0, zeta=0.02, t_end=0.3)
    m5 = step_metrics(t5, sp5, y5, band=0.02)
    ok5, _ = judge("ctl.osc", m5, {"settling_time": (None, 1.0)})
    check(m5["settling_time"] is None and not ok5, "None → FAIL")

    print("[6] 지연 분포: 고정 0.8 ms + 패킷 대기 U(0,2) ms (500 Hz 링크 가정) + 드문 꼬리")
    rnd = random.Random(42)
    lat = [0.8 + rnd.uniform(0, 2.0) for _ in range(5000)]
    lat += [0.8 + 2.0 + 2.0 for _ in range(25)]  # 0.5%: 패킷 하나 잃고 다음 주기까지
    s = latency_summary(lat)
    print("     ", {k: (round(v, 3) if isinstance(v, float) else v) for k, v in s.items()})
    print(histogram_text(lat, bins=8, width=30))
    check(1.7 < s["p50"] < 1.9, "p50 ~ 1.8 ms")
    check(s["max"] > s["p99"] >= s["p95"] > s["p50"], "max > p99 >= p95 > p50")
    okl, linesl = judge("lat.crsf_to_dshot", s, {"p50": (None, 2.0), "p99": (None, 3.5), "max": (None, 5.0)})
    for ln in linesl:
        print("     ", ln)
    check(okl, "지연 한계 PASS")
    check(abs(percentile([1, 2, 3, 4], 50) - 2.5) < 1e-12, "percentile 보간")

    print("[7] 통계적 판정 / flaky")
    check(runs_needed_zero_fail(0.99, 0.95) == 299, "99%를 95% 신뢰로: 0 실패 299회 (rule of three ~ 300)")
    f1 = flaky_check([True] * 20)
    f2 = flaky_check([True] * 19 + [False])
    f3 = flaky_check([True] * 300, required_rate=0.99)
    print("     ", f1, "\n      ", f2, "\n      ", f3)
    check(f1["verdict"] == "NEED_MORE", "20/20 은 99% 주장에 부족")
    check(f2["verdict"] == "FLAKY", "19/20 은 FLAKY")
    check(f3["verdict"] == "STABLE_PASS", "300/300 은 STABLE_PASS (정확 하한 0.9901)")
    check(flaky_check([True] * 250)["verdict"] == "NEED_MORE", "250/250 은 아직 부족")

    print()
    if fails:
        print(f"SELF-TEST FAIL ({fails} checks)")
        raise SystemExit(1)
    print("SELF-TEST PASS")


if __name__ == "__main__":
    _self_test()
