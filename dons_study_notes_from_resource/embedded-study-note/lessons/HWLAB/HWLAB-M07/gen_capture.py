#!/usr/bin/env python3
"""HWLAB-M07 Part C 캡처 생성기 (결정적: 같은 seed → 바이트 단위로 같은 .bin)

가상의 보드 NETBRD-C rev A. 클럭 생성기 U12 의 25 MHz LVCMOS 출력 CLK25_PHY 가 이더넷 PHY U20 의 XI 핀으로 간다.
TP44 (U20 XI 바로 옆)에서 액티브 프로브로 64 µs 를 5 GS/s 로 찍은 스코프 파형 두 장 (8-bit BYTE 포맷 + 프리앰블).
  SN0107  정상 보드 (golden)                                  : RJ 3 ps + PJ 4 ps @ 1.2 MHz (벅 컨버터) + 12 ppm
  SN0108  U12 의 SS_EN 스트랩에 풀업 R231 이 실장됨 (DNP 여야 함) : 위와 같음 + SSC −0.5 % down-spread 삼각파 31.5 kHz
          = 심어 둔 결함. 이더넷 PHY 기준 클럭에는 SSC 가 들어가면 안 된다.

파형 = 에지마다 tanh 모양 전이 (10–90 % = 2.0 ns) + 백색 전압 잡음 4 mV rms
     + 8-bit 양자화 (450 mV/div × 8 div, 화면 중앙 1.65 V → −0.15 V .. 3.45 V, LSB = 14.06 mV).

파일 형식 (스코프 BYTE 포맷 흉내): 첫 줄 = ASCII JSON 프리앰블 + '\\n', 그 뒤 n 바이트 int8 코드를 zlib 으로 압축
(320 kB → 약 75 kB. zlib.compress 는 타임스탬프를 넣지 않으므로 결정적이다).
  volt = yorg + code · yinc,   time = xorg + i · xinc

  python3 -I gen_capture.py          # SN0107_tp44_clk25.bin, SN0108_tp44_clk25.bin 을 다시 쓴다
"""
import json
import math
import os
import random
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

FS = 5e9                 # 5 GS/s (10–90 % 에지 위에 10 샘플)
N = 320000               # 64 µs ≈ 2.02 SSC 주기
F_NOM = 25e6             # 25 MHz
T10_90 = 2.0e-9          # 10–90 % 전이 시간
VLO, VHI = 0.0, 3.3      # LVCMOS33
NOISE_SD = 4e-3          # 전압 잡음 4 mV rms (프로브 + 스코프 + 보드)
VDIV, YORG, BITS = 0.45, 1.65, 8
T_FIRST = 7.3e-9         # 첫 상승 에지 시각

BOARDS = [
    # (serial, ppm, rj_s, (pj_amp_s, pj_hz), ssc(spread, fm_hz) or None, seed, 설명)
    ("SN0107", 12.0, 3e-12, (4e-12, 1.2e6), None, 107, "golden: SS_EN strap R231 DNP"),
    ("SN0108", 12.0, 3e-12, (4e-12, 1.2e6), (0.005, 31.5e3), 108, "R231 populated -> SSC on (planted)"),
]


def ssc_dev(t, spread, fm):
    """down-spread 삼각파 SSC 의 분수 주파수 편차 y(t) ∈ [−spread, 0]. t=0 에서 0(최고 주파수)."""
    u = (t * fm) % 1.0
    return -spread * (2.0 * u if u < 0.5 else 2.0 - 2.0 * u)


def rising_edges(t_end, ppm, rj, pj, ssc, rng):
    """참 상승 에지 시각. 반송파 시각 τ_n 은 SSC 로 주기가 변하며 누적되고, 지터는 그 위에 더해진다."""
    T = 1.0 / (F_NOM * (1.0 + ppm * 1e-6))
    pa, pf = pj
    out = []
    tau = T_FIRST
    while tau < t_end:
        out.append(tau + pa * math.sin(2 * math.pi * pf * tau) + rng.gauss(0.0, rj))
        y = ssc_dev(tau, *ssc) if ssc else 0.0
        tau += T / (1.0 + y)
    return out


def render(rises, rng):
    """상승 에지 목록 → 샘플 (볼트). 하강 에지는 이웃 상승 에지의 가운데(duty 50 %)."""
    tc = T10_90 / math.log(9.0)                  # tanh 전이: 10–90 % = tc · ln 9
    edges = []
    for k, r in enumerate(rises):
        edges.append((r, +1))
        nxt = rises[k + 1] if k + 1 < len(rises) else r + 1.0 / F_NOM
        edges.append((0.5 * (r + nxt), -1))
    mid, half = 0.5 * (VLO + VHI), 0.5 * (VHI - VLO)
    v = []
    j = 0
    for i in range(N):
        t = i / FS
        while j + 1 < len(edges) and t > 0.5 * (edges[j][0] + edges[j + 1][0]):
            j += 1
        te, s = edges[j]
        v.append(mid + s * half * math.tanh((t - te) / tc) + rng.gauss(0.0, NOISE_SD))
    return v


def quantize(v):
    yinc = VDIV * 8 / (1 << BITS)
    lo, hi = -(1 << (BITS - 1)), (1 << (BITS - 1)) - 1
    return [max(lo, min(hi, int(round((x - YORG) / yinc)))) for x in v], yinc


def write_bin(path, codes, yinc, meta):
    pre = dict(format="BYTE", encoding="zlib", points=len(codes), xinc=1.0 / FS, xorg=0.0, yinc=yinc, yorg=YORG)
    pre.update(meta)
    with open(path, "wb") as f:
        f.write(json.dumps(pre, sort_keys=True).encode("ascii") + b"\n")
        f.write(zlib.compress(bytes(c & 0xFF for c in codes), 9))


def main():
    for sn, ppm, rj, pj, ssc, seed, note in BOARDS:
        rng = random.Random(seed)
        rises = rising_edges(N / FS + 50e-9, ppm, rj, pj, ssc, rng)
        codes, yinc = quantize(render(rises, rng))
        path = os.path.join(HERE, "%s_tp44_clk25.bin" % sn)
        write_bin(path, codes, yinc, dict(board="NETBRD-C rev A", serial=sn, net="CLK25_PHY", probe="TP44",
                                          vdiv=VDIV, note="synthetic capture (gen_capture.py)"))
        print("wrote %s  (%d samples, %d rising edges, %s)" % (os.path.basename(path), len(codes), len(rises), note))


if __name__ == "__main__":
    main()
