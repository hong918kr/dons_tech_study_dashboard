# N05G · 상태 머신 · 파서
<!-- theme: pastel -->

> **묶음**: G 상태 머신 · 파서 · **문제 수**: 8 (★★★ 1 · ★★ 5 · ★ 2) · **목표**: 바이트/샘플/틱 단위로 들어오는 입력을 **enum 상태 + switch(또는 테이블)** 로 처리하고, 에러가 나면 **안전한 상태로 돌아가는(resync)** 코드를 종이에 20분 안에 쓰기 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: [N05F 메모리 테스트·무결성](2026-11-01_N05F_memtest_integrity.html) / [N05H C 함정·코드 읽기](2026-11-01_N05H_c_pitfalls_read_code.html) · **코드**: onsitePrep/n05/g/ (`./run.sh` → G1–G8 PASS)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **"입력 1개 → 상태 1번 전이"** 패턴. debounce(G1), UART 프레임(G2), PCIe 링크(G5), 전원 상태(G6)는 FSM 그 자체이고, TLV(G3)·쉘(G4)·카운터 체커(G7)·로그 라인(G8)은 **bounds 체크 + 에러 코드**가 핵심인 파서다.
- **외울 한 줄 1**: "Every state handles every input — and every error path goes back to a known state (IDLE / DETECT)." — 종이에 `switch` 쓰고 나서 **상태마다 '이상한 입력이 오면?'** 을 한 번씩 묻기.
- **외울 한 줄 2**: bounds 체크는 **`len > n - i - 2`** 처럼 **빼기 쪽**으로 (`i + 2 + len > n` 은 오버플로 가능). `n - i` 가 음수가 안 되게 **먼저 `n - i < 2` 확인**.
- **외울 한 줄 3**: 16-bit 카운터 차이는 **`(uint16_t)(cur - prev)`** — 그냥 `cur - prev` 는 `int` 로 승격돼서 wrap 때 음수가 된다.
- **★★★ = G2 (UART 프레임 파서)** — 온사이트 모의 세트 1 (C5 + G2)에 들어감. G5는 스크린 질문(LTSSM)과 이어지므로 꼬리 질문까지 소리 내서. G7은 카메라/센서 validation 그대로라 ★★지만 확률 높음 [추정].
- 각 FSM 문제 안에 **ASCII 상태도**가 있다 — 종이에도 **코드 전에 상태도부터 그리고** 시작하는 습관 (면접관이 제일 좋아하는 순서).
- 시간: ★ 15분 (G1, G8) · ★★/★★★ 20분. 순서 추천: G2 → G1 → G7 → G3 → G6 → G5 → G4 → G8.

## 1. 문제지 (인쇄용 — 답 없음)

### G1 · Button debounce FSM (★ · 15분)

A button is sampled every 1 ms. Write a debounce state machine: a change of the stable level is confirmed only after `n` consecutive samples at the new level; any sample at the old level cancels the pending change. Return `EV_PRESS` (low→high) or `EV_RELEASE` (high→low) exactly once per confirmed edge.

```c
typedef enum { EV_NONE, EV_PRESS, EV_RELEASE } db_event_t;
void       db_init(debounce_t *d, bool level, uint8_t n);   /* n == 0 treated as 1 */
db_event_t db_sample(debounce_t *d, bool raw);              /* called every 1 ms */
```

Example: `n = 3`, start low, raw `1 0 1 1 1` → `NONE NONE NONE NONE PRESS`.

### G2 · UART frame parser FSM (★★★ · 20분)

Frames on a UART: `0xAA` (SOF) · `LEN` (0..32) · `LEN` payload bytes · `CHK` = (LEN + sum of payload) mod 256. Bytes arrive one at a time. Write a byte-by-byte parser that returns `PR_FRAME` when a valid frame is complete, `PR_ERR_LEN` / `PR_ERR_CHK` on errors, and resynchronizes on the next SOF. `0xAA` inside the payload is just data.

```c
void      parser_init(parser_t *p);
presult_t parser_feed(parser_t *p, uint8_t b);   /* PR_NONE, PR_FRAME, PR_ERR_LEN, PR_ERR_CHK */
```

Example: `00 55 AA 03 10 20 30 63` → `PR_FRAME` on the last byte, payload `10 20 30`.

### G3 · TLV parser (★★ · 20분)

Parse a device-info blob of records `[type:1][len:1][value:len]`. Known: `0x01` TEMP (len 2, BE int16, 0.1 °C), `0x02` SERIAL (len 4, BE), `0x03` FWVER (len 3). `0xFF` = END (no length byte). Skip unknown types. Reject truncated records (`TLV_TRUNC`) and known types with the wrong length (`TLV_BADLEN`).

```c
int tlv_parse(const uint8_t *buf, size_t n, devinfo_t *out);   /* TLV_OK 0, TLV_TRUNC -1, TLV_BADLEN -2 */
```

Example: `01 02 FF 83 | 02 04 DE AD BE EF | 7E 03 09 09 09 | 03 03 01 02 03 | FF` → temp −125 (−12.5 °C), serial `0xDEADBEEF`, fw 1.2.3, unknown 1.

### G4 · Debug shell command dispatch (★★ · 20분)

Write `shell_exec(char *line)` for a board debug shell: tokenize in place on spaces/tabs (max 4 tokens), look the command up in a table `{name, fn, argc}`, check the argument count, call the handler. Commands: `rd <addr>`, `wr <addr> <val>`, `help`. Numbers are hex (optional `0x`). Addresses must be 4-byte aligned inside 16 registers at `0x40000000`. `parse_hex()` and `reg_ptr()` are given.

```c
int shell_exec(char *line);   /* SH_OK 0, SH_EMPTY 1, SH_UNKNOWN -1, SH_ARGC -2, SH_BADNUM -3, SH_BADADDR -4 */
```

Example: `"wr 0x40000004 0xDEADBEEF"` → `SH_OK`, then `"rd 0x40000004"` → `SH_OK`, `g_out == 0xDEADBEEF`; `"rd"` → `SH_ARGC`.

### G5 · Simplified PCIe link FSM (★★ · 20분)

Model a simplified link-training FSM stepped every 1 ms: `DETECT → POLLING → CONFIG → L0`, `L0 → RECOVERY` on error, `RECOVERY → L0` when retrained. `POLLING`, `CONFIG` and `RECOVERY` time out after 24 ms and fall back to `DETECT`; after more than 3 fallbacks without reaching L0, go to sticky `FAIL`. `DETECT` has no timeout. Reaching L0 resets the retry count; count L0→Recovery entries.

```c
void     link_init(link_t *l);
lstate_t link_step(link_t *l, const link_in_t *in);   /* in: rx_detected, ts_locked, cfg_done, link_err, rcv_done */
```

Example: inputs `rx_detected`, then `ts_locked`, then `cfg_done` on three ticks → `POLLING`, `CONFIG`, `L0`.

### G6 · Table-driven power FSM (★★ · 20분)

States `OFF, ACTIVE, IDLE, SLEEP`; events `PWR_ON, PWR_OFF, NO_ACT, ACT, TIMER, FAULT`. Implement the FSM as a 2-D table `[state][event] → {next, action}`. Events with no table entry are ignored and counted. The action runs, then the state changes. Validate the state and event ids.

```c
bool fsm_dispatch(fsm_t *f, pevent_t ev);   /* true if a transition happened */
```

Example: from `OFF`: `PWR_ON, NO_ACT, TIMER, ACT` → `ACTIVE, IDLE, SLEEP, ACTIVE`, actions `power_up, dim, suspend, resume`.

### G7 · 16-bit frame-counter sequence checker (★★ · 20분)

A sensor stamps every frame with a 16-bit counter that wraps 65535 → 0. Write a streaming checker that counts `lost`, `dup`, `ooo` (late frame, at most 8 behind) and `resync` (forward jump > 1024 or backward > 8 — sensor reset). Lost frames are counted for forward gaps up to 1024. The first frame only sets the baseline.

```c
void seq_init(seq_t *s);
void seq_feed(seq_t *s, uint16_t cnt);
```

Example: `10 11 11 13 12 14` → `lost 1, dup 1, ooo 1, resync 0`; `65534 65535 0 1 4` → `lost 2`.

### G8 · Log line parser without sscanf (★ · 15분)

Parse a line `"T=<decimal> V=0x<hex>"` (one or more spaces between fields, optional `\r\n` or `\n` at the end) into `{uint32_t t, v}`. Return `LP_RANGE` if a number does not fit in 32 bits, `LP_FORMAT` for anything else wrong (missing digits, wrong order, trailing junk).

```c
int parse_line(const char *s, rec_t *r);   /* LP_OK 0, LP_FORMAT -1, LP_RANGE -2 */
```

Example: `"T=42   V=0XdeadBEEF\r\n"` → `t = 42, v = 0xDEADBEEF`; `"T=4294967296 V=0x1"` → `LP_RANGE`.

## G1 · 버튼 debounce 상태 머신 (★ · 15분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: 1 ms마다 읽은 버튼 핀 값이 튀어도(bounce), **n번 연속 같은 새 값**이 나와야만 "눌림/뗌"을 한 번 보고하는 상태 머신.

**상황**: 기계식 스위치·커넥터 감지 핀·카드 present 핀은 접점이 붙는 순간 수 ms 동안 0/1이 마구 튄다. firmware가 이걸 그대로 인터럽트로 받으면 한 번 누름이 여러 번으로 세진다. 보드 validation에서도 "hot-plug 감지 핀이 몇 번 튀었나"를 세는 데 같은 로직을 쓴다.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `db_init(d, level, n)` | 시작 안정 레벨과 확정 샘플 수 n 설정 | n == 0 이면 1로 |
| `db_sample(d, raw)` | 1 ms마다 raw 레벨 1개 입력 | 확정된 edge에서만 `EV_PRESS` / `EV_RELEASE`, 나머지는 `EV_NONE` |

**예시를 말로 풀면**: n = 3, 처음엔 low. 1 → 0 → 1 → 1 → 1 이 들어오면, 첫 1은 중간에 0이 와서 취소되고, 뒤의 1 세 개가 연속이라 **다섯 번째 샘플에서 PRESS 1번**.

**지켜야 할 조건**: 누르고 있는 동안 PRESS가 반복되면 안 됨 · n보다 짧은 glitch는 아무 이벤트도 안 냄 · n = 255까지 동작 · 전역 변수 없이 struct 하나에 상태 보관.

### 1. Problem (English)

A push button is sampled by a 1 ms timer. The raw level bounces for a few milliseconds on every press and release. Implement a debounce state machine: the stable level changes only after `n` **consecutive** samples at the new level; a sample at the old level during that window cancels the pending change. Report each confirmed edge exactly once.

```c
typedef enum { DB_LOW, DB_MAYBE_HIGH, DB_HIGH, DB_MAYBE_LOW } db_state_t;
typedef enum { EV_NONE, EV_PRESS, EV_RELEASE } db_event_t;
typedef struct { db_state_t st; uint8_t cnt; uint8_t n; } debounce_t;

void       db_init(debounce_t *d, bool level, uint8_t n);
db_event_t db_sample(debounce_t *d, bool raw);
```

- `n = 3`, start low, raw `1 0 1 1 1` → `NONE NONE NONE NONE PRESS`
- `n = 3`, start high, raw `0 0 1 0 0 0` → `NONE NONE NONE NONE NONE RELEASE`
- `n = 4`, start low, raw `0 1 1 1 0 1 1 1 0` → no event (every run of 1s is shorter than 4)

### 2. 먼저 물어볼 것 (clarify)

- "Is the sample rate fixed at 1 ms, and is the threshold given in samples, not milliseconds?" → 네, n은 샘플 수.
- "Should I report events, or just expose the stable level?" → 둘 다: 이벤트 반환 + `d->st` 로 레벨 확인 가능.
- "Is this called from the timer ISR?" → 그렇다고 가정. 그래서 루프·블로킹 없이 O(1).
- "Active-high?" → raw 1 = 눌림.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (n 단위, ISR 여부) |
| 2–5 | 상태도 그리기 + 예시 `1 0 1 1 1` 손으로 |
| 5–12 | enum · struct · switch 코드 |
| 12–14 | trace |
| 14–15 | 테스트 · 확장(카운터 방식, 비대칭 n) |

- 소리 내서: "I'll use four states — two stable and two 'maybe' states — so a bounce in the maybe state just goes back to the stable one."
- 소리 내서: "This runs in the 1 ms timer ISR, so it's O(1) per sample and never blocks."

<details><summary>힌트 1 (막히면 5분 후)</summary>

