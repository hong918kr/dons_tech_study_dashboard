---
id: C-M14
time: 80분
updated: 2026-11-07
---
# C-M14 인터럽트 안전 코드: 공유 변수와 원자성

## 학습 목표

- ISR 과 메인 루프가 공유하는 **64비트 값·구조체의 찢어진 읽기(torn read)** 와 **lost update** 를 결정적으로 재현하고, "왜 그 순간에 깨지는지" 를 명령어 단위로 설명할 수 있다.
- C11 이 시그널 핸들러(=ISR 모델)에 실제로 보장하는 것 — `volatile sig_atomic_t` 대입과 lock-free 원자 객체 — 만으로 공유를 설계할 수 있다 (7.14.1.1p5, 5.1.2.3p5).
- `sigprocmask` 로 **저장/복원형 임계 구역(critical section)** 을 만들고, 이것이 Cortex-M 의 `__get_PRIMASK()` / `__disable_irq()` / `__set_PRIMASK()` 와 같은 구조임을 말할 수 있다 (중첩 안전, 펜딩 인터럽트).
- 인터럽트를 막지 않는 **재읽기 루프(hi → lo → hi)** 와 **seqlock** 을 구현하고, 언제 쓸 수 있고 언제 못 쓰는지(리더가 ISR 일 때, 인터럽트가 막힌 상태일 때) 구분한다.
- Cortex-M 현실: `LDRD` 는 64비트 원자가 아니다, PRIMASK vs BASEPRI, LDREX/STREX, 핸들러 안에서 `printf` 금지(async-signal-safe).

## 1. 문제

"ISR 과 main 이 같이 쓰는 변수는 어떻게 보호하나요?" 는 펌웨어 면접에서 C-B09 의 volatile 질문 바로 다음에 나오는 꼬리질문이다. 정답은 "volatile 붙이면 됩니다" 가 아니라 **"그 값이 CPU 에서 한 번의 접근으로 읽히는가, 그렇지 않으면 어떤 방식으로 일관성을 지키는가"** 다.

32비트 MCU(Cortex-M0/M3/M4)에서 64비트 타임스탬프를 읽는 코드는 명령어 **두 개**다. 그 사이에 1 ms 타이머 ISR 이 들어와 하위 32비트가 자리올림(carry)하면, 메인은 **옛 hi + 새 lo** 를 합쳐 존재한 적 없는 시각을 얻는다. 시간이 2^32 µs ≈ **71.6분 뒤로** 간다. 이런 버그는 며칠에 한 번, 카운터가 넘치는 순간에만 나서 재현이 어렵다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ts_read_naive(): hi 를 읽은 직후 1 ms tick ISR 이 끼어들고, 하필 lo 가 자리올림한다</text>
  <text x="10" y="62" fill="currentColor">main</text>
  <text x="10" y="132" fill="currentColor">ISR</text>
  <line x1="70" y1="150" x2="730" y2="150" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="80" y="42" width="150" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="90" y="62" fill="currentColor" font-family="monospace">rH → hi = 0</text>
  <rect x="245" y="112" width="250" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="255" y="132" fill="currentColor" font-family="monospace">lo += 1000 → 0x1F4, hi = 1</text>
  <rect x="510" y="42" width="170" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="520" y="62" fill="currentColor" font-family="monospace">rL → lo = 0x1F4</text>
  <path d="M230 57 L245 127" stroke="currentColor" fill="none"/>
  <path d="M495 127 L510 57" stroke="currentColor" fill="none"/>
  <g fill="currentColor">
    <text x="80" y="176">메모리 (진짜 값)</text>
    <text x="80" y="198" font-family="monospace">ISR 전: 0x0_FFFFFE0C = 4294966796 µs</text>
    <text x="80" y="218" font-family="monospace">ISR 후: 0x1_000001F4 = 4294967796 µs</text>
    <text x="420" y="176">main 이 조립한 값</text>
    <text x="420" y="198" font-family="monospace">0x0_000001F4 = 500 µs</text>
    <text x="420" y="218">→ 어느 순간에도 존재한 적 없는 값</text>
  </g>
  <rect x="10" y="236" width="720" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="258" fill="currentColor">옛 hi + 새 lo → 2^32 µs(약 71.6분) 뒤로 간다.</text>
  <text x="22" y="280" fill="currentColor">lo 를 먼저 읽으면 반대로 옛 lo + 새 hi → 71.6분 앞으로. 읽는 순서만 바꿔선 못 고친다.</text>
</svg>
```

호스트(맥)에는 인터럽트가 없으니 **POSIX 시그널로 ISR 을 흉내** 낸다: `SIGALRM` 핸들러 = ISR, 메인 흐름 = 스레드 모드(thread mode). 그런데 arm64 맥은 `uint64_t` 를 `ldr x0` 한 번에 읽으므로 진짜 64비트 변수로는 찢어짐이 안 생긴다 (5. 풀이에서 asm 확인). 그래서 32비트 MCU 처럼 **`uint32_t g_ts_hi, g_ts_lo` 두 칸에 나눠 저장** 한다.

경쟁을 매번 똑같이 재현하기 위해, 메인 쪽 공유 변수 읽기는 모두 `rd32()` 를 거친다. `rd32()` 는 "n 번째 읽기 직후" 에 `raise(SIGALRM)` 을 호출해 **정확히 그 지점에 ISR 을 끼워 넣는다**. 실제 칩에서 `rd32(p)` 는 `*p` 한 줄이다. 진짜 타이머(`setitimer`)로 찢어짐을 "통계적으로" 세는 실험은 `demo_itimer.c` 에 따로 있다.

주어진 ISR (`timer_isr`) 이 하는 일:

| 순서 | 공유 데이터 | ISR 의 쓰기 |
|---|---|---|
| ① | 64비트 µs 타임스탬프 `g_ts_hi:g_ts_lo` | `lo += 1000`, 넘치면 `hi += 1` (두 번에 나눠 씀) |
| ② | ADC 샘플 구조체 `g_sample {seq, ch0, ch1, ch2}` (불변식 `chK == seq*100+K`) | `ver++` → 필드 4개 → `ver++` (seqlock) |
| ③ | `volatile sig_atomic_t g_tick_flag`, `atomic_uint g_tick_count` | `flag = 1`, `count += 1` (원자적) |

시작 코드에는 버그 재현용 함수 3개가 주어진다 — `ts_read_naive()` (찢어진 읽기), `sample_read_naive()` (섞인 구조체), `main_take_count_naive()` ("읽고 → 0 쓰기" 사이에 tick 유실). 테스트가 이 버그를 먼저 재현해 보여 준다.

### Part A — 재현, 그리고 첫 번째 고침: ISR 은 "신호" 만 보낸다

가장 확실한 해법은 **64비트 값을 아예 공유하지 않는 것** 이다. ISR 은 원자적으로 쓸 수 있는 작은 것만 건드리고, 64비트 시간 `g_main_ts` 는 main 이 혼자 소유한다.

- `int main_poll_flag(void)` — `g_tick_flag` 가 서 있으면 내리고 `g_main_ts += TICK_US`, 1 반환. 아니면 0. (C11 이 비동기 핸들러에 허용하는 유일한 일반 변수 쓰기: `volatile sig_atomic_t` 에 **대입**.)
- `unsigned main_take_count(void)` — 쌓인 tick 수를 **원자 연산 하나로** 가져가며 0으로 만들고 `g_main_ts += n * TICK_US`. 주어진 naive 버전(load → store)이 왜 tick 을 잃는지 테스트가 보여 준다.

### Part B — 임계 구역: `sigprocmask` = PRIMASK

- `irq_save()` / `irq_restore(old)` — SIGALRM 을 막고 **이전 마스크** 를 돌려준다 / 이전 마스크로 **복원** 한다. 중첩 호출에서 안쪽 restore 가 마스크를 풀어 버리면 안 되고, 다른 시그널의 마스크 상태도 보존해야 한다.
- `ts_read_masked()`, `sample_read_masked(out)` — 임계 구역 안에서 읽는다. 마스크 중 도착한 ISR 은 펜딩되었다가 restore 직후 실행된다 → trace `"rH rL IRQ"`.

### Part C — 인터럽트를 막지 않는 재읽기 (SysTick / DWT 64비트 확장 방식)

- `ts_read_reread(&tries)` — `hi → lo → hi` 를 읽고 두 hi 가 다르면 처음부터. Cortex-M 에서 32비트 하드웨어 카운터를 소프트웨어 오버플로 카운트로 64비트로 늘릴 때 쓰는 바로 그 루프다.
- `sample_read_seq(out, &tries)` — seqlock 읽기: 버전 `v1` (홀수면 재시도) → 필드 4개 → 버전 `v2`, 다르면 재시도.

## 2. Problem (English)

> **Interrupt-safe sharing of a 64-bit timestamp and a struct.**
>
> A `SIGALRM` handler models a 1 ms timer ISR on a 32-bit MCU. It advances a 64-bit microsecond timestamp stored as two `volatile uint32_t` halves (`lo += 1000`, carry into `hi`), publishes a new ADC sample `{seq, ch0, ch1, ch2}` (invariant `chK == seq*100+K`) bracketed by a seqlock version counter, sets `volatile sig_atomic_t g_tick_flag = 1`, and does `atomic_fetch_add(&g_tick_count, 1)`. All main-side reads of shared data go through `rd32()`, which can inject the ISR (via `raise`) right after the n-th read.
>
> 1. `int main_poll_flag(void)` / `unsigned main_take_count(void)` — keep a main-owned `uint64_t g_main_ts`; consume the flag (0 or 1 tick) or atomically take-and-zero the counter.
> 2. `irq_state_t irq_save(void)` / `void irq_restore(irq_state_t)` with `irq_state_t = sigset_t`: block SIGALRM returning the previous mask; restore exactly that mask (must nest). Then `uint64_t ts_read_masked(void)` and `void sample_read_masked(sample_t *)`.
> 3. `uint64_t ts_read_reread(int *tries)` (read hi, lo, hi; retry if hi changed) and `void sample_read_seq(sample_t *, int *tries)` (seqlock reader). Interrupts must stay enabled.
>
> Example: timestamp `0x0_FFFFFE0C`, ISR injected after the first read. Naive → `0x0_000001F4` (torn, 71.6 min backwards). Masked → `0x0_FFFFFE0C`, trace `"rH rL IRQ"`. Re-read → `0x1_000001F4`, `tries == 2`, trace `"rH IRQ rL rH rH rL rH"`.
>
> Follow-ups: what does C11 actually allow inside an asynchronous signal handler? Is `LDRD` atomic on Cortex-M4? PRIMASK vs BASEPRI? Why must the re-read loop not be used when the *ISR* is the reader?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "10/33 passed" (버그 재현 테스트만 통과) — TODO 를 채워 33/33 을 만든다
```

