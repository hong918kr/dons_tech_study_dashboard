---
id: DS-M10
time: 90분
updated: 2026-11-07
---
# DS-M10 가변 길이 레코드 로그 링 버퍼

## 학습 목표

- `[len|type|timestamp|payload]` 가변 길이 레코드를 **바이트 링** 위에 쌓고, 공간이 모자라면 **가장 오래된 레코드를 통째로** 버리는 로거(flight recorder)를 malloc 없이 구현한다.
- 레코드가 링 끝에서 **쪼개져 이어지는(split)** 방식과 **패딩으로 건너뛰는(skip-record)** 방식을 둘 다 구현해 보고, 보존 레코드 수·낭비 바이트를 실측으로 비교한다.
- 헤더를 **바이트 단위로 직렬화**해야 하는 이유(정렬, strict aliasing, 구조체 패딩, 엔디언, 쪼개진 헤더)를 C 표준 조항과 Cortex-M 동작으로 설명한다.
- 리셋을 넘어 살아남는 **noinit RAM 크래시 트레이스**를 만들고, 신뢰할 수 없는 메모리를 안전하게 걸어 warm/cold 를 판정하는 검증기와 호스트 쪽 **덤프 파서**를 작성한다.
- Linux ftrace 링 버퍼·printk 링 버퍼가 같은 문제를 어떻게 풀었는지 한 줄씩 비교할 수 있다.

## 1. 문제

필드에서 죽은 보드의 "죽기 직전 0.5초" 를 보여 주는 것이 **트레이스 버퍼**다. 펌웨어 면접에서 "가변 길이 로그를 링 버퍼에 어떻게 넣나요? 꽉 차면요? 리셋 뒤에도 읽을 수 있나요?" 는 DS-B03/B04 의 바이트 링 버퍼를 **레코드 단위**로 끌어올리는 단골 꼬리질문이다.

DS-B04 의 바이트 링(크기 2^n, `uint32_t` 자유 증가 head/tail, `& MASK` 인덱스)을 그대로 쓴다. 달라지는 점은 단 하나 — **원소가 1바이트가 아니라 길이가 제각각인 레코드**라는 것. 그래서 세 가지 문제가 새로 생긴다.

1. 레코드가 링 끝에 걸리면? → 이 레슨은 **쪼개서 앞으로 이어 쓴다** (split). 패딩 방식은 연습 1에서 만들어 비교한다.
2. 꽉 차면? → 바이트 단위가 아니라 **레코드 단위로** 가장 오래된 것부터 버린다. 반쯤 남은 레코드는 쓰레기이기 때문이다. 그래서 `tail` 은 언제나 **레코드 시작**에 있다.
3. 헤더(len, type, ts)는 어떻게 읽나? → 아무 바이트 위치에서 시작하고, 끝에서 둘로 쪼개질 수도 있다. 구조체 포인터로 캐스팅할 수 없다 → **바이트 단위 직렬화**(little-endian 고정).

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">레코드 = 7바이트 헤더 + payload (패딩 없음, 모든 필드 little-endian)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="62" height="32" class="f-acc"/><rect x="82" y="40" width="62" height="32" class="f-acc"/>
    <rect x="144" y="40" width="62" height="32" class="f-ok"/>
    <rect x="206" y="40" width="62" height="32" class="f-mut"/><rect x="268" y="40" width="62" height="32" class="f-mut"/>
    <rect x="330" y="40" width="62" height="32" class="f-mut"/><rect x="392" y="40" width="62" height="32" class="f-mut"/>
    <rect x="454" y="40" width="250" height="32"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="51" y="61">len lo</text><text x="113" y="61">len hi</text><text x="175" y="61">type</text>
    <text x="237" y="61">ts b0</text><text x="299" y="61">ts b1</text><text x="361" y="61">ts b2</text><text x="423" y="61">ts b3</text>
    <text x="579" y="61">payload (len 바이트)</text>
    <text x="51" y="88">+0</text><text x="113" y="88">+1</text><text x="175" y="88">+2</text><text x="237" y="88">+3</text>
    <text x="299" y="88">+4</text><text x="361" y="88">+5</text><text x="423" y="88">+6</text><text x="470" y="88">+7</text>
  </g>
  <text x="10" y="122" fill="currentColor" font-weight="bold">LOG_SIZE = 256 바이트 링 (칸 = 카운터 &amp; 255)</text>
  <g stroke="currentColor">
    <rect x="20" y="140" width="64" height="40" class="f-acc"/>
    <rect x="84" y="140" width="100" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="184" y="140" width="88" height="40" class="f-mut"/>
    <rect x="272" y="140" width="158" height="40" class="f-mut"/>
    <rect x="430" y="140" width="137" height="40" class="f-mut"/>
    <rect x="567" y="140" width="153" height="40" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="52" y="165">D 뒷부분</text><text x="134" y="165">빈 공간</text>
    <text x="228" y="165">A</text><text x="351" y="165">B</text><text x="498" y="165">C</text>
    <text x="643" y="165">D 앞부분</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="20" y="198">0</text><text x="84" y="198">23</text><text x="184" y="198">60</text>
    <text x="567" y="198">200</text><text x="716" y="198">255</text>
  </g>
  <path d="M705 140 C705 112, 40 112, 40 136" stroke="currentColor" fill="none"/>
  <path d="M40 136 l-4 -8 M40 136 l4 -8" stroke="currentColor" fill="none"/>
  <g fill="currentColor">
    <text x="300" y="108">D 는 255 다음 0 으로 이어진다 (split)</text>
    <text x="84" y="220">↑ head (다음 레코드 시작)</text>
    <text x="184" y="240">↑ tail (가장 오래된 레코드 A 의 시작 — 항상 레코드 경계)</text>
    <text x="10" y="270">head − tail = 사용 바이트 (DS-B04). 공간이 모자라면 A, B, … 순서로 레코드 통째로 버린다.</text>
  </g>