상태 4개로 그려 보자. "확정된 low/high" 2개 + "바뀔지도 모르는 중" 2개.

```text
            raw=1 (cnt=1)                  cnt reaches n -> EV_PRESS
  +-------+ ------------> +-------------+ ------------------> +--------+
  |  LOW  |               | MAYBE_HIGH  |                     |  HIGH  |
  +-------+ <------------ +-------------+                     +--------+
     ^  ^      raw=0 (bounce, cancel)    raw=1: cnt++            |   |
     |  |                                                 raw=0  |   |
     |  |   cnt reaches n -> EV_RELEASE   +-------------+ <------+   |
     |  +-------------------------------- | MAYBE_LOW   |  (cnt=1)   |
     |                                    +-------------+ ---------->+
     |                                       raw=0: cnt++   raw=1 (cancel)
   raw=0: stay
```

</details>

<details><summary>힌트 2</summary>

stable 상태에서 반대 값이 오면 `cnt = 0` 으로 하고 **maybe 상태의 코드로 fall-through** 시키면, "첫 샘플도 1로 센다"가 한 군데서 처리된다. n == 1이면 그 자리에서 바로 이벤트가 나와야 한다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

typedef enum { DB_LOW, DB_MAYBE_HIGH, DB_HIGH, DB_MAYBE_LOW } db_state_t;
typedef enum { EV_NONE, EV_PRESS, EV_RELEASE } db_event_t;

typedef struct {
    db_state_t st;
    uint8_t    cnt;   /* consecutive samples that disagree with the stable level */
    uint8_t    n;     /* samples needed to confirm a change (n >= 1) */
} debounce_t;

void db_init(debounce_t *d, bool level, uint8_t n)
{
    d->st  = level ? DB_HIGH : DB_LOW;
    d->cnt = 0;
    d->n   = n ? n : 1;
}

/* Call once per 1 ms tick with the raw pin level. Returns an event on a confirmed edge. */
db_event_t db_sample(debounce_t *d, bool raw)
{
    switch (d->st) {
    case DB_LOW:
        if (!raw) return EV_NONE;
        d->cnt = 0;
        d->st  = DB_MAYBE_HIGH;
        /* fall through: this sample counts as the first high one */
    case DB_MAYBE_HIGH:
        if (!raw) { d->st = DB_LOW; return EV_NONE; }      /* bounce: back to stable */
        if (++d->cnt < d->n) return EV_NONE;
        d->st = DB_HIGH;
        return EV_PRESS;
    case DB_HIGH:
        if (raw) return EV_NONE;
        d->cnt = 0;
        d->st  = DB_MAYBE_LOW;
        /* fall through */
    case DB_MAYBE_LOW:
        if (raw) { d->st = DB_HIGH; return EV_NONE; }
        if (++d->cnt < d->n) return EV_NONE;
        d->st = DB_LOW;
        return EV_RELEASE;
    }
    return EV_NONE;
}
```

시간 O(1)/샘플 · 공간 O(1) (3바이트 + enum).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

n = 3, 시작 `DB_LOW`, raw `1 0 1 1 1`

| ms | raw | 상태(전) | cnt | 상태(후) | 반환 |
|---|---|---|---|---|---|
| 1 | 1 | LOW | 0→1 | MAYBE_HIGH | NONE |
| 2 | 0 | MAYBE_HIGH | 1 | LOW | NONE (취소) |
| 3 | 1 | LOW | 0→1 | MAYBE_HIGH | NONE |
| 4 | 1 | MAYBE_HIGH | 2 | MAYBE_HIGH | NONE |
| 5 | 1 | MAYBE_HIGH | 3 = n | HIGH | **PRESS** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: n = 3, `1 1 1` → 3번째에 PRESS, 그 뒤 `1` 계속 → 이벤트 없음 (반복 안 됨).
- bounce: `1 0 1 1 0 1 1 1` → PRESS 정확히 1번, 마지막 샘플에서.
- glitch < n: n = 4, `0111 0111 0111 0` → 이벤트 0개.
- 경계: n = 1 → 모든 레벨 변화가 이벤트 (`1 0 1 0` → 4개); n = 0 → 1로 처리; n = 255 → 254번째까지 NONE, 255번째 PRESS (uint8 cnt 오버플로 없음).
- 시작 레벨 high: `0 0` (n = 2) → RELEASE 1번.

</details>

### 4. 꼬리 질문

**Q1. Can you do it without explicit states — just a counter?**

<details><summary>답</summary>

Yes: keep `stable` and `cnt`; if `raw == stable` reset `cnt` to 0, otherwise increment it and when it reaches `n` flip `stable` and report the edge. For a binary input that's equivalent to my four states — the "maybe" states are just `cnt > 0`. I drew the explicit FSM because it's easier to explain and to extend, for example with different press and release thresholds.

한국어 핵심: 2-레벨 입력이면 "stable + 카운터" 한 줄짜리와 동치 — maybe 상태 = `cnt > 0`. (스니펫은 `stable` 필드를 쓰는 다른 struct 가정 `[미검증]`)

```c
if (raw == d->stable) d->cnt = 0;
else if (++d->cnt >= d->n) { d->stable = raw; d->cnt = 0; return raw ? EV_PRESS : EV_RELEASE; }
```

</details>

**Q2. You have 16 buttons on one GPIO port. How do you debounce all of them at once?**

<details><summary>답</summary>

Use the vertical-counter trick or a shift-history approach: keep the last N port reads, and a bit is stable-high when it's 1 in all of them (AND of the history) and stable-low when it's 0 in all of them (OR is 0). Then `changed = new_stable ^ old_stable` gives all edges in one XOR. It's bit-parallel, so 16 or 32 buttons cost the same as one.

한국어 핵심: 포트 전체를 한 번에 — 최근 N개 읽기의 AND/OR로 안정 비트 판정, XOR로 edge.

</details>

**Q3. Why not just use an edge interrupt on the pin?**

<details><summary>답</summary>

A bouncing contact can fire dozens of edge interrupts in a few milliseconds, which wastes CPU and can starve other ISRs. A common pattern is: take the first edge interrupt, mask it, then debounce with the timer and re-enable after the level is confirmed. For a validation test I'd also count the raw edges, because the bounce count itself is a useful hardware metric.

한국어 핵심: edge IRQ는 bounce마다 터짐 → 첫 IRQ 후 마스크 + 타이머 debounce, raw edge 수는 HW 지표로 따로 기록.

</details>

### 5. 채점 · 자주 틀리는 것

- 누르고 있는 동안 PRESS가 **반복**되지 않는가 (stable 상태에서 같은 값이면 즉시 `return`).
- 첫 번째 새 값 샘플을 **카운트에 포함**했는가 (n = 1에서 바로 이벤트).
- bounce 시 **cnt 리셋** (maybe → stable 복귀 후 다시 들어올 때 `cnt = 0`).
- `n == 0` 방어 (안 하면 cnt가 255까지 돌아야 확정).
- fall-through에 `/* fall through */` 주석 — 의도임을 보여주기 (gcc `-Wimplicit-fallthrough`).

## G2 · UART 프레임 파서 — SOF · LEN · payload · checksum (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: UART로 한 바이트씩 들어오는 `AA | LEN | payload | CHK` 프레임을 **바이트 단위 FSM**으로 파싱하고, 깨진 프레임은 버린 뒤 **다음 SOF(0xAA)에서 다시 동기화**한다.

**상황**: 센서·PMIC·테스트 장비와 MCU 사이 UART 프로토콜은 거의 다 이런 모양이다. RX 인터럽트(또는 링버퍼, [N05E](2026-11-01_N05E_ring_queue_isr.html))에서 1바이트씩 꺼내 parser에 먹인다. validation에서는 "몇 프레임 받았고 몇 개 깨졌나"가 링크 품질 지표다.

```text
                 b == 0xAA
   +--------+ ---------------> +--------+   LEN > 32 (and b != 0xAA)  -> ERR_LEN, IDLE
   |  IDLE  |                  |  LEN   |   LEN > 32 and b == 0xAA    -> ERR_LEN, stay LEN
   +--------+ <-+              +--------+
     ^  b!=AA   |      LEN 1..32 |    | LEN == 0
     |  (drop)--+                v    |
     |                     +-----------+   (got LEN bytes)
     |                     |  PAYLOAD  | -----------------+
     |                     +-----------+                  v
     |                         ^   |  idx < LEN      +---------+
     |                         +---+                 |   CHK   | <---- (LEN == 0)
     |                                               +---------+
     +---------------------------------------------- CHK ok  -> PR_FRAME
                                                      CHK bad -> PR_ERR_CHK
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `parser_init(p)` | 상태 IDLE, 통계 0 | |
| `parser_feed(p, b)` | 바이트 1개 처리 | 프레임 완성 → `PR_FRAME` (payload는 `p->buf`, 길이 `p->len`) · 길이 이상 → `PR_ERR_LEN` · checksum 불일치 → `PR_ERR_CHK` · 그 외 `PR_NONE` |

- CHK = (LEN + payload 전부) mod 256. LEN 최대 32. LEN = 0 이면 payload 없이 바로 CHK.
- payload 안의 0xAA는 그냥 데이터 (byte stuffing 없음).
- 통계 `frames`, `errors` 를 struct에 누적.

**예시를 말로 풀면**: `00 55` 는 잡음이라 버림 → `AA` 에서 시작 → LEN 3 → `10 20 30` → CHK = 3+0x10+0x20+0x30 = 0x63 → 마지막 바이트 `63` 에서 PR_FRAME.

**지켜야 할 조건**: 버퍼 overflow 불가 (LEN 검사 먼저) · 에러 후 다음 프레임은 정상으로 받아야 함 · 바이트당 O(1) (ISR에서 불러도 됨).

### 1. Problem (English)

A sensor sends frames over UART: `0xAA` start-of-frame, a length byte `LEN` (0..32), `LEN` payload bytes, and a checksum byte `CHK = (LEN + Σ payload) mod 256`. Bytes arrive one at a time from the RX interrupt. Write a parser that is fed one byte per call, reports a complete valid frame, reports length and checksum errors, and recovers by waiting for the next SOF.

```c
#define SOF     0xAAu
#define MAX_LEN 32u
typedef enum { ST_IDLE, ST_LEN, ST_PAYLOAD, ST_CHK } pstate_t;
typedef enum { PR_NONE, PR_FRAME, PR_ERR_LEN, PR_ERR_CHK } presult_t;
typedef struct { pstate_t st; uint8_t len, idx, sum; uint8_t buf[MAX_LEN]; uint32_t frames, errors; } parser_t;

void      parser_init(parser_t *p);
presult_t parser_feed(parser_t *p, uint8_t b);
```

- `AA 03 10 20 30 63` → `PR_FRAME` on `63`, payload `10 20 30`
- `AA 01 05 00  AA 01 05 06` → `PR_ERR_CHK` on `00`, then `PR_FRAME` on `06`
- `AA 40  AA AA 01 07 08` → `PR_ERR_LEN` on `40`; the next `AA` is an SOF; the `AA` after it lands in the LEN slot → `PR_ERR_LEN`, but it is kept as a new SOF; then `PR_FRAME` on `08`

### 2. 먼저 물어볼 것 (clarify)

- "Can 0xAA appear in the payload, and is there byte stuffing?" → 나올 수 있음, stuffing 없음. 그래서 LEN으로만 끝을 안다.
- "What's covered by the checksum — LEN too, or payload only?" → LEN + payload.
- "On an error, do you want me to re-scan the bytes I already consumed, or just wait for the next SOF?" → 다음 SOF 대기 (단순 버전). 재스캔은 꼬리 질문.
- "Is there an inter-byte timeout?" → 이번엔 없음 (꼬리 질문).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (stuffing? checksum 범위? 에러 처리) |
| 2–5 | 상태도 그리기 + 예시 한 프레임 손으로 |
| 5–15 | enum · struct · `parser_feed` switch |
| 15–18 | trace `AA 03 10 20 30 63` |
| 18–20 | 테스트 (garbage, bad CHK, LEN 0, LEN 32/33, 연속 프레임) + 확장 |

- 소리 내서: "It's a byte-driven state machine: IDLE, LEN, PAYLOAD, CHK. Every error path goes back to IDLE, so a corrupted frame costs at most one frame."
- 소리 내서: "I validate LEN before I touch the buffer, so a bad length byte can never overflow `buf`."

<details><summary>힌트 1 (막히면 5분 후)</summary>

LEN을 받는 순간 `idx = 0`, `sum = LEN` 으로 초기화해 두면 CHK 상태에서 비교 한 번으로 끝난다. LEN이 0이면 PAYLOAD를 건너뛰고 바로 CHK로.

