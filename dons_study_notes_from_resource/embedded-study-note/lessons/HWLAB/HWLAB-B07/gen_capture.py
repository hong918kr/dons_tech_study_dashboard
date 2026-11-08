#!/usr/bin/env python3
"""HWLAB-B07 합성 로직 애널라이저 캡처 생성기 (결정적, stdlib only)

"보드"(EVT1, SPI NOR U12 = W25Q128JV 가정)의 SPI 버스를 연속 시간 모델로 만들고,
"로직 애널라이저"처럼 고정 샘플링률로 샘플링해서 세 가지 포맷으로 저장한다.

  evt1_spi_100M.sr   sigrok 세션 v2 (zip: version + metadata + logic-1-1..N 청크), 100 MS/s
  evt1_spi_100M.csv  Saleae Logic 식 CSV (transition 이 있는 시각만 한 줄)
  evt1_spi_100M.vcd  VCD (IEEE 1364 value change dump)
  evt1_spi_20M.sr    같은 버스를 20 MS/s 로 캡처 (Part C: 언더샘플링된 캡처, 샘플 위상 19 ns)

  python3 -I gen_capture.py      # 위 4개 파일을 다시 만든다 (같은 바이트, sha256 출력)

starter/solution/practice 는 이 모듈의 파형 모델(spi_waveform), 무차별 샘플러(sample_bytes),
writer(write_sr/write_csv/write_vcd)를 import 해서 쓴다.

모델 가정 (합성, 실측 아님):
  SCLK 10 MHz (주기 100 ns), LA 가 보는 high 폭 46 ns (듀티 46%: 출력단 rise/fall 비대칭 + LA 임계값),
  각 SCLK 에지 지터 가우시안 sigma 0.7 ns, SPI mode 0 (CPOL=0, CPHA=0, MSB first),
  MOSI/MISO 는 SCLK falling 후 7 ns 에 바뀜, CS_N->첫 rise 60 ns, 마지막 fall->CS_N rise 60 ns,
  MISO 는 hi-Z 일 때 풀업으로 1.
"""
import bisect
import hashlib
import os
import random
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

NS = 1000          # 시간 단위: ps
US = 1000 * NS
NAMES = ["CS_N", "SCLK", "MOSI", "MISO"]   # probe1..probe4 = bit0..bit3
INIT = {"CS_N": 1, "SCLK": 0, "MOSI": 0, "MISO": 1}
T_SCLK = 100 * NS
T_HIGH = 46 * NS
JITTER = 0.7 * NS
T_CO = 7 * NS       # master MOSI clock-to-out (falling 기준)
T_V = 7 * NS        # flash tCLQV 가정 (falling -> DO valid)
T_CSS = 60 * NS
T_CSH = 60 * NS
ZIP_TIME = (2026, 11, 8, 0, 0, 0)   # zip 타임스탬프 고정 -> 결정적 파일

JEDEC = ("JEDEC_ID", [0x9F, 0, 0, 0], [0xFF, 0xEF, 0x40, 0x18])
TRANSACTIONS = [
    JEDEC,
    ("RDSR1", [0x05, 0x00], [0xFF, 0x00]),
    ("READ", [0x03, 0, 0, 0] + [0] * 8, [0xFF] * 4 + list(b"BRDID=01")),
]
SEED = 707
PHASE_20M = 19 * NS   # 20 MS/s 캡처의 샘플 위상: 이 위상에서 JEDEC 프레임의 SCLK 펄스 일부가 샘플 사이로 빠진다


def _bits(byte_list):
    out = []
    for b in byte_list:
        out.extend((b >> (7 - i)) & 1 for i in range(8))
    return out


