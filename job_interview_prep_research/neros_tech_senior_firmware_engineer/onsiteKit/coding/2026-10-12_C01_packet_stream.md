# 📦 C01 바이트 스트림 → 패킷 (sync · 길이 · checksum · resync)

> 출제 가능성 가장 높음. 스크린 보너스 `extract_packet`의 Python판인데, 이번엔 **"buffer"라는 말 없이** 시리얼 스트림만 준다. 핵심: 잘린 패킷 대기 · 깨진 패킷 뒤 **다음 바이트부터** 재동기 · 쓰레기만 와도 버퍼가 커지지 않음.

## 1. 면접관 문장 (이것만 보고 시작)

> "Bytes come in from a serial port in random-sized chunks. Each message is: 0xAA, a length byte, that many payload bytes, then a checksum — the low 8 bits of the sum of the length and payload bytes. Write something I can call with each chunk that gives me back the complete, valid messages."

## 2. 먼저 던질 질문

1. "Is there a maximum payload length?" → 없으면 길이 바이트가 쓰레기일 때 255바이트까지 기다리게 된다
2. "Can 0xAA appear inside the payload?" → 보통 yes (escape 없음) → sync 하나만 믿으면 안 됨
3. "On a bad checksum, do you want counters, or just drop?" → 테스트 직군: **카운터를 둔다**
4. "Is memory bounded? What if we only ever get garbage?"

## 3. 손으로 예시

| 입력 청크 | 버퍼 상태 | 출력 |
|---|---|---|
| `AA 02` | `AA 02` (길이는 알지만 payload 없음) | [] |
| `10 20 32` | `AA 02 10 20 32` → 2+0x10+0x20=0x32 ✓ | [`10 20`] |
| `00 AA 05 AA 01 42 43` | 앞 `00` 버림 → `AA 05`는 8바이트 필요, 7바이트뿐 → 대기 | [] |
| `00 00` | 다 모임 → checksum 틀림 → **AA 하나만** 버리고 다시 찾기 → `AA 01 42 43` ✓ | [`42`] |

## 4. 엣지 케이스 체크리스트

- [ ] 한 바이트씩 들어와도 같은 결과
- [ ] 한 청크에 패킷 여러 개
- [ ] 앞에 쓰레기
- [ ] 길이 0 payload
- [ ] payload 안의 0xAA
- [ ] checksum 틀린 패킷 바로 뒤 정상 패킷
- [ ] 가짜 sync + 불가능한 길이 (max 초과) → 즉시 버림
- [ ] 가짜 sync + 그럴듯한 길이 → 기다렸다가 checksum 실패 후 재동기
- [ ] 쓰레기만 계속 → 버퍼 크기 상한 유지 (**이번 overflow 포인트**)

## 5. 접근

- `bytearray` 하나에 쌓고, 루프: sync 찾기 → 앞 쓰레기 버림 → 길이 확인 → 다 왔나 → checksum → 맞으면 꺼내고 전체 삭제, 틀리면 **1바이트만** 삭제
- 버퍼 상한의 근거: sync가 없으면 전부 버림, sync가 있으면 최대 한 프레임(3 + max_payload)까지만 기다림 → **항상 < max_frame**. 이 문장을 면접에서 말한다
- 복잡도: 바이트당 상수 (최악은 재동기 때 재검사)

## 6. 테스트 (직접 쓸 것)

```python
def frame(payload):
    body = bytes([len(payload)]) + bytes(payload)
    return bytes([0xAA]) + body + bytes([sum(body) & 0xFF])

p = FrameParser(max_payload=8)
assert p.feed(b"\xAA\x05" + frame(b"q")) == []      # fake sync waits
assert p.feed(b"\x00\x00") == [b"q"]                # then resync finds the real one
```

- 랜덤 분할 테스트: 같은 스트림을 아무 데서나 잘라 넣어도 결과가 같아야 한다 → **이게 가장 강한 테스트**

## 7. 후속 질문

| 질문 | 답 |
|---|---|
| C로 하면? | 고정 크기 ring + peek(offset)로 길이 확인, 다 모였을 때만 pop |
| 바이트마다 상태 머신으로? | `HUNT → LEN → PAYLOAD → CHK` 상태 + 카운터. 재동기 때 버퍼링한 바이트 재검사가 필요하다는 단점 |
| checksum 대신 CRC면? | 함수만 바뀜 ([C07](2026-10-12_C07_binary_telemetry.md) CRC-8) |
| 길이 필드가 깨지는 경우를 줄이려면? | 헤더 CRC 별도, 또는 COBS 같은 바이트 스터핑 |
| 처리량 테스트는? | 1 MB 랜덤 프레임 + 노이즈 주입, 유실/오검출 수 측정 |

## 8. 정답 코드 (다 풀고 열기)

- [c01_packet_stream.py](../code/c01_packet_stream.py) — `python3 code/c01_packet_stream.py` → `c01 OK`
- 연습 파일: `code/practice/c01_packet_stream.py` → `python3 code/run_all.py --practice c01`

## 9. 내 풀이 기록

| 날짜 | 걸린 시간 | 놓친 엣지 | 다음엔 |
|---|---|---|---|
| | | | |
