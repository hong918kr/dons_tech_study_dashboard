#!/usr/bin/env python3
"""SIV-M13 데이터 생성기 — DMA0 야간 회귀(nightly regression)의 시퀀서 실행 로그(JSONL)를 결정적으로 만든다.

  python3 -I gen_runlog.py        # runlog_v1.jsonl, runlog_v2.jsonl 를 다시 쓴다 (같은 바이트가 나와야 한다)

한 줄 = 트랜잭션 하나 (SIV-M12 같은 시퀀서가 남기는 로그를 흉내 냄):
  {"seed":3,"step":5,"test":"dma_rand","status":"PASS","ctrl":"0x1D","len":4096,"int_status":"0x3"}
  ctrl       = DMA0.CTRL 에 쓴 값   MODE[1:0] (0 M2M, 1 M2P, 2 P2M, 3 SG) · BURST[3:2] (1/4/8/16 beat) · IE[4]
  len        = DMA0.LEN (16비트, 0 = 즉시 완료)
  int_status = 트랜잭션 뒤 읽은 DMA0.INT_STATUS  DONE[0] HALF[1] ERR[2] TIMEOUT[3]

v1 = 첫 야간 회귀 (자극 생성기에 버그 3개가 숨어 있다 — 커버리지 모델이 찾아야 할 '구멍'):
  G1  interesting-length 목록에 0 이 없다 (LEN=0 이 스펙상 합법인데 '불법'이라 착각)
  G2  65535 는 randint(1, 65535) 로만 나온다 → 확률 1/65535, 사실상 안 나옴 (directed 1건은 DUT 버그로 FAIL)
  G3  버스 에러 주입 hook 이 16-beat 경로에 없다 → BURST=16 × ERR 조합이 절대 안 나옴
v2 = 생성기만 고친 다음 날 밤 (G1~G3 수정). DUT 버그는 그대로:
  D1  M2P 모드 + LEN=65535 → 마지막 beat 데이터 불일치 (16비트 카운터 wrap) → FAIL
  D2  LEN=1 에서 에러 주입 → INT_STATUS 에 DONE 과 ERR 가 동시에 섬 (스펙상 불가 = illegal) — 테스트는 PASS
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SEEDS = 32
STEPS = 20
DONE, HALF, ERR, TIMEOUT = 0x1, 0x2, 0x4, 0x8
M2M, M2P, P2M, SG = 0, 1, 2, 3

INTERESTING = {
    "v1": [1, 2, 63, 64, 4095, 4096, 4097],                    # G1: 0 없음, G2: 65535 없음
    "v2": [0, 1, 2, 63, 64, 4095, 4096, 4097, 65534, 65535],
}


def dut(rng, mode, burst, length, inject):
    """DMA0 동작 모델 → (int_status, status, note). inject: '' / 'bus_err' / 'stall'"""
    if mode == M2P and length == 65535:                      # D1
        return DONE | HALF, "FAIL", "dst mismatch at byte 65534 (len counter wrap)"
    half = HALF if length >= 128 else 0
    if length == 0 or not inject:
        return (DONE | half) if length else DONE, "PASS", ""
    pos = rng.randrange(length)                              # 에러/스톨이 일어난 바이트 위치
    h = HALF if (length >= 128 and pos >= length // 2) else 0
    if inject == "bus_err":
        if length == 1:                                      # D2: DONE 이 같이 선다
            return DONE | ERR, "PASS", ""
        return ERR | h, "PASS", ""
    return TIMEOUT | h, "PASS", ""                           # stall → watchdog timeout


def gen_len(rng, ver):
    if rng.random() < 0.3:
        return rng.choice(INTERESTING[ver])
    return rng.randint(1, 65535)


def generate(ver):
    out = []
    for seed in range(SEEDS):
        rng = random.Random(1000 + seed)
        step = 0
        if seed == 0:                                        # directed 스위트는 seed 0 에서 한 번
            for test, mode, burst, length in (("dma_len_max", M2P, 1, 65535),
                                              ("dma_page", M2M, 3, 4096)):
                st, status, note = dut(rng, mode, burst, length, "")
                out.append(rec(seed, step, test, status, mode, burst, length, st, "", note))
                step += 1
        for _ in range(STEPS):
            mode = rng.randrange(4)
            burst = rng.randrange(4)
            length = gen_len(rng, ver)
            r = rng.random()
            inject = ""
            if r < 0.10 and (ver == "v2" or burst != 3):     # G3: v1 은 16-beat 에 에러 주입 못 함
                inject = "bus_err"
            elif r >= 0.90 and mode in (M2P, P2M):           # 주변장치 모드만 stall 가능
                inject = "stall"
            st, status, note = dut(rng, mode, burst, length, inject)
            out.append(rec(seed, step, "dma_rand", status, mode, burst, length, st, inject, note))
            step += 1
    return out


def rec(seed, step, test, status, mode, burst, length, st, inject, note):
    d = {"seed": seed, "step": step, "test": test, "status": status,
         "ctrl": "0x%02X" % (mode | burst << 2 | 1 << 4), "len": length, "int_status": "0x%X" % st}
    if inject:
        d["inject"] = inject
    if note:
        d["note"] = note
    return d


def main():
    for ver in ("v1", "v2"):
        path = os.path.join(HERE, "runlog_%s.jsonl" % ver)
        recs = generate(ver)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            for d in recs:
                f.write(json.dumps(d, separators=(",", ":")) + "\n")
        print("wrote %s (%d records, %d bytes)" % (os.path.basename(path), len(recs), os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
