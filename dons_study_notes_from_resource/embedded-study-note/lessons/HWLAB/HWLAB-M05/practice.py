#!/usr/bin/env python3
"""HWLAB-M05 추가 연습 — 한 번에 검증

  연습 1  miso_tv: SCLK falling -> MISO 변화 (출력 유효 시간 tV, max 스펙) + 가드밴드
  연습 2  min_fs_for: 진짜 마진 M 을 '반드시' 판정하려면 필요한 샘플링률 (Ts < M/2 - skew)
  연습 3  deglitch + 견고한 baud 추정: 1~2 샘플 글리치가 히스토그램 T0 를 망가뜨리는 것을 막기
  연습 4  Modbus RTU t1.5 / t3.5: inter-character gap 분류 (19200 이하 = 문자 시간 비례, 초과 = 750/1750 us 고정)

  python3 -I practice.py     # "N/M passed", 실패 시 exit 1
"""
import bisect
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import gen_capture as G  # noqa: E402
import solution as S  # noqa: E402

PS = S.PS


# ---------------------------------------------------------------- 연습 1
def miso_tv(cap, cs="CS_N", sclk="SCLK", miso="MISO"):
    """CS_N 창 안에서 SCLK falling 마다, 다음 rising 전에 일어난 첫 MISO 변화까지의 시간 (ns).
    MISO 가 안 바뀐 비트는 측정 불가 (tSU/tH 와 같은 커버리지 문제)."""
    falls = [t for t, v in cap["tr"][sclk][1:] if v == 0]
    rises = [t for t, v in cap["tr"][sclk][1:] if v == 1]
    mt = [t for t, _ in cap["tr"][miso][1:]]
    out = []
    for lo, hi in S.cs_windows(cap, cs):
        for f in falls[bisect.bisect_left(falls, lo):bisect.bisect_left(falls, hi)]:
            j = bisect.bisect_right(rises, f)
            nxt = min(rises[j] if j < len(rises) else hi, hi)
            k = bisect.bisect_left(mt, f)
            if k < len(mt) and mt[k] < nxt:
                out.append((mt[k] - f) / 1000.0)
    return out


# ---------------------------------------------------------------- 연습 2
def min_fs_for(margin_ns, skew_ns, rates_msps):
    """진짜 값이 한계에서 margin 만큼 떨어져 있을 때 가드밴드 판정이 *반드시* 결정되려면:
    측정 오차 |m - x| < Ts + skew 이고 판정은 |m - L| > U = Ts + skew 를 요구
    -> 최악 |m - L| = M - (Ts + skew) > Ts + skew  ->  Ts < M/2 - skew.  없으면 None (스코프의 일)."""
    for r in sorted(rates_msps):
        ts = 1000.0 / r
        if ts < margin_ns / 2 - skew_ns:
            return r
    return None


# ---------------------------------------------------------------- 연습 3
def deglitch(cap, ch, min_ps):
    """폭 < min_ps 인 펄스를 양쪽 에지째 지운다 (LA 의 glitch filter 와 같은 일). 반환 (새 캡처, 지운 펄스 수)"""
    tr = list(cap["tr"][ch])
    removed = 0
    j = 1
    while j < len(tr) - 1:
        if tr[j + 1][0] - tr[j][0] < min_ps:
            del tr[j:j + 2]
            removed += 1
            j = max(1, j - 1)
            continue
        j += 1
    out = [tr[0]]
    for t, v in tr[1:]:                 # 지운 뒤 같은 레벨이 연속되면 합친다
        if v != out[-1][1]:
            out.append((t, v))
    c = dict(cap)
    c["tr"] = dict(cap["tr"])
    c["tr"][ch] = out
    return c, removed


def uart_measure_robust(cap, ch="TX", baud_nom=115200, min_ps=500 * 1000):
    clean, n = deglitch(cap, ch, min_ps)
    m = S.uart_measure(clean, ch, baud_nom)
    m["glitches"] = n
    return m


def glitched_uart(n_glitch=12, width_ps=200 * 1000, seed=9):
    """EVT2-B UART 파형에 크로스토크 글리치 (200 ns 펄스) 를 심는다."""
    w = G.uart_wave(G.clocks(24.0)[1], gaps_ps=G.BOARDS["EVT2-B"]["gaps_ps"], seed=23)
    rng = random.Random(seed)
    tog = list(w["toggles"]["TX"])
    for _ in range(n_glitch):
        t = rng.randrange(w["toggles"]["TX"][0], w["toggles"]["TX"][-1])
        tog += [t, t + width_ps]
    w = dict(w)
    w["toggles"] = {"TX": sorted(tog)}
    return w


# ---------------------------------------------------------------- 연습 4
def modbus_timers_us(baud):
    """Modbus over Serial Line v1.02: RTU 문자 = 11 비트. baud <= 19200 이면 t1.5/t3.5 = 1.5/3.5 문자 시간,
    초과면 고정 750 us / 1750 us."""
    if baud > 19200:
        return 750.0, 1750.0
    tc = 11 * 1e6 / baud
    return 1.5 * tc, 3.5 * tc


def modbus_classify(gaps_us, baud):
    """문자 사이 silent interval 마다: < t1.5 'OK', t1.5 이상 t3.5 미만 'ERR', t3.5 이상 'END' (프레임 경계)"""
    t15, t35 = modbus_timers_us(baud)
    return ["OK" if g < t15 else ("ERR" if g < t35 else "END") for g in gaps_us]


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


