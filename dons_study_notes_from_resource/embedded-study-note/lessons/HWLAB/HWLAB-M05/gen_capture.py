#!/usr/bin/env python3
"""HWLAB-M05 합성 캡처 생성기 (결정적: 고정 시드, zip 타임스탬프 고정)

보드: EVT2 센서 허브. MCU U1 -> SPI ADC U7 (mode 0), MCU U1 UART TX -> BLE 모듈 U9 RX (115200 8N1).
클럭 설계: HSE 24 MHz -> PCLK 76.8 MHz -> SPI /8 = 9.6 MHz, UART BRR=667 -> 115142.4 baud.
  EVT2-A: 26 MHz 크리스털이 실장됐는데 펌웨어는 HSE_VALUE=24 MHz 가정 -> 모든 클럭 x26/24 (심어 둔 결함 1)
          + CS_N 을 TXE 만 보고 올림 -> 마지막 SCLK rising 4 ns 뒤 CS_N 상승 (결함 2)
          + UART TX 를 ISR 로 보내다 상위 ISR 에 밀려 바이트 간 180 us 공백 2곳 (결함 3)
  EVT2-B: 수정본 (HSE_VALUE 26->24 크리스털 교체, BSY 대기, DMA TX)

연속 시간 파형 = {"names", "init": {ch: lvl}, "toggles": {ch: [t_ps, ...]}, "t_end"} (정수 ps).
LA 샘플링 = resample(): 샘플 k (시각 phase + k*Ts) 는 그 시각까지 일어난 모든 에지를 본다
-> 에지 t 는 k = ceil((t - phase)/Ts) 에 나타난다 (HWLAB-B07 의 모델). 캡처 시각은 k*Ts (샘플 0 = 0 ps).

  python3 -I gen_capture.py      # 커밋된 .sr 5개를 다시 만든다
"""
import io
import os
import random
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PS = 10 ** 12
NS = 1000
US = 1000 * NS
ZIP_TIME = (2026, 11, 8, 0, 0, 0)
CHUNK = 4096                      # 바이트/청크 (libsigrok 은 4 MiB; 작은 파일에서도 logic-1-N 여러 개가 생기게)

SPI_CH = ["CS_N", "SCLK", "MOSI", "MISO"]
UART_CH = ["TX"]

DESIGN = {"hse_mhz": 24.0, "pclk_hz": 76.8e6, "sclk_div": 8, "brr": 667, "baud_nom": 115200}

# 레지스터 읽기/쓰기 4 프레임 (각 3바이트): (MOSI, MISO)
SPI_FRAMES = [
    ([0x8F, 0x00, 0x00], [0xFF, 0x6B, 0x00]),   # read ID
    ([0x0B, 0xA5, 0x3C], [0xFF, 0xFF, 0xFF]),   # write config
    ([0xC0, 0x00, 0x00], [0xFF, 0x7F, 0x3E]),   # read conversion
    ([0xC0, 0x00, 0x00], [0xFF, 0x7F, 0x41]),
]
UART_MSG = b"AT+SEND=T,23.5\r\n"

BOARDS = {
    "EVT2-A": {"xtal_mhz": 26.0, "tcsh_ps": 4 * NS,
               "gaps_ps": [9 * US] * 4 + [180 * US] + [9 * US] * 5 + [180 * US] + [9 * US] * 4},
    "EVT2-B": {"xtal_mhz": 24.0, "tcsh_ps": 58 * NS, "gaps_ps": [0] * 15},
}


def clocks(xtal_mhz):
    """크리스털 -> (SCLK Hz, UART baud). 펌웨어는 24 MHz 를 가정하고 분주비를 정했다."""
    pclk = DESIGN["pclk_hz"] * xtal_mhz / DESIGN["hse_mhz"]
    return pclk / DESIGN["sclk_div"], pclk / DESIGN["brr"]


def _bits_msb(bs):
    return [(b >> (7 - i)) & 1 for b in bs for i in range(8)]


def _finish(names, init, ev, t_end):
    toggles = {}
    for n in names:
        lvl, out = init[n], []
        for t, v in sorted(ev[n]):
            if v != lvl:
                out.append(t)
                lvl = v
        toggles[n] = out
    return {"names": list(names), "init": dict(init), "toggles": toggles, "t_end": t_end}


