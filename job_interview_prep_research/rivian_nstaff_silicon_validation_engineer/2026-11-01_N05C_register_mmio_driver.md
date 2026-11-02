# N05C · 레지스터 · MMIO 드라이버
<!-- theme: pastel -->

> **묶음**: C 레지스터·MMIO 드라이버 (polling·W1C·struct overlay·64-bit 카운터·ISR 디스패치·UART·SPI·레지스터 sanity) · **문제 수**: 8 (★★★ 3개: C1 · C2 · C5) · **목표**: 하드웨어 동작(W1C, 읽기 부작용, FIFO, wrap)을 **먼저 clarify**하고 20분 안에 timeout 있는 드라이버 코드를 종이에 쓰기 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · [C 정리 C4·C6](2026-10-29_c_basics_cram.html) · [V2 레지스터 RMW·sanity](hmScreenPrep/problems/V2_reg_field_rmw/index.html) · **이전/다음**: [N05B 비트 응용·엔디안·패킹](2026-11-01_N05B_bits_endian_packing.html) / [N05D 버퍼·문자열·메모리](2026-11-01_N05D_buffers_strings_memory.html) · **코드**: onsitePrep/n05/c/ (`./run.sh` → C1–C8 PASS)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **"하드웨어가 이렇게 동작한다"는 가정을 말로 꺼내고** 그 가정에 맞는 드라이버 코드를 쓰는 습관. 문법 문제가 아니라 **W1C에 RMW 하지 않기, FIFO는 읽으면 빠진다, 모든 polling에는 timeout** 같은 펌웨어 상식 문제.
- 하드웨어는 전부 `reg_read(off)` / `reg_write(off, val)` 스텁 + **가짜 레지스터 모델**로 흉내 냈다 (ready가 N번째 읽기에서 뜨는 STATUS, W1C STATUS, 깊이 4 FIFO, 읽을 때마다 증가하는 카운터). 모델 코드는 각 문제의 "HW 모델" 접힘에 따로 있다.
- **외울 한 줄 ①**: `if ((uint32_t)(now - start) >= timeout)` — unsigned 뺄셈은 wrap에서도 정답. `now >= start + timeout`은 wrap에서 즉시 거짓 timeout (C1 테스트로 확인).
- **외울 한 줄 ②**: W1C는 `STATUS = pending;` — 지울 비트만 쓴다. `STATUS |= bit`(RMW)는 **읽은 pending 전부를 지운다** (C2 테스트로 확인).
- **외울 한 줄 ③**: 레지스터 읽기는 **부작용이 있을 수 있다** (FIFO pop, read-to-clear, latch) → 한 번 읽어서 변수에 담고, 비어 있으면 DATA를 읽지 않는다.
- ★★★ (온사이트 확률 높음): **C1 polling+timeout · C2 RMW+W1C · C5 ISR 디스패치**. C4는 [C 정리 C4](2026-10-29_c_basics_cram.html)(hi-lo-hi)의 변형, C8은 [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html)의 종이용 축소판 — 원본은 링크로.
- 사용법: §1을 인쇄 → 문제당 20분 종이 → 각 문제 `<details>`로 채점. 하루 3문제면 C1·C2·C5 → C4·C6·C7 → C3·C8 순서.

## 1. 문제지 (인쇄용 — 답 없음)

### C1 · Poll status bits with a wrap-safe timeout

Wait until `(reg_read(off) & mask) == want`, or until `timeout` ticks have passed on a free-running 32-bit tick counter that wraps. Return `0` on success, `-2` on timeout, `-1` if the request can never match.

```c
uint32_t reg_read(uint32_t off);   /* MMIO read */
uint32_t ticks_now(void);          /* 32-bit, wraps */
int wait_bits(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout);
```

Example: READY (bit 0) becomes 1 on the 20th read, ticks start at `0xFFFFFFF0`, timeout 100 → returns `0`.

### C2 · RMW helper + W1C interrupt status

Registers: `CTRL` 0x00 (RW), `INT_STATUS` 0x04 (W1C: writing 1 clears a bit, writing 0 does nothing), `INT_EN` 0x08 (RW). Write a read-modify-write helper for RW registers, a W1C ack, and `irq_take_pending()` that returns enabled pending bits and acknowledges exactly those.

```c
void     reg_rmw(uint32_t off, uint32_t clr, uint32_t set);
void     irq_ack(uint32_t bits);
uint32_t irq_take_pending(void);
```

Example: `INT_STATUS = 0x15`, `INT_EN = 0x05` → `irq_take_pending()` returns `0x05`, `INT_STATUS` becomes `0x10`.

### C3 · Register block struct overlay

UART map: `0x000 CTRL`, `0x004 STATUS (RO)`, `0x008 BAUD_DIV`, `0x00C reserved`, `0x010 DATA`, `0x014–0x0FC reserved`, `0x100 ID (RO)`. Define `uart_regs_t` so that every member lands at its offset, prove it at compile time, and write `uart_setup()` (disable → set divisor → enable TX/RX).

```c
void uart_setup(uart_regs_t *u, uint16_t div);
```

Example: `offsetof(uart_regs_t, ID) == 0x100`, `sizeof(uart_regs_t) == 0x104`.

### C4 · 64-bit counter from two 32-bit registers

`CNT_LO` 0x00, `CNT_HI` 0x04. (a) The block latches HI into a shadow when LO is read. (b) Another block has no latch. Read a consistent 64-bit value in both cases, and convert ticks to nanoseconds without overflow.

```c
uint64_t cnt_read_latched(void);
uint64_t cnt_read_hlh(void);
uint64_t ticks_to_ns(uint64_t ticks, uint32_t hz);
```

Example: no latch, counter at `0x1_FFFF_FFFF` and +1 per bus read → `cnt_read_hlh()` = `0x2_0000_0003`. `ticks_to_ns(24000000, 24000000)` = `1000000000`.

### C5 · ISR dispatch table

`INT_STATUS` 0x04 (W1C), `INT_EN` 0x08. Keep a table of 32 `{handler, ctx}`. In the ISR, take enabled pending bits, ack them, then call the handler for each set bit, lowest bit first. A pending bit with no handler must not cause an interrupt storm.

```c
typedef void (*irq_fn_t)(unsigned bit, void *ctx);
int      irq_register(unsigned bit, irq_fn_t fn, void *ctx);
unsigned irq_dispatch(void);   /* returns handlers called */
```

Example: `STATUS = 0x80000019`, `EN = 0x80000009`, handlers on 0, 3, 31 → calls 0, 3, 31 in that order, returns 3, `STATUS` becomes `0x10`.

### C6 · Polled UART without printf

`DATA` 0x00 (write = push TX FIFO, read = **pop** RX FIFO), `STAT` 0x04 (bit 0 TXFULL, bit 1 RXEMPTY). Write `uart_putc` (wait while TX full, `'\n'` → `"\r\n"`), non-blocking `uart_getc_nb` (−1 if empty), and `uart_print_hex32` (`"0x"` + 8 hex digits).

```c
void uart_putc(char c);
int  uart_getc_nb(void);
void uart_print_hex32(uint32_t v);
```

Example: `uart_print_hex32(0xA)` → `0x0000000A`. RX holds `0xFF` → `uart_getc_nb()` = 255, not −1.

### C7 · SPI full-duplex transfer

`DATA` 0x00 (write = push TX, read = pop RX), `STAT` 0x04 (TXFULL, RXEMPTY). TX and RX FIFOs are 4 deep; every byte sent clocks one byte in; if RX is full, the incoming byte is lost. Transfer `n` bytes; `tx == NULL` sends `0xFF`, `rx == NULL` discards. Return −1 if the block stops making progress.

```c
int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t n);
```

Example: `n = 10`, slave answers `~tx` → `rx[i] == ~tx[i]` for all 10, no overrun.

### C8 · Table-driven register sanity test

Each register has `{off, reset, rw_mask, type}` with `type` RW / RO / W1C / SKIP. For every non-SKIP register check the reset value; for RW registers also walk a 1 through every RW bit and check read-back; never write RO/W1C registers, never touch SKIP registers (FIFO). Count all failures, log the first `cap`, restore registers.

```c
size_t reg_sanity(const reg_desc_t *t, size_t n, reg_fail_t *log, size_t cap);
```

Example: SCRATCH (RW, mask `0xFFFFFFFF`) has bit 13 stuck at 0 → returns 1, `log[0] = {0x00, expect 0x2000, got 0, kind rw}`.

## C1 · status 비트 polling + timeout (32-bit tick wrap 안전) (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 상태 레지스터의 특정 비트가 원하는 값이 될 때까지 기다리되, **정해진 tick 수가 지나면 포기**하는 함수.
- **상황**: bring-up 펌웨어에서 PLL lock, DMA done, PHY link-up, reset 해제 완료를 기다리는 코드가 전부 이 모양이다. timeout이 없으면 실리콘 하나가 안 올라왔을 때 "보드가 멈췄다"로만 보이고 원인 로그가 안 남는다 ([BV04 PLL lock](2026-11-01_BV04_clocks_osc_pll_refclk.html) 참고). tick 카운터는 32-bit라서 **언젠가 반드시 0으로 돌아간다**(1 MHz면 약 71.6분, 1 GHz면 약 4.3초마다).

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `wait_bits(off, mask, want, timeout)` | `(reg & mask) == want` 될 때까지 대기 | 성공 0, 시간 초과 −2, `want`에 `mask` 밖 비트가 있으면 −1 (버스 접근 없이) |

- **예시를 말로 풀면**: tick이 `0xFFFFFFF0`에서 시작해 중간에 0으로 넘어가도, 실제로 100 tick이 지나기 전에 READY가 떴으면 성공이어야 한다. "BUSY 비트가 0이 될 때까지"도 같은 함수로 (`mask = BUSY, want = 0`).
- **지켜야 할 조건**: tick wrap에서 정확, timeout 0이어도 **최소 1번은 읽기**, 무한 루프 없음.

### 1. Problem (English)

"You have a status register at offset `off` and a free-running 32-bit tick counter. Write a function that waits until the masked bits equal `want`, with a timeout in ticks. The counter wraps. Return 0 on success, a negative error on timeout."

```c
uint32_t reg_read(uint32_t off);
uint32_t ticks_now(void);
int wait_bits(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout);
```

- READY on 3rd read, `timeout = 100` → `0` (3 reads).
- start `0xFFFFFFF0`, READY on 20th read, `timeout = 100` → `0` (crosses the wrap).
- never ready, `timeout = 10`, 1 tick per `ticks_now()` call → `-2` after exactly 10 reads.

### 2. 먼저 물어볼 것 (clarify)

- "Is the tick counter 32-bit and does it wrap? What's its frequency?" → 32-bit, wraps; units are ticks.
- "Do reads of the status register have side effects, like clear-on-read?" → No, plain status.
- "Busy-wait is fine, or should I sleep/yield between polls?" → Busy-wait, bare-metal.
- "Should I return the last value read?" → Not required (extension).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: wrap? 단위? 읽기 부작용? |
| 2–5 | 예시: start=0xFFFFFFF0, now=0x10 → elapsed=0x20 손으로 |
| 5–15 | 코드: EINVAL 체크 → start → 루프 (time sample → read → expired) |
| 15–18 | trace: wrap 지점 2–3줄 |
| 18–20 | 테스트 4개 + "last read after deadline" 확장 |