def main():
    spec = S.load_spec()
    la = spec["la"]
    lim_tv = {"max": 20.0}                      # 가상 U7 tV max 20 ns (대표값)
    wb, _ = G.board_waves("EVT2-B")

    print("-- 연습 1: MISO tV (max) --")
    tv_true = miso_tv(G.ideal(wb))
    res = {}
    for name, fs in (("evt2b_spi_100M.sr", 100), ("evt2b_spi_500M.sr", 500)):
        cap = S.load_sr(os.path.join(HERE, name))
        tv = miso_tv(cap)
        u = la["skew_ns"] + 1000.0 / fs + la["timebase_ppm"] * 1e-6 * max(tv)
        res[fs] = (tv, u, S.guard_verdict(min(tv), max(tv), u, lim_tv))
        print("      %3d MS/s tV n=%d max=%.1f +- %.2f ns (truth max %.2f) -> %s (naive %s)"
              % (fs, len(tv), max(tv), u, max(tv_true), res[fs][2], S.guard_verdict(min(tv), max(tv), 0, lim_tv)))
    check("ideal tV = 12 ns (+-0.6 jitter) for every MISO change, same count as captures",
          lambda: all(abs(v - 12) < 0.6 for v in tv_true) and len(res[100][0]) == len(tv_true) == len(res[500][0]))
    check("100 MS/s -> UNRESOLVED, 500 MS/s -> PASS; truth max inside +-U both",
          lambda: res[100][2] == "UNRESOLVED" and res[500][2] == "PASS"
          and all(abs(max(r[0]) - max(tv_true)) <= r[1] for r in res.values()))

    print("-- 연습 2: required sample rate --")
    rates = la["rates_msps"]
    table = [(m, min_fs_for(m, la["skew_ns"], rates)) for m in (3.0, 6.0, 26.0, 40.0)]
    print("      margin -> min rate:", ", ".join("%g ns -> %s" % (m, r) for m, r in table))
    check("margin 6 ns (tCSS 26 vs 20) -> 1000 MS/s; 3 ns -> None (scope); 26 ns -> 100 MS/s",
          lambda: dict(table) == {3.0: None, 6.0: 1000, 26.0: 100, 40.0: 100})

    def decided(fs, n=60):
        w = G.spi_wave(9.6e6, tcss_ps=26000, seed=6)
        rng = random.Random(8)
        out = 0
        for _ in range(n):
            m = S.spi_measure(G.resample(w, fs * 10 ** 6, rng.randrange(PS // (fs * 10 ** 6))))
            u = S.meas_uncertainty("tCSS", m, PS // (fs * 10 ** 6), la)
            out += S.guard_verdict(min(m["tCSS"]), 0, u, {"min": 20}) == "PASS"
        return out
    d = {fs: decided(fs) for fs in (100, 500, 1000)}
    print("      tCSS truth 26 ns: PASS decided in %s of 60 random phases" % d)
    check("tCSS 26 ns: 1000 MS/s decides 60/60 (rule holds); 100 MS/s decides 0/60",
          lambda: d[1000] == 60 and d[100] == 0)

    print("-- 연습 3: glitches vs histogram baud --")
    gw = glitched_uart()
    cap = G.resample(gw, 10 ** 7, 777)
    naive = None
    try:
        naive = S.uart_measure(cap)
    except Exception as e:  # noqa: BLE001
        print("      naive uart_measure raised %s: %s" % (type(e).__name__, e))
    if naive:
        print("      naive : T0=%.1f ns Tbit=%.1f ns bytes=%r" % (naive["t0_ns"], naive["tbit_ns"], naive["bytes"]))
    rob = uart_measure_robust(cap)
    u = S.meas_uncertainty("baud_err", rob, 100000, la)
    print("      robust: glitches=%d Tbit=%.2f ns baud_err=%+.3f +- %.3f %% bytes=%r"
          % (rob["glitches"], rob["tbit_ns"], rob["baud_err"][0], u, rob["bytes"]))
    true_err = (G.clocks(24.0)[1] / 115200 - 1) * 100
    check("naive histogram estimate is wrong (bytes != message) with 12 x 200 ns glitches",
          lambda: naive is None or naive["bytes"] != G.UART_MSG)
    check("deglitch(<500 ns) -> 12 glitches counted, bytes recovered, baud_err truth inside +-U",
          lambda: rob["glitches"] == 12 and rob["bytes"] == G.UART_MSG and abs(rob["baud_err"][0] - true_err) <= u)

    print("-- 연습 4: Modbus RTU t1.5 / t3.5 --")
    t19 = modbus_timers_us(19200)
    print("      19200: t1.5=%.1f us t3.5=%.1f us ; 115200: %s" % (t19[0], t19[1], modbus_timers_us(115200)))
    check("19200 baud: t1.5 = 859.4 us, t3.5 = 2005.2 us; > 19200: 750 / 1750 us fixed",
          lambda: abs(t19[0] - 859.375) < 1e-6 and abs(t19[1] - 2005.208) < 1e-3
          and modbus_timers_us(115200) == (750.0, 1750.0))
    ua = S.uart_measure(S.load_sr(os.path.join(HERE, "evt2a_uart_10M.sr")))
    gaps_us = [g * ua["_uart"]["tchar_ns"] / 1000 for g in ua["gap"]]
    cls = modbus_classify(gaps_us, 115200)
    print("      EVT2-A gaps as Modbus RTU @115200:", " ".join(cls))
    check("EVT2-A gaps (9 us / 180 us) are all 'OK' under Modbus 750 us rule (different protocol, different limit)",
          lambda: cls == ["OK"] * 15)
    check("classify at 19200: 500/900/2100 us -> OK/ERR/END",
          lambda: modbus_classify([500, 900, 2100], 19200) == ["OK", "ERR", "END"])

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