def spi_wave(f_hz, duty=0.48, tcss_ps=26 * NS, tcsh_ps=58 * NS, tcsd_ps=600 * NS, d_ps=3 * NS,
             tv_ps=12 * NS, jit_ps=150, frames=None, seed=1, t0_ps=300 * NS):
    """연속 시간 SPI mode 0 파형.
    SCLK: 주기 T=1/f, LA 가 보는 high = duty*T, 각 에지 지터 N(0, jit).
    tCSS = CS_N fall -> 첫 SCLK rise, tCSH = 마지막 SCLK rise -> CS_N rise, tCSD = CS_N high 시간.
    MOSI: 첫 비트는 CS_N fall + 5 ns, 이후 SCLK fall + d.  MISO: CS_N fall + 15 ns, 이후 SCLK fall + tv."""
    frames = SPI_FRAMES if frames is None else frames
    rng = random.Random(seed)
    T = PS / f_hz
    th = duty * T
    ev = {n: [] for n in SPI_CH}
    t = t0_ps
    f = t
    for mo, mi in frames:
        mob, mib = _bits_msb(mo), _bits_msb(mi)
        cs_f = t
        ev["CS_N"].append((cs_f, 0))
        ev["MOSI"].append((cs_f + 5 * NS, mob[0]))
        ev["MISO"].append((cs_f + 15 * NS, mib[0]))
        r0 = cs_f + tcss_ps
        r = r0
        for k in range(len(mob)):
            r = round(r0 + k * T + rng.gauss(0, jit_ps)) if k else r0
            f = round(r0 + k * T + th + rng.gauss(0, jit_ps))
            ev["SCLK"].append((r, 1))
            ev["SCLK"].append((f, 0))
            if k + 1 < len(mob):
                ev["MOSI"].append((f + d_ps, mob[k + 1]))
                ev["MISO"].append((f + tv_ps, mib[k + 1]))
        cs_r = r + tcsh_ps
        ev["CS_N"].append((cs_r, 1))
        ev["MOSI"].append((max(f, cs_r) + d_ps, 0))
        ev["MISO"].append((cs_r + 10 * NS, 1))            # DOUT hi-Z -> 풀업
        t = cs_r + tcsd_ps
    t_end = t + 300 * NS
    init = {"CS_N": 1, "SCLK": 0, "MOSI": 0, "MISO": 1}
    return _finish(SPI_CH, init, ev, t_end)


def uart_wave(baud_hz, data=UART_MSG, gaps_ps=None, jit_ps=1000, seed=2, t0_ps=40 * US):
    """연속 시간 UART 8N1 파형 (LSB first, idle high). gaps_ps[i] = 바이트 i 의 stop 끝 -> 바이트 i+1 start."""
    rng = random.Random(seed)
    tb = PS / baud_hz
    gaps_ps = [0] * (len(data) - 1) if gaps_ps is None else gaps_ps
    ev = {"TX": []}
    t = float(t0_ps)
    for i, byte in enumerate(data):
        bits = [0] + [(byte >> k) & 1 for k in range(8)] + [1]
        for k, b in enumerate(bits):
            ev["TX"].append((round(t + k * tb + rng.gauss(0, jit_ps)), b))
        t += 10 * tb + (gaps_ps[i] if i < len(gaps_ps) else 0)
    return _finish(UART_CH, {"TX": 1}, ev, round(t + 60 * US))


