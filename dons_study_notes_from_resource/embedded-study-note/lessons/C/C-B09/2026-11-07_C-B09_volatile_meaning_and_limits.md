---
id: C-B09
time: 50분
updated: 2026-11-07
---
# C-B09 volatile의 의미와 한계

## 학습 목표

- `volatile` 이 컴파일러에게 금지하는 것 세 가지 — 접근 **생략(elision)**, **병합(merging)**, 다른 volatile 접근과의 **순서 바꾸기(reordering)** — 를 asm 으로 직접 보여줄 수 있다.
- `volatile` 이 보장하지 **않는** 것 — 원자성(atomicity), non-volatile 접근과의 순서, CPU 메모리 순서(배리어), 캐시 일관성(DMA) — 을 구분해 말할 수 있다.
- 폴링 루프, 읽으면 지워지는(read-to-clear) 레지스터, 같은 레지스터 연속 쓰기를 "접근 횟수·순서" 관점으로 올바르게 작성한다.
- `volatile` 카운터의 lost update 를 두 스레드로 재현하고 C11 `_Atomic` / `atomic_fetch_add` 또는 임계 구역(critical section)으로 고친다.

## 1. 문제

"volatile 이 뭐예요?" 는 펌웨어 면접 단골 질문이고, 정답의 절반은 **"volatile 이 하지 않는 일"** 이다. 레지스터 폴링이 `-O2` 에서만 멈추는 버그, ISR 과 공유한 카운터가 가끔 틀리는 버그는 모두 이 경계를 모를 때 생긴다.

컴파일러는 "이 변수는 내 코드만 바꾼다" 고 가정하고 최적화한다. 그런데 레지스터(MMIO)는 **하드웨어가**, ISR 공유 변수는 **인터럽트가** 바꾼다. `volatile` 은 "이 객체는 네가 모르는 방법으로 바뀌거나, 읽고 쓰는 것 자체가 부작용이 있다 — 소스에 적힌 접근을 하나도 빼거나 합치지 마라" 는 표시다 (C11 6.7.3p7).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">while (!ready) { }  — ISR 이 ready 를 1로 바꾼다고 할 때 (clang -O2, arm64 실제 출력)</text>
  <rect x="10" y="40" width="350" height="180" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="380" y="40" width="350" height="180" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="25" y="64" fill="currentColor" font-weight="bold">int ready;  (volatile 없음)</text>
  <text x="395" y="64" fill="currentColor" font-weight="bold">volatile int ready_v;</text>
  <g fill="currentColor">
    <text x="25" y="90">컴파일러: "루프 안에서 ready 를 바꾸는 코드가</text>
    <text x="25" y="110">없다 → 값은 안 변한다."</text>
    <text x="25" y="136">while 버전 → 루프 통째로 삭제:</text>
    <text x="25" y="182">for(;;) 버전 → 한 번 읽고 무한 루프:</text>
    <text x="395" y="90">컴파일러: "읽을 때마다 메모리에서</text>
    <text x="395" y="110">다시 가져와야 한다."</text>
    <text x="395" y="136">매 반복 ldr 로 다시 읽는다:</text>
    <text x="395" y="206">ISR 이 1을 쓰면 다음 ldr 에서 보고 탈출</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="45" y="158">_wait_plain:  ret</text>
    <text x="45" y="204">ldr w8,[x8]; cbz → b LBB9_2(자기자신)</text>
    <text x="415" y="158">LBB1_1: ldr  w9, [x8]</text>
    <text x="415" y="178">        cbz  w9, LBB1_1</text>
  </g>
  <text x="10" y="245" fill="currentColor">왼쪽: ISR 이 플래그를 바꿔도 영원히 못 보거나(무한 루프), 아예 기다리지 않는다(삭제). 둘 다 -O0 에선 "잘 되던" 코드.</text>
  <text x="10" y="264" fill="currentColor">오른쪽: volatile 은 "매번 읽어라" 만 보장한다. 그 이상(원자성·캐시·배리어)은 Part C 와 풀이에서.</text>
