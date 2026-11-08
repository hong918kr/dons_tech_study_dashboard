#!/usr/bin/env python3
"""HWLAB-M05 LA 캡처 타이밍 컴플라이언스: SPI·UART vs 데이터시트

  Part A  디코드 위에 타이밍 측정: SPI tCSS/tCSH/tCSD/tSU/tH/tCH/tCL, SCLK 평균 주파수·duty,
          UART 비트 폭 히스토그램 -> 실제 baud, inter-byte gap
  Part B  샘플링 주기에 의한 불확도 U (+-Ts, skew, 타임베이스 ppm, span 평균) 와 가드밴드 판정
          PASS / FAIL / UNRESOLVED — 순진한(naive) 비교가 거짓 PASS/FAIL 을 내는 경우를 실험
  Part C  EVT2 보드 A/B 위반 리포트 (데이터시트 표 = evt2_limits.json) + 한 줄 요약 + 원인 힌트

  python3 -I solution.py      # "N/M passed", 실패 시 exit 1

Python 3 표준 라이브러리만. .sr 로더는 HWLAB-B07 을, SPI/UART 디코더는 COMM-A10 식 디코더를 축약해 넣었다.
"""
import bisect
import json
import os
import random
import re
import statistics
import sys
import time
import zipfile
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True   # 같은 디렉터리 모듈 import 시 __pycache__ 를 남기지 않는다
sys.path.insert(0, HERE)          # python3 -I 는 스크립트 디렉터리를 sys.path 에 넣지 않는다
import gen_capture as G  # noqa: E402

PS = 10 ** 12


