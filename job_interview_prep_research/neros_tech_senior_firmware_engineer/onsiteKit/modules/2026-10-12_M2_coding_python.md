# 🐍 M2 코딩 45분 (Python) — 힌트 없는 문제에서 자료구조 찾기 + overflow 놓치지 않기

> 리크루터: 지난번 ring buffer는 **거의 다 했지만 overflow case를 놓쳤다**. 이번엔 비슷한 유형을 **Python으로**, 그리고 "ring buffer"라고 **말해 주지 않을 것** 같다. → 문제 문장에서 "고정 크기 · 스트림 · 시간 초과"를 듣고 구조를 스스로 꺼내는 연습을 한다.

> 🆕 10-14 확정 일정: 1:1 **Jon Kotowski 45분 (13:45~14:30, 마지막 세션)** — 지난 코딩 스크린 면접관이라 코딩일 가능성이 가장 높다 [추정]. 하루의 마지막이라 지쳐 있을 때 → 모의 45분은 **오후에 피곤할 때** 해 본다.

## 1. 지난번 놓친 것 — overflow를 어떻게 다뤄야 했나

| 질문 | 답해야 했던 것 |
|---|---|
| 꽉 찼을 때 push하면? | **정책을 먼저 정한다**: (a) 새 것을 거절하고 False + `dropped += 1` (b) 가장 오래된 것을 덮어쓰고 `overwritten += 1` |
| 누가 정하나? | 면접관에게 묻는다: "When it's full, should new data be rejected or should it overwrite the oldest?" |
| 여러 개를 한 번에 넣을 때 공간이 모자라면? | all-or-nothing(아무것도 안 넣음) vs 들어가는 만큼만 넣고 개수 반환 — **역시 묻는다** |
| 테스트로 무엇을 보여야 하나? | 꽉 찬 상태에서 push → 반환값 · 카운터 · 기존 데이터 순서가 그대로인지 |
| 왜 중요한가 (테스트 직군) | 조용히 데이터를 잃는 것이 가장 나쁜 버그다. **잃었으면 센다** |

> 습관: 코드 첫 줄을 쓰기 전에 **경계 4개를 말로 체크** — empty · full · 1개 · wrap-around. 마지막에 그 4개 테스트를 직접 쓴다.

## 2. 문제 문장 → 자료구조 (힌트 없을 때)

| 이런 말이 들리면 | 꺼낼 것 | Python |
|---|---|---|
| "fixed memory", "can't allocate", "ISR hands bytes to main loop" | 고정 크기 FIFO (ring buffer) | list + head/tail/count, 또는 `deque(maxlen=N)` |
| "bytes arrive in chunks", "serial", "framing", "sync byte" | 스트림 버퍼 + 상태 기반 파서 | `bytearray`, `.find()`, `del buf[:n]` |
| "last N readings", "moving average", "K of the last N" | 슬라이딩 윈도우 | `deque` + 누적 합/개수 |
| "timeout", "retry", "ACK", "if no response" | id → (보낸 시각, 횟수) 표 + 시간 주입 | `dict`, `now` 인자 |
| "how many were lost", "sequence number", "counter wraps" | 모듈러 차이 | `(a - b) % 2**16`, 반쪽 기준 |
| "only arm when...", "mode", "go to safe state" | 상태 머신 | 상태 상수 + 전이 함수 + 전이 로그 |
| "binary record", "little-endian", "int16" | 고정 레이아웃 디코드 | `struct.unpack("<HhhhHB", ...)` |
| "earliest deadline", "schedule the next" | 우선순위 큐 | `heapq` |

## 3. 45분 진행 순서

| 분 | 할 일 |
|---|---|
| 0~5 | **질문 3개 이상**: 크기 한계 · 꽉 찼을 때 정책 · 잘못된 입력 처리 · 시간 단위 |
| 5~8 | 입력/출력 예시를 손으로 하나 (작은 숫자) |
| 8~10 | 접근 한 줄 + 복잡도 말하기, 자료구조 이름 말하기 |
| 10~30 | 코딩. 말하면서. 함수는 작게 |
| 30~40 | **테스트 직접 작성**: happy · empty · full/overflow · wrap · 잘못된 입력 · 랜덤 대조 |
| 40~45 | 후속: "C로 하면?", "스레드 두 개면?", "메모리 더 줄이려면?" |

## 4. 문제 세트 (전부 실행·테스트됨)