</svg>
```

호스트(맥)에는 진짜 레지스터도 인터럽트도 없으므로, 이번 문제의 하네스는 모든 레지스터 접근을 `bus_read()` / `bus_write()` 로 보내서 **접근 기록(trace)** 을 문자열로 남긴다 (예: `"rSR rSR wDR=41"`). 실제 칩에서 이 두 함수는 `*r` 과 `*r = v` 한 줄이고, volatile 이 그 접근의 **횟수와 순서** 를 소스 그대로 지켜 준다. 테스트는 그 횟수와 순서가 의도대로인지 검사한다. 시뮬레이터는 정해진 n번째 접근 직후에 "인터럽트" 를 끼워 넣을 수도 있다.

### Part A — 폴링 루프 (낡은 사본 vs 다시 읽기)

`dev_wait_idle(d, max_polls)` — SR 의 BUSY 비트가 풀릴 때까지 폴링한다. 성공하면 SR 을 읽은 횟수(≥1), `max_polls` 번 읽어도 BUSY 면 `-1`. 시작 코드는 SR 을 **한 번만 읽고** 그 사본으로 루프를 도는데, 이것은 volatile 이 없을 때 컴파일러가 만드는 코드와 똑같은 모양이다.

### Part B — 읽기 부작용과 연속 쓰기

- `dev_drain(d, buf, cap)` — SR.RXNE 가 서 있는 동안 DR 을 읽어 `buf` 에 담는다. DR 읽기는 FIFO 에서 한 바이트를 **꺼내는(pop)** 부작용이 있다 → 바이트마다 SR 1번, DR **정확히 1번**. `cap` 에 도달하면 더 읽지 않는다.
- `dev_send_cmd(d, arg)` — `DR = arg → CR = CMD_LOAD → CR = CMD_GO`. 같은 CR 에 **연달아 두 번** 쓰는 프로토콜 — 하나로 합쳐지면 장치는 LOAD 를 못 본다.

### Part C — ISR 과 공유하는 카운터

`g_ticks` 는 `volatile` 이고 타이머 ISR 이 `+100` 한다. 주어진 `ticks_add_unsafe(n)` 은 읽기 1번 + 쓰기 1번이라, 그 사이에 인터럽트가 들어오면 ISR 의 갱신이 사라진다 (테스트가 이를 재현한다). `irq_disable()` / `irq_enable()` (Cortex-M 의 `__disable_irq()` / `__enable_irq()` 흉내) 로 `ticks_add_safe(n)` 을 작성하라. 마스크 중에 온 인터럽트는 펜딩(pending)되었다가 enable 직후 들어온다.

## 2. Problem (English)

> **volatile: what it guarantees and what it does not.**
>
> All register accesses go through `uint32_t bus_read(const volatile uint32_t *r)` and `void bus_write(volatile uint32_t *r, uint32_t v)`, which on target are simply `*r` and `*r = v`. A test harness records every access as a trace string.
>
> 1. `int dev_wait_idle(DEV_TypeDef *d, int max_polls)` — poll `SR` until `SR_BUSY` is clear. Return the number of `SR` reads performed on success, or `-1` after exactly `max_polls` reads that all showed BUSY.
> 2. `int dev_drain(DEV_TypeDef *d, uint8_t *buf, int cap)` — while `SR_RXNE` is set and fewer than `cap` bytes were read, read `DR` (a read-to-pop register) exactly once per byte. Return the byte count.
>    `void dev_send_cmd(DEV_TypeDef *d, uint8_t arg)` — write `DR = arg`, then `CR = CMD_LOAD`, then `CR = CMD_GO`. Both `CR` writes must reach the bus.
> 3. `g_ticks` is a `volatile uint32_t` also incremented by a timer ISR. Implement `void ticks_add_safe(uint32_t n)` so that an interrupt arriving between the read and the write cannot cause a lost update.
>
> Then explain: why does `while (!flag) {}` without `volatile` compile to `ret` under clang `-O2`? Why is `volatile` not sufficient for a counter shared between two threads, nor for a DMA buffer on a Cortex-M7 with D-cache enabled?
>
> Example traces: busy for 3 reads → `dev_wait_idle(d, 10) == 4`, trace `"rSR rSR rSR rSR"`; interrupt after the first access → `ticks_add_safe(5)` yields `g_ticks == 105`, trace `"DI rT wT=5 EI IRQ"`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 나오고 "5/17 passed" — TODO 를 채워 17/17 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

관찰용 파일 두 개가 같은 폴더에 있다 (하네스와 별개):

```bash
clang -std=c11 -O2 -S demo_poll.c -o - | less                     # volatile 유무별 asm 비교 (main 없음)
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_race.c -o demo_race && ./demo_race   # 두 스레드 카운터
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: "읽기" 를 루프 안으로</summary>

`bus_read(&d->SR)` 를 루프 **조건/본문 안에서** 매번 호출해야 한다. 루프 밖에서 한 번 읽은 지역 변수는 절대 안 바뀐다 — 그게 바로 volatile 없는 컴파일러가 하는 일이다.

</details>

<details>
<summary>힌트 2 — Part B: 부작용 있는 읽기는 한 번만, 결과는 지역 변수에</summary>

`if (DR != 0) buf[n] = DR;` 처럼 쓰면 DR 을 두 번 읽어 바이트 두 개가 빠진다. 한 번 읽어서 저장한다. 그리고 `n < cap` 을 **먼저** 검사해야 cap 에 도달했을 때 SR 을 쓸데없이 한 번 더 읽지 않는다 (`&&` 는 왼쪽이 거짓이면 오른쪽을 평가하지 않는다).

</details>

<details>
<summary>힌트 3 — Part C: 읽기~쓰기 구간을 통째로 막는다</summary>

`volatile` 은 "읽기 1번, 쓰기 1번이 실제로 일어난다" 만 보장한다. 그 **사이** 를 보호하려면 인터럽트를 잠깐 막는다: `irq_disable(); 읽기; 쓰기; irq_enable();`. 실제 펌웨어에서는 PRIMASK 를 저장·복원하는 형태가 더 안전하다 (중첩 호출 대비).

</details>

## 5. 풀이

