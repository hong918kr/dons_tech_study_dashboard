# E2 · Extract Packets from a Byte Buffer

> **ID**: E2 · **Priority**: P0 · **Pattern**: buffer scan + resync + caller-kept tail · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh E2` / `./run.sh E2 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: UART로 쌓이는 바이트 버퍼에서 첫 번째 완전하고 올바른 프레임을 찾아내고, 앞에서 몇 바이트를 버려도 되는지 알려주는 파서(+ 온도 디코드, + 수신 루프)를 만든다.
- **상황**: 온도 보드가 메인 MCU로 UART 프레임을 보낸다. UART 드라이버는 받은 바이트를 누적 버퍼 뒤에 계속 붙인다. 호출자는 처리 안 된 꼬리를 남겨두고, 바이트가 더 오면 다시 부른다.
- **배경 개념**
  - **UART 바이트 스트림**: 바이트가 아무 크기 덩어리로 끊겨 도착한다. 한 번 읽었을 때 프레임이 반만 와 있을 수도, 앞에 쓰레기 바이트가 있을 수도 있다.
  - **sync 바이트**: 프레임 시작 표시 `0xAA 0x55`. 하지만 payload 안에도 같은 값이 나올 수 있어서, 프레임 경계는 LEN으로 정한다.
  - **CRC**: 바이트가 깨졌는지 검사하는 16비트 체크값. `crc16()`은 주어진 라이브러리(CRC-16/CCITT-FALSE)로 TYPE, LEN, PAYLOAD에 대해 계산하고, 프레임에는 little-endian(LO 먼저)으로 실린다.
- **입력**: `extract_packet(buf, len, out, consumed)` — 현재 버퍼 바이트 `buf[len]`. `decode_temp(p, r)` — 찾은 `Packet`. `rx_feed(rx, data, n, out, out_cap)` — 누적 상태 `RxAcc`(64바이트 `buf`, `len`, `bad_crc`, `bad_len`)와 새로 받은 `n`바이트.
- **출력**: `extract_packet`은 `PktStatus` 반환 + `*consumed`(앞에서 버려도 되는 바이트 수), FOUND면 `*out` 채움. `decode_temp`은 `bool` + `r->temp_c100`(0.01 °C), `r->t_ms`(ms). `rx_feed`는 찾은 패킷 **전체 개수** 반환, `out`에는 최대 `out_cap`개만 씀.
- **규칙**
  - 프레임: `[0xAA] [0x55] [TYPE] [LEN] [PAYLOAD LEN바이트] [CRC_LO] [CRC_HI]`, `0 <= LEN <= 32`.
  - Part A 반환: `PKT_FOUND` → consumed = 앞 쓰레기 + 프레임 전체. `PKT_NEED_MORE` → consumed = 쓰레기만(미완성 프레임이나 끝의 외로운 `0xAA`는 **남김**). `PKT_BAD_LEN`(LEN > 32), `PKT_BAD_CRC` → consumed = 쓰레기 + **1**(`0xAA` 하나만 건너뛰고 다시 스캔).
  - `buf`는 수정 금지, 호출당 O(len), malloc 금지.
  - Part B: TYPE `0x01`이고 LEN == 6일 때만 true. 앞 2바이트 `int16` 온도(0.01 °C, LE), 뒤 4바이트 `uint32` 타임스탬프(ms, LE).
  - Part C: 64바이트 버퍼에 들어가는 만큼 붙이고 → `PKT_NEED_MORE`까지 `extract_packet` 반복 → bad 프레임 개수 세기 → 남은 꼬리를 `memmove`로 앞으로. `n`이 남은 공간보다 크면 들어가는 만큼 붙이고 처리하고 반복.
