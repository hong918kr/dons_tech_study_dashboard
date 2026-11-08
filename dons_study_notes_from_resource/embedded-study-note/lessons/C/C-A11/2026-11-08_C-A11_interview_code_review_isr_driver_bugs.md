---
id: C-A11
time: 100분
updated: 2026-11-08
---
# C-A11 면접형 코드 리뷰: ISR·드라이버 버그 찾기

## 학습 목표

- 처음 보는 펌웨어 코드 5조각을 **15분 안에** 훑어 숨은 버그를 찾고, 각 버그를 "증상 → 원인(C11 조항·레지스터 의미) → 고침" 순서로 말로 설명할 수 있다.
- 리뷰 체크리스트를 순서대로 적용한다: **공유 데이터(volatile·원자성) → ISR 몸통(블로킹·할당·출력) → 레지스터 쓰기(W1C·rc_w0·RMW) → 정수(부호·shift·승격) → 버퍼(경계·가득 참)**.
- 타이밍에 좌우되는 버그(경쟁·찢어진 갱신)를 **접근 횟수 기반 인터럽트 주입** 으로 매번 똑같이 재현하는 테스트를 읽고 쓸 수 있다.
- 테스트로는 안 보이는 버그(UB, 넘침)를 ASan / UBSan / TSan 실제 보고서로 증명하고, `-O2` asm 으로 volatile 누락의 결과를 보여 줄 수 있다.
- **고치면 안 되는 줄(decoy)** — `TIM->SR = ~UIF`, `DMA->IFCR = isr` — 을 알아보고 왜 맞는지 설명한다.

## 1. 문제

시니어 펌웨어 면접의 단골 형식: 화면에 20줄짜리 ISR·드라이버 코드를 띄우고 "뭐가 문제죠?" 라고 묻는다. 버그를 많이 찾는 것보다 **어떤 순서로 보는지, 영향이 얼마나 큰지, 어떻게 증명하고 고칠지** 를 말하는 게 점수다. 이 레슨은 C-M01(MMIO volatile)·C-M02(RMW/W1C)·C-M12(UB)·C-M14(ISR 공유 데이터)에서 배운 것을 **리뷰 실력** 으로 바꾸는 종합 문제다.

**진행 방법**

1. 타이머 15분. 이 절(1. 문제)의 스니펫 1~5 만 본다. **4. 힌트 이후는 접어 두거나 가린다.**
2. 스니펫마다 "줄 · 버그 종류 · 무엇이 깨지나 · 고침" 을 한 줄씩 적는다. 정답 키(5. 풀이 첫 표)는 버그 15개 + 고치면 안 되는 줄 2개다.
3. `starter.c` 를 열면 각 버그를 재현하는 테스트가 이미 있다. 적은 목록과 FAIL 줄을 맞춰 보고, 스니펫을 고쳐 35/35 를 만든다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">리뷰 대상 지도 — 누가(ISR / main) 무엇을(변수·레지스터) 건드리나</text>
  <rect x="10" y="40" width="150" height="270" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">ISR 쪽</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="92">USART1_IRQHandler</text>
    <text x="22" y="142">(파서는 main)</text>
    <text x="22" y="192">DMA1_IRQHandler</text>
    <text x="22" y="242">TIM3_IRQHandler</text>
    <text x="22" y="292">EXTI0_IRQHandler</text>
  </g>
  <rect x="580" y="40" width="150" height="270" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="592" y="62" fill="currentColor" font-weight="bold">main 쪽</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="592" y="92">uart_getc(_wait)</text>
    <text x="592" y="142">pkt_parse</text>
    <text x="592" y="192">take_events</text>
    <text x="592" y="242">led_err</text>
    <text x="592" y="292">btn_poll</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="200" y="74" width="340" height="28" rx="4" class="f-acc"/>
    <rect x="200" y="124" width="340" height="28" rx="4" class="f-acc"/>
    <rect x="200" y="174" width="340" height="28" rx="4" class="f-acc"/>
    <rect x="200" y="224" width="340" height="28" rx="4" class="f-acc"/>
    <rect x="200" y="274" width="340" height="28" rx="4" class="f-acc"/>
    <line x1="160" y1="88" x2="200" y2="88"/><line x1="540" y1="88" x2="580" y2="88"/>
    <line x1="540" y1="138" x2="580" y2="138"/>
    <line x1="160" y1="188" x2="200" y2="188"/><line x1="540" y1="188" x2="580" y2="188"/>
    <line x1="160" y1="238" x2="200" y2="238"/><line x1="540" y1="238" x2="580" y2="238"/>
    <line x1="160" y1="288" x2="200" y2="288"/><line x1="540" y1="288" x2="580" y2="288"/>
  </g>
  <g fill="currentColor">
    <text x="210" y="93">1 rx_buf · rx_head · rx_tail (링 버퍼)</text>
    <text x="210" y="143">2 char rx[] → pkt_t (UART 프레임)</text>
    <text x="210" y="193">3 g_events (이벤트 비트) · DMA ISR/IFCR</text>
    <text x="210" y="243">4 GPIOA ODR/BSRR · TIM3 SR</text>
    <text x="210" y="293">5 EXTI PR · 이벤트 리스트 · 힙 · UART 로그</text>
  </g>
</svg>
```

**호스트 모델 (리뷰 대상 아님).** 레지스터는 STM32 CMSIS 헤더와 같은 이름의 `READ_REG / WRITE_REG / SET_BIT / CLEAR_BIT` 로 접근한다. 타깃에서는 `SET_BIT(r, b)` 가 `((r) |= (b))` 그대로이고, 호스트에서는 버스 모델(`bus_rd/bus_wr`)을 거쳐 레지스터 의미(rc_w1, rc_w0, BSRR, IFCR)를 흉내 내고 **n 번째 접근 직후 인터럽트를 끼워 넣는다**. `irq_save()/irq_restore()` 는 CMSIS 의 `__get_PRIMASK / __disable_irq / __set_PRIMASK` 모델이다 (C-M14). `log_printf` = UART 로 retarget 한 printf, `pvPortMalloc` = FreeRTOS 힙. 아래 코드는 `starter.c` 의 스니펫 그대로이고, 테스트용 red zone(배열 뒤 여분 바이트)만 뺐다.

### Part A — 데이터 경로: 스니펫 1 (UART RX 링 버퍼), 스니펫 2 (패킷 파서)

```c
/* ---- 스니펫 1 — UART 수신 링 버퍼: ISR 가 넣고 main 이 꺼낸다 ---- */
#define RX_SIZE 8u
static uint8_t  rx_buf[RX_SIZE];
static uint32_t rx_head, rx_tail;             /* head: ISR 만 씀, tail: main 만 씀 */
static uint32_t rx_overflow;                  /* 공간이 없어 버린 바이트 수 */

void USART1_IRQHandler(void) {
    uint8_t c = (uint8_t)READ_REG(UART1->DR);
    rx_buf[rx_head] = c;
    if (++rx_head > RX_SIZE) rx_head = 0;
}
int uart_getc(void) {                         /* 꺼낼 게 없으면 -1 */
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_buf[rx_tail];
    if (++rx_tail > RX_SIZE) rx_tail = 0;
    return c;
}
int uart_getc_wait(void) {                    /* main: 한 바이트 올 때까지 기다린다 */
    int c;
    while ((c = uart_getc()) < 0) { }
    return c;
}

/* ---- 스니펫 2 — 패킷 헤더 파서 ----
 * 프레임: [0]=0xA5  [1]=type  [2..3]=len (big-endian)  [4..7]=ts (little-endian)  [8..]=payload */
#define PKT_MAX 240u
typedef struct { uint8_t type; uint16_t len; uint32_t ts; uint8_t payload[PKT_MAX]; } pkt_t;

int pkt_parse(const char *rx, size_t n, pkt_t *p) {
    if (n < 8u || (uint8_t)rx[0] != 0xA5u) return -1;
    p->type = (uint8_t)rx[1];
    p->len  = (uint16_t)((rx[2] << 8) | rx[3]);
    p->ts   = (uint32_t)(rx[4] | (rx[5] << 8) | (rx[6] << 16) | (rx[7] << 24));
    if (p->len > n - 8u) return -1;           /* 잘린 프레임 */
    memcpy(p->payload, rx + 8, p->len);
    return 0;
}
```

### Part B — main ↔ ISR 경쟁: 스니펫 3 (이벤트 플래그), 스니펫 4 (LED 두 개)

```c
/* ---- 스니펫 3 — DMA 완료 이벤트 플래그: ISR 가 세우고 main 이 가져간다 ---- */
static volatile uint32_t g_events;

void DMA1_IRQHandler(void) {
    uint32_t isr = READ_REG(DMA1->ISR);
    WRITE_REG(DMA1->IFCR, isr);               /* 본 플래그만 클리어 */
    if (isr & DMA_ISR_TCIF1) SET_BIT(g_events, EV_ADC_DONE);
    if (isr & DMA_ISR_TEIF1) SET_BIT(g_events, EV_DMA_ERR);
}
uint32_t take_events(void) {                  /* main 루프가 매번 호출 */
    uint32_t ev = READ_REG(g_events);
    WRITE_REG(g_events, 0u);
    return ev;
}

/* ---- 스니펫 4 — 에러 LED (main) 와 하트비트 LED (TIM3 ISR, 1 kHz) ---- */
#define PIN(n)        (1 << (n))
#define LED_ERR       PIN(15)                 /* main 이 켜고 끈다 (고장 ISR 도 켠다) */
#define HB_PIN        PIN(5)                  /* TIM3 ISR 가 토글한다 */

