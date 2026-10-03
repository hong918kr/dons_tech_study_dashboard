# 📡 06 · 바이트 스트림 → 프레임 — chunk 경계 · resync · CRC8 · 버퍼 상한

> UART `read()`는 프레임 경계를 모른다. 조각이 프레임 중간에서 잘리고, 노이즈와 CRC 틀린 프레임이 섞인다. 내부 버퍼(사실상 ring buffer)에 쌓아 두고 완성된 프레임만 꺼내는 `Framer.feed(chunk)`를 만든다. **어떤 크기로 잘라 넣어도 결과가 같아야** 한다는 것을 parametrize로 증명한다.

## 왜 나오나

- ring buffer가 실제로 쓰이는 곳이 바로 여기다: UART RX → 링 → 파서. "ring buffer를 만들었으면 이제 그 위에서 프레임을 뽑아 봐"는 자연스러운 2단계 [추정]
- 테스트 팀 실무: 텔레메트리·CRSF·MSP 같은 프레임 프로토콜을 PC 쪽에서 파싱 → [FTE 문제 02 CRSF](../../../firmwareTestEngineerPrep/site/problems/02_crsf_frame_parser.html) (그건 완성된 바이트열, 이번엔 **스트리밍**)
- Full Stack 관점: TCP 스트림에서 메시지 경계 찾기(length-prefixed framing)와 똑같은 문제

## 문제

> "Bytes arrive from a UART in arbitrary chunks. Frames look like: sync byte 0xAA, a length byte, the payload, then a CRC-8 over length and payload. Write a `feed(chunk)` that returns any complete frames, survives noise, and doesn't grow memory without bound."

```
0xAA | LEN (0..64) | PAYLOAD (LEN bytes) | CRC8 (poly 0x07, over LEN+PAYLOAD)
```

- `feed(chunk) -> [payload, ...]`, 통계 `frames`, `crc_errors`, `resync_bytes`
- CRC가 틀리면 0xAA **한 바이트만** 버리고 다시 sync를 찾는다 (그 안에 진짜 프레임이 숨어 있을 수 있다)
- `LEN > 64`면 가짜 sync → 기다리지 말고 바로 1바이트 버림
- 버퍼가 `max_buffer`를 넘으면 앞을 잘라 낸다

## 엣지 케이스

- chunk 크기 1 (최악), 프레임 길이와 무관한 크기 7, 전체 한 번에
- payload 안에 0xAA가 들어 있음 → sync는 프레임 **시작**에서만 의미
- 길이 0 payload
- 앞쪽 쓰레기 바이트, CRC 깨진 프레임 다음의 정상 프레임
- 가짜 헤더(0xAA, LEN) 안에 진짜 프레임이 들어 있는 경우
- 끝나지 않는 프레임 → 메모리 상한

## 힌트

1. 상태 머신 대신 **"버퍼에서 찾기"** 방식이 짧고 버그가 적다: `while True: find(SYNC) → 길이 확인 → 전체 길이 확인 → CRC → 꺼내기`
2. "더 필요한데 아직 안 옴"이면 `break` (다음 feed를 기다림), "틀렸다"면 1바이트 버리고 `continue`
3. CRC-8/SMBUS(poly 0x07)의 표준 check 값: `crc8(b"123456789") == 0xF4` — 구현을 이걸로 먼저 검증
4. 테스트: `@pytest.mark.parametrize("size", [1, 2, 3, 7, 64, len(STREAM)])` 로 같은 스트림을 다르게 잘라서 결과 비교

## 풀이 해설

- **불변식**: feed가 끝났을 때 버퍼는 "SYNC로 시작하는 미완성 프레임" 또는 빈 상태. 이걸 말로 하면 코드 리뷰가 쉬워진다
- **1바이트만 버리는 이유**: CRC 실패 시 LEN만큼 통째로 버리면, 가짜 sync(노이즈 0xAA) 뒤에 숨은 진짜 프레임을 잃는다. 비용은 최악 O(n²)지만 n ≤ 67이라 무시 가능
- **`bytearray` + `del buf[:n]`**: 앞에서 지우는 건 O(n) 복사다. 고속 스트림이면 `memoryview` + 읽기 인덱스, 즉 진짜 ring buffer로 바꾼다 — 이 trade-off를 먼저 말하기
- **C로 옮기면**: 고정 크기 링 + 상태 머신(WAIT_SYNC → WAIT_LEN → WAIT_BODY → CHECK) + 타임아웃(프레임 사이 간격이 길면 리셋). DMA idle-line 이벤트로 chunk가 들어온다 → [dma_circular_rx.c](../../../onsitePrep/site/code/dma_circular_rx.c.html)

## 말하면서 풀기

- "The key property is that chunking must not matter — so my main test feeds the same stream at chunk sizes from 1 to all-at-once and expects identical frames."
- "On a CRC failure I only drop the sync byte, not the whole claimed frame, because the length byte itself might be noise."
- "I'm counting resync bytes and CRC errors — in a test rig those counters are how you notice a marginal baud rate or a noisy cable."

## 꼬리 질문

- "Throughput is 2 MB/s — is this fast enough?" → 앞 삭제가 병목. 읽기 인덱스 + 주기적 compact, 또는 C 확장/`numpy` 검색. 먼저 측정(`cProfile`)
- "How would you fuzz it?" → 무작위 바이트 + 정상 프레임 섞기, 크래시 없음·메모리 상한·정상 프레임은 모두 복구를 불변식으로. Hypothesis `@given(st.binary())`
- "Escape bytes instead of length?" → SLIP/COBS. COBS는 0x00을 구분자로 쓰고 오버헤드가 작다
- "What if LEN itself is corrupted to a small valid value?" → CRC가 잡는다. 그래서 CRC는 LEN을 포함해야 한다

## 파일

- [starter](../starters/06_stream_framer.py) · [모범답안](../solutions/06_stream_framer.py)
- 채점: `python3 python/run.py 06` · `python3 python/run.py 06 --sol`
