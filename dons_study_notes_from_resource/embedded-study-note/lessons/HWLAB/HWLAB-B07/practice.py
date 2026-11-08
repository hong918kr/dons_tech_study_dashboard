#!/usr/bin/env python3
"""HWLAB-B07 추가 연습 — 한 번에 검증

  연습 1  infer_ts: transition 만 있는 CSV 에서 샘플 주기 추정 (시간 차이의 GCD)
  연습 2  decode_spi_mode: CPOL/CPHA 4 모드 디코더
  연습 3  interval_verdict + tCLQV: 샘플링 캡처로 잰 시간 간격의 +-Ts 가드밴드 판정 (PASS/FAIL/UNRESOLVED)
  연습 4  csv_to_sr: CSV(transition) -> sigrok .sr(샘플) 변환, 왕복 일치

  python3 -I practice.py     # "N/M passed", 실패 시 exit 1
"""
import io
import math
import os
import sys
from functools import reduce

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True   # 같은 디렉터리 모듈 import 시 __pycache__ 를 남기지 않는다
sys.path.insert(0, HERE)
import gen_capture as G  # noqa: E402
import solution as S  # noqa: E402

PS = S.PS_PER_S


# ---------------------------------------------------------------- 연습 1
def infer_ts(cap):
    """모든 채널 transition 시각 차이의 GCD (ps). 진짜 Ts 의 정수배 (>= Ts) 라는 것만 보장된다."""
    ts = sorted({t for n in cap["names"] for t, _ in cap["tr"][n]})
    return reduce(math.gcd, (b - a for a, b in zip(ts, ts[1:])), 0)


# ---------------------------------------------------------------- 연습 2
def decode_spi_mode(cap, cpol, cpha, cs="CS_N", sclk="SCLK", mosi="MOSI", miso="MISO"):
    """샘플 에지: leading edge(=CPOL 반대 레벨로 가는 에지) if CPHA=0 else trailing edge.
    CPOL=0: leading = rising. CPOL=1: leading = falling. -> 샘플 에지가 rising 인 경우 = (CPOL xor CPHA) == 0"""
    sample_on = 1 if (cpol ^ cpha) == 0 else 0
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
            fr = {"t0": t, "mo": [], "mi": [], "partial": prev[sclk] != cpol}
        if fr is not None and prev[sclk] != st[sclk] and st[sclk] == sample_on:
            fr["mo"].append(st[mosi])
            fr["mi"].append(st[miso])
        if fr is not None and prev[cs] == 0 and st[cs] == 1:
            frames.append(S._close(fr, t))
            fr = None
    return frames


def invert(cap, name):
    c = dict(cap)
    c["tr"] = dict(cap["tr"])
    c["tr"][name] = [(t, 1 - v) for t, v in cap["tr"][name]]
    return c


# ---------------------------------------------------------------- 연습 3
def interval_verdict(meas_ps, ts_ps, spec_ps, kind="max"):
    """샘플된 두 에지의 간격 meas 의 진짜 값은 (meas - Ts, meas + Ts) 안 (각 에지 오차가 [0, Ts)).
    kind="max": 진짜 <= spec 를 증명해야 PASS. kind="min": 진짜 >= spec."""
    lo, hi = meas_ps - ts_ps, meas_ps + ts_ps
    if kind == "max":
        return "PASS" if hi <= spec_ps else ("FAIL" if lo >= spec_ps else "UNRESOLVED")
    return "PASS" if lo >= spec_ps else ("FAIL" if hi <= spec_ps else "UNRESOLVED")


def tclqv(cap, sclk="SCLK", miso="MISO", window_ps=50_000):
    """SCLK falling -> 그 뒤 window 안의 첫 MISO 변화 까지의 지연 목록 (ps)"""
    falls = [t for t, v in cap["tr"][sclk][1:] if v == 0]
    mch = [t for t, _ in cap["tr"][miso][1:]]
    out, j = [], 0
    for f in falls:
        while j < len(mch) and mch[j] < f:
            j += 1
        if j < len(mch) and mch[j] - f <= window_ps:
            out.append(mch[j] - f)
    return out


def worst(verdicts):
    for v in ("FAIL", "UNRESOLVED", "PASS"):
        if v in verdicts:
            return v
    return "NODATA"


# ---------------------------------------------------------------- 연습 4
def csv_to_sr(cap, fs, dst):
    """transition 캡처 -> .sr. 모든 시각이 Ts 정수배여야 한다 (아니면 ValueError). t0 를 0 으로 민다."""
    ts = PS // fs
    names = cap["names"]
    t0 = min(cap["tr"][n][0][0] for n in names)
    t_last = max(t for n in names for t, _ in cap["tr"][n])
    for n in names:
        for t, _ in cap["tr"][n]:
            if (t - t0) % ts:
                raise ValueError("%s edge at %d ps is not on the %d ps sample grid" % (n, t, ts))
    nsamp = (t_last - t0) // ts + 1
    unitsize = (len(names) + 7) // 8
    words = [0] * nsamp
    for i, n in enumerate(names):
        tr = cap["tr"][n]
        for j, (t, v) in enumerate(tr):
            k0 = (t - t0) // ts
            k1 = (tr[j + 1][0] - t0) // ts if j + 1 < len(tr) else nsamp
            if v:
                for k in range(k0, k1):
                    words[k] |= 1 << i
    data = b"".join(w.to_bytes(unitsize, "little") for w in words)
    G.write_sr(dst, names, data, unitsize, fs)


