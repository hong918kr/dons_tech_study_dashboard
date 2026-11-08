#!/usr/bin/env python3
"""HWLAB-B07 LA 캡처 포맷과 샘플링률: sigrok .sr · CSV · VCD

  Part A  sigrok .sr (zip: version + metadata + logic-1-N 청크) · Saleae 식 CSV · VCD 를
          하나의 공통 transition 리스트 {채널: [(t_ps, level), ...]} 로 읽는 로더
  Part B  10 MHz SPI 를 20/50/100 MS/s 로 다시 샘플링 -> 에지 위치 오차(+-1 샘플)와
          디코드 실패가 생기는 샘플링률 경계를 실험
  Part C  EVT1 보드 SPI NOR(U12) JEDEC ID 캡처 리포트: 캡처가 유효한가(오버샘플링) -> DUT PASS/FAIL

  python3 -I solution.py      # "N/M passed", 실패 시 exit 1

Python 3 표준 라이브러리만 (zipfile, csv 대신 직접 split, fractions, statistics ...).
"""
import io
import math
import os
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

PS_PER_S = 10 ** 12


# ---------------------------------------------------------------- 공통 표현 (주어짐)
def make_capture(names, tr, ts_ps=None, t_end=None, src=""):
    """공통 캡처 표현.
    tr[name] = [(t_ps, level), ...]  첫 원소 = 캡처 시작 시각의 초기 레벨, 이후는 레벨이 바뀐 시각만.
    level 은 0/1, 모르면(VCD x/z) None. ts_ps = 샘플 주기(ps) 또는 None(CSV 처럼 모를 때)."""
    return {"names": list(names), "tr": tr, "ts_ps": ts_ps, "t_end": t_end, "src": src}


def same_transitions(a, b):
    return a["names"] == b["names"] and all(a["tr"][n] == b["tr"][n] for n in a["names"])


def parse_ini(text):
    """sigrok metadata (GKeyFile 식 INI): [section] / key=value, '#' 줄 주석, '=' 주변 공백 무시."""
    out, sec = {}, None
    for line in re.split(r"\r\n|\n\r|\r|\n", text):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("[") and s.endswith("]"):
            sec = s[1:-1]
            out[sec] = {}
        elif "=" in s and sec is not None:
            k, v = s.split("=", 1)
            out[sec][k.strip()] = v.strip()
    return out