</svg>
```

```c
#define LOG_SIZE 256u            /* 2^n */
#define LOG_HDR  7u              /* len(2) + type(1) + ts(4) */
typedef struct { uint16_t len; uint8_t type; uint32_t ts; } rec_hdr_t;   /* CPU 쪽 표현 */
typedef struct {
    uint32_t head, tail;         /* 자유 증가 바이트 카운터 (DS-B04) */
    uint32_t nrec, dropped;      /* 들어 있는 레코드 수, 덮어써서 버린 누적 수 */
    uint8_t  data[LOG_SIZE];
} tlog_t;
```

### Part A — 바이트 직렬화와 쪼개진 복사, append/pop

- `put_le16/put_le32/get_le16/get_le32` — 시프트로 LE 바이트 변환. 포인터 캐스팅 금지.
- `hdr_encode(out[7], h)` / `hdr_decode(in[7], h)` — 위 그림의 배치 그대로.
- `ring_put(data, pos, src, n)` / `ring_get(data, pos, dst, n)` — 카운터 `pos` 부터 n 바이트, 끝에서 잘리면 **memcpy 2번** (DS-B04 Part C 와 같은 모양).
- `log_append(l, type, ts, payload, len)` — 헤더 7바이트를 로컬 배열에 인코딩한 뒤 `ring_put`, 이어서 payload. **head 는 마지막에 한 번** 전진. `len > LOG_MAX_PAYLOAD (249)` 면 false.
- `log_pop(l, &h, payload, cap)` — 가장 오래된 레코드를 꺼낸다. 비었으면 -1, `len > cap` 이면 **소비하지 않고** -2.
- 테스트: 헤더 바이트 배치 `02 01 AB 44 33 22 11`, 홀수 주소 직렬화, 헤더가 끝에 0~7바이트만 걸치는 **모든 경우**(시작 카운터 = 256 − k).

### Part B — 가장 오래된 레코드를 통째로 버리기

- `log_drop_oldest(l)` — tail 의 헤더(이것도 쪼개져 있을 수 있다)에서 len 을 읽어 tail 을 레코드 하나만큼 전진. `nrec--`, `dropped++`.
- `log_make_room(l, need)` — 빈 공간 `LOG_SIZE − used` 가 need 이상이 될 때까지 drop. `log_append` 가 **크기 검사를 통과한 뒤에만** 부른다.
- 경계: 딱 맞게 가득(used == 256), 1개만 버리면 되는 경우, 여러 개 버려야 하는 경우, 링 전체 크기 레코드(need == 256 → 전부 버림), 너무 큰 레코드는 **아무것도 버리지 않고** 거절, 길이 0 payload.
- 하네스의 랜덤 테스트가 시작 카운터 4곳(2^32 경계 근처 포함)에서 20,000 연산씩 **레코드 큐 참조 모델**과 매 연산 뒤 전체 내용을 대조한다.

### Part C — noinit 크래시 트레이스와 덤프 파서

- `log_walk(data, head, tail, cb, ctx, &ok)` — **믿을 수 없는** head/tail/data 를 tail 부터 걸으며 구조를 검증한다. head 에 정확히 도착해야 ok. 망가진 곳에서 멈추되 그 앞의 검증된 레코드는 cb 로 넘긴다. 어떤 입력에도 링 밖을 읽거나 무한 루프하면 안 된다.
- `trace_attach(nv)` — 부팅마다 호출. `magic` 이 맞고 walk 가 ok 면 **warm**(로그 유지, `boot++`, nrec 재계산), 아니면 **cold**(초기화, magic 은 마지막에 기록).
- `dump_parse(img, n, cb, ctx, &ok)` — 호스트 PC 에서 바이트 이미지(`[magic|boot|head|tail|dropped|data]`, 모두 LE)를 파싱. `trace_save`(주어짐)의 거울상.
- 시나리오: 센서 로그 40개 → `"i2c timeout"` → HardFault 핸들러가 `TR_FAULT{pc, lr}` 기록 → 리셋 → attach 가 warm 판정 → BOOT 레코드 추가 → 덤프에 FAULT 가 보여야 한다. 쓰레기 RAM 2,000가지와 magic 만 맞는 쓰레기 20,000가지에서 attach 가 안전해야 한다.

## 2. Problem (English)

> **Variable-length record trace log on a byte ring.**
>
> A power-of-two byte ring (`LOG_SIZE = 256`) with free-running `uint32_t head/tail` byte counters stores records `[len:u16][type:u8][ts:u32][payload: len bytes]`, all little-endian, 7-byte header, no padding. Records may wrap around the end of the array (split records). No dynamic allocation.
>
> 1. Implement byte-wise LE serialization and `ring_put/ring_get` (at most two `memcpy`s). Implement `bool log_append(tlog_t*, uint8_t type, uint32_t ts, const uint8_t *payload, uint16_t len)` and `int32_t log_pop(tlog_t*, rec_hdr_t*, uint8_t *payload, uint32_t cap)` (returns length, -1 empty, -2 buffer too small without consuming). Do not cast the byte buffer to a header struct — explain why.
> 2. When there is not enough room, drop the **oldest whole records** until the new one fits. Reject records larger than the ring without touching existing data. `tail` must always point at a record boundary.
> 3. The log lives in `.noinit` RAM. Write `log_walk` that validates untrusted `head/tail/data` (must terminate, never read out of bounds, report how many records are valid), `trace_attach` that decides warm vs cold boot, and a host-side `dump_parse` for a serialized image.
>
> Example: ring offset 253, append `{type 0x43, ts 0xCAFE0003, 5 bytes}` → header bytes at offsets 253, 254, 255, 0, 1, 2, 3; payload at 4..8; `pop` returns the same record.
>
> Follow-ups: split vs padding? Worst-case cost of `append`? ISR-safe? How do ftrace and printk handle this?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c && ./a.out
# 처음엔 "6/37 passed" (exit 1). Part A → B → C 순서로 TODO 를 채운다
```

## 4. 힌트

<details>
<summary>힌트 1 — 헤더는 "작은 로컬 배열" 을 거친다</summary>

헤더가 링 끝에서 쪼개질 수 있으니 링 안에서 직접 해석하지 않는다. 쓸 때는 `uint8_t hb[7]` 에 `hdr_encode` 한 뒤 `ring_put(data, head, hb, 7)`, 읽을 때는 `ring_get(data, tail, hb, 7)` 후 `hdr_decode`. 7바이트 복사는 공짜에 가깝고, 쪼개짐 처리가 `ring_put/get` **한 곳**에만 남는다.

</details>

<details>
<summary>힌트 2 — get_le32 의 함정</summary>

`p[3] << 24` 에서 `p[3]` 은 `int` 로 승격된다. `p[3] >= 0x80` 이면 결과가 `int` 범위를 넘어 **UB**. 각 바이트를 `(uint32_t)` 로 바꾼 **다음에** 시프트한다.

</details>

<details>
<summary>힌트 3 — append 의 순서</summary>

① 크기 검사(너무 크면 바로 false — 아무것도 버리지 않는다) ② `log_make_room(need)` ③ 헤더·payload 쓰기 ④ `head += need; nrec++`. head 를 먼저 올리면 쓰는 도중 리셋·인터럽트가 반쯤 쓴 레코드를 보게 된다.

</details>

<details>
<summary>힌트 4 — make_room 의 루프 횟수</summary>

`while (nrec != 0 && LOG_SIZE - used < need) drop_oldest();`. need ≤ 256 이 보장되면 nrec 이 0 이 될 때 빈 공간은 256 이라 반드시 멈춘다. `nrec != 0` 은 혹시 모를 손상에서 무한 루프를 막는 안전장치.

</details>

<details>
<summary>힌트 5 — 믿을 수 없는 메모리 걷기</summary>

