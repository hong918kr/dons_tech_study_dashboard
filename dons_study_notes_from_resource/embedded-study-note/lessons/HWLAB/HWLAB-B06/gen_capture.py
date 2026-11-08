#!/usr/bin/env python3
"""HWLAB-B06 합성 캡처 생성기 — 10 MHz 3.3 V 클록을 1 GSa/s · 8비트 ADC 로 10 ms(10M 샘플) 찍은 것

  - 파일로 쓰지 않는다 (10 MB). build_capture() 가 메모리에 bytes 로 만든다 (~0.2 s).
  - 결정적: random.Random(SEED) 의 random() 만 쓴다 → 어느 Python 3 에서나 같은 바이트 (sha256 로 확인).
  - 심어 둔 이상 이벤트 4개 (PLANTED): 양(+) runt, 좁은 글리치, 음(-) runt, 2 us 클록 정지(stall).

  python3 -I gen_capture.py     # 요약 + sha256 출력 (solution.py 의 CAPTURE_SHA256 과 같아야 한다)

ADC 모델 (스코프 8비트 ADC 흉내): code = round((v - V_OFF) / LSB), 0..255 로 포화.
  V_OFF = -0.5 V, LSB = 20 mV → 0 V = 25, 3.3 V = 190, 풀스케일 -0.5 .. 4.6 V
"""
import hashlib
import math
import random
import sys

SEED = 20261108
FS = 1e9                 # 1 GSa/s → 샘플 1개 = 1 ns
N_PERIOD = 100           # 10 MHz 클록 = 100 샘플/주기
N_PERIODS = 100000       # 100k 주기 = 10M 샘플 = 10 ms
N = N_PERIOD * N_PERIODS
V_OFF, LSB = -0.5, 0.02
V_LOW, V_HIGH = 0.0, 3.3
TAU = 0.91               # tanh 에지 시정수 [ns] → 10-90% 상승시간 = 2*atanh(0.8)*TAU ≈ 2.0 ns
NOISE_RMS = 0.03         # 30 mV rms (가우시안)
RISE_AT, FALL_AT = 25, 75   # 주기 안 에지 위치: 0..24 low, 25..74 high, 75..99 low
N_VARIANTS = 256         # 잡음·지터가 다른 정상 주기 템플릿 개수

# 심어 둔 이벤트: (주기 번호, 종류). stall 은 그 주기부터 STALL_PERIODS 개를 low 로 채운다.
STALL_PERIODS = 20
PLANTED = [
    (13579, "runt_pos"),   # low 구간에 1.3 V 까지 올라왔다 돌아가는 펄스 (0.8 V 는 넘고 2.0 V 는 못 넘음)
    (41234, "glitch"),     # low 구간에 폭 ~4 ns 의 풀스윙 펄스 (두 임계 다 넘음 → runt 가 아님)
    (58000, "stall"),      # 20 주기(2 us) 동안 클록이 low 로 멈춤
    (77777, "runt_neg"),   # high 구간에 1.3 V 까지 내려갔다 돌아가는 펄스
]


def v2code(v):
    """전압 → ADC 코드 (float, 반올림·포화 안 함). 트리거 임계값 설정용."""
    return (v - V_OFF) / LSB


def code2v(c):
    return V_OFF + c * LSB


def quant(v):
    c = int(math.floor((v - V_OFF) / LSB + 0.5))
    return 0 if c < 0 else 255 if c > 255 else c


def step(t):
    """0→1 tanh 에지, t = 에지 중심으로부터의 시간 [ns]"""
    return 0.5 * (1.0 + math.tanh(t / TAU))


class _Gauss:
    """Box-Muller 를 random() 으로 직접 — Random.gauss() 내부 상태에 기대지 않는다"""
    def __init__(self, rng):
        self.rng = rng

    def __call__(self):
        u1 = 1.0 - self.rng.random()
        u2 = self.rng.random()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)


def _period(rng, g, kind):
    jr = (rng.random() - 0.5) * 0.6       # 에지 지터 ±0.3 ns (sub-sample)
    jf = (rng.random() - 0.5) * 0.6
    out = []
    for k in range(N_PERIOD):
        if kind == "stall":
            v = V_LOW
        else:
            v = V_LOW + (V_HIGH - V_LOW) * (step(k - RISE_AT - jr) - step(k - FALL_AT - jf))
        if kind == "runt_pos":              # low 구간(중심 12 ns) 가우시안 범프, 피크 1.3 V
            v += 1.3 * math.exp(-0.5 * ((k - 12) / 2.5) ** 2)
        elif kind == "runt_neg":            # high 구간(중심 50 ns) 가우시안 딥, 바닥 1.3 V
            v -= 2.0 * math.exp(-0.5 * ((k - 50) / 2.5) ** 2)
        elif kind == "glitch":              # low 구간 10 ns 에서 올라가 14 ns 에서 내려오는 풀스윙 펄스
            v += (V_HIGH - V_LOW) * (step(k - 10) - step(k - 14))
        out.append(quant(v + NOISE_RMS * g()))
    return bytes(out)


def build_capture(seed=SEED):
    """→ (buf: bytes 길이 N, events: [{kind, period, index}])  index = 이벤트 중심 샘플 번호"""
    rng = random.Random(seed)
    g = _Gauss(rng)
    normal = [_period(rng, g, "normal") for _ in range(N_VARIANTS)]
    lows = [_period(rng, g, "stall") for _ in range(16)]
    special = {}
    events = []
    for p, kind in PLANTED:
        if kind == "stall":
            for q in range(p, p + STALL_PERIODS):
                special[q] = lows[q % 16]
            events.append({"kind": kind, "period": p, "index": p * N_PERIOD,
                           "end": (p + STALL_PERIODS) * N_PERIOD + RISE_AT})
        else:
            special[p] = _period(rng, g, kind)
            center = {"runt_pos": 12, "glitch": 12, "runt_neg": 50}[kind]
            events.append({"kind": kind, "period": p, "index": p * N_PERIOD + center})
    parts = []
    nv = len(normal)
    for p in range(N_PERIODS):
        r = rng.random()                   # 템플릿 선택도 같은 rng 로 (특수 주기도 소비해 순서 고정)
        parts.append(special.get(p) or normal[int(r * nv)])
    return b"".join(parts), events


def main():
    import time
    t0 = time.perf_counter()
    buf, events = build_capture()
    dt = time.perf_counter() - t0
    print("samples   : %d (%.1f ms @ %.0f GSa/s)" % (len(buf), len(buf) / FS * 1e3, FS / 1e9))
    print("build time: %.2f s" % dt)
    print("min/max   : %d / %d codes (%.2f / %.2f V)" % (min(buf), max(buf), code2v(min(buf)), code2v(max(buf))))
    for e in events:
        print("planted   : %-8s period=%-6d index=%d" % (e["kind"], e["period"], e["index"]))
    print("sha256    : %s" % hashlib.sha256(buf).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