- 소리 내서: **"I compute elapsed as unsigned `now - start`, which is correct across the wrap as long as the timeout is less than 2^32 ticks. Comparing `now` against `start + timeout` breaks when the deadline wraps."**
- 소리 내서: **"I sample the time before the read, so if I get preempted past the deadline I still do one final read before declaring timeout — Linux's `read_poll_timeout` does the same."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

`deadline = start + timeout`을 만들지 말고, 매번 "얼마나 지났나"를 계산하라. unsigned에서 `5 - 0xFFFFFFF0`은 얼마인가?

</details>

<details><summary>힌트 2</summary>

루프 안 순서를 세 줄로: ① 지금 시간이 만료인지 `bool`로 기억 ② 레지스터 읽고 맞으면 성공 ③ ①이 만료였으면 timeout. ①과 ②의 순서가 왜 중요한지 생각해 보라 (읽고 나서 오래 선점당하면?).

</details>

<details><summary>모범 답 (C)</summary>

```c
#define POLL_OK       0
#define POLL_EINVAL  (-1)
#define POLL_TIMEOUT (-2)

uint32_t reg_read(uint32_t off);   /* 32-bit MMIO read (side effects allowed) */
uint32_t ticks_now(void);          /* free-running 32-bit tick counter, wraps */

/* Wait until (reg & mask) == want, or `timeout` ticks elapse. */
int wait_bits(uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout)
{
    if (want & ~mask)
        return POLL_EINVAL;                 /* can never match */
    uint32_t start = ticks_now();
    for (;;) {
        /* sample time BEFORE the read: after a long preemption we still get one last read */
        bool expired = (uint32_t)(ticks_now() - start) >= timeout;   /* unsigned: wrap-safe */
        if ((reg_read(off) & mask) == want)
            return POLL_OK;
        if (expired)
            return POLL_TIMEOUT;
    }
}
```

- 시간 O(timeout / 읽기 1회 비용), 공간 O(1).
- 같은 파일에 **틀린 두 버전**도 있다: `deadline = start + timeout` 버전은 start=0xFFFFFFF0, timeout=100에서 **첫 체크에 바로 TIMEOUT** (deadline이 0x54로 wrap), "읽고 나서 시간 체크" 버전은 읽기 직후 1000 tick 선점이 끼면 **HW는 ready인데 TIMEOUT** — 둘 다 테스트로 확인.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only): STATUS becomes ready after N reads; ticks advance per call ---- */
#define STATUS_OFF 0x04u
#define ST_READY   (1u << 0)
#define ST_BUSY    (1u << 1)
static uint32_t m_reads, m_ready_after, m_tick, m_tick_step, m_preempt;
uint32_t reg_read(uint32_t off)
{
    (void)off;
    m_reads++;
    if (m_reads == 1) m_tick += m_preempt;    /* simulate a long ISR/preemption after read #1 */
    return (m_reads >= m_ready_after) ? ST_READY : ST_BUSY;   /* BUSY until READY */
}
uint32_t ticks_now(void) { uint32_t t = m_tick; m_tick += m_tick_step; return t; }
static void model_reset(uint32_t ready_after, uint32_t tick0, uint32_t step)
{
    m_reads = 0; m_ready_after = ready_after; m_tick = tick0; m_tick_step = step; m_preempt = 0;
}
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

예시: start = `0xFFFFFFF0`, `ticks_now()` 1회당 +1, READY는 20번째 읽기, timeout = 100.

| 반복 | now | now − start (uint32) | expired | READY? | 결과 |
|---|---|---|---|---|---|
| 1 | 0xFFFFFFF1 | 1 | no | no | 계속 |
| 16 | 0x00000000 | 16 | no | no | 계속 (wrap 지점) |
| 20 | 0x00000004 | 20 | no | yes | return 0 |
| (naive) | deadline = 0xFFFFFFF0 + 100 = 0x54 | — | 0xFFFFFFF1 ≥ 0x54 → 참 | — | 즉시 TIMEOUT ✘ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 3번째 읽기에 ready → 0, 읽기 3회.
- 비트 clear 대기: `mask = BUSY, want = 0` → 0.
- wrap: start 0xFFFFFFF0 → 0 (naive 버전은 −2).
- 경계: `timeout = 0` → 정확히 1회 읽고, 이미 ready면 0 아니면 −2.
- 영원히 안 뜸: timeout 10 → −2, 읽기 횟수 유한 (10회).
- 에러 입력: `want`에 `mask` 밖 비트 → −1, 버스 접근 0회.

</details>

### 4. 꼬리 질문

Q1. "Why not just `while (!(reg & READY));`?"

<details><summary>답</summary>

If the block never comes up — clock not enabled, held in reset, wrong address — the CPU spins forever and the only symptom is a hung board with no log. With a timeout I can return an error code, dump the status register, and let the test framework mark that test as failed and keep going. On silicon bring-up that's the difference between "the board is dead" and "PLL2 didn't lock, status was 0x4".

- 한국어 핵심: timeout = **원인이 남는 실패**. 무한 대기는 증상만 남김.

</details>

Q2. "What if `ticks_now()` and the status read can be reordered or cached by the compiler?"

<details><summary>답</summary>

`reg_read` is an out-of-line accessor here, but on target it would be a `volatile` load, so the compiler must perform every read in program order and can't hoist it out of the loop. `volatile` doesn't order against other CPUs or the bus, but for a single core polling one device register that's enough; if the tick is a memory-mapped timer on a different bus I'd still be fine because both are volatile accesses in program order.

- 한국어 핵심: `volatile` = 컴파일러가 읽기를 지우거나 합치거나 루프 밖으로 못 뺌. 루프 안 읽기가 1번으로 합쳐지면 영원히 같은 값.

</details>

Q3. "Timeout in microseconds instead of ticks, and a 1 GHz counter?"

<details><summary>답</summary>

Convert once before the loop: `timeout_ticks = us * (hz / 1000000)`, checking that it stays below 2^31 so the wrap-safe comparison is valid — at 1 GHz a 32-bit counter wraps every 4.29 seconds, so timeouts above about 2 seconds need the 64-bit counter from C4. I'd also return the last value read through an out pointer so the caller can log it.

- 한국어 핵심: 단위 변환은 루프 밖에서 1번, timeout < 2^31 tick 보장, 길면 64-bit 카운터.

</details>

### 5. 채점 · 자주 틀리는 것

- `start + timeout` deadline 비교 → wrap에서 즉시 거짓 timeout.
- 시간 체크를 읽기 **뒤**에만 → 선점 후 거짓 timeout (마지막 1회 읽기 없음).
- `want & ~mask` 검사 없음 → 절대 만족 못 하는 조건으로 timeout까지 낭비.
- `(int)` 비교나 `int timeout` → 부호 혼합 비교.
- 실제 MMIO에서 `volatile` 빠뜨림 → 루프가 읽기 1회로 최적화.

## C2 · RMW 헬퍼 + W1C 인터럽트 status 처리 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 일반 RW 레지스터용 read-modify-write 헬퍼와, W1C 인터럽트 상태 레지스터를 **올바르게** 지우는 함수를 만든다.
- **상황**: 인터럽트 상태 레지스터는 대부분 **W1C**(write-1-to-clear)다. HW가 이벤트마다 비트를 1로 세우고, SW가 그 비트에 1을 쓰면 지워지고, 0을 쓰면 아무 일도 없다. 이렇게 만든 이유는 **SW가 RMW 없이 원하는 비트만 지울 수 있게** 하기 위해서다. 그런데 습관적으로 `STATUS |= BIT(0)`을 쓰면 RMW가 되어 **읽은 pending 1들을 전부 다시 써서 모두 지운다** — 처리 안 한 인터럽트가 사라진다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `reg_rmw(off, clr, set)` | RW 레지스터의 일부 비트 변경 | 정확히 읽기 1회 + 쓰기 1회 |
| `irq_ack(bits)` | W1C 비트 지우기 | 읽지 않고, 지울 비트만 쓴다 |
| `irq_take_pending()` | enable된 pending 비트 가져오고 ack | 가져온 비트만 ack, 가져온 값 반환 |

- **예시를 말로 풀면**: STATUS = 0x15 (비트 0, 2, 4 pending). 비트 0만 지우고 싶다 → 결과는 0x14여야 한다. RMW로 하면 0x00이 된다.
- **지켜야 할 조건**: disable된 비트는 ack하지 않음(나중에 enable하면 처리해야 하므로), 처리 도중 새로 뜬 비트는 살아 있어야 함.

### 1. Problem (English)

"`INT_STATUS` at 0x04 is write-one-to-clear. Write a generic RMW helper for normal registers, then an acknowledge function and a function the ISR calls to take the pending, enabled interrupts. Explain why you can't use your RMW helper on `INT_STATUS`."

```c
void     reg_rmw(uint32_t off, uint32_t clr, uint32_t set);
void     irq_ack(uint32_t bits);
uint32_t irq_take_pending(void);
```

- `CTRL = 0xFFFF00A5`, `reg_rmw(CTRL, 0xF0, 0x30)` → `0xFFFF0035`.
- `STATUS = 0x15`, `irq_ack(0x1)` → `0x14`; `reg_rmw(STATUS, 0, 0x1)` → `0x00` (bug).
- `STATUS = 0x15`, `EN = 0x05` → `irq_take_pending()` returns `0x05`, `STATUS = 0x10`.

### 2. 먼저 물어볼 것 (clarify)

- "Is `INT_STATUS` W1C, or read-to-clear, or does it need a separate clear register?" → W1C.
- "Is `INT_STATUS` masked by `INT_EN`, or does it show raw events?" → Raw; mask in software.
- "Can the ISR and main code touch `CTRL` concurrently?" → Assume no for now (follow-up).
- "Edge or level events?" → Edge-like: the bit is set again if the event re-fires.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: W1C/RC/별도 CLR 레지스터? raw vs masked? |
| 2–5 | 0x15에서 비트 0 지우기를 RMW vs W1C로 손 계산 |
| 5–15 | `reg_rmw` → `irq_ack` → `irq_take_pending` |
| 15–18 | trace: 0x15/0x05 예시 |
| 18–20 | 테스트 + "ack 먼저 vs 나중" 설명 |

- 소리 내서: **"On a W1C register, writing zero is a no-op, so I don't need to read first — I write exactly the bits I want to clear. A read-modify-write would write back every pending one and silently acknowledge interrupts I never handled."**
- 소리 내서: **"I ack before running the handlers: if the event fires again while I'm handling it, the bit gets set again and I'll get another interrupt instead of losing it."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

W1C 레지스터에 0x15를 쓰면 어떤 비트가 지워지나? 0x01을 쓰면? 그러면 "지우기 전에 읽을" 필요가 있나?

</details>

<details><summary>힌트 2</summary>

`pending = STATUS & EN` 을 **한 번** 읽어 변수에 둔다. 그 변수를 그대로 STATUS에 쓰면 정확히 그 비트들만 지워진다. 그 사이에 새로 뜬 비트는 변수에 없으니 살아남는다.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