① `head - tail > LOG_SIZE` 면 즉시 실패. ② `remain = head - pos` 가 7 미만이면 실패 — **먼저** 검사해야 `remain - 7` 이 unsigned 언더플로하지 않는다. ③ `len > remain - 7` 이면 실패. 매 걸음 pos 가 7 이상 전진하고 remain 이 줄어드니 최대 256/7 걸음 안에 끝난다. 인덱스는 언제나 `& MASK` 라 링 밖은 읽을 수 없다.

</details>

## 5. 풀이

### Part A — 직렬화 + 쪼개진 복사

**왜 바이트 단위인가.** `*(uint32_t *)(data + pos + 3)` 처럼 읽고 싶어지지만 네 가지 이유로 안 된다.

| 이유 | 내용 |
|---|---|
| 쪼개짐 | 헤더가 offset 253 에서 시작하면 바이트가 `253, 254, 255, 0, 1, 2, 3` 에 흩어져 있다. 연속 메모리가 아니니 어떤 캐스팅도 불가능 |
| 정렬 (C11 6.3.2.3p7) | 정렬이 맞지 않는 포인터로의 변환 자체가 UB. Cortex-M0/M0+(ARMv6-M)는 비정렬 접근이 **HardFault**, M3/M4/M7(ARMv7-M)은 `LDR/STR` 은 허용(CCR.UNALIGN_TRP = 0 일 때)이지만 `LDRD/STRD/LDM/STM` 은 항상 UsageFault — 컴파일러가 두 필드 읽기를 `LDRD` 로 합치면 터진다 |
| strict aliasing (C11 6.5p7) | `uint8_t` 배열을 `uint32_t` lvalue 로 읽는 것은 허용 목록에 없다 (char 예외는 반대 방향만) |
| 레이아웃·엔디언 | `struct {u16; u8; u32;}` 는 패딩 때문에 7 이 아니라 **8** 바이트. 덤프를 읽는 호스트 컴파일러·엔디언이 달라도 같은 바이트를 같은 값으로 읽으려면 형식이 코드가 아니라 **명세**여야 한다 |