# ---------------------------------------------------------------- 하네스
check = S.check


def main():
    sr = S.load_sr(os.path.join(HERE, "evt1_spi_100M.sr"))
    csv_ = S.load_saleae_csv(os.path.join(HERE, "evt1_spi_100M.csv"))
    print("-- 연습 1: infer_ts --")
    check("CSV of 100 MS/s capture -> inferred 10 ns", lambda: infer_ts(csv_) == 10_000)
    check("only bounds Ts from above: 3 edges 40/80 ns apart -> 40 ns (true Ts could be 10/20/40)",
          lambda: infer_ts(S.make_capture(["A"], {"A": [(0, 0), (40_000, 1), (120_000, 0)]})) == 40_000)

    def rep():
        c = dict(csv_)
        c["ts_ps"] = infer_ts(csv_)
        v, lines = S.capture_report(c, label="evt1_spi_100M.csv (ts inferred)")
        print("      " + lines[-1])
        return v == "PASS"
    check("report on CSV with inferred ts -> PASS (label says 'inferred')", rep)

    print("-- 연습 2: decode_spi_mode --")
    want = [b"\x9f\x00\x00\x00", b"\x05\x00", bytes([3, 0, 0, 0] + [0] * 8)]
    check("mode 0 == solution.decode_spi", lambda: [f["mosi"] for f in decode_spi_mode(sr, 0, 0)] == want)
    inv = invert(sr, "SCLK")
    check("SCLK inverted, decoded as mode 2 (CPOL=1,CPHA=0) -> same bytes",
          lambda: [f["mosi"] for f in decode_spi_mode(inv, 1, 0)] == want)
    check("same capture decoded as mode 0 -> garbage", lambda: [f["mosi"] for f in decode_spi_mode(inv, 0, 0)] != want)
    wave = G.spi_waveform()
    want_mi = [b"\xff\xef\x40\x18", b"\xff\x00", b"\xff" * 4 + b"BRDID=01"]

    def cpha_mixup():
        ok0 = ok1 = 0
        for ph in range(0, 10_000, 1_000):
            c = S.resample(wave, 100e6, ph)
            ok0 += [f["miso"] for f in decode_spi_mode(c, 0, 0)] == want_mi
            ok1 += [f["miso"] for f in decode_spi_mode(c, 0, 1)] == want_mi
        print("      100 MS/s, 10 sample phases: MISO correct as mode 0: %d/10, as mode 1: %d/10" % (ok0, ok1))
        return ok0 == 10 and 0 < ok1 < 10
    check("CPHA mix-up (mode 1 on mode 0 bus): right only for some sample phases", cpha_mixup)

    print("-- 연습 3: guard-banded timing verdict --")
    check("interval_verdict max: 0+-10 vs 10 -> PASS, 10+-10 -> UNRESOLVED, 30+-10 -> FAIL",
          lambda: (interval_verdict(0, 10_000, 10_000), interval_verdict(10_000, 10_000, 10_000),
                   interval_verdict(30_000, 10_000, 10_000)) == ("PASS", "UNRESOLVED", "FAIL"))
    check("interval_verdict min: tSLCH 60+-10 ns vs min 5 ns -> PASS",
          lambda: interval_verdict(60_000, 10_000, 5_000, "min") == "PASS")

    def clqv(fs):
        c = S.resample(wave, fs, 0.0)
        d = tclqv(c)
        vs = [interval_verdict(x, c["ts_ps"], 10_000) for x in d]
        print("      %6g MS/s  tCLQV meas set (ns) %s  -> %s  (spec max 10 ns, model 7 ns)"
              % (fs / 1e6, sorted({round(x / 1000, 1) for x in d}), worst(vs)))
        return worst(vs)
    check("tCLQV <= 10 ns at 100 MS/s -> UNRESOLVED (Ts too coarse)", lambda: clqv(100e6) == "UNRESOLVED")
    check("tCLQV <= 10 ns at 1 GS/s -> PASS", lambda: clqv(1e9) == "PASS")

    print("-- 연습 4: csv_to_sr --")

    def rt():
        b = io.BytesIO()
        csv_to_sr(csv_, 100_000_000, b)
        b.seek(0)
        return S.same_transitions(S.load_sr(b), csv_)
    check("CSV -> .sr (100 MS/s) -> load == CSV transitions", rt)
    check("CSV -> .sr at 30 MS/s (33333 ps grid) -> ValueError",
          lambda: S.raises(ValueError, lambda: csv_to_sr(csv_, 30_000_000, io.BytesIO())))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return S.g_pass == S.g_total


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