`-std=c11` 은 엄격 모드라 POSIX 함수(`sigaction`, `sigprocmask`)의 선언을 숨긴다. 그래서 **모든 `#include` 앞에** `#define _POSIX_C_SOURCE 200809L` 을 둔다 (뒤에 두면 이미 읽은 헤더에는 효과가 없다). 이 맥(SDK 26.4)에서는 `_POSIX_C_SOURCE 200809L` 하나로 `setitimer`·`clock_gettime` 까지 선언된다 (`demo_itimer.c` 로 확인). 다른 libc 에서 `setitimer` 가 안 보이면 `_DARWIN_C_SOURCE`(맥) / `_DEFAULT_SOURCE`(glibc) 를 추가한다.

관찰용 파일 세 개 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_itimer.c -o demo_itimer && ./demo_itimer          # 진짜 setitimer 로 찢어짐 통계
cc -std=c11 -Wall -Wextra -Werror -O2 demo_pending_merge.c -o dpm && ./dpm                   # 막힌 동안 3번 → 핸들러 1번
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -              # 64비트 접근·PRIMASK·BASEPRI asm
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_m4.c -o -           # M0+ 비교
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 플래그는 "먼저 내리고 일한다", 카운터는 exchange</summary>

`if (g_tick_flag) { g_tick_flag = 0; ...일... }` 순서. 일을 먼저 하고 나중에 내리면, 일하는 동안 온 tick 의 플래그까지 지워 버린다. 카운터는 `n = load; store 0` 두 단계 사이가 틈이다 — `<stdatomic.h>` 에 "읽으면서 새 값을 쓰는" RMW 함수 하나가 있다.

</details>

<details>
<summary>힌트 2 — Part B: SIG_BLOCK 의 세 번째 인자</summary>

`sigprocmask(SIG_BLOCK, &set, &old)` 는 "set 을 막으면서 **그 직전 마스크를 old 에 저장**" 을 한 호출로 한다. 복원은 `SIG_UNBLOCK` 이 아니라 `SIG_SETMASK, &old` — 바깥에서 이미 막혀 있었다면 계속 막혀 있어야 하니까. CMSIS 로는 `uint32_t pm = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(pm);`.

</details>

<details>
<summary>힌트 3 — Part C: 무엇이 바뀌었는지 알려 주는 값을 앞뒤로 읽는다</summary>

ISR 은 main 을 선점하지만 main 은 ISR 을 선점하지 못한다 → ISR 의 쓰기는 main 입장에서 "통째로 일어나거나 안 일어나거나" 다. 그러니 **hi 가 앞뒤로 같으면** 그 사이 읽은 lo 는 그 hi 와 짝이다. 구조체는 hi 대신 버전 카운터를 앞뒤로 읽는다. `tries` 는 루프 맨 앞에서 센다.

</details>

<details>
<summary>힌트 4 — 테스트 trace 읽는 법</summary>

`rH rL rV rS r0 r1 r2` 는 각각 hi, lo, 버전, seq, ch0..2 읽기이고 `IRQ` 는 ISR 실행. `"rH IRQ rL rH rH rL rH"` = 1차 시도(hi, [ISR], lo, hi 다름) → 2차 시도(hi, lo, hi 같음). 읽는 순서가 다르면 trace 테스트가 FAIL 한다.

</details>

## 5. 풀이

### 0. 모델: Cortex-M ↔ POSIX 시그널 대응표

| Cortex-M (단일 코어) | 이 레슨의 호스트 모델 | 비고 |
|---|---|---|
| ISR (`SysTick_Handler`) | `SIGALRM` 핸들러 `timer_isr` | 둘 다 메인을 선점, 끝까지 실행된 뒤 복귀 |
| SysTick / TIMx 주기 인터럽트 | `setitimer(ITIMER_REAL, ...)` | `demo_itimer.c` |
| `NVIC_SetPendingIRQ()` / 하드웨어 이벤트 | `raise(SIGALRM)` | 테스트에서 "정확한 지점" 주입 |
| `__disable_irq()` (`CPSID i`, PRIMASK=1) | `sigprocmask(SIG_BLOCK, {SIGALRM})` | |
| `__set_PRIMASK(saved)` | `sigprocmask(SIG_SETMASK, &old)` | 중첩 안전한 복원 |
| BASEPRI (우선순위 이하만 막기) | 일부 시그널만 막기 | |
| NVIC 펜딩 비트 (IRQ 당 1비트) | 펜딩 시그널 (같은 시그널은 합쳐짐) | `demo_pending_merge.c` |
| ISR 우선순위 / 선점 | `sa_mask` (핸들러 실행 중 막을 시그널) | |

`demo_pending_merge.c` 의 실제 출력 — 막혀 있는 동안 3번 와도 핸들러는 1번:

```text
blocked, raised 3x: runs=0
after unblock:      runs=1
```

NVIC 도 IRQ 마다 펜딩 비트가 **1개** 라 똑같다. 임계 구역이 tick 주기보다 길면 tick 을 **잃는다** → 임계 구역은 짧게 (몇 µs 이내가 보통의 기준).