void TIM3_IRQHandler(void) {
    WRITE_REG(TIM3->SR, ~TIM_SR_UIF);                         /* UIF 클리어 */
    WRITE_REG(GPIOA->ODR, READ_REG(GPIOA->ODR) ^ HB_PIN);     /* 하트비트 토글 */
}
void led_err(int on) {
    if (on) SET_BIT(GPIOA->ODR, LED_ERR);
    else    CLEAR_BIT(GPIOA->ODR, LED_ERR);
}
```

### Part C — ISR 설계: 스니펫 5 (버튼 EXTI ISR)

```c
typedef struct btn_evt { uint32_t t_ms; struct btn_evt *next; } btn_evt_t;
static btn_evt_t *volatile g_btn_list;        /* ISR 가 push, main 이 pop */

void EXTI0_IRQHandler(void) {
    SET_BIT(EXTI->PR, EXTI_PR_PR0);                           /* 펜딩 클리어 */
    btn_evt_t *e = pvPortMalloc(sizeof *e);
    e->t_ms = g_ms;
    e->next = g_btn_list;
    g_btn_list = e;
    log_printf("btn @%lu ms\n", (unsigned long)e->t_ms);
}
int btn_poll(uint32_t *t_ms) {                /* main: 하나 꺼내면 1, 없으면 0 */
    btn_evt_t *e = g_btn_list;
    if (e == NULL) return 0;
    g_btn_list = e->next;
    *t_ms = e->t_ms;
    vPortFree(e);
    return 1;
}
```

Part C 는 "버그 몇 개 고치기" 가 아니라 **설계를 바꾸는 문제** 다: ISR 은 고정 크기 큐에 시각만 넣고, 출력은 main 이 한다. 고친 뒤에도 `btn_poll()` 의 인터페이스(꺼낸 시각 반환, 없으면 0, 로그 한 줄)는 그대로여야 한다.

## 2. Problem (English)

> **Firmware code review (15 minutes).** You are given five short snippets from an STM32-style code base (UART RX ring buffer, packet parser, DMA event flags, GPIO LEDs driven from main and a timer ISR, a button EXTI ISR). Registers are accessed with CMSIS-style `READ_REG/WRITE_REG/SET_BIT/CLEAR_BIT`; `SET_BIT` is a read-modify-write. `EXTI->PR` is write-1-to-clear, `TIMx->SR` is write-0-to-clear, `GPIOx->BSRR` sets the low 16 bits / resets the high 16 bits, `DMA->IFCR` clears the flags written as 1.
>
> 1. List every defect: line, category (missing `volatile`, non-atomic shared data, ISR doing blocking/allocating work, wrong RMW on a register, signed shift / sign extension, buffer overrun), impact, and fix. Also name the lines that look suspicious but are correct.
> 2. For each defect, point to (or write) a deterministic host test that fails on the original code: interrupt injection after the n-th bus access, red-zone bytes after buffers, `_Generic` type checks for qualifiers/signedness. For UB that a test cannot observe reliably, show an ASan/UBSan/TSan report.
> 3. Fix the snippets without changing their interfaces. `btn_poll()` must still return events oldest-first and log `"btn @<ms> ms\n"` — but from main, not from the ISR.
>
> Follow-ups: Why is `volatile` not enough for `take_events()`? When is a lock-free SPSC ring safe between an ISR and main, and what changes on a dual-core MCU? Why does `HAL_GPIO_TogglePin` write BSRR instead of ODR? How do you sleep (`WFI`) in the main loop without missing an event?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "16/35 passed" — FAIL 19줄이 버그 목록과 맞는지 먼저 대조하고, 스니펫을 고쳐 35/35 를 만든다
```

테스트가 쓰는 기법 세 가지 (Part B 에서 자세히):

| 기법 | 무엇을 잡나 | 예 |
|---|---|---|
| 접근 횟수 기반 IRQ 주입 (`bus_reset(isr, n)`) | 읽기~쓰기 사이에 끼어든 ISR (경쟁) | 스니펫 3·4 |
| red zone (배열 뒤 0xEE 4~32바이트) | 경계 넘침을 UB 없이 "가드 손상" 으로 | 스니펫 1·2 |
| `_Generic(&x, volatile uint32_t *: 1, default: 0)` | 한정자·부호 같은 **타입** 결함 | 스니펫 1·4 |

관찰용 데모 (하네스와 별개, 결과는 5. 풀이):

```bash
cc -std=c11 -O2 -S demo_volatile.c -o -                                                   # 대기 루프 asm (arm64)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_volatile.c -o -    # 같은 것, Cortex-M4
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overrun.c -o demo_overrun && ./demo_overrun 1
cc -std=c11 -g -O1 -fsanitize=undefined -fno-omit-frame-pointer demo_shift.c -o demo_shift && ./demo_shift
cc -std=c11 -O2 -pthread demo_race.c -o demo_race && ./demo_race
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DN=2000L demo_race.c -o demo_race_tsan && ./demo_race_tsan
```

## 4. 힌트

<details>
<summary>힌트 1 — 리뷰 순서: 공유 변수부터 표시한다</summary>

각 전역 변수 옆에 "누가 쓰나 / 누가 읽나" 를 적는다. ISR 과 main 이 둘 다 건드리는 것은 ① `volatile` 인가 ② 읽기~쓰기가 두 단계로 나뉘어 있는가 ③ 여러 변수의 순서가 중요한가를 본다. 레지스터는 데이터시트 표기(rc_w1, rc_w0, w)를 떠올린다 — `SET_BIT` 는 이름과 달리 **읽고 쓰는** 매크로다.

</details>

<details>
<summary>힌트 2 — 스니펫 1: 인덱스가 가질 수 있는 값을 전부 나열</summary>

`++rx_head > RX_SIZE` 일 때만 0 으로 돌아간다면 `rx_head` 는 0..8 — 9개 값이다. 배열 칸은 몇 개인가? 그리고 ISR 이 main 보다 빨라서 head 가 tail 을 따라잡으면 무엇이 일어나나? 가득 찼을 때도 DR 은 읽어야 하는 이유도 생각해 본다 (RXNE).

</details>

<details>
<summary>힌트 3 — 스니펫 2: <code>char</code> 하나가 0x84 이면?</summary>

이 맥(Apple arm64)에서 `char` 는 signed 다. `rx[3]` 이 0x84 면 값은 −124, 정수 승격 후 `|` 하면 상위 비트가 전부 1 이 된다. `rx[5] << 8` 은 음수를 shift — C11 6.5.7p4 로 UB. 캐스트를 shift **뒤** 에 붙이면 늦다. 그리고 `len` 은 무엇과 비교해야 하나 — 받은 길이만? 받는 버퍼의 크기는?

</details>

<details>
<summary>힌트 4 — 스니펫 3·4: "읽은 값을 쓴다" 사이에 ISR 이 오면</summary>

`take_events` 는 읽기와 0 쓰기 사이, `led_err` 는 ODR 읽기와 쓰기 사이가 틈이다. volatile 은 이 틈을 없애지 못한다. 고치는 방법은 둘: 틈을 인터럽트에 대해 막거나(임계 구역 — enable 이 아니라 **복원**), 하드웨어가 한 번의 쓰기로 처리하게 한다(BSRR). TIM3 ISR 의 ODR 토글도 더 급한 ISR 에게는 똑같은 틈이다.

</details>

<details>
<summary>힌트 5 — 스니펫 5: ISR 은 "기록만"</summary>

`EXTI->PR` 은 1 을 쓴 비트가 지워진다. 읽어서 OR 하면 다른 라인의 펜딩 1 까지 다시 쓰게 된다. `malloc` 과 UART `printf` 는 시간이 정해져 있지 않고, 내부에 락·공유 상태가 있다. 고정 크기 배열 + ISR 만 쓰는 쓰기 인덱스 + main 만 쓰는 읽기 인덱스(스니펫 1 을 고친 모양)면 충분하다.

</details>

## 5. 풀이

### 정답 키 — 버그 15개, 고치면 안 되는 줄 2개

심각도: **치명** = 데이터 손상·메모리 손상·정지, **높음** = 이벤트 유실·간헐 오동작, **중간** = 이식성/잠재 UB.

