# 🐍 02 · CRSF 프레임 파서 — 스트리밍 · CRC-8 · resync

> ExpressLRS 수신기가 FC로 보내는 CRSF 프레임을, 아무렇게나 잘려 들어오는 바이트 스트림에서 뽑아낸다. CRC-8/DVB-S2 검증, 깨진 프레임에서 1바이트씩 resync, 그리고 16채널 11-bit RC 값 언패킹까지.

## 왜 나오나

- JD Nice-to-have: "FPV Drone software including Betaflight, **ExpressLRS**", "Experience testing RF products"
- JD 필수: "Familiarity with embedded communication protocols — e.g. I2C, SPI, **UART**"
- HIL에서 테스트 PC는 **가짜 RC 수신기 역할**을 한다. Python이 CRSF 프레임을 만들어 FC UART로 넣고(스틱 입력 주입), FC가 돌려주는 텔레메트리 프레임을 파싱한다. 이 문제는 그 인코더/디코더의 핵심
- "바이트 스트림 → 프레임" 파서는 임베디드 테스트 면접의 단골. bytes/bytearray, 비트 연산, 상태 관리를 한 번에 본다

## 문제

> "Our receiver talks CRSF over UART. Write a parser that takes chunks of bytes as they arrive and returns complete, CRC-valid frames. Chunks can split a frame anywhere, and the line can have garbage. If a frame is corrupted, recover and keep going. Then decode the RC channels frame."

프레임 구조 (Betaflight `rx/crsf.c`, ExpressLRS 문서 기준):

| 필드 | 크기 | 설명 |
|---|---|---|
| sync / address | 1 | FC 방향은 `0xC8`. 그 외 `0xEA`(radio), `0xEE`(TX module), `0xEC`(receiver) |
| length | 1 | 뒤따르는 바이트 수 = type + payload + crc = N + 2 |
| type | 1 | `0x16` RC_CHANNELS_PACKED, `0x14` LINK_STATISTICS, `0x08` BATTERY_SENSOR 등 |
| payload | N | 타입별 |
| crc | 1 | CRC-8/DVB-S2 (poly `0xD5`, init 0) over **type + payload** |

- 전체 프레임은 최대 64바이트 → length 최대 62
- RC_CHANNELS_PACKED payload = 22바이트 = 16채널 × 11bit, **LSB-first little-endian 비트 패킹**
- 채널 값 범위 172~1811, 중앙 992. µs로는 대략 988~2012, 992 → 1500µs

구현할 것: `crc8_dvb_s2(data)`, `build_frame(type, payload)`, `CrsfParser.feed(chunk) -> [(addr, type, payload)]`, `unpack_rc_channels(payload) -> [16 ints]`

## 예시

```python
>>> f = build_frame(0x14, b"\x01\x02\x03")
>>> f.hex(" ")
'c8 05 14 01 02 03 ..'          # len = 3 + 2 = 5, 마지막은 CRC
>>> p = CrsfParser()
>>> p.feed(f[:3]); p.feed(f[3:])
[]
[(200, 20, b'\x01\x02\x03')]
>>> crc8_dvb_s2(b"123456789") == 0xBC   # 표준 check 값으로 CRC 구현 검증
True
```

## 엣지 케이스

- 한 바이트씩 들어와도 결과가 같아야 한다 (chunk 경계 독립성)
- 앞에 쓰레기 바이트 → 버리고 `dropped`로 센다
- sync는 맞는데 length가 0, 1, 63 이상 → 가짜 sync로 보고 1바이트 drop
- CRC 불일치 → **프레임 전체가 아니라 sync 1바이트만** 버리고 재탐색. 깨진 "프레임" 안에 진짜 프레임 시작이 들어 있을 수 있다
- 프레임 뒤쪽이 아직 안 왔으면 버퍼에 남겨 두고 다음 `feed`를 기다린다
- payload 안에 `0xC8`이 들어 있어도 문제없다 (length로 경계를 정하므로)

## 힌트