### Part A — 컴파일러는 왜 루프를 지우나

`demo_poll.c` 의 ①과 ⑥ 을 `clang -std=c11 -O2 -S` (Apple clang 21, arm64) 로 컴파일한 **실제 출력** (`.cfi`/`Lloh` 줄 생략):

```text
_wait_plain:                     ; while (!ready) { }        ← volatile 없음
	ret
_wait_volatile:                  ; while (!ready_v) { }      ← volatile
	adrp	x8, _ready_v@GOTPAGE
	ldr	x8, [x8, _ready_v@GOTPAGEOFF]
LBB1_1:
	ldr	w9, [x8]                 ; 매 반복 메모리에서 다시 읽음
	cbz	w9, LBB1_1
	ret
_wait_plain_for:                 ; for (;;) { if (ready) break; }   ← volatile 없음
	adrp	x8, _ready@GOTPAGE
	ldr	x8, [x8, _ready@GOTPAGEOFF]
	ldr	w8, [x8]                 ; 딱 한 번 읽고
	cbz	w8, LBB9_2
	ret
LBB9_2:
	b	LBB9_2                   ; 0 이었으면 영원히 제자리
```

두 가지 다른 최적화가 보인다.

| 함수 | 컴파일러의 추론 | 결과 |
|---|---|---|
| `wait_plain_for` | 루프 안에 `ready` 를 바꾸는 코드가 없다 → 값은 루프 불변(loop-invariant) → 읽기를 루프 밖으로 **끌어올림(hoist)** | 한 번 읽고 0이면 `b .` 무한 루프 |
| `wait_plain` | 위 추론 + C11 6.8.5p6: 제어식이 상수가 아니고, 본문에 I/O·volatile 접근·원자 연산이 없는 루프는 **끝난다고 가정해도 된다**. 끝나려면 `ready != 0` 이어야 하므로 기다릴 필요 자체가 없다 | 루프 삭제, `ret` |
| `wait_volatile` | volatile 접근은 추상 기계 규칙대로 "정확히" 평가해야 하고(6.7.3p7), volatile 접근은 부작용이다(5.1.2.3p2) → 6.8.5p6 대상도 아님 | 매 반복 `ldr` |

Cortex-M4 로 크로스 컴파일해도 (`clang --target=armv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`) `wait_plain` 은 `bx lr` 한 줄이 된다. GCC 는 (여기선 돌려보지 않았지만 흔히 알려진 대로) C 의 `while` 루프도 보통 `wait_plain_for` 모양(한 번 읽고 무한 루프)으로 만든다. **어느 쪽이든 버그다.** 하네스의 시작 코드는 이 "끌어올린" 모양을 손으로 쓴 것이고, 정답은 매 반복 `bus_read` 를 호출하는 것이다.

### Part B — 읽기·쓰기를 빼거나 합치지 마라

같은 파일 ②③ 의 실제 출력:

```text
_read_twice_plain:      ldr w8,[x8]  ;  lsl w0, w8, #1        ← 1번 읽고 ×2
_read_twice_volatile:   ldr w9,[x8]  ;  ldr w8,[x8]  ;  add   ← 2번 읽음
_write_twice_plain:     mov w9,#0x42 ;  str w9,[x8]           ← 0x41 쓰기 사라짐
_write_twice_volatile:  mov w9,#0x41 ;  str  ;  mov w9,#0x42 ;  str   ← 둘 다 쓴다
```

일반 메모리라면 이 최적화는 전부 옳다. 하지만 레지스터에선:

| 레지스터 | 읽기/쓰기의 부작용 | 합치거나 빼면 |
|---|---|---|
| UART `DR` (읽기) | RX FIFO pop, RXNE 클리어 | 바이트 유실 / 같은 값 재사용 |
| TX FIFO `DR` (쓰기) | 바이트 push | 앞 바이트 전송 안 됨 |
| 명령 레지스터 `CR` | 쓸 때마다 명령 실행 (LOAD → GO) | LOAD 없이 GO |
| STM32F4 I2C `SR1` → `SR2` | 이 순서로 읽어야 ADDR 플래그가 지워짐 | 플래그가 안 지워져 ISR 무한 재진입 |

`dev_drain` 의 trace 가 `"rSR rDR rSR rDR rSR rDR rSR"` 이어야 하는 이유: 마지막 `rSR` 은 "RXNE 가 꺼졌다" 를 확인하는 읽기다. `cap` 이 2 일 때는 `n < cap` 이 먼저 거짓이 되어 SR 을 다시 읽지 않으므로 `"rSR rDR rSR rDR"` 로 끝나고, 세 번째 바이트는 FIFO 에 그대로 남는다.

> **순서**: 컴파일러는 volatile 접근끼리의 순서를 바꾸지 못한다 (각 volatile 접근은 부작용이고, 시퀀스 포인트 사이의 부작용 순서를 지켜야 한다 — 5.1.2.3). 그래서 `DR=arg; CR=LOAD; CR=GO;` 는 이 순서로 명령어가 나온다. 하지만 **non-volatile 접근은 그 사이를 자유롭게 넘나든다** — 아래 ⑤.