| # | 스니펫·줄 | 분류 | 무엇이 깨지나 | 재현 | 고침 |
|---|---|---|---|---|---|
| 1-1 | 1 `rx_head, rx_tail` | volatile 누락 | `uart_getc_wait()` 이 `-O2` 에서 무한 루프 `b .` 로 컴파일 | `_Generic` 검사 + demo_volatile asm | `volatile uint32_t` |
| 1-2 | 1 `rx_buf` | volatile 누락 (순서) | 데이터 쓰기가 head 공개 뒤로 밀려도 합법 → main 이 옛 바이트를 읽을 수 있음 | `_Generic` 검사 | `volatile uint8_t rx_buf[]` 또는 signal fence (C-A02) |
| 1-3 | 1 `> RX_SIZE` (ISR, getc 둘 다) | 버퍼 오버런 | `rx_buf[8]` 쓰기 — 배열 밖 1바이트 | red zone + ASan | `(h + 1u) % RX_SIZE` |
| 1-4 | 1 ISR | 버퍼 오버런 (논리) | 가득 차도 계속 써서 안 읽은 데이터를 덮고, head 가 tail 을 지나 링이 "비어" 보임 | `"ABCDEFGHIJ"` → `"J"` 만 나옴 | `next == tail` 이면 버리고 `rx_overflow++` (DR 은 읽은 뒤) |
| 2-1 | 2 `len` 줄 | 부호 확장 (char) | len 132 → 65412, 정상 프레임 거부 | len 0x0084 프레임 | `const uint8_t *u` 로 읽기 |
| 2-2 | 2 `ts` 줄 | 부호 있는 shift | `rx[5] << 8` 음수 shift = UB, 결과 0xFFFF9078 | ts 0xF0 프레임 + UBSan | `(uint32_t)u[k] << n` |
| 2-3 | 2 `memcpy` | 버퍼 오버런 | len ≤ 받은 길이만 확인 → 272 바이트를 240 칸에 | len 0x0110 프레임 + ASan | `len > PKT_MAX` 도 거부 |
| 3-1 | 3 `take_events` | 비원자적 공유 플래그 | 읽기와 0 쓰기 사이에 온 DMA 완료가 사라짐 | 접근 1 직후 IRQ 주입 + TSan | `irq_save/restore` 로 감싸기 (또는 `atomic_exchange`, 연습 2) |
| 3-✓ | 3 `WRITE_REG(DMA1->IFCR, isr)` | **맞음** | 본 플래그만 지운다 (IFCR 은 쓰기 전용 클리어 레지스터) | 기본 테스트 PASS | 그대로 |
| 4-1 | 4 `led_err` | 잘못된 RMW | main 의 ODR RMW 사이에 TIM3 토글이 오면 토글이 되돌려짐 | 접근 1 직후 IRQ 주입 | `BSRR = LED` / `BSRR = LED << 16` |
| 4-2 | 4 TIM3 ODR 토글 | 잘못된 RMW | 더 급한 ISR(고장 처리)이 켠 LED_ERR 을 TIM3 가 덮어씀 | 중첩 ISR 주입 | ODR 읽고 BSRR 로 자기 핀만 |
| 4-3 | 4 `PIN(n)` | 부호 있는 shift | `PIN(15) << 16` (BSRR 리셋 워드) = int 오버플로 UB | `_Generic` 검사 + UBSan | `1u << (n)` |
| 4-✓ | 4 `WRITE_REG(TIM3->SR, ~TIM_SR_UIF)` | **맞음** | rc_w0: 0 쓴 비트만 지워짐, 다른 플래그 보존 | 기본 테스트 PASS | 그대로 (`CLEAR_BIT` 로 "고치면" 버그) |
| 5-1 | 5 `SET_BIT(EXTI->PR, …)` | 잘못된 RMW (W1C) | 같이 펜딩된 line 3 까지 지워짐 → 그 인터럽트 영영 안 옴 | PR=0x9 → 0x0 | `WRITE_REG(EXTI->PR, PR0)` |
| 5-2 | 5 `pvPortMalloc` | ISR 내 malloc | 비결정 시간, 힙 락(FreeRTOS: 스케줄러 정지), NULL 미검사 | `g_unsafe_in_isr` | 고정 크기 큐 |
| 5-3 | 5 `log_printf` | ISR 내 printf | 블로킹 UART 송신, 재진입 불가, 스택 | `g_unsafe_in_isr` | 출력은 main 에서 |
| 5-4 | 5 리스트 | 순서·경쟁 | LIFO 라 순서가 뒤집힘. main 의 pop(읽기→쓰기) 사이에 push 가 오면 새 이벤트 유실·누수 | FIFO 테스트 (pop 경쟁은 리뷰로만) | SPSC 큐 |

테스트 결과: starter **16/35**, solution **35/35**. starter 가 통과하는 16개는 기본 동작과 decoy 두 줄 확인이다 — 고치면서 이걸 깨면 "고친 게 아니라 망가뜨린 것" 이다.

### 15분 안에 보는 순서

| 순서 | 볼 것 | 이 레슨에서 걸리는 것 |
|---|---|---|
| ① 공유 데이터 | 전역마다 writer/reader 표시. volatile? 다단계 RMW? | 1-1, 1-2, 3-1, 5-4 |
| ② ISR 몸통 | 블로킹(UART, 폴링), 할당, 출력, 긴 루프, 플래그 클리어 위치 | 5-2, 5-3 |
| ③ 레지스터 쓰기 | 비트 의미(rc_w1 / rc_w0 / w / BSRR)와 `|=` `&=` | 4-1, 4-2, 5-1, decoy 2개 |
| ④ 정수 | `char`·`int8_t` 바이트, shift 의 왼쪽 피연산자 타입, 캐스트 위치 | 2-1, 2-2, 4-3 |
| ⑤ 버퍼 | 인덱스 범위, 가득 참, 길이 필드 검증 (외부 입력은 적대적이다) | 1-3, 1-4, 2-3 |

면접에서 말하는 형식: **"N번째 줄, (분류). (조건)이면 (증상). (고침). 테스트는 (방법)."** 예: "`take_events` 의 읽기와 0 쓰기 사이에 DMA ISR 이 오면 완료 이벤트가 사라집니다. 짧은 PRIMASK 저장/복원 구역으로 묶거나 `atomic_exchange` 를 씁니다. 테스트는 첫 번째 읽기 직후에 ISR 을 주입해서 이벤트 수를 봅니다."

### Part A — 스니펫 1: 링 버퍼

**1-1 volatile 누락 → 대기 루프가 진짜 무한 루프가 된다.** `uart_getc_wait()` 에 `uart_getc()` 가 인라인되면 루프 몸통에는 `rx_head`, `rx_tail` 읽기만 남는다. 루프 안에서 둘을 바꾸는 코드가 없으니 컴파일러는 한 번만 읽는다 (C-B09). `demo_volatile.c` 의 실제 출력 (Apple clang 21, `-O2`):

```text
_getc_wait_plain:                          getc_wait_plain:            @ Cortex-M4
	ldr	w10, [x8]        ; rx_head 1번         ldr	r0, [r0]           @ rx_head 1번
	ldr	w9, [x8]         ; rx_tail 1번         ldr	r2, [r1]           @ rx_tail 1번
	cmp	w10, w9                                cmp	r0, r2
	b.ne	LBB0_2                                 bne	.LBB0_2
LBB0_1:                                       .LBB0_1:
	b	LBB0_1           ; 영원히                b	.LBB0_1            @ 영원히
```

`volatile` 판(`getc_wait_volatile`)은 arm64 에서 `LBB1_1: ldr w10,[x9]; ldr w11,[x8]; cmp; b.eq LBB1_1` — 매 바퀴 다시 읽는다. Cortex-M4 판은 같은 비교를 4번 펼쳐(unroll) 놓았지만 각 바퀴마다 `ldr` 두 개가 그대로 있다. 덧붙여 C11 6.8.5p6 은 "제어식이 상수가 아니고 몸통에 I/O·volatile·원자 연산이 없는 반복문은 끝난다고 가정해도 된다" 고 허용하므로, 컴파일러가 이 루프를 아예 지워도 표준 위반이 아니다. clang 21 은 `b .` 로 남겼다 — 어느 쪽이든 고장이다.

테스트에서는 "멈춤" 을 기다릴 수 없으니(starter 가 멈추면 안 된다) **타입을 검사** 한다: `_Generic(&rx_head, volatile uint32_t *: 1, default: 0)`. `&x` 는 lvalue 가 아니라 포인터 값이므로 가리키는 타입의 `volatile` 이 그대로 남아 구분된다.

**1-2 버퍼도 volatile (또는 배리어).** C11 5.1.2.3p6 이 순서를 보장하는 것은 **volatile 접근끼리** 뿐이다. ISR 의 `rx_buf[h] = c;`(일반 쓰기)는 `rx_head = next;`(volatile 쓰기) 뒤로 옮겨져도 합법이고, main 쪽 `rx_buf[t]` 읽기도 head 검사 앞으로 당겨질 수 있다. 실제 컴파일러가 이렇게 하는 경우는 드물지만 "드물다" 는 리뷰 답이 아니다. 단일 코어에서는 버퍼를 `volatile` 로 두는 게 가장 단순하고, 대안은 `atomic_signal_fence(memory_order_release/acquire)` 같은 컴파일러 배리어다 (C-A02). **멀티코어나 DMA** 면 CPU 재배치·캐시까지 따져야 한다 (C-A03, Q5).