- **예제 풀어 읽기**: 24바이트 버퍼. 앞 `00 AA 13`은 쓰레기, `AA 55 09 04`는 가짜 sync(TYPE 09, LEN 4로 읽히지만 CRC가 안 맞음) → 1차 호출 `PKT_BAD_CRC`, consumed = 4(쓰레기 3 + `0xAA` 1). 2차 호출(buf + 4, 20바이트) → 남은 쓰레기 3바이트 + 프레임 A 12바이트 = consumed 15, 온도 `0x09E6` = 2534(25.34 °C), t = 1000 ms. 3차 호출(buf + 19, 5바이트 `AA 55 01 06 F0`) → 미완성이라 `PKT_NEED_MORE`, consumed = 0(5바이트 유지). 호출자가 5바이트를 앞으로 옮기고 7바이트를 더 붙이면 12바이트 → 4차 호출 `PKT_FOUND`, consumed = 12, 28.00 °C @ 2000 ms.
- **놓치기 쉬운 것**: NEED_MORE에서 버리는 양 — `AA 55 01` → 0, `00 01 AA 55 01` → 2, `01 02 AA` → 2(끝의 `AA`는 sync일 수 있어 유지), `01 02 03` → 3, `NULL, 0` → 0. LEN 33 → `PKT_BAD_LEN`, consumed 1. LEN 32(프레임 38바이트)와 LEN 0(heartbeat, 6바이트)은 정상. 프레임 두 개 연속. payload 안의 `AA 55`는 데이터. 음수 온도(-150), LEN 5나 TYPE `0x02`면 `decode_temp` false. `rx_feed`는 한 번에 통째로 넣든 1..20바이트씩 랜덤으로 쪼개 넣든 똑같이 패킷 6개, `bad_crc` 2, `bad_len` 1, 끝에 `rx.len` 0이어야 한다.
- **구현할 함수**
  - Part A — `extract_packet(buf, len, out, consumed)`: 스캔 → 헤더 → LEN 검사 → 길이 확인 → CRC → 복사.
  - Part B — `decode_temp(p, r)`: 온도 패킷 payload를 LE로 풀기.
  - Part C — `rx_feed(rx, data, n, out, out_cap)`: 붙이기 + 반복 추출 + 꼬리 당기기.

## 1. Problem

A temperature board sends frames to the main MCU over UART. The UART driver **appends** received bytes to an accumulation buffer. Implement `extract_packet()`: look at the bytes currently in the buffer and find the **first complete, valid frame**. It reports how many bytes at the front of `buf` the caller may drop (`*consumed`). The caller keeps the unprocessed tail and calls again after more UART bytes are appended.

Frame format:

```
[0xAA] [0x55] [TYPE] [LEN] [PAYLOAD: LEN bytes] [CRC_LO] [CRC_HI]
```

- `0 <= LEN <= MAX_PAYLOAD` (32). A larger `LEN` means this was a false sync.
- `CRC = crc16(TYPE, LEN, PAYLOAD)` — CRC-16/CCITT-FALSE, **given library** (like a provided `CRC.c`), sent **little-endian**.
- `0xAA 0x55` can also appear inside a payload (framing is length-based).

| Return | When | `*consumed` |
|---|---|---|
| `PKT_FOUND` | a valid frame | garbage before it + the whole frame; `*out` filled |
| `PKT_NEED_MORE` | no complete frame yet | garbage only — a partial frame (or a trailing lone `0xAA`) is **kept** |
| `PKT_BAD_LEN` | `LEN > 32` | garbage + **1** (skip only the `0xAA`) |
| `PKT_BAD_CRC` | CRC mismatch | garbage + **1** (skip only the `0xAA`) |

**Part B** — `decode_temp()`: a `TYPE 0x01` packet has `LEN == 6`: `int16` temperature in 0.01 °C (LE), then `uint32` timestamp in ms (LE). Return `false` for any other type or length.

**Part C** — `rx_feed()`: the driver loop. Append `n` bytes into a fixed **64-byte** buffer (if `n` is larger than the free space, append what fits, process, repeat), call `extract_packet` until `PKT_NEED_MORE`, count bad frames, then `memmove` the unprocessed tail to the front.

```c
#define MAX_PAYLOAD 32u
typedef enum { PKT_NEED_MORE, PKT_FOUND, PKT_BAD_CRC, PKT_BAD_LEN } PktStatus;
typedef struct { uint8_t type; uint8_t len; uint8_t payload[MAX_PAYLOAD]; } Packet;
typedef struct { int16_t temp_c100; uint32_t t_ms; } TempReading;
typedef struct { uint8_t buf[64]; size_t len; uint32_t bad_crc, bad_len; } RxAcc;

uint16_t  crc16(const uint8_t *data, size_t len);                      /* given */
PktStatus extract_packet(const uint8_t *buf, size_t len, Packet *out, size_t *consumed);
bool      decode_temp(const Packet *p, TempReading *r);
size_t    rx_feed(RxAcc *rx, const uint8_t *data, size_t n, Packet *out, size_t out_cap);
```