/* RMW for normal RW registers: exactly one read, one write. Not atomic vs ISRs. */
void reg_rmw(uint32_t off, uint32_t clr, uint32_t set)
{
    uint32_t v = reg_read(off);
    v = (v & ~clr) | set;
    reg_write(off, v);
}

/* W1C ack: write ONLY the bits to clear. 0-bits are no-ops, so no read needed. */
void irq_ack(uint32_t bits) { reg_write(STAT_OFF, bits); }

/* Snapshot enabled pending bits, ack exactly those, return them for handling.
 * Ack BEFORE handling: an event that re-fires during handling sets the bit again. */
uint32_t irq_take_pending(void)
{
    uint32_t pending = reg_read(STAT_OFF) & reg_read(EN_OFF);
    if (pending)
        irq_ack(pending);
    return pending;
}
```

- 시간 O(1), 공간 O(1). `STAT_OFF`/`EN_OFF`는 문제의 레지스터 맵 (0x04 / 0x08).

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only) ----
 * 0x00 CTRL       RW
 * 0x04 INT_STATUS W1C: HW sets bits; writing 1 clears that bit, writing 0 does nothing
 * 0x08 INT_EN     RW
 * Optional "late event": bits OR-ed into INT_STATUS right after the Nth bus access,
 * i.e. an interrupt that fires while the ISR is running.                          */
#define CTRL_OFF   0x00u
#define STAT_OFF   0x04u
#define EN_OFF     0x08u
static uint32_t m_ctrl, m_stat, m_en, m_reads, m_writes, m_late_bits, m_late_at;
static void model_tick(void)
{
    if (m_late_bits && m_reads + m_writes == m_late_at) { m_stat |= m_late_bits; m_late_bits = 0; }
}
uint32_t reg_read(uint32_t off)
{
    uint32_t v = (off == CTRL_OFF) ? m_ctrl : (off == STAT_OFF) ? m_stat : m_en;
    m_reads++; model_tick();
    return v;
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off == CTRL_OFF) m_ctrl = val;
    else if (off == STAT_OFF) m_stat &= ~val;     /* W1C */
    else m_en = val;
    m_writes++; model_tick();
}
static void model_reset(uint32_t ctrl, uint32_t stat, uint32_t en)
{
    m_ctrl = ctrl; m_stat = stat; m_en = en; m_reads = m_writes = 0; m_late_bits = 0; m_late_at = 0;
}
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 단계 | 동작 | STATUS 전 | 쓴 값 | STATUS 후 |
|---|---|---|---|---|
| 버그 | `reg_rmw(STAT, 0, 0x1)`: 읽기 0x15 → v = 0x15 OR 0x1 = 0x15 → 쓰기 | 0x15 | 0x15 | 0x00 ✘ |
| 정답 | `irq_ack(0x1)` | 0x15 | 0x01 | 0x14 |
| take | pending = 0x15 AND 0x05 = 0x05 → ack | 0x15 | 0x05 | 0x10 (비트 4는 disable이라 남김) |
| 늦게 온 비트 5 | 두 번 읽은 직후 HW가 비트 5 set → 0x21, ack 0x01 | 0x21 | 0x01 | 0x20 (다음 ISR에서 처리) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- RMW 정상: 필드 [7:4]만 바뀌고 나머지 유지, 읽기 1·쓰기 1.
- W1C에 RMW = 전부 지워짐 (버그 재현), `irq_ack`은 읽기 0회.
- disable된 pending 비트는 ack 안 됨.
- 처리 중 새로 뜬 비트는 살아남음.
- pending 0 → 쓰기 0회 (불필요한 버스 쓰기 없음).
- 비트 31 (`1u << 31`, signed 아님).

</details>

### 4. 꼬리 질문

Q1. "Main code and the ISR both do RMW on `CTRL`. What breaks and how do you fix it?"

<details><summary>답</summary>

RMW is three steps — read, modify, write — so if the ISR runs between my read and my write, its update is overwritten by my stale value. Fixes, from cheapest: disable interrupts around the RMW, use the hardware's separate SET/CLR alias registers if the block has them so each side does a single write, or on Cortex-M3/M4 use bit-banding for single-bit changes. On multi-core I'd need a spinlock in addition.

- 한국어 핵심: RMW는 원자적이지 않음 → IRQ off, SET/CLR 레지스터, bit-band.

```c
uint32_t key = irq_save();  reg_rmw(CTRL_OFF, clr, set);  irq_restore(key);
```

</details>

Q2. "Why ack before handling and not after?"

<details><summary>답</summary>

If I handle first and ack after, an event that fires during the handler sets a bit that's already 1, and then my ack clears it — that event is lost. Acking first means a new event during handling re-sets the bit and raises a new interrupt. The exception is level-triggered sources where the status bit reflects a condition: there you first clear the cause in the device, then ack, or the bit immediately re-asserts.

- 한국어 핵심: edge 성격 이벤트는 **ack 먼저**, level(조건 유지형)은 **원인 제거 → ack**.

</details>

Q3. "After the ack write, the CPU returns from the ISR and immediately re-enters it. Why?"

<details><summary>답</summary>

Device writes are often posted: the store leaves the CPU but hasn't reached the peripheral when the exception returns, so the interrupt line is still asserted and the NVIC or GIC sees it again. The fix is to read back a register from the same device after the ack, which forces the write to complete, or use a barrier like `dsb` where the architecture guarantees that's enough for the bus.

- 한국어 핵심: posted write → **같은 디바이스 read-back**으로 쓰기 완료 보장 후 ISR 복귀.

</details>

### 5. 채점 · 자주 틀리는 것

- W1C에 `|=` / `&=` / RMW 헬퍼 사용.
- STATUS를 두 번 읽어서 (판단용 1번, ack용 1번) 그 사이 새 비트까지 ack.
- disable된 비트까지 ack → enable 후 처리할 이벤트가 사라짐.
- `reg_rmw`가 읽기 2번 (`reg_read(off) & ~clr` 따로, `| set` 따로 접근).
- `1 << 31` (signed) — `1u` 사용.

## C3 · 레지스터 블록 struct overlay (reserved 패딩, offset static_assert) (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 데이터시트의 레지스터 맵을 C struct로 옮기고, 각 멤버가 정확한 오프셋에 놓였는지 **컴파일 타임에 증명**한다.
- **상황**: CMSIS(`UART0->CTRL`)나 벤더 HAL이 쓰는 방식. 오프셋 한 칸 틀리면 엉뚱한 레지스터에 써서 "가끔 이상하게 동작"하는 디버그 지옥이 된다. reserved 구간 크기를 손으로 세다가 틀리는 게 제일 흔하다.

| 할 것 | 내용 | 규칙 |
|---|---|---|
| `uart_regs_t` | 맵 그대로 멤버 + reserved 배열 | 레지스터 멤버는 `volatile`, RO는 `volatile const` |
| `_Static_assert` | 주요 오프셋과 전체 크기 | `offsetof` 사용 |
| `uart_setup(u, div)` | disable → divisor → enable | 동작 중 divisor 변경 금지 |

- **예시를 말로 풀면**: DATA 다음 레지스터 ID는 0x100에 있다. 그 사이 0x014–0x0FC가 reserved → 몇 워드인지 계산해야 한다.
- **지켜야 할 조건**: 비트필드(`uint32_t en:1`) 쓰지 않기, packed 없이도 패딩 없음을 보장.

### 1. Problem (English)

"Here's the register map of a UART. Represent it as a C struct so that `UART0->DATA` works, make the compiler prove the layout, and write the init sequence."

```c
/* 0x000 CTRL  0x004 STATUS(RO)  0x008 BAUD_DIV  0x00C rsvd
   0x010 DATA  0x014..0x0FC rsvd  0x100 ID(RO) */
void uart_setup(uart_regs_t *u, uint16_t div);
```

- `offsetof(uart_regs_t, DATA)` → `0x10`; `offsetof(uart_regs_t, ID)` → `0x100`; `sizeof` → `0x104`.
- `uart_setup(u, 26)` → `BAUD_DIV = 26`, `CTRL = 0x7` (EN, TXEN, RXEN); reserved words untouched.
- `u->STATUS = 1;` → compile error (const).

### 2. 먼저 물어볼 것 (clarify)

- "Are all registers 32-bit and must be accessed as 32-bit?" → Yes.
- "Is the init order specified — disable before changing the divisor?" → Yes.
- "Do you want a struct or offset macros?" → Struct; mention macros as alternative.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 접근 폭, 초기화 순서 |
| 2–5 | reserved 크기 계산: (0x100 − 0x014) / 4 = 59 |
| 5–12 | struct + static_assert + setup |
| 12–14 | 오프셋 표로 trace |
| 14–15 | 테스트 + 비트필드 안 쓰는 이유 |

- 소리 내서: **"The reserved gap is (0x100 − 0x14) / 4 = 59 words. Rather than trust my arithmetic, I add `_Static_assert` on `offsetof` for every register I care about, so a wrong padding count fails the build."**
- 소리 내서: **"I make the members volatile, and the read-only ones `volatile const`, so a stray write to STATUS is a compile error."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

reserved 구간은 "다음 레지스터 오프셋 − 이 구간 시작 오프셋"을 4로 나눈 개수의 `uint32_t` 배열.

</details>

<details><summary>힌트 2</summary>

`<stddef.h>`의 `offsetof(type, member)`와 C11 `_Static_assert(cond, "msg")`. 모든 멤버가 `uint32_t`라 컴파일러가 패딩을 넣을 이유가 없다.

</details>

<details><summary>모범 답 (C)</summary>

```c
/* UART block map: 0x000 CTRL, 0x004 STATUS(RO), 0x008 BAUD_DIV, 0x00C reserved,
 *                 0x010 DATA, 0x014..0x0FC reserved, 0x100 ID(RO). Total 0x104. */
typedef struct {
    volatile uint32_t       CTRL;        /* 0x000 */
    volatile const uint32_t STATUS;      /* 0x004 RO: const catches stray writes */
    volatile uint32_t       BAUD_DIV;    /* 0x008 */
    uint32_t                RSVD0[1];    /* 0x00C */
    volatile uint32_t       DATA;        /* 0x010 */
    uint32_t                RSVD1[59];   /* 0x014..0x0FC: (0x100 - 0x014) / 4 = 59 */
    volatile const uint32_t ID;          /* 0x100 RO */
} uart_regs_t;

_Static_assert(offsetof(uart_regs_t, STATUS)   == 0x004, "STATUS offset");
_Static_assert(offsetof(uart_regs_t, BAUD_DIV) == 0x008, "BAUD_DIV offset");
_Static_assert(offsetof(uart_regs_t, DATA)     == 0x010, "DATA offset");
_Static_assert(offsetof(uart_regs_t, ID)       == 0x100, "ID offset");
_Static_assert(sizeof(uart_regs_t)             == 0x104, "block size");

#define UART_CTRL_EN    (1u << 0)
#define UART_CTRL_TXEN  (1u << 1)
#define UART_CTRL_RXEN  (1u << 2)
/* On target: #define UART0 ((uart_regs_t *)0x40002000u) */