</details>

<details><summary>힌트 2</summary>

LEN 자리에 0xAA가 왔다면? 0xAA(170)은 MAX_LEN(32)보다 크니 무조건 길이 에러인데, 동시에 **새 SOF일 가능성**이 높다 (`AA AA 03 …` = 앞 SOF 뒤 바이트가 유실). 그때는 IDLE 대신 LEN 상태에 머물면 한 프레임을 살린다. 그리고 `sum` 은 `uint8_t` 로 두면 mod 256이 자동.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

#define SOF      0xAAu
#define MAX_LEN  32u
/* CHK = (LEN + payload[0] + ... + payload[LEN-1]) mod 256 */

typedef enum { ST_IDLE, ST_LEN, ST_PAYLOAD, ST_CHK } pstate_t;
typedef enum { PR_NONE, PR_FRAME, PR_ERR_LEN, PR_ERR_CHK } presult_t;

typedef struct {
    pstate_t st;
    uint8_t  len, idx, sum;
    uint8_t  buf[MAX_LEN];
    uint32_t frames, errors;    /* stats for the validation log */
} parser_t;

void parser_init(parser_t *p) { p->st = ST_IDLE; p->frames = p->errors = 0; }

/* Feed one received byte. PR_FRAME => p->buf[0..p->len-1] holds a valid payload. */
presult_t parser_feed(parser_t *p, uint8_t b)
{
    switch (p->st) {
    case ST_IDLE:
        if (b == SOF) p->st = ST_LEN;          /* anything else: noise, drop it */
        return PR_NONE;
    case ST_LEN:
        if (b > MAX_LEN) {
            p->errors++;
            p->st = (b == SOF) ? ST_LEN : ST_IDLE;   /* "AA AA 03 ..": 2nd AA is the real SOF */
            return PR_ERR_LEN;
        }
        p->len = b; p->idx = 0; p->sum = b;
        p->st = (b == 0) ? ST_CHK : ST_PAYLOAD;
        return PR_NONE;
    case ST_PAYLOAD:
        p->buf[p->idx++] = b;
        p->sum = (uint8_t)(p->sum + b);
        if (p->idx == p->len) p->st = ST_CHK;
        return PR_NONE;
    case ST_CHK:
        p->st = ST_IDLE;
        if (b != p->sum) { p->errors++; return PR_ERR_CHK; }
        p->frames++;
        return PR_FRAME;
    }
    p->st = ST_IDLE;                            /* corrupted state: recover */
    return PR_NONE;
}
```

시간 O(1)/바이트 · 공간 O(MAX_LEN).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

입력 `AA 03 10 20 30 63`

| 바이트 | 상태(전) | 동작 | len · idx · sum | 상태(후) | 반환 |
|---|---|---|---|---|---|
| AA | IDLE | SOF | – | LEN | NONE |
| 03 | LEN | 3 ≤ 32 | 3 · 0 · 0x03 | PAYLOAD | NONE |
| 10 | PAYLOAD | buf[0]=10 | 3 · 1 · 0x13 | PAYLOAD | NONE |
| 20 | PAYLOAD | buf[1]=20 | 3 · 2 · 0x33 | PAYLOAD | NONE |
| 30 | PAYLOAD | buf[2]=30, idx==len | 3 · 3 · 0x63 | CHK | NONE |
| 63 | CHK | 0x63 == sum | – | IDLE | **FRAME** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 1프레임 → FRAME, `frames == 1`.
- 앞에 잡음 + 연속 2프레임, payload에 0xAA 포함 + LEN 0 프레임(`AA 00 00`) → FRAME 2개, 에러 0.
- CHK 틀림 → ERR_CHK, 바로 뒤 정상 프레임은 FRAME (에러가 다음 프레임에 번지지 않음).
- LEN = 32 (경계, payload 0xFF×32 → CHK 0x00) → FRAME; LEN = 33 → ERR_LEN, IDLE.
- `AA 40 AA AA 01 07 08` → ERR_LEN 2개 후 FRAME (중복 SOF resync).
- 순수 잡음 `"hello"` → 아무 결과 없음, IDLE 유지.

</details>

### 4. 꼬리 질문

**Q1. A byte is lost in the middle of a frame. What happens, and how would you make resync faster?**

<details><summary>답</summary>

The parser treats the next byte as payload, so it eats into the following frame and usually fails the checksum; then it waits for an SOF, which may cost a second frame. To recover faster I'd keep the raw bytes of the current frame and, on a checksum error, re-scan them from the byte after the old SOF for another 0xAA — or add an inter-byte timeout so a gap in the stream forces IDLE.

한국어 핵심: 유실 시 최대 2프레임 손해 → 에러 시 버퍼 재스캔, 또는 inter-byte timeout으로 IDLE 강제.

</details>

**Q2. Add an inter-byte timeout of 5 ms.**

<details><summary>답</summary>

Store the tick of the last byte in the parser. At the top of `parser_feed`, if the state is not IDLE and `(uint32_t)(now - p->last_tick) > 5`, count a timeout error and reset to IDLE before processing the byte. The subtraction is unsigned so it stays correct across tick wrap-around, same as the polling timeout in C1.

한국어 핵심: `last_tick` 저장, `(uint32_t)(now - last) > T` 면 IDLE로 — wrap 안전 뺄셈 (C1과 같은 패턴). 아래 스니펫은 `parser_t` 에 `last_tick` 필드를 추가한 가정 `[미검증]`.

```c
if (p->st != ST_IDLE && (uint32_t)(now - p->last_tick) > 5u) { p->errors++; p->st = ST_IDLE; }
p->last_tick = now;
```

</details>

**Q3. Where do you call `parser_feed` — in the UART ISR or in main?**

<details><summary>답</summary>

I'd keep the ISR minimal: read the RX data register, push into an SPSC ring buffer, and return. Main drains the ring and calls `parser_feed`, and hands complete frames to the application. If I parsed in the ISR, I'd have to copy the frame out before the next byte overwrites `buf`, and long frame handling would add interrupt latency.

한국어 핵심: ISR은 링버퍼 push만, 파싱은 main에서 — `buf` 덮어쓰기와 ISR 지연 방지.

</details>

**Q4. Why is an 8-bit sum a weak checksum? What would you use instead?**

<details><summary>답</summary>

A sum can't detect reordered bytes, and two errors that cancel out, like +1 in one byte and −1 in another, pass. A CRC-8 or CRC-16 detects every single-bit error and every burst up to its width, and a well-chosen polynomial also catches double-bit errors up to its design length, which is why real protocols use CRC. It's a drop-in change: replace the `sum` update with a CRC update per byte.

한국어 핵심: 합은 순서 바뀜·상쇄 에러를 못 잡음 → CRC-8/16 (N05F F5)로 교체, 바이트당 업데이트 구조는 그대로.

</details>

### 5. 채점 · 자주 틀리는 것

- **LEN 검사 전에 `buf` 에 쓰지 않기** — `LEN > MAX_LEN` 이면 즉시 에러.
- LEN = 0 처리 (PAYLOAD를 건너뛰지 않으면 다음 프레임의 SOF를 payload로 먹음).
- checksum에 LEN 포함 여부를 clarify에서 확정하고 `sum = LEN` 으로 시작.
- 모든 에러 경로가 IDLE(또는 정당한 LEN)로 — 이상한 상태에 갇히지 않기 (`default`/switch 밖 복구).
- `sum` 을 `uint8_t` 로 — int 누적 후 비교하면 `0x163 != 0x63` 버그.
- 통계(frames/errors) 카운트를 말하기 — validation에선 이게 결과물.

## G3 · TLV 파서 — bounds 체크 · 모르는 type 건너뛰기 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: `[type][len][value]` 레코드가 이어진 바이트 배열을 처음부터 걸으면서 아는 type은 구조체에 채우고, 모르는 type은 건너뛰고, **잘린 레코드나 길이가 틀린 레코드는 거부**한다.

**상황**: 보드 EEPROM의 board-ID/serial, 센서 calibration blob, PCIe VPD, USB descriptor, firmware 이미지 헤더가 모두 TLV다. 외부에서 들어오는 바이트라 **len 값을 믿으면 안 된다** — 부트로더 보안 버그의 단골.

```text
  i ->  +------+-----+---------------+------+-----+-------+ ... +------+
        | type | len | value[len]    | type | len | value |     | 0xFF |
        +------+-----+---------------+------+-----+-------+ ... +------+
        <-- 2 --><--- len bytes ---->                            END: no len byte
        next record starts at i + 2 + len
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `tlv_parse(buf, n, out)` | buf 전체를 파싱해 `out` 을 채움 | 시작 시 `out` 0으로 · 성공 `TLV_OK` · 잘림 `TLV_TRUNC` · 아는 type인데 길이 틀림 `TLV_BADLEN` |

| type | 이름 | len | 값 |
|---|---|---|---|
| 0x01 | TEMP | 2 | big-endian int16, 0.1 °C 단위 |
| 0x02 | SERIAL | 4 | big-endian uint32 |
| 0x03 | FWVER | 3 | major, minor, patch |
| 0xFF | END | (없음) | 여기서 파싱 종료, 뒤는 무시 |
| 그 외 | unknown | 아무거나 | 건너뛰고 `unknown` 카운트 |

**예시를 말로 풀면**: `01 02 FF 83` → 온도 0xFF83 = −125 → −12.5 °C. `02 04 DE AD BE EF` → serial. `7E 03 09 09 09` → 모르는 type, 3바이트 건너뜀. `03 03 01 02 03` → fw 1.2.3. `FF` → 끝.

**지켜야 할 조건**: buf 밖을 절대 읽지 않음 (len = 255 같은 악의적 값 포함) · n = 0 이면 OK · END 없이 버퍼가 딱 끝나도 OK.

### 1. Problem (English)

Board EEPROM holds a device-info blob as a sequence of TLV records: one type byte, one length byte, then `len` value bytes. Type `0xFF` marks the end and has no length byte. Parse it into a struct. Unknown types must be skipped (forward compatibility). Never read past `n`: return `TLV_TRUNC` if a header or value is cut off, `TLV_BADLEN` if a known type has the wrong length.

```c
enum { T_TEMP = 0x01, T_SERIAL = 0x02, T_FWVER = 0x03, T_END = 0xFF };
enum { TLV_OK = 0, TLV_TRUNC = -1, TLV_BADLEN = -2 };
typedef struct {
    bool has_temp, has_serial, has_fw;
    int16_t temp_c10; uint32_t serial; uint8_t fw[3]; unsigned unknown;
} devinfo_t;

int tlv_parse(const uint8_t *buf, size_t n, devinfo_t *out);
```

- `01 02 FF 83 | 02 04 DE AD BE EF | 7E 03 09 09 09 | 03 03 01 02 03 | FF | 01 02 00 00` → `TLV_OK`, temp −125, serial `0xDEADBEEF`, fw 1.2.3, unknown 1 (bytes after END ignored)
- `02 04 11 22 33` → `TLV_TRUNC`
- `01 03 00 00 00` → `TLV_BADLEN`

### 2. 먼저 물어볼 것 (clarify)

- "Is the length one byte, and does it count only the value?" → 1바이트, value만.
- "Multi-byte values — big or little endian?" → big-endian.
- "If a known type appears twice, last one wins?" → 그렇게 가정.
- "On error, should `out` keep what was parsed so far?" → 부분 결과는 남지만 반환값으로 실패를 알림.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (len 의미, 엔디안, END) |
| 2–5 | 레코드 그림 + 루프 불변식 "i = 다음 레코드 시작" |
| 5–15 | while 루프 · 두 단계 bounds 체크 · switch |
| 15–18 | trace (예시 앞 3레코드) |
| 18–20 | 테스트 (빈 버퍼, 헤더 잘림, len=255, 길이 틀림) |

- 소리 내서: "The invariant is that `i` always points at the start of a record. Before reading anything I check there are enough bytes left — header first, then value."
- 소리 내서: "I compare `len > n - i - 2` instead of `i + 2 + len > n`, so it can't overflow."

<details><summary>힌트 1 (막히면 5분 후)</summary>

체크 순서: ① `type == END` 면 바로 OK (len 없음) ② `n - i < 2` 면 헤더 잘림 ③ `len > n - i - 2` 면 value 잘림. ②를 통과했으니 ③의 `n - i - 2` 는 음수(= size_t wrap)가 안 된다.

</details>

<details><summary>힌트 2</summary>