### Part A — 재현: 무엇이 언제 깨지나

테스트 앞부분이 주어진 naive 코드로 세 가지 버그를 재현한다 (starter 에서도 통과하는 10개 중 일부):

| 함수 | ISR 주입 지점 | 결과 | 버그 |
|---|---|---|---|
| `ts_read_naive` | 1번째 읽기(hi) 직후 | `0x0_000001F4` = 500 µs, trace `"rH IRQ rL"` | 찢어진 읽기: 71.6분 뒤로 |
| `ts_read_naive` | 2번째 읽기(lo) 직후 | `0x0_FFFFFE0C` (정상) | 타이밍 운에 따라 멀쩡 → 재현 어려움 |
| `sample_read_naive` | ch0 읽은 직후 | `seq=7, ch0=700, ch1=801, ch2=802` | 섞인 구조체: 두 샘플의 반반 |
| `main_take_count_naive` | load 직후 | 3 을 가져갔는데 count 는 0, ISR 은 4번 | lost update: 4번째 tick 증발 |

마지막 줄이 C-B09 Part C 의 lost update 와 같은 구조다. `atomic_load` 와 `atomic_store` **각각은** 원자적이지만, 둘을 이은 "가져가고 비우기" 는 원자적이지 않다. **원자적 부품을 이어 붙여도 원자적 전체가 되지 않는다** — 이 레슨 전체의 핵심 문장이다. hi/lo 도 각각은 원자적인 32비트 접근이다.

### Part A — 고침 ①: C11 이 핸들러에 보장하는 것만 쓴다

C11 7.14.1.1p5 (요지): 시그널이 `abort`/`raise` 호출의 결과가 **아닌** 경우(= 진짜 비동기), 핸들러가

- lock-free 원자 객체가 아닌 정적/스레드 저장 기간 객체를 **참조**하면 — 단, `volatile sig_atomic_t` 로 선언된 객체에 **값을 대입** 하는 것은 예외 —
- 또는 `abort`, `_Exit`, `quick_exit`, (같은 시그널에 대한) `signal` 이외의 표준 라이브러리 함수를 호출하면

동작이 정의되지 않는다(UB). 5.1.2.3p5 는 반대 방향을 말한다: 핸들러가 실행되는 동안 lock-free 원자 객체나 `volatile sig_atomic_t` 가 아닌 객체의 값은 **미지정(unspecified)** 이고, 핸들러가 그런 객체를 수정하면 핸들러가 끝날 때 그 값은 **불확정(indeterminate)** 이 된다.

| 핸들러 안에서 | 순수 C11 (비동기 시그널) | 이 레슨에서 |
|---|---|---|
| `g_tick_flag = 1;` (`volatile sig_atomic_t` 대입) | 정의됨 | Part A 플래그 |
| `g_tick_flag++` / `if (g_tick_flag)` (읽기) | **UB** — 대입만 허용 | 안 함 |
| `atomic_fetch_add(&cnt, 1)` (lock-free 확인됨) | 정의됨 | Part A 카운터 |
| `g_ts_lo = g_ts_lo + 1000` (일반 volatile) | **UB** | ISR 모델(테스트는 `raise` 라 정의됨) |
| `printf(...)` | **UB** (POSIX 로도 async-signal-safe 아님) | 금지, 아래 별도 절 |

그래서 시작 코드 머리 주석이 "raise() 로 생긴 시그널은 7.14.1.1p5 밖이라 정의된 동작" 이라고 말한다. 체크 테스트가 결정적일 뿐 아니라 **표준적으로도 깨끗한** 이유다. (대신 `raise` 가 일으킨 시그널의 핸들러는 `raise` 를 다시 부르면 안 된다(p4) — 그래서 `rd32` 의 주입은 핸들러 밖에서만 일어난다.) 반면 `demo_itimer.c` 의 진짜 비동기 핸들러가 `volatile uint32_t` 를 갱신하는 것은 엄밀히 표준 밖이고, 파일 머리에 그렇게 적어 두었다. 실제 MCU ISR 은 시그널 핸들러가 아니라 컴파일러 입장에서는 "언제 불릴지 모르는 평범한 함수" 이고, 단일 코어에서는 volatile 접근이 실무적인 도구다.

`sig_atomic_t` 의 한계도 알아 두자. 7.20.3p2: 부호 있는 타입이면 범위가 최소 −127…127, 부호 없는 타입이면 최소 0…255 만 보장된다. 이 맥에서는 `int` 이고 `__SIG_ATOMIC_WIDTH__ 32` (Cortex-M 타깃도 32). "원자적" 이라는 이름은 **한 번의 읽기/쓰기** 가 찢어지지 않는다는 뜻일 뿐, `++` 같은 RMW 는 원자적이지 않다.

**플래그 vs 카운터:**

```c
static int main_poll_flag(void) {
    if (!g_tick_flag) return 0;
    g_tick_flag = 0;                  /* 먼저 내린다 */
    g_main_ts += TICK_US;
    return 1;
}
static unsigned main_take_count(void) {
    unsigned n = atomic_exchange_explicit(&g_tick_count, 0u, memory_order_relaxed);
    g_main_ts += (uint64_t)n * TICK_US;
    return n;
}
```

| 폴링 사이 tick 수 | 플래그 | 원자 카운터 |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 1 | 1 |
| 2 | **1** (합쳐짐) | 2 |

플래그는 "1개 이상 왔다" 만 안다 — 펜딩 비트와 같은 정보량이다. 이벤트 **수** 가 중요하면(타임스탬프, 엔코더 펄스) 카운터가 필요하고, 비동기 핸들러에서 카운트하려면 `sig_atomic_t` 로는 불가능하므로(읽기가 금지) **lock-free 원자** 를 쓴다. 테스트가 `ATOMIC_INT_LOCK_FREE == 2 && atomic_is_lock_free(&g_tick_count)` 를 확인하는 이유다: lock-free 가 아니면 원자 연산이 내부 락을 쓸 수 있고, 메인이 그 락을 잡은 순간 핸들러가 같은 락을 기다리면 **교착(deadlock)** 이다.

`demo_itimer.c` 마지막 줄 — main 이 매 루프 "다른 일" 을 하며 0.5 s 동안 폴링한 실측 (3회):

```text
signal-only 0.5 s: ticks seen by flag=4872, by atomic counter=4916
signal-only 0.5 s: ticks seen by flag=4882, by atomic counter=4919
signal-only 0.5 s: ticks seen by flag=4941, by atomic counter=4958
```

플래그는 약 1% 의 tick 을 합쳐 버렸다. 카운터는 하나도 잃지 않았다.

Cortex-M 에서 `atomic_exchange` 는 무엇이 되나 (`demo_m4.c` ④, 실제 출력):

```text
@ Cortex-M4 (ARMv7E-M)                       @ Cortex-M0+ (ARMv6-M)
.LBB3_1: ldrex r0, [r1]                      bl __atomic_exchange_4      ← 라이브러리 호출
         strex r3, r2, [r1]                  warning: ... exceeds the max lock-free size (0 bytes)
         cmp   r3, #0                              [-Watomic-alignment]
         bne   .LBB3_1
@ 호스트 arm64: swp wzr, w0, [x8]   (ARMv8.1 LSE 원자 교환 1개)
```

| 타깃 (clang `-dM -E`) | `__CLANG_ATOMIC_INT_LOCK_FREE` | `__CLANG_ATOMIC_LLONG_LOCK_FREE` |
|---|---|---|
| arm64 맥 | 2 (항상) | 2 |
| Cortex-M4 | 2 | **1** (때때로 → 실제로는 libcall) |
| Cortex-M0+ | **1** | 1 |

M0+ 에서는 32비트 원자 RMW 도 libcall 이고, 그 함수는 보통 PRIMASK 로 인터럽트를 잠깐 막아 구현한다 — 결국 Part B 다.