/* Disable, program divisor, then enable: never change the divisor while running. */
void uart_setup(uart_regs_t *u, uint16_t div)
{
    u->CTRL     = 0;
    u->BAUD_DIV = div;
    u->CTRL     = UART_CTRL_EN | UART_CTRL_TXEN | UART_CTRL_RXEN;
}
```

- 시간 O(1), 공간 O(1) (struct는 메모리를 할당하지 않음 — 주소에 덮어씌우는 "틀").
- 검증: `RSVD1[60]`으로 바꿔 컴파일하면 `ID offset`, `block size` static assertion 2개가 실패하는 것 확인. `r->STATUS = 1;`은 clang에서 "cannot assign to ... const-qualified type" 에러 확인.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

테스트는 정렬된 `static uint32_t fake[0x104 / 4]`를 MMIO 창 대신 쓰고 `(uart_regs_t *)(void *)fake`로 overlay 한 뒤 `fake[0x010/4]` 등을 직접 확인한다 (`onsitePrep/n05/c/C3_struct_overlay.c`의 `main`).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 멤버 | 크기(바이트) | 시작 오프셋 | 다음 오프셋 |
|---|---|---|---|
| CTRL | 4 | 0x000 | 0x004 |
| STATUS | 4 | 0x004 | 0x008 |
| BAUD_DIV | 4 | 0x008 | 0x00C |
| RSVD0[1] | 4 | 0x00C | 0x010 |
| DATA | 4 | 0x010 | 0x014 |
| RSVD1[59] | 236 | 0x014 | 0x100 |
| ID | 4 | 0x100 | 0x104 = sizeof |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 각 오프셋 `_Static_assert` (컴파일 타임).
- 런타임: `&u->DATA − u == 0x10` 바이트.
- `uart_setup` 후 CTRL = 0x7, BAUD_DIV = 26, reserved·DATA는 그대로.
- RO 멤버 쓰기 → 컴파일 에러.
- 패딩 개수 틀리게 → 빌드 실패 (네거티브 테스트).

</details>

### 4. 꼬리 질문

Q1. "Why not use bitfields like `uint32_t en : 1;` for the CTRL bits?"

<details><summary>답</summary>

Bitfield layout — bit order, how fields pack across storage units, and the access width the compiler uses — is implementation-defined, so the same header can map differently across compilers or ABIs. Worse, writing one bitfield is a hidden read-modify-write, which is wrong on registers with W1C or read-side-effect bits. I use plain 32-bit members with mask and shift macros.

- 한국어 핵심: 비트필드 = 배치·접근 폭 **구현 정의** + 숨은 RMW → mask/shift 매크로.

</details>

Q2. "Struct overlay vs `#define UART_DATA (*(volatile uint32_t *)(BASE + 0x10))`?"

<details><summary>답</summary>

Macros are simple and work for one instance, but the struct gives one type for all instances — `uart_setup(UART1)` — type checking, debugger visibility, and compile-time offset checks. Both compile to the same load or store with an immediate offset. Linux drivers use neither: they use `readl(base + OFF)`/`writel()`, which also add the right barriers.

- 한국어 핵심: struct = 여러 인스턴스·타입 체크·static_assert, 매크로 = 단순. Linux는 `readl/writel`.

</details>

Q3. "A block has 8-bit registers at 0x0, 0x4, 0x8 that must be accessed with byte loads. How?"

<details><summary>답</summary>

Make them `volatile uint8_t REG; uint8_t _pad[3];` and assert the offsets as before; the member type decides the access width, so the compiler emits byte loads. If the bus requires 32-bit accesses instead, keep `uint32_t` members and mask the low byte.

- 한국어 핵심: 멤버 타입 = 버스 접근 폭. 패딩은 직접 배열로.

</details>

### 5. 채점 · 자주 틀리는 것

- reserved 개수 계산 틀림 (0x100 − 0x014 = 0xEC = 236바이트 = 59워드) — static_assert 없으면 못 잡음.
- `volatile` 누락 → 쓰기 순서·횟수 보장 안 됨 (CTRL=0 쓰기가 지워질 수 있음).
- RO에 `const` 안 붙임 (감점은 아니지만 말하면 가점).
- 비트필드로 CTRL 표현.
- disable 없이 divisor 변경.

## C4 · 64-bit free-running counter: latch vs hi-lo-hi, 경과 시간 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 32-bit 레지스터 2개로 나뉜 64-bit 카운터를 **찢어지지 않게** 읽는 방법 두 가지(HW latch가 있을 때 / 없을 때)와, tick을 ns로 **오버플로 없이** 바꾸는 함수.
- **상황**: SoC 타이머, PTP 타임스탬프, 성능 카운터. hi-lo-hi 기본형은 [C 정리 C4](2026-10-29_c_basics_cram.html)에서 했다 — 여기선 **latch 방식이면 읽는 순서가 정답의 전부**라는 점과 **단위 변환 오버플로**가 추가.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `cnt_read_latched()` | LO 읽으면 HI가 shadow에 고정되는 HW | LO → HI 순서 1회 |
| `cnt_read_hlh()` | latch 없는 HW | HI → LO → HI, 다르면 재시도 |
| `ticks_to_ns(ticks, hz)` | tick → ns | 중간 곱셈 오버플로 없음 |

- **예시를 말로 풀면**: 카운터가 0x1_FFFF_FFFF이고 읽을 때마다 1씩 증가. HI→LO 순진하게 읽으면 0x1_0000_0000 (2^32 tick 과거). 24 MHz에서 2^40 tick을 ns로 바꾸면 `ticks * 1e9`가 2^64를 넘는다.
- **지켜야 할 조건**: `(uint64_t)hi << 32` 캐스트, 재시도는 최대 1회라는 논증.

### 1. Problem (English)

"The timer exposes `CNT_LO` and `CNT_HI`. Block A latches the high word when you read the low word; block B doesn't. Write a 64-bit read for each, and a function converting ticks to nanoseconds that doesn't overflow for large tick counts."

```c
uint64_t cnt_read_latched(void);
uint64_t cnt_read_hlh(void);
uint64_t ticks_to_ns(uint64_t ticks, uint32_t hz);
```

- Block B, counter `0x1_FFFF_FFFF`, +1 per read → `cnt_read_hlh()` = `0x2_0000_0003` (one retry, 6 reads).
- Block A, same counter → `cnt_read_latched()` = `0x1_FFFF_FFFF`.
- `ticks_to_ns(1ull << 40, 24000000)` = `45812984490666`.

### 2. 먼저 물어볼 것 (clarify)

- "Does reading LO latch HI, or reading HI latch LO? Datasheet wording?" → Block A: LO latches HI.
- "Is the CPU 32-bit? On AArch64 a single 64-bit load may be atomic." → 32-bit bus access only.
- "Counter frequency fits in 32 bits?" → Yes, `hz` ≤ 4.29 GHz.
- "Can an ISR also read the counter (latch shared state)?" → Follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: latch 방향, 32/64-bit 버스 |
| 2–5 | 0x1_FFFF_FFFF 숫자 예시로 torn read 보여주기 |
| 5–14 | latched → hlh → ticks_to_ns |
| 14–18 | hlh trace 6 reads |
| 18–20 | 테스트 + ISR 공유 시 latch 문제 |

- 소리 내서: **"With a latching timer the order is the whole trick: reading LO snapshots HI into a shadow, so I read LO first, then HI. If I read HI first I get the shadow from the previous snapshot."**
- 소리 내서: **"`ticks * 1e9` overflows 64 bits after about 18 billion ticks — at 24 MHz that's 12.8 minutes. I split into whole seconds and remainder; the remainder is below `hz`, so remainder times 1e9 fits."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

latch HW에서 HI를 먼저 읽으면 HI는 어디서 오나? (마지막으로 LO를 읽었을 때의 shadow.) latch 없는 HW에서 HI는 2^32 tick에 한 번만 바뀐다.

</details>

<details><summary>힌트 2</summary>

`ticks = q * hz + r` (0 ≤ r < hz) 이면 `ns = q * 1e9 + r * 1e9 / hz`. `r < 2^32`, `1e9 < 2^30` → `r * 1e9 < 2^62`.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);

/* (a) HW latches HI when LO is read: LO first, then HI. Order is the whole trick. */
uint64_t cnt_read_latched(void)
{
    uint32_t lo = reg_read(CNT_LO);          /* snapshot: HI copied to shadow */
    uint32_t hi = reg_read(CNT_HI);          /* shadow, consistent with lo    */
    return ((uint64_t)hi << 32) | lo;
}

