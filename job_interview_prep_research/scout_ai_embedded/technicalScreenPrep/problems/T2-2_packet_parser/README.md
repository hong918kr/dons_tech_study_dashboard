# T2-2 · UART Temperature Packet Parser

> **ID**: T2-2 · **Priority**: P0 · **Pattern**: byte-wise state machine + resync · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-2` / `./run.sh T2-2 sol`

## 1. Problem

A temperature board streams packets to the robot's main MCU over UART. The UART RX interrupt hands you **one byte at a time**. Write a parser that emits every complete, valid packet and silently **resynchronizes** after line noise or a corrupted packet.

Frame format:

```
[0xAA SYNC] [LEN] [PAYLOAD: LEN bytes] [CHK]
```

- `1 <= LEN <= MAX_PAYLOAD` (16). Any other `LEN` is a bad frame.
- `CHK` = **XOR of `LEN` and every payload byte** (SYNC is not included).
- A temperature payload is `sensor_id` followed by `int16` temperatures (0.1 °C, little-endian). The parser does not interpret it; it emits the raw payload.
- On a bad `LEN` or bad `CHK`: increment `bad_frames` and go back to hunting for SYNC. If the bad `LEN` byte is itself `0xAA`, treat it as a new SYNC.
- `0xAA` may legally appear inside the payload or as the `CHK` byte (framing is length-based).
- `parser_feed` returns `true` exactly when the byte completes a valid packet, and only then writes `*out`.

```c
#define SYNC_BYTE   0xAAu
#define MAX_PAYLOAD 16u

typedef enum { ST_SYNC, ST_LEN, ST_PAYLOAD, ST_CHK } ParseState;
typedef struct { uint8_t len; uint8_t data[MAX_PAYLOAD]; } Packet;
typedef struct {
    ParseState st;
    uint8_t len, idx, chk;
    uint8_t data[MAX_PAYLOAD];
    uint32_t bad_frames;
} Parser;

void parser_init(Parser *p);
bool parser_feed(Parser *p, uint8_t b, Packet *out);
```

### Example

```
bytes:  00 13 | AA 03 01 2C 03 2D | AA 03 01 2C 03 00 | AA 05 02 2C 03 38 03 13
          noise   good: id 1, 812     CHK 00 (want 2D)    good: id 2, 812, 824

output: 2 packets  {len 3: 01 2C 03}  {len 5: 02 2C 03 38 03}
        bad_frames = 1
```

### Constraints

- Bytes arrive one at a time and a packet may be split across any number of calls.
- No `malloc`. Parser state must fit in a small struct.
- O(1) work per byte.

### Clarifying questions to ask (first 3 minutes)

1. Which checksum — XOR, 8-bit sum, or CRC-8/16? Does it cover LEN? SYNC?
2. What is the maximum payload length? Is `LEN = 0` valid?
3. Can `0xAA` appear inside the payload? (If yes, length-based framing; no byte stuffing?)
4. After a bad checksum, resume from the next byte or rescan the bytes of the bad frame?
5. Is there an inter-byte timeout that should reset the parser?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-2`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 상태 4개</summary>

"지금 무엇을 기다리는가"가 곧 상태입니다: SYNC 바이트 → LEN 바이트 → payload 바이트들 → CHK 바이트. `enum` + `switch` 하나로 끝납니다. 버퍼 전체를 다시 훑는 코드는 필요 없습니다.

</details>

<details>
<summary>Hint 2 — 체크섬은 받으면서 누적</summary>

LEN을 받을 때 `chk = LEN`, payload 바이트마다 `chk ^= b`. CHK 바이트가 오면 비교만 하면 됩니다. 나중에 다시 계산하지 않습니다.

</details>

<details>
<summary>Hint 3 — 실패하면 어디로?</summary>

나쁜 LEN, 나쁜 CHK 모두 `bad_frames++` 후 SYNC 상태로 돌아갑니다. 예외 하나: LEN 자리에 온 바이트가 `0xAA`면 그게 진짜 SYNC일 수 있으니 LEN 상태에 머뭅니다(테스트 3번).

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 상태 머신

ISR이 주는 바이트 하나마다 `switch`가 한 번 돌고, 각 상태는 그 바이트를 어떻게 쓸지와 다음 상태만 정합니다.

<div class="chart" data-src="chart1.svg" data-caption="상태 머신: 정상 경로는 SYNC → LEN → PAYLOAD → CHK → SYNC. 빨간 경로는 재동기화(resync)"></div>

### 4.2 바이트 스트림에 상태를 겹쳐 보기

§1 예제(테스트 1번)의 22바이트를 상태와 함께 그리면 이렇습니다. 13번 바이트 `00`은 CHK 자리에서 기대값 `2D`와 달라 프레임 전체가 버려지고, 14번 `AA`부터 다시 잡습니다.

<div class="chart" data-src="chart2.svg" data-caption="바이트 아래 글자는 그 바이트를 받기 직전의 상태. 깨진 프레임 하나가 뒤 프레임을 망치지 않습니다"></div>

### 4.3 손으로 따라가기 (trace)

테스트 1번 입력에서 `parser_feed` 직후 상태입니다(바이트 0~1은 noise라 생략).