### Part B — 임계 구역: 막고, 읽고, **원래대로** 돌린다

```c
static irq_state_t irq_save(void) {
    irq_state_t blk, old;
    sigemptyset(&blk);
    sigaddset(&blk, SIGALRM);
    sigprocmask(SIG_BLOCK, &blk, &old);   /* 막으면서 이전 마스크 저장 */
    return old;
}
static void irq_restore(irq_state_t old) {
    sigprocmask(SIG_SETMASK, &old, NULL); /* 이전 상태 그대로 */
}
```

`ts_read_masked()` 는 이 둘 사이에서 `rd32(hi)`, `rd32(lo)` 를 할 뿐이다. 테스트가 1번째 읽기 직후 `raise` 해도 시그널은 **펜딩** 될 뿐이고, POSIX `sigprocmask` 규정 — "호출 후 막히지 않은 펜딩 시그널이 있으면, 호출이 돌아오기 **전에** 적어도 하나를 전달한다" — 덕분에 restore 안에서 ISR 이 실행된다. 그래서 trace 가 정확히 `"rH rL IRQ"` 다. Cortex-M 도 같다: PRIMASK 를 0 으로 되돌리는 `MSR`/`CPSIE` 직후 펜딩 IRQ 가 바로 들어온다.

**왜 enable 이 아니라 restore 인가 — 중첩:**

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">임계 구역 안에서 임계 구역을 쓰는 함수를 호출할 때 (raise 는 안쪽 구역 중에)</text>
  <text x="10" y="64" fill="currentColor">disable/enable</text>
  <rect x="140" y="44" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="152" y="64" fill="currentColor">DI(바깥)</text>
  <rect x="215" y="44" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="227" y="64" fill="currentColor">DI(안)</text>
  <rect x="290" y="44" width="80" height="30" class="f-acc" stroke="currentColor"/><text x="300" y="64" fill="currentColor">EI(안)</text>
  <rect x="375" y="44" width="70" height="30" class="f-warn" stroke="currentColor"/><text x="387" y="64" fill="currentColor">IRQ!</text>
  <rect x="450" y="44" width="170" height="30" class="f-warn" stroke="currentColor"/><text x="460" y="64" fill="currentColor">바깥 구역 나머지</text>
  <text x="630" y="64" fill="currentColor">보호 깨짐</text>
  <text x="10" y="134" fill="currentColor">save/restore</text>
  <rect x="140" y="114" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="148" y="134" fill="currentColor">save→0</text>
  <rect x="215" y="114" width="70" height="30" class="f-mut" stroke="currentColor"/><text x="223" y="134" fill="currentColor">save→1</text>
  <rect x="290" y="114" width="80" height="30" class="f-acc" stroke="currentColor"/><text x="296" y="134" fill="currentColor">restore 1</text>
  <rect x="375" y="114" width="170" height="30" class="f-ok" stroke="currentColor"/><text x="385" y="134" fill="currentColor">바깥 구역 나머지</text>
  <rect x="550" y="114" width="80" height="30" class="f-acc" stroke="currentColor"/><text x="556" y="134" fill="currentColor">restore 0</text>
  <rect x="635" y="114" width="60" height="30" class="f-ok" stroke="currentColor"/><text x="645" y="134" fill="currentColor">IRQ</text>
  <g fill="currentColor">
    <text x="10" y="180">위: 안쪽 함수의 enable 이 바깥 함수의 보호까지 풀어 버린다. ISR 이 바깥 구역 한가운데로 끼어든다.</text>
    <text x="10" y="202">아래: 각 구역이 "들어올 때 상태" 를 기억했다가 그대로 돌려놓는다. 0 으로 돌아가는 건 바깥 restore 뿐.</text>
    <text x="10" y="232" font-family="monospace">uint32_t pm = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(pm);</text>
  </g>
</svg>
```

테스트 4개가 이걸 확인한다: 안쪽 restore 후에도 `sigalrm_blocked()` 이고 `g_isr_runs == 0`, 바깥 restore 에서야 ISR 1회; 그리고 미리 막혀 있던 SIGUSR1 이 `irq_restore(irq_save())` 뒤에도 막혀 있어야 한다 (`SIG_UNBLOCK` 으로 "SIGALRM 만 푸는" 구현은 중첩 테스트에서, `SIG_SETMASK` 에 빈 집합을 넣는 구현은 SIGUSR1 테스트에서 걸린다). 같은 문제를 RAII 가드로 푸는 C++ 버전은 CPP-B06, 카운터 방식 중첩은 추가 연습 3.

**비용:** `demo_itimer.c` 실측 (Apple Silicon, Apple clang 21, 3회 — 각 읽기에 `GAP` 지연 포함):

```text
naive    reads=2000000  ISR=244   backwards(torn)=59  retries=0   12.2 ns/read
masked   reads=2000000  ISR=7950  backwards(torn)=0   retries=0  416.6 ns/read
reread   reads=2000000  ISR=172   backwards(torn)=0   retries=37   8.8 ns/read

naive    reads=2000000  ISR=242   backwards(torn)=56  retries=0   12.4 ns/read
masked   reads=2000000  ISR=8342  backwards(torn)=0   retries=0  432.2 ns/read
reread   reads=2000000  ISR=160   backwards(torn)=0   retries=35   8.0 ns/read

naive    reads=2000000  ISR=253   backwards(torn)=59  retries=0   12.7 ns/read
masked   reads=2000000  ISR=7105  backwards(torn)=0   retries=0  356.1 ns/read
reread   reads=2000000  ISR=146   backwards(torn)=0   retries=30   7.3 ns/read
```

읽는 법:

- ISR 은 `lo += 2^30` 이라 4 tick 에 한 번 자리올림한다. naive 는 244 tick ≈ 61 번의 자리올림 중 **59번이 찢어진 읽기** 로 관찰됐다 — 이 데모는 `GAP` 지연으로 hi 와 lo 사이 창을 일부러 넓혀서 루프 시간 대부분이 "위험 구간" 이다. 실제 펌웨어에서는 창이 1~2 명령이라 훨씬 드물다 → 그래서 더 위험하다 (며칠에 한 번).
- masked 는 찢어짐 0 이지만 읽기마다 `sigprocmask` 시스템 콜 2번이라 **30배 이상 느리다** (그래서 같은 200만 번이 더 오래 걸려 ISR 수가 많다). 호스트의 이 비용은 MCU 와 전혀 다르다: Cortex-M 의 `CPSID i` / `MSR PRIMASK` 는 명령어 1개씩, 몇 사이클이다. MCU 에서 임계 구역의 진짜 비용은 **인터럽트 지연(latency)** 이다.
- reread 는 찢어짐 0, 재시도 약 35번 — 자리올림 ISR 이 hi 와 hi 사이에 들어온 횟수다.

**Cortex-M 의 실제 명령어** (`demo_m4.c` ⑤⑥, Cortex-M4 실제 출력):

```text
read_v64_crit:                         read_v64_basepri:
    movw  r0, :lower16:v_ts                movs  r0, #64
    mrs   r2, primask      ; 저장           mrs   r2, basepri       ; 저장
    cpsid i                ; 모두 막기      msr   basepri_max, r0   ; 우선순위값 ≥ 0x40 만 막기
    movt  r0, :upper16:v_ts                movw/movt r0, v_ts
    ldrd  r0, r1, [r0]                     ldrd  r0, r1, [r0]
    msr   primask, r2      ; 복원           msr   basepri, r2       ; 복원
    bx    lr                               bx    lr