# ---------------------------------------------------------------- 주어짐: .sr 로더 (HWLAB-B07 축약)
def load_sr(src):
    """sigrok session v2 .sr -> 공통 캡처 {"names", "tr": {ch: [(t_ps, lvl), ...]}, "ts_ps", "t_end"}"""
    with zipfile.ZipFile(src) as z:
        if z.read("version").strip() != b"2":
            raise ValueError("unsupported .sr version")
        dev, sec = {}, None
        for line in z.read("metadata").decode().splitlines():
            s = line.strip()
            if s.startswith("["):
                sec = s[1:-1]
            elif "=" in s and sec == "device 1":
                k, v = s.split("=", 1)
                dev[k.strip()] = v.strip()
        m = re.match(r"^([0-9.]+)\s*([kMG]?)Hz$", dev["samplerate"])
        fs = int(Fraction(m.group(1)) * {"": 1, "k": 10 ** 3, "M": 10 ** 6, "G": 10 ** 9}[m.group(2)])
        u = int(dev["unitsize"])
        names = [dev["probe%d" % i] for i in range(1, int(dev["total probes"]) + 1)]
        pat = re.compile(re.escape(dev["capturefile"]) + r"-(\d+)$")
        chunks = sorted((int(mm.group(1)), n) for n in z.namelist() for mm in [pat.match(n)] if mm)
        data = b"".join(z.read(n) for _, n in chunks)          # 숫자 순서 (logic-1-10 > logic-1-2)
    if len(data) % u:
        raise ValueError("truncated sample data")
    ts = PS // fs
    tr = {n: [] for n in names}
    prev = None
    for k in range(len(data) // u):
        w = int.from_bytes(data[k * u:(k + 1) * u], "little")
        if w == prev:
            continue
        for b, n in enumerate(names):
            v = (w >> b) & 1
            if prev is None or v != (prev >> b) & 1:
                tr[n].append((k * ts, v))
        prev = w
    return {"names": names, "tr": tr, "ts_ps": ts, "t_end": len(data) // u * ts}


def load_spec(path=None):
    with open(path or os.path.join(HERE, "evt2_limits.json"), encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------- 주어짐: 디코더 (COMM-A10 식 축약)
def decode_spi(cap, cs="CS_N", sclk="SCLK", mosi="MOSI", miso="MISO"):
    """SPI mode 0 (CPOL=0, CPHA=0, MSB first). 같은 시각의 변화는 모두 적용한 뒤 에지 판정 (LA 샘플 의미론).
    반환 [{"t0", "t1", "bits", "mosi": bytes, "miso": bytes}] — CS_N 이 캡처 안에서 열리고 닫힌 프레임만."""
    chans = [cs, sclk, mosi, miso]
    st = {c: cap["tr"][c][0][1] for c in chans}
    ev = sorted((t, c, v) for c in chans for (t, v) in cap["tr"][c][1:])
    frames, fr, i = [], None, 0
    while i < len(ev):
        t = ev[i][0]
        prev = dict(st)
        while i < len(ev) and ev[i][0] == t:
            st[ev[i][1]] = ev[i][2]
            i += 1
        if prev[cs] == 1 and st[cs] == 0:
            fr = {"t0": t, "mo": [], "mi": []}
        if fr is not None and prev[sclk] == 0 and st[sclk] == 1:
            fr["mo"].append(st[mosi])
            fr["mi"].append(st[miso])
        if fr is not None and prev[cs] == 0 and st[cs] == 1:
            nb = len(fr["mo"])

            def pack(bits):
                return bytes(int("".join(map(str, bits[j:j + 8])), 2) for j in range(0, nb - nb % 8, 8))
            frames.append({"t0": fr["t0"], "t1": t, "bits": nb, "mosi": pack(fr["mo"]), "miso": pack(fr["mi"])})
            fr = None
    return frames


def decode_uart(cap, ch, tbit_ps):
    """UART 8N1 (LSB first, idle high). idle(1) 에서 falling = start, 비트 중앙 (k+0.5)*Tbit 에서 샘플.
    반환 [{"t0", "byte", "stop_ok", "edges": [프레임 안 에지 시각 (start 포함, stop 중앙 이전)]}]"""
    tr = cap["tr"][ch]
    times = [t for t, _ in tr]

    def lvl(t):
        return tr[bisect.bisect_right(times, t) - 1][1]
    frames, ready, i = [], tr[0][0], 1
    while i < len(tr):
        t, v = tr[i]
        if v == 0 and t >= ready and tr[i - 1][1] == 1:
            byte = sum(lvl(t + (k + 1.5) * tbit_ps) << k for k in range(8))
            stop_end = t + 9.5 * tbit_ps
            j = bisect.bisect_left(times, stop_end)
            frames.append({"t0": t, "byte": byte, "stop_ok": lvl(stop_end) == 1, "edges": times[i:j]})
            ready = stop_end
            i = j
            continue
        i += 1
    return frames


def cs_windows(cap, cs="CS_N"):
    """CS_N low 구간 [(fall, rise)] — 캡처 안에서 열리고 닫힌 것만"""
    tr = cap["tr"][cs]
    return [(tr[j][0], tr[j + 1][0]) for j in range(1, len(tr) - 1) if tr[j][1] == 0 and tr[j + 1][1] == 1]


# 측정 이름 분류 (불확도 규칙이 다르다)
SAME_CH = ("tCH", "tCL", "tCSD")                 # 같은 채널 두 에지
CROSS_CH = ("tCSS", "tCSH", "tSU", "tH", "tV")   # 다른 채널 두 에지 -> skew 추가


# ---------------------------------------------------------------- Part A: 타이밍 측정
def spi_measure(cap, cs="CS_N", sclk="SCLK", mosi="MOSI"):
    """반환 dict (값은 ns, fSCLK 는 MHz, duty 는 %):
      tCSS, tCSH  : CS_N 창 [fall, rise] 마다 (fall -> 첫 SCLK rise), (마지막 SCLK rise -> CS_N rise)
                    같은 샘플의 SCLK rise 는 창 안으로 친다 (순서를 모름 -> 간격 0 +- U 가 정직한 값)
      tCSD        : 프레임 사이 CS_N high 폭
      tCH         : 창 안의 SCLK rise -> 다음 fall,  tCL: 창 안의 fall -> 같은 창의 다음 rise
      tSU, tH     : 창 안의 MOSI 변화마다 (변화 -> 다음 rise), (직전 rise -> 변화)
      fSCLK       : [Σ(n_i - 1) / Σ(마지막 rise - 첫 rise)]  (span 평균, 프레임 i 의 rise 수 n_i)
      duty        : [mean(tCH) / mean(T) x 100]
      _f          : {"cycles": Σ(n_i - 1), "spans": 프레임 수, "T_ns": mean(T)}  (Part B 불확도용)"""
    # TODO(Part A): cs_windows() 마다 rises/falls 를 bisect 로 잘라 7개 간격 목록을 채우고,
    #   span 평균 T = Σ(마지막 rise - 첫 rise) / Σ(n-1) 로 fSCLK, mean(tCH)/T 로 duty 를 만든다 (ns 단위).
    raise NotImplementedError("spi_measure")


def uart_measure(cap, ch="TX", baud_nom=115200):
    """반환 dict:
      hist      : {n_bits: [count, min_smp, max_smp]}  (run 폭 히스토그램, n = round(w/T0), n <= 10)
      t0_ns     : 히스토그램 (1비트 클러스터 -> n<=9 run 전체 Σw/Σn) 으로 얻은 거친 Tbit
      tbit_ns   : 프레임 안 에지 span 으로 다듬은 Tbit = Σspan / Σn
      baud, baud_err : [ (baud/baud_nom - 1) x 100 ]  (%)
      gap       : 연속 프레임 사이 idle = 다음 start - (start + 10 Tbit), 공칭 문자 시간 단위 (char)
      bytes, stop_errors
      _uart     : {"n_bits": Σn, "spans": span 수, "tbit_ns", "tnom_ns", "tchar_ns"} (Part B 불확도용)"""
    # TODO(Part A): run 폭 -> 1비트 클러스터 T_init -> 히스토그램 -> T0 = Σw/Σn (n<=9)
    #   -> decode_uart(T0) 프레임 안 에지 span 으로 Tbit = Σspan/Σn (2회) -> baud_err, gap(char).
    raise NotImplementedError("uart_measure")


# ---------------------------------------------------------------- Part B: 불확도 + 가드밴드 판정
def meas_uncertainty(name, m, ts_ps, la):
    """측정 m[name] 의 최악값에 대한 +-U (그 측정의 단위). 각 에지는 [0, Ts) 늦게 보인다.
      같은 채널 간격      U = Ts + ppm*|v|
      다른 채널 간격      U = Ts + skew + ppm*|v|
      fSCLK (span 평균)   U_T = spans*Ts/cycles + ppm*T,  U_f = f*U_T/T
      duty                U = 100*Ts/T + duty*U_T/T      (high 폭 평균의 오차 한계는 Ts — 평균해도 줄지 않는다)
      baud_err            U_Tb = spans*Ts/n_bits + ppm*Tb,  U = 100*(Tnom/Tb)*(U_Tb/Tb)
      gap (char)          U = (Ts + 10*U_Tb + ppm*|gap|*Tchar) / Tchar"""
    # TODO(Part B): docstring 의 규칙대로 U 를 돌려준다 (ts = ts_ps/1000 ns, ppm = timebase_ppm*1e-6).
    raise NotImplementedError("meas_uncertainty")


def guard_verdict(vmin, vmax, u, lim):
    """진짜 최악값은 [v - u, v + u] 안. lim = {"min": x} / {"max": y} / 둘 다.
    FAIL       : 구간 전체가 한계 밖  (min: vmin + u < min,  max: vmax - u > max)
    UNRESOLVED : 구간이 한계에 걸침
    PASS       : 구간 전체가 한계 안.   u = 0 이면 순진한(naive) 비교."""
    # TODO(Part B): min 쪽은 vmin, max 쪽은 vmax 로 판정. FAIL 이 UNRESOLVED 보다 우선.
    raise NotImplementedError("guard_verdict")


def compliance(meas, params, ts_ps, la):
    """데이터시트 표(params) 순서대로 행을 만든다. 측정 인스턴스가 없으면 verdict = "NO DATA".
    행: {"param", "n", "min", "max", "u", "unit", "lim", "verdict", "naive"}"""
    # TODO(Part B): params 순서대로 행 dict 를 만든다. u 는 meas_uncertainty, naive 는 u=0 판정.
    raise NotImplementedError("compliance")


# ---------------------------------------------------------------- Part C: 보드 리포트
STD_XTAL_MHZ = (8.0, 12.0, 16.0, 19.2, 20.0, 24.0, 25.0, 26.0, 27.0, 32.0, 38.4, 40.0, 48.0)


def worst(r):
    """판정에 쓰인 쪽의 값: min 한계를 어기거나 걸치면 min, 아니면 max (한쪽 한계면 그쪽)."""
    lim = r["lim"]
    if "min" in lim and ("max" not in lim or r["min"] - r["u"] < lim["min"]):
        return r["min"]
    return r["max"]


def fmt_row(r):
    """주어짐: 리포트 한 줄"""
    if r["verdict"] == "NO DATA":
        return "  %-8s n=  0  (no instance in capture)%37s NO DATA" % (r["param"], "")
    lim = r["lim"]
    lims = " ".join("%s%g" % (op, lim[k]) for k, op in (("min", ">="), ("max", "<=")) if k in lim)
    return "  %-8s n=%3d  worst=%9.3f +-%7.3f %-4s  limit %-11s %-10s (naive %s)" % (
        r["param"], r["n"], worst(r), r["u"], r["unit"], lims, r["verdict"], r["naive"])


def _overall(rows):
    v = [r["verdict"] for r in rows]
    return "FAIL" if "FAIL" in v else ("UNRESOLVED" if ("UNRESOLVED" in v or "NO DATA" in v) else "PASS")


def board_report(label, spi_cap, uart_cap, spec):
    """반환 (overall, lines, oneline).
    overall: FAIL > UNRESOLVED(가드밴드에 걸림 또는 NO DATA) > PASS.
    lines  : 캡처 헤더 + fmt_row 행들 (SPI, 그리고 uart_cap 이 있으면 UART) + 마지막에 oneline
    oneline: '<label> <overall>: <p=v+-u unit>, ... (FAIL 행) | unresolved <p>,.. -> re-capture <bus> at >= 5x fs | 힌트 ...'
             PASS 면 '<label> PASS: all N limits met with guard band'
    힌트: SCLK 와 UART 가 같은 비율(>1% 차이, 서로 0.3% 이내)로 틀어졌으면 공통 클럭 ->
          'HSE 24 MHz x r = 26.00 MHz -> 26 MHz xtal fitted vs HSE_VALUE=24?' (가장 가까운 표준 크리스털),
          tCSH FAIL -> 'wait BSY/EOT (not TXE)', gap FAIL -> 'DMA', NO DATA -> 토글 테스트 패턴"""
    # TODO(Part C): spi_measure/uart_measure -> compliance -> fmt_row 줄들 -> _overall
    #   -> oneline (FAIL 값, unresolved 재캡처 제안, 공통 클럭/BSY/DMA/NO DATA 힌트).
    raise NotImplementedError("board_report")


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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


def info(fn):
    """측정값 출력 (starter 에서 미구현이면 조용히 건너뛴다)"""
    try:
        fn()
    except Exception as e:  # noqa: BLE001
        print("      (skipped: %s: %s)" % (type(e).__name__, e))


def p(name):
    return os.path.join(HERE, name)


def errs_within(meas, truth, keys, bound_ns):
    """같은 인스턴스끼리 |측정 - 진짜| < bound 인가 (인스턴스 수도 같아야 한다)"""
    for k in keys:
        if len(meas[k]) != len(truth[k]):
            return False
        if any(abs(a - b) >= bound_ns for a, b in zip(meas[k], truth[k])):
            return False
    return True


def truth_inside(meas, truth, spec_params, ts_ps, la):
    """진짜 최악값이 [측정 최악값 - U, + U] 안인가 (모든 파라미터)"""
    for p_, lim in spec_params.items():
        if not meas.get(p_):
            continue
        u = meas_uncertainty(p_, meas, ts_ps, la)
        if "min" in lim and abs(min(meas[p_]) - min(truth[p_])) > u:
            return False
        if "max" in lim and abs(max(meas[p_]) - max(truth[p_])) > u:
            return False
    return True


def main():
    t_start = time.time()
    spec = load_spec()
    la = spec["la"]
    sp = spec["spi_U7"]["params"]
    up = spec["uart_U9"]["params"]
    wa_spi, wa_uart = G.board_waves("EVT2-A")
    wb_spi, wb_uart = G.board_waves("EVT2-B")
    A100 = load_sr(p("evt2a_spi_100M.sr"))
    AU10 = load_sr(p("evt2a_uart_10M.sr"))
    B100 = load_sr(p("evt2b_spi_100M.sr"))
    B500 = load_sr(p("evt2b_spi_500M.sr"))
    BU10 = load_sr(p("evt2b_uart_10M.sr"))
    TIMING = ("tCSS", "tCSH", "tCSD", "tSU", "tH", "tCH", "tCL")

    print("-- Part A: SPI timing on top of the decoder --")
    check("given: load .sr (4 ch, 100 MS/s) + decode_spi 4 frames == SPI_FRAMES",
          lambda: B100["ts_ps"] == 10000 and [(list(f["mosi"]), list(f["miso"])) for f in decode_spi(B100)]
          == [(mo, mi) for mo, mi in G.SPI_FRAMES])
    tb = None
    try:
        tb = spi_measure(G.ideal(wb_spi))
    except Exception:  # noqa: BLE001
        pass
    T_B = 1e3 / 9.6        # ns
    check("ideal EVT2-B: tCSS == 26.000 ns x4, tCSD ~ 600 ns x3",
          lambda: tb["tCSS"] == [26.0] * 4 and len(tb["tCSD"]) == 3 and all(abs(v - 600) < 1 for v in tb["tCSD"]))
    check("ideal EVT2-B: counts tCH 96, tCL 92 (23/frame), tSU 20 / tH 17 (one per in-frame MOSI edge)",
          lambda: len(tb["tCH"]) == 96 and len(tb["tCL"]) == 92 and len(tb["tSU"]) == 20 and len(tb["tH"]) == 17)
    check("ideal EVT2-B: tSU = 21 (1st bit: tCSS-5) or T*0.52-3ns, tH = 0.48T+3ns (+-1 ns jitter)",
          lambda: all(v == 21.0 or abs(v - (0.52 * T_B - 3)) < 1 for v in tb["tSU"])
          and all(abs(v - (0.48 * T_B + 3)) < 1 for v in tb["tH"]))
    check("ideal EVT2-B: fSCLK (span) = 9.6 MHz +-0.02%, duty = 48% +-0.3",
          lambda: abs(tb["fSCLK"][0] - 9.6) / 9.6 < 2e-4 and abs(tb["duty"][0] - 48) < 0.3)
    mb100 = mb500 = None
    try:
        mb100, mb500 = spi_measure(B100), spi_measure(B500)
    except Exception:  # noqa: BLE001
        pass
    check("100 MS/s capture: every instance |meas - true| < Ts = 10 ns (all 7 timing params)",
          lambda: errs_within(mb100, tb, TIMING, 10.0))
    check("500 MS/s capture: every instance |meas - true| < Ts = 2 ns",
          lambda: errs_within(mb500, tb, TIMING, 2.0))
    info(lambda: [print("      %-5s ideal %-22s 100M %-22s 500M %s" % (
        k, "%.2f..%.2f" % (min(tb[k]), max(tb[k])), "%.2f..%.2f" % (min(mb100[k]), max(mb100[k])),
        "%.2f..%.2f" % (min(mb500[k]), max(mb500[k])))) for k in TIMING + ("fSCLK", "duty")])

    print("-- Part A: UART baud from bit-width histogram, inter-byte gap --")
    ua = ub = None
    try:
        ua, ub = uart_measure(AU10), uart_measure(BU10)
    except Exception:  # noqa: BLE001
        pass
    tbA, tbB = PS / G.clocks(26.0)[1] / 1000, PS / G.clocks(24.0)[1] / 1000
    check("EVT2-B UART: bytes == 'AT+SEND=T,23.5\\r\\n', no stop errors",
          lambda: ub["bytes"] == G.UART_MSG and ub["stop_errors"] == 0)
    check("EVT2-B UART: Tbit within 0.05% of truth (8684.9 ns), histogram has 1-bit cluster",
          lambda: abs(ub["tbit_ns"] - tbB) / tbB < 5e-4 and ub["hist"].get(1, [0])[0] > 10)
    check("EVT2-A UART: decodes the same text with measured Tbit, baud_err = +8.28% +-0.05",
          lambda: ua["bytes"] == G.UART_MSG and abs(ua["baud_err"][0] - 8.279) < 0.05)
    check("EVT2-A UART: gaps -> 2 of 15 are 180 us = 2.07 char, rest 0.10 char (+-0.01)",
          lambda: sorted(round(g, 2) for g in ua["gap"])[-2:] == [2.07, 2.07]
          and all(abs(g - 0.104) < 0.01 for g in ua["gap"] if g < 1))
    check("given: same EVT2-A capture decoded at NOMINAL 115200 (what U9 does) -> garbage",
          lambda: bytes(f["byte"] for f in decode_uart(AU10, "TX", PS / 115200)) != G.UART_MSG)
    info(lambda: print("      EVT2-A hist {n: [count, min_smp, max_smp]} %s  T0=%.1fns Tbit=%.2fns baud=%.1f"
                       % (ua["hist"], ua["t0_ns"], ua["tbit_ns"], ua["baud"])))
    info(lambda: print("      EVT2-A nominal-rate decode: %r" % bytes(
        f["byte"] for f in decode_uart(AU10, "TX", PS / 115200))))

    print("-- Part B: uncertainty and guard-band verdicts --")
    GV = [((25, 25, 3, {"min": 20}), "PASS"), ((22, 22, 3, {"min": 20}), "UNRESOLVED"),
          ((16, 16, 3, {"min": 20}), "FAIL"), ((17, 17, 3, {"min": 20}), "UNRESOLVED"),
          ((9.0, 9.0, 0.5, {"max": 10}), "PASS"), ((10.4, 10.4, 0.05, {"max": 10}), "FAIL"),
          ((45, 52, 4, {"min": 40, "max": 60}), "PASS"), ((38, 52, 4, {"min": 40, "max": 60}), "UNRESOLVED"),
          ((45, 66, 4, {"min": 40, "max": 60}), "FAIL"), ((19.5, 19.5, 0, {"min": 20}), "FAIL")]
    check("guard_verdict table (min / max / both / u=0 naive), 10 cases",
          lambda: all(guard_verdict(*a) == want for a, want in GV))
    check("U(tCSS @100M) = Ts + skew + ppm*v = 11.0013 ns; U(tCH @100M) = 10.0025 ns",
          lambda: abs(meas_uncertainty("tCSS", mb100, 10000, la) - (11 + 50e-6 * max(mb100["tCSS"]))) < 1e-9
          and abs(meas_uncertainty("tCH", mb100, 10000, la) - (10 + 50e-6 * max(mb100["tCH"]))) < 1e-9)
    check("truth inside [worst +- U] for every SPI param, EVT2-B 100M and 500M",
          lambda: truth_inside(mb100, tb, sp, 10000, la) and truth_inside(mb500, tb, sp, 2000, la))
    ta = None
    try:
        ta = spi_measure(G.ideal(wa_spi))
    except Exception:  # noqa: BLE001
        pass
    check("truth inside [worst +- U] for every SPI param, EVT2-A 100M",
          lambda: truth_inside(spi_measure(A100), ta, sp, 10000, la))

    # 스윕: 진짜 tCSS 17 ns (위반) / 23 ns (통과), 한계 min 20 ns, 무작위 위상 100번
    rng = random.Random(55)
    sweep = {}

    def run_sweep():
        for tcss in (17, 23):
            w = G.spi_wave(9.6e6, tcss_ps=tcss * 1000, seed=3)
            for fs_m in (100, 200, 500, 1000):
                fs = fs_m * 10 ** 6
                cnt = {"naive_wrong": 0, "guard_wrong": 0, "unres": 0}
                for _ in range(100):
                    m = spi_measure(G.resample(w, fs, rng.randrange(PS // fs)))
                    u = meas_uncertainty("tCSS", m, PS // fs, la)
                    truth = "FAIL" if tcss < 20 else "PASS"
                    gv = guard_verdict(min(m["tCSS"]), 0, u, {"min": 20})
                    nv = guard_verdict(min(m["tCSS"]), 0, 0.0, {"min": 20})
                    cnt["naive_wrong"] += nv != truth
                    cnt["guard_wrong"] += gv not in (truth, "UNRESOLVED")
                    cnt["unres"] += gv == "UNRESOLVED"
                sweep[(tcss, fs_m)] = cnt
    t0 = time.time()
    info(run_sweep)
    if sweep:
        print("      tCSS truth  fs[MS/s]  naive wrong  guard wrong  UNRESOLVED   (100 random phases, limit >= 20 ns)")
        for (tc, fm), c in sorted(sweep.items()):
            print("      %5d ns   %6d     %5d%%       %5d%%       %5d%%" % (tc, fm, c["naive_wrong"],
                                                                         c["guard_wrong"], c["unres"]))
        print("      (sweep %.1f s)" % (time.time() - t0))
    check("sweep: guard band is NEVER wrong (0 false PASS / false FAIL) at any fs",
          lambda: sweep and all(c["guard_wrong"] == 0 for c in sweep.values()))
    check("sweep: naive is wrong > 30% at 100 MS/s for truth 17 ns (false PASS)",
          lambda: sweep[(17, 100)]["naive_wrong"] > 30)
    check("sweep: 17 ns decided (FAIL) from 500 MS/s, 23 ns only at 1 GS/s (margin 3 ns vs U = Ts + 1)",
          lambda: sweep[(17, 500)]["unres"] == 0 and sweep[(23, 500)]["unres"] > 50
          and sweep[(23, 1000)]["unres"] == 0 and sweep[(17, 1000)]["unres"] == 0)

    def per_cycle_fmax(m):
        return 1e3 / (min(m["tCH"]) + min(m["tCL"]))
    check("EVT2-A fSCLK: span mean 10.400 MHz +-U (U < 0.5%) vs single shortest cycle off by > 3%",
          lambda: abs(spi_measure(A100)["fSCLK"][0] - 10.4) <= meas_uncertainty("fSCLK", spi_measure(A100), 10000, la)
          and meas_uncertainty("fSCLK", spi_measure(A100), 10000, la) / 10.4 < 5e-3
          and abs(per_cycle_fmax(spi_measure(A100)) - 10.4) / 10.4 > 0.03)
    info(lambda: print("      EVT2-A 100M: fSCLK span = %.4f +- %.4f MHz ; 1/(min tCH + min tCL) = %.3f MHz"
                       % (spi_measure(A100)["fSCLK"][0], meas_uncertainty("fSCLK", spi_measure(A100), 10000, la),
                          per_cycle_fmax(spi_measure(A100)))))

    # coherent vs incoherent: duty 48%, 지터 0, 100 MS/s, 위상 8개
    duty_res = {}

    def run_duty():
        for f_hz in (10.0e6, 9.6e6):
            w = G.spi_wave(f_hz, jit_ps=0, frames=[([0x55] * 12, [0xFF] * 12)], seed=4)   # 한 프레임 96 클럭
            ds = [spi_measure(G.resample(w, 10 ** 8, ph))["duty"][0] for ph in range(0, 10000, 1250)]
            duty_res[f_hz] = ds
    info(run_duty)
    info(lambda: [print("      f=%4.1f MHz @100MS/s duty over 8 phases: %s  (truth 48.00)"
                        % (f / 1e6, " ".join("%.2f" % d for d in ds))) for f, ds in duty_res.items()])
    check("coherent 10.0 MHz @100 MS/s: averaged duty biased (|err| >= 2% for every phase)",
          lambda: all(abs(d - 48) >= 2 for d in duty_res[10.0e6]))
    check("incoherent 9.6 MHz @100 MS/s: averaging works (|err| < 1% for every phase)",
          lambda: all(abs(d - 48) < 1 for d in duty_res[9.6e6]))
    check("both: truth 48% inside duty +- U (U = 100*Ts/T ~ 10%)",
          lambda: all(abs(d - 48) <= 100 * 10 / (1e3 / f * 1) * 1.01 for f, ds in
                      ((10.0, duty_res[10.0e6]), (9.6, duty_res[9.6e6])) for d in ds))

    # UART: +1.5% baud, 1 MS/s vs 10 MS/s
    def uart_at(fs, ph):
        w = G.uart_wave(115200 * 1.015, seed=7)
        m = uart_measure(G.resample(w, fs, ph))
        u = meas_uncertainty("baud_err", m, PS // fs, la)
        return m, u, guard_verdict(m["baud_err"][0], m["baud_err"][0], u, up["baud_err"])
    info(lambda: [print("      U9' +1.500%% @%2d MS/s: baud_err=%+.3f +- %.3f %%  hist=%s  -> %s"
                        % (fs // 10 ** 6, r[0]["baud_err"][0], r[1], r[0]["hist"] if fs == 10 ** 6 else "...", r[2]))
                  for fs in (10 ** 6, 10 ** 7) for r in [uart_at(fs, 123)]])
    check("UART +1.5%: 1 MS/s -> UNRESOLVED, 10 MS/s -> PASS, truth inside +-U both",
          lambda: uart_at(10 ** 6, 123)[2] == "UNRESOLVED" and uart_at(10 ** 7, 123)[2] == "PASS"
          and all(abs(uart_at(fs, 123)[0]["baud_err"][0] - 1.5) <= uart_at(fs, 123)[1] for fs in (10 ** 6, 10 ** 7)))
    check("truth inside +-U: EVT2-A baud_err and every gap (10 MS/s)",
          lambda: abs(ua["baud_err"][0] - (G.clocks(26.0)[1] / 115200 - 1) * 100)
          <= meas_uncertainty("baud_err", ua, 100000, la)
          and all(abs(g - t / (10 * PS / 115200)) <= meas_uncertainty("gap", ua, 100000, la)
                  for g, t in zip(ua["gap"], G.BOARDS["EVT2-A"]["gaps_ps"])))

    print("-- Part C: EVT2 violation reports vs datasheet table (evt2_limits.json) --")
    rep = {}

    def run_reports():
        for key, label, s, u in (("A", "EVT2-A", A100, AU10), ("B100", "EVT2-B", B100, BU10),
                                 ("B500", "EVT2-B", B500, BU10)):
            rep[key] = board_report(label, s, u, spec)
            for ln in rep[key][1]:
                print("      " + ln)
    info(run_reports)

    def verdicts(key):
        return {ln.split()[0]: ln for ln in rep[key][1] if ln.startswith("  ")}
    check("EVT2-A: FAIL on fSCLK, tCSH, baud_err, gap",
          lambda: all(" FAIL " in verdicts("A")[k] for k in ("fSCLK", "tCSH", "baud_err", "gap"))
          and rep["A"][0] == "FAIL")
    check("EVT2-A one-line: common clock hint 26 MHz xtal + BSY hint + DMA hint",
          lambda: "26 MHz xtal" in rep["A"][2] and "BSY" in rep["A"][2] and "DMA" in rep["A"][2])
    check("EVT2-B @100M: no FAIL, tCSS UNRESOLVED -> overall UNRESOLVED with re-capture advice",
          lambda: rep["B100"][0] == "UNRESOLVED" and "UNRESOLVED" in verdicts("B100")["tCSS"]
          and " FAIL " not in "".join(verdicts("B100").values()) and "re-capture" in rep["B100"][2])
    check("EVT2-B @500M: overall PASS (all 11 limits with guard band)",
          lambda: rep["B500"][0] == "PASS")
    check("EVT2-B @100M: naive comparison already 'decided' a row the guard band calls UNRESOLVED",
          lambda: any("UNRESOLVED" in ln.split("(naive")[0] and "(naive UNRESOLVED)" not in ln
                      for ln in verdicts("B100").values()))

    def no_toggle():
        w = G.spi_wave(9.6e6, frames=[([0, 0, 0], [0xFF, 0, 0])] * 3, seed=5)
        return board_report("EVT2-B/zeros", G.resample(w, 5 * 10 ** 8, 0), BU10, spec)
    check("coverage: MOSI never toggles (all 0x00) -> tSU/tH NO DATA -> overall not PASS",
          lambda: no_toggle()[0] == "UNRESOLVED" and "NO DATA" in "\n".join(no_toggle()[1]))

    print("%d/%d passed  (%.1f s)" % (g_pass, g_total, time.time() - t_start))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