def spi_waveform(transactions=None, seed=SEED, t_high=T_HIGH, jitter=JITTER, miso_stuck=None):
    """연속 시간 SPI 파형. 반환: {"names", "init", "toggles": {name: [t_ps(float), ...]}, "t_end"}
    toggles 는 레벨이 실제로 바뀌는 시각만 (정렬됨)."""
    if transactions is None:
        transactions = TRANSACTIONS
    rng = random.Random(seed)
    ev = {n: [] for n in NAMES}
    t = rng.uniform(0.5, 1.0) * US
    cs_rise = t
    for _name, mo, mi in transactions:
        mob, mib = _bits(mo), _bits(mi)
        nbits = len(mob)
        cs_fall = t
        ev["CS_N"].append((cs_fall, 0))
        ev["MOSI"].append((cs_fall + 10 * NS, mob[0]))
        ev["MISO"].append((cs_fall + T_V, mib[0]))
        r0 = cs_fall + T_CSS
        f = r0
        for i in range(nbits):
            r = r0 + i * T_SCLK + rng.gauss(0.0, jitter)
            f = r0 + i * T_SCLK + t_high + rng.gauss(0.0, jitter)
            ev["SCLK"].append((r, 1))
            ev["SCLK"].append((f, 0))
            if i + 1 < nbits:
                ev["MOSI"].append((f + T_CO, mob[i + 1]))
                ev["MISO"].append((f + T_V, mib[i + 1]))
        cs_rise = f + T_CSH
        ev["CS_N"].append((cs_rise, 1))
        ev["MISO"].append((cs_rise + T_V, 1))          # DO -> hi-Z, 풀업
        t = cs_rise + rng.uniform(1.5, 2.5) * US
    if miso_stuck is not None:
        ev["MISO"] = []
    init = dict(INIT)
    if miso_stuck is not None:
        init["MISO"] = miso_stuck
    toggles = {}
    for n in NAMES:
        lvl, tl = init[n], []
        for tt, v in sorted(ev[n]):
            if v != lvl:
                tl.append(tt)
                lvl = v
        toggles[n] = tl
    return {"names": list(NAMES), "init": init, "toggles": toggles, "t_end": cs_rise + 1 * US}