/* (b) No latch: hi-lo-hi, retry if LO wrapped in between (at most 1 retry). */
uint64_t cnt_read_hlh(void)
{
    uint32_t hi, lo, hi2;
    do {
        hi  = reg_read(CNT_HI);
        lo  = reg_read(CNT_LO);
        hi2 = reg_read(CNT_HI);
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

/* Overflow-safe ticks -> ns: split into whole seconds + remainder. */
uint64_t ticks_to_ns(uint64_t ticks, uint32_t hz)
{
    const uint64_t NS = 1000000000ull;
    return (ticks / hz) * NS + (ticks % hz) * NS / hz;  /* rem < hz <= 2^32: no overflow */
}

/* 64-bit unsigned subtraction is wrap-safe too (wrap = 584 years @1 GHz). */
uint64_t elapsed_ns(uint64_t start, uint64_t end, uint32_t hz) { return ticks_to_ns(end - start, hz); }
```

- 읽기: latched 2회 고정, hlh 3회 (wrap 순간 6회). ticks_to_ns O(1).
- 확인값 (테스트): naive HI→LO = `0x1_0000_0000`, latched HW에서 HI 먼저 읽으면 shadow 0 → `0x0`, `ticks_to_ns(1, 24 MHz)` = 41 (41.67 내림), `ticks_to_ns(UINT64_MAX, 1 GHz)` = `UINT64_MAX`.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only): counter advances `m_step` ticks on every bus read.
 * latch mode: reading LO copies the live HI into a shadow; reading HI returns the shadow. */
#define CNT_LO 0x00u
#define CNT_HI 0x04u
static uint64_t m_cnt, m_step;
static uint32_t m_shadow_hi, m_reads;
static bool m_latch;
uint32_t reg_read(uint32_t off)
{
    uint32_t v;
    if (off == CNT_LO) { v = (uint32_t)m_cnt; m_shadow_hi = (uint32_t)(m_cnt >> 32); }
    else               { v = m_latch ? m_shadow_hi : (uint32_t)(m_cnt >> 32); }
    m_cnt += m_step; m_reads++;
    return v;
}
static void model_reset(uint64_t cnt, uint64_t step, bool latch)
{
    m_cnt = cnt; m_step = step; m_latch = latch; m_shadow_hi = 0; m_reads = 0;
}
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

latch 없음, 시작 0x1_FFFF_FFFF, 버스 읽기마다 +1.

| 읽기 | 레지스터 | 읽을 때 카운터 | 읽은 값 |
|---|---|---|---|
| 1 | HI | 0x1_FFFF_FFFF | hi = 1 |
| 2 | LO | 0x2_0000_0000 | lo = 0 |
| 3 | HI | 0x2_0000_0001 | hi2 = 2 → 다름, 재시도 |
| 4–6 | HI, LO, HI | 0x2_0000_0002 … 04 | hi = 2, lo = 3, hi2 = 2 → `0x2_0000_0003` |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- torn read 재현: naive → 2^32 과거 값.
- hlh: wrap 순간 재시도 1회(읽기 6), wrap 아니면 읽기 3.
- latch: LO→HI는 wrap 순간에도 일관, HI→LO는 stale shadow.
- ticks_to_ns: 0 → 0, 1초 정확, 1 tick 내림, `UINT64_MAX` @1 GHz 오버플로 없음, 2^40 @24 MHz.
- elapsed: 32-bit LO wrap 걸친 두 시각 차이 = 64-bit 뺄셈.

</details>

### 4. 꼬리 질문

Q1. "Main code and an ISR both read the latching timer. Problem?"

<details><summary>답</summary>

Yes: the shadow is shared hardware state. If the ISR fires between main's LO read and HI read and does its own LO read, it overwrites the shadow, and main then reads the ISR's HI with its own older LO. Either disable interrupts around the two reads, or use the hi-lo-hi method, which is stateless and reentrant.

- 한국어 핵심: latch shadow = 공유 상태 → IRQ off 2읽기, 또는 상태 없는 hi-lo-hi.

</details>

Q2. "Why is one retry enough in hi-lo-hi?"

<details><summary>답</summary>

HI changes only when LO wraps, once every 2^32 ticks. The three reads take a few bus cycles, so two wraps inside one attempt are impossible unless I'm preempted for 2^32 ticks — 4.3 seconds at 1 GHz. In theory the loop is unbounded under extreme preemption, but in practice it retries at most once.

- 한국어 핵심: HI는 2^32 tick에 1번 → 재시도 최대 1회 (현실적으로).

</details>

Q3. "Convert to microseconds with a 19.2 MHz clock — any issue?"

<details><summary>답</summary>

The same split works with `NS` replaced by 1,000,000. 19.2 MHz isn't an integer number of ticks per microsecond — 19.2 — so a shortcut like `ticks / 19` would drift by about 1%; the split form stays exact up to truncation. For hot paths I'd precompute a fixed-point multiplier and shift, like Linux's clocksource `mult`/`shift`.

- 한국어 핵심: 정수 아닌 MHz는 나눗셈 근사 금지, split 또는 mult/shift 고정소수점.

</details>

### 5. 채점 · 자주 틀리는 것

- `hi << 32` (32-bit shift UB) — `(uint64_t)hi << 32`.
- latch HW에서 HI 먼저 읽음.
- `ticks * 1000000000 / hz` 직접 곱 → 오버플로.
- `volatile` 없으면 hi 두 번 읽기가 1번으로 합쳐져 retry 무의미.
- 데이터시트 latch 방향 확인을 clarify 안 함.

## C5 · ISR: status 읽기 → 비트마다 handler 테이블 호출 → ack (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 하나의 인터럽트 라인에 32개 소스가 묶인 블록의 ISR — 어떤 비트가 섰는지 보고, 비트별로 등록된 함수를 불러 준다.
- **상황**: GPIO 컨트롤러, DMA 채널 인터럽트, PCIe/Ethernet 블록의 "top-level ISR"이 전부 이 모양. 함수 포인터 테이블 + 비트 순회 + W1C ack 순서 + **핸들러 없는 비트 처리**(안 막으면 interrupt storm으로 CPU가 ISR만 돌게 됨)가 핵심.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `irq_register(bit, fn, ctx)` | 비트에 handler 등록 | bit ≥ 32 또는 fn NULL → −1 |
| `irq_dispatch()` | ISR 본체 | enable된 pending만, 한 번 읽은 스냅샷 기준, 낮은 비트 먼저, 호출한 handler 수 반환 |

- **예시를 말로 풀면**: 비트 0, 3, 31이 pending + enable, 비트 4는 pending이지만 disable → 0, 3, 31 순서로 호출, 비트 4는 그대로 남김.
- **지켜야 할 조건**: 핸들러 없는 비트는 ack + disable + 카운트, pending 0이면 버스 쓰기 없음.

### 1. Problem (English)

"Write the top-level ISR for a block with 32 interrupt sources. Drivers register a handler per bit. Dispatch every pending, enabled source, lowest bit first, and acknowledge correctly — `INT_STATUS` is W1C."

```c
typedef void (*irq_fn_t)(unsigned bit, void *ctx);
int      irq_register(unsigned bit, irq_fn_t fn, void *ctx);
unsigned irq_dispatch(void);
```

- `STATUS = 0x80000019`, `EN = 0x80000009`, handlers 0/3/31 → order 0, 3, 31; returns 3; `STATUS = 0x10`.
- bit 7 pending + enabled, no handler → returns 0; bit 7 cleared from `EN`; spurious count 1.
- `STATUS = 0` → returns 0, no writes.

### 2. 먼저 물어볼 것 (clarify)

- "Is there a priority among sources?" → Lowest bit first.
- "Should a handler be able to see which bit fired and carry driver context?" → Yes: `(bit, ctx)`.
- "Can registration happen while interrupts are live?" → Assume at init (follow-up).
- "What should happen with a pending bit that has no handler?" → Don't storm; mask it and count.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 우선순위, ctx, 핸들러 없는 비트 |
| 2–5 | 0x80000009에서 비트 꺼내는 순서 손으로 |
| 5–15 | 테이블 + register + dispatch |
| 15–18 | trace: pending 3비트 |
| 18–20 | 테스트 + "while 루프로 다시 읽기" 확장 |

- 소리 내서: **"I take one snapshot of status AND enable, ack exactly that snapshot, then walk it with `x &= x - 1`, which clears the lowest set bit each step — so the loop runs once per set bit, not 32 times."**
- 소리 내서: **"An enabled source with no handler would re-fire forever, so I mask it in `INT_EN` and bump a spurious counter for debugging."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

테이블은 `struct { irq_fn_t fn; void *ctx; } tbl[32];`. 가장 낮은 set 비트 번호는 count-trailing-zeros, 그 비트 지우기는 `p &= p - 1`.

</details>

<details><summary>힌트 2</summary>

순서: 스냅샷(`STATUS & EN`) → 0이면 바로 return → ack(스냅샷 W1C 쓰기) → 비트 루프. 핸들러가 NULL이면 `EN`에서 그 비트를 빼라.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

typedef void (*irq_fn_t)(unsigned bit, void *ctx);
static struct { irq_fn_t fn; void *ctx; } irq_tbl[32];
static uint32_t spurious;                       /* debug counter */

int irq_register(unsigned bit, irq_fn_t fn, void *ctx)
{
    if (bit >= 32u || fn == NULL)
        return -1;
    irq_tbl[bit].fn  = fn;
    irq_tbl[bit].ctx = ctx;
    return 0;
}

/* Returns number of handlers called. */
unsigned irq_dispatch(void)
{
    uint32_t pending = reg_read(STAT_OFF) & reg_read(EN_OFF);  /* one snapshot */
    unsigned n = 0;
    if (pending == 0)
        return 0;
    reg_write(STAT_OFF, pending);              /* W1C: ack only what we took, before handling */
    while (pending) {
        unsigned bit = (unsigned)__builtin_ctz(pending);       /* lowest set bit = priority */
        pending &= pending - 1u;                               /* clear lowest set bit */
        if (irq_tbl[bit].fn) {
            irq_tbl[bit].fn(bit, irq_tbl[bit].ctx);
            n++;
        } else {                                /* no handler: mask it, or it storms forever */
            reg_write(EN_OFF, reg_read(EN_OFF) & ~(1u << bit));
            spurious++;
        }
    }
    return n;
}
```

- 시간 O(set 비트 수), 공간 O(32) 테이블.
- `__builtin_ctz`는 GCC/clang 내장 (pending ≠ 0일 때만 호출 — 0이면 UB). 금지라면 [N05A A4](2026-11-01_N05A_bit_basics.html)의 이진 탐색 ctz, 또는 Cortex-M `__CLZ(__RBIT(x))`.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only): INT_STATUS (W1C) at 0x04, INT_EN (RW) at 0x08 ---- */
#define STAT_OFF 0x04u
#define EN_OFF   0x08u
static uint32_t m_stat, m_en, m_writes;
uint32_t reg_read(uint32_t off) { return off == STAT_OFF ? m_stat : m_en; }
void reg_write(uint32_t off, uint32_t val)
{
    if (off == STAT_OFF) m_stat &= ~val; else m_en = val;
    m_writes++;
}
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

STATUS = 0x80000019, EN = 0x80000009 → pending = 0x80000009, ack 쓰기 0x80000009 → STATUS = 0x00000010.

| 반복 | pending (전) | ctz → bit | pending AND (pending − 1) | 호출 |
|---|---|---|---|---|
| 1 | 0x80000009 | 0 | 0x80000008 | handler[0] |
| 2 | 0x80000008 | 3 | 0x80000000 | handler[3] |
| 3 | 0x80000000 | 31 | 0x00000000 | handler[31] |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 0 → 3 → 31 순서, 반환 3, disable된 비트 4는 pending 유지.
- 등록 에러: bit 32, fn NULL → −1.
- 핸들러 없는 비트: ack + `EN`에서 제거 + spurious 1.
- pending 0: 버스 쓰기 0회.
- 핸들러 실행 중 같은 이벤트 재발생: ack를 먼저 했으므로 STATUS에 다시 1 → 다음 인터럽트로 처리.

</details>

### 4. 꼬리 질문

Q1. "Should the ISR loop and re-read status until it's zero?"

<details><summary>답</summary>

It's a trade-off. Re-reading in a `do { } while (pending)` loop handles bursts without paying exception entry and exit again, which helps on a busy line. But it can starve lower-priority work if a source fires continuously, so I'd cap the number of passes. With an edge-to-the-CPU line, looping until zero is also the safe way to avoid missing an edge.

- 한국어 핵심: 재확인 루프 = 진입 비용 절약 + edge 누락 방지, 대신 횟수 상한.

</details>

Q2. "Is `irq_register` safe to call while interrupts are enabled?"

<details><summary>답</summary>

Not as written: the ISR could see `fn` updated but `ctx` still old and call the new handler with the wrong context. I'd write `ctx` first and `fn` last with a compiler barrier between, or simply mask that bit in `INT_EN` while updating the entry, or disable interrupts briefly. Making the table entries `volatile` alone doesn't make the two-field update atomic.

- 한국어 핵심: fn/ctx 2필드 갱신은 원자적이지 않음 → 해당 비트 mask 후 갱신, 또는 ctx 먼저·fn 나중.

</details>

Q3. "Handlers take too long. How do you restructure?"

<details><summary>답</summary>

Keep the ISR to ack plus recording the pending bits — OR them into a `volatile` event word — and do the work in a bottom half: main loop, an RTOS task woken by a semaphore, or a Linux threaded IRQ. The ISR becomes constant-time and interrupt latency for everything else drops.

- 한국어 핵심: top half = ack + 이벤트 기록, bottom half = 실제 처리 ([N05E E6](2026-11-01_N05E_ring_queue_isr.html) 이벤트 플래그).

</details>

### 5. 채점 · 자주 틀리는 것

- 핸들러 호출 **후**에 ack → 처리 중 재발생 이벤트 유실.
- `STATUS`를 루프 안에서 매번 다시 읽고 ack → 스냅샷 불일치.
- `for (bit = 0; bit < 32; bit++)` 자체는 정답이지만 `x &= x-1` 루프를 말하면 가점.
- `1 << bit` (signed, bit=31에서 UB) — `1u << bit`.
- 핸들러 없는 비트 방치 → interrupt storm.
- `__builtin_ctz(0)` 호출 (UB).

## C6 · UART: putc(TX FIFO full polling) · 논블로킹 getc · print_hex32 (printf 없이) (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: bring-up 첫날 쓰는 최소 UART 드라이버 — 한 글자 보내기, 한 글자 받기(없으면 바로 리턴), 32-bit 값을 16진수로 출력.
- **상황**: printf/libc가 아직 없는 BootROM 직후, SRAM에서 도는 테스트 펌웨어. 레지스터 덤프를 찍으려면 `print_hex32`가 필수. 핵심 함정은 **DATA 레지스터 읽기 = RX FIFO pop**(읽기 부작용)과 **getc 반환형**.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `uart_putc(c)` | TX FIFO에 1바이트 | TXFULL이면 대기, `'\n'` 앞에 `'\r'` |
| `uart_getc_nb()` | RX에서 1바이트 | 비어 있으면 −1, DATA는 읽지 않음 |
| `uart_print_hex32(v)` | `0x` + 16진 8자리 | 앞자리 0 유지, MSB 니블부터 |

- **예시를 말로 풀면**: 0xA → "0x0000000A". RX에 0xFF가 있으면 255를 돌려줘야 하고, −1(=없음)과 헷갈리면 안 된다.
- **지켜야 할 조건**: TX FIFO 깊이(4)보다 긴 문자열에서도 바이트 유실 없음.

### 1. Problem (English)

"Write a polled UART driver for bring-up: `putc` that waits while the TX FIFO is full, a non-blocking `getc`, and a function that prints a 32-bit value in hex without printf."

```c
void uart_putc(char c);
int  uart_getc_nb(void);   /* -1 if no data */
void uart_print_hex32(uint32_t v);
```

- `uart_print_hex32(0xDEADBEEF)` → `0xDEADBEEF` on the wire (10 bytes through a 4-deep FIFO, no loss).
- `uart_puts("ok\n")` → `o k \r \n`.
- RX empty → `-1` and `DATA` not read; RX `{'A', 0xFF, 0x00}` → `65, 255, 0`, then `-1`.

### 2. 먼저 물어볼 것 (clarify)

- "Does reading DATA pop the RX FIFO?" → Yes.
- "Upper or lower case hex, fixed 8 digits?" → Upper, fixed 8.
- "Should putc block forever or have a timeout?" → Block for now (follow-up).
- "Do RX error flags (overrun, framing) live in DATA's upper bits?" → Ignore here.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: 읽기 pop, 대소문자, 블로킹 |
| 2–4 | 0xDEADBEEF 니블 꺼내기: shift 28, 24, … 0 |
| 4–11 | putc → getc_nb → print_hex32 |
| 11–13 | trace: 니블 3개 |
| 13–15 | 테스트 + timeout/인터럽트 확장 |

- 소리 내서: **"`getc` returns `int`, not `char`: 0xFF is valid data, so I need an out-of-band value, −1, for 'nothing there'. And I check RXEMPTY first, because reading DATA pops the FIFO."**
- 소리 내서: **"For hex I walk the shift from 28 down to 0 in steps of 4 and index a 16-character table — no division, fixed width."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

니블 → 문자는 `"0123456789ABCDEF"[n]`. 가장 위 니블은 `(v >> 28) & 0xF`.

</details>

<details><summary>힌트 2</summary>

getc는 STAT을 먼저 한 번 읽어 RXEMPTY면 −1, 아니면 DATA를 읽어 `& 0xFF` 후 `int`로. `char`로 반환하면 signed char 플랫폼에서 0xFF가 −1이 된다.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');                        /* terminals want CRLF */
    while (reg_read(UART_STAT) & ST_TXFULL)
        ;                                       /* bring-up: block; see follow-up for timeout */
    reg_write(UART_DATA, (uint8_t)c);
}