**1-3 off-by-one.** `++rx_head > RX_SIZE` 는 head 가 8 일 때 통과시켜 `rx_buf[8]` 에 쓴다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">RX_SIZE = 8: 원본은 인덱스 0..8 (9개 값), 고친 판은 0..7 + "한 칸 비워 두기"</text>
  <g stroke="currentColor">
    <rect x="20" y="44" width="60" height="36" class="f-ok"/><rect x="80" y="44" width="60" height="36" class="f-ok"/>
    <rect x="140" y="44" width="60" height="36" class="f-ok"/><rect x="200" y="44" width="60" height="36" class="f-ok"/>
    <rect x="260" y="44" width="60" height="36" class="f-ok"/><rect x="320" y="44" width="60" height="36" class="f-ok"/>
    <rect x="380" y="44" width="60" height="36" class="f-ok"/><rect x="440" y="44" width="60" height="36" class="f-ok"/>
    <rect x="500" y="44" width="60" height="36" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="42" y="67">0</text><text x="102" y="67">1</text><text x="162" y="67">2</text><text x="222" y="67">3</text>
    <text x="282" y="67">4</text><text x="342" y="67">5</text><text x="402" y="67">6</text><text x="462" y="67">7</text>
    <text x="512" y="67">[8]!</text>
  </g>
  <text x="575" y="60" fill="currentColor">배열 밖: 다른 전역,</text>
  <text x="575" y="78" fill="currentColor">스택, 또는 red zone</text>
  <g fill="currentColor">
    <text x="20" y="112" font-family="monospace">원본: rx_buf[head] = c; if (++head &gt; 8) head = 0;   → head 가 8 인 채로 다음 바이트를 받는다</text>
    <text x="20" y="136" font-family="monospace">고침: next = (head + 1) % 8; if (next == tail) { 버리고 센다 } 아니면 쓰고 head = next</text>
  </g>
  <rect x="20" y="156" width="700" height="80" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="32" y="178">비었음: head == tail.  가득: (head + 1) % N == tail  → 실제 용량은 N − 1 (7바이트).</text>
    <text x="32" y="200">한 칸을 비워 두는 이유: 두 인덱스만으로 "비었음" 과 "가득" 을 구분하려고. 공유 카운터가 없으니</text>
    <text x="32" y="222">head 는 ISR 만, tail 은 main 만 쓴다 → 읽기-수정-쓰기 경쟁이 원천적으로 없다 (단일 writer).</text>
  </g>
</svg>
```

호스트에서 `rx_buf[8]` 에 실제로 쓰면 UB 라 테스트가 믿을 수 없다. 그래서 starter 는 `rx_buf[RX_SIZE + RX_GUARD]` 로 4바이트를 더 잡고 0xEE 로 채운 뒤, 넘침을 **가드 손상** 으로 본다 (객체 안이므로 정의된 동작). 진짜 크기 배열에서 무슨 일이 나는지는 `demo_overrun.c` — ASan 없이 `-O2` 로 돌리면 아무 일도 없던 것처럼 끝난다:

```text
$ ./ov_plain 1
snippet1: rx_head=0 rx_buf[0]=A neighbour=0x11111111
```

ASan(`-O1`)은 첫 넘침에서 멈춘다 (실제 출력, 주소 줄 일부):

```text
==87756==ERROR: AddressSanitizer: global-buffer-overflow on address 0x000104c80228 ...
WRITE of size 1 at 0x000104c80228 thread T0
    #0 0x000104c78a1c in main demo_overrun.c:36
0x000104c80228 is located 0 bytes after global variable 'rx_buf' defined in 'demo_overrun.c' (0x000104c80220) of size 8
```

함정 하나: 처음 데모에서는 `rx_buf` 를 아무도 읽지 않았더니 `-O1` 이 쓰기 자체를 지워서(dead store) ASan 이 **아무것도 보고하지 않았다**. `-O0` 에서는 `uart_isr_buggy demo_overrun.c:19` 로 잡혔다. 새니타이저는 "실제로 실행된 접근" 만 본다 — 결과를 읽는 테스트를 쓰자.

**1-4 가득 참.** 원본은 10바이트를 받으면 0..8 칸을 다 쓰고 0 번 칸에 'J' 를 덮어써 head=1, tail=0 이 된다. main 은 `"J"` 한 글자만 읽고 나머지 8바이트는 사라진다 (테스트 FAIL 줄 `rx_drain(...) == RX_SIZE - 1u`). 고친 판은 `"ABCDEFG"` 7바이트를 지키고 `rx_overflow == 3`. **가득 차도 `DR` 은 읽는다** — STM32 USART 는 DR 을 읽어야 RXNE 가 내려가서, 안 읽고 `return` 하면 ISR 이 끝나자마자 다시 들어오는 인터럽트 폭주(interrupt storm)가 된다. "새 바이트를 버릴까, 옛 바이트를 버릴까" 는 정책이다: 로그 스트림이면 옛 것을, 명령 프로토콜이면 새 것을 버리고 오류를 알린다. 어느 쪽이든 **세어서 보이게** 하는 것이 핵심이다.

### Part A — 스니펫 2: 패킷 파서

**2-1 `char` 의 부호.** C11 6.2.5p15: `char` 가 `signed char` 와 같을지 `unsigned char` 와 같을지는 구현 정의다. 이 맥의 `cc -dM -E` 에는 `__CHAR_UNSIGNED__` 가 없고(=signed, `CHAR_MIN=-128` 실측), Cortex-M 타깃과 aarch64 Linux 에는 있다:

```text
$ clang --target=thumbv7em-none-eabi -dM -E -x c /dev/null | grep CHAR_UNSIGNED
#define __CHAR_UNSIGNED__ 1
$ clang --target=aarch64-linux-gnu -dM -E -x c /dev/null | grep CHAR_UNSIGNED
#define __CHAR_UNSIGNED__ 1
```

즉 이 버그는 **보드에서는 숨고, 호스트 단위 테스트(맥·x86)에서만 터진다**. 반대로 타깃 코드를 다른 MCU 로 옮기면 터질 수도 있다. 계산: `rx[2]=0x00, rx[3]=0x84` → `rx[3]` = −124 → `0 | −124` = −124 → `(uint16_t)` 변환 = 65412 (부호 없는 타입으로의 변환은 모듈로 2^16, 6.3.1.3p2 — 정의됨). 65412 > n − 8 이라 **정상 프레임이 거부** 된다.

**2-2 부호 있는 shift.** C11 6.5.7p4: `E1 << E2` 에서 E1 이 부호 있는 타입이면 E1 이 음이 아니고 결과가 표현 가능할 때만 정의되고, 그 밖은 **UB** 다. `rx[5] << 8` 에서 `rx[5]` 가 음수면 UB, `u[7] << 24` 에서 `u[7] >= 0x80` 이면 (uint8_t 도 int 로 승격되므로) 역시 UB. `demo_shift.c` 실제 UBSan 출력:

```text
demo_shift.c:13:38: runtime error: left shift of negative value -112
demo_shift.c:13:69: runtime error: left shift of negative value -126
demo_shift.c:16:65: runtime error: left shift of 130 by 24 places cannot be represented in type 'int'
demo_shift.c:29:73: runtime error: left shift of 32768 by 16 places cannot be represented in type 'int'
char is signed (CHAR_MIN=-128)
ts_char   = 0xFFFF9078
ts_u8_int = 0x82349078
ts_ok     = 0x82349078
BSRR reset word for pin 15 = 0x80000000
```

읽는 법: 입력 ts 바이트는 `78 90 34 82` (기대값 0x82349078). `char` 판은 `rx[5]=0x90` 이 −112 가 되어 상위 16비트가 1 로 채워진 **0xFFFF9078** — 테스트로 결정적으로 보이는 오답이다. `uint8_t` 로만 바꾼 판(`ts_u8_int`)은 **답이 맞게 나오지만** 16번째 줄에서 UB 다 (`130 << 24`). 오늘의 clang 이 기대대로 계산해 줬을 뿐이다. 그래서 고침은 "바이트를 unsigned 로" + "shift 하기 **전에** `uint32_t` 로 캐스트" 둘 다다. `(uint32_t)(a << 24)` 처럼 shift **뒤** 의 캐스트는 이미 일어난 UB 를 되돌리지 못한다.

`>>` 쪽도 기억: 음수의 오른쪽 shift 는 UB 가 아니라 **구현 정의** (6.5.7p5). 부호 확장이 필요하면 연습 3 처럼 구현 정의 동작 없이 쓴다.

**2-3 길이 검증.** 원본은 `len ≤ n − 8`(받은 만큼은 있나)만 보고 `len ≤ PKT_MAX`(받을 자리가 있나)를 안 본다. 공격자·노이즈가 len=272 를 보내면 240칸 payload 뒤 32바이트를 덮는다. starter 테스트는 payload 뒤 32바이트 red zone 이 손상되는 것으로 보고, `demo_overrun.c 2` 는 진짜 크기 구조체에서:

```text
$ ./ov_plain 2; echo $?            # ASan 없이 -O2
133
==85911==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x611000000138 ...
WRITE of size 272 at 0x611000000138 thread T0
    #0 0x0001036aabb0 in __asan_memcpy+0x330 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x3ebb0)
    #1 0x000102fdc9c0 in main demo_overrun.c:44
