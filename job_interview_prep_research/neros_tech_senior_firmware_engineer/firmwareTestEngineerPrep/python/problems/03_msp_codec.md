# 🐍 03 · MSP v1 코덱 — Betaflight에 명령 보내고 자세(attitude) 읽기

> Betaflight Configurator가 FC와 대화하는 MSP v1 프로토콜을 Python으로 인코딩/디코딩한다. `$M<` 요청, `$M>` 응답, `$M!` 에러, XOR 체크섬, 그리고 `struct`로 MSP_ATTITUDE(108)의 int16 필드를 꺼낸다.

## 왜 나오나

- JD Nice-to-have: "Experience with FPV Drone software including **Betaflight**"
- JD 업무: "Design, develop, and maintain test suites to validate the Neros drone & ground control software"
- HIL 테스트에서 FC의 **내부 상태를 읽는 가장 쉬운 창구**가 MSP다. arm 상태, 자세, 모터 출력, 배터리 전압을 MSP로 폴링해서 "스틱을 넣었더니 FC가 기대대로 반응했나"를 assert 한다 [추정: Neros 스택이 Betaflight 계열이라는 전제. 공고에 Betaflight가 나오지만 내부 fork 여부는 모름]
- `struct.pack/unpack`과 체크섬은 Python 테스트 자동화 면접의 기본기

## 문제

> "We talk to the flight controller over MSP. Write the encoder for requests and a parser that pulls responses out of a serial byte stream. Handle the error response too. Then decode the MSP_ATTITUDE reply into degrees."

| 필드 | 크기 | 값 |
|---|---|---|
| preamble | 2 | `$M` |
| direction | 1 | `<` 요청 (host→FC), `>` 응답 (FC→host), `!` 에러 |
| size | 1 | payload 길이 (0~255) |
| cmd | 1 | 명령 번호 (예: 108 = MSP_ATTITUDE) |
| payload | size | 명령별 |
| checksum | 1 | `size ^ cmd ^ payload[0] ^ ... ^ payload[n-1]` |

- MSP_ATTITUDE 응답 payload: `int16 roll` (0.1°), `int16 pitch` (0.1°), `int16 yaw` (1°), little-endian
- 참고: **MSP v2**는 `$X` 헤더, 16bit cmd, 16bit size, CRC-8/DVB-S2 체크섬을 쓴다. 255바이트 이상 payload나 256번 이상 명령에 쓴다. 이 문제에서는 v1만

구현할 것: `checksum`, `encode_request(cmd, payload=b"")`, `parse_responses(buf: bytearray)` (완성된 프레임만 소비하고 나머지는 buf에 남김, 에러 프레임은 `MspError` 객체로), `decode_attitude(payload)`

## 예시

```python
>>> encode_request(108)
b'$M<\x00ll'                      # size 0, cmd 108(0x6c), checksum 0^108 = 108
>>> buf = bytearray(encode_response(108, struct.pack("<hhh", -125, 30, 270)))
>>> [(cmd, p)] = parse_responses(buf)
>>> decode_attitude(p)
(-12.5, 3.0, 270.0)
```

## 엣지 케이스

- 응답이 두 번의 `read()`에 걸쳐 들어옴 → 첫 호출은 `[]`, buf에 남은 조각이 다음 호출에서 합쳐져야 함
- 앞에 쓰레기 바이트 / 부팅 메시지 → `$M` 전까지 버림
- 버퍼 끝이 `$` 한 글자 → 다음 chunk의 `M`일 수 있으니 **보존**
- 체크섬 불일치 → 1바이트 버리고 재탐색
- `$M<` (우리가 보낸 요청의 에코, half-duplex 배선에서 생김) → 응답이 아니므로 건너뜀
- payload 256바이트 이상 인코딩 시도 → `ValueError`

## 힌트

1. `bytearray.find(b"$M")`로 헤더 위치를 찾고 `del buf[:i]`로 앞을 버린다. 이후 buf의 맨 앞은 항상 `$M`
2. `size = buf[3]`, `cmd = buf[4]` — bytearray 인덱싱은 int를 준다. `buf[2:3]`은 bytes를 준다 (방향 문자 비교용)
3. 체크섬은 `functools.reduce(lambda a, b: a ^ b, payload, size ^ cmd)`. `struct.unpack("<hhh", p)` — `<`는 little-endian + 패딩 없음, `h`는 signed int16

## 풀이 해설

- **구조는 02번과 같다**: 버퍼 맨 앞 = 후보, 틀리면 1바이트 drop, 모자라면 대기. 다른 점은 sync가 2바이트(`$M`)라서 `find`로 점프할 수 있다는 것
- **에러를 반환값으로**: `$M!`에서 예외를 바로 던지면 같은 버퍼에 있던 뒤쪽 응답을 잃는다. 결과 리스트에 `MspError` 객체를 넣고 호출자가 판단하게 했다. 면접에서 "왜 raise 안 했나?"라고 물으면 이 이유를 말한다
- **struct 포맷 문자**: `<` little-endian, `>` big-endian, `h` int16, `H` uint16, `i` int32, `B` uint8, `f` float32. `<`를 빼면 native 정렬·패딩이 들어가서 크기가 달라질 수 있다
- **복잡도**: 버퍼 길이 N에 대해 O(N). 프레임 최대 261바이트
- **흔한 실수**
- 체크섬에 `$M<`까지 포함 — size부터다
- roll/pitch를 10으로 안 나눔 (단위를 문서에서 확인하는 습관을 보여 주기)
- `buf[2] == b">"` — int와 bytes 비교라서 항상 False. `buf[2] == ord(">")` 또는 `buf[2:3] == b">"`

## 말하면서 풀기

- "MSP v1 is simple: dollar-M, a direction byte, size, command, payload, and an XOR checksum over size, command, and payload."
- "I'll parse into a caller-owned bytearray, consume only complete frames, and leave partial data for the next read, since serial reads never line up with frame boundaries."
- "I return error frames as objects instead of raising, so one rejected command doesn't make us drop the other responses already in the buffer."
- "In a HIL test I'd use this to poll attitude and arming state, and assert the flight controller responded to the injected stick input within a time budget."

## 꼬리 질문

- MSP로 FC를 테스트할 때 한계는? → 폴링이라 타이밍 해상도가 낮고, FC 스케줄러에 부하를 준다. 고속 데이터는 blackbox 로그나 전용 텔레메트리가 낫다
- 응답이 안 오면? → 요청마다 timeout + 재시도 (문제 04의 드라이버 패턴). 재시도해도 안 되면 FC hang 의심 → 전원 사이클 후 로그 수집
- 요청과 응답을 어떻게 짝짓나? → v1에는 시퀀스 번호가 없다. 응답의 cmd로 맞추고, 한 번에 한 요청만 in-flight로 두는 게 안전
- MSP_SET_RAW_RC(200)로 스틱을 넣을 수도 있는데 CRSF 주입과 차이는? → MSP 주입은 RX 경로(CRSF 파서, failsafe 로직)를 우회한다. **실제 경로를 테스트하려면 CRSF 주입**이 맞다
- 이 코덱 자체는 어떻게 테스트? → 알려진 바이트열(골든 벡터), round-trip, 랜덤 chunk 분할 fuzz

## 파일

- [starter](../starters/03_msp_codec.py) · [모범답안](../solutions/03_msp_codec.py)
- 채점: `python3 python/run.py 03` (내 풀이) · `python3 python/run.py 03 --sol` (답안)
