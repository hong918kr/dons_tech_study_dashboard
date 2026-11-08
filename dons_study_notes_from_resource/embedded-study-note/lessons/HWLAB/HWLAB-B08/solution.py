#!/usr/bin/env python3
"""HWLAB-B08 전원 레일 측정: DC 정확도·리플·20MHz BW Limit

벅 출력 캡처(DC + 스위칭 리플 + 스위칭 스파이크 + 잡음)를 분석한다.
  Part A  Vavg / Vmin / Vmax / Vpp / AC RMS 와 벅 리플 이론값(ΔI, ΔV_C, ΔV_ESR)
  Part B  20 MHz BW limit 을 Butterworth 바이쿼드(bilinear transform)로 흉내내고,
          스파이크가 Vpp 를 얼마나 부풀리는지, 필터 초기 상태·잡음 대역폭 함정까지 측정
  Part C  보드 EVT1-07 의 세 레일을 스펙(±3 %, 30 mVpp @20 MHz)으로 판정하는 리포트

  python3 -I solution.py           # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report  # 테스트 + 레일 리포트 출력

Python 3 표준 라이브러리만 쓴다. 데이터: cap_VDD_1V8.csv (gen_capture.py 로 재생성 가능).
"""
import cmath
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # python3 -I 는 스크립트 디렉터리를 sys.path 에 넣지 않는다
import gen_capture as gen           # noqa: E402

FS = gen.FS


# ---------------------------------------------------------------- Part A: 기본 통계
def load_csv(path):
    """스코프 CSV ('#' 주석, 'time_ns,volt_V' 헤더) → (t[s], v[V]) (주어진 코드)"""
    t, v = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line[0].isalpha():
                continue
            a, b = line.split(",")
            t.append(float(a) * 1e-9)
            v.append(float(b))
    return t, v


def rail_stats(v):
    """전압 샘플 → dict(n, vavg, vmin, vmax, vpp, rms_ac)
    rms_ac 는 평균을 뺀 RMS (모집단 표준편차, N 으로 나눔) = AC 커플링한 RMS 리플"""
    n = len(v)
    vavg = math.fsum(v) / n
    vmin, vmax = min(v), max(v)
    rms_ac = math.sqrt(math.fsum((x - vavg) ** 2 for x in v) / n)
    return {"n": n, "vavg": vavg, "vmin": vmin, "vmax": vmax, "vpp": vmax - vmin, "rms_ac": rms_ac}


def buck_ripple_theory(vin, vout, fsw, L, C, esr=0.0):
    """이상적 벅(CCM) 리플 → dict(D, dI, dv_c, dv_esr)
    D = Vout/Vin,  ΔI = (Vin-Vout)·D/(L·fsw),  ΔV_C = ΔI/(8·fsw·C),  ΔV_ESR = ΔI·ESR"""
    D = vout / vin
    dI = (vin - vout) * D / (L * fsw)
    return {"D": D, "dI": dI, "dv_c": dI / (8.0 * fsw * C), "dv_esr": dI * esr}