0x611000000138 is located 0 bytes after 248-byte region [0x611000000040,0x611000000138)
```

ASan 없이도 프로세스가 exit 133(=128+SIGTRAP)으로 죽은 이유: asm 을 보면 `bl ___memcpy_chk` — 맥 SDK 가 최적화 빌드에서 켜는 `_FORTIFY_SOURCE` 가 목적지 크기(`__builtin_object_size`)를 알아서 런타임에 막았다. **MCU 의 newlib 빌드에는 보통 이런 안전망이 없다** — 그대로 옆 메모리를 덮는다. 검사 순서도 중요하다: `n < 8u` 를 먼저 봐야 `n - 8u` 가 size_t 언더플로로 거대한 값이 되지 않는다 (원본도 이 순서는 맞다).

### Part B — 스니펫 3: 비원자적 공유 플래그

`take_events()` 는 "읽기 → 0 쓰기" 두 단계다. `g_events` 가 volatile 이라 두 접근 모두 실제로 일어나지만, **그 사이** 는 아무것도 막지 않는다. C-M14 의 핵심 문장 그대로: 원자적 부품(32비트 읽기, 32비트 쓰기)을 이어 붙여도 원자적 전체가 되지 않는다.

테스트가 경쟁을 결정적으로 만드는 법 — `bus_reset(DMA1_IRQHandler, 1)`: main 의 **첫 번째 버스 접근 직후** ISR 실행.

| 버스 접근 | 원본 | 고친 판 (`irq_save` 안) |
|---|---|---|
| #1 main: `ev = g_events` (0) | 직후 ISR 실행 → `g_events = EV_ADC_DONE` | 직후 IRQ → **펜딩** |
| #2… main: `g_events = 0` | ISR 이 세운 비트를 덮어씀 | 0 씀 |
| `irq_restore(pm)` | — | 펜딩 ISR 실행 → `g_events = EV_ADC_DONE` |
| 다음 `take_events()` | 0 — **이벤트 유실** | EV_ADC_DONE |

테스트는 `e1 | e2 == EV_ADC_DONE` 로 본다. 그래서 `atomic_exchange` 로 고쳐도(그 경우 ISR 이 RMW 앞뒤 어디에 오든 하나의 take 가 가져간다) 통과한다. 추가 검사 두 개: 끝난 뒤 `g_primask == 0` (복원했나), 그리고 **이미 막힌 상태에서 불렀을 때 나올 때도 막혀 있어야** 한다 — `__disable_irq(); … __enable_irq();` 로 고치면 이 테스트가 잡는다 (C-M14 Part B).

진짜 동시 실행에서는? `demo_race.c` — pthread 하나가 ISR 역할로 비트가 내려가 있을 때마다 다시 세우고(매 set 이 별개 사건), main 은 `take()` 로 센다. `-O2` 실제 3회:

```text
events sent=200000  seen by main=16  lost=199984
events sent=200000  seen by main=6  lost=199994
events sent=200000  seen by main=4  lost=199996
```

거의 전부 사라졌다. 이 데모의 main 루프는 `take()` 만 반복하므로 "읽기~0 쓰기" 창이 루프 시간 대부분이고, main 의 0 쓰기가 계속 나가서 다른 코어의 1 을 덮는다. MCU 에서는 창이 명령어 몇 개라 **며칠에 한 번** 이 된다 — 그래서 더 위험하다. 원자 연산 판(`-DFIXED`: ISR 쪽 `atomic_fetch_or`, main 쪽 `atomic_exchange`)은 3회 모두 `seen by main=200000  lost=0`. TSan(`-DN=2000L`, 느려서 횟수를 줄임) 실제 보고:

```text
WARNING: ThreadSanitizer: data race (pid=87377)
  Write of size 4 at 0x000102834004 by main thread:
    #0 main demo_race.c:55 (race_tsan:arm64+0x1000006b4)
  Previous write of size 4 at 0x000102834004 by thread T1:
    #0 isr_thread demo_race.c:43 (race_tsan:arm64+0x100000758)
  Location is global 'g_events' at 0x000102834004 (race_tsan+0x100008004)
SUMMARY: ThreadSanitizer: data race demo_race.c:55 in main
events sent=2000  seen by main=601  lost=1399
ThreadSanitizer: reported 1 warnings
```

(이 맥에서 TSan 경고가 있는 실행의 종료 코드는 134 였다. `-DFIXED` TSan 빌드는 보고 없이 `lost=0`, 종료 코드 0.) 주의: TSan 은 **스레드** 사이의 경쟁을 보는 도구다. 실제 ISR↔main 경쟁은 TSan 이 볼 수 없으니, 펌웨어에서는 주입 테스트(이 레슨의 bus 모델)와 리뷰가 주 무기다.

**decoy: `WRITE_REG(DMA1->IFCR, isr)`.** STM32F1/F3/L4 계열 DMA 는 상태(`ISR`, 읽기 전용)와 클리어(`IFCR`, 1 쓰면 해당 플래그 클리어)가 **다른 레지스터** 다. 본 플래그 그대로 쓰는 것은 "본 것만 ack" 하는 정석이다 — 읽은 뒤에 새로 생긴 플래그는 쓴 값에 없으므로 안 지워진다.

### Part B — 스니펫 4: GPIO 의 RMW 두 곳과 부호 있는 PIN

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">led_err(1) 의 ODR RMW 사이에 TIM3 ISR (테스트: 접근 #1 직후 주입)</text>
  <text x="10" y="62" fill="currentColor">원본</text>
  <rect x="80" y="44" width="150" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="90" y="63" fill="currentColor" font-family="monospace">rd ODR → 0x0000</text>
  <rect x="240" y="44" width="200" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="250" y="63" fill="currentColor" font-family="monospace">ISR: ODR ^= HB → 0x0020</text>
  <rect x="450" y="44" width="270" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="460" y="63" fill="currentColor" font-family="monospace">wr ODR = 0x0000|0x8000 = 0x8000</text>
  <text x="80" y="94" fill="currentColor">→ 결과 0x8000: 하트비트 토글이 main 의 "옛 값 쓰기" 로 되돌려졌다 (LED 가 멈춘 듯 보임)</text>
  <text x="10" y="142" fill="currentColor">BSRR</text>
  <rect x="80" y="124" width="250" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="90" y="143" fill="currentColor" font-family="monospace">wr BSRR = 0x00008000 (set 15)</text>
  <rect x="340" y="124" width="380" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="350" y="143" fill="currentColor" font-family="monospace">ISR: odr=0x8000 → BSRR = 0x00000020 (set 5)</text>
  <text x="80" y="174" fill="currentColor">→ 결과 0x8020: 쓰기 1번이라 틈이 없고, 각자 자기 핀만 건드린다</text>
  <rect x="10" y="192" width="720" height="68" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="214">BSRR: 하위 16비트 1 = 그 핀 set, 상위 16비트 1 = 그 핀 reset, 0 = 변화 없음, 둘 다 1 이면 set 우선.</text>
    <text x="22" y="236" font-family="monospace">reset 워드 = PIN(15) &lt;&lt; 16 = 0x80000000 → PIN 이 int 면 1&lt;&lt;31 을 넘는 UB (4-3)</text>
    <text x="22" y="254">RMW 토글은 ISR 안에서도 "더 급한 ISR" 에게는 같은 틈이다 (4-2).</text>
  </g>
</svg>
```

**4-1** 은 C-M02 Part C 의 그 버그다. main 은 ISR 에게 선점당하므로 main 쪽 RMW 는 언제든 끊길 수 있다. 하드웨어가 **원자적 set/reset 레지스터** 를 주면 그것이 정답이다 (STM32 BSRR, nRF `OUTSET/OUTCLR`, RP2040 SIO `GPIO_OUT_SET/CLR`, Cortex-M3/M4 bit-band). 없으면 짧은 임계 구역.

