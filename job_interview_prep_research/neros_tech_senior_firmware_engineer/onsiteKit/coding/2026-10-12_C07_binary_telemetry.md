# 🧮 C07 바이너리 텔레메트리 디코드 — struct + 비트 플래그 + CRC-8 직접

> FC가 덤프한 고정 길이 레코드를 해석하고 이상한 것을 표시한다. 핵심 도구: `struct` (리틀 엔디안 · 부호 있는 16비트), 비트 마스크, **CRC-8을 손으로** 쓰고 check value로 먼저 검증.

## 1. 면접관 문장

> "The flight controller dumps 12-byte records: seq u16, three int16 accel values in milli-g, battery millivolts u16, a flags byte, and a CRC-8 (poly 0x07, init 0) over the first 11 bytes. Bit 0 is armed, bit 1 failsafe, bit 2 GPS fix. Decode a dump and flag anything suspicious."

## 2. 먼저 던질 질문

1. "Little-endian?" → `<`
2. "What counts as suspicious?" → 범위 밖 전압, 가속도 포화(full scale 근처), 정의 안 된 플래그 비트, CRC 실패, 남는 바이트
3. "On a CRC failure, skip the record or stop?" → 건너뛰고 기록
4. "Is there a known check value for the CRC?" → CRC-8/SMBUS: `"123456789"` → **0xF4**

## 3. 레이아웃

| 오프셋 | 크기 | 필드 | struct |
|---|---|---|---|
| 0 | 2 | seq | `H` |
| 2 | 2 | ax | `h` |
| 4 | 2 | ay | `h` |
| 6 | 2 | az | `h` |
| 8 | 2 | vbat mV | `H` |
| 10 | 1 | flags | `B` |
| 11 | 1 | CRC-8 | (따로) |

`struct.Struct("<HhhhHB").size == 11`

## 4. CRC-8 (MSB first, poly 0x07)

```python
def crc8(data, poly=0x07, init=0x00):
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc

assert crc8(b"123456789") == 0xF4
```

- `& 0xFF`를 빼먹으면 Python int는 계속 커진다 (C의 uint8_t와 다름) — **가장 흔한 Python 실수**

## 5. 엣지 케이스

- [ ] CRC 먼저 check value로 검증
- [ ] 음수 가속도 (`h` vs `H` 실수)
- [ ] 비트 하나 뒤집힌 레코드 → CRC 실패, 다음 레코드는 정상 처리
- [ ] 길이가 12의 배수가 아님 → 남는 바이트 보고
- [ ] 정의 안 된 플래그 비트

## 6. 후속 질문

| 질문 | 답 |
|---|---|
| CRC를 빠르게? | 256개 테이블 (스크린 때 C로 했던 것). `T[1] == poly` |
| 레코드 경계를 잃으면? | 고정 길이라 한 번 어긋나면 전부 깨짐 → sync 바이트 추가 ([C01](2026-10-12_C01_packet_stream.md)) |
| C 구조체로 바로 읽으면? | 패딩 · 엔디안 · 정렬 문제 → `__attribute__((packed))` 또는 바이트 단위 조립 |
| 몇 MB 덤프면? | `memoryview` + `struct.iter_unpack` |

## 7. 정답 코드

- [c07_binary_telemetry.py](../code/c07_binary_telemetry.py) · 연습: `python3 code/run_all.py --practice c07`

## 8. 내 풀이 기록

| 날짜 | 걸린 시간 | 놓친 엣지 | 다음엔 |
|---|---|---|---|
| | | | |