# ---------------------------------------------------------------- Part B: BW limit
def butter_qs(order):
    """Butterworth n차(짝수) → 2차 섹션들의 Q. Q_k = 1 / (2·sin((2k-1)π/(2n))) (주어진 코드)"""
    if order % 2:
        raise ValueError("even order only")
    return [1.0 / (2.0 * math.sin((2 * k - 1) * math.pi / (2 * order))) for k in range(1, order // 2 + 1)]


def butter_biquad(fc, fs, q):
    """아날로그 2차 저역통과 H(s) = 1/(s²/ωc² + s/(Q·ωc) + 1) 을 프리워핑한 bilinear transform 으로 이산화.
    반환 (b0, b1, b2, a1, a2) — 정규화된 차분방정식 y = b0·x + b1·x1 + b2·x2 - a1·y1 - a2·y2"""
    K = math.tan(math.pi * fc / fs)            # 프리워핑: 디지털 fc 에서 아날로그와 정확히 같은 응답
    norm = 1.0 / (1.0 + K / q + K * K)
    b0 = K * K * norm
    return (b0, 2.0 * b0, b0, 2.0 * (K * K - 1.0) * norm, (1.0 - K / q + K * K) * norm)


def biquad_filter(x, c, init="dc"):
    """Transposed Direct Form II 바이쿼드. init="dc": x[0] 이 영원히 이어져 온 정상 상태로 시작,
    init=None: 상태 0 으로 시작 (1.8 V 레일이 0 V 에서 출발하는 시작 과도응답이 생긴다)"""
    b0, b1, b2, a1, a2 = c
    if init == "dc" and x:
        g = (b0 + b1 + b2) / (1.0 + a1 + a2)   # DC 이득 (저역통과면 1)
        u = x[0]
        z2 = (b2 - a2 * g) * u
        z1 = (b1 - a1 * g) * u + z2
    else:
        z1 = z2 = 0.0
    y = []
    for xi in x:
        yi = b0 * xi + z1
        z1 = b1 * xi - a1 * yi + z2
        z2 = b2 * xi - a2 * yi
        y.append(yi)
    return y


def bw_limit(v, fs=FS, fc=20e6, order=2, init="dc"):
    """스코프 BW limit 흉내: Butterworth order 차 = 2차 섹션 직렬 연결 (주어진 코드)"""
    y = v
    for q in butter_qs(order):
        y = biquad_filter(y, butter_biquad(fc, fs, q), init)
    return y


def freq_response(sections, f, fs=FS):
    """바이쿼드 섹션 목록의 |H(e^jω)| (주어진 코드)"""
    z1 = cmath.exp(-2j * math.pi * f / fs)     # z^-1
    h = 1.0
    for b0, b1, b2, a1, a2 in sections:
        h *= (b0 + b1 * z1 + b2 * z1 * z1) / (1.0 + a1 * z1 + a2 * z1 * z1)
    return abs(h)


def find_spikes(t, v_full, v_bwl, thr=0.008, gap=20e-9):
    """잔차 r = v_full - v_bwl 에서 |r| > thr 인 샘플을 이벤트로 묶는다 (간격 < gap 이면 같은 이벤트).
    반환: [(t_start, pk)] — pk 는 이벤트 안에서 |r| 가 최대인 부호 있는 값"""
    events = []
    cur = None                                  # [t_start, t_last, pk]
    for ti, a, b in zip(t, v_full, v_bwl):
        r = a - b
        if abs(r) <= thr:
            continue
        if cur is not None and ti - cur[1] < gap:
            cur[1] = ti
            if abs(r) > abs(cur[2]):
                cur[2] = r
        else:
            if cur is not None:
                events.append((cur[0], cur[2]))
            cur = [ti, ti, r]
    if cur is not None:
        events.append((cur[0], cur[2]))
    return events


# ---------------------------------------------------------------- Part C: 레일 리포트
# 레일 스펙 시트 + 설계값 (분석하는 사람이 가진 정보. 보드의 실제 결함은 모른다)
SPECS = {
    "VDD_1V8": dict(nominal=1.8, tol_pct=3.0, vpp_max=0.030, bw=20e6, vin=5.0, fsw=2e6, L=1.0e-6, C=4.7e-6),
    "VDD_0V8": dict(nominal=0.8, tol_pct=3.0, vpp_max=0.030, bw=20e6, vin=3.3, fsw=1e6, L=0.47e-6, C=30e-6),
    "VDD_3V3": dict(nominal=3.3, tol_pct=3.0, vpp_max=0.030, bw=20e6, vin=5.0, fsw=2e6, L=1.5e-6, C=10e-6),
}


def rail_report(name, t, v, fs=FS):
    """한 레일을 스펙으로 판정 → dict(verdict, dc_ok, dc_err_pct, vavg, vpp_full, vpp_bwl, rms_bwl,
    ripple_ok, full_ok, spikes, spike_pk, c_est, hints, line)"""
    sp = SPECS[name]
    s_full = rail_stats(v)
    v_bwl = bw_limit(v, fs, sp["bw"]) if sp["bw"] else v
    s_bwl = rail_stats(v_bwl)
    dc_err = (s_full["vavg"] - sp["nominal"]) / sp["nominal"] * 100.0
    dc_ok = abs(dc_err) <= sp["tol_pct"]
    vpp_meas = s_bwl["vpp"]
    ripple_ok = vpp_meas <= sp["vpp_max"]
    full_ok = s_full["vpp"] <= sp["vpp_max"]
    ev = find_spikes(t, v, v_bwl)
    spike_pk = max([abs(p) for _, p in ev] or [0.0])
    th = buck_ripple_theory(sp["vin"], sp["nominal"], sp["fsw"], sp["L"], sp["C"])
    c_est = th["dI"] / (8.0 * sp["fsw"] * vpp_meas)       # ESR·ESL·잡음 무시한 1차 근사
    hints = []
    if not dc_ok:
        hints.append("DC %+.2f%% out of ±%.0f%%: DMM으로 sense 지점 재측정, FB 분압 저항·부하 IR drop 확인"
                      % (dc_err, sp["tol_pct"]))
    if not ripple_ok:
        hints.append("ripple %.1fmV@20MHz → C_eff≈%.1fuF (설계 %.1fuF): C_out 실장/DC-bias derating 확인"
                     % (vpp_meas * 1e3, c_est * 1e6, sp["C"] * 1e6))
    if ripple_ok and not full_ok:
        hints.append("full-BW 초과는 스위칭 스파이크(n=%d, pk=%.0fmV): 스펙은 20MHz 기준. "
                     "spring-tip으로 재측정해 진짜인지 확인" % (len(ev), spike_pk * 1e3))
    verdict = "PASS" if (dc_ok and ripple_ok) else "FAIL"
    line = ("%s rail=%s Vavg=%.4fV (%+.2f%%, DC %s) Vpp=%.1fmV@full %.1fmV@20MHz (limit %.0fmV, ripple %s) "
            "rms=%.2fmV spikes=%d pk=%.0fmV"
            % (verdict, name, s_full["vavg"], dc_err, "PASS" if dc_ok else "FAIL",
               s_full["vpp"] * 1e3, vpp_meas * 1e3, sp["vpp_max"] * 1e3, "PASS" if ripple_ok else "FAIL",
               s_bwl["rms_ac"] * 1e3, len(ev), spike_pk * 1e3))
    return {"verdict": verdict, "dc_ok": dc_ok, "dc_err_pct": dc_err, "vavg": s_full["vavg"],
            "vpp_full": s_full["vpp"], "vpp_bwl": vpp_meas, "rms_bwl": s_bwl["rms_ac"],
            "ripple_ok": ripple_ok, "full_ok": full_ok, "spikes": ev, "spike_pk": spike_pk,
            "c_est": c_est, "hints": hints, "line": line}


def board_report(board="EVT1-07"):
    """세 레일 리포트 (VDD_1V8 은 CSV 파일, 나머지는 메모리 합성) (주어진 코드)"""
    caps = {"VDD_1V8": load_csv(os.path.join(HERE, "cap_VDD_1V8.csv")),
            "VDD_0V8": gen.synth("VDD_0V8"), "VDD_3V3": gen.synth("VDD_3V3")}
    reps = {name: rail_report(name, *caps[name]) for name in ("VDD_0V8", "VDD_1V8", "VDD_3V3")}
    npass = sum(r["verdict"] == "PASS" for r in reps.values())
    lines = ["board=%s rails=%d pass=%d fail=%d" % (board, len(reps), npass, len(reps) - npass)]
    for name, r in reps.items():
        lines.append(r["line"])
        lines.extend("    -> " + h for h in r["hints"])
    return reps, "\n".join(lines)


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 (ok, detail) 또는 bool 을 돌려준다. 예외도 FAIL (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    try:
        r = fn()
        ok, detail = r if isinstance(r, tuple) else (bool(r), "")
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("  [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail))


def try_(fn):
    """하네스 밖에서 미리 계산하는 값: 예외 → None (그 값을 쓰는 CHECK 들이 FAIL 로 보고한다)"""
    try:
        return fn()
    except Exception:  # noqa: BLE001
        return None


def near(x, ref, tol, unit=1.0, u=""):
    """(|x-ref| <= tol, 'measured vs expected' 문자열)"""
    return abs(x - ref) <= tol, "got %.4f%s exp %.4f%s ±%.4f%s" % (x / unit, u, ref / unit, u, tol / unit, u)


def run_tests():
    print("-- Part A: stats & buck ripple theory --")
    check("rail_stats([1,2,3,4]) vavg/vpp/rms_ac",
          lambda: (lambda s: (abs(s["vavg"] - 2.5) < 1e-12 and abs(s["vpp"] - 3) < 1e-12
                              and abs(s["rms_ac"] - math.sqrt(1.25)) < 1e-12, "%s" % s))(rail_stats([1, 2, 3, 4])))
    tri = [1.8 + 0.020 * (abs(((k % 1000) / 1000.0) * 2 - 1) - 0.5) for k in range(10000)]
    check("triangle 20mVpp: rms_ac = Vpp/sqrt(12)",
          lambda: near(rail_stats(tri)["rms_ac"], 0.020 / math.sqrt(12), 0.00002, 1e-3, "mV"))
    sine = [1.8 + 0.010 * math.sin(2 * math.pi * k / 1000) for k in range(10000)]
    check("sine 20mVpp: rms_ac = Vpp/(2*sqrt(2))",
          lambda: near(rail_stats(sine)["rms_ac"], 0.020 / (2 * math.sqrt(2)), 0.00001, 1e-3, "mV"))
    th = try_(lambda: buck_ripple_theory(5.0, 1.8, 2e6, 1.0e-6, 4.7e-6, 0.010))
    check("theory VDD_1V8: D=0.36 dI=0.576A",
          lambda: (abs(th["D"] - 0.36) < 1e-12 and abs(th["dI"] - 0.576) < 1e-9, "D=%.3f dI=%.4fA" % (th["D"], th["dI"])))
    check("theory VDD_1V8: dv_c=dI/(8fC)=7.66mV dv_esr=5.76mV",
          lambda: (abs(th["dv_c"] - 0.576 / (8 * 2e6 * 4.7e-6)) < 1e-12 and abs(th["dv_esr"] - 0.00576) < 1e-12,
                   "dv_c=%.3fmV dv_esr=%.3fmV" % (th["dv_c"] * 1e3, th["dv_esr"] * 1e3)))
    _, vc = gen.synth("VDD_1V8", vout=1.8, noise=0, spike_rise=0, spike_fall=0, esr=0, esl=0)
    thc = try_(lambda: buck_ripple_theory(5.0, 1.8, 2e6, 1.0e-6, 4.7e-6))
    check("cap-only synth: measured Vpp == dI/(8fC) within 0.5%",
          lambda: near(rail_stats(vc)["vpp"], thc["dv_c"], 0.005 * thc["dv_c"], 1e-3, "mV"))
    t1, v1 = load_csv(os.path.join(HERE, "cap_VDD_1V8.csv"))
    check("load_csv: 10000 samples, dt=0.4ns",
          lambda: (len(v1) == 10000 and abs((t1[1] - t1[0]) - 0.4e-9) < 1e-15, "n=%d" % len(v1)))

    def stale():
        with open(os.path.join(HERE, "cap_VDD_1V8.csv"), encoding="utf-8") as f:
            return f.read() == gen.csv_text("VDD_1V8")
    check("committed cap_VDD_1V8.csv == gen_capture output (stale check)", stale)
    check("VDD_1V8 capture Vavg = 1.8030V ±0.3mV (spikes/noise average out)",
          lambda: near(rail_stats(v1)["vavg"], 1.8030, 0.0003, 1e-3, "mV"))

    print("-- Part B: 20 MHz BW limit (Butterworth biquad) --")
    check("butter_qs(4) = [1.3066, 0.5412]",
          lambda: (lambda q: (abs(q[0] - 1.306563) < 1e-5 and abs(q[1] - 0.541196) < 1e-5, "%r" % q))(butter_qs(4)))
    s2 = try_(lambda: [butter_biquad(20e6, FS, q) for q in butter_qs(2)])
    s4 = try_(lambda: [butter_biquad(20e6, FS, q) for q in butter_qs(4)])
    check("|H(0)| == 1 (DC passes untouched)", lambda: near(freq_response(s2, 0.0), 1.0, 1e-9))
    check("|H(20MHz)| = 0.7071 (-3dB) order 2", lambda: near(freq_response(s2, 20e6), 1 / math.sqrt(2), 1e-4))
    check("|H(20MHz)| = 0.7071 (-3dB) order 4", lambda: near(freq_response(s4, 20e6), 1 / math.sqrt(2), 1e-4))
    check("|H(2MHz)| > 0.9999 (fsw ripple passes)", lambda: (freq_response(s2, 2e6) > 0.9999,
                                                             "%.6f" % freq_response(s2, 2e6)))
    check("|H(180MHz)| < 0.0125 (ring ~ -38dB, order 2)",
          lambda: (freq_response(s2, 180e6) < 0.0125, "%.5f" % freq_response(s2, 180e6)))
    yb = try_(lambda: bw_limit(v1))
    check("dc init: first output == first input (no start-up transient)", lambda: near(yb[0], v1[0], 1e-6))
    y0 = try_(lambda: bw_limit(v1, init=None))
    check("zero-state init: start-up transient makes Vpp > 1V (the artifact)",
          lambda: (rail_stats(y0)["vpp"] > 1.0, "Vpp=%.3fV" % rail_stats(y0)["vpp"]))
    rng = random.Random(42)
    wn = [rng.gauss(0.0, 1e-3) for _ in range(100000)]
    enbw = 20e6 * (math.pi / 4) / math.sin(math.pi / 4)        # 2차 Butterworth ENBW = 1.1107·fc
    check("white noise sigma ratio = sqrt(ENBW/(fs/2)) = 0.1333 ±6%",
          lambda: (lambda r: near(r, math.sqrt(enbw / (FS / 2)), 0.06 * math.sqrt(enbw / (FS / 2))))(
              rail_stats(bw_limit(wn))["rms_ac"] / rail_stats(wn)["rms_ac"]))
    _, vtrue = gen.synth("VDD_1V8", noise=0, spike_rise=0, spike_fall=0)
    vpp_true = try_(lambda: rail_stats(vtrue)["vpp"])
    sf, sb = try_(lambda: rail_stats(v1)), try_(lambda: rail_stats(yb))
    check("full-BW Vpp inflated by spikes: > 30mV and > truth + 25mV",
          lambda: (sf["vpp"] > 0.030 and sf["vpp"] - vpp_true > 0.025,
                   "full=%.1fmV truth=%.1fmV" % (sf["vpp"] * 1e3, vpp_true * 1e3)))
    check("20MHz Vpp within 3mV of spike/noise-free truth",
          lambda: near(sb["vpp"], vpp_true, 0.003, 1e-3, "mV"))
    ev = try_(lambda: find_spikes(t1, v1, yb))
    check("find_spikes: 16 events (8 ON + 8 OFF edges in 4us @2MHz)",
          lambda: (len(ev) == 16 and sum(p > 0 for _, p in ev) == 8, "n=%d pos=%d" % (len(ev), sum(p > 0 for _, p in ev))))
    check("ON-edge spikes every 500ns (= 1/fsw) ±2ns",
          lambda: (lambda ts: (len(ts) == 8 and all(abs((b - a) - 500e-9) < 2e-9 for a, b in zip(ts, ts[1:])),
                               "dt=%s ns" % [round((b - a) * 1e9, 1) for a, b in zip(ts, ts[1:])][:3]))(
              [t0 for t0, p in ev if p > 0]))

    print("-- Part C: board EVT1-07 rail report --")
    reps, text = try_(board_report) or ({}, "")
    r18, r08, r33 = reps.get("VDD_1V8"), reps.get("VDD_0V8"), reps.get("VDD_3V3")
    check("VDD_1V8: full-BW FAIL but 20MHz PASS → verdict PASS",
          lambda: (not r18["full_ok"] and r18["ripple_ok"] and r18["verdict"] == "PASS", r18["line"][:60]))
    check("VDD_1V8: hint says spikes, not ripple", lambda: any("스파이크" in h for h in r18["hints"]))
    check("VDD_0V8: ripple FAIL even at 20MHz (> 30mV)",
          lambda: (not r08["ripple_ok"] and r08["vpp_bwl"] > 0.030 and r08["verdict"] == "FAIL",
                   "%.1fmV" % (r08["vpp_bwl"] * 1e3)))
    check("VDD_0V8: C_eff estimate < 25% of design 30uF → C_out hint",
          lambda: (r08["c_est"] < 0.25 * 30e-6 and any("C_out" in h for h in r08["hints"]),
                   "c_est=%.2fuF" % (r08["c_est"] * 1e6)))
    check("VDD_3V3: DC -3.27% → DC FAIL, ripple PASS",
          lambda: (not r33["dc_ok"] and r33["ripple_ok"] and abs(r33["dc_err_pct"] - (-3.27)) < 0.05,
                   "%+.2f%%" % r33["dc_err_pct"]))
    check("board summary: 1 pass / 2 fail", lambda: text.splitlines()[0].endswith("pass=1 fail=2"))
    check("report deterministic", lambda: board_report()[1] == text)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    ok = run_tests()
    if "--report" in argv:
        print()
        print(board_report()[1])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