### Part C — volatile 은 원자성을 주지 않는다

`volatile` 변수에 `+= n` 을 하면 명령어는 여전히 **읽기 → 더하기 → 쓰기** 3개다. 하네스는 그 "읽기 직후" 에 인터럽트를 끼워 넣는다:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">g_ticks = 0, 1번째 접근 직후 타이머 IRQ (ISR: ticks += 100)</text>
  <text x="10" y="58" fill="currentColor">unsafe</text>
  <rect x="90" y="40" width="110" height="28" class="f-acc"/><text x="100" y="59" fill="currentColor">rT → t=0</text>
  <rect x="215" y="40" width="170" height="28" class="f-warn"/><text x="225" y="59" fill="currentColor">IRQ: ticks = 100</text>
  <rect x="400" y="40" width="140" height="28" class="f-acc"/><text x="410" y="59" fill="currentColor">wT = t+5 = 5</text>
  <text x="560" y="59" fill="currentColor">→ 5  (100 유실)</text>
  <text x="10" y="128" fill="currentColor">safe</text>
  <rect x="90" y="110" width="50" height="28" class="f-mut"/><text x="100" y="129" fill="currentColor">DI</text>
  <rect x="150" y="110" width="100" height="28" class="f-acc"/><text x="160" y="129" fill="currentColor">rT → t=0</text>
  <rect x="260" y="110" width="100" height="28" class="f-acc"/><text x="270" y="129" fill="currentColor">wT = 5</text>
  <rect x="370" y="110" width="50" height="28" class="f-mut"/><text x="380" y="129" fill="currentColor">EI</text>
  <rect x="430" y="110" width="110" height="28" class="f-ok"/><text x="440" y="129" fill="currentColor">IRQ: 105</text>
  <text x="560" y="129" fill="currentColor">→ 105</text>
  <path d="M175 150 Q 300 185 470 142" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="200" y="190" fill="currentColor">마스크 중 도착한 IRQ 는 펜딩 → EI 직후 실행</text>
  <text x="10" y="220" fill="currentColor">volatile 은 rT·wT 가 실제로 일어나게 할 뿐, 두 접근 "사이" 를 지켜 주지 않는다.</text>
</svg>
```

진짜 동시성으로도 확인해 보자. `demo_race.c` 는 두 스레드가 각각 1,000,000번 `v_count = v_count + 1` (volatile) 과 `atomic_fetch_add_explicit(&a_count, 1, memory_order_relaxed)` (`_Atomic`) 를 한다. M 시리즈 맥에서 세 번 실행한 실제 출력:

```text
expected          : 2000000
volatile counter  : 1001745  (lost 998255)
_Atomic  counter  : 2000000  (lost 0)

expected          : 2000000
volatile counter  : 1016830  (lost 983170)
_Atomic  counter  : 2000000  (lost 0)

expected          : 2000000
volatile counter  : 1001033  (lost 998967)
_Atomic  counter  : 2000000  (lost 0)
```

거의 절반이 사라진다. 두 루프의 `-O2` asm (실제 출력, 루프 본문만):

```text
_inc_volatile:   LBB1_1: ldr x10,[x9,_v_count@PAGEOFF] ; add x10,x10,#1 ; str x10,[...] ; subs ; b.ne
_inc_atomic:     LBB2_1: ldadd x10, x11, [x9]          ; subs ; b.ne     ← ARMv8.1 LSE 원자 덧셈 1개
```

Cortex-M 에서는 어떨까 (`--target=armv7em-none-eabi -mcpu=cortex-m4`, 실제 출력):

```text
inc_v:  ldr r1,[r0] ; adds r1,#1 ; str r1,[r0]                         ← volatile: 여전히 3단계
inc_a:  .LBB1_1: ldrex r1,[r0] ; adds r1,#1 ; strex r2,r1,[r0] ; cmp r2,#0 ; bne .LBB1_1
```

`LDREX/STREX` 는 "읽은 뒤 누가 끼어들었으면(인터럽트 포함) STREX 가 실패 → 재시도" 하는 배타적 접근 쌍이다. 반면 **Cortex-M0/M0+ (ARMv6-M)** 는 이 명령이 없어서 같은 코드가 `bl __atomic_fetch_add_4` 라이브러리 호출이 되고, clang 이 `-Watomic-alignment` 경고(“max lock-free size (0 bytes)”)를 낸다 — 그 함수는 보통 인터럽트를 잠깐 꺼서 구현한다. 그래서 단일 코어 MCU 의 가장 보편적인 해법이 Part C 의 **임계 구역** 이다.

> 표준 관점: 두 스레드가 같은 non-atomic 객체에 동기화 없이 접근하면 **데이터 경쟁(data race) → UB** 다 (C11 5.1.2.4p25). `volatile` 은 원자(atomic) 타입이 아니므로 이 규칙에서 빠지지 못한다. `demo_race.c` 의 volatile 절반은 "관찰용 UB" 다. ISR↔main 공유는 같은 스레드 안의 시그널 핸들러 모델에 가깝고, 그 깊은 이야기는 C-M14 에서.

### ⑤ volatile 이 non-volatile 접근을 지켜 주지 않는 증거

```c
int data;                 /* 일반 변수 */
void publish(void) { data = 1; ready_v = 1; data = 2; }
```

실제 `-O2` 출력:

```text
_publish:
	mov	w9, #1
	str	w9, [ready_v]     ; volatile 쓰기
	mov	w9, #2
	str	w9, [data]        ; data = 1 은 사라졌다 (dead store 제거)