### Example

```
buf (24 bytes):
  00 AA 13 | AA 55 09 04 | AA 55 01 06 E6 09 E8 03 00 00 14 21 | AA 55 01 06 F0
  garbage    false sync    frame A: temp 0x09E6 = 2534, t 1000   partial frame B

call 1: extract_packet(buf,      24) -> PKT_BAD_CRC,   consumed = 4
call 2: extract_packet(buf + 4,  20) -> PKT_FOUND,     consumed = 15   (25.34 C @ 1000 ms)
call 3: extract_packet(buf + 19,  5) -> PKT_NEED_MORE, consumed = 0    (keep 5 bytes)

caller: memmove 5 bytes to the front, append 0A D0 07 00 00 49 2D
call 4: extract_packet(buf, 12)      -> PKT_FOUND,     consumed = 12   (28.00 C @ 2000 ms)
```

### Constraints

- No `malloc`. The accumulation buffer is a fixed 64 bytes (a max frame is 38 bytes).
- `extract_packet` does not modify `buf`; it is O(len) per call.
- The decoded sequence must be the same no matter how the stream is chunked.

### Clarifying questions to ask (first 3 minutes)

1. Which CRC exactly (poly, init, reflect, xorout)? Over which bytes — does it include the sync? Byte order of the CRC field?
2. Does `LEN` count only the payload, or also TYPE / CRC? Is `LEN = 0` valid? What is the max?
3. On a bad CRC, do I skip the whole frame, or only the sync byte and rescan?
4. Should `NEED_MORE` drop leading garbage, or leave the buffer untouched?
5. Who owns the buffer — do I return a count and the caller compacts, or do I compact?

### First 5 minutes (before typing any code)