big-endian 조립은 캐스트 대신 shift: `((uint32_t)v[0] << 24) | …`. `v[0] << 24` 를 캐스트 없이 쓰면 `v[0]` 이 int로 승격돼서 0x80 이상일 때 부호 비트로 시프트 = UB. 그리고 unknown이든 known이든 **마지막에 `i += 2 + len`** 하나로 다음 레코드로.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum { T_TEMP = 0x01, T_SERIAL = 0x02, T_FWVER = 0x03, T_END = 0xFF };
enum { TLV_OK = 0, TLV_TRUNC = -1, TLV_BADLEN = -2 };

typedef struct {
    bool     has_temp, has_serial, has_fw;
    int16_t  temp_c10;          /* T_TEMP: 2 bytes big-endian, signed, 0.1 C units */
    uint32_t serial;            /* T_SERIAL: 4 bytes big-endian */
    uint8_t  fw[3];             /* T_FWVER: major, minor, patch */
    unsigned unknown;           /* how many unknown records were skipped */
} devinfo_t;

int tlv_parse(const uint8_t *buf, size_t n, devinfo_t *out)
{
    *out = (devinfo_t){0};
    size_t i = 0;
    while (i < n) {
        uint8_t type = buf[i];
        if (type == T_END) return TLV_OK;               /* END has no length byte */
        if (n - i < 2) return TLV_TRUNC;                /* header cut off */
        uint8_t len = buf[i + 1];
        if (len > n - i - 2) return TLV_TRUNC;          /* no i+2+len overflow */
        const uint8_t *v = &buf[i + 2];
        switch (type) {
        case T_TEMP:
            if (len != 2) return TLV_BADLEN;
            out->temp_c10 = (int16_t)(uint16_t)((v[0] << 8) | v[1]);
            out->has_temp = true;
            break;
        case T_SERIAL:
            if (len != 4) return TLV_BADLEN;
            out->serial = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                          ((uint32_t)v[2] << 8)  |  (uint32_t)v[3];
            out->has_serial = true;
            break;
        case T_FWVER:
            if (len != 3) return TLV_BADLEN;
            out->fw[0] = v[0]; out->fw[1] = v[1]; out->fw[2] = v[2];
            out->has_fw = true;
            break;
        default:
            out->unknown++;                             /* forward compatible: skip */
            break;
        }
        i += 2u + len;
    }
    return TLV_OK;                                      /* ran out of bytes cleanly */
}
```

시간 O(n) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

예시 1 (n = 25)

| i | type | n−i | len | 체크 | 동작 | 다음 i |
|---|---|---|---|---|---|---|
| 0 | 01 | 25 | 2 | 2 ≤ 23 | temp = 0xFF83 = −125 | 4 |
| 4 | 02 | 21 | 4 | 4 ≤ 19 | serial = 0xDEADBEEF | 10 |
| 10 | 7E | 15 | 3 | 3 ≤ 13 | unknown = 1 | 15 |
| 15 | 03 | 10 | 3 | 3 ≤ 8 | fw = 1.2.3 | 20 |
| 20 | FF | – | – | END | return OK | – |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 위 예시 → 모든 필드 + unknown 1, END 뒤 바이트 무시.
- 빈 버퍼 n = 0 → OK, `has_*` 전부 false.
- 헤더 잘림 `01` 하나 → TRUNC. value 잘림 `02 04 11 22 33` → TRUNC.
- 악의적 len: `7E FF 00` (n = 3) → TRUNC, buf 밖 읽기 없음 (ASan으로 확인).
- 길이 틀림 `01 03 …` → BADLEN. 길이 0 unknown `40 00` 다음 레코드가 버퍼 끝에 딱 맞음 → OK.

</details>

### 4. 꼬리 질문

**Q1. The blob is in EEPROM behind I2C and you can't load it all into RAM. How does the parser change?**

<details><summary>답</summary>

I'd turn it into a streaming parser: read the 2-byte header, check `len` against the remaining size from a known total length, then either read the value for known types or just advance the EEPROM offset by `len` for unknown ones. The bounds logic is the same — the offset plays the role of `i`. I'd also cap the total walk so a corrupted blob can't loop forever.

한국어 핵심: 헤더만 읽고 unknown은 offset만 전진 — `i` 가 EEPROM offset이 될 뿐 bounds 로직은 동일.

</details>

**Q2. What if the length field is 2 bytes and values can be large?**

<details><summary>답</summary>

The header becomes 3 bytes, so the first check is `n - i < 3`, and `len` becomes `uint16_t`. The value check `len > n - i - 3` is still overflow-safe because I subtract from `n - i`, which I already know is at least 3. The same pattern is used in PCIe VPD large resource tags.

한국어 핵심: 헤더 크기만 바꾸면 됨 — "빼기 쪽으로 비교"가 그대로 오버플로를 막음.

</details>

**Q3. How do you protect against a blob that is valid TLV but corrupted?**

<details><summary>답</summary>

TLV structure alone doesn't prove integrity, so I'd add a CRC record — for example a last record holding a CRC-32 over all preceding bytes — and check it before trusting any field. Also a version or magic header at offset 0, so an erased EEPROM (all 0xFF) is recognized immediately instead of parsing as "END".

한국어 핵심: CRC 레코드 + magic/version 헤더 — 지워진 EEPROM(0xFF)은 그냥 END로 읽혀서 "빈 정보"로 오인됨.

</details>

### 5. 채점 · 자주 틀리는 것

- **두 단계 bounds 체크** (헤더 → value), `i + 2 + len > n` 대신 빼기 쪽.
- END는 len이 없음 — `buf[i+1]` 을 읽기 전에 END 확인.
- BE 조립 시 `(uint32_t)` 캐스트 먼저 (`v[0] << 24` 는 int 부호 비트 UB).
- `(int16_t)(uint16_t)0xFF83` 은 **implementation-defined** 변환 (C11 6.3.1.3) — 실제로는 모든 2의 보수 컴파일러에서 −125, 면접에선 "implementation-defined, fine on every two's-complement target"이라고 한 마디.
- unknown도 `i += 2 + len` 로 전진 (안 하면 무한 루프).
- 시작할 때 `out` 초기화.

## G4 · 디버그 쉘 — 명령 테이블 디스패치 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: UART 콘솔에서 받은 한 줄 `"wr 0x40000004 0xDEADBEEF"` 를 **제자리 토큰화** → **명령 테이블에서 이름 찾기** → **인자 개수 확인** → **함수 포인터 호출**.

**상황**: bring-up 보드의 bare-metal 디버그 쉘(U-Boot `md`/`mw`, 사내 diag 쉘)이 정확히 이 구조다. silicon validation에서 레지스터를 손으로 찔러 보는 첫 도구. 명령을 추가할 때 **테이블에 한 줄만** 넣으면 되게 만드는 것이 포인트.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `shell_exec(line)` | 한 줄 실행 | 공백/탭으로 제자리 분리, 토큰 최대 4개 (넘으면 `SH_ARGC`) · 빈 줄 `SH_EMPTY` · 모르는 명령 `SH_UNKNOWN` · 인자 수 틀림 `SH_ARGC` |
| `cmd_rd` / `cmd_wr` / `cmd_help` | 각 명령 handler | 숫자 틀림 `SH_BADNUM` · 주소 범위/정렬 틀림 `SH_BADADDR` · rd 결과는 `g_out` 에 |
| (주어짐) `parse_hex(s, &v)` | "0x1F" 또는 "1f" → uint32 | 실패 시 false (D4에서 연습) |
| (주어짐) `reg_ptr(addr)` | 주소 → 가짜 레지스터 포인터 | 0x40000000부터 16개, 4바이트 정렬 아니면 NULL |

**예시를 말로 풀면**: `wr 0x40000004 0xDEADBEEF` → 토큰 3개, "wr" 찾음, argc 3 맞음 → 레지스터 1번에 씀. 이어서 `rd 0x40000004` → `g_out = 0xDEADBEEF`. `rd` 만 치면 인자 부족 → `SH_ARGC`.

**지켜야 할 조건**: `printf`·`strtok`·`malloc` 없이 (`strtok` 은 내부 static 상태라 재진입 불가) · "rdx" 같은 접두사가 "rd"로 매칭되면 안 됨 · 앞뒤·중간 공백 여러 개 허용.

### 1. Problem (English)

Write the dispatcher for a bare-metal debug shell. `shell_exec` gets one mutable input line, splits it into at most 4 tokens in place, finds the command in a static table `{name, handler, argc, usage}`, checks the token count, and calls the handler. Implement `rd <addr>`, `wr <addr> <val>` and `help`. `parse_hex()` and `reg_ptr()` are given.

```c
typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
    uint8_t argc;            /* exact token count including the command */
    const char *usage;
} cmd_t;

int shell_exec(char *line);   /* SH_OK, SH_EMPTY, SH_UNKNOWN, SH_ARGC, SH_BADNUM, SH_BADADDR */
```

- `"wr 0x40000004 0xDEADBEEF"` → `SH_OK`; then `"rd 0x40000004"` → `SH_OK`, `g_out == 0xDEADBEEF`
- `"  \t rd   40000004  "` → `SH_OK` (extra blanks, no `0x`)
- `"rdx 0x40000000"` → `SH_UNKNOWN`; `"rd 0x40000002"` → `SH_BADADDR`; `"help me"` → `SH_ARGC`

### 2. 먼저 물어볼 것 (clarify)

- "Is the line mutable, so I can tokenize in place?" → 네 (RX 라인 버퍼).
- "Exact argument count, or optional arguments?" → 이번엔 exact. 옵션 인자는 꼬리 질문.
- "How should a read result be reported — UART print?" → 여기선 `g_out` 에 저장 (target에서는 `uart_puts`).
- "Should the shell reject addresses outside the register window?" → 네, 정렬도.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (mutable? argc 규칙?) |
| 2–5 | 테이블 구조 먼저 쓰기 + 에러 코드 목록 |
| 5–15 | 토큰화 루프 · 테이블 검색 · `cmd_wr` (rd는 "같은 패턴"이라 말하고 짧게) |
| 15–18 | trace `"wr 0x40000004 0xDEADBEEF"` |
| 18–20 | 테스트 + 확장 (help, 옵션 인자, 히스토리) |

- 소리 내서: "Adding a command should be one line in the table — the dispatcher never changes."
- 소리 내서: "I tokenize in place by writing NUL over the separators, so no allocation and no `strtok` static state."

<details><summary>힌트 1 (막히면 5분 후)</summary>

토큰화 루프: 바깥 `for (p = line; *p;)` 안에서 ① 구분자를 `'\0'` 으로 덮으며 건너뛰기 ② 끝이면 break ③ `argv[argc++] = p` ④ 다음 구분자까지 전진. **argc가 MAX에 도달했는데 토큰이 또 있으면** 바로 에러 (배열 overflow 방지).

</details>

<details><summary>힌트 2</summary>

명령 비교는 `strcmp(argv[0], cmds[i].name) == 0` — 토큰이 이미 `'\0'` 으로 끝나니 접두사 문제가 없다 (`strncmp(…, 2)` 를 쓰면 "rdx"가 매칭되는 버그). 테이블 크기는 `sizeof cmds / sizeof cmds[0]`. handler가 테이블보다 아래 정의되면 **전방 선언** 필요.

</details>

<details><summary>모범 답 (C)</summary>

주어진 helper (종이엔 "assume these exist"라고 말하고 시그니처만):

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define MAX_ARGS 4
enum { SH_OK = 0, SH_EMPTY = 1, SH_UNKNOWN = -1, SH_ARGC = -2, SH_BADNUM = -3, SH_BADADDR = -4 };

/* --- fake hardware: 16 registers at 0x40000000 --- */
#define REG_BASE 0x40000000u
static uint32_t fake_regs[16];
uint32_t g_out;                            /* last value "printed" (no printf on target) */

static bool parse_hex(const char *s, uint32_t *v)     /* "0x1F" or "1f"; D4 has the full version */
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (*s == '\0') return false;
    uint32_t r = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9')      d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (r > (UINT32_MAX >> 4)) return false;      /* overflow */
        r = (r << 4) | d;
    }
    *v = r;
    return true;
}

static volatile uint32_t *reg_ptr(uint32_t addr)       /* NULL if outside the window / unaligned */
{
    if (addr < REG_BASE || (addr & 3u) || (addr - REG_BASE) / 4u >= 16u) return NULL;
    return &fake_regs[(addr - REG_BASE) / 4u];
}
```

종이에 쓸 부분:

```c
static int cmd_rd(int argc, char **argv);
static int cmd_wr(int argc, char **argv);
static int cmd_help(int argc, char **argv);

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
    uint8_t argc;                          /* exact argc including the command name */
    const char *usage;
} cmd_t;

static const cmd_t cmds[] = {
    { "rd",   cmd_rd,   2, "rd <addr>"       },
    { "wr",   cmd_wr,   3, "wr <addr> <val>" },
    { "help", cmd_help, 1, "help"            },
};
#define NCMDS (sizeof cmds / sizeof cmds[0])

static int cmd_rd(int argc, char **argv)
{
    (void)argc;
    uint32_t a;
    if (!parse_hex(argv[1], &a)) return SH_BADNUM;
    volatile uint32_t *r = reg_ptr(a);
    if (!r) return SH_BADADDR;
    g_out = *r;
    return SH_OK;
}

static int cmd_wr(int argc, char **argv)
{
    (void)argc;
    uint32_t a, v;
    if (!parse_hex(argv[1], &a) || !parse_hex(argv[2], &v)) return SH_BADNUM;
    volatile uint32_t *r = reg_ptr(a);
    if (!r) return SH_BADADDR;
    *r = v;
    return SH_OK;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_out = (uint32_t)NCMDS;               /* real target: uart_puts(cmds[i].usage) per entry */
    return SH_OK;
}

/* Tokenize line in place (spaces/tabs -> '\0'), look up the command, check argc, call it. */
int shell_exec(char *line)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *p = line; *p; ) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        if (argc == MAX_ARGS) return SH_ARGC;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return SH_EMPTY;
    for (size_t i = 0; i < NCMDS; i++) {
        if (strcmp(argv[0], cmds[i].name) != 0) continue;
        if (argc != cmds[i].argc) return SH_ARGC;
        return cmds[i].fn(argc, argv);
    }
    return SH_UNKNOWN;
}
```

시간 O(len(line) + 명령 수 × 이름 길이) · 공간 O(MAX_ARGS).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

입력 `"wr 0x40000004 0xDEADBEEF"`

| 단계 | 상태 | 결과 |
|---|---|---|
| 토큰화 | 공백 2개 → `'\0'` | argc = 3, argv = "wr", "0x40000004", "0xDEADBEEF" |
| 검색 i=0 | strcmp("wr","rd") ≠ 0 | continue |
| 검색 i=1 | strcmp("wr","wr") == 0, argc 3 == 3 | `cmd_wr` 호출 |
| cmd_wr | a = 0x40000004, v = 0xDEADBEEF, reg_ptr → (4/4)=index 1 | `fake_regs[1] = 0xDEADBEEF`, SH_OK |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: wr 후 rd로 되읽기 (round-trip), 마지막 레지스터 0x4000003C.
- 빈 줄 `""`, 공백만 `"    "` → SH_EMPTY.
- 인자 수: `"rd"`, `"wr 0x40000000"`, `"help me"`, 토큰 5개 → SH_ARGC.
- 모르는 명령 `"peek"`, 접두사 `"rdx"` → SH_UNKNOWN.
- 숫자: `"0x4000000G"`, `"0x"`, 33비트 `"0x100000000"` → SH_BADNUM.
- 주소: 0x40000040(한 칸 밖), 0x40000002(비정렬), 0x3FFFFFFC(아래) → SH_BADADDR.

</details>

### 4. 꼬리 질문

**Q1. How would you support optional arguments, like `rd <addr> [count]`?**

<details><summary>답</summary>

Replace the exact `argc` with `min_args` and `max_args` in the table, and check `argc < min || argc > max`. The handler then checks `argc` itself to decide whether the count was given. `help` can print `usage` from the same table, so the documentation never drifts from the code.

한국어 핵심: 테이블에 min/max argc, handler가 argc로 옵션 판단 · help는 같은 테이블의 usage 출력.

</details>

**Q2. `rd` on a real target hits an address that isn't mapped and the CPU takes a bus fault. What do you do?**

<details><summary>답</summary>

First, the shell validates against a table of allowed regions, as `reg_ptr` does here. For a validation shell that deliberately probes unknown addresses, I'd install a fault handler that sets a flag and skips the faulting instruction or longjmps back to the shell prompt, then report "bus error at address X" instead of hanging the board. On Cortex-M that means checking the BusFault status registers in the handler.

한국어 핵심: 허용 영역 테이블로 1차 차단 + fault handler에서 플래그·복귀 → 보드가 멈추지 않고 "bus error" 출력.

</details>

**Q3. Why `volatile uint32_t *` in `reg_ptr`, and what about 8/16-bit registers?**

<details><summary>답</summary>

`volatile` forces the compiler to do exactly one 32-bit load or store per access, which matters because register reads can have side effects, like clearing a status. Many peripherals require the access width to match the register, so I'd add `rd8`/`rd16` commands, or a width suffix like `rd.w`, that cast to `volatile uint8_t *` or `uint16_t *` — never let the compiler merge or split the access.

한국어 핵심: volatile = 접근 1번·폭 그대로 · 폭이 다른 레지스터는 rd8/rd16 명령으로 폭을 명시.

</details>

### 5. 채점 · 자주 틀리는 것

- `argv` 배열 overflow — MAX_ARGS 도달 후 토큰이 더 있으면 에러.
- `strcmp` 로 정확 비교 (strncmp 접두사 버그).
- 전방 선언 또는 handler 먼저 정의 (테이블이 함수 주소를 쓰니까).
- 주소 검사: 정렬 `addr & 3u`, 범위는 `(addr - BASE) / 4 >= N` — `addr < BASE` 를 먼저 (unsigned 뺄셈 wrap).
- 쓰지 않는 파라미터 `(void)argc;` — `-Werror` 에서 경고 제거.
- `strtok` 을 쓴다면 "not reentrant"를 언급 (`strtok_r` 은 POSIX).

## G5 · 단순화 PCIe 링크 FSM — Detect · Polling · Config · L0 · Recovery (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: 1 ms마다 PHY 상태 입력을 보고 **Detect → Polling → Config → L0** 로 가고, 오류면 **Recovery**, 시간 초과면 **Detect로 retry**, retry가 너무 많으면 **FAIL** 하는 **단순화된** 링크 FSM.

**상황**: 스크린 질문이 "L0 전에 무슨 일이?"였다. 온사이트에서 이걸 C로 모델링하라는 변형이 나올 수 있다 [추정]. 실제 LTSSM은 substate·L0s/L1·equalization·Hot Reset까지 훨씬 복잡하다 — **이 문제는 일부러 단순화한 모델**이고, 진짜 상태도와 타임아웃 수치는 [LP03 링크 트레이닝 · LTSSM](2026-10-31_LP03_link_training_ltssm.html) §2.4–2.5 참고. 면접에선 "This is a simplified model of the LTSSM"이라고 먼저 말하기.

```text
  (simplified - NOT the full LTSSM; see LP03)

             rx_detected            ts_locked             cfg_done (retries=0)
  +--------+ ----------> +---------+ --------> +--------+ ---------> +------+
  | DETECT |             | POLLING |           | CONFIG |            |  L0  |
  +--------+             +---------+           +--------+            +------+
   ^  ^  ^                    |                    |                  |  ^
   |  |  |   24 ms timeout    |                    |      link_err    |  | rcv_done
   |  |  +--------------------+                    |   (recoveries++) v  |
   |  |        24 ms timeout                       |              +----------+
   |  +--------------------------------------------+              | RECOVERY |
   |                   24 ms timeout                              +----------+
   +--------------------------------------------------------------------+

  every timeout: retries++ ; if retries > 3 -> FAIL (sticky until link_init)
  DETECT itself has no timeout (empty slot = keep detecting forever)
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `link_init(l)` | 상태 DETECT, 카운터 0 | |
| `link_step(l, in)` | 1 ms 틱마다 1번 호출, 새 상태 반환 | 상태에 들어온 뒤 경과 ms를 `ms_in_state` 로 셈 · Polling/Config/Recovery는 24번째 틱에 timeout · timeout마다 `retries++`, 3 초과면 FAIL · L0 도달 시 `retries = 0` · L0→Recovery마다 `recoveries++` |

**예시를 말로 풀면**: 카드가 꽂혀 첫 틱에 `rx_detected` → POLLING, 다음 틱 `ts_locked` → CONFIG, 다음 틱 `cfg_done` → L0. 반대로 Polling에서 TS를 못 받으면 24 ms 뒤 DETECT로 돌아가고 retries 1. 네 번째 실패에서 FAIL.

**지켜야 할 조건**: 현재 상태와 관계없는 입력은 무시 (예: Config에서 `ts_locked`) · FAIL은 다시 init할 때까지 유지 · 틱당 O(1).

### 1. Problem (English)

Model a **simplified** PCIe link-training state machine in C, stepped by a 1 ms timer. The PHY gives you status flags each tick. States: `DETECT → POLLING → CONFIG → L0`; from `L0`, a link error goes to `RECOVERY`, and a successful retrain returns to `L0`. `POLLING`, `CONFIG` and `RECOVERY` each time out after 24 ms in the state and fall back to `DETECT`. After more than 3 fallbacks without reaching L0, enter a sticky `FAIL` state. Reaching L0 clears the retry count. Count L0→Recovery transitions as a link-health metric.

```c
typedef enum { L_DETECT, L_POLLING, L_CONFIG, L_L0, L_RECOVERY, L_FAIL } lstate_t;
typedef struct { bool rx_detected, ts_locked, cfg_done, link_err, rcv_done; } link_in_t;
typedef struct { lstate_t st; uint32_t ms_in_state; uint8_t retries; uint32_t recoveries; } link_t;

void     link_init(link_t *l);
lstate_t link_step(link_t *l, const link_in_t *in);
```

- ticks with `{rx_detected}`, `{ts_locked}`, `{cfg_done}` → `POLLING`, `CONFIG`, `L0`
- enter POLLING, then 23 empty ticks → still `POLLING`; 24th → `DETECT`, `retries == 1`
- 4 polling timeouts in a row → `FAIL`, and it stays `FAIL` even with all flags set

### 2. 먼저 물어볼 것 (clarify)

- "Is this meant to be the full LTSSM or a simplified model?" → 단순화 모델 (substate, L0s/L1, EQ 없음).
- "Is the timeout per state or for the whole training?" → 상태별 24 ms.
- "Should Detect time out if no card is present?" → 아니요, 빈 슬롯은 영원히 Detect.
- "Does a successful L0 reset the retry counter?" → 네.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | "simplified" 확인 + timeout/retry 규칙 clarify |
| 2–5 | 상태도 그리기 (위 그림 축소판) |
| 5–15 | `go()` · `fall_back()` helper → switch |
| 15–18 | trace: Polling timeout 경계 (23 vs 24번째 틱) |
| 18–20 | 테스트 + "real LTSSM differs in …" 한 문장 |

- 소리 내서: "I keep one time-in-state counter and reset it in a single `go()` helper, so every transition restarts the timeout — I can't forget it in one branch."
- 소리 내서: "All timeout paths go through `fall_back()`, which owns the retry limit."

<details><summary>힌트 1 (막히면 5분 후)</summary>

helper 두 개로 중복을 없애자: `go(l, s)` = 상태 바꾸고 `ms_in_state = 0`. `fall_back(l)` = `++retries > MAX_RETRY` 면 FAIL, 아니면 DETECT. 그러면 switch의 각 case는 "성공 조건이면 go, 아니고 timeout이면 fall_back" 두 줄.

</details>

<details><summary>힌트 2</summary>

틱 시작에 `ms_in_state++` 하고 `tmo = ms_in_state >= 24`. 상태에 들어온 틱에 0으로 리셋되니, 들어온 뒤 **24번째 호출**에서 timeout. 성공 조건을 timeout보다 **먼저** 검사하면 "24번째 틱에 딱 성공"이 성공으로 처리된다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

typedef enum { L_DETECT, L_POLLING, L_CONFIG, L_L0, L_RECOVERY, L_FAIL } lstate_t;

typedef struct {               /* inputs sampled each 1 ms tick (from PHY/MAC status) */
    bool rx_detected;          /* far-end receiver termination seen        */
    bool ts_locked;            /* Polling: enough TS1/TS2 received         */
    bool cfg_done;             /* Config: link/lane numbers agreed         */
    bool link_err;             /* in L0: error or speed-change request     */
    bool rcv_done;             /* Recovery: retrained OK                   */
} link_in_t;

typedef struct {
    lstate_t st;
    uint32_t ms_in_state;
    uint8_t  retries;          /* fallbacks to Detect since last L0 */
    uint32_t recoveries;       /* L0 -> Recovery count (link health metric) */
} link_t;

#define T_STATE_MS 24u         /* simplified: one timeout for Polling/Config/Recovery */
#define MAX_RETRY  3u

static void go(link_t *l, lstate_t s) { l->st = s; l->ms_in_state = 0; }

static void fall_back(link_t *l)      /* timeout: back to Detect, or give up */
{
    if (++l->retries > MAX_RETRY) go(l, L_FAIL);
    else                          go(l, L_DETECT);
}

void link_init(link_t *l) { *l = (link_t){0}; go(l, L_DETECT); }

lstate_t link_step(link_t *l, const link_in_t *in)    /* call every 1 ms */
{
    l->ms_in_state++;
    bool tmo = l->ms_in_state >= T_STATE_MS;
    switch (l->st) {
    case L_DETECT:                                    /* no timeout: keep looking for a card */
        if (in->rx_detected) go(l, L_POLLING);
        break;
    case L_POLLING:
        if (in->ts_locked) go(l, L_CONFIG);
        else if (tmo)      fall_back(l);
        break;
    case L_CONFIG:
        if (in->cfg_done) { go(l, L_L0); l->retries = 0; }
        else if (tmo)       fall_back(l);
        break;
    case L_L0:
        if (in->link_err) { go(l, L_RECOVERY); l->recoveries++; }
        break;
    case L_RECOVERY:
        if (in->rcv_done) go(l, L_L0);
        else if (tmo)     fall_back(l);
        break;
    case L_FAIL:                                      /* sticky until link_init() */
        break;
    }
    return l->st;
}
```