def sample_bytes(wave, fs, phase_ps=0.0):
    """무차별(brute-force) 샘플러 = '진짜 LA'. 샘플 k 는 시각 phase + k*Ts 의 레벨.
    반환 (data bytes, unitsize). names[i] -> 비트 i, 샘플 워드는 little-endian."""
    ts = 1e12 / fs
    n = int((wave["t_end"] - phase_ps) // ts) + 1
    names = wave["names"]
    unitsize = (len(names) + 7) // 8
    out = bytearray()
    for k in range(n):
        tk = phase_ps + k * ts
        word = 0
        for i, nm in enumerate(names):
            lvl = wave["init"][nm] ^ (bisect.bisect_right(wave["toggles"][nm], tk) & 1)
            word |= lvl << i
        out += word.to_bytes(unitsize, "little")
    return bytes(out), unitsize


def samplerate_string(fs):
    """libsigrok sr_samplerate_string 흉내 ("100 MHz"). 정수 배수만 지원 (생성기 전용)."""
    fs = int(fs)
    for div, unit in ((10 ** 9, "GHz"), (10 ** 6, "MHz"), (10 ** 3, "kHz")):
        if fs % div == 0:
            return "%d %s" % (fs // div, unit)
    return "%d Hz" % fs


def _zput(z, name, payload):
    zi = zipfile.ZipInfo(name, date_time=ZIP_TIME)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    z.writestr(zi, payload)


def write_sr(dst, names, data, unitsize, fs, chunk_samples=200, order=None, legacy=False,
             sigrok_version="0.5.2"):
    """sigrok session v2 (.sr) 쓰기. dst = 경로 또는 BytesIO.
    order: 청크 번호를 zip 에 넣는 순서 (테스트용: 섞어서 넣기). legacy: 단일 'logic-1' 멤버."""
    meta = ["[global]", "sigrok version=%s" % sigrok_version, "",
            "[device 1]", "capturefile=logic-1", "total probes=%d" % len(names),
            "samplerate=%s" % samplerate_string(fs), "total analog=0"]
    meta += ["probe%d=%s" % (i + 1, nm) for i, nm in enumerate(names)]
    meta += ["unitsize=%d" % unitsize, ""]
    step = chunk_samples * unitsize
    chunks = [data[i:i + step] for i in range(0, len(data), step)] or [b""]
    with zipfile.ZipFile(dst, "w") as z:
        _zput(z, "version", b"2")
        _zput(z, "metadata", "\n".join(meta).encode("ascii"))
        if legacy:
            _zput(z, "logic-1", data)
        else:
            idx = list(range(1, len(chunks) + 1)) if order is None else order
            for i in idx:
                _zput(z, "logic-1-%d" % i, chunks[i - 1])


def transitions_from_samples(names, data, unitsize, ts_ps):
    """생성기 쪽 기준 구현 (LA 소프트웨어의 'export'). 샘플 -> {name: [(t_ps, level), ...]}"""
    n = len(data) // unitsize
    tr = {nm: [] for nm in names}
    prev = None
    for k in range(n):
        w = int.from_bytes(data[k * unitsize:(k + 1) * unitsize], "little")
        if prev is None or w != prev:
            for i, nm in enumerate(names):
                b = (w >> i) & 1
                if prev is None or b != ((prev >> i) & 1):
                    tr[nm].append((k * ts_ps, b))
        prev = w
    return tr


def _fmt_s(t_ps):
    """int ps -> 초 단위 10진 문자열, 9자리 (ns 해상도). ns 배수가 아니면 12자리."""
    sign = "-" if t_ps < 0 else ""
    a = abs(t_ps)
    if a % 1000 == 0:
        return "%s%d.%09d" % (sign, a // 10 ** 12, (a % 10 ** 12) // 1000)
    return "%s%d.%012d" % (sign, a // 10 ** 12, a % 10 ** 12)


def write_csv(dst, names, tr):
    """Saleae Logic 식 디지털 CSV: 'Time [s],<ch>,...' + 어떤 채널이든 바뀐 시각마다 한 줄."""
    times = sorted({t for nm in names for t, _ in tr[nm]})
    idx = {nm: 0 for nm in names}
    cur = {}
    lines = ["Time [s]," + ",".join(names)]
    for t in times:
        for nm in names:
            lst = tr[nm]
            while idx[nm] < len(lst) and lst[idx[nm]][0] <= t:
                cur[nm] = lst[idx[nm]][1]
                idx[nm] += 1
        lines.append(_fmt_s(t) + "," + ",".join(str(cur[nm]) for nm in names))
    text = "\n".join(lines) + "\n"
    if hasattr(dst, "write"):
        dst.write(text)
    else:
        with open(dst, "w", encoding="ascii", newline="\n") as f:
            f.write(text)


def write_vcd(dst, names, tr):
    """VCD: 1-bit wire 들, #시각 + 0!/1! 변화. 모든 시각이 ns 배수면 timescale 1 ns."""
    all_t = [t for nm in names for t, _ in tr[nm]]
    unit = 1000 if all(t % 1000 == 0 for t in all_t) else 1
    ids = {nm: chr(33 + i) for i, nm in enumerate(names)}
    out = ["$date 2026-11-08 $end", "$version HWLAB-B07 gen_capture.py $end",
           "$timescale 1 %s $end" % ("ns" if unit == 1000 else "ps"),
           "$scope module la $end"]
    out += ["$var wire 1 %s %s $end" % (ids[nm], nm) for nm in names]
    out += ["$upscope $end", "$enddefinitions $end"]
    ev = {}
    for nm in names:
        for t, v in tr[nm]:
            ev.setdefault(t, []).append("%d%s" % (v, ids[nm]))
    first = True
    for t in sorted(ev):
        out.append("#%d" % (t // unit))
        if first:
            out.append("$dumpvars")
            out += ev[t]
            out.append("$end")
            first = False
        else:
            out += ev[t]
    text = "\n".join(out) + "\n"
    if hasattr(dst, "write"):
        dst.write(text)
    else:
        with open(dst, "w", encoding="ascii", newline="\n") as f:
            f.write(text)


def build_files(outdir=HERE):
    wave = spi_waveform()
    written = []
    for fs, tag, ph in ((100_000_000, "100M", 0.0), (20_000_000, "20M", PHASE_20M)):
        data, us = sample_bytes(wave, fs, ph)
        p = os.path.join(outdir, "evt1_spi_%s.sr" % tag)
        write_sr(p, NAMES, data, us, fs)
        written.append(p)
        if tag == "100M":
            tr = transitions_from_samples(NAMES, data, us, 10 ** 12 // fs)
            for ext, fn in (("csv", write_csv), ("vcd", write_vcd)):
                q = os.path.join(outdir, "evt1_spi_%s.%s" % (tag, ext))
                fn(q, NAMES, tr)
                written.append(q)
    return written


def main():
    for p in build_files():
        with open(p, "rb") as f:
            b = f.read()
        print("%-20s %6d B  sha256=%s" % (os.path.basename(p), len(b), hashlib.sha256(b).hexdigest()[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