/* Non-blocking: -1 if nothing. int, not char: 0xFF is a valid byte. */
int uart_getc_nb(void)
{
    if (reg_read(UART_STAT) & ST_RXEMPTY)
        return -1;                              /* never read DATA when empty: read pops */
    return (int)(reg_read(UART_DATA) & 0xFFu);
}

void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* "0x" + exactly 8 upper-case hex digits, MSB nibble first. */
void uart_print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    for (int sh = 28; sh >= 0; sh -= 4)
        uart_putc(hex[(v >> sh) & 0xFu]);
}
```

- putc O(1) (FIFO 대기 제외), print_hex32 O(8).
- `uart_puts`는 테스트 편의용 1줄 (`"ok\n"` → `"ok\r\n"` 확인).

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only) ----
 * 0x00 DATA : write = push TX FIFO; read = pop RX FIFO (READ SIDE EFFECT)
 * 0x04 STAT : bit0 TXFULL, bit1 RXEMPTY
 * TX FIFO depth 4; the "wire" drains one TX byte every 3 STAT reads into m_wire[]. */
#define UART_DATA 0x00u
#define UART_STAT 0x04u
#define ST_TXFULL  (1u << 0)
#define ST_RXEMPTY (1u << 1)
#define TXDEPTH 4u
static uint8_t  m_txq[TXDEPTH]; static unsigned m_txn;
static char     m_wire[128];    static unsigned m_wn;
static uint8_t  m_rx[16];       static unsigned m_rxh, m_rxn;
static unsigned m_stat_reads, m_overflow;
uint32_t reg_read(uint32_t off)
{
    if (off == UART_STAT) {
        if (++m_stat_reads % 3 == 0 && m_txn) {          /* shift one byte out */
            m_wire[m_wn++] = (char)m_txq[0];
            memmove(m_txq, m_txq + 1, --m_txn);
        }
        return (m_txn == TXDEPTH ? ST_TXFULL : 0) | (m_rxh == m_rxn ? ST_RXEMPTY : 0);
    }
    return (m_rxh < m_rxn) ? m_rx[m_rxh++] : 0;        /* pop */
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off != UART_DATA) return;
    if (m_txn == TXDEPTH) { m_overflow++; return; }     /* byte lost */
    m_txq[m_txn++] = (uint8_t)val;
}
static void flush_wire(void) { while (m_txn) (void)reg_read(UART_STAT); m_wire[m_wn] = 0; }
static void model_reset(void) { m_txn = m_wn = m_rxh = m_rxn = m_stat_reads = m_overflow = 0; }
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`uart_print_hex32(0xDEADBEEF)`

| sh | (v >> sh) AND 0xF | 문자 |
|---|---|---|
| 28 | 0xD | 'D' |
| 24 | 0xE | 'E' |
| 20 | 0xA | 'A' |
| … | … | … |
| 0 | 0xF | 'F' → 전체 "0xDEADBEEF" |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 0xDEADBEEF: FIFO 깊이(4)보다 긴 10바이트, 유실 0.
- 0 → "0x00000000", 0xA → "0x0000000A" (앞자리 0 유지).
- `UINT32_MAX` → "0xFFFFFFFF".
- `'\n'` → `"\r\n"`.
- getc: 비었을 때 −1이고 DATA 안 읽음, 0xFF → 255, 0x00 → 0.

</details>

### 4. 꼬리 질문

Q1. "The UART clock isn't enabled yet and `putc` hangs. How do you design against that?"

<details><summary>답</summary>

Give `putc` a bounded spin — a loop count calibrated generously or a timer deadline like C1 — and on timeout drop the character and set a sticky 'uart dead' flag so later calls return immediately. That way a misconfigured UART can't hang the whole test firmware, and the test results can still go out through another channel like a RAM log buffer or JTAG semihosting.

- 한국어 핵심: putc에도 상한, 실패 시 sticky 플래그로 이후 즉시 리턴 + RAM 로그 대체 ([BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)).

</details>

Q2. "How would you make the TX side interrupt-driven?"

<details><summary>답</summary>

`putc` writes into a software ring buffer and enables the TX-empty interrupt; the ISR moves bytes from the ring to the FIFO while it isn't full, and disables the TX interrupt when the ring is empty. The ring is single-producer single-consumer, so the index rules from E3 apply. For panic output I'd keep the polled path as a fallback.

- 한국어 핵심: SW 링 + TX empty 인터럽트, 링 비면 인터럽트 끄기, panic은 polled 경로 ([N05E](2026-11-01_N05E_ring_queue_isr.html)).

</details>

Q3. "Print in decimal without printf or division?"

<details><summary>답</summary>

On cores without hardware divide I'd subtract powers of ten: for 1,000,000,000 down to 1, count how many times it fits, emit that digit, and suppress leading zeros. With hardware divide, fill a 10-byte buffer from the end with `v % 10`, then print it forward.

- 한국어 핵심: 10의 거듭제곱 빼기 (나눗셈 없음) 또는 `% 10`로 뒤에서 채우기.

</details>

### 5. 채점 · 자주 틀리는 것

- `char uart_getc()` → 0xFF와 "없음" 구분 불가.
- RXEMPTY 확인 없이 DATA 읽기 → 쓰레기 값 + (일부 HW) 언더플로 에러.
- `v >> sh & 0xF`에서 sh 시작을 32로 (UB) 또는 24로 (니블 누락).
- 앞자리 0 생략 → 레지스터 덤프 정렬 깨짐.
- `'\r'` 누락은 사소하지만 터미널 출력이 계단 모양.

## C7 · SPI full-duplex transfer: TX FIFO 넣고 RX FIFO 빼기 (FIFO depth 고려) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: SPI는 1바이트 보내면 동시에 1바이트 받는다. n바이트를 FIFO로 주고받되 **RX FIFO가 넘치지 않게** 보내는 양을 조절한다.
- **상황**: 센서/플래시/PMIC 통신, SPI 컨트롤러 bring-up 테스트. 대부분의 SPI 블록은 RX가 가득 차도 **shifter가 기다려 주지 않고** 바이트를 버린다(overrun). TXFULL만 보고 계속 밀어 넣으면, RX를 빼기 전에 RX가 넘친다. 그래서 **"보냈지만 아직 못 받은 바이트 수"(in-flight)**를 SW가 세서 FIFO 깊이 이하로 유지한다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `spi_xfer(tx, rx, n)` | n바이트 동시 송수신 | tx NULL → 0xFF 더미 송신, rx NULL → 수신 버림, in-flight ≤ 4, 진전 없으면 −1 |

- **예시를 말로 풀면**: 10바이트를 깊이 4 FIFO로 → 4개 넣고 4개 빼고, 다시 4개, 2개. 슬레이브가 `~tx`로 응답하면 rx[i] = ~tx[i].
- **지켜야 할 조건**: overrun 0, n = 0 / 1 / 정확히 4 처리, 블록이 멈추면 무한 대기 대신 −1.

### 1. Problem (English)

"Implement a full-duplex SPI transfer on a controller with 4-deep TX and RX FIFOs. Every byte shifted out shifts one in; if the RX FIFO is full the incoming byte is lost. Support write-only and read-only transfers."

```c
int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t n);
```

- `n = 10`, slave answers `~tx` → `rx[i] == ~tx[i]`, no overrun.
- `tx = NULL`, `n = 6` → sends `0xFF` ×6, `rx` = `0x00` ×6.
- Controller clock stalled → returns `-1` (bounded).

### 2. 먼저 물어볼 것 (clarify)

- "Does the shifter stall when RX is full, or drop bytes?" → Drops (overrun).
- "FIFO depth, and is it the same for TX and RX?" → 4 and 4.
- "Who drives chip-select?" → Caller, outside this function.
- "Polling OK, or should I use DMA/interrupts?" → Polling.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: RX full 시 동작, 깊이, CS |
| 2–5 | n=6, 깊이 4로 sent/recvd 표 그리기 |
| 5–15 | 바깥 루프 (fill → drain → idle 체크) |
| 15–18 | trace 2 라운드 |
| 18–20 | 테스트 + "cap 빼면 어떻게 깨지나" |

- 소리 내서: **"TXFULL alone isn't enough: bytes already moved out of TX are sitting in the shifter or in RX, and if I keep filling TX faster than I drain RX, RX overflows. So I cap `sent − recvd` at the FIFO depth — the Linux DesignWare SPI driver does exactly this with its `rxtx_gap`."**
- 소리 내서: **"The loop ends on `recvd == n`, not `sent == n`, because the transfer isn't done until the last byte has come back."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

변수 두 개: `sent`, `recvd`. 보낼 조건 3개, 받을 조건 2개를 각각 써 보라.

</details>

<details><summary>힌트 2</summary>

보내기: `sent < n && sent - recvd < DEPTH && !TXFULL`. 받기: `recvd < sent && !RXEMPTY`. 한 라운드에 둘 다 진전이 없으면 idle 카운트 증가 → 상한 넘으면 −1.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

/* tx == NULL: send 0xFF dummies (read-only). rx == NULL: discard (write-only).
 * Returns 0, or -1 if the engine makes no progress for SPI_IDLE_MAX rounds. */
int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t n)
{
    size_t sent = 0, recvd = 0;
    uint32_t idle = 0;
    while (recvd < n) {
        size_t before = sent + recvd;
        /* fill: TX has room AND in-flight (sent - recvd) < depth, else RX overruns */
        while (sent < n && sent - recvd < SPI_FIFO_DEPTH &&
               !(reg_read(SPI_STAT) & ST_TXFULL)) {
            reg_write(SPI_DATA, tx ? tx[sent] : 0xFFu);
            sent++;
        }
        /* drain: every TX byte clocks exactly one RX byte back */
        while (recvd < sent && !(reg_read(SPI_STAT) & ST_RXEMPTY)) {
            uint8_t b = (uint8_t)reg_read(SPI_DATA);
            if (rx)
                rx[recvd] = b;
            recvd++;
        }
        idle = (sent + recvd == before) ? idle + 1 : 0;
        if (idle > SPI_IDLE_MAX)
            return -1;                          /* clock gated? block in reset? */
    }
    return 0;
}
```