| # | 문제 | 지난번과의 연결 | 우선 |
|---|---|---|---|
| [C01](../coding/2026-10-12_C01_packet_stream.md) | 바이트 스트림 → 패킷 (sync · len · checksum · resync) | 스크린 보너스 extract_packet의 Python판 | S |
| [C02](../coding/2026-10-12_C02_fixed_fifo.md) | 고정 크기 FIFO, 꽉 참 정책 2가지 | **놓친 overflow 그 자체** | S |
| [C03](../coding/2026-10-12_C03_sliding_window.md) | 센서 이동 평균 + K-of-N 알람 | 고정 크기 윈도우 = 덮어쓰는 ring | A |
| [C04](../coding/2026-10-12_C04_retry_queue.md) | 명령 · ACK · timeout · retry · 8비트 id | 큐 포화 · 늦은 ACK | S |
| [C05](../coding/2026-10-12_C05_seq_gap_log.md) | 로그에서 유실 · 중복 · 순서 바뀜 (16비트 wrap) | 테스트 엔지니어 일상 업무 | A |
| [C06](../coding/2026-10-12_C06_failsafe_fsm.md) | arm / failsafe 상태 머신 | 시간 경계 · 히스테리시스 | A |
| [C07](../coding/2026-10-12_C07_binary_telemetry.md) | struct 디코드 + CRC-8 직접 구현 | 스크린 CRC의 Python판 | B |

## 5. 연습 방법 (파일 구조)

```text
onsiteKit/code/
  c01_packet_stream.py ... c07_binary_telemetry.py   정답 + 테스트 (python3 파일명 → "cNN OK")
  practice/cNN_*.py      같은 파일인데 풀이 부분만 TODO (테스트는 그대로)
  run_all.py             python3 run_all.py --practice [cNN]
  make_practice.py       python3 make_practice.py cNN --reset   (연습 파일 다시 비우기)
```

1. **1회차**: 문제 페이지의 "면접관 문장"만 읽고 **빈 파일**에 처음부터 (테스트도 직접)
2. **2회차**: `practice/cNN_*.py`의 TODO만 채워 `python3 run_all.py --practice cNN` → 내가 놓친 테스트가 무엇이었나 확인
3. **3회차(이틀 뒤)**: 다시 빈 파일, 20분 제한

## 6. 맨손 Python 치트시트 (자동완성 없이)

```python
from collections import deque, defaultdict, Counter
import struct, re, heapq, math

buf = bytearray()
buf += b"\xaa\x01"          # append bytes
i = buf.find(0xAA)          # -1 if missing
del buf[:i]                 # drop front (O(n) but fine at these sizes)
n = buf[1]                  # int 0..255
payload = bytes(buf[2:2+n]) # copy out

q = deque(maxlen=5)         # oldest falls out automatically
q.append(x); q.popleft(); q[0]

(seq - expected) % 65536    # wrap-safe difference
int.from_bytes(b"\x34\x12", "little") == 0x1234
struct.unpack("<Hh", b"\x01\x00\xff\xff") == (1, -1)
x & 0xFF, x >> 8, (x >> 3) & 1, x | (1 << 2), x & ~(1 << 2)

heapq.heappush(h, (deadline, cid)); heapq.heappop(h)
math.isnan(v)               # NaN != NaN
```

## 7. Python에서 놓치기 쉬운 것

- `None`, `NaN` 입력: `x < low`는 None이면 TypeError, NaN이면 항상 False → **먼저 검사**
- float 시간 경계: `1.10 + 0.05 == 1.1500000000000001` → 시간은 **정수 ms**로 (C04에서 실제로 걸림)
- `bytes[i]`는 int, `bytes[i:i+1]`은 bytes
- 리스트를 순회하면서 삭제 금지 → 새 리스트를 만들거나 키 목록을 먼저 복사 (`list(d)`)
- 기본 인자에 `[]` 금지

## 8. C로 바꿔 말하기 (후속 질문 대비)

| Python | C에서는 |
|---|---|
| `bytearray`, `del buf[:n]` | 고정 배열 + 인덱스(head/tail), 앞으로 당기기 대신 ring |
| `deque(maxlen)` | 2의 거듭제곱 크기 + mask, 또는 `% N` |
| dict id 표 | id를 인덱스로 쓰는 고정 배열 `inflight[256]` |
| 예외 | 반환 코드 (`bool`, 음수 에러) |
| GIL 덕에 단순 | ISR ↔ main: SPSC면 lock 없이 head는 쓰는 쪽만, tail은 읽는 쪽만 갱신 + `volatile`/atomic |