def resample(wave, fs, phase_ps=0):
    """LA 샘플링 (샘플을 순회하지 않고 에지마다 ceil). 같은 샘플 사이 짝수 토글 = 펄스 소실.
    반환 공통 캡처 {"names", "tr": {ch: [(t_ps, lvl), ...]}, "ts_ps", "t_end"} — 시각은 k*Ts."""
    if PS % fs:
        raise ValueError("fs must give integer ps period")
    ts = PS // fs
    n = (wave["t_end"] - phase_ps) // ts + 1
    tr = {}
    for nm in wave["names"]:
        per_k = {}
        for t in wave["toggles"][nm]:
            k = max(0, -((phase_ps - t) // ts))          # 정수 ceil((t - phase)/ts)
            if k < n:
                per_k[k] = per_k.get(k, 0) + 1
        lvl = wave["init"][nm] ^ (per_k.get(0, 0) & 1)
        out = [(0, lvl)]
        for k in sorted(per_k):
            if k and per_k[k] & 1:
                lvl ^= 1
                out.append((k * ts, lvl))
        tr[nm] = out
    return {"names": list(wave["names"]), "tr": tr, "ts_ps": ts, "t_end": n * ts}


def ideal(wave):
    """샘플링 없는 '진짜' transition (ts_ps=1 로 표시). 측정 함수의 정답 기준."""
    tr = {}
    for nm in wave["names"]:
        lvl = wave["init"][nm]
        out = [(0, lvl)]
        for t in wave["toggles"][nm]:
            lvl ^= 1
            out.append((t, lvl))
        tr[nm] = out
    return {"names": list(wave["names"]), "tr": tr, "ts_ps": 1, "t_end": wave["t_end"]}


def to_samples(cap):
    """공통 캡처 -> 샘플 바이트 (names[i] = 비트 i, little-endian, unitsize = ceil(ch/8))"""
    ts = cap["ts_ps"]
    n = cap["t_end"] // ts
    u = (len(cap["names"]) + 7) // 8
    words = [0] * n
    for i, nm in enumerate(cap["names"]):
        tr = cap["tr"][nm]
        for j, (t, v) in enumerate(tr):
            if not v:
                continue
            k0 = t // ts
            k1 = tr[j + 1][0] // ts if j + 1 < len(tr) else n
            for k in range(k0, min(k1, n)):
                words[k] |= 1 << i
    return b"".join(w.to_bytes(u, "little") for w in words), u


def _rate_str(fs):
    for div, unit in ((10 ** 9, "GHz"), (10 ** 6, "MHz"), (10 ** 3, "kHz")):
        if fs % div == 0:
            return "%d %s" % (fs // div, unit)
    return "%d Hz" % fs


def _zput(z, name, payload):
    zi = zipfile.ZipInfo(name, date_time=ZIP_TIME)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    z.writestr(zi, payload)


def write_sr(cap, dst, fs):
    """sigrok session v2 (.sr) 쓰기: version / metadata(INI) / logic-1-1..N"""
    data, u = to_samples(cap)
    meta = ["[global]", "sigrok version=0.5.2", "", "[device 1]", "capturefile=logic-1",
            "total probes=%d" % len(cap["names"]), "samplerate=%s" % _rate_str(fs), "total analog=0"]
    meta += ["probe%d=%s" % (i + 1, nm) for i, nm in enumerate(cap["names"])]
    meta += ["unitsize=%d" % u, ""]
    with zipfile.ZipFile(dst, "w") as z:
        _zput(z, "version", b"2")
        _zput(z, "metadata", "\n".join(meta).encode())
        for c in range(0, len(data), CHUNK):
            _zput(z, "logic-1-%d" % (c // CHUNK + 1), data[c:c + CHUNK])


def board_waves(board):
    b = BOARDS[board]
    f_sclk, baud = clocks(b["xtal_mhz"])
    seed = 11 if board == "EVT2-A" else 22
    sw = spi_wave(f_sclk, tcsh_ps=b["tcsh_ps"], seed=seed)
    uw = uart_wave(baud, gaps_ps=b["gaps_ps"], seed=seed + 1)
    return sw, uw


FILES = [  # (파일, 보드, spi|uart, fs)
    ("evt2a_spi_100M.sr", "EVT2-A", "spi", 100 * 10 ** 6),
    ("evt2a_uart_10M.sr", "EVT2-A", "uart", 10 * 10 ** 6),
    ("evt2b_spi_100M.sr", "EVT2-B", "spi", 100 * 10 ** 6),
    ("evt2b_spi_500M.sr", "EVT2-B", "spi", 500 * 10 ** 6),
    ("evt2b_uart_10M.sr", "EVT2-B", "uart", 10 * 10 ** 6),
]


def main():
    rng = random.Random(505)
    for fname, board, kind, fs in FILES:
        sw, uw = board_waves(board)
        wave = sw if kind == "spi" else uw
        phase = rng.randrange(PS // fs)
        buf = io.BytesIO()
        write_sr(resample(wave, fs, phase), buf, fs)
        with open(os.path.join(HERE, fname), "wb") as f:
            f.write(buf.getvalue())
        print("%-20s %-7s %-4s fs=%4d MS/s phase=%6d ps  %6d B" % (fname, board, kind, fs // 10 ** 6, phase,
                                                                    len(buf.getvalue())))


if __name__ == "__main__":
    main()