이 Mac 에서 직접 확인한 결과 (`demo_unaligned.c`):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=alignment demo_unaligned.c && ./a.out   # 스택 주소는 실행마다 다름
```

```text
demo_unaligned.c:12:49: runtime error: load of misaligned address 0x00016d026b83 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
0x00016d026b83: note: pointer points here
 00  05 00 02 44 33 22 11 00  00 00 00 00 00 00 00 00  00 dc 27 e7 01 00 00 00  23 00 fa d9 d3 49 1a
              ^
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior demo_unaligned.c:12:49
sizeof(struct naive_hdr) = 8 (on-wire header = 7)
offsetof len=0 type=2 ts=4
shift : 0x11223344
memcpy: 0x11223344
cast  : 0x11223344  <- 이 Mac 에선 값은 맞게 나온다 (UBSan 보고를 보라)
```

캐스팅 버전은 Apple Silicon 에서 **값이 맞게 나온다** — 그래서 위험하다. 호스트 테스트는 통과하고 Cortex-M0 보드에서만 HardFault 가 난다. 비용 걱정도 필요 없다. `cc -O2 -S` 로 본 arm64 코드는 세 함수 모두 똑같다:

| 함수 | arm64 (-O2) |
|---|---|
| `ts_cast` (UB) | `ldur w0, [x0, #3]` |
| `ts_memcpy` | `ldur w0, [x0, #3]` |
| `ts_shift` (LE 시프트) | `ldur w0, [x0, #3]` |

컴파일러는 "비정렬 접근이 되는 LE 타깃" 에서 시프트 패턴을 load 한 번으로 바꾼다. Cortex-M0 에서는 바이트 load 4번이 되지만 그게 **정답**이다. `memcpy` 는 호스트 엔디언을 따르므로 덤프 형식처럼 엔디언이 정해진 데이터에는 시프트가 낫다.

**쪼개진 헤더 추적** — 테스트의 `k = 3` 경우 (`log_init(l, 253)`, payload 5바이트):

| 단계 | 호출 | `i = pos & 255` | first | memcpy |
|---|---|---|---|---|
| 헤더 | `ring_put(data, 253, hb, 7)` | 253 | min(3, 7) = 3 | `hb[0..2] → [253..255]`, `hb[3..6] → [0..3]` |
| payload | `ring_put(data, 260, p, 5)` | 4 | min(252, 5) = 5 | `p → [4..8]`, 두 번째는 0바이트 |
| 커밋 | `head += 12` | | | head = 265 |
| pop 헤더 | `ring_get(data, 253, hb, 7)` | 253 | 3 | 거울상 |

`k = 0..7` 여덟 경우를 모두 돌리는 이유: 쪼개짐 위치가 len 필드 가운데(k=1), type 직전(k=2), ts 가운데(k=4,5,6), 헤더 직후(k=7)일 때 각각 다른 버그가 숨는다. 경계는 **전수 검사**가 싸다.

### Part B — 레코드 통째로 버리기

불변식 (하네스 `log_check` 가 매 연산 뒤 확인한다):

1. `0 ≤ head − tail ≤ LOG_SIZE` (unsigned 뺄셈, DS-B04 증명 그대로)
2. tail 에서 시작해 `7 + len` 씩 걸으면 **정확히 head 에 도착** — 즉 tail 은 항상 레코드 경계
3. 걸은 레코드 수 == `nrec`, 각 `len ≤ head − pos − 7`

`log_drop_oldest` 는 2번을 지키는 유일한 방법이다. 바이트 링(DS-B03 연습 1)처럼 1바이트씩 버리면 tail 이 레코드 한가운데에 떨어지고, 그다음 읽는 "헤더" 는 payload 바이트라 길이가 엉터리가 된다.

테스트 시나리오 추적 (`start = 0xFFFFFF80` → offset 128, 레코드 = 7 + 25 = 32 B):

| 연산 | 빈 공간 | 버린 것 | nrec | dropped | 비고 |
|---|---|---|---|---|---|
| ts 0..7 append | 256 → 0 | — | 8 | 0 | 딱 맞게 가득, head 가 2^32 를 넘음 |
| ts 8 append (need 32) | 0 → 32 | ts 0 | 8 | 1 | |
| pop | 32 | — | 7 | 1 | ts 1 이 나온다 |
| ts 9, payload 80 (need 87) | 32 → 64 → 96 | ts 2, ts 3 | 6 | 3 | 96 ≥ 87 에서 멈춤 |
| payload 250 (need 257) | — | **없음** | 6 | 3 | 거절, head 그대로 |
| ts 11, payload 249 (need 256) | → 256 | 6개 전부 | 1 | 9 | used == 256 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">append(need = 87): 빈 공간 32 → ts2 버림(64) → ts3 버림(96 ≥ 87) → 기록</text>
  <text x="10" y="58" fill="currentColor">전</text>
  <g stroke="currentColor">
    <rect x="40" y="40" width="85" height="30" class="f-mut"/><rect x="125" y="40" width="85" height="30" class="f-mut"/>
    <rect x="210" y="40" width="85" height="30" class="f-mut"/><rect x="295" y="40" width="85" height="30" class="f-mut"/>
    <rect x="380" y="40" width="85" height="30" class="f-mut"/>
    <rect x="465" y="40" width="85" height="30" fill="none" stroke-dasharray="4 3"/>
    <rect x="550" y="40" width="85" height="30" class="f-warn"/><rect x="635" y="40" width="85" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="82" y="60">ts4</text><text x="167" y="60">ts5</text><text x="252" y="60">ts6</text><text x="337" y="60">ts7</text>
    <text x="422" y="60">ts8</text><text x="507" y="60">빈 32</text><text x="592" y="60">ts2</text><text x="677" y="60">ts3</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="465" y="88">↑ head (160)</text><text x="550" y="104">↑ tail (192) — 가장 오래됨</text>
  </g>
  <text x="10" y="158" fill="currentColor">후</text>
  <g stroke="currentColor">
    <rect x="40" y="140" width="85" height="30" class="f-mut"/><rect x="125" y="140" width="85" height="30" class="f-mut"/>
    <rect x="210" y="140" width="85" height="30" class="f-mut"/><rect x="295" y="140" width="85" height="30" class="f-mut"/>
    <rect x="380" y="140" width="85" height="30" class="f-mut"/>
    <rect x="465" y="140" width="232" height="30" class="f-acc"/>
    <rect x="697" y="140" width="23" height="30" fill="none" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="82" y="160">ts4</text><text x="167" y="160">ts5</text><text x="252" y="160">ts6</text><text x="337" y="160">ts7</text>
    <text x="422" y="160">ts8</text><text x="581" y="160">ts9 (7 + 80 = 87 B)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="40" y="188">↑ tail (0 = ts4)</text><text x="640" y="188">↑ head (247)</text>
    <text x="10" y="218">레코드 단위로 버리므로 tail 은 ts2 중간이 아니라 ts4 의 시작으로 "점프" 한다. 남은 9 B 는 다음 레코드 몫.</text>
    <text x="10" y="240">dropped 1 → 3, nrec 7 → 6. 빈 공간이 32 + 64 = 96 ≥ 87 이 되는 첫 레코드 경계에서 멈춘다.</text>
  </g>
</svg>
```

**복잡도와 결정성.** 버리기 루프는 매번 빈 공간을 7바이트 이상 늘린다. 루프가 k 번째로 돌 때는 빈 공간 ≥ 7(k−1) 이면서 < need 이므로 **k ≤ ⌈need / 7⌉ ≤ ⌈256 / 7⌉ = 37**. 따라서 append 최악 = 헤더 디코드 37번 + 바이트 복사 256개 — 상수로 묶인다. ISR 에서 불러도 최악 지연을 계산할 수 있다는 뜻이다. 메모리 오버헤드는 레코드당 헤더 7 B 뿐(포인터·인덱스 없음, `nrec/dropped` 는 전체 8 B).

**랜덤 테스트** (시작 카운터 `0, 0xFFFFFF9C, 0xFFFFF448, 0x7FFFFFF0`, 각 20,000 연산, 시드 `0xD5A10 + s`). append 60% (길이 80% 는 0..39, 15% 는 0..249, 5% 는 250..269 = 거절), pop 40% (25% 는 cap 0..15 로 -2 유도). 매 연산 뒤 링 전체를 바이트로 다시 읽어 모델과 비교한다. 실제 출력:

```text
[B] random 4x20000 vs model: match | append 45403, reject 2320, pop 23612, dropped(last run) 5308
[B] records split at wrap: header 1044, payload-only 6736 | max records held 14
```

| 항목 | 값 | 의미 |
|---|---|---|
| append / reject / pop | 45,403 / 2,320 / 23,612 | 거절된 2,320번 모두 버퍼 무변화 |
| 헤더가 쪼개진 append | 1,044 | len·type·ts 가운데서 잘린 경우가 1천 번 넘게 섞였다 |
| payload 만 쪼개진 append | 6,736 | |
| 마지막 실행의 dropped | 5,308 | 모델의 버림 횟수와 매 연산 일치 |

### split vs 패딩(skip-record) — 왜 split 을 골랐나

다른 방법은 **레코드를 절대 쪼개지 않는 것**이다. 끝까지 남은 칸(to_end)이 레코드보다 작으면 남은 칸을 PAD 레코드로 채우고(to_end < 7 이면 아무것도 쓰지 않고 "암묵적으로" 건너뜀) 0 번 칸부터 쓴다. 연습 1 에서 실제로 만든다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 상황: head = offset 214, 새 레코드 57 B, 끝까지 42 B</text>
  <text x="10" y="58" fill="currentColor">split</text>
  <g stroke="currentColor">
    <rect x="90" y="40" width="68" height="30" class="f-acc"/>
    <rect x="158" y="40" width="250" height="30" class="f-mut"/>
    <rect x="408" y="40" width="200" height="30" class="f-mut"/>
    <rect x="608" y="40" width="112" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="124" y="60">새 뒷 15</text><text x="283" y="60">(빈 칸 → 다음 몫)</text><text x="508" y="60">레코드 1 (107)</text><text x="664" y="60">새 앞 42</text>
  </g>
  <text x="10" y="128" fill="currentColor">패딩</text>
  <g stroke="currentColor">
    <rect x="90" y="110" width="112" height="30" class="f-acc"/>
    <rect x="202" y="110" width="206" height="30" fill="none" stroke-dasharray="4 3"/>
    <rect x="408" y="110" width="200" height="30" class="f-mut"/>
    <rect x="608" y="110" width="112" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="146" y="130">새 레코드 57</text><text x="305" y="130">빈 칸</text><text x="508" y="130">레코드 1 (107)</text><text x="664" y="130">PAD 42</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="170">split: 링 바이트를 전부 레코드에 쓴다. 대신 읽을 때 복사(또는 두 조각 iovec)가 필요하다.</text>
    <text x="10" y="192">패딩: 레코드가 항상 연속 → 포인터로 바로 읽기(zero-copy)·DMA 한 번. 대신 끝의 42 B 가 낭비되고</text>
    <text x="10" y="214">"to_end + need" 만큼 비워야 하므로 레코드를 더 일찍 버린다. 이 그림은 연습 1 의 정해진 테스트 그대로다.</text>
  </g>
</svg>
```

연습 1 의 실측 (같은 길이 시퀀스를 두 로거에 20,000번 넣고 1,000번째 이후 매 append 뒤 평균, LOG_SIZE 256):

| workload | split 평균 보존 레코드 | 패딩 평균 보존 레코드 | split 사용률 | 패딩 사용률 | 패딩 평균 낭비 B | 패딩 최대 낭비 B |
|---|---|---|---|---|---|---|
| small 0..39 | 9.27 | 8.78 | 94.1% | 89.1% | 13.5 | 45 |
| mixed | 5.97 | 5.52 | 84.0% | 78.6% | 14.6 | 116 |
| large 60..180 | 1.54 | 1.45 | 72.2% | 68.7% | 4.7 | 90 |

(사용률 = 살아 있는 레코드 바이트 / 256. split 도 100% 가 아닌 이유: 레코드 단위로 버리므로 "다음 레코드보다 작은 빈 칸" 이 남는다. large 에서 패딩 평균 낭비가 작은 것은 버퍼가 자주 텅 비어 간격을 공짜로 건너뛰기 때문.)

| | split (이 레슨) | 패딩 / skip-record (연습 1) |
|---|---|---|
| 공간 | 링 전체 사용 | 랩마다 최대 (최대 레코드 − 1) B 낭비 |
| 최대 레코드 | ≤ LOG_SIZE, 언제나 | ≤ LOG_SIZE 지만 비어 있을 때만 확실 |
| 읽기 | 복사 필수(헤더 7 B 는 로컬로) | 포인터로 바로, 헤더도 연속 |
| 쓰는 쪽 분기 | `ring_put` 안의 두 조각 | to_end 비교, PAD 또는 암묵 간격, 비었으면 재정렬 |
| 읽는 쪽 규칙 | 없음 | PAD·암묵 간격 건너뛰기를 **양쪽이 똑같이** 알아야 함 |
| 실제 예 | 이 레슨, 많은 MCU 트레이스 로거 | ftrace(페이지 끝 PADDING 이벤트), printk(데이터 블록은 랩하지 않음) |

**결론**: 작은 RAM(수 KB)에서 "최대한 많은 이력" 이 목적인 크래시 트레이스는 split. 레코드를 그대로 DMA 로 보내거나 다른 코드에 `const uint8_t *` 로 빌려줘야 하면 패딩(DS-A02 의 bip buffer 가 이 방향의 끝판).

### Part C — noinit 크래시 트레이스

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">리셋을 넘어 살아남는 로그: 부팅 경로</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="130" height="44"/><rect x="170" y="40" width="190" height="44"/>
    <rect x="390" y="40" width="150" height="44"/><rect x="570" y="40" width="160" height="44"/>
  </g>
  <rect x="390" y="150" width="150" height="44" class="f-ok" stroke="currentColor"/>
  <rect x="570" y="150" width="160" height="44" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="60">리셋</text><text x="75" y="77" font-size="12">WDT·Fault·SW</text>
    <text x="265" y="60">startup: .data 복사</text><text x="265" y="77" font-size="12">.bss = 0, .noinit 손대지 않음</text>
    <text x="465" y="60">magic 맞나?</text><text x="465" y="77" font-size="12">log_walk ok?</text>
    <text x="650" y="60">아니오 → cold</text><text x="650" y="77" font-size="12">log_init, magic 마지막</text>
    <text x="465" y="170">예 → warm</text><text x="465" y="187" font-size="12">nrec 재계산, boot++</text>
    <text x="650" y="170">BOOT 레코드 append</text><text x="650" y="187" font-size="12">(reset reason payload)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M140 62 L165 62"/><path d="M165 62 l-7 -4 M165 62 l-7 4"/>
    <path d="M360 62 L385 62"/><path d="M385 62 l-7 -4 M385 62 l-7 4"/>
    <path d="M540 62 L565 62"/><path d="M565 62 l-7 -4 M565 62 l-7 4"/>
    <path d="M465 84 L465 145"/><path d="M465 145 l-4 -7 M465 145 l4 -7"/>
    <path d="M540 172 L565 172"/><path d="M565 172 l-7 -4 M565 172 l-7 4"/>
    <path d="M650 84 L650 145"/><path d="M650 145 l-4 -7 M650 145 l4 -7"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="222" font-family="monospace">링커: .noinit (NOLOAD) : { *(.noinit*) } &gt; RAM</text>
    <text x="10" y="242" font-family="monospace">변수: __attribute__((section(".noinit"))) trace_nv_t g_trace;</text>
    <text x="10" y="264">전원 투입 직후 SRAM 은 쓰레기 → magic 만으로는 부족, 구조 검증(walk)까지 해야 안전하다.</text>
  </g>
</svg>
```

**왜 nrec 을 다시 세나.** 리셋 직전에 무엇이 반쯤 쓰였는지 모른다. head/tail 은 "커밋 순서" 덕에 일관성이 있지만(아래), 파생 값인 `nrec` 은 walk 결과로 다시 만드는 것이 가장 싸고 확실하다. 같은 이유로 덤프 이미지에도 nrec 은 넣지 않았다.

**커밋 순서 = 크래시 일관성.** append 는 ① 오래된 레코드 버리기(tail 전진, 한 레코드씩) ② 바이트 쓰기 ③ `head += need` 순서다. 어느 지점에서 리셋되든 [tail, head) 는 완전한 레코드들이다 — ② 도중이면 반쯤 쓴 바이트는 head 바깥이라 보이지 않는다. 단, 이것은 **컴파일러와 CPU 가 저장 순서를 지킬 때** 얘기다. 워치독 리셋처럼 비동기로 끊길 수 있다면 head 저장 앞에 최소한 컴파일러 배리어(`atomic_signal_fence(memory_order_release)` 또는 `__asm volatile("" ::: "memory")`)를 둔다. Cortex-M7 처럼 D-cache 가 write-back 이면 dirty 라인은 리셋 때 RAM 에 써지지 않고 사라질 수 있으니 noinit 영역을 DTCM 이나 MPU non-cacheable 영역에 두거나, fault 핸들러에서 리셋 전에 `SCB_CleanDCache()` 를 부른다.

**walk 가 끝나는 이유 (종료 증명).** 매 걸음 `pos` 는 `7 + len ≥ 7` 전진하고, `len ≤ remain − 7` 이므로 head 를 넘지 못한다. remain 은 처음 ≤ 256 이고 걸음마다 7 이상 줄어드니 최대 36걸음. 모든 읽기는 `& MASK` 인덱스라 data[256] 밖은 읽지 않는다 — ASan 아래 쓰레기 22,000가지 입력으로 확인했다.

```text
[C] garbage with valid magic: accepted 460 / 20000 (non-empty 0), accepted all valid: yes
[C] dump: valid, boot=1, records=22, dropped=22, last 4:
    t=400   SENSOR len=4 : 0f 04 1c 00
    t=410   MSG    len=11: 69 32 63 20 74 69 6d 65 6f 75 74
    t=411   FAULT  len=8 : 2c 1a 00 08 11 1f 00 08
    t=0     BOOT   len=1 : 08
```

- magic 이 맞고 head/tail/data 가 무작위인 20,000 경우 중 460 번을 받아들였는데 **모두 빈 로그(head == tail)** 였다. 무작위 바이트가 "len 필드들이 정확히 head 에 맞아떨어지는" 사슬을 만들 확률은 사실상 0 — 구조 검증은 쓰레기 RAM 에 강하다.
- 하지만 구조 검증은 **len 만** 본다. ts·type·payload 의 비트 오류는 못 잡는다 → 연습 4 에서 레코드 CRC-8 을 붙여 실측한다.
- 덤프의 FAULT payload `2c 1a 00 08 | 11 1f 00 08` 은 LE 로 PC = `0x08001A2C`, LR = `0x08001F11` (Thumb 비트 1). 호스트 파서는 타깃 구조체 정의 없이 명세만으로 읽는다.
- 손상 테스트: 두 번째 레코드의 len 상위 바이트를 0xFF 로 바꾸면 dump_parse 는 첫 레코드 1개만 넘기고 `ok = false`, 같은 손상이 noinit 에 있으면 attach 는 cold 로 판정해 새로 시작한다. 잘린 이미지·틀린 magic·`head − tail > 256` 도 모두 거절.

**Linux 와 비교 (한 줄씩).**

- **ftrace** (`kernel/trace/ring_buffer.c`): CPU 마다 링, 링은 페이지(sub-buffer)들의 리스트. 이벤트는 페이지 경계를 넘지 않고 남은 칸은 `RINGBUF_TYPE_PADDING` 이벤트로 채운다 → 패딩 방식. 헤더는 `u32 type_len:5, time_delta:27` 4바이트, 타임스탬프는 **델타**(넘치면 `TIME_EXTEND`). overwrite 모드에서는 이벤트가 아니라 **페이지 단위**로 오래된 것을 버리고, 잃은 이벤트 수를 `overrun` 통계로 센다.
- **printk** (5.10+, `kernel/printk/printk_ringbuffer.c`): 디스크립터 링 + 텍스트 데이터 링, 여러 CPU 가 락 없이 쓴다. 데이터 블록은 랩하지 않고 끝에 안 맞으면 링 처음에 놓는다 → 역시 패딩 방식. 레코드마다 **시퀀스 번호**가 있어 느린 리더가 덮어쓰인 레코드를 알아챈다(연습 2). 5.10 이전 printk 도 `len == 0` 헤더를 "링 끝, 처음으로" 표시로 쓰는 패딩 방식이었다.
- **pstore/ramoops**: 예약된 RAM 영역에 커널 로그·oops 를 남겨 warm reboot 뒤 `/sys/fs/pstore` 로 읽는다 — 이 레슨 Part C 의 리눅스판.

커널이 패딩을 택한 이유는 페이지 단위 splice(제로카피로 파일에 쓰기)·락 없는 다중 생산자 때문이고, RAM 이 수 KB 인 MCU 크래시 로그는 이력 길이가 더 중요해서 split 이 흔하다. 정답은 "요구사항에 따라" 이고, 그 근거를 숫자로 대는 것이 면접 포인트다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
# [B] random 4x20000 vs model: match | append 45403, reject 2320, pop 23612, dropped(last run) 5308
# ...
# 37/37 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c && ./a.out   # 같은 출력, 리포트 없음
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=alignment demo_unaligned.c && ./a.out   # 캐스팅 UB 데모
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| tail 의 헤더를 `data[tail & MASK]`, `data[(tail & MASK) + 1]` … 로 직접 읽기 | 헤더가 쪼개져 있으면 `data[256]` 이상을 읽는다 — 링 밖 접근(ASan 이 잡는다) + 엉터리 len | `ring_get(data, tail, hb, 7)` 후 `hdr_decode` |
| `*(uint32_t *)(rec + 3)` 로 ts 읽기, 헤더 구조체 캐스팅 | 비정렬 UB(6.3.2.3p7), aliasing UB(6.5p7), 구조체는 8 B, M0 에서 HardFault. 호스트에선 멀쩡해 보여 늦게 터진다 | 시프트 기반 `get_le32` (비용 0, 위 표) |
| `get_le32` 에서 `p[3] << 24` | `p[3]` 이 int 로 승격, 0x80 이상이면 부호 있는 오버플로 UB | `(uint32_t)p[3] << 24` |
| 너무 큰 레코드인데 `make_room` 부터 호출 | 로그를 다 지운 뒤 거절 — 크래시 직전 이력을 스스로 날린다 | 크기 검사 → make_room → 쓰기 |
| 공간이 모자라면 **바이트 단위로** tail 전진 (DS-B03 overwrite 처럼) | tail 이 레코드 중간에 떨어져 다음 "헤더" 가 payload 바이트 | `tail += 7 + len` (레코드 통째로) |
| `head += need` 를 바이트 쓰기 전에 | 리셋·인터럽트가 반쯤 쓴 레코드를 완전한 것으로 본다 | 커밋(head 갱신)은 마지막에 한 번 |
| walk 에서 `len > remain - 7` 만 검사 | remain < 7 이면 `remain - 7` 이 unsigned 언더플로 → 거대한 값 → 아무 len 이나 통과 | `if (remain < LOG_HDR) fail;` 을 먼저 |
| 리셋 뒤 저장된 `nrec`, head/tail 을 그대로 신뢰 | 전원 투입 쓰레기에서 len 을 따라가다 무한 루프·링 밖 접근 | magic + `log_walk` 로 검증, nrec 은 다시 센다 |
| `ring_put(data, pos, NULL, 0)` 에서 그대로 memcpy | C11/C17 은 길이 0 이어도 포인터가 유효해야 한다 (7.24.1p2) — `memcpy(_, NULL, 0)` 은 UB | `if (n == 0) return;` |

## 8. 면접 Q&A

<details>
<summary>Q1. 레코드가 링 끝에 걸리면 쪼개나요, 건너뛰나요? 각각 언제 고르나요?</summary>

split 은 공간을 전부 쓰고 최대 레코드 크기가 언제나 링 크기까지다. 대신 읽는 쪽이 복사(또는 두 조각 iovec)를 해야 한다. 패딩은 레코드가 항상 연속이라 포인터로 빌려주거나 DMA 한 번에 보낼 수 있지만 랩마다 최대 (최대 레코드 − 1) B 를 버리고 레코드를 일찍 버린다. 이 레슨 실측(256 B 링): 작은 레코드에서 보존 레코드 9.27 vs 8.78, mixed 에서 5.97 vs 5.52. 꼬리질문 "ftrace 는?" → 페이지 끝을 PADDING 이벤트로 채운다. 페이지 단위 splice 와 per-CPU 락 없는 쓰기를 위해서다.

</details>

<details>
<summary>Q2. 헤더를 구조체로 정의하고 캐스팅하면 안 되나요? __attribute__((packed)) 는요?</summary>

안 된다: (1) 헤더가 끝에서 쪼개지면 연속 메모리가 아니다 (2) 비정렬 포인터 변환 UB, ARMv6-M 은 비정렬 접근이 HardFault, ARMv7-M 도 LDRD/LDM 은 fault (3) uint8_t 배열을 uint32_t 로 읽는 것은 strict aliasing 위반 (4) 패딩·엔디언이 컴파일러/타깃마다 다르다. `packed` 는 비표준이고, 컴파일러가 결국 바이트 접근 코드를 만들며, packed 멤버의 주소를 넘기면 정렬을 잃은 포인터가 퍼진다(clang `-Waddress-of-packed-member`). 시프트 기반 LE 직렬화는 이식성 있고, arm64 `-O2` 에서는 캐스팅과 똑같은 `ldur` 한 개로 컴파일됐다.

</details>

<details>
<summary>Q3. append 의 최악 시간은? ISR 과 태스크가 모두 로그를 쓰면?</summary>

버리기 루프 ≤ ⌈need/7⌉ ≤ 37 번 + 복사 ≤ 256 B → 상수 상한. 그래서 append 전체를 짧은 임계 구역(PRIMASK, 또는 FreeRTOS `taskENTER_CRITICAL_FROM_ISR` 류의 BASEPRI)으로 감쌀 수 있다. 생산자가 tail 까지 움직이므로(덮어쓰기) DS-B04 의 "head 는 생산자, tail 은 소비자" SPSC 분리가 깨진다 — 덮어쓰기 로그가 락 없이 만들기 어려운 이유. 인터럽트 지연이 문제면 (a) 레코드를 작게 (b) CPU/우선순위별 링을 따로 두고 덤프 때 ts 로 병합(ftrace 의 per-CPU 방식) (c) 예약(reserve) → 쓰기 → 커밋 단계로 나눠 예약만 임계 구역에 넣는다(printk 링 버퍼, DS-A02 bip buffer).

</details>

<details>
<summary>Q4. 느린 리더가 읽는 도중 그 레코드가 덮어써지면 어떻게 알죠?</summary>

레코드마다 단조 증가 시퀀스 번호를 매긴다(연습 2: `first_seq = appended − nrec`). 리더 커서가 `seq < first_seq` 면 그 사이 `first_seq − seq` 개를 잃은 것이다. 리눅스 printk 도 같은 생각이다 — 콘솔 출력이 밀리면 `** N printk messages dropped **` 를 찍고, `/dev/kmsg` 리더는 자기 seq 가 덮어써졌으면 `EPIPE` 를 받는다. 복사 도중 덮어쓰기(찢어진 레코드)는 복사 **후에** first_seq 를 다시 확인해서 커서가 여전히 유효할 때만 결과를 믿는다(seqlock 패턴). 단일 코어 MCU 라면 레코드 하나 복사를 임계 구역에 넣는 것이 가장 간단하다.

</details>

<details>
<summary>Q5. noinit RAM 로그가 유효한지 부팅 때 어떻게 판단하나요? 함정은?</summary>

magic 만으로는 부족하다(전원 투입 쓰레기가 우연히 맞을 수도, 리셋 직전에 반쯤 쓰였을 수도). magic + 구조 walk(경계가 head 에 정확히 도착) + 필요하면 레코드 CRC. 함정: (1) 링커 스크립트에서 NOLOAD 를 안 하면 이미지에 포함되거나 startup 이 지운다 (2) ROM 부트로더가 쓰는 RAM 영역과 겹치면 덮인다 (3) Cortex-M7 write-back D-cache 의 dirty 라인은 리셋 때 사라질 수 있다 → non-cacheable/DTCM 또는 리셋 전 clean (4) 저전력 모드에서 SRAM 이 꺼지는 칩이 있다 (5) ECC SRAM 은 초기화 안 된 영역을 읽을 때 ECC 오류를 낼 수 있어, 리셋 원인 레지스터(STM32F4 의 `RCC_CSR`: PORRSTF, IWDGRSTF, SFTRSTF …)로 power-on 을 먼저 거르는 설계가 많다. 리눅스의 같은 기능이 pstore/ramoops.

</details>

<details>
<summary>Q6. 타임스탬프 4바이트가 아깝습니다. 줄이려면?</summary>

1 MHz `uint32_t` 는 2^32 µs ≈ 71.6분에 넘친다(덤프 파서는 단조성을 가정하면 안 된다). 줄이는 법: ftrace 처럼 **이전 레코드와의 델타**를 작은 필드(27비트)에 넣고, 넘치면 확장 레코드(`TIME_EXTEND`)를 끼운다. 혹은 LEB128 같은 가변 길이 정수. 단점: 델타는 앞 레코드에 의존하므로 tail 의 레코드를 버릴 때 "기준 시각" 도 함께 옮겨야 하고, 손상 하나가 이후 시각을 모두 틀리게 만든다. 크래시 로그에선 레코드당 절대 시각이 단순하고 견고하다.

</details>

<details>
<summary>Q7. 왜 tail 은 언제나 레코드 경계여야 하나요? 레코드 최대 크기 제한은 왜 필요하죠?</summary>

길이 정보가 헤더에만 있기 때문에 "다음 레코드 위치" 를 아는 유일한 방법은 경계에서 len 을 읽는 것이다. tail 이 한 번이라도 중간에 떨어지면 이후 모든 경계를 잃고 복구할 수 없다(동기화 마커나 CRC 로 재동기하는 방법이 있지만 비용이 크다). 최대 크기 `7 + len ≤ LOG_SIZE` 가 없으면 make_room 이 전부 버려도 공간이 모자라 루프가 끝나지 않거나 새 레코드가 자기 자신을 덮는다. 반대로 len 이 16비트라 256 B 링에서는 검사가 필수, 64 KiB 이상 링이면 len 필드도 넓혀야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 패딩(skip-record) 방식으로 다시 만들고 비교

레코드를 절대 쪼개지 않는 `plog_t` 를 만들어라. 끝까지 남은 칸 to_end 가 레코드보다 작으면: to_end ≥ 7 이면 `type = 0xFF` PAD 레코드(len = to_end − 7)로 채우고, to_end < 7 이면 아무것도 쓰지 않는다(읽는 쪽도 "to_end < 7 이면 처음으로" 규칙). 같은 길이 시퀀스를 split 로거와 함께 넣어 보존 레코드 수와 낭비 바이트를 재라.

<details>
<summary>답 보기</summary>

핵심은 ① 공간을 `to_end + need` 만큼 확보 ② **그 과정에서 버퍼가 텅 비면** 간격을 세지 않고 head = tail = 다음 랩 시작으로 옮기기(그래야 링 전체 크기 레코드가 언제나 들어간다) ③ tail 쪽 버리기도 PAD 와 암묵 간격을 똑같이 건너뛰고 PAD 는 nrec 에 세지 않기. 검증: 매 append 뒤 걸은 레코드가 "가장 최근 레코드들의 빈틈없는 suffix" 인지, 레코드 바이트 + 낭비 ≤ 256 인지. 실측 표는 5. 풀이의 비교 표 — 패딩이 보존 레코드 약 5~8% 손해, 최대 낭비 116 B(256 B 의 45%).

</details>

### 연습 2 — 시퀀스 번호 리더와 lost 보고

소비하지 않는 리더 커서 `{seq, pos}` 를 만들어라. 로그는 `appended`(총 append 수)만 추가로 센다. 리더가 늦어서 자기 레코드가 덮어써졌으면 놓친 개수를 보고하고 가장 오래된 레코드로 재동기한다.

<details>
<summary>답 보기</summary>

tail 레코드의 seq = `appended − nrec`. `(int32_t)(c->seq − first) < 0` 이면(32비트 랩에도 안전한 비교) `lost += first − c->seq`, 커서를 `{first, tail}` 로. `c->seq == appended` 면 더 없음. 실측: 30,000 연산 중 append 20,073, read 13,950, lost 6,123 — **read + lost == appended** 로 하나도 중복·누락 없음을 확인. 정해진 테스트: 16 B 레코드 40개를 256 B 링에 넣으면 최근 16개만 남고, seq 1 에 있던 커서는 lost 23 을 보고하고 ts 24 를 읽는다.

</details>

### 연습 3 — 마지막 n개 레코드 (tail -n)

크래시 덤프에서 가장 궁금한 것은 마지막 몇 개다. 길이가 헤더 앞에만 있어 **뒤로 걸을 수 없다**. 마지막 n(≤ 8)개 레코드의 위치를 구하라.

<details>
<summary>답 보기</summary>

앞으로 걸으면서 레코드 시작 위치를 크기 n 짜리 작은 링(`ring[seen % n]`)에 기록하고, 끝나면 가장 오래된 것부터 꺼낸다. O(nrec) 시간, O(n) 메모리. 뒤로 O(1) 걷기가 필요하면 레코드 끝에 길이를 한 번 더 쓰는 **경계 태그**(boundary tag, TLSF/DS-A06 의 물리 인접 블록과 같은 아이디어)를 붙이면 된다 — 레코드당 2 B 비용.

</details>

### 연습 4 — 레코드 CRC-8: 구조 검사만으로 몇 개 잡히나

payload 마지막 바이트에 `CRC-8(헤더 7 B + payload 나머지)` (poly 0x07, init 0 = CRC-8/SMBUS)를 넣고, 꽉 찬 링의 살아 있는 모든 비트를 하나씩 뒤집어 (a) 구조 walk 만 (b) walk + CRC 가 몇 개를 잡는지 세라.

<details>
<summary>답 보기</summary>

실제 출력 (14 레코드, 246 B):

| 필드 | 뒤집은 비트 | 구조 검사만 | 구조 + CRC-8 |
|---|---|---|---|
| len | 224 | 220 | 224 |
| type | 112 | 0 | 112 |
| ts | 448 | 0 | 448 |
| payload + crc | 1184 | 0 | 1184 |
| 합계 | 1968 | 220 | 1968 |

구조 검사는 len 오류의 대부분(220/224)만 잡고 나머지 필드는 0개. len 비트 4개는 걸음이 우연히 head 에 맞아 구조 검사를 통과했지만 CRC 가 잡았다. 다항식에 항이 2개 이상인 CRC 는 단일 비트 오류를 **모두** 검출한다는 성질이 실측과 일치한다(type/ts/payload 100%). 비용은 레코드당 1 B + 바이트당 8번 시프트(테이블 256 B 를 쓰면 바이트당 조회 1번).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out   # 19/19 passed
```

## 10. 핵심 정리

- 가변 길이 로그 = DS-B04 바이트 링 + 레코드 헤더. head/tail 은 자유 증가 카운터, `used = head − tail`, 칸 = `& MASK`.
- **tail 은 언제나 레코드 경계** — 공간이 모자라면 `tail += 7 + len` 으로 가장 오래된 레코드를 통째로 버린다.
- split: 쪼개짐 처리를 `ring_put/ring_get`(memcpy ≤ 2) 한 곳에. 헤더는 7 B 로컬 배열로 인코딩/디코딩.
- 헤더는 시프트 기반 LE 직렬화 — 캐스팅은 쪼개짐·비정렬(M0 HardFault)·aliasing·패딩(8 ≠ 7) 때문에 금지. 비용은 0 (`ldur` 한 개).
- append 순서: 크기 검사 → 버리기 → 쓰기 → `head += need` 커밋. 최악 ⌈need/7⌉ 번 버리기 = 상수.
- 패딩 방식은 zero-copy 가 장점, 공간·이력이 단점(실측 보존 레코드 5~8% 손해). ftrace·printk 는 패딩 쪽.
- noinit 크래시 로그: magic + 구조 walk(remain < 7 먼저, len ≤ remain − 7, head 정확히 도착)로 warm/cold 판정, nrec 은 다시 센다. 내용 손상은 레코드 CRC 로.

## 참고자료

- [Linux kernel/trace/ring_buffer.c — per-CPU 페이지 링, RINGBUF_TYPE_PADDING, overwrite](https://github.com/torvalds/linux/blob/master/kernel/trace/ring_buffer.c)
- [Linux include/linux/ring_buffer.h — struct ring_buffer_event (type_len:5, time_delta:27)](https://github.com/torvalds/linux/blob/master/include/linux/ring_buffer.h)
- [Linux kernel/printk/printk_ringbuffer.c — 디스크립터 링 + 데이터 링, 시퀀스 번호](https://github.com/torvalds/linux/blob/master/kernel/printk/printk_ringbuffer.c)
- [Linux Documentation/admin-guide/ramoops.rst — pstore RAM 백엔드](https://www.kernel.org/doc/html/latest/admin-guide/ramoops.html)
- ISO/IEC 9899:2011 (C11) 6.3.2.3p7 포인터 정렬, 6.5p7 effective type/aliasing, 6.5.7p4 시프트, 7.24.1p2 문자열 함수의 포인터 인자
- Arm ARMv7-M Architecture Reference Manual — Alignment support (CCR.UNALIGN_TRP, LDRD/STRD/LDM/STM 은 항상 정렬 필요); ARMv6-M Architecture Reference Manual — 비정렬 접근은 HardFault
- DS-B03 (full/empty 판별, 덮어쓰기 모드), DS-B04 (자유 증가 카운터 증명), DS-A02 (bip buffer — 연속 영역 예약)