1. **Restate** the task in one sentence: "Given the bytes so far, return the first valid frame and how many bytes you may drop."
2. **Hand-build one example with the interviewer**: write 1 garbage byte + one valid frame + half a frame, and agree on the expected `status` and `consumed` for each call (like the Example above).
3. **List the return cases** (the table above) and write them as a comment / enum **before** the loop.
4. Only then code: scan → header → LEN check → length check → CRC → copy.

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh E2`로 바로 테스트하세요. 테스트 8묶음(0~7)이 들어 있고, 7번은 같은 스트림을 201가지 방식(한 번에 + 랜덤 1~20바이트 조각 200가지)으로 쪼개서 넣습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 함수 하나 = for 루프 하나 + return 6개</summary>

`for (i = 0; i < len; i++)`로 `AA 55`를 찾고, 찾으면 그 자리에서 **위에서부터 검사하며 바로 return**합니다. 헤더 부족 → NEED_MORE(`i`), LEN 초과 → BAD_LEN(`i+1`), 프레임 부족 → NEED_MORE(`i`), CRC 틀림 → BAD_CRC(`i+1`), 성공 → FOUND(`i+frame`). 루프를 다 돌면 NEED_MORE(`len`). 상태 머신이 필요 없습니다 — 매번 버퍼 처음부터 다시 봅니다.

</details>

<details>
<summary>Hint 2 — consumed는 "버려도 되는 바이트 수"</summary>

규칙 하나로 정리됩니다: **프레임 시작일 수 있는 바이트는 절대 버리지 않는다.** 그래서 NEED_MORE는 sync 앞 쓰레기까지만 버리고(`i`), 마지막 바이트가 `AA` 하나면 그것도 남깁니다(다음에 `55`가 올 수 있음). 실패(BAD_*)는 `AA` **한 바이트만** 넘깁니다 — 가짜 프레임 안에 진짜 프레임이 숨어 있을 수 있기 때문입니다(예제 call 1).

</details>

<details>
<summary>Hint 3 — 드라이버 루프는 pos 하나로</summary>

`pos = 0`에서 시작해 `extract_packet(buf + pos, len - pos, …)`, `pos += used`를 NEED_MORE까지 반복합니다. 그다음 한 번만 `memmove(buf, buf + pos, len - pos); len -= pos;`. 호출마다 `memmove`하면 맞기는 하지만 느립니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 버퍼 위에 consumed 화살표 그리기

예제 버퍼를 그대로 그리면 이렇습니다. 노란 4바이트는 노이즈가 우연히 `AA 55`가 된 가짜 sync입니다. LEN=4를 믿으면 프레임이 10바이트(3~12번)라 CRC를 계산할 수 있고, 값이 달라 BAD_CRC가 됩니다.

<div class="chart" data-src="chart1.svg" data-caption="호출 3번에 consumed = 4, 15, 0. BAD_CRC에서 10바이트를 버렸다면 7번에서 시작하는 진짜 frame A를 잃습니다"></div>

**왜 1바이트만 넘기나**: 가짜 헤더가 주장하는 범위(3~12번) 안에 진짜 frame A의 시작(7번)이 들어 있습니다. "틀린 프레임 전체 건너뛰기"로 짜면 테스트 1번 call 2가 실패합니다.

### 4.2 return 경로 — 코드 쓰기 전에 그리는 그림

면접에서 코딩 전에 화이트보드나 주석으로 이 그림(또는 §1의 표)을 먼저 적으면, 코드는 위에서 아래로 옮겨 적기만 하면 됩니다.

<div class="chart" data-src="chart2.svg" data-caption="노랑 = 기다림(가능한 프레임 시작은 남김), 빨강 = 가짜 sync(AA 하나만 버림), 초록 = 성공. 위에서부터 차례로 검사하고 바로 return"></div>

### 4.3 누적 버퍼 compaction

호출자는 `pos`만 앞으로 옮기다가, NEED_MORE가 나오면 남은 꼬리를 앞으로 한 번 당깁니다. 링버퍼가 아니라 **선형 버퍼 + memmove**라서 `extract_packet`은 프레임이 항상 연속된 메모리에 있다고 가정할 수 있습니다.

<div class="chart" data-src="chart3.svg" data-caption="예제를 64바이트 누적 버퍼로: 19바이트 처리 → 꼬리 5바이트를 앞으로 → 새 UART 7바이트 붙이면 frame B 완성 (테스트 1b)"></div>

### 4.4 손으로 따라가기 (trace)

| 호출 | 입력 | sync 위치 (상대) | 검사 | 결과 | consumed | pos 후 |
|---|---|---|---|---|---|---|
| 1 | `buf+0`, 24 B | 3 (`1`번 AA는 뒤가 13이라 통과) | LEN 4 ≤ 32, 21 ≥ 10, CRC `0xABE0` ≠ 받은 값 `0x09E6` | **BAD_CRC** | 3 + 1 = **4** | 4 |
| 2 | `buf+4`, 20 B | 3 (절대 7) | LEN 6, 17 ≥ 12, CRC `0x2114` == `14 21` | **FOUND** | 3 + 12 = **15** | 19 |
| 3 | `buf+19`, 5 B | 0 | 5 ≥ 4, LEN 6, 5 < 12 | **NEED_MORE** | **0** | 19 |
| – | `memmove(buf, buf+19, 5)`, `0A D0 07 00 00 49 2D` 추가 | | | | | len 12 |
| 4 | `buf+0`, 12 B | 0 | LEN 6, 12 ≥ 12, CRC `0x2D49` == `49 2D` | **FOUND** | **12** | 12 → 0 |

payload 해석: `E6 09` → `0x09E6` = 2534 → 25.34 °C, `E8 03 00 00` → 1000 ms. frame B는 `F0 0A` → 2800(28.00 °C), `D0 07 00 00` → 2000 ms.

테스트 7번 스트림(112 B): 쓰레기 + 가짜 sync, frame A, `AA 55 01 FF`(BAD_LEN), frame B, heartbeat(LEN 0), 1비트 깨진 temp 프레임, payload에 `AA 55`가 든 TYPE 2, −1.50 °C 프레임, LEN 32 프레임 → 어떻게 쪼개도 **패킷 6개, bad_crc 2, bad_len 1, 남은 바이트 0**.

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `len - i < HDR_LEN`처럼 **남은 길이와 비교**합니다. `i + HDR_LEN > len`도 같지만, 뺄셈 형태는 `i < len`이 보장된 상태라 언더플로가 없고 읽기 쉽습니다.
- LEN 검사가 프레임 길이 검사보다 **먼저**입니다. 순서가 바뀌면 가짜 `LEN = 255`에서 261바이트를 기다리며 NEED_MORE만 반복합니다. 64바이트 버퍼는 영원히 안 차고 링크가 멈춥니다 — Neros 재구성([L09](../../../../neros_tech_senior_firmware_engineer/technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html) 5.4절)에서 찾은 바로 그 데드락입니다. `_Static_assert(ACC_CAP >= MAX_FRAME)`가 나머지 절반의 보장입니다.
- CRC 필드는 `f[k] | (f[k+1] << 8)`로 조립합니다. 구조체 캐스팅이나 `*(uint16_t *)`는 정렬·엔디언·strict aliasing 문제가 있습니다.
- `int16_t` 온도는 `(int16_t)(uint16_t)(lo | hi << 8)` — 먼저 16비트 무부호로 만든 다음 부호 있는 값으로 바꿉니다(−150 테스트).
- `*out`은 FOUND일 때만 씁니다. 호출자는 status만 보고 `out`을 믿으면 됩니다.

### 4.6 복잡도

- `extract_packet` 한 번: O(len) 스캔 + O(LEN) CRC. 추가 메모리 O(1), `buf`는 읽기만 합니다.
- 스트림 전체: 정상 바이트는 한 번씩. 가짜 sync마다 최대 38바이트 CRC를 다시 볼 수 있어 최악 O(n × MAX_FRAME). 실제로는 노이즈 비율만큼만 늘어납니다.
- 메모리: 누적 버퍼 64 B + `Packet` 34 B. 동적 할당 없음.

---

## 5. 임베디드 관점으로 말할 것

- **처음 5분이 결과를 정합니다**: Neros에서는 C로 시간이 모자랐고, 온사이트 Python 변형은 문제를 이해하는 데 시간이 걸렸습니다. 둘 다 "코드 실력"이 아니라 **return 경우와 consumed 규칙을 먼저 합의하지 않아서** 생긴 일입니다. 예제 하나를 면접관과 손으로 만들고 표(§1)를 먼저 적으면, 변형이 와도 표의 칸만 바뀝니다.
- **버퍼 방식 vs 바이트 상태 머신**: 이 문제는 DMA나 idle-line 인터럽트로 바이트가 **덩어리로** 들어올 때 자연스럽습니다. ISR이 1바이트씩 주면 [T2-2 상태 머신](../T2-2_packet_parser/index.html)이 더 쌉니다(재스캔 없음). 대신 T2-2 방식은 bad CRC 뒤 "1바이트만 넘기고 재스캔"이 어렵습니다 — 이 문제는 버퍼가 있으니 쉽습니다. 트레이드오프를 한 문장으로 말하면 좋습니다.
- **선형 버퍼 + memmove**: 링버퍼보다 단순하고 프레임이 항상 연속이라 CRC를 포인터 하나로 계산합니다. 비용은 NEED_MORE마다 최대 37바이트 복사 — 115200 baud에서 무시할 수준입니다.
- **진단 카운터**: `bad_crc`, `bad_len`은 현장에서 "노이즈인가, baud 불일치인가, 프로토콜 버전이 다른가"를 가르는 첫 단서입니다.
- **주어진 라이브러리부터 검증**: 테스트 0번처럼 `crc16("123456789") == 0x29B1`을 먼저 확인하면, 이후 실패가 내 코드 탓인지 바로 압니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| A false sync with a huge LEN arrives and the line goes quiet. What happens? | LEN ≤ 32라 최대 38바이트만 기다리지만, 그 동안 뒤의 진짜 프레임이 늦어집니다. **inter-byte timeout**(예: 마지막 바이트 후 5 ms)이 지나면 sync 1바이트를 버리고 재스캔합니다. wrap-safe `now - last >= timeout`. |
| Why not use a ring buffer instead of memmove? | 가능합니다. 그러면 프레임이 wrap 지점에 걸칠 수 있어 `rb_peek(offset)`으로 읽고 CRC도 바이트 단위로 누적해야 합니다(Neros Task 1+2 조합). 코드가 길어지는 대신 복사가 없습니다. |
| The payload has its own fields. How do you decode them portably? | 시프트와 OR로 조립(`decode_temp`). `#pragma pack` 구조체 캐스팅은 정렬·엔디언 때문에 MCU마다 깨질 수 있습니다. |
| How would you test this on real hardware? | 같은 스트림을 랜덤 조각으로 나눠 넣는 테스트(7번)를 UART 루프백으로: 프레임 + 랜덤 노이즈 + 비트 오류 주입, 카운터와 받은 시퀀스 번호로 손실 확인. |
| Throughput: how many frames per second at 115200 baud? | 8N1이면 바이트당 10비트 → 11520 B/s. 12바이트 temp 프레임이면 약 960 frames/s. |