def _read_text(src):
    if hasattr(src, "read"):
        return src.read()
    with open(src, encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------- Part A: 로더
SI = {"": 1, "k": 10 ** 3, "M": 10 ** 6, "G": 10 ** 9}


def parse_samplerate(s):
    """ "100 MHz" / "1.5 MHz" / "200 kHz" / "1 GHz" / "500 Hz" / "20000000" -> int Hz. 이상하면 ValueError """
    m = re.match(r"^\s*([0-9]+(?:\.[0-9]+)?)\s*([kMG]?)(?:Hz)?\s*$", s)
    if not m:
        raise ValueError("bad samplerate %r" % s)
    v = Fraction(m.group(1)) * SI[m.group(2)]
    if v.denominator != 1 or v <= 0:
        raise ValueError("non-integer samplerate %r" % s)
    return int(v)


def samples_to_transitions(data, unitsize, probes, ts_ps):
    """data: 샘플당 unitsize 바이트 (little-endian), probes: {bit_index: name}
    -> {name: [(k*ts_ps, level), ...]} (첫 샘플 + 바뀐 샘플만). 길이가 unitsize 배수가 아니면 ValueError"""
    if unitsize <= 0 or len(data) % unitsize:
        raise ValueError("data length %d is not a multiple of unitsize %d" % (len(data), unitsize))
    bits = sorted(probes)
    tr = {probes[b]: [] for b in bits}
    prev = None
    for k in range(len(data) // unitsize):
        w = int.from_bytes(data[k * unitsize:(k + 1) * unitsize], "little")
        if w == prev:
            continue
        for b in bits:
            v = (w >> b) & 1
            if prev is None or v != ((prev >> b) & 1):
                tr[probes[b]].append((k * ts_ps, v))
        prev = w
    return tr


def load_sr(src):
    """sigrok session v2 .sr (경로 또는 파일 객체) -> 공통 캡처"""
    with zipfile.ZipFile(src) as z:
        members = z.namelist()
        ver = z.read("version").decode("ascii").strip()
        if ver != "2":
            raise ValueError("unsupported sigrok session version %r" % ver)
        dev = parse_ini(z.read("metadata").decode("utf-8"))["device 1"]
        fs = parse_samplerate(dev["samplerate"])
        unitsize = int(dev["unitsize"])
        nprobes = int(dev["total probes"])
        probes = {i - 1: dev["probe%d" % i] for i in range(1, nprobes + 1) if "probe%d" % i in dev}
        base = dev["capturefile"]
        pat = re.compile(re.escape(base) + r"-(\d+)$")
        chunks = sorted((int(m.group(1)), n) for n in members for m in [pat.match(n)] if m)
        if chunks:
            data = b"".join(z.read(n) for _, n in chunks)      # 숫자 순서! (logic-1-10 < logic-1-2 아님)
        elif base in members:
            data = z.read(base)                                # 구형 단일 'logic-1'
        else:
            raise ValueError("no logic data for capturefile=%s" % base)
    if PS_PER_S % fs:
        raise ValueError("sample period of %d Hz is not an integer ps" % fs)
    ts = PS_PER_S // fs
    names = [probes[b] for b in sorted(probes)]
    tr = samples_to_transitions(data, unitsize, probes, ts)
    n = len(data) // unitsize
    return make_capture(names, tr, ts, n * ts, getattr(src, "name", src) if not hasattr(src, "read") else "<mem>")


def load_saleae_csv(src):
    """Saleae 식 디지털 CSV: 'Time [s],ch0,ch1,...' 헤더 + transition 시각마다 한 줄 -> 공통 캡처.
    시간은 초(음수 = 트리거 이전 가능), Fraction 으로 정확히 ps 정수로 바꾼다. 샘플 주기는 모름(None)."""
    rows = [ln for ln in _read_text(src).splitlines() if ln.strip()]
    head = [h.strip() for h in rows[0].split(",")]
    if not head[0].lower().startswith("time"):
        raise ValueError("first column must be time, got %r" % head[0])
    names = head[1:]
    tr = {n: [] for n in names}
    for ln in rows[1:]:
        cells = [c.strip() for c in ln.split(",")]
        t = round(Fraction(cells[0]) * PS_PER_S)
        for n, c in zip(names, cells[1:]):
            v = int(c)
            if not tr[n] or tr[n][-1][1] != v:
                tr[n].append((t, v))
    return make_capture(names, tr, None, None, "<csv>")


VCD_UNITS = {"s": 10 ** 12, "ms": 10 ** 9, "us": 10 ** 6, "ns": 10 ** 3, "ps": 1}


def load_vcd(src):
    """최소 VCD 파서 (1-bit wire, $timescale, #t, 0/1/x/z 와 b1 id 표기) -> 공통 캡처.
    완전한 파서와 프로토콜 체크는 EMU-B07."""
    tok = _read_text(src).split()
    i, mult, ids, order, cur_t = 0, None, {}, [], 0
    tr = {}
    while i < len(tok):
        w = tok[i]
        if w == "$timescale":
            j = tok.index("$end", i)
            m = re.match(r"^(1|10|100)\s*(s|ms|us|ns|ps)$", "".join(tok[i + 1:j]))
            if not m:
                raise ValueError("bad timescale %r" % " ".join(tok[i + 1:j]))
            mult = int(m.group(1)) * VCD_UNITS[m.group(2)]
            i = j + 1
        elif w == "$var":
            j = tok.index("$end", i)
            _typ, width, code, ref = tok[i + 1:i + 5]
            if width != "1":
                raise ValueError("only 1-bit vars supported: %s" % ref)
            ids.setdefault(code, []).append(ref)
            order.append(ref)
            tr[ref] = []
            i = j + 1
        elif w in ("$dumpvars", "$dumpall", "$dumpon", "$dumpoff", "$end"):
            i += 1
        elif w.startswith("$"):
            i = tok.index("$end", i) + 1                   # $date $version $comment $scope ...
        elif w.startswith("#"):
            cur_t = int(w[1:]) * (mult if mult is not None else 1)
            i += 1
        else:
            if w[0] in "bB":
                val, code, i = w[1:], tok[i + 1], i + 2
            else:
                val, code, i = w[0], w[1:], i + 1
            v = {"0": 0, "1": 1}.get(val.lower())          # x/z -> None
            for ref in ids.get(code, []):
                if not tr[ref] or tr[ref][-1][1] != v:
                    tr[ref].append((cur_t, v))
    if mult is None:
        raise ValueError("missing $timescale")
    return make_capture(order, tr, None, None, "<vcd>")


# ---------------------------------------------------------------- Part B: 재샘플링 · 디코드
def resample(wave, fs, phase_ps=0.0):
    """연속 파형(G.spi_waveform) 을 fs 로 샘플링한 캡처 (샘플 k = phase + k*Ts).
    에지 t 는 처음으로 t <= t_k 인 샘플 k = ceil((t-phase)/Ts) 에 나타난다.
    같은 샘플 사이에 짝수 번 토글 -> 펄스가 통째로 사라짐. 샘플 순회 없이 에지만으로 계산."""
    ts = PS_PER_S / fs
    n = int((wave["t_end"] - phase_ps) // ts) + 1
    tr = {}
    for nm in wave["names"]:
        lvl = wave["init"][nm]
        per_k = {}
        for t in wave["toggles"][nm]:
            k = max(0, math.ceil((t - phase_ps) / ts))
            if k < n:
                per_k[k] = per_k.get(k, 0) + 1
        out = []
        if per_k.get(0, 0) % 2:
            lvl ^= 1
        out.append((round(phase_ps), lvl))
        for k in sorted(per_k):
            if k == 0 or per_k[k] % 2 == 0:
                continue
            lvl ^= 1
            out.append((round(phase_ps + k * ts), lvl))
        tr[nm] = out
    return make_capture(wave["names"], tr, ts, round(phase_ps + n * ts), "resample@%g" % fs)


def decode_spi(cap, cs="CS_N", sclk="SCLK", mosi="MOSI", miso="MISO"):
    """SPI mode 0 (CPOL=0, CPHA=0, MSB first) 디코더. CS_N low 구간 = 프레임, SCLK rising 에서
    MOSI/MISO 를 읽는다. 같은 시각의 변화는 모두 먼저 적용한 뒤 판정한다 (LA 샘플 의미론).
    반환: [{"t0", "t1", "bits", "mosi": bytes, "miso": bytes, "ok"}]  ok = bits>0, 8의 배수, x 없음, 완결"""
    chans = [cs, sclk, mosi, miso]
    st = {c: cap["tr"][c][0][1] for c in chans}
    events = sorted((t, c, v) for c in chans for (t, v) in cap["tr"][c][1:])
    frames, fr = [], None
    if st[cs] == 0:
        fr = {"t0": cap["tr"][cs][0][0], "mo": [], "mi": [], "partial": True}
    i = 0
    while i < len(events):
        t = events[i][0]
        prev = dict(st)
        while i < len(events) and events[i][0] == t:
            st[events[i][1]] = events[i][2]
            i += 1
        if prev[cs] == 1 and st[cs] == 0:
            fr = {"t0": t, "mo": [], "mi": [], "partial": False}
        if fr is not None and prev[sclk] == 0 and st[sclk] == 1:
            fr["mo"].append(st[mosi])
            fr["mi"].append(st[miso])
        if fr is not None and prev[cs] == 0 and st[cs] == 1:
            frames.append(_close(fr, t))
            fr = None
    if fr is not None:
        fr["partial"] = True
        frames.append(_close(fr, None))
    return frames


def _close(fr, t1):
    nb = len(fr["mo"])
    clean = None not in fr["mo"] and None not in fr["mi"]

    def pack(bits):
        return bytes(int("".join(str(b) for b in bits[i:i + 8]), 2) for i in range(0, nb - nb % 8, 8)) if clean else b""
    ok = nb > 0 and nb % 8 == 0 and clean and not fr["partial"]
    return {"t0": fr["t0"], "t1": t1, "bits": nb, "mosi": pack(fr["mo"]), "miso": pack(fr["mi"]), "ok": ok}


# ---------------------------------------------------------------- Part C: 캡처 유효성 + DUT 판정
def pulse_widths(cap, name, level=1):
    """level 인 펄스들의 폭(ps). 캡처 시작/끝에 걸친 펄스는 제외."""
    tr = cap["tr"][name]
    return [tr[j + 1][0] - tr[j][0] for j in range(1, len(tr) - 1) if tr[j][1] == level]


def rising_periods(cap, name, cs="CS_N"):
    """CS_N low 구간 안에서 연속 rising 에지 간격(ps)"""
    lows = [(a[0], b[0]) for a, b in zip(cap["tr"][cs], cap["tr"][cs][1:]) if a[1] == 0]
    rises = [t for (t, v) in cap["tr"][name][1:] if v == 1]
    out = []
    for lo, hi in lows:
        r = [t for t in rises if lo <= t <= hi]
        out += [b - a for a, b in zip(r, r[1:])]
    return out


def capture_report(cap, expect=(0xEF, 0x40, 0x18), min_oversample=4.0, label="capture"):
    """반환 (verdict, lines). verdict:
      INVALID  캡처 자체를 못 믿음 (샘플 주기 모름 / 오버샘플링 < min / SCLK high 가 1 샘플 이하)
      PASS     유효한 캡처 + 모든 프레임 정상 + JEDEC ID == expect
      FAIL     유효한 캡처인데 DUT 응답이 틀림 (MISO stuck 등 힌트 포함)"""
    lines = []
    ts = cap["ts_ps"]
    if ts is None:
        return "INVALID", ["CAPTURE %s fs=unknown" % label,
                           "INVALID: sample period unknown (transition-only export) -> cannot bound edge error"]
    fs = PS_PER_S / ts
    per = rising_periods(cap, "SCLK")
    f_sclk = PS_PER_S / statistics.median(per) if per else 0.0
    over = fs / f_sclk if f_sclk else 0.0
    hi = pulse_widths(cap, "SCLK", 1)
    min_hi = round(min(hi) / ts) if hi else 0
    frames = decode_spi(cap)
    jed = None
    for fr in frames:
        if fr["mosi"][:1] == b"\x9f" and len(fr["miso"]) >= 4:
            jed = tuple(fr["miso"][1:4])
    nok = sum(fr["ok"] for fr in frames)
    lines.append("CAPTURE %s fs=%gMS/s Ts=%.1fns" % (label, fs / 1e6, ts / 1000))
    lines.append("SCLK f=%.2fMHz oversample=%.1fx min_high=%d smp (%.1fns)"
                 % (f_sclk / 1e6, over, min_hi, min_hi * ts / 1000))
    lines.append("DECODE frames=%d ok=%d JEDEC=%s bits=%s" % (
        len(frames), nok, "".join("%02X" % b for b in jed) if jed else "none", [fr["bits"] for fr in frames]))
    why = []
    if over < min_oversample:
        why.append("oversample %.1fx < %.1fx" % (over, min_oversample))
    if min_hi < 2:
        why.append("SCLK high = %d sample" % min_hi)
    if why:
        need = math.ceil(min_oversample * f_sclk / 1e6)
        lines.append("INVALID: %s -> re-capture at >= %d MS/s; decode above is NOT evidence about the DUT"
                     % (", ".join(why), need))
        return "INVALID", lines
    exp = "".join("%02X" % b for b in expect)
    if nok == len(frames) and jed == tuple(expect):
        lines.append("PASS: JEDEC=%s (expected %s), %d/%d frames clean" % (exp, exp, nok, len(frames)))
        return "PASS", lines
    miso_bytes = b"".join(fr["miso"][1:] for fr in frames)
    hint = "JEDEC mismatch"
    if miso_bytes and set(miso_bytes) == {0xFF}:
        hint = "MISO stuck high: flash not driving DO (check VCC, CS_N routing, HOLD#/RESET#, pin mux)"
    elif miso_bytes and set(miso_bytes) == {0x00}:
        hint = "MISO stuck low: short to GND or contention"
    lines.append("FAIL: JEDEC=%s expected %s, %d/%d frames clean -> %s"
                 % ("".join("%02X" % b for b in jed) if jed else "none", exp, nok, len(frames), hint))
    return "FAIL", lines


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


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def p(name):
    return os.path.join(HERE, name)


def edge_errors(wave, cap, name="SCLK"):
    """샘플된 rising 에지마다 (샘플 시각 - 그 직전의 진짜 rising 에지) [ps]"""
    tog = wave["toggles"][name]
    init = wave["init"][name]
    true_r = [t for j, t in enumerate(tog) if (init ^ ((j + 1) & 1)) == 1]
    out = []
    for t, v in cap["tr"][name][1:]:
        if v == 1:
            cand = [x for x in true_r if x <= t]
            out.append(t - cand[-1])
    return out


def sweep(rates_msps, n_frames=200, seed=7):
    """JEDEC 프레임 n 개 (파형마다 다른 지터·시작 위치) x 무작위 샘플 위상 -> 샘플링률별 실패 프레임 수"""
    import random
    rng = random.Random(seed)
    waves = [G.spi_waveform([G.JEDEC], seed=1000 + j) for j in range(n_frames)]
    want = (bytes(G.JEDEC[1]), bytes(G.JEDEC[2]))
    res = {}
    for r in rates_msps:
        fs = r * 1e6
        fail = 0
        for w in waves:
            fr = decode_spi(resample(w, fs, rng.uniform(0, PS_PER_S / fs)))
            if not (len(fr) == 1 and fr[0]["ok"] and (fr[0]["mosi"], fr[0]["miso"]) == want):
                fail += 1
        res[r] = fail
    return res


def run_tests():
    print("-- Part A: loaders --")
    check("parse_samplerate('100 MHz') == 100000000", lambda: parse_samplerate("100 MHz") == 100_000_000)
    check("parse_samplerate('1.5 MHz'/'200 kHz'/'1 GHz'/'20000000')", lambda: (
        parse_samplerate("1.5 MHz"), parse_samplerate("200 kHz"), parse_samplerate("1 GHz"),
        parse_samplerate("20000000")) == (1_500_000, 200_000, 10 ** 9, 20_000_000))
    check("parse_samplerate('fast') -> ValueError", lambda: raises(ValueError, lambda: parse_samplerate("fast")))
    sr = csv_ = vcd = None
    try:
        sr = load_sr(p("evt1_spi_100M.sr"))
        csv_ = load_saleae_csv(p("evt1_spi_100M.csv"))
        vcd = load_vcd(p("evt1_spi_100M.vcd"))
    except Exception as e:  # noqa: BLE001
        print("      (loader raised %s: %s)" % (type(e).__name__, e))
    check(".sr: names/ts/length from metadata", lambda: sr["names"] == G.NAMES and sr["ts_ps"] == 10_000
          and sr["t_end"] == 1974 * sr["ts_ps"])
    check(".sr: 10 chunks logic-1-1..10 joined in numeric order", lambda: len(
        [n for n in zipfile.ZipFile(p("evt1_spi_100M.sr")).namelist() if n.startswith("logic-1-")]) == 10
          and sr["tr"]["SCLK"][0] == (0, 0))
    check(".sr: SCLK rising edges == 8 x (4+2+12) bytes = 144",
          lambda: sum(v for _, v in sr["tr"]["SCLK"][1:]) == 144)
    check("CSV == .sr (same transitions)", lambda: same_transitions(sr, csv_))
    check("VCD == .sr (same transitions)", lambda: same_transitions(sr, vcd))
    check("CSV/VCD keep no sample period (ts_ps is None)", lambda: csv_["ts_ps"] is None and vcd["ts_ps"] is None)

    print("-- Part B: format traps --")
    data = bytes(range(16)) * 160                       # 2560 samples, unitsize 1
    names8 = ["D%d" % i for i in range(8)]

    def mk(**kw):
        b = io.BytesIO()
        G.write_sr(b, names8, data, 1, 1_000_000, chunk_samples=200, **kw)
        b.seek(0)
        return load_sr(b)
    check(".sr chunks stored shuffled (10,2,13,1,...) load the same", lambda: same_transitions(
        mk(), mk(order=[10, 2, 13, 1, 12, 3, 11, 4, 5, 6, 7, 8, 9])))
    check(".sr legacy single 'logic-1' member loads the same", lambda: same_transitions(mk(), mk(legacy=True)))

    def u2():
        words = [0x0000, 0x0801, 0x0801, 0x0FFF, 0x0000]
        b = io.BytesIO()
        G.write_sr(b, ["P%d" % i for i in range(1, 13)], b"".join(w.to_bytes(2, "little") for w in words),
                   2, 1_000_000)
        b.seek(0)
        return load_sr(b)
    check("unitsize=2: probe12 = bit 11 of little-endian word", lambda: u2()["tr"]["P12"]
          == [(0, 0), (1_000_000, 1), (4_000_000, 0)] and u2()["tr"]["P9"] == [(0, 0), (3_000_000, 1), (4_000_000, 0)])
    check("truncated sample data -> ValueError", lambda: raises(
        ValueError, lambda: samples_to_transitions(b"\x00\x01\x02", 2, {0: "A"}, 1)))
    check("CSV pre-trigger negative time, exact ps", lambda: load_saleae_csv(io.StringIO(
        "Time [s], A, B\n-0.000000100,0,1\n0.000000000,1,1\n0.000000250,1,0\n"))["tr"]
        == {"A": [(-100_000, 0), (0, 1)], "B": [(-100_000, 1), (250_000, 0)]})
    check("VCD timescale 10 ns, x initial, 'b1 id' form", lambda: load_vcd(io.StringIO(
        "$timescale 10 ns $end $scope module t $end $var wire 1 %a clk $end $upscope $end "
        "$enddefinitions $end #0 $dumpvars x%a $end #3 1%a #7 b0 %a\n"))["tr"]
        == {"clk": [(0, None), (30_000, 1), (70_000, 0)]})

    print("-- Part B: sample-rate experiment (10 MHz SCLK, high 46 ns) --")
    wave = G.spi_waveform()
    for fs in (100e6, 50e6, 20e6):
        ph = 3_141.0
        check("resample(%gM) == brute-force sampling" % (fs / 1e6), lambda fs=fs, ph=ph: same_transitions(
            resample(wave, fs, ph), make_capture(G.NAMES, {
                n: [(round(ph + t), v) for t, v in tr] for n, tr in G.transitions_from_samples(
                    G.NAMES, *G.sample_bytes(wave, fs, ph), PS_PER_S / fs).items()})))
    caps = {}
    for r, ph in ((100, 0.0), (50, 0.0), (20, G.PHASE_20M)):
        try:
            caps[r] = resample(wave, r * 1e6, ph)
        except Exception:  # noqa: BLE001
            caps[r] = None

    def coherent():
        e = edge_errors(wave, caps[50])
        print("       50 MS/s  this one run: rising-edge error %.1f..%.1f ns (10 MHz vs 50 MS/s = fixed phase)"
              % (min(e) / 1000, max(e) / 1000))
        return max(e) - min(e) < 10_000
    check("one run at 50 MS/s: edge errors cluster (spread < Ts/2), not uniform", coherent)
    import random
    rng = random.Random(181)
    pool = [G.spi_waveform([G.JEDEC], seed=2000 + j) for j in range(40)]
    for r in (100, 50, 20):
        ts = PS_PER_S / (r * 1e6)

        def err_ok(r=r, ts=ts):
            e = []
            for w in pool:
                e += edge_errors(w, resample(w, r * 1e6, rng.uniform(0, ts)))
            print("      %3d MS/s  Ts=%2.0fns  40 runs, random phase: rising-edge error min %4.1f  mean %4.1f"
                  "  max %4.1f ns  (n=%d)" % (r, ts / 1000, min(e) / 1000, statistics.mean(e) / 1000,
                                              max(e) / 1000, len(e)))
            return all(0 <= x < ts for x in e) and max(e) > 0.9 * ts and min(e) < 0.1 * ts
        check("%d MS/s: edge error spans [0, Ts) = 1 sample" % r, err_ok)

    def periods(r):
        per = rising_periods(caps[r], "SCLK")
        hist = {}
        for x in per:
            hist[round(x / 1000)] = hist.get(round(x / 1000), 0) + 1
        print("      %3d MS/s  measured SCLK period histogram [ns:count] %s" % (r, sorted(hist.items())))
        return hist
    check("100 MS/s: period reads 90/100/110 ns only (+-1 sample)",
          lambda: set(periods(100)) <= {90, 100, 110})
    check("50 MS/s: period reads 80/100/120 ns only (+-1 sample)",
          lambda: set(periods(50)) <= {80, 100, 120})
    check("20 MS/s capture of this board run: decode broken (lost SCLK pulse)", lambda: not all(
        f["ok"] for f in decode_spi(caps[20])))
    check("100 MS/s capture decodes JEDEC EF 40 18", lambda: decode_spi(caps[100])[0]["miso"] == b"\xff\xef\x40\x18")
    t0 = time.time()
    res = {}
    try:
        res = sweep([16, 18, 20, 22, 23, 24, 25, 26, 28, 30, 40, 50, 100])
        print("      failed frames /200 by MS/s: %s  (%.1f s)" % (res, time.time() - t0))
    except Exception as e:  # noqa: BLE001
        print("      (sweep raised %s: %s)" % (type(e).__name__, e))
    check("sweep: 0 failures at 30/40/50/100 MS/s, > 0 at 20 MS/s", lambda: all(
        res[r] == 0 for r in (30, 40, 50, 100)) and res[20] > 0)
    check("sweep: boundary (lowest rate with 0 failures from there up) is in 22..30 MS/s", lambda: 22 <= min(
        r for r in res if all(res[q] == 0 for q in res if q >= r)) <= 30)

    print("-- Part C: EVT1 U12 JEDEC capture report --")
    reps = {}
    for fn in ("evt1_spi_20M.sr", "evt1_spi_100M.sr"):
        try:
            v, lines = capture_report(load_sr(p(fn)), label=fn)
            reps[fn] = v
            print("\n".join("      " + ln for ln in lines))
        except Exception as e:  # noqa: BLE001
            print("      (report raised %s: %s)" % (type(e).__name__, e))
    check("20 MS/s capture -> INVALID (not a DUT verdict)", lambda: reps.get("evt1_spi_20M.sr") == "INVALID")
    check("100 MS/s capture -> PASS JEDEC EF4018", lambda: reps.get("evt1_spi_100M.sr") == "PASS")

    def stuck():
        w = G.spi_waveform(miso_stuck=1)
        data, us = G.sample_bytes(w, 100e6)
        b = io.BytesIO()
        G.write_sr(b, G.NAMES, data, us, 100_000_000)
        b.seek(0)
        v, lines = capture_report(load_sr(b), label="evt1_u12_miso_stuck(100M)")
        print("\n".join("      " + ln for ln in lines))
        return v == "FAIL" and "MISO stuck high" in lines[-1]
    check("planted defect: MISO stuck high at 100 MS/s -> FAIL with hint", stuck)
    check("CSV export (no sample period) -> INVALID", lambda: capture_report(
        load_saleae_csv(p("evt1_spi_100M.csv")))[0] == "INVALID")

    def roundtrip():
        a = load_sr(p("evt1_spi_100M.sr"))
        v, c = io.StringIO(), io.StringIO()
        G.write_vcd(v, a["names"], a["tr"])
        G.write_csv(c, a["names"], a["tr"])
        v.seek(0)
        c.seek(0)
        return same_transitions(a, load_vcd(v)) and same_transitions(a, load_saleae_csv(c))
    check("round trip .sr -> VCD/CSV -> load == original", roundtrip)

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