1. 내부에 `bytearray` 버퍼 하나. `feed`는 붙이고 → `while len(buf) >= 2:` 루프로 가능한 만큼 뽑는다
2. 루프 한 바퀴에서 결정은 네 가지뿐: (a) 헤더가 이상함 → drop 1 (b) 아직 다 안 옴 → break (c) CRC 틀림 → drop 1 (d) 성공 → 프레임 떼어 냄
3. 11bit 언패킹은 `int.from_bytes(payload, "little")`로 176bit 정수 하나를 만들면 `(v >> 11*i) & 0x7FF` 한 줄. C처럼 바이트 경계를 계산할 필요가 없다 — Python 정수는 임의 정밀도

## 풀이 해설

- **상태 = 버퍼 하나**: 별도의 FSM enum 없이 "버퍼 맨 앞이 항상 프레임 후보"라는 불변식 하나로 끝난다. 후보가 틀렸으면 맨 앞 1바이트를 버린다
- **왜 1바이트만 버리나**: 노이즈로 가짜 `0xC8`이 잡혔을 때 length만큼 통째로 버리면, 그 구간에 걸쳐 있던 진짜 프레임까지 잃는다. 1바이트 drop은 최악이 O(N·L)이지만 L ≤ 64라 사실상 O(N)
- **CRC 범위**: sync와 length는 CRC에 **포함되지 않는다** (type부터 payload 끝까지). 여기서 틀리는 사람이 많다
- **CRC-8/DVB-S2 검증법**: 구현 후 `b"123456789"` → `0xBC`인지 확인. 표준 CRC 카탈로그의 check 값이라, 면접에서 "어떻게 CRC 구현이 맞는지 아나?"에 대한 답이 된다
- **µs 변환**: `(t - 992) * 5/8 + 1500`. 172 → 988, 992 → 1500. 1811은 이 식으로 ≈2012 (Betaflight 코드는 약간 다른 계수를 쓴다 [추정])
- **흔한 실수**
- `del buf[:n+2]` 대신 `buf = buf[n+2:]`로 매번 새 객체 생성 (동작은 하지만 느림)
- `& 0xFF`를 빼서 CRC가 8bit를 넘어감 — Python 정수는 오버플로가 없다
- 버퍼가 부족할 때 `break` 대신 drop 해 버려서 chunk 경계에서 프레임을 잃음

## 말하면서 풀기

- "I'll keep a bytearray buffer, and the invariant is that the head of the buffer is always a frame candidate. Each loop iteration either rejects one byte, waits for more data, or pops a full frame."
- "On a CRC failure I only drop one byte, not the whole frame, because a real frame header could be inside the corrupted region."
- "To validate my CRC implementation, I'd check it against the standard check value: CRC-8/DVB-S2 of the ASCII string 123456789 is 0xBC."
- "For the 11-bit channels, Python integers are arbitrary precision, so I'll load all 22 bytes as one little-endian integer and shift out 11 bits at a time."

## 꼬리 질문

- 이걸 HIL에서 어떻게 쓰나? → 테스트 PC가 USB-UART로 FC의 RX 포트에 붙어서 `build_frame(0x16, pack_rc_channels(sticks))`를 약 50~500Hz로 보냄. 스틱을 움직였을 때 FC가 arm/모터 출력을 기대대로 바꾸는지 검증 [추정: 실제 레이트는 ELRS 패킷 레이트 설정에 따라 다름]
- RC 프레임을 **안 보내면** FC는? → failsafe 진입해야 한다. "링크 끊김 → N ms 안에 failsafe" 테스트가 대표적인 HIL 케이스
- CRC 에러율을 링크 품질 메트릭으로 쓸 수 있나? → 유선 UART에서 CRC 에러가 0이 아니면 baud 불일치·배선·접지 문제. 무선 쪽 품질은 LINK_STATISTICS(0x14)의 LQ/RSSI로 본다
- 성능이 문제라면? → 파이썬 바이트 루프는 느리므로 `bytes.find(0xC8)`로 다음 sync까지 한 번에 점프, 또는 CRC 테이블(256 entry) 사용
- CRSF baud는? → Betaflight 기본 420000 baud, ELRS는 더 높게도 설정 가능 [추정]

## 파일

- [starter](../starters/02_crsf_frame_parser.py) · [모범답안](../solutions/02_crsf_frame_parser.py)
- 채점: `python3 python/run.py 02` (내 풀이) · `python3 python/run.py 02 --sol` (답안)