```

ISR 이 `ready_v == 1` 을 보고 `data` 를 읽으면 1 을 기대하지만, `data = 1` 저장은 **존재하지도 않는다**. volatile 접근은 "이 접근을 지켜라" 이지 "이 지점을 넘어 다른 메모리 접근을 옮기지 마라(컴파일러 배리어)" 가 아니다. 공유되는 `data` 도 volatile 로 하거나, C11 원자 연산(release/acquire)이나 컴파일러 배리어를 써야 한다 — 메모리 모델 상세는 이후 M 레벨.

### volatile 이 책임지는 층 vs 책임지지 않는 층

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">소스 한 줄 REG = x 가 실제 장치에 닿기까지 — 각 층의 책임자</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="170" height="44" rx="6" class="f-ok"/>
    <rect x="10" y="100" width="170" height="44" rx="6" class="f-mut"/>
    <rect x="10" y="160" width="170" height="44" rx="6" class="f-mut"/>
    <rect x="10" y="220" width="170" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="60">① 컴파일러</text><text x="22" y="77" font-size="12">소스 → 명령어</text>
    <text x="22" y="120">② CPU 파이프라인</text><text x="22" y="137" font-size="12">write buffer, 순서 재배치</text>
    <text x="22" y="180">③ 캐시 (M7, A코어)</text><text x="22" y="197" font-size="12">D-cache 라인</text>
    <text x="22" y="240">④ 다른 마스터</text><text x="22" y="257" font-size="12">ISR·다른 코어·DMA</text>
    <text x="200" y="60">volatile: 접근 생략·병합 금지, volatile 끼리 순서 유지  ✔</text>
    <text x="200" y="77" font-size="12">(non-volatile 접근은 그 사이를 넘나들 수 있음 ✘)</text>
    <text x="200" y="120">volatile ✘ → DMB / DSB / ISB 배리어 (CMSIS __DMB() __DSB())</text>
    <text x="200" y="137" font-size="12">예: ISR 끝에서 플래그 클리어 후 DSB 또는 되읽기 — 안 하면 ISR 재진입</text>
    <text x="200" y="180">volatile ✘ → 캐시 유지보수: Clean(DMA TX 전), Invalidate(DMA RX 후)</text>
    <text x="200" y="197" font-size="12">또는 MPU 로 버퍼 영역을 non-cacheable 지정 (32B 라인 정렬)</text>
    <text x="200" y="240">volatile ✘ → 원자성: 임계 구역, LDREX/STREX, C11 _Atomic</text>
    <text x="200" y="257" font-size="12">멀티 스레드/멀티 코어 공유는 data race = UB (5.1.2.4p25)</text>
  </g>
  <text x="10" y="290" fill="currentColor">volatile 은 ① 층 하나만 책임진다. 나머지는 각각 다른 도구가 필요하다.</text>
</svg>
```

정리 표:

| volatile 이 보장 | 근거 |
|---|---|
| 소스의 모든 volatile 읽기·쓰기가 실제로 일어남 (생략·병합 없음) | 6.7.3p7, 5.1.2.3p6 |
| volatile 접근끼리의 프로그램 순서 유지 (컴파일러 수준) | 5.1.2.3p2·p6 (부작용) |
| 매번 메모리에서 다시 읽음 (레지스터 캐싱 금지) | 6.7.3p7 |

| volatile 이 보장하지 **않음** | 대신 쓰는 것 |
|---|---|
| 원자성 (`v++` 은 RMW 3단계, 64비트 값은 32비트 MCU 에서 2번에 나눠 접근) | 임계 구역, `_Atomic`, LDREX/STREX |
| non-volatile 접근과의 순서 | 둘 다 volatile, 원자 연산, 컴파일러 배리어 |
| CPU/버스 수준 순서 (write buffer, 멀티 코어) | DMB/DSB, 원자 연산의 memory_order |
| 캐시 일관성 (DMA 버퍼) | 캐시 clean/invalidate, non-cacheable 영역 |
| "접근" 의 폭·방식 | **구현 정의** (6.7.3p7 마지막 문장) — 비트필드 volatile 쓰기 폭은 ABI 에 따라 다름 (C-M03) |