- 시간 O(n) 버스 접근 (블록 속도에 묶임), 공간 O(1).
- **검증한 반례**: `sent - recvd < SPI_FIFO_DEPTH` 조건만 지운 버전을 같은 모델로 돌리면 10바이트 전송에서 바이트가 버려져 `recvd`가 영원히 n에 못 미치고 **idle timeout으로 −1** (assert 실패). 같은 파일의 "전부 넣고 나서 읽기" naive 버전도 6바이트에서 overrun.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only) ----
 * 0x00 DATA : write = push TX FIFO; read = pop RX FIFO
 * 0x04 STAT : bit0 TXFULL, bit1 RXEMPTY
 * TX and RX FIFO depth 4. Fast SPI clock: on EVERY bus access the shifter moves one
 * byte TX -> wire -> RX (slave answers ~tx). If RX is full the byte is LOST (overrun),
 * like most real SPI blocks - the shifter does not wait for software.             */
#define SPI_DATA 0x00u
#define SPI_STAT 0x04u
#define ST_TXFULL  (1u << 0)
#define ST_RXEMPTY (1u << 1)
#define SPI_FIFO_DEPTH 4u
#define SPI_IDLE_MAX   1000u
static uint8_t m_tx[SPI_FIFO_DEPTH], m_rx[SPI_FIFO_DEPTH];
static unsigned m_txn, m_rxn, m_ovr;
static bool m_stalled;
static void shifter(void)
{
    if (!m_txn || m_stalled) return;
    uint8_t in = (uint8_t)~m_tx[0];
    memmove(m_tx, m_tx + 1, --m_txn);
    if (m_rxn == SPI_FIFO_DEPTH) m_ovr++; else m_rx[m_rxn++] = in;
}
uint32_t reg_read(uint32_t off)
{
    uint32_t v;
    if (off == SPI_STAT) {
        v = (m_txn == SPI_FIFO_DEPTH ? ST_TXFULL : 0) | (m_rxn == 0 ? ST_RXEMPTY : 0);
    } else {
        v = m_rx[0];
        if (m_rxn) memmove(m_rx, m_rx + 1, --m_rxn);
    }
    shifter();
    return v;
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off == SPI_DATA && m_txn < SPI_FIFO_DEPTH) m_tx[m_txn++] = (uint8_t)val;
    shifter();
}
static void model_reset(void) { m_txn = m_rxn = m_ovr = 0; m_stalled = false; }
```

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

n = 6, 깊이 4, 모델 실행 결과 (라운드 끝 시점).

| 라운드 | fill 후 sent | drain 후 recvd | in-flight 최대 | 비고 |
|---|---|---|---|---|
| 1 | 4 | 4 | 4 | cap에서 fill 멈춤 |
| 2 | 6 | 6 | 2 | 끝, rx[0] = 0xEF (= ~0x10), rx[5] = 0xEA |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- n = 10 > 깊이: 전부 순서대로, overrun 0.
- n = 0 → 즉시 0.
- n = 1, n = 4 (깊이와 같음).
- 읽기 전용(tx NULL → 0xFF 더미), 쓰기 전용(rx NULL, RX는 빼서 버림 → 끝나면 RX 비어 있음).
- 블록 멈춤 → −1 (유한).

</details>

### 4. 꼬리 질문

Q1. "For a write-only transfer, why do you still read the RX FIFO?"

<details><summary>답</summary>

Because the hardware still clocks a byte in for every byte out. If I don't drain RX, it fills after four bytes, the overrun flag sets, and the next read transfer starts with four stale bytes in the FIFO. Some controllers have a TX-only mode that disables RX; without that, draining and discarding is the correct thing.

- 한국어 핵심: 송신만 해도 RX는 찬다 → 빼서 버려야 다음 전송이 깨끗.

</details>

Q2. "What does an overrun look like in your function, and how would you detect it directly?"

<details><summary>답</summary>

A lost byte means `recvd` can never reach `n`, so my function spins until the idle limit and returns −1 — it shows up as a timeout. Better is to check the controller's RX-overrun sticky bit in the status register each round and return a distinct error like `-EOVERFLOW`, then clear it — often W1C — before the next transfer.

- 한국어 핵심: 유실 = timeout으로 보임 → RXOVR 비트 직접 확인해 별도 에러, 그리고 W1C로 지우기.

</details>

Q3. "When would you switch to DMA?"

<details><summary>답</summary>

When transfers are long — flash page reads, display frames — polling burns the CPU for the whole transfer. With DMA I set up two channels, TX and RX, start RX first so it's ready for the first byte, and wait for the RX completion, since RX finishing means the transfer is truly done. Short register reads of a few bytes stay polled because DMA setup costs more than the transfer.

- 한국어 핵심: 긴 전송 = DMA (RX 채널 먼저 시작, RX 완료로 끝 판정), 짧은 레지스터 접근 = polling.

</details>

### 5. 채점 · 자주 틀리는 것

- TXFULL만 보고 in-flight 제한 없음 → RX overrun.
- 종료 조건을 `sent == n`으로 → 마지막 바이트들 못 받음.
- rx NULL일 때 RX 안 빼기.
- 무한 polling (진전 없음 감지 없음).
- `size_t` 뺄셈 `sent - recvd`는 항상 sent ≥ recvd라 안전 — 반대로 쓰면 언더플로.

## C8 · 테이블 기반 레지스터 sanity 테스트: reset 값 + RW 마스크 walking-1 (RO/W1C 건너뛰기) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 레지스터 설명 표를 받아 **reset 값 확인 + RW 비트 walking-1 테스트**를 하는 검증 함수. 쓰면 안 되는 레지스터는 쓰지 않고, 읽으면 안 되는 레지스터는 읽지도 않는다.
- **상황**: 새 RTL/FPGA 빌드, 첫 실리콘에서 가장 먼저 도는 테스트. 모든 비트에 막 쓰면 W1C 상태가 지워지고, FIFO 읽기로 데이터가 빠지고, 리셋/클럭 레지스터를 건드리면 시스템이 죽는다. 큰 버전(RO 반전 쓰기, W1C/SC 마스크, mismatch 종류 3개)은 [V2](hmScreenPrep/problems/V2_reg_field_rmw/index.html), 제외 규칙 설명은 [BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html) — 여기는 **종이 20분 축소판**.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `reg_sanity(t, n, log, cap)` | 표 순서대로 검사 | SKIP은 접근 0회, RO/W1C는 읽기만, RW는 rw_mask 비트마다 walking-1 + 복구, 실패 총수 반환, 앞의 cap개만 기록 |

- **예시를 말로 풀면**: SCRATCH의 비트 13이 0에 붙어 있으면(stuck-at-0) 0x2000을 썼는데 0이 읽힌다 → 실패 1개. ID의 reset 값이 틀리면 reset 실패 1개.
- **지켜야 할 조건**: 끝나면 레지스터 상태 원복, `log == NULL && cap == 0` 허용.

### 1. Problem (English)

"Given a table describing each register — offset, reset value, writable mask, and type — write a sanity test that checks reset values and walks a one through every writable bit. It must not write read-only or W1C registers and must not touch registers with read side effects. Return the total number of failures and log the first `cap`."

```c
enum { REG_RW, REG_RO, REG_W1C, REG_SKIP };
typedef struct { uint32_t off, reset, rw_mask; uint8_t type; } reg_desc_t;
typedef struct { uint32_t off, expect, got; uint8_t kind; } reg_fail_t;
size_t reg_sanity(const reg_desc_t *t, size_t n, reg_fail_t *log, size_t cap);
```

- Clean hardware → `0`, no writes to RO/W1C, FIFO never read, all registers restored.
- SCRATCH bit 13 stuck at 0 → `1`, `log[0] = {0x00, 0x2000, 0x0, rw}`.
- Three faults, `cap = 2` → returns `3`, only `log[0..1]` written.

### 2. 먼저 물어볼 것 (clarify)

- "Are there registers that must not be touched at all — FIFOs, clear-on-read, soft reset, clock control?" → Yes, marked SKIP.
- "For mixed registers, does `rw_mask` list only safely writable bits?" → Yes.
- "Walking-1 only, or also walking-0 and alias checks?" → Walking-1 (others as follow-up).
- "Stop at first failure or count all?" → Count all, log first `cap`.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify: SKIP 대상, rw_mask 의미, 로그 정책 |
| 2–5 | CTRL(reset 0xFF00, rw 0xFFFF) 손으로 2비트 |
| 5–15 | log 헬퍼 → 바깥 루프 → reset 체크 → walking-1 → 복구 |
| 15–18 | trace: stuck bit 1개 |
| 18–20 | 테스트 + alias/walking-0 확장 |

- 소리 내서: **"Registers with read side effects — FIFOs, clear-on-read — I don't even read; RO and W1C I read once for the reset check but never write. Only bits in `rw_mask` get walked."**
- 소리 내서: **"I count every failure but only log the first `cap`, so one bad register can't overflow the log, and the return value still tells me how bad it is."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

레지스터마다: `type == SKIP`이면 continue → `orig = read` → reset 비교 → RW가 아니면 continue → 비트 루프 → 복구 쓰기.

</details>

<details><summary>힌트 2</summary>

walking-1: `bit = 1u << b`를 쓰고, 다시 읽은 값의 `& rw_mask`가 `bit`와 같아야 한다 (RO 비트는 비교에서 제외). 로그는 `if (nf < cap) log[nf] = …; nf++;`.

</details>

<details><summary>모범 답 (C)</summary>

```c
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

