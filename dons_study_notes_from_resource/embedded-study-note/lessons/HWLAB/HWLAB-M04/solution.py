#!/usr/bin/env python3
"""HWLAB-M04 저전력 전류 프로파일 측정: 다이내믹 레인지·Burden 전압

sleep 2 µA ↔ active 15 mA 버스트가 섞인 MCU 전류(SENSNODE-L, 1 s 프레임)를 '진짜 값'으로 두고,
계측기가 그 값을 어떻게 왜곡하는지 원인별로 떼어 내 수치로 잰다.
  Part A  샘플링: 점 샘플(point) vs 구간 평균(integrate) × 1k/10k/100k S/s, 위상 스윕
          → 평균 전류·피크·배터리 수명 오차
  Part B  분해능·레인지·burden: 단일 레인지 ADC 비트 수 → sleep 오차,
          자동 레인지 전환 지연 + 션트 burden + DUT 디캡 → 전압 강하(sag)·brown-out (RC 를 1 µs 씩 정확히 적분)
  Part C  후보 계측기 6종(instruments.json)을 같은 모델로 돌려 PASS/MARGINAL/FAIL + 필요한 장비 사양 도출

  python3 -I solution.py            # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --report   # Part C 리포트만

Python 3.9+, 표준 라이브러리만 (bisect, json, math, os, sys).
단위: 시간 µs, 전류 A, 전하 A·µs (= µC), 전압 V.
"""
import bisect
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_json(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------- 세그먼트 도구 (주어짐)
def build_profile(prof):
    """JSON 프로파일 → 한 프레임(주기적으로 반복)의 진짜 전류 [(t0_us, t1_us, I_A)], 0..frame 를 빈틈없이 덮는다."""
    T, Is = prof["frame_us"], prof["sleep_a"]
    segs, t = [], 0
    for b in sorted(prof["bursts"], key=lambda b: b["start_us"]):
        a, e = b["start_us"], b["start_us"] + b["width_us"]
        if a > t:
            segs.append((t, a, Is))
        segs.append((a, e, b["current_a"]))
        t = e
    if t < T:
        segs.append((t, T, Is))
    return segs


def charge_fn(segs):
    """Q(t) = ∫[0,t] I dt [A·µs] 를 계산하는 함수를 돌려준다 (0 <= t <= 프레임 끝). 누적합 + 이분 탐색."""
    starts = [a for a, _, _ in segs]
    cum = [0.0]
    for a, b, I in segs:
        cum.append(cum[-1] + I * (b - a))

    def Q(t):
        k = max(0, bisect.bisect_right(starts, t) - 1)
        a, b, I = segs[k]
        return cum[k] + I * (min(t, b) - a)
    return Q


def rotate(segs, phase_us):
    """주기 프레임을 phase 만큼 당긴다: 새 시각 0 = 원래 시각 phase. 샘플러는 항상 0 에서 시작하면 된다."""
    T = segs[-1][1]
    p = phase_us % T
    if p == 0:
        return list(segs)
    head, tail = [], []
    for a, b, I in segs:
        if b <= p:
            tail.append((a - p + T, b - p + T, I))
        elif a >= p:
            head.append((a - p, b - p, I))
        else:                                   # p 를 품은 세그먼트는 둘로 쪼갠다
            head.append((0.0, b - p, I))
            tail.append((a - p + T, T, I))
    return head + tail


def merge(segs):
    """값이 같은 인접 세그먼트를 합친다 (µs 단위 시뮬레이션 결과를 가볍게)."""
    out = []
    for a, b, I in segs:
        if out and out[-1][2] == I and out[-1][1] == a:
            out[-1] = (out[-1][0], b, I)
        else:
            out.append((a, b, I))
    return out


_EPS = 1e-9


def _idx(t, Ts):
    """k·Ts >= t 인 첫 샘플 번호 k (부동소수 오차 흡수)."""
    return int(math.ceil(t / Ts - _EPS))


# ---------------------------------------------------------------- Part A: 진짜 값과 두 가지 샘플러
def average_current(segs):
    """프레임 평균 전류 [A] = ∫I dt / T."""
    return sum(I * (b - a) for a, b, I in segs) / (segs[-1][1] - segs[0][0])


def battery_life_h(i_avg_a, cap_mah):
    """가장 단순한 수명 모델: 용량[mAh] / 평균 전류[mA] (용량 감소·자가 방전·펄스 부하 보정은 see:RF-A04)."""
    return cap_mah * 1e-3 / i_avg_a


def sample_point(segs, fs_hz, phase_us=0.0):
    """점 샘플(sample-and-hold, aperture≈0): 샘플 k = I(phase + k·Ts), k = 0..N-1, N = T·fs.

    rotate() 로 phase 를 0 으로 옮긴 뒤, 세그먼트 [a,b) 마다 그 안에 떨어지는 샘플 수
    n = _idx(b) - _idx(a) 를 세어 [I]*n 을 한꺼번에 붙인다 (Python 루프가 샘플 수가 아니라 세그먼트 수만큼만 돈다).
    """
    Ts = 1e6 / fs_hz
    out = []
    for a, b, I in rotate(segs, phase_us):
        n = _idx(b, Ts) - _idx(a, Ts)
        if n > 0:
            out.extend([I] * n)
    return out


def sample_avg(segs, fs_hz, phase_us=0.0):
    """구간 평균(integrate-and-dump = boxcar 안티에일리어싱): 샘플 k = ∫[k·Ts,(k+1)·Ts) I dt / Ts.

    rotate() 로 phase 를 0 으로 옮긴 뒤, 세그먼트 하나 안에 통째로 들어가는 칸은 I 그대로 [I]*n,
    세그먼트 경계에 걸친 칸만 charge_fn() 의 Q 로 적분한다. 칸 수 N = round(T/Ts).
    """
    r = rotate(segs, phase_us)
    T = r[-1][1]
    Ts = 1e6 / fs_hz
    n = int(round(T / Ts))
    Q = charge_fn(r)
    out = []
    k = 0
    for a, b, I in r:
        k_in = _idx(a, Ts)                          # a 이후에 시작하는 첫 칸
        k_end = int(math.floor(b / Ts + _EPS))      # 이 칸 번호 직전까지는 [a,b) 안에 통째로
        while k < min(k_in, n):                     # a 를 가로지르는 칸 (앞 세그먼트와 걸침)
            out.append((Q(min((k + 1) * Ts, T)) - Q(k * Ts)) / Ts)
            k += 1
        if k_end > k:
            out.extend([I] * (k_end - k))
            k = k_end
    while k < n:
        out.append((Q(min((k + 1) * Ts, T)) - Q(k * Ts)) / Ts)
        k += 1
    return out


def phase_sweep(segs, fs_hz, mode, n_phase):
    """샘플 위상을 한 주기 안에서 n_phase 개로 고르게 바꿔 가며 [(캡처 평균, 캡처 피크)] — 한 번의 캡처 = 위상 하나."""
    Ts = 1e6 / fs_hz
    f = sample_point if mode == "point" else sample_avg
    res = []
    for j in range(n_phase):
        s = f(segs, fs_hz, (j + 0.5) / n_phase * Ts)
        res.append((sum(s) / len(s), max(s)))
    return res


# ---------------------------------------------------------------- Part B: ADC·레인지·burden
def quantize(i_a, lsb_a, fs_a):
    """ADC 한 샘플: [0, FS] 로 자른 뒤(포화) 가장 가까운 LSB 배수로 반올림. 단방향 측정이라 음수는 0."""
    x = min(max(i_a, 0.0), fs_a)
    return round(x / lsb_a) * lsb_a


def simulate_event(a_us, w_us, i_burst, sleep_a, meter, dut, max_us=60000):
    """버스트 하나(시작 a, 폭 w, 전류 i_burst)를 1 µs 씩 정확히 적분한다.

    회로: V_src ─ R_shunt(레인지) ─┬─ DUT(정전류 부하 I_load) ;  C_dut 가 DUT 단자에 병렬.
      한 스텝(dt = 1 µs) 동안 R, I_load 가 일정하므로 정확해:
        V_inf = V_src − I_load·R,   V' = V_inf + (V − V_inf)·exp(−dt/(R·C))
        션트 평균 전류 i_sh = I_load + C·(V' − V)/dt          (전하 보존: 디캡이 낸 만큼 션트는 덜 흐른다)
      계측값 = quantize(i_sh, 현재 레인지 lsb, FS). i_sh > FS 면 과전류(over) 카운트.
    레인지 로직 (스텝 끝에서):
      전환 대기 중이면 카운트다운 → 0 이 되면 최고 레인지(top)로.
      아니고 r < top 이고 i_sh > FS[r] 이면 전환 대기 시작 (t_switch_us 스텝 뒤 전환, 0 이면 즉시).
      아니고 r > 0 이면 i_sh < down_frac·FS[r−1] 가 t_hold_us 스텝 연속이면 한 단계 내려간다.
    시작: 긴 sleep 뒤 정착 상태 (r = 0, V = V_src − sleep·R0). 끝: 버스트 이후 r == 0, 대기 없음,
    |V − V_시작| < 1 µV 가 되면 (또는 max_us).
    반환 dict(t0, t1, segs=[(t, t+1, 계측값)] 병합, q_true, q_meas [A·µs], v_min, t_vmin, over_us, switches=[(t, r)])
    """
    rng = meter["ranges"]
    top = len(rng) - 1
    C, Vs = dut["c_dut_f"], dut["v_src"]
    t_sw, t_hold = meter.get("t_switch_us", 0), meter.get("t_hold_us", 0)
    down = meter.get("down_frac", 0.9)
    dt = 1e-6
    r = 0
    v = v0 = Vs - sleep_a * rng[0]["r_ohm"]
    pending, below = None, 0
    out, switches = [], []
    q_true = q_meas = 0.0
    v_min, t_vmin, over = v, a_us, 0
    t, b_us = a_us, a_us + w_us
    while True:
        i_load = i_burst if t < b_us else sleep_a
        R = rng[r]["r_ohm"]
        v_inf = Vs - i_load * R
        v_new = v_inf + (v - v_inf) * math.exp(-dt / (R * C))
        i_sh = i_load + C * (v_new - v) / dt
        v = v_new
        if v < v_min:
            v_min, t_vmin = v, t + 1
        if i_sh > rng[r]["fs_a"]:
            over += 1
        m = quantize(i_sh, rng[r]["lsb_a"], rng[r]["fs_a"])
        out.append((t, t + 1, m))
        q_true += i_load
        q_meas += m
        if pending is not None:                                  # 전환 지연 중: 아직 이전 레인지
            pending -= 1
            if pending <= 0:
                r, pending, below = top, None, 0
                switches.append((t + 1, r))
        elif r < top and i_sh > rng[r]["fs_a"]:                  # 과전류 감지
            if t_sw <= 0:
                r, below = top, 0
                switches.append((t + 1, r))
            else:
                pending = t_sw
        elif r > 0:                                              # 내려갈 수 있나 (히스테리시스 + hold)
            if i_sh < down * rng[r - 1]["fs_a"]:
                below += 1
                if below >= t_hold:
                    r, below = r - 1, 0
                    switches.append((t + 1, r))
            else:
                below = 0
        t += 1
        if t >= b_us and r == 0 and pending is None and abs(v - v0) < 1e-6:
            break
        if t - a_us >= max_us:
            break
    return dict(t0=a_us, t1=t, segs=merge(out), q_true=q_true, q_meas=q_meas,
                v_min=v_min, t_vmin=t_vmin, over_us=over, switches=switches)


def meter_trace(prof, meter):
    """프레임 전체에서 '계측기가 본 전류' 세그먼트 + 버스트별 시뮬레이션 결과.
    버스트 창 밖은 정착 상태: 레인지 0, 션트 전류 = sleep → quantize 한 상수."""
    dut, Is, T = prof["dut"], prof["sleep_a"], prof["frame_us"]
    r0 = meter["ranges"][0]
    idle = quantize(Is, r0["lsb_a"], r0["fs_a"])
    segs, evs, t = [], [], 0
    for b in sorted(prof["bursts"], key=lambda b: b["start_us"]):
        ev = simulate_event(b["start_us"], b["width_us"], b["current_a"], Is, meter, dut)
        assert ev["t0"] >= t, "event windows overlap"
        if ev["t0"] > t:
            segs.append((t, ev["t0"], idle))
        segs.extend(ev["segs"])
        t = ev["t1"]
        evs.append(ev)
    if t < T:
        segs.append((t, T, idle))
    return merge(segs), evs


def evaluate_meter(prof, meter, n_phase=10):
    """계측기 하나를 끝까지: RC/레인지 시뮬레이션 → 그 계측기의 샘플러(fs, mode)로 위상 스윕 → 지표 dict."""
    truth = build_profile(prof)
    i_true = average_current(truth)
    msegs, evs = meter_trace(prof, meter)
    sweep = phase_sweep(msegs, meter["fs_hz"], meter["mode"], n_phase)
    errs = [(x / i_true - 1.0) * 100.0 for x, _ in sweep]
    worst = max(errs, key=abs)
    avg_mean = sum(x for x, _ in sweep) / len(sweep)
    r0 = meter["ranges"][0]
    sleep_meas = quantize(prof["sleep_a"], r0["lsb_a"], r0["fs_a"])
    return dict(name=meter["name"], label=meter["label"], i_true=i_true, avg_mean=avg_mean,
                err_mean_pct=(avg_mean / i_true - 1.0) * 100.0, err_worst_pct=worst,
                peak_true=max(I for _, _, I in truth), peak_worst=min(p for _, p in sweep),
                sleep_meas=sleep_meas, sleep_err_pct=(sleep_meas / prof["sleep_a"] - 1.0) * 100.0,
                v_min=min(e["v_min"] for e in evs), over_us=sum(e["over_us"] for e in evs),
                life_true_h=battery_life_h(i_true, prof["battery_mah"]),
                life_meas_h=battery_life_h(avg_mean, prof["battery_mah"]) if avg_mean > 0 else float("inf"))


# ---------------------------------------------------------------- Part C: 필요 사양 도출과 판정
def required_spec(prof):
    """프로파일·DUT·목표 → 필요한 계측기 사양.

    dr            = I_peak / (e_sleep·I_sleep)            LSB ≤ e·I_sleep 이어야 sleep 를 e 정밀도로 본다
    dr_db         = 20·log10(dr),  bits_single = ceil(log2(dr))   (레인지 하나로 다 덮을 때 필요한 비트)
    fs_point_min  = 1e6 / Ts_max,  Ts_max = e_avg·Q_frame / Σ(I_burst − I_sleep)
                    (점 샘플은 버스트마다 최대 ±1 샘플 → 위상이 고정된 최악 캡처에서 오차 ≤ ΣΔI·Ts)
    fs_avg_peak   = 2e6 / w_min       (적분형이 피크를 그대로 보려면 가장 짧은 버스트 안에 통째로 든 칸이 1개 이상)
    r_fixed_max   = headroom / I_peak, headroom = V_src − V_bor − V_margin   (고정 레인지 정상상태 burden)
    t_sw_max_us   = C_dut·(headroom − V_det)/I_peak · 1e6,  V_det = target.range_burden_v
                    (최저 레인지에서 과전류 감지까지 V_det 만큼 처지고, 전환 지연 동안 디캡이 I_peak 를 공급)
    """
    Is, T = prof["sleep_a"], prof["frame_us"]
    tg, dut = prof["target"], prof["dut"]
    ipk = max(b["current_a"] for b in prof["bursts"])
    wmin = min(b["width_us"] for b in prof["bursts"])
    dr = ipk / (tg["sleep_err_pct"] / 100.0 * Is)
    q_frame = average_current(build_profile(prof)) * T
    ts_max = tg["avg_err_pct"] / 100.0 * q_frame / sum(b["current_a"] - Is for b in prof["bursts"])
    head = dut["v_src"] - dut["v_bor"] - dut["v_margin"]
    return dict(dr=dr, dr_db=20.0 * math.log10(dr), bits_single=int(math.ceil(math.log2(dr))),
                fs_point_min=1e6 / ts_max, fs_avg_peak=2e6 / wmin,
                r_fixed_max=head / ipk,
                t_sw_max_us=dut["c_dut_f"] * (head - tg["range_burden_v"]) / ipk * 1e6)


def judge(res, prof):
    """evaluate_meter() 결과 → (verdict, 이유 목록). 규칙:
      FAIL     v_min < v_bor ("BROWNOUT"), v_min < v_bor + v_margin ("Vmin margin"),
               |sleep_err| > sleep_err_pct, |err_mean| > avg_err_pct, peak_worst < (1 − peak_err)·peak_true
      MARGINAL 위 FAIL 은 없고 |err_worst| > avg_err_pct  (위상 평균은 맞지만 1 s 캡처 한 번은 틀릴 수 있음)
      PASS     나머지.  이유 문자열 예: "BROWNOUT Vmin=1.536V<1.8V", "avg err -11.2%>1.0%"
    """
    tg, dut = prof["target"], prof["dut"]
    fail, marg = [], []
    if res["v_min"] < dut["v_bor"]:
        fail.append("BROWNOUT Vmin=%.3fV<%.1fV" % (res["v_min"], dut["v_bor"]))
    elif res["v_min"] < dut["v_bor"] + dut["v_margin"]:
        fail.append("Vmin margin %.3fV<%.1fV" % (res["v_min"], dut["v_bor"] + dut["v_margin"]))
    if abs(res["sleep_err_pct"]) > tg["sleep_err_pct"]:
        fail.append("sleep %.3fuA err %+.1f%%" % (res["sleep_meas"] * 1e6, res["sleep_err_pct"]))
    if abs(res["err_mean_pct"]) > tg["avg_err_pct"]:
        fail.append("avg err %+.1f%%>%.1f%%" % (res["err_mean_pct"], tg["avg_err_pct"]))
    elif abs(res["err_worst_pct"]) > tg["avg_err_pct"]:
        marg.append("1-capture worst avg err %+.2f%%>%.1f%%" % (res["err_worst_pct"], tg["avg_err_pct"]))
    if res["peak_worst"] < (1.0 - tg["peak_err_pct"] / 100.0) * res["peak_true"]:
        fail.append("peak %.2fmA<%.2fmA" % (res["peak_worst"] * 1e3, (1.0 - tg["peak_err_pct"] / 100.0) * res["peak_true"] * 1e3))
    verdict = "FAIL" if fail else ("MARGINAL" if marg else "PASS")
    return verdict, fail + marg


def report(prof, meters, n_phase=10):
    """후보 계측기 표 + 필요 사양 + 한 줄 결론(PICK)."""
    req = required_spec(prof)
    lines = ["%s %s: I_true=%.3fuA life=%.2fy (%dmAh) | REQ DR %.0f:1 = %.1fdB (single range %d bit), "
             "fs_point>=%.0fkS/s or integrating fs>=%.1fkS/s, R_fixed<=%.1fohm, t_switch<=%.0fus"
             % (prof["board"], prof["rail"], average_current(build_profile(prof)) * 1e6,
                battery_life_h(average_current(build_profile(prof)), prof["battery_mah"]) / 8760.0,
                prof["battery_mah"], req["dr"], req["dr_db"], req["bits_single"], req["fs_point_min"] / 1e3,
                req["fs_avg_peak"] / 1e3, req["r_fixed_max"], req["t_sw_max_us"])]
    results = []
    for m in meters:
        res = evaluate_meter(prof, m, n_phase)
        verdict, why = judge(res, prof)
        results.append((m, res, verdict, why))
        lines.append("  %-9s %-8s avg %7.3fuA (mean %+6.2f%% worst %+6.2f%%) sleep %6.3fuA peak %6.2fmA "
                     "Vmin %.3fV life %5.2fy  %s"
                     % (m["name"], verdict, res["avg_mean"] * 1e6, res["err_mean_pct"], res["err_worst_pct"],
                        res["sleep_meas"] * 1e6, res["peak_worst"] * 1e3, res["v_min"],
                        res["life_meas_h"] / 8760.0, "; ".join(why) or "-"))
    pick = next((x for x in results if x[2] == "PASS"), None)
    if pick:
        m, res, _, _ = pick
        lines.append("PICK meter=%s (%s): avg %.3fuA err %+.2f%% (worst %+.2f%%), Vmin %.3fV, life %.2fy"
                     % (m["name"], m["label"], res["avg_mean"] * 1e6, res["err_mean_pct"], res["err_worst_pct"],
                        res["v_min"], res["life_meas_h"] / 8760.0))
    else:
        lines.append("PICK none - no candidate meets the target")
    return "\n".join(lines), results, req


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


def safe(fn, default=None):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def run_tests():
    prof = load_json("profile_sensnode.json")
    meters = {m["name"]: m for m in load_json("instruments.json")["meters"]}
    truth = build_profile(prof)
    Is, Ib = prof["sleep_a"], 0.015
    w_tot = sum(b["width_us"] for b in prof["bursts"])
    i_ref = Is + (Ib - Is) * w_tot / prof["frame_us"]            # 해석해: sleep + ΔI·duty

    # ---- Part A: 진짜 값, 그리고 샘플러 두 종류 × 1k/10k/100k S/s
    print("-- Part A: truth, point vs integrating samplers @ 1k/10k/100k S/s --")
    i_true = safe(lambda: average_current(truth))
    check("average_current == sleep + dI*duty = %.4fuA (1e-12 A)" % (i_ref * 1e6),
          lambda: (near(i_true, i_ref, 1e-12), "%.6fuA" % (i_true * 1e6)))
    check("battery life 220mAh / I_avg (h)",
          lambda: (near(battery_life_h(i_ref, 220), 0.220 / i_ref, 1e-6),
                   "%.1fh = %.3fy" % (battery_life_h(i_ref, 220), battery_life_h(i_ref, 220) / 8760)))
    check("sample_point: 1 S/s on [(0,1e6,5)] -> [5.0]; 4 S/s phase 0 on a 2-step -> [1,1,3,3]",
          lambda: sample_point([(0, 1e6, 5.0)], 1) == [5.0]
          and sample_point([(0, 5e5, 1.0), (5e5, 1e6, 3.0)], 4) == [1.0, 1.0, 3.0, 3.0])
    check("sample_avg: bin straddling a step is the exact mean (2 S/s, phase 250ms -> [2,2])",
          lambda: (sample_avg([(0, 5e5, 1.0), (5e5, 1e6, 3.0)], 2, 2.5e5) == [2.0, 2.0],
                   "%s" % sample_avg([(0, 5e5, 1.0), (5e5, 1e6, 3.0)], 2, 2.5e5)))
    table = {}
    for fs in (1000, 10000, 100000):
        for mode in ("point", "avg"):
            sw = safe(lambda: phase_sweep(truth, fs, mode, 100 if mode == "point" else 10), [])
            if sw:
                errs = [(a / i_ref - 1) * 100 for a, _ in sw]
                table[(fs, mode)] = dict(mean=sum(errs) / len(errs), worst=max(errs, key=abs),
                                         peak=min(p for _, p in sw))
                d = table[(fs, mode)]
                print("      %6d S/s %-5s  mean err %+7.2f%%  worst 1-capture err %+8.2f%%  worst peak %8.3fmA"
                      % (fs, mode, d["mean"], d["worst"], d["peak"] * 1e3))
    check("integrating sampler: average exact at every fs and phase (<1e-9 %)",
          lambda: all(abs(table[(fs, "avg")]["worst"]) < 1e-9 for fs in (1000, 10000, 100000)))
    check("point @1kS/s: unbiased over phase (|mean| < 1%) but one capture can be off > 50%",
          lambda: (abs(table[(1000, "point")]["mean"]) < 1.0 and abs(table[(1000, "point")]["worst"]) > 50,
                   "mean %+.2f%% worst %+.1f%%" % (table[(1000, "point")]["mean"], table[(1000, "point")]["worst"])))
    check("point worst-capture error shrinks with fs: |1k| > |10k| > |100k|, and 100k < 5%",
          lambda: (abs(table[(1000, "point")]["worst"]) > abs(table[(10000, "point")]["worst"])
                   > abs(table[(100000, "point")]["worst"]) and abs(table[(100000, "point")]["worst"]) < 5,
                   "%.1f / %.1f / %.2f %%" % tuple(abs(table[(f, "point")]["worst"]) for f in (1000, 10000, 100000))))
    check("point @1kS/s worst phase misses every burst (peak reads sleep 2uA)",
          lambda: (near(table[(1000, "point")]["peak"], Is, 1e-12), "%.3fuA" % (table[(1000, "point")]["peak"] * 1e6)))
    check("integrating @1kS/s smears the 0.817ms burst: worst peak < 50% of 15mA; @100kS/s exact 15mA",
          lambda: (table[(1000, "avg")]["peak"] < 0.5 * Ib and near(table[(100000, "avg")]["peak"], Ib, 1e-12),
                   "1k %.2fmA, 100k %.3fmA" % (table[(1000, "avg")]["peak"] * 1e3, table[(100000, "avg")]["peak"] * 1e3)))
    check("battery-life error of worst 1kS/s point capture > 30%",
          lambda: (abs(1 / (1 + table[(1000, "point")]["worst"] / 100) - 1) > 0.30,
                   "life x%.2f" % (1 / (1 + table[(1000, "point")]["worst"] / 100))))

    # ---- Part B: ADC 분해능, burden, 자동 레인지 전환 지연
    print("-- Part B: ADC bits, burden voltage, auto-range switch delay --")
    check("quantize: clip to FS, negative -> 0, round to LSB",
          lambda: quantize(0.03, 1e-3, 0.02) == 0.02 and quantize(-1e-6, 1e-6, 1.0) == 0.0
          and near(quantize(2.6e-6, 1e-6, 1.0), 3e-6, 1e-18))
    bits_tab = {}
    for bits in (8, 12, 16, 20):
        lsb = 0.02 / 2 ** bits
        val = safe(lambda: (quantize(Is, lsb, 0.02),
                            average_current([(a, b, quantize(I, lsb, 0.02)) for a, b, I in truth])), None)
        if val:
            bits_tab[bits] = val
            print("      single range FS 20mA %2d-bit: LSB %10.3fnA  sleep reads %8.4fuA (%+7.2f%%)  avg err %+7.3f%%"
                  % (bits, lsb * 1e9, bits_tab[bits][0] * 1e6, (bits_tab[bits][0] / Is - 1) * 100,
                     (bits_tab[bits][1] / i_ref - 1) * 100))
    check("12-bit single range: sleep reads 0 -> avg err ~ -11% (sleep share of the budget)",
          lambda: (bits_tab[12][0] == 0.0 and near((bits_tab[12][1] / i_ref - 1) * 100, -100 * Is * (1 - w_tot / 1e6) / i_ref, 1e-6),
                   "avg err %+.2f%%" % ((bits_tab[12][1] / i_ref - 1) * 100)))
    check("20-bit single range: sleep within 1%",
          lambda: (abs(bits_tab[20][0] / Is - 1) < 0.01, "%.4fuA" % (bits_tab[20][0] * 1e6)))

    dut = prof["dut"]
    dmm = meters["DMM_uA"]
    ev_d = safe(lambda: simulate_event(500411, 817, Ib, Is, dmm, dut), {})
    R, C, Vs = 100.0, dut["c_dut_f"], dut["v_src"]
    v0 = Vs - Is * R
    v_an = (Vs - Ib * R) + (v0 - (Vs - Ib * R)) * math.exp(-817e-6 / (R * C))
    check("DMM uA range (100 ohm) on 0.817ms burst: Vmin == analytic RC %.4fV (1uV) -> BROWNOUT < 1.8V" % v_an,
          lambda: (near(ev_d["v_min"], v_an, 1e-6) and ev_d["v_min"] < dut["v_bor"], "Vmin=%.4fV" % ev_d["v_min"]))
    check("DMM uA range clips (shunt current > 6mA FS) -> measured charge low",
          lambda: (ev_d["over_us"] > 0 and ev_d["q_meas"] < 0.8 * ev_d["q_true"],
                   "over %dus, q_meas/q_true=%.3f" % (ev_d["over_us"], ev_d["q_meas"] / ev_d["q_true"])))

    ar = dict(meters["AR_100K"])
    sw_tab = {}
    for tsw in (1, 20, 100, 200):
        ar["t_switch_us"] = tsw
        e = safe(lambda: simulate_event(500411, 817, Ib, Is, ar, dut), None)
        if e:
            sw_tab[tsw] = e
            print("      auto-range t_switch %4dus: Vmin %.4fV  over-range %4dus  charge err %+7.3f%%  switches %s"
                  % (tsw, e["v_min"], e["over_us"], (e["q_meas"] / e["q_true"] - 1) * 100, e["switches"][:5]))
    lin = lambda tsw: 0.05 + Ib * (tsw + 1) * 1e-6 / C                       # 감지 50mV + 지연 동안 I·t/C
    check("t_switch=20us sag ~= 50mV + I*(t_sw+1us)/C within 10%",
          lambda: (near(Vs - sw_tab[20]["v_min"], lin(20), 0.10 * lin(20)),
                   "sim %.1fmV, linear %.1fmV" % ((Vs - sw_tab[20]["v_min"]) * 1e3, lin(20) * 1e3)))
    check("fast switch (1us/20us): charge conserved within 0.1% (decap redistributes, nothing clipped for long)",
          lambda: (all(abs(sw_tab[k]["q_meas"] / sw_tab[k]["q_true"] - 1) < 1e-3 for k in (1, 20)),
                   "%+.4f%% / %+.4f%%" % tuple((sw_tab[k]["q_meas"] / sw_tab[k]["q_true"] - 1) * 100 for k in (1, 20))))
    check("slow switch 200us: BROWNOUT and recharge spike clips the top range -> charge lost > 5%",
          lambda: (sw_tab[200]["v_min"] < dut["v_bor"] and sw_tab[200]["q_meas"] / sw_tab[200]["q_true"] < 0.95,
                   "Vmin %.3fV, charge err %+.1f%%" % (sw_tab[200]["v_min"], (sw_tab[200]["q_meas"] / sw_tab[200]["q_true"] - 1) * 100)))

    # ---- Part C: SENSNODE-L 후보 계측기 판정 + 필요 사양
    print("-- Part C: SENSNODE-L VDD_MCU - which meter, and what spec is required --")
    req = safe(lambda: required_spec(prof), {})
    check("required: DR 750000:1 = 117.5dB -> 20-bit single range",
          lambda: (near(req["dr"], 750000, 1e-6) and near(req["dr_db"], 117.50, 0.01) and req["bits_single"] == 20,
                   "%.0f:1 %.2fdB %dbit" % (req["dr"], req["dr_db"], req["bits_single"])))
    check("required: point fs >= 415.9kS/s, integrating fs >= 31.7kS/s",
          lambda: (near(req["fs_point_min"], 415.9e3, 0.1e3) and near(req["fs_avg_peak"], 31.75e3, 0.01e3),
                   "%.1fk / %.2fk" % (req["fs_point_min"] / 1e3, req["fs_avg_peak"] / 1e3)))
    check("required: fixed shunt <= 66.7ohm, auto-range switch <= 139.3us",
          lambda: (near(req["r_fixed_max"], 66.67, 0.01) and near(req["t_sw_max_us"], 139.33, 0.01),
                   "%.2fohm %.2fus" % (req["r_fixed_max"], req["t_sw_max_us"])))
    rep = safe(lambda: report(prof, load_json("instruments.json")["meters"]), None)
    if rep:
        print(rep[0])
    verdicts = safe(lambda: {m["name"]: v for m, _, v, _ in rep[1]}, {})
    exp = dict(DMM_uA="FAIL", DMM_mA="FAIL", SCOPE_10R="FAIL", AR_SLOW="FAIL", AR_100K="MARGINAL", AR_1M="PASS")
    check("verdicts DMM_uA/DMM_mA/SCOPE_10R/AR_SLOW FAIL, AR_100K MARGINAL, AR_1M PASS",
          lambda: (verdicts == exp, "%s" % verdicts))
    why = safe(lambda: {m["name"]: w for m, _, _, w in rep[1]}, {})
    tags = safe(lambda: {k: sorted(set(x.split()[0] for x in w)) for k, w in why.items()}, {})
    check("failure reasons are the physical ones (brownout / sleep resolution / peak blindness)",
          lambda: (tags["DMM_uA"] == ["BROWNOUT", "avg", "peak"] and tags["DMM_mA"] == ["avg", "peak", "sleep"]
                   and tags["SCOPE_10R"] == ["avg", "sleep"] and tags["AR_SLOW"] == ["BROWNOUT", "avg"]
                   and tags["AR_100K"] == ["1-capture"] and tags["AR_1M"] == [], "%s" % tags))
    check("PICK line names AR_1M", lambda: (rep[0].splitlines()[-1].startswith("PICK meter=AR_1M"), rep[0].splitlines()[-1]))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 2 and argv[1] == "--report":
        print(report(load_json("profile_sensnode.json"), load_json("instruments.json")["meters"])[0])
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
