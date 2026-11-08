#!/usr/bin/env python3
"""HWLAB-M06 합성 캡처 생성기 (결정적, stdlib only)

EVT1 "상태 표시 보드"의 두 버스를 연속 시간 모델로 만들고 로직 애널라이저처럼 샘플링해 sigrok .sr 로 저장한다.

  evt1_led_ws2812_16M.sr   LED_DIN (WS2812B 8개 체인), 16 MS/s, 3 프레임.
                           프레임 2 에 **심어 둔 결함**: LED 5 뒤 20 us low (SPI DMA underrun).
  evt1_i2c_tmp117_2M.sr    I2C2_SCL / I2C2_SDA (100 kHz), 2 MS/s, TMP117(U7, 0x48) 트랜잭션 5개.
                           **심어 둔 결함**: CONFIG 쓰기가 0x49 로 나가 NACK.

  python3 -I gen_capture.py      # 위 2개 파일을 다시 만든다 (같은 바이트, sha256 출력)

모델 가정 (합성, 실측 아님):
  WS2812 은 MCU 의 SPI MOSI 2.4 MHz 로 만든다 (LED 비트 1개 = SPI 3비트, 0 -> 100, 1 -> 110):
  T0H = 416.7 ns, T1H = 833.3 ns, 비트 주기 1.25 us. 에지마다 가우시안 지터 sigma 4 ns.
  프레임 사이 low 80 us (> reset 50 us). 색 순서 GRB, MSB first.
  I2C: SCL 10 us 주기 (low 5 / high 5), SDA 는 SCL fall 후 1.5 us 에 바뀜, START hold 4 us, 트랜잭션 사이 40 us.
"""
import bisect
import hashlib
import os
import random
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
NS = 1000            # ps
US = 1000 * NS
ZIP_TIME = (2026, 11, 8, 0, 0, 0)
SEED = 612

# ---------------------------------------------------------------- WS2812
SPI_HZ = 2_400_000
U = 10 ** 12 / SPI_HZ          # SPI 비트 1개 = 416666.67 ps
JITTER = 4 * NS
T_RESET_FW = 80 * US           # 펌웨어가 프레임 끝에 두는 low
LED_NAMES = ["LED_DIN"]
FRAME1 = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255),
          (16, 32, 64), (0x12, 0x34, 0x56), (0xA5, 0x5A, 0x0F), (0, 0, 0)]
FRAME3 = [(0, 40, 0)] * 6 + [(40, 20, 0), (40, 0, 0)]
FRAMES = [FRAME1, FRAME1, FRAME3]
GAP = (1, 5, 20 * US)          # (프레임 index, 이 LED 개수 뒤, 추가 low) = 프레임 2 의 LED5 뒤 20 us


def grb_bits(rgb):
    r, g, b = rgb
    return [(byte >> (7 - i)) & 1 for byte in (g, r, b) for i in range(8)]


def ws2812_waveform(frames=None, gap=GAP, seed=SEED, jitter=JITTER, t_lead=T_RESET_FW, high_scale=1.0):
    """연속 시간 DIN 파형. 반환 {"names", "init", "toggles": {name: [t_ps,...]}, "t_end"}.
    high_scale: T0H/T1H 를 곱으로 늘이고 줄이는 고장 주입용 (주기는 그대로)."""
    if frames is None:
        frames = FRAMES
    rng = random.Random(seed)
    tog = []
    t = float(t_lead)
    for fi, leds in enumerate(frames):
        for li, rgb in enumerate(leds):
            for b in grb_bits(rgb):
                h = (2 if b else 1) * U * high_scale
                tog.append(t + rng.gauss(0.0, jitter))
                tog.append(t + h + rng.gauss(0.0, jitter))
                t += 3 * U
            if gap is not None and gap[0] == fi and gap[1] == li + 1:
                t += gap[2]
        t += T_RESET_FW                       # 프레임 끝: 마지막 비트 low + 80 us
    tog.sort()
    return {"names": list(LED_NAMES), "init": {"LED_DIN": 0}, "toggles": {"LED_DIN": tog}, "t_end": t}


# ---------------------------------------------------------------- I2C
I2C_NAMES = ["I2C2_SCL", "I2C2_SDA"]
TMP117 = 0x48
I2C_TXNS = [
    # (설명, [(addr, rw, bytes 또는 None, addr_ack)])  rw: 'W'/'R'.  읽기 bytes 는 슬레이브가 보낸 값
    ("read Device_ID", [(0x48, "W", [0x0F], True), (0x48, "R", [0x01, 0x17], True)]),
    ("write CONFIG (wrong address!)", [(0x49, "W", [0x01, 0x0C, 0x20], False)]),
    ("read Temp_Result", [(0x48, "W", [0x00], True), (0x48, "R", [0x0B, 0xC0], True)]),
    ("read CONFIG back", [(0x48, "W", [0x01], True), (0x48, "R", [0x02, 0x20], True)]),
    ("read Temp_Result", [(0x48, "W", [0x00], True), (0x48, "R", [0x0B, 0xC1], True)]),
]