enum { REG_RW = 0, REG_RO = 1, REG_W1C = 2, REG_SKIP = 3 };
typedef struct { uint32_t off, reset, rw_mask; uint8_t type; } reg_desc_t;  /* type: REG_* */
typedef struct { uint32_t off, expect, got; uint8_t kind; } reg_fail_t;     /* kind: 0 reset, 1 rw */

static void log_fail(reg_fail_t *log, size_t cap, size_t *nf,
                     uint32_t off, uint32_t expect, uint32_t got, uint8_t kind)
{
    if (*nf < cap)
        log[*nf] = (reg_fail_t){ off, expect, got, kind };
    (*nf)++;                                    /* count all, log first `cap` */
}

size_t reg_sanity(const reg_desc_t *t, size_t n, reg_fail_t *log, size_t cap)
{
    size_t nf = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i].type == REG_SKIP)
            continue;                           /* FIFO / read-to-clear / self-clearing */
        uint32_t orig = reg_read(t[i].off);
        if (orig != t[i].reset)
            log_fail(log, cap, &nf, t[i].off, t[i].reset, orig, 0);
        if (t[i].type != REG_RW || t[i].rw_mask == 0)
            continue;                           /* RO, W1C: read-only check, never write */
        for (unsigned b = 0; b < 32; b++) {
            uint32_t bit = 1u << b;
            if (!(t[i].rw_mask & bit))
                continue;
            reg_write(t[i].off, bit);           /* walking-1 over RW bits only */
            uint32_t got = reg_read(t[i].off) & t[i].rw_mask;
            if (got != bit)
                log_fail(log, cap, &nf, t[i].off, bit, got, 1);
        }
        reg_write(t[i].off, orig & t[i].rw_mask);   /* restore; never write 1s outside RW */
    }
    return nf;
}
```

- 시간 O(n × 32) 버스 접근, 공간 O(1).
- 복구를 `orig & rw_mask`로 쓰는 이유: 한 레지스터에 W1C 비트가 섞여 있을 때 `orig`를 그대로 쓰면 그 1들이 지워진다.

</details>

<details><summary>HW 모델 (테스트 하네스)</summary>

```c
/* ---- HW model (test only): 6 registers, each with real reset / writable mask / kind.
 * Fault injection: stuck-at-0 bits, wrong reset value. Counters prove the test never
 * wrote a W1C/RO register and never read the FIFO (read pops data). */
#define NREG 6
static const uint32_t m_off[NREG]   = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14 };
static const uint32_t m_reset[NREG] = { 0x00000000, 0x0000FF00, 0xC0DE0101, 0x00000003, 0, 0x00000010 };
static const uint32_t m_wmask[NREG] = { 0xFFFFFFFF, 0x0000FFFF, 0x00000000, 0, 0, 0x000000F0 };
static uint32_t m_val[NREG], m_stuck0[NREG], m_bad_writes, m_fifo_reads;
static int idx(uint32_t off) { for (int i = 0; i < NREG; i++) if (m_off[i] == off) return i; return -1; }
uint32_t reg_read(uint32_t off)
{
    int i = idx(off);
    if (off == 0x10) m_fifo_reads++;                       /* FIFO: read pops */
    return m_val[i] & ~m_stuck0[i];
}
void reg_write(uint32_t off, uint32_t v)
{
    int i = idx(off);
    if (off == 0x08 || off == 0x0C || off == 0x10) m_bad_writes++;  /* RO / W1C / FIFO */
    m_val[i] = (m_val[i] & ~m_wmask[i]) | (v & m_wmask[i]);
}
static void model_reset(void)
{
    memcpy(m_val, m_reset, sizeof m_val); memset(m_stuck0, 0, sizeof m_stuck0);
    m_bad_writes = m_fifo_reads = 0;
}
```

테스트용 표 (`main` 안): SCRATCH 0x00 RW `0xFFFFFFFF` · CTRL 0x04 reset `0xFF00` RW `0xFFFF` · ID 0x08 RO `0xC0DE0101` · INT_STATUS 0x0C W1C reset 3 · RX_FIFO 0x10 SKIP · THRESH 0x14 reset `0x10` RW `0xF0`.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

CTRL 0x04: reset 0x0000FF00, rw_mask 0x0000FFFF, 비트 0 stuck-at-0 주입.

| 단계 | 쓴 값 | 읽은 값 AND rw_mask | 기대 | 결과 |
|---|---|---|---|---|
| reset | — | orig = 0x0000FF00 | 0x0000FF00 | OK |
| b = 0 | 0x00000001 | 0x00000000 | 0x00000001 | FAIL {0x04, 0x1, 0x0, rw} |
| b = 1 | 0x00000002 | 0x00000002 | 0x00000002 | OK |
| 복구 | 0x0000FF00 | — | — | 원상태 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 HW: 실패 0, RO/W1C/FIFO 쓰기 0회, FIFO 읽기 0회, 전 레지스터 원복.
- stuck bit 1개 → 1개, log 내용 정확.
- 실패 3개 + cap 2 → 반환 3, log[2]는 안 건드림, 표 순서대로 기록.
- `log = NULL, cap = 0` → 카운트만 (stuck 4비트 → 4).
- 빈 표 → 0.

</details>

### 4. 꼬리 질문

Q1. "Your walking-1 passes, but two registers are actually the same flop because of an address decode bug. How do you catch it?"

<details><summary>답</summary>

Walking-1 on one register at a time can't see aliasing, because each register reads back what I just wrote to it. I add a pass that writes a unique pattern to every RW register first — for example its own offset in the RW bits — and only then reads them all back. If offset 0x00 and 0x40 alias, one of them comes back with the other's pattern.

- 한국어 핵심: **write-all-then-read-all** 패스 (레지스터마다 고유 패턴) — 주소 alias 검출 ([BV08](2026-11-01_BV08_boot_flow_bringup_firmware.html)).

</details>

Q2. "Why also walking-0, or `0x55555555`/`0xAAAAAAAA`?"

<details><summary>답</summary>

Walking-1 writes mostly zeros, so it finds stuck-at-0 bits and bits shorted to a neighbor that's 0, but a bit stuck at 1 only shows when I write 0 to it with others at 1. Walking-0 or the inverted patterns catch stuck-at-1 and neighbor shorts in the other direction; the checkerboard pair is a cheap 2-write version that toggles every bit and its neighbors.

- 한국어 핵심: walking-1 = stuck-0, walking-0·0x55/0xAA = stuck-1·인접 short.

</details>

Q3. "The reset check fails on a register that's documented RO with reset 0. What do you check before filing an RTL bug?"

<details><summary>답</summary>

First whether something legitimately changed it before my test ran — boot ROM, an earlier test, or hardware status reflecting a live condition like a link-up bit — and whether I'm reading after a real reset. Then whether the spec or the generated header is stale, by comparing against the RTL's register description such as IP-XACT or SystemRDL. Only after that do I file it, with the value read, the expected value, the build ID and the reset sequence.

- 한국어 핵심: 앞 단계가 바꿨나 / live status인가 / 스펙·헤더가 오래됐나 → 그다음 버그 리포트 (값·빌드·리셋 순서 첨부).

</details>

### 5. 채점 · 자주 틀리는 것

- SKIP 레지스터를 reset 체크한다고 **읽음** → FIFO pop.
- W1C 레지스터에 walking-1 쓰기 → pending 지움 (+ 테스트 의미 없음).
- 읽은 값 전체를 `bit`와 비교 → RO 비트 때문에 거짓 실패 (`& rw_mask` 필요).
- 첫 실패에서 return → 실패 규모를 모름.
- `log[nf]`를 cap 검사 없이 → 버퍼 오버런.
- 복구 쓰기 누락 → 다음 테스트가 오염된 상태에서 시작.

## 출처

1. Linux kernel `include/linux/iopoll.h` — `read_poll_timeout`/`poll_timeout_us`: 만료 여부를 읽기 **전에** 계산, 만료 후 1회 더 읽고 판정 — https://github.com/torvalds/linux/blob/master/include/linux/iopoll.h
2. Linux kernel `drivers/spi/spi-dw-core.c` — `dw_spi_tx_max()`의 `rxtx_gap = fifo_len − (rx_len − tx_len)` 및 "shift register 안 데이터는 FIFO 레벨로 안 보인다" 주석 — https://github.com/torvalds/linux/blob/master/drivers/spi/spi-dw-core.c
3. Linux kernel `include/linux/jiffies.h` — `time_after()` wrap-safe 비교 — https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h
4. Linux kernel docs, Bus-Independent Device Accesses (`readl`/`writel` 순서, posted write) — https://docs.kernel.org/driver-api/device-io.html
5. cppreference: `volatile` type qualifier — https://en.cppreference.com/w/c/language/volatile · `offsetof` — https://en.cppreference.com/w/c/types/offsetof · `_Static_assert` — https://en.cppreference.com/w/c/language/_Static_assert · bit-fields (구현 정의 배치) — https://en.cppreference.com/w/c/language/bit_field
6. C11 draft N1570 (6.5.7 shift, 6.7.3 type qualifiers, 6.7.2.1 bit-field 구현 정의) — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
7. 내부: [C 정리 C4·C6](2026-10-29_c_basics_cram.html) · [V2 레지스터 RMW·sanity](hmScreenPrep/problems/V2_reg_field_rmw/index.html) · [BV08 bring-up 테스트 FW](2026-11-01_BV08_boot_flow_bringup_firmware.html)