Part C 는 C-B01 의 GPIO 와 이어진다: `ODR |= m` 이 ISR 과 경쟁하는 건 volatile 이어도 RMW 이기 때문이고, 그래서 하드웨어가 **단일 쓰기로 끝나는 BSRR** 을 준다 (C-B01 Part C 참고). Part C 의 임계 구역은 BSRR 같은 레지스터가 없을 때의 소프트웨어 해법이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san   # 17/17, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 이 세우는 플래그를 그냥 `int flag;` 로 선언 | `-O2` 에서 폴링 루프가 삭제되거나 무한 루프 (Part A asm) | `volatile` (또는 `_Atomic`) |
| `volatile int count; count++;` 를 ISR 과 공유하며 "안전하다" 고 믿음 | RMW 3단계 → lost update (demo_race: 절반 유실) | 임계 구역 / `atomic_fetch_add` |
| `if (UART->DR == 'a' \|\| UART->DR == 'b')` | DR 을 두 번 읽어 바이트 두 개를 pop | `uint8_t c = UART->DR;` 후 비교 |
| `x = REG + REG;` | 같은 volatile 객체 접근 두 개가 서로 unsequenced → 엄밀히 UB (5.1.2.3p2 + 6.5p2), MISRA C 13.2 위반 | 문장을 나눠 `a = REG; b = REG;` |
| 플래그만 volatile, 데이터는 일반 변수 | 데이터 저장이 사라지거나 플래그 뒤로 이동 가능 (⑤ publish) | 데이터도 volatile, 또는 release/acquire 원자 연산 |
| `uint32_t *p = (uint32_t *)&DEV->SR;` 로 volatile 캐스트 제거 | volatile 로 정의된 객체를 non-volatile lvalue 로 접근 = UB (6.7.3p6) | 포인터 타입에 volatile 유지 |
| DMA 수신 버퍼에 `volatile` 을 붙여 캐시 문제 해결 시도 | CPU 는 여전히 낡은 캐시 라인을 읽는다 | `SCB_InvalidateDCache_by_Addr()` 또는 non-cacheable 영역 |
| `for (volatile int i = 0; i < 1000; i++);` 로 정밀 지연 | 클럭·최적화·플래시 대기 상태에 따라 시간이 바뀜 | 하드웨어 타이머, `DWT->CYCCNT` |

## 8. 면접 Q&A

<details>
<summary>Q1. volatile 은 언제 써야 하나요? 대표 용도를 말해보세요.</summary>

① 메모리 맵 레지스터(MMIO) ② ISR(시그널 핸들러)과 메인이 공유하는 플래그 — 표준은 시그널 핸들러에서 `volatile sig_atomic_t` 정적 객체(와 lock-free 원자 객체) 쓰기만 허용한다 (7.14.1.1p5) ③ `setjmp` 후 수정되고 `longjmp` 뒤에 쓰이는 지역 변수 (7.13.2.1p3, 추가 연습 3). 꼬리질문 "스레드 간 공유는?" → volatile 이 아니라 `_Atomic`/뮤텍스. volatile 은 스레드 동기화 도구가 아니다.

</details>

<details>
<summary>Q2. volatile int 카운터를 두 스레드(또는 ISR 과 메인)가 증가시키면 어떻게 되나요?</summary>

`ldr/add/str` 3개 명령이라 그 사이에 끼어들면 갱신을 잃는다. 실측: 두 스레드 × 1,000,000 → 약 1,000,000 만 남음. 고침: 멀티 스레드면 `atomic_fetch_add` (arm64 `ldadd`, Cortex-M3/M4 `ldrex/strex` 루프), 단일 코어 MCU 의 ISR 공유면 임계 구역. 꼬리질문 "Cortex-M0 는?" → LDREX/STREX 가 없어 `_Atomic` RMW 가 libcall 이 되므로 보통 PRIMASK 로 인터럽트를 막는다.

</details>

<details>
<summary>Q3. const volatile 은 모순 아닌가요? 어디에 쓰나요?</summary>

모순이 아니다. `const` 는 "**이 코드가** 이 lvalue 로 쓰지 않는다", `volatile` 은 "**다른 무언가가** 바꿀 수 있으니 매번 읽어라". 읽기 전용 상태 레지스터(`const volatile uint32_t *SR`), 하드웨어가 갱신하는 입력 레지스터(GPIO `IDR`)에 쓴다. CMSIS 의 `__I` 매크로가 `volatile const` 다.

</details>

<details>
<summary>Q4. volatile uint32_t *p 와 uint32_t * volatile p 의 차이는?</summary>

앞은 **가리키는 대상** 이 volatile (레지스터 포인터 — `*p` 접근을 생략 못 함). 뒤는 **포인터 변수 자체** 가 volatile (ISR 이 포인터를 바꾸는 경우, 예: 현재 DMA 버퍼 포인터를 ISR 이 스왑). 둘 다면 `volatile uint32_t * volatile p`. 오른쪽에서 왼쪽으로 읽는다 — `const` 와 같은 규칙 (C-B08 참고). 추가 연습 1 이 `_Generic` 으로 판별한다.

</details>

<details>
<summary>Q5. 플래그를 volatile 로 했으니 ISR 이 채운 버퍼를 메인에서 읽어도 안전하죠?</summary>

아니다. ① 버퍼가 non-volatile 이면 컴파일러가 버퍼 접근을 플래그 접근 너머로 옮기거나 없앨 수 있다 (⑤ publish 실측: `data = 1` 이 사라짐) ② 멀티 코어/A 코어면 CPU 가 순서를 바꿀 수 있어 배리어가 필요 ③ 단일 코어 Cortex-M 에서 버퍼까지 volatile 로 하거나 컴파일러 배리어(`__asm volatile("" ::: "memory")`)를 쓰면 실무적으로는 동작하지만, 이식성 있는 답은 C11 `atomic_store_explicit(&flag, 1, memory_order_release)` / `acquire` 쌍이다.