시간 O(1)/틱 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

Polling timeout 경계 (MAX_RETRY = 3)

| 틱 | 입력 | 상태(전) | ms_in_state | 상태(후) | retries |
|---|---|---|---|---|---|
| 1 | rx_detected | DETECT | 1 → go → 0 | POLLING | 0 |
| 2 | 없음 | POLLING | 1 | POLLING | 0 |
| … | 없음 | POLLING | … | POLLING | 0 |
| 24 | 없음 | POLLING | 23 | POLLING | 0 |
| 25 | 없음 | POLLING | 24 → tmo | DETECT | 1 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 3틱 만에 L0, 1000틱 동안 L0 유지.
- 빈 슬롯: 입력 없음 100틱 → DETECT 그대로 (FAIL 아님).
- 경계: Polling 23틱 → POLLING, 24틱 → DETECT (retries 1).
- retry 한계: timeout 3번은 DETECT, 4번째 → FAIL, 그 뒤 모든 플래그 켜도 FAIL.
- Recovery: L0 → link_err → RECOVERY(recoveries 1) → rcv_done → L0; Recovery timeout → DETECT, 다시 L0 도달 시 retries 0.
- 상관없는 입력: Config에서 `ts_locked` 만 계속 → 24틱 후 DETECT.

</details>

### 4. 꼬리 질문

**Q1. How is the real LTSSM different from your model?**

<details><summary>답</summary>

The real one has substates — Detect.Quiet and Active, Polling.Active and Configuration, several Configuration and Recovery substates — and different timeouts, for example 12 ms in Detect.Quiet and 24 ms in Polling.Active. It always reaches L0 at 2.5 GT/s first and then uses Recovery to change speed, with equalization phases at 8 GT/s and above. It also has L0s, L1 and L2 power states, Hot Reset, Disabled and Loopback. The structure — state plus timer plus fallback to Detect — is the same as my model.

한국어 핵심: substate·타임아웃 값·2.5 GT/s 첫 진입·Recovery 속도 변경·EQ·PM 상태가 빠진 단순화 — 뼈대(상태+타이머+Detect 복귀)는 같다. 자세히는 LP03.

</details>

**Q2. In validation, how would you use this kind of FSM model?**

<details><summary>답</summary>

As a reference model or checker: I'd sample the controller's LTSSM debug register, for example the DesignWare LTSSM state field, every few microseconds, and feed the trace into a checker that flags illegal transitions, time spent in each state, and how many Recovery entries happen per second in L0. A rising Recovery count with a stable L0 is a classic sign of marginal signal integrity.

한국어 핵심: vendor LTSSM 레지스터를 주기적으로 샘플 → 불법 전이·상태별 시간·L0 중 Recovery 빈도 체크 (Recovery 증가 = SI 마진 부족 신호).

</details>

**Q3. Your link keeps going L0 → Recovery → L0 every few milliseconds, and your retry counter never trips. Fix the model.**

<details><summary>답</summary>

Because reaching L0 resets `retries`, a flapping link is never declared failed. I'd add a rate limit: count Recovery entries in a sliding window, for example more than 10 in 100 ms, and report a "link unstable" error even though it technically recovers. In real systems the equivalent signals are the Link Bandwidth Management Status bit and the AER correctable error counters.

한국어 핵심: L0에서 retries가 리셋돼 flapping을 못 잡음 → 시간 창 안의 recovery 횟수로 "불안정" 판정.

</details>

### 5. 채점 · 자주 틀리는 것

- "simplified"라고 먼저 말했는가 (LTSSM을 다 아는 척하면 꼬리 질문에서 무너짐).
- 상태 바뀔 때 `ms_in_state = 0` — helper 하나로 (한 분기에서 빠뜨리는 실수 방지).
- 성공 조건을 timeout보다 먼저 검사.
- retry 한계 off-by-one: "more than 3" = `++retries > 3` → 4번째 실패에서 FAIL.
- FAIL은 sticky, Detect는 timeout 없음 — 규칙을 clarify에서 확정.
- 32-bit tick이면 `ms_in_state` 는 상태 안 경과 시간이라 wrap 걱정 없음 (절대 시각 비교와 구분).

## G6 · 테이블 기반 FSM — 전원 상태 머신 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: switch 대신 **`table[state][event] = {next, action}` 2차원 배열**로 전원 상태 머신을 만들고, `fsm_dispatch()` 하나로 이벤트를 처리한다.

**상황**: SoC/PMIC 전원 관리(Active → Idle → Sleep), 센서 모드 전환, 프로토콜 스택이 테이블 FSM을 많이 쓴다. 장점: 전이 전체가 **한눈에 보이는 데이터**라서 리뷰·테스트(모든 (state, event) 조합 순회)가 쉽고, 코드는 몇 줄로 고정.

```text
                      PWR_ON / power_up
        +-------+ ---------------------------> +--------+
        |  OFF  |                              | ACTIVE |
        +-------+ <--------------------------- +--------+
          ^   ^    PWR_OFF / power_down          |    ^
          |   |    FAULT   / emergency_off       |    |
          |   |                       NO_ACT/dim |    | ACT / wake
          |   |                                  v    |
          |   |  PWR_OFF, FAULT                +--------+
          |   +------------------------------- |  IDLE  |
          |                                    +--------+
          |                                        | TIMER / suspend
          |  PWR_OFF, FAULT                        v
          |                                    +--------+   ACT / resume
          +----------------------------------- | SLEEP  | ---------------> ACTIVE
                                               +--------+
   anything not drawn = ignored (no action, ignored++)
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `table[S_COUNT][E_COUNT]` | 전이 표 (const) | 빈 칸 = 무시 |
| `fsm_dispatch(f, ev)` | 이벤트 1개 처리 | 전이 있으면 action 실행 → 상태 변경 → true · 없으면 `ignored++`, false · 잘못된 state/event id도 무시 처리 |

**예시를 말로 풀면**: OFF에서 PWR_ON → power_up 실행, ACTIVE. NO_ACT → dim, IDLE. TIMER → suspend, SLEEP. ACT → resume, ACTIVE. OFF에서 TIMER는 표에 없으니 무시.

**지켜야 할 조건**: 전이 로직에 switch/if 체인 없이 표 조회만 · action은 NULL일 수 있음 · 범위 밖 인덱스로 배열 접근 금지.

### 1. Problem (English)

Implement a device power-state machine as a 2-D transition table indexed by `[state][event]`, where each entry holds the next state and an action function pointer. `fsm_dispatch` looks up the entry, runs the action (if any), then updates the state. Events with no entry in the current state are ignored and counted. Range-check the state and event before indexing.

```c
typedef enum { S_NONE = 0, S_OFF, S_ACTIVE, S_IDLE, S_SLEEP, S_COUNT } pstate_t;
typedef enum { E_PWR_ON, E_PWR_OFF, E_NO_ACT, E_ACT, E_TIMER, E_FAULT, E_COUNT } pevent_t;
typedef void (*action_fn)(void);
typedef struct { pstate_t next; action_fn act; } trans_t;
typedef struct { pstate_t st; uint32_t ignored; } fsm_t;

bool fsm_dispatch(fsm_t *f, pevent_t ev);
```

- from `OFF`: `PWR_ON, NO_ACT, TIMER, ACT, NO_ACT, ACT, PWR_OFF` → actions `power_up, dim, suspend, resume, dim, wake, power_down`, final `OFF`
- `OFF` + `TIMER` → `false`, state stays `OFF`, `ignored == 1`
- `SLEEP` + `FAULT` → `emergency_off`, `OFF`

### 2. 먼저 물어볼 것 (clarify)

- "Should the action run before or after the state changes?" → 전이 전 (action이 이전 상태를 볼 수 있게).
- "What should happen on an event that isn't valid in the current state — ignore, assert, or error state?" → 무시 + 카운트.
- "Can actions post new events?" → 이번엔 아니요 (꼬리 질문: 이벤트 큐).
- "Is the table allowed to be const in flash?" → 네.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (action 순서, 무효 이벤트) |
| 2–5 | 상태도 → 표로 옮기기 (행 = state, 열 = event) |
| 5–15 | enum · trans_t · designated initializer 표 · dispatch |
| 15–18 | trace 예시 4이벤트 |
| 18–20 | 테스트 (무시 이벤트, 범위 밖 id) + 확장 |

- 소리 내서: "I make enum value 0 a 'no transition' sentinel, so every entry I don't write in the table is automatically 'ignore' — zero-initialized."
- 소리 내서: "The behavior is all data; the dispatcher is five lines and never changes when we add a state."

<details><summary>힌트 1 (막히면 5분 후)</summary>

C의 **designated initializer** `[S_OFF] = { [E_PWR_ON] = { S_ACTIVE, act_power_up } }` 를 쓰면 나머지 칸은 0으로 채워진다. 그래서 상태 enum의 0번을 `S_NONE`(= 전이 없음)으로 만들어 두면 빈 칸이 자동으로 "무시"가 된다. `S_OFF = 0` 이면 빈 칸이 전부 "OFF로 가라"가 되는 버그.

</details>

<details><summary>힌트 2</summary>

dispatch 순서: ① `f->st` 와 `ev` 범위 검사 (손상된 RAM 값으로 배열 밖을 읽지 않게) ② `t = &table[st][ev]` ③ `t->next == S_NONE` 이면 무시 ④ `if (t->act) t->act();` ⑤ `f->st = t->next`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { S_NONE = 0, S_OFF, S_ACTIVE, S_IDLE, S_SLEEP, S_COUNT } pstate_t;  /* 0 = "no transition" */
typedef enum { E_PWR_ON, E_PWR_OFF, E_NO_ACT, E_ACT, E_TIMER, E_FAULT, E_COUNT } pevent_t;

typedef void (*action_fn)(void);
typedef struct { pstate_t next; action_fn act; } trans_t;

void act_power_up(void);  void act_power_down(void);  void act_dim(void);
void act_wake(void);      void act_suspend(void);     void act_resume(void);
void act_emergency_off(void);

/* Missing entries are zero = {S_NONE, NULL} = event ignored in that state. */
static const trans_t table[S_COUNT][E_COUNT] = {
    [S_OFF]    = { [E_PWR_ON]  = { S_ACTIVE, act_power_up } },
    [S_ACTIVE] = { [E_NO_ACT]  = { S_IDLE,   act_dim },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
    [S_IDLE]   = { [E_ACT]     = { S_ACTIVE, act_wake },
                   [E_TIMER]   = { S_SLEEP,  act_suspend },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
    [S_SLEEP]  = { [E_ACT]     = { S_ACTIVE, act_resume },
                   [E_PWR_OFF] = { S_OFF,    act_power_down },
                   [E_FAULT]   = { S_OFF,    act_emergency_off } },
};

typedef struct { pstate_t st; uint32_t ignored; } fsm_t;

/* Returns true if the event caused a transition. */
bool fsm_dispatch(fsm_t *f, pevent_t ev)
{
    if (f->st <= S_NONE || f->st >= S_COUNT || (unsigned)ev >= E_COUNT) {
        f->ignored++;                         /* corrupt state or bad event id */
        return false;
    }
    const trans_t *t = &table[f->st][ev];
    if (t->next == S_NONE) { f->ignored++; return false; }
    if (t->act) t->act();                     /* action runs before state changes */
    f->st = t->next;
    return true;
}
```