```

`movt` 가 `cpsid` 뒤로 옮겨진 것에 주목: 주소 계산은 메모리 접근이 아니라 `"memory"` 클로버와 무관하게 자유롭게 움직인다. 메모리 접근(`ldrd`)은 클로버 덕분에 두 asm 사이에 머문다. CMSIS 의 `__disable_irq()` 등이 바로 이 `"memory"` 클로버를 가진 inline asm 이다 — 없으면 컴파일러가 공유 변수 읽기를 임계 구역 밖으로 옮길 수 있다.

| | PRIMASK (`CPSID i`) | BASEPRI |
|---|---|---|
| 막는 범위 | 우선순위 설정 가능한 모든 예외 (NMI, HardFault 제외) | 우선순위 **값** ≥ BASEPRI 인 것만 (값이 작을수록 높은 우선순위) |
| 높은 우선순위 ISR (모터 제어 등) | 같이 막힘 → 지터 | 계속 돈다 |
| 지원 | 모든 Cortex-M | ARMv7-M / ARMv8-M Mainline (M3/M4/M7/M33). **M0/M0+ 없음** |
| 대표 사용처 | 짧은 공용 임계 구역 | FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` — 그보다 높은 ISR 은 RTOS 임계 구역 중에도 실행 (대신 RTOS API 호출 금지) |
| 중첩 편의 | 저장/복원 | `MSR BASEPRI_MAX` 는 마스크를 **올리는** 방향으로만 써진다 |

(Cortex-M7 r0p1 에는 BASEPRI 쓰기가 바로 효과를 내지 않는 에라타가 있어 FreeRTOS 의 해당 포트는 `cpsid i` / `msr basepri` / `cpsie i` 로 감싼다. 칩의 리비전을 확인할 것.) 상세 구현은 ARM-M10.

### Part C — 인터럽트를 막지 않는 재읽기

```c
do {
    ++*tries;
    hi = rd32(&g_ts_hi);
    lo = rd32(&g_ts_lo);
} while (rd32(&g_ts_hi) != hi);
```

정확성 논증: ISR 은 main 을 선점하고 **끝까지 실행된 뒤** 돌아온다. 그러니 main 이 보기에 ISR 의 hi/lo 쓰기는 한꺼번에 일어난다. 세 읽기 사이 어디에 ISR 이 들어와도:

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">hi1 → lo → hi2 : ISR 이 어디에 들어오든 "hi1 == hi2 이면 lo 와 짝이 맞다"</text>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="60">경우 1  rH IRQ rL rH</text>
    <text x="10" y="120">경우 2  rH rL IRQ rH</text>
    <text x="10" y="180">경우 3  rH IRQ rL rH</text>
  </g>
  <rect x="215" y="42" width="515" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="225" y="62" fill="currentColor">자리올림 O: hi1=0, lo=새 0x1F4, hi2=1 → 재시도 → 0x1_000001F4</text>
  <rect x="215" y="102" width="515" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="225" y="122" fill="currentColor">자리올림 O: hi1=0, lo=옛 0xFFFFFE0C, hi2=1 → 재시도 → 새 값</text>
  <rect x="215" y="162" width="515" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="225" y="182" fill="currentColor">자리올림 X: hi1 == hi2, lo=새 6000 → 통과 → 새 값 6000</text>
  <text x="10" y="224" fill="currentColor">hi 가 안 바뀌었다면 ISR 이 lo 만 바꿨거나 아예 안 왔다 → (hi, lo) 는 ISR 전이든 후든 실제 존재한 값.</text>
  <text x="10" y="248" fill="currentColor">재시도는 거의 항상 1번이면 끝난다: 다음 자리올림은 2^32 µs(71분) 뒤. tries 상한을 두면 더 안전하다.</text>
