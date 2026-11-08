#!/usr/bin/env python3
"""SIV-M10 합성 야간 회귀 로그 생성기 (결정적: 같은 seed → 바이트 단위로 같은 파일)

  python3 -I gen_logs.py                  # → nightly_regress.jsonl (레슨 폴더)
  python3 -I gen_logs.py /tmp/x.jsonl     # 다른 경로로

형식: JSON Lines. 런마다 "run" 헤더 1줄 + 테스트마다 "test" 1줄.
  {"type":"run","run":"R001","night":"2026-11-03","board":"EVB-07","chip":"A0-0117","temp_c":85,"fw":"1.5.0","seed":"0x..."}
  {"type":"test","run":"R001","test":"pcie.link.width","status":"FAIL","code":"E_LINK_DEGRADED",
   "reg":"LNKSTA","addr":"0x00000082","exp":"0x00000044","act":"0x00000024","msg":"..."}

심어 둔 상관 (solution.py 가 되찾아야 하는 정답):
  1. 보드     pcie.link.width  — EVB-07 에서만 x4 → x2 로 학습 (모든 온도·FW, 3일째 칩을 교체해도 계속)
  2. 온도     ddr.stress.march — 85°C 이상에서만 DQ13 비트가 0으로 읽힘 (모든 보드)
  3. FW       irq.dma.done     — FW 1.5.0 부터 DMA 완료 인터럽트 timeout (1.5.0, 1.5.1)
  4. 퍼짐     spi.flash.read   — 어디서나 ~12% timeout. FW 1.4.x 는 "E_TIMEOUT", 1.5.x 는 "ETIMEDOUT" 로 찍는다
  + EVB-04 는 USB PHY 미장착 → usb.hs.enum 항상 SKIP,  + 콘솔이 끊겨 잘린 줄 2개
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SEED = 0x5EED10
NIGHTS = ["2026-11-03", "2026-11-04", "2026-11-05", "2026-11-06", "2026-11-07"]
BOARDS = ["EVB-%02d" % i for i in range(1, 9)]
TEMPS = [-40, 0, 25, 60, 85, 105]
FWS = ["1.4.2", "1.5.0", "1.5.1"]
TESTS = ["boot.rom.id", "i2c.pmic.id", "spi.flash.read", "ddr.stress.march",
         "pcie.link.width", "irq.dma.done", "usb.hs.enum", "gpio.loopback"]
DDR_PATTERNS = [0xAAAAAAAA, 0xFFFFFFFF, 0x3C3C3C3C, 0xF0F0F0F0, 0x2D2D2D2D]  # 모두 bit13 = 1


def chip_of(board, night_idx):
    """보드마다 칩 하나. EVB-07 은 3일째(night_idx 2)에 칩을 새 것(A0-0142)으로 교체했다."""
    if board == "EVB-07" and night_idx >= 2:
        return "A0-0142"
    return "A0-01%02d" % (int(board[-2:]) + 10)


def J(d):
    return json.dumps(d, separators=(",", ":"))


def test_record(rng, run, test, board, temp, fw):
    """테스트 하나의 결과 레코드 (dict)."""
    rec = {"type": "test", "run": run, "test": test, "status": "PASS"}
    if test == "ddr.stress.march":
        rec["test"] = "ddr.stress.march[seed=0x%04X]" % rng.randrange(1 << 16)   # 파라미터가 매번 다르다
        if temp >= 85 and rng.random() < 0.40:
            exp = rng.choice(DDR_PATTERNS)
            addr = 0x80000000 + 4 * rng.randrange(1 << 24)
            rec.update(status="FAIL", code="E_MISCOMPARE", reg="DDR_RDATA", addr="0x%08X" % addr,
                       exp="0x%08X" % exp, act="0x%08X" % (exp & ~(1 << 13)),
                       msg="miscompare at 0x%08X iter %d after %d ms (DQ13)"
                       % (addr, rng.randrange(1, 400), rng.randrange(100, 9000)))
    elif test == "pcie.link.width":
        if board == "EVB-07" and rng.random() < 0.50:
            rec.update(status="FAIL", code="E_LINK_DEGRADED", reg="LNKSTA", addr="0x00000082",
                       exp="0x00000044", act="0x00000024",
                       msg="link up Gen4 x2, expected x4 (retrain %d)" % rng.randrange(1, 4))
    elif test == "irq.dma.done":
        if fw != "1.4.2" and rng.random() < 0.25:
            rec.update(status="FAIL", code="ETIMEDOUT", reg="DMA_INT_STATUS", addr="0x4002000C",
                       exp="0x00000001", act="0x00000000",
                       msg="DMA ch%d done IRQ not seen within 100 ms" % rng.randrange(4))
    elif test == "spi.flash.read":
        if rng.random() < 0.12:
            rec.update(status="FAIL", code="E_TIMEOUT" if fw == "1.4.2" else "ETIMEDOUT",
                       reg="SPI_STATUS", addr="0x40013008", exp="0x00000000", act="0x00000001",
                       msg="flash read %d B timed out after %d ms (BUSY)"
                       % (rng.choice([256, 4096, 65536]), rng.randrange(50, 60)))
    elif test == "usb.hs.enum":
        if board == "EVB-04":
            rec.update(status="SKIP", msg="USB PHY not populated on this board")
    return rec


def generate(path):
    rng = random.Random(SEED)
    lines = []
    n = 0
    for ni, night in enumerate(NIGHTS):
        for temp in TEMPS:                    # 챔버 온도 하나에 보드 8장이 같이 들어간다
            for board in BOARDS:
                n += 1
                run = "R%03d" % n
                fw = rng.choice(FWS)          # 매 런 FW 빌드를 무작위로 (main + release 브랜치)
                lines.append(J({"type": "run", "run": run, "night": night, "board": board,
                                "chip": chip_of(board, ni), "temp_c": temp, "fw": fw,
                                "seed": "0x%08X" % rng.getrandbits(32)}))
                for test in TESTS:
                    lines.append(J(test_record(rng, run, test, board, temp, fw)))
    # 보드 리셋으로 UART 콘솔이 끊긴 줄 2개 (줄 중간에서 잘림)
    for idx in (777, 1500):
        lines[idx] = lines[idx][: len(lines[idx]) // 2]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return len(lines)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "nightly_regress.jsonl")
    n = generate(out)
    print("wrote %s (%d lines, %d bytes)" % (out, n, os.path.getsize(out)))