시간 O(1)/이벤트 · 공간 O(S × E) 표 (const → flash).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 이벤트 | 상태(전) | table[st][ev] | action | 상태(후) | 반환 |
|---|---|---|---|---|---|
| PWR_ON | OFF | {ACTIVE, power_up} | power_up | ACTIVE | true |
| NO_ACT | ACTIVE | {IDLE, dim} | dim | IDLE | true |
| TIMER | IDLE | {SLEEP, suspend} | suspend | SLEEP | true |
| TIMER | SLEEP | {NONE, NULL} | – | SLEEP | false, ignored 1 |
| ACT | SLEEP | {ACTIVE, resume} | resume | ACTIVE | true |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 life cycle: 7이벤트 → action 로그 `"UisriwD"`, ignored 0.
- 무시: OFF+TIMER, OFF+FAULT, ACTIVE+PWR_ON(중복), ACTIVE+TIMER → 상태 그대로, action 없음, ignored 4.
- FAULT: SLEEP에서 → emergency_off, OFF.
- 범위 밖: `ev = E_COUNT`, `st = S_NONE`, `st = 99` → false, 배열 접근 없음 (ASan 통과).
- 표 전수 검사: 모든 (state, event)를 돌려 next가 `S_NONE` 이거나 유효 상태인지 — 테이블 FSM의 장점.

</details>

### 4. 꼬리 질문

**Q1. Table vs switch — when would you choose each?**

<details><summary>답</summary>

A table is best when the state-event matrix is dense and uniform: it's compact, reviewable as data, and easy to test exhaustively. A switch is better when transitions need guards — conditions like "only if battery above 10 percent" — or when each state has very different logic, like my link FSM with timers. A common hybrid is a table whose entries include a guard function pointer.

한국어 핵심: 조밀·균일하면 표, 조건(guard)·상태별 로직이 다르면 switch — 절충은 표에 guard 함수 포인터 추가.

</details>

**Q2. An action needs to raise another event, like suspend failing and wanting to go back to ACTIVE. How?**

<details><summary>답</summary>

Don't call `fsm_dispatch` recursively from inside an action, because the state hasn't been updated yet and you can get re-entrancy bugs. Instead the action posts the event to a small queue, and the main loop dispatches queued events one at a time after the current transition finishes — run-to-completion semantics, which is how most RTOS state-machine frameworks work.

한국어 핵심: action 안에서 재귀 dispatch 금지 → 이벤트 큐에 넣고 현재 전이 완료 후 처리 (run-to-completion).

</details>

**Q3. Events come from ISRs — power button IRQ, timer IRQ. Is `fsm_dispatch` ISR-safe?**

<details><summary>답</summary>

No: two contexts dispatching at once can interleave the read of `f->st` and the write, and actions like power-down shouldn't run in interrupt context anyway. I'd have the ISRs only set event flags or push to an SPSC queue, and run the FSM from one thread. That keeps the FSM single-threaded, and the ISR side is just the event-flag pattern from E6.

한국어 핵심: dispatch는 한 컨텍스트에서만 — ISR은 이벤트 플래그/큐에 넣기만 (E6 패턴).

</details>

### 5. 채점 · 자주 틀리는 것

- **enum 0 = sentinel** — 안 하면 빈 칸이 "OFF로 전이"가 됨.
- 인덱스 범위 검사 (`(unsigned)ev >= E_COUNT`, state 하한/상한).
- `if (t->act)` NULL 체크.
- action 먼저, 상태 변경 나중 (또는 clarify에서 정한 순서를 일관되게).
- 표는 `static const` — RAM 절약 + 실수로 덮어쓰기 방지.

## G7 · 센서 프레임 시퀀스 체커 — 16-bit 카운터 gap · 중복 · wrap (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: 프레임마다 붙은 **16-bit 카운터**를 하나씩 받아서 (스트리밍) **유실(lost) · 중복(dup) · 늦게 온 프레임(ooo) · 센서 리셋(resync)** 을 센다. 65535 → 0 wrap은 정상.

**상황**: 카메라(CSI-2 frame number), 레이더/IMU 패킷, DMA 디스크립터 시퀀스 번호 — 센서 validation의 기본 체커. **이 문제는 카메라 1대 스트림 + 리셋 감지**에 집중한다. 타임스탬프 jitter와 여러 카메라 sync까지 하는 큰 버전은 [hmScreenPrep V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) (30분짜리, 겹치지 않게 여기선 생략).

```text
  d = (cnt - prev) mod 65536         (prev = baseline)

  d:   0        1 .. 1024          1025 ............. 65527   65528 .. 65535
       |  dup  |  in order:       |      resync:            |  ooo: late frame,
       |       |  lost += d-1,    |      re-baseline,        |  back = 1..8,
       |       |  prev = cnt      |      no "lost" counted   |  prev unchanged
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `seq_init(s)` | 통계 0, baseline 없음 | |
| `seq_feed(s, cnt)` | 프레임 1개 | 첫 프레임은 baseline만 · 앞으로 1..1024칸 = 정상(+유실) · 0칸 = dup · 뒤로 1..8칸 = ooo · 그 외 = resync |

**예시를 말로 풀면**: `10 11 11 13 12 14` → 11 중복 1, 11→13에서 12 유실 1, 12는 뒤로 1칸이라 ooo 1, 14는 정상. `65534 65535 0 1 4` → wrap은 정상, 1→4에서 2개 유실. `4999 5000 0 1 2` → 5000→0은 "60535개 유실"이 아니라 **센서 리셋** (resync 1).

**지켜야 할 조건**: 프레임 저장 없이 O(1) 상태 · `frames` 는 받은 개수 전부 · ooo/dup는 baseline을 바꾸지 않음 · resync는 baseline을 새 값으로.

### 1. Problem (English)

A sensor stamps every frame with a 16-bit counter that wraps from 65535 to 0. Write a streaming checker that is fed one counter at a time. With `d = (cnt − prev) mod 2^16`: `d == 0` is a duplicate; `1 ≤ d ≤ 1024` is in order and `d − 1` frames were lost; a backward step of 1..8 is a late (out-of-order) frame and keeps the baseline; anything else is a resync (sensor reset or counter jump) — re-baseline, don't count losses. The first frame only sets the baseline.

```c
#define MAX_GAP     1024u
#define REORDER_WIN 8u
typedef struct { bool have_prev; uint16_t prev; uint32_t frames, lost, dup, ooo, resync; } seq_t;

void seq_init(seq_t *s);
void seq_feed(seq_t *s, uint16_t cnt);
```

- `10 11 11 13 12 14` → `lost 1, dup 1, ooo 1, resync 0`, baseline 14
- `65534 65535 0 1 4` → `lost 2` (the wrap itself is clean); `65533 2` → `lost 4`
- `4999 5000 0 1 2` → `resync 1, lost 0`

### 2. 먼저 물어볼 것 (clarify)

- "How far ahead is still a gap rather than a reset — is there a max expected gap?" → 1024.
- "Can frames arrive out of order, and how late?" → 최대 8칸 늦게.
- "If a frame I counted as lost arrives late, should I decrement `lost`?" → 아니요, `lost` 는 상한값(upper bound)으로 둠.
- "Does the counter start at 0 after reset?" → 보통 그렇지만 아무 값이나 가능 — 첫 프레임을 baseline으로.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (MAX_GAP, reorder window, lost 보정?) |
| 2–5 | 위 숫자선 그림 + `(uint16_t)` 뺄셈이 왜 wrap을 해결하나 |
| 5–13 | struct · init · feed (if/else 4갈래) |
| 13–17 | trace `10 11 11 13 12 14` |
| 17–20 | 테스트 (wrap, gap over wrap, reset, 경계 1024/1025, 8/9) |

- 소리 내서: "I compute the difference in `uint16_t`, so 65535 to 0 is just a step of one — no special case for the wrap."
- 소리 내서: "The thresholds split the 16-bit circle into four regions: duplicate, forward gap, late frame, and reset."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`uint16_t fwd = (uint16_t)(cnt - prev);` 와 `uint16_t back = (uint16_t)(prev - cnt);` 두 개를 다 구해 두면 조건이 단순해진다. `cnt - prev` 는 int로 승격되므로 **반드시 `(uint16_t)` 로 다시 자르기**.

</details>

<details><summary>힌트 2</summary>

판정 순서: `fwd == 0` → dup · `fwd <= MAX_GAP` → `lost += fwd - 1`, baseline 갱신 · `back <= REORDER_WIN` → ooo · 나머지 → resync, baseline 갱신. ooo를 forward보다 먼저 검사하면 안 되는 이유는 없지만, forward가 흔한 경우라 먼저 두면 읽기 쉽다.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

#define MAX_GAP     1024u   /* forward jump > this = counter reset/jump, not "lost frames" */
#define REORDER_WIN 8u      /* backward step <= this = late (out-of-order) frame */

typedef struct {
    bool     have_prev;
    uint16_t prev;          /* baseline = highest in-order counter so far */
    uint32_t frames, lost, dup, ooo, resync;
} seq_t;

void seq_init(seq_t *s) { *s = (seq_t){0}; }

void seq_feed(seq_t *s, uint16_t cnt)
{
    s->frames++;
    if (!s->have_prev) { s->have_prev = true; s->prev = cnt; return; }

    uint16_t fwd  = (uint16_t)(cnt - s->prev);    /* wrap-safe: 65535 -> 0 gives 1 */
    uint16_t back = (uint16_t)(s->prev - cnt);

    if (fwd == 0) {
        s->dup++;                                 /* same frame twice */
    } else if (fwd <= MAX_GAP) {
        s->lost += fwd - 1u;                      /* 0 when fwd == 1 */
        s->prev = cnt;
    } else if (back <= REORDER_WIN) {
        s->ooo++;                                 /* late frame: keep baseline */
    } else {
        s->resync++;                              /* sensor reset / big jump: re-baseline */
        s->prev = cnt;
    }
}
```

시간 O(1)/프레임 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

입력 `10 11 11 13 12 14`

| cnt | prev(전) | fwd | back | 판정 | lost · dup · ooo | prev(후) |
|---|---|---|---|---|---|---|
| 10 | – | – | – | 첫 프레임 | 0 · 0 · 0 | 10 |
| 11 | 10 | 1 | 65535 | 정상 | 0 · 0 · 0 | 11 |
| 11 | 11 | 0 | 0 | dup | 0 · 1 · 0 | 11 |
| 13 | 11 | 2 | 65534 | gap | 1 · 1 · 0 | 13 |
| 12 | 13 | 65535 | 1 | ooo | 1 · 1 · 1 | 13 |
| 14 | 13 | 1 | 65535 | 정상 | 1 · 1 · 1 | 14 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 `100 101 102 103` → 모두 0.
- wrap `65534 65535 0 1` → 0 유실; gap across wrap `65533 → 2` → 4 유실.
- dup + ooo 예시 → 각 1.
- 리셋 `5000 → 0` → resync 1, lost 0 (리셋 없이 짜면 lost 60535가 나오는 버그).
- 경계: fwd 1024 → lost 1023; 1025 → resync; back 8 → ooo; 9 → resync.
- 빈 스트림(frames 0) · 프레임 1개 (lost 0).

</details>

### 4. 꼬리 질문

**Q1. A late frame arrives that you already counted as lost. How would you correct `lost`?**

<details><summary>답</summary>

Keep a small bitmap of the last 8 or 32 counters behind the baseline, set a bit for each one received. When a late frame arrives and its bit is clear, decrement `lost` and set the bit; if the bit is already set, it's a duplicate. That's exactly the sliding-window replay check that IPsec and DTLS use, and it's one `uint32_t` of state.

한국어 핵심: baseline 뒤 W칸 비트맵 — 늦게 온 프레임이 미수신 비트면 `lost--`, 이미 있으면 dup (IPsec anti-replay 창과 같은 구조). baseline이 전진할 때 `win <<= fwd` 로 창을 미는 부분은 생략한 스케치 `[미검증]`.

```c
/* bit k of win = (prev - k) was received */
uint32_t bit = 1u << back;            /* back = 1..31 */
if (win & bit) s->dup++; else { win |= bit; s->lost--; s->ooo++; }
```

</details>

**Q2. Why not just use a 32-bit counter so wrap never matters?**

<details><summary>답</summary>

The counter width is set by the hardware or the protocol — CSI-2 frame numbers and many packet headers are 16 bits or less — so the checker has to handle wrap. You can extend it to 32 or 64 bits in software by adding the wrap-safe delta to a running total each frame, which gives you a monotonic frame index for logging.