</svg>
```

테스트 trace 가 그대로 이 표다: `"rH IRQ rL rH rH rL rH"`, `"rH rL IRQ rH rH rL rH"`, `"rH IRQ rL rH"` (tries 1, 값 6000).

seqlock 은 hi 대신 **버전 카운터** 로 같은 일을 한다. ISR 이 `ver++` (홀수) → 필드 쓰기 → `ver++` (짝수). 리더는 `v1` (홀수면 쓰는 중) → 복사 → `v2`, `v1 == v2` 면 일관된 스냅샷이다. 테스트: ch0 직후 ISR → `"rV rS r0 IRQ r1 r2 rV rV rS r0 r1 r2 rV"`, 2번째 시도에서 seq 8 의 온전한 샘플. 단일 코어에서 ISR 이 writer 면 리더는 홀수 버전을 **볼 수 없다** (ISR 은 끝까지 실행되니까). 홀수 검사는 writer 가 선점될 수 있을 때(RTOS 태스크 writer, 멀티코어)를 위한 것이고, 그때는 volatile 로 부족하고 acquire/release 순서가 필요하다 (LLAT-M02, Linux `seqlock_t`).

**재읽기를 쓰면 안 되는 두 경우** — 면접 꼬리질문 단골:

| 상황 | 왜 깨지나 | 대신 |
|---|---|---|
| 리더가 **ISR**, writer 가 main | ISR 은 main 이 쓰기를 끝내길 기다릴 수 없다 (main 은 ISR 이 끝나야 진행) → 재시도해도 같은 반쯤 쓴 값 → 무한 루프 | main 쪽 쓰기를 임계 구역으로, 또는 더블 버퍼 + 인덱스 1개 전환 (추가 연습 2) |
| **인터럽트가 막힌 상태** 에서 호출 (다른 임계 구역 안) | 32비트 하드웨어 카운터가 넘쳤는데 오버플로 ISR 이 못 돌아 hi 가 그대로 → hi1 == hi2 인데 lo 는 이미 넘친 작은 값 → 71분 뒤로 | 오버플로 펜딩 플래그(TIMx_SR.UIF, SysTick COUNTFLAG)를 보고 보정 (추가 연습 1) |

Cortex-M 실전 형태 — DWT 사이클 카운터(32비트, 168 MHz 면 약 25.6 s 마다 넘침)나 32비트 타이머를 64비트로 확장:

```c
uint64_t now64(void) {            /* g_ovf_hi 는 오버플로 ISR 이 ++ */
    uint32_t hi, lo;
    do { hi = g_ovf_hi; lo = TIM2->CNT; } while (hi != g_ovf_hi);
    return ((uint64_t)hi << 32) | lo;
}
```

(SysTick 은 다운카운터에 24비트라 `lo` 계산이 조금 다르다 — BM-B13 참고.)

### 64비트 접근은 Cortex-M 에서 원자적인가 — `demo_m4.c` ①②③

```text
@ Cortex-M4                          @ Cortex-M0+                       ; 호스트 arm64
read_v64:  ldrd r0, r1, [r0]         read_v64: ldr r1, [r0, #4]  ; hi    _read_v64: ldr x0, [x8]
                                               ldr r0, [r0]      ; lo
isr_v64:   ldrd r1, r2, [r0]         isr_v64:  ... str r2, [r0]      ; lo    _isr_v64: ldr x9 / add / str x9
           adds.w / adc                        adcs ... str r4, [r0, #4] ; hi
           strd r1, r2, [r0]
read_a64:  bl __atomic_load          read_a64: bl __atomic_load          _read_a64: ldar x0, [x8]
```

- M0+ 는 **LDR 두 개** — 이 레슨의 `ts_read_naive` 와 똑같이 hi 를 먼저 읽는다. 찢어짐 그대로.
- M4 는 `LDRD` 한 개라 "한 명령이니 안전하겠지" 싶지만 **아니다.** ARMv7-M 아키텍처(ARM DDI0403, A3.5.3)는 단일 복사 원자성(single-copy atomicity)을 바이트, 정렬된 하프워드, 정렬된 워드 접근에만 보장하고, `LDRD`/`STRD`/`LDM`/`STM` 은 **워드 접근의 연속** 으로 정의해 그 묶음의 원자성은 보장하지 않는다. 다중 워드 로드/스토어는 인터럽트가 오면 중간에 버렸다가 복귀 후 다시 실행될 수 있다 — `STRD` 가 첫 워드만 쓰고 버려지면 ISR 이 반쯤 쓴 값을 본다. 그리고 컴파일러가 `LDRD` 를 낸다는 보장도 애초에 없다 (최적화 수준·정렬·컴파일러에 따라 LDR 2개).
- `_Atomic uint64_t` 는 M4/M0+ 모두 `__atomic_load` 라이브러리 호출. ARMv7-M 에는 64비트 배타적 접근(`LDREXD`)이 없으므로(ARMv7-A/R 에만 있음) 이 함수는 보통 인터럽트를 막아 구현된다. 그 구현은 툴체인 런타임이 제공해야 하고, 없으면 링크 에러 — 64비트 공유는 Part B/C 처럼 **직접** 처리하는 편이 투명하다.
- arm64 에서는 정렬된 64비트 `ldr x` 가 단일 복사 원자적이다. 그래서 이 레슨은 hi/lo 로 쪼개 32비트 MCU 를 흉내 낸 것이다.

**LDREX/STREX 와 ISR:** `LDREX` 가 주소를 "배타적 모니터" 에 등록하고, `STREX` 는 그 사이 모니터가 지워졌으면 쓰지 않고 1 을 돌려준다 (→ `bne` 재시도). ARMv7-M 은 예외 진입/복귀 때 로컬 모니터를 클리어하므로 ISR 이 끼어든 뒤의 `STREX` 는 반드시 실패한다 — 인터럽트를 막지 않고도 RMW 가 원자적이 되는 원리다 (ARM-M04 는 AArch64 판). 32비트까지만 되고, ARMv6-M 에는 없다.

### async-signal-safe: 핸들러(=ISR) 안에서 하면 안 되는 일

| 하지 말 것 | 왜 | 대신 |
|---|---|---|
| `printf`, `malloc`, `free` | 내부 락/공유 버퍼. main 이 `printf` 안에서 stdout 락을 잡은 순간 핸들러가 `printf` → 같은 락 대기 → **교착**, 또는 버퍼 오염 | 플래그/카운터/링 버퍼로 넘기고 main 이 출력. 꼭 써야 하면 `write(2)` (추가 연습 4) |
| `strtok`, `rand` 등 내부 정적 상태 함수 | 재진입(reentrant) 불가 | 상태를 인자로 받는 버전 |
| 블로킹 (`sleep`, 뮤텍스 lock, UART 폴링 송신) | ISR 이 멈추면 시스템 전체가 멈춤 | 이벤트만 기록 |
| `errno` 를 바꾸는 호출 후 복구 안 함 | main 이 보던 `errno` 가 바뀜 | 핸들러 시작에 저장, 끝에 복구 |

POSIX 는 핸들러에서 불러도 되는 **async-signal-safe 함수 목록** 을 정해 둔다 (`write`, `_exit`, `sigprocmask`, `sigaction`, `kill`, `raise` 등 — `printf` 계열은 없다). 펌웨어도 같은 규칙이다: newlib 의 `printf` 는 재진입 지원 없이 빌드되는 경우가 많고, ISR 에서 부르면 스택도 터지기 쉽다. 시그널 처리 전반(SA_RESTART, self-pipe)은 OS-B07.

### 어느 방법을 언제 쓰나

| 방법 | 인터럽트 지연 | 리더 쪽 비용 | 쓸 수 있는 조건 | 대표 예 |
|---|---|---|---|---|
| ISR 은 플래그/원자 카운터만 (A) | 없음 | 아주 작음 | 일을 main 으로 미룰 수 있을 때 | 이벤트 통지, tick 카운트 |
| 임계 구역 (B) | 구역 길이만큼 증가 | 마스크 2번 | 어디서나. 짧게만 | 링 버퍼 인덱스, 구조체 복사, 리더가 ISR 일 때의 main writer |
| 재읽기 / seqlock (C) | 없음 | 가끔 재시도 | 리더가 선점 **당하는** 쪽, writer 가 끝까지 실행, 인터럽트 켜진 상태 | 64비트 시간, 센서 스냅샷 |
| 하드웨어 원자 (LDREX/STREX, `_Atomic` lock-free) | 없음 | RMW 1개 | 32비트 이하, M3 이상 | 카운터, 비트 플래그 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 33/33 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 33/33, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `volatile uint64_t g_ts;` 로 끝 | volatile 은 접근을 지울 뿐 쪼개진 접근을 묶어 주지 않는다. M0+ 는 LDR 2개, M4 의 LDRD 도 원자 보장 없음 | 임계 구역 또는 hi-lo-hi 재읽기 |
| `__disable_irq(); ... __enable_irq();` 를 라이브러리 함수 안에서 | 호출자가 이미 막아 둔 상태였다면 enable 이 바깥 보호를 깨뜨린다 (Part B 중첩 테스트) | `pm = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(pm);` |
| `irq_restore` 를 `SIG_UNBLOCK` 으로 | 위와 같은 버그의 시그널 판 | `sigprocmask(SIG_SETMASK, &old, NULL)` |
| 플래그를 일 **끝난 뒤** 내림: `if (f) { work(); f = 0; }` | work 중 도착한 이벤트의 플래그를 지워 버림 | `if (f) { f = 0; work(); }` |
| `n = cnt; cnt = 0;` 으로 카운터 가져가기 (각각 원자적이어도) | 두 연산 사이 ISR 의 +1 이 0 에 덮임 (naive 테스트: 4번째 tick 증발) | `atomic_exchange(&cnt, 0)` 또는 임계 구역 |
| 핸들러에서 `volatile sig_atomic_t` 를 `++` | RMW 는 원자적이지 않고, 비동기 핸들러가 그 객체를 **읽는** 것 자체가 7.14.1.1p5 위반 | lock-free `atomic_fetch_add` |
| ISR 이 리더인데 재읽기 루프 사용 | main writer 는 ISR 이 끝나야 진행 → 영원히 반쯤 쓴 값 | main writer 를 임계 구역/더블 버퍼로 |
| 인터럽트를 막은 채 `now64()` (재읽기 버전) 호출 | 오버플로 ISR 이 못 돌아 hi 가 낡음 → 71분 뒤로 | 펜딩 플래그 확인 + lo 재읽기 (연습 1) |
| 임계 구역 안에서 UART 로 로그 출력 | 구역이 ms 단위로 길어져 tick 펜딩이 합쳐지고(유실) 다른 ISR 지연 | 구역 안에서는 복사만, 출력은 밖에서 |
| 핸들러 안 `printf` | async-signal-safe 아님 → 교착/버퍼 오염 | 값만 넘기기, 또는 `write(2)` |
| `#define _POSIX_C_SOURCE` 를 `#include <signal.h>` 뒤에 | 이미 선언이 숨겨진 채 읽힘 → `sigaction` 미선언 에러 | 파일 맨 위, 모든 include 앞 |

## 8. 면접 Q&A

<details>
<summary>Q1. ISR 이 갱신하는 uint64_t 타임스탬프를 메인에서 읽습니다. volatile 붙이면 충분한가요?</summary>

아니다. volatile 은 "매번 메모리에서 읽어라" 만 보장하고, 32비트 CPU 에서 64비트 읽기는 접근 두 개다 (M0+ 실측: `ldr` 2개, M4: `ldrd` 지만 아키텍처상 64비트 원자 보장 없음). 사이에 자리올림 ISR 이 오면 옛 hi + 새 lo → 2^32 단위로 시간이 튄다. 고침: ① 짧은 임계 구역(PRIMASK 저장/복원) ② hi-lo-hi 재읽기 ③ ISR 은 tick 만 세고 64비트 값은 main 이 소유. 꼬리: "64비트 CPU 면?" → 정렬된 64비트 접근이 원자적인 아키텍처(arm64 `ldr x`)에선 찢어지지 않지만, 구조체(여러 워드)는 여전히 같은 문제다.

</details>

<details>
<summary>Q2. sig_atomic_t 는 정확히 무엇을 보장하나요? volatile sig_atomic_t counter++ 는 안전한가요?</summary>

`sig_atomic_t` 는 "시그널이 끼어들어도 **한 번의 접근** 이 찢어지지 않는" 정수 타입이다 (7.14p2). 범위는 최소 −127…127 또는 0…255 만 보장 (7.20.3). C11 7.14.1.1p5 는 비동기 핸들러에 정적 객체 접근으로 **`volatile sig_atomic_t` 에 대입하는 것만** 허용하고(+ lock-free 원자 객체), 읽기·`++` 는 허용 목록 밖이다. `++` 는 어차피 읽기-수정-쓰기라 원자적이지도 않다. 카운트가 필요하면 `atomic_is_lock_free` 가 참인 `atomic_uint` 에 `atomic_fetch_add`. 꼬리: "왜 volatile 이 필요?" → 5.1.2.3p5: volatile sig_atomic_t 가 아니면 핸들러 실행 중/후의 값이 미지정·불확정이다. 또 volatile 없으면 메인의 폴링 루프가 최적화로 사라진다 (C-B09).

</details>

<details>
<summary>Q3. 임계 구역을 __disable_irq() / __enable_irq() 로 감싸면 뭐가 문제죠?</summary>

중첩. 이미 인터럽트를 막고 있던 호출자(다른 임계 구역, HardFault 처리, 부트 코드) 안에서 이 함수를 부르면 마지막 `__enable_irq()` 가 호출자의 보호까지 풀어 버린다. 정석은 `uint32_t pm = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(pm);`. RTOS 는 카운터 방식(`uxCriticalNesting`)을 쓴다 (추가 연습 3). 꼬리: "펜딩된 인터럽트는?" → PRIMASK 를 0 으로 되돌리는 순간 바로 진입한다. 같은 IRQ 가 여러 번 왔어도 펜딩 비트는 1개라 한 번만 — 구역이 길면 이벤트를 잃는다.

</details>

<details>
<summary>Q4. PRIMASK 와 BASEPRI 의 차이는? 언제 BASEPRI 를 쓰나요?</summary>

PRIMASK=1 은 NMI·HardFault 를 뺀 모든 예외를 막는다. BASEPRI=N 은 우선순위 값이 N 이상(=덜 급한)인 것만 막고, 더 급한 ISR(모터 전류 루프, 안전 정지)은 계속 돌게 한다. ARMv7-M 이상(M3/M4/M7/M33)에만 있고 M0/M0+ 에는 없다. FreeRTOS Cortex-M3/M4 포트는 BASEPRI 로 임계 구역을 만들고, `configMAX_SYSCALL_INTERRUPT_PRIORITY` 보다 높은 ISR 은 RTOS 임계 구역 중에도 실행된다 — 대신 그 ISR 에선 FreeRTOS API 를 부르면 안 된다. 주의: STM32 처럼 우선순위 비트가 4개면 값이 상위 비트에 들어가므로 우선순위 4 → `0x40`.

</details>

<details>
<summary>Q5. LDREX/STREX 는 인터럽트 상대로 어떻게 원자성을 보장하나요? 64비트도 되나요?</summary>

`LDREX` 가 로컬 배타적 모니터를 "이 주소 독점" 상태로 두고, `STREX` 는 모니터가 여전히 그 상태일 때만 쓰고 0 을 돌려준다. ARMv7-M 은 예외 진입/복귀 때 로컬 모니터를 클리어하므로, 사이에 ISR 이 오면 `STREX` 가 실패해 루프가 다시 돈다 (실측 `take_a32`: `ldrex/strex/cmp/bne`). 인터럽트 지연을 늘리지 않는 게 장점. 64비트는 안 된다: `LDREXD/STREXD` 는 ARMv7-A/R 에만 있고 ARMv7-M 에는 없어서, `_Atomic uint64_t` 가 `__atomic_load` 라이브러리 호출이 된다 (실측). M0/M0+ 는 32비트 배타 명령도 없다.

</details>

<details>
<summary>Q6. ISR 이나 시그널 핸들러에서 printf 로 디버그 로그를 찍으면 왜 안 되나요?</summary>

`printf` 는 async-signal-safe 가 아니다. 내부에서 stdout 락과 공유 버퍼를 쓰므로, 메인이 `printf` 도중(락 보유) 핸들러가 `printf` 를 부르면 같은 락을 기다리며 교착하거나 버퍼가 오염된다. 펌웨어에선 거기에 더해 UART 송신이 블로킹이라 ISR 이 ms 단위로 길어지고, newlib `printf` 는 스택을 수백 바이트~수 KB 쓴다. 대안: ISR 은 값/이벤트를 링 버퍼나 카운터에 넣고 메인(또는 저우선 태스크)이 출력. 호스트에서 꼭 핸들러에서 써야 한다면 `write(2)` + 직접 만든 정수→문자열 변환 (추가 연습 4). 디버깅엔 GPIO 토글 + 로직 분석기, SEGGER RTT, ITM 이 흔하다.

</details>

<details>
<summary>Q7. hi-lo-hi 재읽기 루프가 깨지는 상황을 두 가지 말해 보세요.</summary>

① **리더가 ISR** 이고 writer 가 main 일 때: ISR 은 main 이 쓰기를 끝낼 때까지 기다릴 수 없다(main 은 ISR 이 끝나야 진행) → 재시도해도 같은 값 → 무한 루프. 이 방향은 main 쪽 쓰기를 임계 구역으로 감싸거나 더블 버퍼 + 인덱스 전환. ② **인터럽트가 막힌 채 호출** 될 때: 하드웨어 카운터는 넘쳤는데 오버플로 ISR 이 못 돌아 hi 가 갱신되지 않으므로 hi1 == hi2 로 통과하고 lo 만 작아져 시간이 뒤로 간다. 펜딩 플래그(TIMx_SR.UIF 등)를 보고 hi+1, lo 재읽기로 보정한다 (추가 연습 1). 덤: ③ 멀티코어에선 volatile 만으로 순서가 보장되지 않아 acquire/release 가 필요.

</details>

<details>
<summary>Q8. 듀얼 코어 MCU (STM32H7 M7+M4, RP2040) 에서도 인터럽트를 막으면 공유 데이터가 안전한가요?</summary>

아니다. PRIMASK/BASEPRI 는 **자기 코어** 의 인터럽트만 막는다. 다른 코어는 그 순간에도 같은 메모리에 접근한다. 필요한 것: 하드웨어 세마포어(STM32H7 HSEM, RP2040 SIO 스핀락) 또는 LDREX/STREX 기반 스핀락 + 메모리 배리어(`DMB`), 그리고 M7 쪽 D-cache 와 공유 메모리 일관성(캐시 끄기/MPU non-cacheable, 또는 clean/invalidate — C-B09 Q6). 실무 패턴은 "임계 구역(자기 코어의 ISR 차단) + 스핀락(다른 코어 차단)" 조합 — Linux 의 `spin_lock_irqsave` 와 같은 구조다.

</details>

## 9. 추가 연습

### 연습 1 — 인터럽트가 막힌 채 부르는 now64()

32비트 하드웨어 카운터 `CNT()` (읽을 때마다 시간이 흐름)와 오버플로 펜딩 플래그 `hw_ovf_pending` 이 있다. 오버플로 ISR(평소엔 `g_ovf_hi++` 후 플래그 클리어)은 지금 **막혀 있다**. 이 상태에서 올바른 64비트 시각을 돌려주는 `now64_masked()` 를 작성하라. "펜딩이면 hi+1" 만 하는 naive 버전은 어떤 경우에 틀리나?

<details>
<summary>답 보기</summary>

```c
uint32_t hi = g_ovf_hi, lo = CNT();
if (hw_ovf_pending) { lo = CNT(); hi++; }
```

펜딩 플래그를 본 시점이 lo 를 읽기 전인지 후인지 모른다. lo 를 `0xFFFFFFF0` 으로 읽은 **직후** 넘쳤다면 플래그가 서 있으니 naive 는 `hi+1 : 0xFFFFFFF0` → 2^32 앞으로 점프 (테스트로 재현). 플래그를 본 **뒤** lo 를 다시 읽으면 그 lo 는 반드시 넘친 이후 값이므로 `hi+1` 과 짝이 맞는다. 이 함수는 인터럽트가 막힌 상태를 전제로 하므로(그래야 ISR 이 그 사이 hi 를 올리고 플래그를 지우지 않는다) 보통 `irq_save()` 로 감싼 범용 `now64()` 의 내부로 쓴다. 대부분의 RTOS/HAL 의 고해상도 타임베이스 코드가 이 구조다.

</details>

### 연습 2 — main → ISR 방향: 더블 버퍼

main 이 64비트 알람 마감 시각을 바꾸고, ISR 이 매 tick 그것을 읽는다. ISR 은 재시도할 수 없다(Q7 ①). 인터럽트를 막지 않고 ISR 이 항상 일관된 값을 보게 하는 `cfg_publish(d)` 를 작성하라. 비교용 단일 버퍼 버전은 어떤 값을 보여 주나?

<details>
<summary>답 보기</summary>

버퍼 두 개 + `volatile sig_atomic_t g_active`. main 은 ISR 이 **안 보는** 쪽(`!g_active`)에 hi, lo 를 다 쓴 뒤 `g_active = next;` **대입 한 번** 으로 전환한다. ISR 은 `g_active` 를 한 번 읽고 그 버퍼만 본다. 쓰는 도중 ISR 이 오면 옛 버퍼의 일관된 옛 값을 본다. 단일 버퍼는 hi 만 쓴 직후 ISR 이 오면 `0x1_FFFFFFFF` (새 hi + 옛 lo) 를 본다. 조건: ISR 이 버퍼를 읽는 중에 main 이 같은 버퍼를 다시 쓰지 않아야 한다 — 단일 코어에선 ISR 이 끝까지 실행되므로 자동으로 성립. (ISR 쪽의 `g_active` 읽기는 비동기 핸들러의 "읽기" 라 순수 C11 로는 lock-free `atomic_int` 가 정석이다. 테스트는 `raise` 라 정의됨.)

</details>

### 연습 3 — 중첩 카운터 방식 임계 구역

FreeRTOS `taskENTER_CRITICAL()` 처럼 깊이 카운터로 중첩을 처리하는 `crit_enter()` / `crit_exit()` 를 작성하라. 바깥 exit 에서만 인터럽트가 풀려야 한다.

<details>
<summary>답 보기</summary>

`if (depth++ == 0) block;` / `if (--depth == 0) unblock;`. 저장/복원 방식과 달리 상태를 호출자가 들고 다닐 필요가 없어 매크로/API 가 간단하다. 단점: 카운터는 "이 컨텍스트 전용" 이어야 한다 — FreeRTOS 는 태스크마다(또는 코어마다) 둔다. ISR 안에서 쓰면 안 되는 이유이기도 하다 (ISR 용 `taskENTER_CRITICAL_FROM_ISR()` 는 저장/복원 방식을 쓴다). 그리고 처음 진입 시 인터럽트가 이미 꺼져 있었다면 바깥 exit 가 켜 버린다 — 부트 코드처럼 그런 호출자가 있으면 저장/복원 방식이 맞다.

</details>

### 연습 4 — async-signal-safe 로깅

핸들러에서 `"T=0x0000001F\n"` 형태로 값을 출력하라. `snprintf` / `printf` 를 쓰지 말고 직접 16진수로 바꾼 뒤 `write(2)` 로 보낸다. 테스트는 파이프로 받아 비교한다.

<details>
<summary>답 보기</summary>

`fmt_hex32` 는 상위 니블부터 `hx[(v >> (28 - 4*i)) & 0xF]` 로 8자리를 채운다 — 라이브러리 함수도, 정적 상태 변경도 없어 재진입 가능하다. `write` 는 POSIX async-signal-safe 목록에 있고 `printf` 는 없다. 펌웨어 판은 "ISR 은 링 버퍼에 바이트를 넣고 UART TX 인터럽트/메인이 뺀다" — 출력 자체를 ISR 밖으로 미룬다. (`write` 가 `errno` 를 바꿀 수 있으므로 실전 핸들러는 `errno` 를 저장·복구한다.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- **원자적 부품을 이어 붙여도 원자적 전체가 아니다**: hi/lo 각각, load/store 각각이 원자적이어도 그 조합은 찢어지거나(torn read) 갱신을 잃는다(lost update).
- 32비트 MCU 의 64비트 값은 접근 2개 — M0+ 는 `LDR` 2개, M4 의 `LDRD` 도 아키텍처상 64비트 원자 보장 없음. `volatile` 은 이걸 고치지 못한다.
- C11 이 비동기 핸들러에 보장하는 것: `volatile sig_atomic_t` 에 **대입**, lock-free 원자 객체. 그래서 ① "ISR 은 신호만, 큰 값은 main 소유" 가 가장 깨끗하다. 수가 중요하면 플래그 대신 `atomic_exchange` 카운터.
- ② 임계 구역은 **저장/복원**: `sigprocmask(SIG_BLOCK,&s,&old)` … `SIG_SETMASK,&old` = `__get_PRIMASK/__disable_irq/__set_PRIMASK`. 펜딩 ISR 은 복원 직후 실행, 같은 IRQ 는 합쳐지므로 짧게.
- ③ 재읽기(hi-lo-hi, seqlock)는 인터럽트 지연 0 이지만 **리더가 선점당하는 쪽** 이고 **인터럽트가 켜져 있을 때만**. ISR 이 리더면 더블 버퍼/임계 구역, 마스크 중이면 펜딩 플래그 보정.
- PRIMASK = 전부(NMI/HardFault 제외), BASEPRI = 우선순위 값 ≥ N 만 (M3 이상). LDREX/STREX 는 예외가 모니터를 지워 STREX 실패 → 재시도. 64비트 배타 명령은 ARMv7-M 에 없다.
- 핸들러/ISR 에서 `printf`·`malloc`·블로킹 금지 (async-signal-safe). 값을 넘기고 출력은 밖에서.

## 참고자료

- C11 (N1570): 7.14.1.1 The signal function (p4 raise 재호출 금지, p5 비동기 핸들러 제약), 5.1.2.3p5 핸들러 중 객체 값, 7.14p2 `sig_atomic_t`, 7.20.3 `SIG_ATOMIC_MIN/MAX`, 7.17.1·7.17.5 lock-free 매크로와 `atomic_is_lock_free`, 7.17.7.3 `atomic_exchange` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- POSIX.1-2017 `sigprocmask` (펜딩 시그널은 반환 전에 전달), 2.4.3 Signal Actions (async-signal-safe 함수 목록) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigprocmask.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03>
- ARMv7-M Architecture Reference Manual (ARM DDI0403E) — A3.5.3 단일 복사 원자성, A3.4 배타적 접근(LDREX/STREX, 모니터), B1.5 예외 모델 — <https://developer.arm.com/documentation/ddi0403/latest/>
- CMSIS-Core: `__get_PRIMASK`, `__disable_irq`, `__set_BASEPRI_MAX` — <https://arm-software.github.io/CMSIS_5/Core/html/group__Core__Register__gr.html>
- FreeRTOS — Running the RTOS on an ARM Cortex-M Core (BASEPRI, `configMAX_SYSCALL_INTERRUPT_PRIORITY`) — <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- Linux kernel docs — Sequence counters and sequential locks — <https://docs.kernel.org/locking/seqlock.html>
- 관련 레슨: C-B09 (volatile 의 한계, lost update), C-B01 (BSRR 와 RMW), OS-B07 (시그널·self-pipe), ARM-M10 (PRIMASK/BASEPRI 구현), ARM-M04 (LDXR/STXR), LLAT-M02 (seqlock 메모리 순서), BM-B13 (SysTick), CPP-B06 (RAII 임계 구역 가드), RUST-M06 (Rust 의 ISR 공유 상태), C-A11 (ISR 버그 코드 리뷰)