## 7. 자주 하는 실수

- BAD_CRC에서 **프레임 전체를 건너뜀** → 테스트 1번 call 1(`used == 4`)과 call 2 실패
- NEED_MORE에서 sync까지 버리거나(`i + 1`) 아무것도 안 버림 → 테스트 2번 실패. 쓰레기가 영원히 버퍼에 쌓이면 64바이트가 차서 링크가 멈춥니다
- 마지막 바이트 `AA` 하나를 쓰레기로 버림 → 다음 조각이 `55`로 시작하면 프레임 손실(테스트 2번 `h3`, 7번 랜덤 조각)
- LEN 검사 전에 프레임 길이를 계산·대기 → 가짜 LEN에서 데드락(Neros L09 버그)
- CRC 범위에 sync를 포함하거나 CRC 바이트 순서를 big-endian으로 읽음 → 모든 FOUND 실패. 질문 1번으로 먼저 확인
- 드라이버에서 `memmove` 길이를 `len`으로 씀(`len - pos`가 맞음) → ASan이 버퍼 오버런으로 잡음

## 8. Variant cards (변형 문제)

면접관이 형식을 조금 바꿔도 §1 표의 **칸만** 바뀝니다. 아래 카드를 보고 "어느 칸이 바뀌는가"를 30초 안에 말하는 연습을 하세요.