한국어 핵심: 폭은 HW/프로토콜이 정함 → 소프트웨어에서 `total += fwd` 로 64-bit 확장 카운터를 만들면 됨.

</details>

**Q3. Frames come from DMA at 120 fps and this runs in the frame-done ISR. Anything to change?**

<details><summary>답</summary>

It's already O(1) and allocation-free, so it's fine in an ISR. The stats are 32-bit and written only by the ISR; if the main loop reads several fields for a report, I'd take a snapshot with interrupts briefly disabled, or double-buffer the struct, so the fields are consistent with each other. That's the tearing issue from E7.

한국어 핵심: ISR에서 OK — main이 여러 필드를 읽을 땐 IRQ 잠깐 끄고 snapshot (E7 tearing).

</details>

### 5. 채점 · 자주 틀리는 것

- `(uint16_t)(cnt - prev)` 캐스트 — 없으면 int로 승격돼 wrap에서 음수.
- 첫 프레임 처리 (`have_prev`) — 안 하면 prev = 0 대비 큰 gap/resync가 하나 생김.
- dup/ooo에서 baseline을 바꾸지 않기 (바꾸면 다음 정상 프레임이 gap으로 보임).
- 리셋 감지 — "카운터가 뒤로 크게 감 = 수만 개 유실"로 세면 validation 리포트가 엉터리.
- `lost += fwd - 1u` 는 fwd ≥ 1에서만 (fwd == 0을 먼저 걸렀으니 underflow 없음).

## G8 · 고정 형식 로그 라인 파서 "T=123 V=0x1F" (★ · 15분)

### 0. 문제 한국어로 먼저 보기

**한 줄 요약**: `"T=<10진수> V=0x<16진수>"` 한 줄을 `sscanf` 없이 **포인터를 앞으로 밀면서** 읽어 `{t, v}` 구조체로 만든다. 형식이 틀리거나 숫자가 32비트를 넘으면 에러.

**상황**: 테스트 장비·DUT 콘솔 로그를 bare-metal 쪽이나 작은 호스트 도구에서 파싱할 때. `sscanf` 는 코드 크기가 크고, `%u` 는 오버플로를 알려주지 않으며, 뒤에 붙은 쓰레기도 그냥 통과시킨다.

```text
   "T=" digits+  ' '+  "V=0x" hexdigits+  ["\r"] ["\n"]  '\0'
    ^     ^        ^      ^        ^                       ^
   lit   dec     skip    lit      hex                  must end here
```

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `parse_num(&p, base, &out)` (helper) | p 위치의 숫자를 읽고 p 전진 | 숫자 0개 → `LP_FORMAT` · 32비트 초과 → `LP_RANGE` |
| `parse_line(s, r)` | 한 줄 전체 | 위 형식 정확히 · 공백 1개 이상 · `0x`/`0X` 둘 다 · 끝에 `\r\n` 또는 `\n` 허용 · 그 뒤 쓰레기 → `LP_FORMAT` |

**예시를 말로 풀면**: `"T=42   V=0XdeadBEEF\r\n"` → t = 42, v = 0xDEADBEEF (공백 여러 개, 대소문자 섞임 OK). `"T=4294967296 V=0x1"` → 2^32라 `LP_RANGE`. `"T=12 V=0x1G"` → G가 남아서 `LP_FORMAT`.

**지켜야 할 조건**: `<stdint.h>` 정도만, 라이브러리 함수 없이 · 오버플로를 **곱하기 전에** 검사 · 순서가 바뀐 필드(`V=… T=…`)는 에러.

### 1. Problem (English)

A test station logs lines like `"T=123 V=0x1F"`: a decimal timestamp and a hex value. Write `parse_line` without `sscanf` or `strtoul`. Accept one or more spaces between the fields, `0x` or `0X`, upper/lower-case hex digits, and an optional `\r\n` or `\n` at the end. Return `LP_RANGE` if a number doesn't fit in `uint32_t`, `LP_FORMAT` for any other deviation, including trailing characters.

```c
typedef struct { uint32_t t; uint32_t v; } rec_t;
enum { LP_OK = 0, LP_FORMAT = -1, LP_RANGE = -2 };

int parse_line(const char *s, rec_t *r);
```

- `"T=123 V=0x1F"` → `LP_OK`, `{123, 0x1F}`
- `"T=4294967295 V=0xFFFFFFFF"` → `LP_OK` (both max); `"T=4294967296 V=0x1"` → `LP_RANGE`
- `"T=12V=0x1"` → `LP_FORMAT` (no space); `"T=12 V=0x1G"` → `LP_FORMAT` (trailing junk)

### 2. 먼저 물어볼 것 (clarify)

- "Is the field order fixed?" → 네, T 먼저.
- "Can there be leading whitespace or a sign on T?" → 아니요, 둘 다 에러.
- "Should overflow be an error, or saturate?" → 에러 (`LP_RANGE`).
- "Is the line NUL-terminated?" → 네.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (순서·공백·오버플로) |
| 2–4 | 형식 그림 (위 text 블록) |
| 4–11 | `parse_num` (base 10/16 공용) → `parse_line` |
| 11–13 | trace `"T=123 V=0x1F"` |
| 13–15 | 테스트 (max, max+1, 빈 숫자, 쓰레기) |

- 소리 내서: "I write one number helper for both bases that advances the pointer, so the line parser is just literal checks and two calls."
- 소리 내서: "I check `v > (UINT32_MAX - d) / base` before multiplying, so the overflow is detected, not wrapped."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`static int parse_num(const char **pp, unsigned base, uint32_t *out)` — **포인터의 포인터**를 받아서 읽은 만큼 호출자의 포인터를 전진시킨다. 숫자 개수 `nd` 를 세서 0이면 `LP_FORMAT`.

</details>

<details><summary>힌트 2</summary>

리터럴은 `s[0] != 'T' || s[1] != '='` 처럼 **순서대로 비교** — `||` 의 short-circuit 덕에 문자열이 짧아도 `'\0'` 에서 멈춰 범위 밖을 읽지 않는다. 마지막에 `\r`, `\n` 을 하나씩 선택적으로 넘기고 `*s == '\0'` 인지 확인.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t t; uint32_t v; } rec_t;
enum { LP_OK = 0, LP_FORMAT = -1, LP_RANGE = -2 };

/* Parse digits at *pp in base 10 or 16, advance *pp. Returns LP_* code. */
static int parse_num(const char **pp, unsigned base, uint32_t *out)
{
    const char *p = *pp;
    uint32_t v = 0;
    int nd = 0;
    for (;; p++, nd++) {
        unsigned d;
        char c = *p;
        if (c >= '0' && c <= '9')                    d = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else break;
        if (v > (UINT32_MAX - d) / base) return LP_RANGE;   /* v*base + d would overflow */
        v = v * base + d;
    }
    if (nd == 0) return LP_FORMAT;
    *pp = p; *out = v;
    return LP_OK;
}

/* Format: "T=<dec>" one-or-more spaces "V=0x<hex>" then optional "\r\n" / "\n". */
int parse_line(const char *s, rec_t *r)
{
    int rc;
    if (s[0] != 'T' || s[1] != '=') return LP_FORMAT;
    s += 2;
    if ((rc = parse_num(&s, 10, &r->t)) != LP_OK) return rc;
    if (*s != ' ') return LP_FORMAT;
    while (*s == ' ') s++;
    if (s[0] != 'V' || s[1] != '=' || s[2] != '0' || (s[3] != 'x' && s[3] != 'X')) return LP_FORMAT;
    s += 4;
    if ((rc = parse_num(&s, 16, &r->v)) != LP_OK) return rc;
    if (*s == '\r') s++;
    if (*s == '\n') s++;
    return (*s == '\0') ? LP_OK : LP_FORMAT;            /* reject trailing junk */
}
```

시간 O(line length) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

입력 `"T=123 V=0x1F"`

| 단계 | s가 가리키는 곳 | 동작 | 값 |
|---|---|---|---|
| 리터럴 | `T=` | 2칸 전진 | – |
| parse_num(10) | `1` `2` `3` → `' '` 에서 멈춤 | v = 1 → 12 → 123 | t = 123 |
| 공백 | `' '` | 1개 이상 확인 후 건너뜀 | – |
| 리터럴 | `V=0x` | 4칸 전진 | – |
| parse_num(16) | `1` `F` → `'\0'` 에서 멈춤 | v = 1 → 0x1F | v = 0x1F |
| 끝 | `'\0'` | LP_OK | – |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 + 줄 끝 `\n`, `\r\n`, 공백 여러 개, `0X` + 대소문자 섞인 hex.
- 경계: `4294967295` / `0xFFFFFFFF` OK, `4294967296` / `0x100000000` → RANGE.
- 빈 숫자: `"T= V=0x1"`, `"T=12 V=0x"` → FORMAT; 빈 문자열 `""` → FORMAT.
- 형식: 공백 없음, `0x` 없음, 순서 바뀜, 음수 `T=-5`, 끝 쓰레기 `0x1G`, `\n\n` → FORMAT.

</details>

### 4. 꼬리 질문

**Q1. Now parse a whole log buffer with many lines and report how many were bad.**

<details><summary>답</summary>

Walk the buffer, find each newline with a pointer scan, and parse the slice. Since `parse_line` expects a NUL-terminated string, either copy each line into a small fixed buffer with a length check, or change the parser to take an end pointer instead of relying on NUL. I'd count OK, FORMAT and RANGE separately and keep the first bad line number for the report.

한국어 핵심: 줄 단위로 끊어서 parse — NUL 대신 end 포인터 버전으로 바꾸면 복사 없이 처리, 에러 종류별 카운트 + 첫 실패 줄 번호.

</details>

**Q2. Fields may come in any order and some may be missing. How do you change it?**

<details><summary>답</summary>

Make it a key-value loop: skip spaces, read a key up to `=`, look it up in a small table of `{name, base, offset, seen-bit}`, parse the number, and set the seen bit. At the end compare the seen mask against the required mask. That's the same table-driven idea as the debug shell in G4.

한국어 핵심: key=value 루프 + 필드 테이블 + seen 비트마스크로 필수 필드 확인 (G4와 같은 테이블 패턴).

</details>

**Q3. Why not `sscanf(line, "T=%u V=%x", &t, &v)`?**

<details><summary>답</summary>

Three reasons: if the number doesn't fit, the behavior is undefined — in practice it silently wraps, so 4294967296 becomes 0; it accepts trailing garbage unless you add a `%n` check; and on a bare-metal target it pulls in a lot of code. A hand-written scanner is 30 lines, deterministic, and reports exactly what went wrong.

한국어 핵심: `sscanf` 는 오버플로가 UB(실제로는 조용히 wrap), 끝 쓰레기 통과, 코드 크기 큼 → 직접 스캐너가 정확한 에러 코드. 이 Mac(arm64, Apple libc) 실측: `sscanf("T=4294967296 V=0x1G", "T=%u V=%x", …)` → 반환 2, `t = 0`, `v = 1` (오버플로 wrap + `G` 무시 둘 다 재현).

</details>

### 5. 채점 · 자주 틀리는 것

- 오버플로를 **곱하기 전에** 검사 (`v * 10 + d` 후 비교는 이미 wrap).
- 숫자 0개 체크 (`"T= "` 가 t = 0으로 통과하는 버그).
- 끝 쓰레기 거부 — 마지막 `*s == '\0'` 확인.
- `'a' + 10` 변환에서 hex는 대소문자 둘 다.
- 문자열 끝을 넘어 읽지 않기 — 리터럴 비교를 `||` 로 순서대로.

## 출처

1. 로컬 노트 — [LP03 링크 트레이닝 · LTSSM](2026-10-31_LP03_link_training_ltssm.html) §2.4–2.5 (G5의 실제 LTSSM 상태·타임아웃, 단순화 대비용)
2. 로컬 문제 — [hmScreenPrep V12 Frame Timestamp & Sync Checker](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) (G7과 겹치지 않게 범위 분리)
3. C11 draft N1570 — 6.3.1.1 integer promotions, 6.3.1.3 signed/unsigned conversion, 6.5.7 shift, 6.7.9 designated initializers: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
4. cppreference — struct/array initialization (designated initializers, zero-fill): https://en.cppreference.com/w/c/language/struct_initialization
5. cppreference — `fscanf`/`sscanf` (overflow behavior of `%u` is undefined): https://en.cppreference.com/w/c/io/fscanf
6. 검증: `onsitePrep/n05/g/run.sh` (macOS arm64, Apple clang, `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined`) → G1–G8 PASS