| i | byte | 받기 전 상태 | 동작 | 받은 후 상태 | len | idx | chk | bad | 반환 |
|---|---|---|---|---|---|---|---|---|---|
| 2 | AA | SYNC | SYNC 발견 | LEN | – | – | – | 0 | false |
| 3 | 03 | LEN | 1 ≤ 3 ≤ 16, `chk = 03` | PAYLOAD | 3 | 0 | 03 | 0 | false |
| 4 | 01 | PAYLOAD | 저장, `03 ^ 01` | PAYLOAD | 3 | 1 | 02 | 0 | false |
| 5 | 2C | PAYLOAD | 저장, `02 ^ 2C` | PAYLOAD | 3 | 2 | 2E | 0 | false |
| 6 | 03 | PAYLOAD | 저장, `2E ^ 03`, idx == len | CHK | 3 | 3 | 2D | 0 | false |
| 7 | 2D | CHK | `2D == 2D` → **EMIT** | SYNC | | | | 0 | **true** |
| 8–12 | AA 03 01 2C 03 | … | 위와 같음 | CHK | 3 | 3 | 2D | 0 | false |
| 13 | 00 | CHK | `00 != 2D` → **drop** | SYNC | | | | **1** | false |
| 14–20 | AA 05 02 2C 03 38 03 | … | chk: 05 → 07 → 2B → 28 → 10 → 13 | CHK | 5 | 5 | 13 | 1 | false |
| 21 | 13 | CHK | `13 == 13` → **EMIT** | SYNC | | | | 1 | **true** |

payload 해석: `2C 03` → `0x032C` = 812(81.2 °C), `38 03` → `0x0338` = 824.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `ST_PAYLOAD`에서 `p->data[p->idx++]`가 안전한 이유: LEN 상태에서 `len <= MAX_PAYLOAD`를 이미 검사했고, `idx == len`이 되는 순간 CHK로 넘어갑니다. **범위 검사를 입구에서 한 번** 하는 구조입니다.
- `ST_CHK`에서 먼저 `p->st = ST_SYNC`로 바꾸고 비교합니다. 성공이든 실패든 다음 상태가 같기 때문에 분기가 줄어듭니다.
- 파서는 `out`을 성공할 때만 씁니다. 그래서 호출자는 반환값만 보고 `out`을 믿을 수 있습니다.
- 테스트 4번: payload 첫 바이트와 CHK가 모두 `0xAA`여도 길이 기반이라 정상 처리됩니다. "0xAA를 보면 무조건 새 프레임"으로 짜면 여기서 깨집니다.

### 4.5 복잡도

- 바이트당 **O(1)**, 분기 몇 개. ISR 안에서 직접 돌려도 됩니다.
- 메모리: `Parser` 28 B(64비트 macOS 기준) + 출력 `Packet` 17 B. 동적 할당 없음.

---

## 5. 임베디드 관점으로 말할 것

- **ISR과 분리**: 보통 UART RX ISR은 바이트를 [T2-1 링버퍼](../T2-1_ring_buffer/index.html)에 넣기만 하고, task가 꺼내서 `parser_feed`를 돌립니다. 파서가 O(1)이라 ISR 안에서 직접 돌려도 되지만, 완성 패킷 처리는 task에서 합니다.
- **XOR의 약점**: XOR은 같은 비트가 두 번 뒤집히는 오류나 바이트 순서 바뀜을 못 잡습니다. 실제 제품이면 CRC-8(예: 0x07)이나 CRC-16-CCITT를 쓰자고 말합니다. 구조는 같고 `chk ^= b` 한 줄만 바뀝니다.
- **inter-byte timeout**: 패킷 중간에 선이 끊기면 파서가 PAYLOAD에 머물러 다음 패킷 앞부분을 먹습니다. 마지막 바이트 시각을 기억해 두고, 예를 들어 5 ms 넘게 조용하면 SYNC로 리셋합니다(wrap-safe `now - last >= timeout`).
- **진단 카운터**: `bad_frames`, `overrun` 같은 카운터는 현장에서 "케이블 노이즈인가, 속도 불일치인가"를 가르는 첫 단서입니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| After a bad checksum you drop the whole frame. Could a real packet be hidden inside it? | 그렇습니다. 깨진 프레임 안에 진짜 `AA`가 있었다면 놓칩니다. 받은 바이트를 작은 버퍼에 두고, 실패 시 SYNC 다음 바이트부터 다시 스캔하면 됩니다. 대신 최악의 경우 바이트당 O(MAX_PAYLOAD). |
| How would you avoid `0xAA` ambiguity entirely? | 바이트 스터핑(SLIP, COBS, HDLC 방식)으로 payload 안의 SYNC 값을 이스케이프하면, SYNC를 보면 무조건 프레임 시작이라 재동기화가 즉시 됩니다. |
| Replace XOR with CRC-8. | `crc = table[crc ^ b]` 테이블 방식(256 B) 또는 비트 루프 8회. 초기값과 다항식을 프로토콜 문서에서 확인합니다. |
| The payload has a 16-bit little-endian temperature. How do you decode it portably? | 하위·상위 바이트를 시프트와 OR로 조립합니다(테스트의 `temp_at` 참고). 구조체 포인터 캐스팅은 정렬·엔디안·strict aliasing 문제가 있습니다. |
| What if packets arrive faster than the task consumes them? | 완성 패킷을 별도 큐에 넣고, 꽉 차면 버린 개수를 셉니다. 원인은 baud rate 대비 task 주기 계산으로 설명합니다. |

## 7. 자주 하는 실수

- `ST_LEN`에서 `LEN == 0` 또는 `LEN > MAX_PAYLOAD` 검사를 빠뜨림 → `data[]` 오버런(테스트 3번, ASan이 잡음)
- 체크섬에 LEN을 포함하지 않음 → 테스트 1번부터 실패. 프로토콜 정의를 먼저 물어볼 것
- payload 중간의 `0xAA`를 SYNC로 처리 → 테스트 4번 실패
- 패킷을 함수 하나에 배열로 받아 파싱 → "바이트 단위로 들어온다"는 요구를 놓침(테스트 2번: 쪼개진 패킷)
- 실패 후 `idx`, `chk`를 리셋하지 않음 → LEN 상태에서 리셋하면 자연히 해결됩니다