</details>

<details>
<summary>Q6. Cortex-M7 에서 DMA 로 받은 데이터가 가끔 옛날 값이에요. 버퍼에 volatile 을 붙이면 되나요?</summary>

안 된다. volatile 은 "CPU 가 load 명령을 낸다" 까지만 보장하고, 그 load 가 **D-cache 에 적중하면 낡은 라인** 을 돌려준다. DMA 는 캐시를 거치지 않고 SRAM 에 직접 쓴다. 해결: RX 완료 후 `SCB_InvalidateDCache_by_Addr(buf, len)`, TX 시작 전 `SCB_CleanDCache_by_Addr`, 버퍼는 캐시 라인(32B) 정렬·크기 배수로. 또는 MPU 로 해당 영역을 non-cacheable 지정. (M0/M3/M4 는 D-cache 가 없어 이 문제가 없다 — 그래서 M7 이식 때 처음 터진다.)

</details>

<details>
<summary>Q7. ISR 마지막 줄에서 인터럽트 플래그를 지웠는데 ISR 이 한 번 더 들어와요. 왜죠?</summary>

플래그 클리어 쓰기는 volatile 이라 명령어로는 나갔지만, write buffer/버스 브리지를 지나 주변장치에 **도착하기 전에** 예외 복귀가 일어나면 NVIC 가 아직 서 있는 요청을 보고 다시 진입한다. 고침: 클리어를 ISR 앞쪽으로 옮기거나, 클리어 후 `__DSB()` 또는 같은 레지스터를 한 번 되읽기. "volatile 은 CPU/버스 순서를 보장하지 않는다" 의 대표 사례.

</details>

<details>
<summary>Q8. Linux 커널은 왜 volatile 을 거의 안 쓰나요?</summary>

커널 문서 "volatile considered harmful": 공유 데이터는 락(스핀락·뮤텍스)으로 보호하는데, 락 연산이 이미 컴파일러·CPU 배리어를 포함하므로 volatile 은 최적화만 막을 뿐 정확성에 기여하지 않는다. MMIO 는 `readl()/writel()` 접근자로 하는데, 이들은 내부적으로 volatile 접근 + 필요한 배리어를 담는다. 한 번만 정확히 읽고 싶을 땐 `READ_ONCE()/WRITE_ONCE()` (내부는 volatile 캐스트). 즉 "volatile 은 접근자 안에 숨겨라".

</details>

## 9. 추가 연습

### 연습 1 — volatile 이 어디에 붙었나

`volatile uint32_t *a`, `uint32_t volatile *b`, `uint32_t *volatile c`, `const volatile uint32_t *d` 중 **가리키는 대상** 이 volatile 인 것은? `_Generic` 으로 판별하는 매크로 `POINTEE_IS_VOLATILE(p)` 를 만들어라.

<details>
<summary>답 보기</summary>

a, b, d (a 와 b 는 같은 타입). c 는 포인터 변수가 volatile 이고 대상은 그냥 `uint32_t`. `_Generic` 의 제어식은 lvalue 변환을 거쳐 **최상위 한정자가 떨어지므로** c 는 `uint32_t *` 로 매칭되어 0. 연관 목록에 `volatile uint32_t *` 와 `const volatile uint32_t *` 두 개를 둔다.

</details>

### 연습 2 — 읽으면 지워지는(read-to-clear) 이벤트 레지스터

읽는 순간 모든 비트가 0이 되는 이벤트 레지스터에서 EV_RX 와 EV_ERR 를 둘 다 처리하라. `if (EV & EV_RX) ...; if (EV & EV_ERR) ...;` 는 무엇을 잃나?

<details>
<summary>답 보기</summary>

두 번째 `if` 의 읽기는 이미 지워진 0을 본다 → EV_ERR 유실. volatile 이라서 컴파일러가 두 읽기를 합쳐 주지도 않는다 (합치면 안 되는 게 맞다). **한 번 읽어 지역 변수(스냅샷)에 저장** 하고 그 사본으로 모든 비트를 검사한다. Part B 의 DR 규칙과 같은 원리.

</details>

### 연습 3 — setjmp/longjmp 와 volatile 지역 변수

`setjmp` 로 재시도 지점을 만들고, 실패하면 `longjmp` 로 돌아와 3번째 시도에서 성공하는 `retry_until_three()` 를 작성하라. 시도 횟수 변수에는 무엇이 필요한가?

<details>
<summary>답 보기</summary>

`volatile int tries`. C11 7.13.2.1p3: `setjmp` 를 호출한 함수의 자동 변수 중 volatile 이 아니고 `setjmp`~`longjmp` 사이에 바뀐 것은 longjmp 후 값이 **불확정(indeterminate)**. 레지스터에만 있던 값이 setjmp 시점 값으로 되돌아갈 수 있기 때문이다. `(void)setjmp(env);` 는 표준이 허용하는 호출 형태 중 하나(식 문장 전체, void 캐스트 가능 — 7.13.1.1p4).