**4-2** 는 한 단계 더 깊은 꼬리질문이다. "ISR 안의 RMW 는 main 이 못 끊으니 안전하다" 는 **우선순위가 하나일 때만** 참이다. 테스트는 TIM3 ISR 가 ODR 을 읽은 직후(접근 #2) 더 급한 `fault_isr` 가 선점해 `led_err(1)` 을 하도록 만든다. 원본 TIM3 는 읽어 둔 옛 ODR(0)에 HB 를 XOR 해 써서 방금 켠 에러 LED 를 끈다 — **고장 표시가 사라지는** 버그다. 고친 판은 ODR 을 읽어 자기 핀의 현재 상태만 보고 BSRR 로 "HB 만 set 또는 reset" 을 쓴다:

```c
uint32_t odr = READ_REG(GPIOA->ODR);
WRITE_REG(GPIOA->BSRR, ((odr & HB_PIN) << 16) | (~odr & HB_PIN));   /* 켜져 있으면 reset, 꺼져 있으면 set */
```

최근 STM32Cube HAL 의 `HAL_GPIO_TogglePin` 도 이 형태(ODR 읽고 BSRR 쓰기)다. 읽기와 쓰기 사이에 누가 HB 핀을 바꾸면 토글 판단이 낡을 수 있지만, **다른 핀은 절대 건드리지 않는다**.

**4-3** `#define PIN(n) (1 << (n))` 은 핀 15 까지는 무해해 보이지만, BSRR 로 고치는 순간 `LED_ERR << 16` = `(1 << 15) << 16` 이 int 범위를 넘는다 (UBSan: `left shift of 32768 by 16 places cannot be represented in type 'int'`). 원본 버그를 고치다가 새 UB 를 만드는 전형적인 경로라, 리뷰에서 함께 지적한다. 테스트는 `_Generic(PIN(0), unsigned int: 1, default: 0)` 로 매크로 결과 타입을 본다. 같은 이유로 `BIT(31)`, `1 << irqn`(irqn ≤ 31)도 모두 `1u`.

**decoy: `WRITE_REG(TIM3->SR, ~TIM_SR_UIF)`.** STM32 TIMx_SR 의 플래그는 rc_w0 — 0 을 쓴 비트만 지워지고 1 을 쓰면 변화 없음. 그러니 `~UIF` 를 쓰면 UIF 만 지워지고 CC1IF 등은 보존된다 (ST HAL 의 `__HAL_TIM_CLEAR_FLAG` 도 `SR = ~(flag)`). 이것을 "더 안전해 보이는" `CLEAR_BIT(TIM3->SR, UIF)` 로 바꾸면 읽기~쓰기 사이에 하드웨어가 세운 CC1IF 를 0 으로 써서 지운다 (C-M02). 테스트: `SR = UIF|CC1IF` → ISR 후 `SR == CC1IF` — starter·solution 모두 PASS 해야 한다.

### Part C — 스니펫 5: ISR 은 기록만, 일은 main 이

**5-1 W1C 에 `SET_BIT`.** `EXTI->PR` 은 rc_w1. `SET_BIT` 는 `PR = PR | PR0` — PR 이 `0x9`(line 0, 3 펜딩)면 `0x9` 를 써서 **둘 다** 지운다. line 3 의 인터럽트는 NVIC 에 도달하기 전에 사라진다 (EXTI3 핸들러가 영영 안 불림). 고침은 `WRITE_REG(EXTI->PR, EXTI_PR_PR0)` — 지울 비트만 쓴다. 테스트: PR=0x9 → ISR 후 0x8 이어야 한다.

**5-2 ISR 안 `pvPortMalloc`.** 문제는 넷: ① 실행 시간이 힙 상태에 따라 달라짐(인터럽트 지연이 비결정), ② 내부 공유 상태 — FreeRTOS heap_4 의 `pvPortMalloc` 은 `vTaskSuspendAll()` 로 보호하므로 ISR 에서 부를 수 없는 API 이고, newlib `malloc` 은 `__malloc_lock` 재진입 지원 없이 빌드되면 main 이 malloc 중일 때 ISR 이 들어오면 자유 리스트가 깨진다, ③ 단편화로 결국 NULL — 원본은 검사도 안 하고 `e->t_ms` 에 쓴다, ④ 안전 규격 관점에서 런타임 동적 할당 자체를 금지 (MISRA-style 21.3, C-A10).

**5-3 ISR 안 `printf`.** UART 로 retarget 된 printf 는 보통 **폴링 송신**: 115200 bps 에서 13바이트 `"btn @1234 ms\n"` 는 약 1.1 ms (10비트/바이트 기준) 동안 ISR 을 붙잡는다 — 1 kHz 타이머 tick 하나를 통째로 놓친다. 게다가 printf 는 재진입 불가(공유 버퍼·락)이고 스택을 수백 바이트 이상 쓴다. POSIX 의 async-signal-safe 목록에도 `printf`·`malloc` 은 없다 (C-M14 Q6).

**5-4 LIFO + pop 경쟁.** 리스트 앞에 push 하고 앞에서 pop 하니 이벤트가 **최신 것부터** 나온다 (테스트: 100, 200 을 눌렀는데 200 이 먼저). 그리고 `btn_poll` 의 `e = g_btn_list; … g_btn_list = e->next;` 사이에 ISR 이 push 하면 새 노드가 리스트에서 떨어져 나간다 — 이벤트 유실 + 메모리 누수. 이 경쟁은 리스트 접근이 bus 모델을 안 거쳐서 테스트로는 주입하지 않았다 (리뷰로 지적할 항목).

**고친 설계 — 스니펫 1 과 같은 SPSC 큐:**

```c
#define BTN_Q 4u
static volatile uint32_t g_btn_q[BTN_Q];
static volatile uint32_t g_btn_wr, g_btn_rd, g_btn_drop;   /* wr: ISR 만, rd: main 만 씀 */

void EXTI0_IRQHandler(void) {
    WRITE_REG(EXTI->PR, EXTI_PR_PR0);         /* rc_w1: line 0 만 */
    uint32_t w = g_btn_wr, next = (w + 1u) % BTN_Q;
    if (next == g_btn_rd) { g_btn_drop = g_btn_drop + 1u; return; }
    g_btn_q[w] = g_ms;
    g_btn_wr = next;
}
```

ISR 은 상수 시간·락 없음·할당 없음. `btn_poll()` 이 꺼내면서 `log_printf` 를 부른다 — 같은 로그가 **main 컨텍스트** 에서 나가므로 `g_unsafe_in_isr == 0`. 큐가 가득 차면 버리고 센다(채터링이 심한 버튼이면 debounce 는 main 쪽 타임스탬프 비교로). FreeRTOS 를 쓴다면 같은 역할을 `xQueueSendFromISR()` + `portYIELD_FROM_ISR()` 가 한다 — "FromISR" 가 붙은 API 만 ISR 에서 부른다.

### 테스트를 직접 설계할 때의 원칙

| 버그 종류 | 결정적 테스트 | 테스트로 안 될 때 |
|---|---|---|
| 경쟁 (RMW 사이 ISR) | 접근 횟수 n 직후 주입, 모든 n 을 훑기 | TSan (스레드 모델), 리뷰 |
| 경계 넘침 | red zone 바이트, 경계 ±1 입력, 가득 참 입력 | ASan (진짜 크기 배열) |
| UB (shift, 오버플로) | 오답이 나는 입력이 있으면 그 입력 / 타입 검사 | UBSan, `-fsanitize=shift` |
| 한정자 누락 | `_Generic` 로 타입 검사 | `-O2` asm |
| ISR 금지 함수 | mock 에 "ISR 컨텍스트" 카운터 | 링커 맵에서 ISR 호출 그래프 확인 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 35/35 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 35/35, 보고 없음
```

solution 은 스레드를 쓰지 않으므로 TSan 대상이 아니다 (TSan 은 `demo_race.c` 에서).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `take_events` 에 `volatile` 만 더 붙이고 끝 | volatile 은 접근을 지우지 못하게 할 뿐, 두 접근 사이를 묶지 않는다 | `irq_save/restore` 또는 `atomic_exchange` |
| 임계 구역을 `__disable_irq(); … __enable_irq();` 로 | 호출자가 이미 막아 둔 상태를 풀어 버림 (테스트 "막힌 상태에서 불러도") | `pm = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm);` |
| `TIM3->SR = ~UIF` 를 `CLEAR_BIT` 로 "고침" | rc_w0 에 RMW: 읽은 뒤 생긴 플래그를 0 으로 써서 지움 | 원래 줄 그대로 (decoy) |
| `EXTI->PR &= ~PR0` 로 고침 | rc_w1 에 0 이 아닌 다른 펜딩 1 들이 그대로 써져 다 지워짐. 0 쓴 PR0 은 안 지워짐 | `EXTI->PR = PR0` |
| `(uint32_t)(rx[7] << 24)` | shift 가 int 에서 먼저 일어나 이미 UB. 캐스트가 늦음 | `(uint32_t)u[7] << 24` |
| 명시적 캐스트로 경고를 "정리" | `-Wsign-conversion` 이 스니펫 2 에서 조용한 이유가 바로 `(uint16_t)(...)`, `(uint32_t)(...)` 캐스트. 실제로 이 경고 옵션은 starter 에서 `CLEAR_BIT(…, LED_ERR)` 한 곳만 지적했다 | 타입을 바꿔서 경고를 없앤다 (`uint8_t` 바이트, `1u`) |
| 가득 찬 링에서 DR 을 안 읽고 return | RXNE 가 안 내려가 ISR 무한 재진입 (interrupt storm) | DR 먼저 읽고, 버릴지 결정 |
| `if (p->len > n - 8)` 를 `n < 8` 검사 앞에 | `n - 8u` 가 size_t 로 언더플로 → 검사가 항상 통과 | 짧은 길이부터 거른다 |
| ISR 의 `malloc` 을 "정적 풀 할당자" 로 바꾸고 락을 `__disable_irq` 로 | 동작은 하지만 ISR 이 할당/해제 정책을 갖게 됨. 고정 크기 큐면 할당 자체가 필요 없다 | SPSC 큐 |
| 링 버퍼 크기를 `% RX_SIZE` 로 쓰면서 RX_SIZE 를 2의 거듭제곱이 아닌 값으로 바꿈 | 동작은 맞지만 `%` 가 나눗셈 명령(또는 M0 에선 라이브러리 호출)이 됨 | 2^k 크기 + `& (N-1)`, `_Static_assert` 로 강제 (C-A08) |
| `while (!(SPI->SR & TXE));` 에 타임아웃 없음 | 클럭·전원 문제로 플래그가 안 서면 펌웨어 전체 정지 | 연습 4 |

## 8. 면접 Q&A

<details>
<summary>Q1. 처음 보는 ISR 코드를 리뷰할 때 무엇부터 봅니까?</summary>

① **공유 데이터**: 전역마다 누가 쓰고 누가 읽는지 표시한다 — ISR↔main 공유면 volatile 인지, 다단계 RMW 인지, 여러 변수 사이 순서가 필요한지. ② **ISR 몸통**: 블로킹(폴링 루프, UART 출력), 할당, 긴 계산, 플래그 클리어가 맞는 의미(rc_w1/rc_w0)로 되는지. ③ **레지스터 쓰기**: `|=`·`&=` 가 보이면 그 레지스터의 비트 의미와 다른 컨텍스트의 접근을 확인. ④ **정수**: 바이트 조립, shift 의 왼쪽 피연산자 타입, `char`. ⑤ **버퍼**: 외부 입력 길이 검증, 인덱스 범위, 가득 참 정책. 찾은 것은 영향 순(메모리 손상 > 이벤트 유실 > 이식성)으로 말한다. 꼬리: "시간이 없으면?" → ①②③ 이 펌웨어 특유의, 테스트로 잘 안 잡히는 버그라 우선.

</details>

<details>
<summary>Q2. take_events 의 g_events 는 이미 volatile 인데 왜 버그죠?</summary>

volatile 은 "이 접근을 지우거나 합치지 말고, volatile 접근끼리 순서를 지켜라" (5.1.2.3p6, 6.7.3p7) 만 보장한다. `ev = g_events; g_events = 0;` 은 접근 두 개이고, 그 사이에 ISR 이 비트를 세우면 0 쓰기가 덮는다 — lost update. 원자성이 필요하다: 단일 코어 MCU 면 짧은 PRIMASK 저장/복원 구역(ISR 지연 수 사이클), Cortex-M3 이상이면 `atomic_exchange` (LDREX/STREX 루프, 인터럽트 지연 0), M0/M0+ 는 원자 RMW 가 라이브러리 호출이 되고 내부적으로 인터럽트를 막는다 (C-M14). 꼬리: "ISR 쪽 `SET_BIT(g_events, …)` 는?" → main 은 ISR 을 선점할 수 없으니 단일 우선순위에선 안전. 같은 변수를 쓰는 ISR 이 둘이고 우선순위가 다르면 그것도 경쟁이다.

</details>

<details>
<summary>Q3. EXTI->PR |= bit 는 왜 틀렸고, TIM->SR = ~UIF 는 왜 맞습니까?</summary>

EXTI_PR 은 rc_w1 — 1 을 쓰면 지워진다. `|=` 는 현재 펜딩된 **모든** 1 을 다시 쓰므로 다른 라인의 펜딩까지 지운다. 정답 `PR = bit`. TIMx_SR 은 rc_w0 — 0 을 쓰면 지워지고 1 은 무시. `SR = ~UIF` 는 UIF 자리에만 0 이라 UIF 만 지운다. 반대로 `SR &= ~UIF` 는 RMW 라 읽기 이후 하드웨어가 세운 다른 플래그를 읽을 때의 0 으로 덮어 지운다. 요점: **클리어 레지스터에는 RMW 를 쓰지 않는다. 지울 비트 위치에만 "지우는 값" 을 쓰는 단일 쓰기.** 꼬리: "모르는 칩이면?" → 레퍼런스 매뉴얼의 비트 접근 표기(rc_w1, rc_w0, w1c, rs, r)를 먼저 확인한다 (C-M02 표).

</details>

<details>
<summary>Q4. ISR 에서 printf / malloc 을 쓰면 정확히 무엇이 잘못됩니까? 디버깅 로그는 어떻게 남기죠?</summary>

printf: UART retarget 이면 폴링 송신이라 ISR 이 ms 단위로 길어져 다른 인터럽트(같은 IRQ 의 다음 이벤트 포함, 펜딩 비트는 1개라 합쳐짐)를 놓친다. 내부 버퍼·락 때문에 재진입 불가 — main 이 printf 중일 때 ISR 이 printf 하면 교착이나 출력 오염. 스택도 크게 쓴다. malloc: 비결정 시간, 힙 자료구조 재진입 문제(FreeRTOS `pvPortMalloc` 은 ISR 금지 API), 실패 처리, 단편화. 로그 대안: ISR 은 이벤트 코드+타임스탬프를 링 버퍼에 넣고 main/저우선 태스크가 출력, 또는 SEGGER RTT·ITM(SWO) 같은 논블로킹 채널, 타이밍 디버깅은 GPIO 토글 + 로직 분석기. FreeRTOS 에선 `xQueueSendFromISR` 처럼 FromISR API 만.

</details>

<details>
<summary>Q5. 고친 링 버퍼는 락 없이 안전하다고 했는데, 조건이 뭡니까? 듀얼 코어나 DMA 라면?</summary>

조건: ① 생산자 1명·소비자 1명 (SPSC), ② 각 인덱스의 writer 가 하나 (head 는 ISR, tail 은 main), ③ 인덱스 읽기/쓰기가 한 번의 접근으로 일어남 (정렬된 32비트 — Cortex-M 에서 단일 복사 원자적), ④ "데이터 쓰고 → 인덱스 공개" / "인덱스 확인 → 데이터 읽기" 순서가 지켜짐. 단일 코어에선 ④ 를 컴파일러만 지키면 되므로 volatile 버퍼나 signal fence 로 충분. 듀얼 코어(STM32H7 M7+M4, RP2040)면 CPU·버스가 재배치하고 캐시가 있으므로 `atomic_store_explicit(…, memory_order_release)` / `acquire` 로드(→ `DMB`), 공유 메모리의 캐시 정책(non-cacheable 영역 또는 clean/invalidate)이 필요하다 (C-A02, C-A03). DMA 가 생산자면 인덱스는 DMA 의 `NDTR` 카운터에서 계산하고, 캐시 있는 코어에선 읽기 전에 invalidate.

</details>

<details>
<summary>Q6. main 루프에서 "이벤트 없으면 WFI 로 잔다" 를 어떻게 안전하게 씁니까?</summary>

`if (take_events() == 0) __WFI();` 는 검사와 WFI 사이에 ISR 이 오면 이벤트를 두고 잠든다 — 다음 인터럽트(1 ms 뒤일 수도, 영영 없을 수도)까지 처리가 밀린다. 정석: 인터럽트를 **막은 채** 검사하고 WFI 한다. Cortex-M 의 WFI 는 PRIMASK=1 이어도 펜딩 인터럽트가 있으면 깨어나며(진입은 PRIMASK 를 풀 때), 그 사이 온 IRQ 는 펜딩 상태라 WFI 가 즉시 돌아온다. `pm = __get_PRIMASK(); __disable_irq(); if (!events) __WFI(); __set_PRIMASK(pm);` — 연습 1 이 이것을 주입 테스트로 재현한다. 꼬리: "WFE 는?" → 이벤트 레지스터가 래치되어 `SEV`/인터럽트 후 첫 WFE 는 바로 지나가므로, 스핀락 대기와 SEVONPEND 조합에 쓴다.

</details>

<details>
<summary>Q7. 스니펫 2 의 char 버그는 보드에서는 재현이 안 된다고 합니다. 어떻게 막습니까?</summary>

arm-none-eabi(AAPCS)와 aarch64 Linux 의 `char` 는 unsigned, Apple arm64·x86 은 signed — 실측 `__CHAR_UNSIGNED__` 로 확인했다. 그래서 같은 코드가 보드에선 맞고 호스트 테스트에선 틀린다(또는 반대로 이식할 때 터진다). 막는 법: ① 바이트 데이터는 항상 `uint8_t` (프로토콜 API 를 `const uint8_t *` 로), ② shift 전에 넓은 unsigned 로 캐스트, ③ 호스트 CI 에서 `-fsigned-char` 와 `-funsigned-char` 둘 다로 테스트하면 이런 의존성이 드러난다, ④ UBSan 으로 shift UB 확인, ⑤ 정적 분석/MISRA-style 규칙(essential type: 문자 타입으로 산술 금지, 10.x 계열 — C-A10). 꼬리: "uint8_t 로만 바꾸면 끝?" → 아니다. `u[7] << 24` 는 int 로 승격돼서 0x80 이상이면 여전히 UB (UBSan 실측 `130 by 24 places`).

</details>

<details>
<summary>Q8. 이 버그들을 사람 리뷰 말고 CI 에서 잡으려면 무엇을 넣겠습니까?</summary>

① **호스트 단위 테스트 + 레지스터 mock** (C-A01): bus 모델로 레지스터 의미(rc_w1 등)를 흉내 내고, 접근 n 직후 IRQ 주입을 n=1..k 전부 훑는다 — 3-1, 4-1, 4-2, 5-1 을 잡는다. ② **새니타이저 빌드**: ASan(1-3, 2-3), UBSan(2-2, 4-3), 스레드로 모델링한 부분은 TSan. ③ **경고·정적 분석**: `-Wall -Wextra -Wconversion -Wsign-conversion`, clang-tidy(bugprone-*), MISRA-style 검사기 — 단, 명시적 캐스트가 경고를 숨긴다는 것을 기억 (실측: starter 에서 `-Wsign-conversion` 은 1건만). ④ **ISR 금지 함수 검사**: 링크 후 호출 그래프에서 `*_IRQHandler` 로부터 `malloc`/`printf`/블로킹 함수에 도달하는지 (스택 사용량 분석 도구도 같은 그래프를 쓴다). ⑤ **타깃 HIL**: 실제 인터럽트 부하 테스트 + 로직 분석기. 어느 것도 혼자서는 충분하지 않다 — 이 레슨의 표 "테스트를 직접 설계할 때의 원칙" 처럼 버그 종류마다 맞는 도구가 다르다.

</details>

## 9. 추가 연습

### 연습 1 — 잠들기 직전 경쟁 (check-then-WFI)

원본 `idle_buggy()`: `if (READ(events) == 0) wfi();`. 검사 읽기 직후 IRQ 를 주입해 "이벤트를 두고 잠든" 것을 재현하고, 인터럽트를 막은 채 검사·WFI 하는 `idle_fixed()` 를 작성하라. 모델의 `wfi()` 는 펜딩 IRQ 가 있으면(PRIMASK 와 무관하게) 바로 깬다.

<details>
<summary>답 보기</summary>

```c
static void idle_fixed(void) {
    uint32_t pm = p_save();                    /* 막은 채로 검사하고 잔다 */
    if (p_rd(&p_events) == 0u) wfi();          /* 사이에 온 IRQ 는 펜딩 → WFI 가 즉시 깬다 */
    p_restore(pm);                             /* 여기서 ISR 실행 */
}
```

원본은 주입된 ISR 이 바로 실행돼 `events=1` 인 상태로 `wfi()` 에 들어간다 (`p_slept_with_event == 1`). 고친 판은 IRQ 가 펜딩되고, `wfi()` 가 펜딩을 보고 즉시 반환(`p_woke_by_pending == 1`), `p_restore` 에서 ISR 이 실행된다. 이벤트가 이미 있으면 잠들지 않는 것, 이벤트가 없으면 깨지 않는 것도 테스트한다. 실제 칩에서는 `__disable_irq(); if (!ev) { __DSB(); __WFI(); } __enable_irq();` 꼴이며, 저전력 모드 진입 코드(STM32 `HAL_PWR_EnterSLEEPMode`)도 같은 구조를 전제한다.

</details>

### 연습 2 — 임계 구역 대신 C11 원자 연산

스니펫 3 을 `_Atomic uint32_t` 로 다시 써라: ISR 은 `atomic_fetch_or`, main 은 전부 가져가는 `a_take()` 와 한 비트만 가져가는 `a_take_one(bit)`. 다른 비트를 건드리면 안 된다.

<details>
<summary>답 보기</summary>

`a_take` = `atomic_exchange_explicit(&a_events, 0u, memory_order_acquire)` — 읽기와 0 쓰기가 한 번의 원자 RMW. `a_take_one` = `(atomic_fetch_and_explicit(&a_events, ~bit, …) & bit) != 0` — 이전 값을 돌려받으므로 "내가 지운 비트가 서 있었는지" 를 정확히 안다. ISR 쪽은 `release` 로 올려서, 이벤트와 함께 ISR 이 쓴 데이터(예: DMA 결과 길이)가 main 의 `acquire` 뒤에 보이게 한다 (단일 코어에선 컴파일러 순서, 멀티코어에선 `DMB` 까지 — C-A03). 테스트는 `atomic_is_lock_free` 도 확인한다: lock-free 가 아니면 ISR↔main 사이에서 내부 락 교착이 날 수 있다 (Cortex-M0+ 는 32비트 RMW 도 lock-free 가 아니다, C-M14 표). `demo_race.c -DFIXED` 가 같은 코드로 200000/200000 을 보였다.

</details>

### 연습 3 — 구현 정의 동작 없는 N비트 부호 확장

24비트 ADC(ADS1220 류)·12비트 온도 센서 값을 `int32_t` 로 바꾸는 `sext(v, bits)` (1 ≤ bits ≤ 32)를 써라. `(int32_t)(v << (32-bits)) >> (32-bits)` 관용구는 쓰지 않는다 — 왜? 64비트 기준값과 10만 개 PRNG 입력으로 비교하라.

<details>
<summary>답 보기</summary>

관용구는 ① `uint32_t` → `int32_t` 변환이 범위 밖이면 구현 정의 (6.3.1.3p3), ② 음수 `>>` 도 구현 정의 (6.5.7p5) — 실무 컴파일러는 다 기대대로 하지만, 리뷰에서 "구현 정의에 기대지 않는 판" 을 요구받을 수 있다.

```c
uint32_t mask = (bits == 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
uint32_t x = v & mask, sign = 1u << (bits - 1u);
if ((x & sign) == 0u) return (int32_t)x;               /* 범위 안 */
return -(int32_t)(~x & mask) - 1;                      /* ~x&mask ≤ 2^31-1 → 오버플로 없음 */
```

음수일 때 값은 `x − 2^bits` = `−((2^bits − 1 − x) + 1)` = `−(~x & mask) − 1`. `bits == 32` 에서 `1u << 32` 는 UB 라 마스크를 따로 만든다. `sext(0x80000000, 32) == INT32_MIN` 까지 정확하다. 다른 정석: `(int32_t)((x ^ sign) - sign)` 은 마지막 변환이 다시 구현 정의.

</details>

### 연습 4 — 스니펫 6 리뷰: SPI 송신 루프

```c
void spi_send(const uint8_t *b, int n) {
    uint32_t *sr = (uint32_t *)&SPI1->SR;          /* 캐스트로 volatile 이 날아감 */
    for (int i = 0; i <= n; i++) {                 /* n+1 바이트 */
        while (!(*sr & SPI_SR_TXE)) { }            /* 타임아웃 없음 */
        SPI1->DR = b[i];                           /* b[n] 읽기 = 넘침 */
    }
}
```

버그 3개를 찾고, `spi_send(b, n, timeout)` 을 n 바이트를 보내면 n, 한 바이트라도 timeout 안에 TXE 가 안 서면 −1 을 돌려주도록 고쳐라. 테스트용 mock 은 DR 을 쓰면 TXE 가 내려가고 SR 을 k 번 읽으면 다시 선다.

<details>
<summary>답 보기</summary>

① `(uint32_t *)` 캐스트가 volatile 한정자를 버려서 대기 루프가 1-1 과 똑같이 `b .` 가 될 수 있다 (`-Wcast-qual` 이 잡는다), ② `i <= n` off-by-one — 쓰레기 1바이트 송신 + `b[n]` 범위 밖 읽기, ③ 타임아웃 없음 — SPI 클럭이 꺼졌거나 슬레이브가 멈추면 시스템 전체가 정지 (워치독 리셋만 남음). 고친 판은 SR 을 레지스터 접근(`spi_sr()` = `READ_REG(SPI1->SR)`)으로 매번 읽고, 바이트마다 남은 횟수를 세다가 0 이면 −1. 테스트: k=2 에서 정확히 3바이트, TXE 가 영원히 안 서면 0바이트 송신 후 −1 (멈추지 않음), 첫 바이트 뒤 타임아웃이면 1바이트 후 −1. 실무에서는 횟수 대신 SysTick ms 로 타임아웃을 재고, 실패 시 SPI 를 리셋하는 복구 경로까지 둔다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 19/19, 보고 없음
```

## 10. 핵심 정리

- 리뷰 순서: **공유 데이터 → ISR 몸통 → 레지스터 쓰기 → 정수 → 버퍼**. 찾은 것은 "줄 · 분류 · 증상 · 고침 · 테스트" 로 말한다.
- volatile 은 접근을 지키고 원자성은 주지 않는다. "읽기 → 쓰기" 가 ISR 과 겹치면 임계 구역(**저장/복원**) 또는 원자 RMW(`atomic_exchange`), 하드웨어가 주면 set/reset 레지스터(BSRR).
- 클리어 레지스터에 RMW 금지: rc_w1(EXTI_PR) 은 `= bit`, rc_w0(TIM_SR) 은 `= ~bit`, 별도 클리어 레지스터(DMA_IFCR)는 본 플래그 그대로. **맞는 줄을 고치지 않는 것도 실력**이다.
- ISR 은 기록만(고정 큐·플래그·카운터), 출력·할당·블로킹은 main. ISR 안의 RMW 도 더 급한 ISR 에게는 경쟁이다.
- 바이트는 `uint8_t`, shift 는 **먼저** `uint32_t` 로. `1 << 31`, `PIN(15) << 16`, `u8 << 24` 는 int 오버플로 UB. `char` 부호는 타깃마다 다르다 (Cortex-M: unsigned, Apple arm64: signed).
- 외부 길이 필드는 "받은 길이" 와 "받을 버퍼 크기" 둘 다로 검증. 링 버퍼는 `% N`, 가득 참은 `next == tail`, 넘친 건 세서 보이게.
- 타이밍 버그는 접근 n 직후 IRQ 주입으로 결정적 테스트, 넘침은 red zone, 한정자는 `_Generic`. 나머지는 ASan/UBSan/TSan 실측으로 — 단, 결과를 읽지 않는 코드는 최적화로 사라져 새니타이저도 못 본다.

## 참고자료

- C11 (N1570): 5.1.2.3p6 volatile 접근, 6.2.5p15 `char` 의 부호, 6.3.1.1p2 정수 승격, 6.3.1.3 정수 변환, 6.5.7p4–5 shift, 6.7.3p7 volatile, 6.8.5p6 반복문 종료 가정, 7.17.7 atomic RMW — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- STM32F4 Reference Manual RM0090 — EXTI_PR (rc_w1), TIMx_SR (rc_w0), GPIOx_BSRR, USART_SR RXNE/DR 읽기 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- STM32F1 Reference Manual RM0008 — DMA_ISR / DMA_IFCR — <https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- CMSIS-Core: `__get_PRIMASK`, `__disable_irq`, `__set_PRIMASK`, `__WFI` — <https://arm-software.github.io/CMSIS_5/Core/html/group__Core__Register__gr.html>
- ARMv7-M Architecture Reference Manual (DDI0403E) — WFI 깨어남 조건, 단일 복사 원자성, LDREX/STREX — <https://developer.arm.com/documentation/ddi0403/latest/>
- FreeRTOS — Memory Management (heap_4, `pvPortMalloc`), ISR 에서는 "FromISR" API 만 — <https://www.freertos.org/a00111.html>
- POSIX.1-2017 2.4.3 Signal Actions (async-signal-safe 함수 목록) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03>
- Clang sanitizers: AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer — <https://clang.llvm.org/docs/AddressSanitizer.html>, <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>, <https://clang.llvm.org/docs/ThreadSanitizer.html>
- 관련 레슨: C-B09 (volatile), C-M01 (MMIO volatile), C-M02 (RMW·W1C·rc_w0), C-M12 (UB 카탈로그), C-M14 (ISR 공유 데이터·PRIMASK), C-A01 (레지스터 mock), C-A02 (컴파일러 배리어), C-A03 (atomics·acquire/release), C-A10 (MISRA-style 정적 검사)
