#!/usr/bin/env python3
"""L10 설명에 쓰는 숫자를 전부 계산해서 out/*.txt 로 저장한다 (페이지는 이 출력을 그대로 보여 준다).
  python3 lecture/crc/explain.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent / "out"
OUT.mkdir(exist_ok=True)
POLY8 = 0x07                       # x^8 + x^2 + x + 1 → 9비트로 쓰면 1_0000_0111 = 0x107


def b(v, n=8):
    return format(v, f"0{n}b")


def crc8_bits(data, trace=None):
    crc = 0
    for byte in data:
        crc ^= byte
        if trace is not None:
            trace.append(f"  입력 0x{byte:02X} 를 XOR → crc = 0x{crc:02X} ({b(crc)})")
        for step in range(1, 9):
            top = crc & 0x80
            crc = ((crc << 1) ^ POLY8) & 0xFF if top else (crc << 1) & 0xFF
            if trace is not None:
                trace.append(f"    {step}단계: 맨 위 비트 {1 if top else 0} → "
                             f"{'왼쪽 1칸 + ^0x07' if top else '왼쪽 1칸만     '} → 0x{crc:02X} ({b(crc)})")
    return crc


T8 = []
for i in range(256):
    c = i
    for _ in range(8):
        c = ((c << 1) ^ POLY8) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    T8.append(c)


def crc8_table(data):
    crc = 0
    for byte in data:
        crc = T8[crc ^ byte]
    return crc


def write(name, lines):
    (OUT / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


# 1) 10진수 나머지 비유
write("decimal.txt", [
    f"보내는 쪽: 데이터 1234, 약속한 수 7 로 나눈 나머지 = 1234 mod 7 = {1234 % 7}",
    f"            → '1234' 와 나머지 '{1234 % 7}' 을 같이 보낸다",
    f"받는 쪽  : 1234 를 받았으면 다시 1234 mod 7 = {1234 % 7} → 같다 → 정상",
    f"            1294 로 깨져서 왔으면 1294 mod 7 = {1294 % 7} → 다르다 → 오류 발견",
])

# 2) 긴 나눗셈: 데이터 0x31 뒤에 0 을 8개 붙인 16비트를 1_0000_0111 로 XOR-나눗셈
data = 0x31 << 8
rows = [f"나눌 수 (데이터 0x31 + 0 여덟 개): {b(data, 16)}",
        f"나누는 수 (poly 0x07 + 맨 위 1)   : 1{b(POLY8)}  (9비트)",
        ""]
cur = data
for pos in range(15, 7, -1):
    if cur >> pos & 1:
        sub = 0x107 << (pos - 8)
        rows.append(f"  비트 {pos:2d} 가 1 → XOR {b(sub, 16)}")
        cur ^= sub
        rows.append(f"                  = {b(cur, 16)}")
    else:
        rows.append(f"  비트 {pos:2d} 가 0 → 건너뜀")
rows += ["", f"나머지 (아래 8비트) = {b(cur, 8)} = 0x{cur:02X}"]
assert cur == 0x97
write("longdiv.txt", rows)

# 3) 비트 방식으로 같은 계산
tr = []
r = crc8_bits(b"1", tr)
assert r == 0x97 == T8[0x31]
write("bitwise_one.txt", ["입력 '1' (0x31), crc 시작값 0x00"] + tr + [f"결과 0x{r:02X}  ← 긴 나눗셈의 나머지와 같다"])

# 4) 두 바이트 "12": 비트 방식 vs 테이블 방식
tr = []
r2 = crc8_bits(b"12", tr)
first = T8[0 ^ 0x31]
idx2 = first ^ 0x32
write("two_bytes.txt", [
    "[비트 방식] 입력 \"12\" = 0x31, 0x32"] + tr + [f"결과 0x{r2:02X}", "",
    "[테이블 방식] 바이트마다 표를 한 번씩",
    f"  1바이트: crc = T[0x00 ^ 0x31] = T[0x31] = 0x{first:02X}",
    f"  2바이트: crc = T[0x{first:02X} ^ 0x32] = T[0x{idx2:02X}] = 0x{T8[idx2]:02X}",
    f"결과 0x{crc8_table(b'12'):02X}  ← 비트 방식과 같다",
])
assert crc8_table(b"12") == r2

# 5) 테이블 앞부분과 XOR 성질 (선형성)
lin = [f"T[0x{1 << k:02X}] = 0x{T8[1 << k]:02X}" for k in range(8)]
x = 0x31
parts = [1 << k for k in range(8) if x >> k & 1]
acc = 0
for p in parts:
    acc ^= T8[p]
lin += ["",
        "0x31 = " + " ^ ".join(f"0x{p:02X}" for p in parts) + "  (켜진 비트들)",
        "T[0x31] = " + " ^ ".join(f"T[0x{p:02X}]" for p in parts) + " = "
        + " ^ ".join(f"0x{T8[p]:02X}" for p in parts) + f" = 0x{acc:02X}",
        f"직접 계산한 T[0x31] = 0x{T8[0x31]:02X}  ← 같다"]
assert acc == T8[0x31]
write("linearity.txt", lin)

# 6) CRC-16/CCITT-FALSE 한 바이트 '1' 따라가기
T16 = []
for i in range(256):
    c = i << 8
    for _ in range(8):
        c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    T16.append(c)
crc = 0xFFFF
top = crc >> 8
idx = top ^ 0x31
shifted = (crc << 8) & 0xFFFF
new = shifted ^ T16[idx]
write("crc16_walk.txt", [
    "CRC-16/CCITT-FALSE, 시작값 0xFFFF, 입력 '1' (0x31)",
    f"  위 바이트   = crc >> 8          = 0x{top:02X}",
    f"  인덱스      = 0x{top:02X} ^ 0x31        = 0x{idx:02X}",
    f"  표 값       = T[0x{idx:02X}]             = 0x{T16[idx]:04X}",
    f"  아래 바이트를 위로 = (crc << 8) & 0xFFFF = 0x{shifted:04X}",
    f"  새 crc      = 0x{shifted:04X} ^ 0x{T16[idx]:04X}   = 0x{new:04X}",
])
c = 0xFFFF
c ^= 0x31 << 8
for _ in range(8):
    c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
assert c == new
CRC16_ONE = new

# 7) L09 프레임에 적용
payload = [0x10, 0x20, 0x30]
body = [len(payload)] + payload
cc = crc8_table(bytes(body))
frame = [0xAA] + body + [cc]
bad = list(body)
bad[2] ^= 0x04
write("frame.txt", [
    "보내는 쪽: payload 10 20 30",
    "  CRC 를 계산하는 범위 = LEN + PAYLOAD = " + " ".join(f"{v:02X}" for v in body),
    f"  crc8 = 0x{cc:02X}",
    "  보내는 프레임       = " + " ".join(f"{v:02X}" for v in frame) + "   (SYNC LEN PAYLOAD CRC)",
    "",
    "받는 쪽 (정상): LEN+PAYLOAD 로 다시 계산 → 0x%02X == 받은 0x%02X → 통과" % (crc8_table(bytes(body)), cc),
    f"받는 쪽 (payload 두 번째 바이트의 비트 하나가 뒤집힘: {body[2]:02X} → {bad[2]:02X})",
    "  다시 계산 → 0x%02X != 받은 0x%02X → 버림 (CRC 오류)" % (crc8_table(bytes(bad)), cc),
    "다른 방법: 받은 'LEN+PAYLOAD+CRC' 전체를 돌리면 정상일 때 0 → 0x%02X" % crc8_table(bytes(body + [cc])),
])
assert crc8_table(bytes(body + [cc])) == 0

# C 테스트가 같은 숫자를 확인하도록 상수를 남긴다
(OUT / "values.h").write_text(
    "/* explain.py 가 만든 값 — test_crc.c 가 C 구현과 교차 확인 */\n"
    f"#define EX_CRC8_12      0x{r2:02X}\n#define EX_CRC16_ONE    0x{CRC16_ONE:04X}\n"
    f"#define EX_FRAME_CRC    0x{cc:02X}\n", encoding="utf-8")
print("ok", hex(r2), hex(CRC16_ONE), hex(cc))