| Variant (English prompt) | 답 힌트 |
|---|---|
| "Frames are COBS-encoded and terminated by `0x00`. Extract one frame." | sync 검색 대신 **`0x00` 검색**. 없으면 NEED_MORE(consumed 0 — 전부 남김), 있으면 그 앞까지 COBS 디코드 → CRC 확인, consumed = 0x00 위치 + 1. 디코드 실패·CRC 실패도 그 프레임 전체를 버려도 됩니다(구분자가 확실하니 재스캔 불필요). SLIP/HDLC(`0x7E`, escape `0x7D ^ 0x20`)도 같은 구조. |
| "`LEN` now includes the two CRC bytes (LEN = payload + 2)." | 프레임 길이 공식만 바뀝니다: `frame = 4 + LEN`, payload 길이 = `LEN − 2`, CRC 범위 = TYPE, LEN, payload. 검사가 두 개로 늘어납니다: `LEN < 2`(CRC도 못 담음) 또는 `LEN > 34`면 BAD_LEN. 예제를 손으로 하나 만들어 공식을 확인하고 시작하세요 — 여기서 off-by-2가 제일 흔합니다. |
| "All multi-byte fields, including the CRC, are big-endian." | `rx_crc = f[k] << 8 | f[k+1]`, 온도 `(int16_t)(uint16_t)(b[0] << 8 | b[1])`. 로직은 그대로. 면접관에게 "CRC도 big-endian인가요?"를 꼭 따로 물어보세요 — 필드와 CRC가 다른 경우가 흔합니다. |
| "Now bytes arrive one at a time from the RX ISR. No buffer." | 바이트 상태 머신으로 바꿉니다 → [T2-2](../T2-2_packet_parser/index.html). 상태: SYNC0 → SYNC1 → TYPE → LEN → PAYLOAD → CRC_LO → CRC_HI. CRC는 받으면서 누적. 실패 시 1바이트 재스캔이 필요하면 받은 바이트를 작은 버퍼에 남겨 두고 이 문제의 `extract_packet`으로 되돌아오면 됩니다. |