def i2c_waveform(txns=None, t_lead=20 * US, gap=40 * US):
    """연속 시간 SCL/SDA. 각 트랜잭션 = START (+ 세그먼트마다 Sr) ... STOP.
    주소가 NACK 이면 그 세그먼트의 데이터는 보내지 않고 바로 STOP."""
    if txns is None:
        txns = I2C_TXNS
    ev = {"I2C2_SCL": [], "I2C2_SDA": []}
    scl, sda = "I2C2_SCL", "I2C2_SDA"
    t = float(t_lead)

    def bit(t0, b):
        ev[sda].append((t0 + 1.5 * US, b))
        ev[scl].append((t0 + 5 * US, 1))
        ev[scl].append((t0 + 10 * US, 0))
        return t0 + 10 * US

    def byte(t0, v):
        for i in range(8):
            t0 = bit(t0, (v >> (7 - i)) & 1)
        return t0

    for _desc, segs in txns:
        ev[sda].append((t, 0))                      # START: SCL high 에서 SDA fall
        ev[scl].append((t + 4 * US, 0))
        t += 4 * US
        for si, (addr, rw, data, ack) in enumerate(segs):
            if si > 0:                              # repeated START
                ev[sda].append((t + 1.5 * US, 1))
                ev[scl].append((t + 5 * US, 1))
                ev[sda].append((t + 9 * US, 0))
                ev[scl].append((t + 13 * US, 0))
                t += 13 * US
            t = byte(t, (addr << 1) | (1 if rw == "R" else 0))
            t = bit(t, 0 if ack else 1)
            if not ack:
                break
            for j, v in enumerate(data):
                t = byte(t, v)
                if rw == "W":
                    t = bit(t, 0)                   # 슬레이브 ACK
                else:
                    t = bit(t, 0 if j + 1 < len(data) else 1)   # 마스터: 마지막 바이트 NACK
        ev[sda].append((t + 1.5 * US, 0))           # STOP: SDA low -> SCL high -> SDA rise
        ev[scl].append((t + 5 * US, 1))
        ev[sda].append((t + 9 * US, 1))
        t += 9 * US + gap
    init = {scl: 1, sda: 1}
    toggles = {}
    for n in I2C_NAMES:
        lvl, tl = init[n], []
        for tt, v in sorted(ev[n]):
            if v != lvl:
                tl.append(tt)
                lvl = v
        toggles[n] = tl
    return {"names": list(I2C_NAMES), "init": init, "toggles": toggles, "t_end": t}


# ---------------------------------------------------------------- 샘플러 + .sr writer (HWLAB-B07 축약)
def sample_bytes(wave, fs, phase_ps=0.0):
    """무차별 샘플러 = '진짜 LA'. 샘플 k = 시각 phase + k*Ts 의 레벨. names[i] -> 비트 i (little-endian)."""
    ts = 1e12 / fs
    n = int((wave["t_end"] - phase_ps) // ts) + 1
    names = wave["names"]
    unitsize = (len(names) + 7) // 8
    out = bytearray()
    for k in range(n):
        tk = phase_ps + k * ts
        word = 0
        for i, nm in enumerate(names):
            word |= (wave["init"][nm] ^ (bisect.bisect_right(wave["toggles"][nm], tk) & 1)) << i
        out += word.to_bytes(unitsize, "little")
    return bytes(out), unitsize


def samplerate_string(fs):
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


def write_sr(dst, names, data, unitsize, fs, chunk_samples=4096):
    """sigrok session v2 (.sr): version + metadata(INI) + logic-1-1..N 청크 (HWLAB-B07 참고)"""
    meta = ["[global]", "sigrok version=0.5.2", "", "[device 1]", "capturefile=logic-1",
            "total probes=%d" % len(names), "samplerate=%s" % samplerate_string(fs), "total analog=0"]
    meta += ["probe%d=%s" % (i + 1, nm) for i, nm in enumerate(names)]
    meta += ["unitsize=%d" % unitsize, ""]
    step = chunk_samples * unitsize
    with zipfile.ZipFile(dst, "w") as z:
        _zput(z, "version", b"2")
        _zput(z, "metadata", "\n".join(meta).encode("ascii"))
        for i, off in enumerate(range(0, len(data), step)):
            _zput(z, "logic-1-%d" % (i + 1), data[off:off + step])


FILES = (("evt1_led_ws2812_16M.sr", ws2812_waveform, 16_000_000, 0.0),
         ("evt1_i2c_tmp117_2M.sr", i2c_waveform, 2_000_000, 137 * NS))


def build_files(outdir=HERE):
    out = []
    for name, wf, fs, ph in FILES:
        w = wf()
        data, us = sample_bytes(w, fs, ph)
        p = os.path.join(outdir, name)
        write_sr(p, w["names"], data, us, fs)
        out.append(p)
    return out


def main():
    for p in build_files():
        with open(p, "rb") as f:
            blob = f.read()
        print("%-26s %6d B  sha256 %s" % (os.path.basename(p), len(blob), hashlib.sha256(blob).hexdigest()[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