</details>

### 연습 4 — volatile 대신 원자 연산으로 이벤트 넘기기

ISR 이 이벤트 비트를 세우고(`isr_post`), 메인이 한 번에 가져가며 0으로 만드는(`main_take`) 코드를 lost update 없이 작성하라. 덤으로 0, 1, 2… 번호표를 주는 `ticket_take()`.

<details>
<summary>답 보기</summary>

`atomic_fetch_or(&ev, bit)` 와 `atomic_exchange(&ev, 0)` — 각각이 하나의 원자 RMW 라 "읽고 0 쓰기" 사이에 세워진 비트를 잃지 않는다. volatile 로 `v = ev; ev = 0;` 을 쓰면 그 사이 ISR 비트가 지워진다. `atomic_fetch_add` 는 **이전 값** 을 돌려주므로 번호표가 된다. (단일 스레드 테스트라 race 자체는 demo_race.c 로 확인.)

</details>

### 연습 5 — 접근 횟수 세기 (asm 으로 확인)

`r` 이 `volatile uint32_t *` 일 때 각 함수의 읽기/쓰기 횟수는? `(a) *r |= 1u;` `(b) *r = *r;` `(c) (void)*r;` `(d) *r &= ~2u; *r |= 4u;` — `clang -std=c11 -O2 -S demo_poll.c -o -` 의 ⑦ 로 확인.

<details>
<summary>답 보기</summary>

실측 asm:

```text
_q_a:  ldr w8,[x0] ; orr w8,w8,#0x1 ; str w8,[x0]                 → 읽기 1, 쓰기 1
_q_b:  ldr w8,[x0] ; str w8,[x0]                                  → 읽기 1, 쓰기 1 (지워지지 않음)
_q_c:  ldr wzr,[x0]                                               → 읽기 1 (결과 버림)
_q_d:  ldr ; and ; str ; ldr ; orr ; str                          → 읽기 2, 쓰기 2 (합쳐지지 않음)
```

(c) 는 "dummy read" — STM32F4 I2C 에서 `(void)I2C1->SR2;` 로 ADDR 플래그를 지우는 관용구. (d) 를 한 번의 쓰기로 만들고 싶으면 지역 변수로 `v = *r; v = (v & ~2u) | 4u; *r = v;` 로 직접 합친다 — 그게 맞는지는 하드웨어(중간 상태가 의미 있는지)가 결정한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
```

## 10. 핵심 정리

- `volatile` = "이 객체는 컴파일러 모르게 바뀌거나 접근 자체가 부작용" → 접근 **생략·병합 금지, volatile 끼리 순서 유지**. 그게 전부다.
- volatile 없는 폴링 루프는 `-O2` 에서 삭제(`ret`)되거나 한 번 읽고 무한 루프가 된다 (C11 6.8.5p6 + loop-invariant hoist).
- 부작용 있는 레지스터(DR pop, read-to-clear, 명령 레지스터)는 **정확히 한 번 읽어 지역 변수에**, 연속 쓰기는 그대로 둘 다.
- `volatile x++` 은 원자적이 아니다: 두 스레드 × 1M → 약 1M 유실. 고침: 임계 구역 / `_Atomic` (`ldadd`, `ldrex/strex`).
- non-volatile 접근은 volatile 접근 너머로 옮겨지거나 사라진다 → 공유 데이터도 보호하라.
- CPU 순서는 DMB/DSB, DMA 캐시는 clean/invalidate — volatile 의 책임이 아니다.
- 표준 3대 용도: MMIO, 시그널/ISR 플래그(`volatile sig_atomic_t`), setjmp 지역 변수. 스레드 동기화는 아니다.

## 참고자료

- C11 (N1570) 6.7.3 Type qualifiers (p6 volatile 캐스트 제거 UB, p7 의미), 5.1.2.3 Program execution, 6.8.5p6 루프 종료 가정, 5.1.2.4p25 data race, 7.13.2.1 longjmp, 7.14.1.1 signal — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [Linux kernel docs — Why the "volatile" type class should not be used](https://www.kernel.org/doc/html/latest/process/volatile-considered-harmful.html)
- [ARM CMSIS-Core — Cortex-M7 D-cache 함수 (`SCB_CleanDCache_by_Addr`, `SCB_InvalidateDCache_by_Addr`)](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [ARM Application Note 321 — ARM Cortex-M Programming Guide to Memory Barrier Instructions](https://developer.arm.com/documentation/dai0321/latest/)
- John Regehr, ["Nine ways to break your systems code using volatile"](https://blog.regehr.org/archives/28)
- STM32F4 Reference Manual RM0090 — I2C SR1/SR2 읽기 순서로 ADDR 클리어
- 관련 레슨: C-B01 (BSRR 와 RMW 경쟁), C-B08 (const 위치), C-M01 (volatile 레지스터 구조체), C-M03 (비트필드 접근 폭), C-M14 (ISR 공유 변수·tearing)
