---
id: RTOS-M14
time: 90분
updated: 2026-11-08
---
# RTOS-M14 재진입성과 스레드 안전: 공유 라이브러리 함수 보호

## 학습 목표

- 두 태스크가 **정적 버퍼 함수**와 **`strtok`** 을 같이 쓸 때, 선점(preemption)이 어느 순간에 걸리면 결과가 섞이는지 tick 단위 타임라인으로 재현하고, "호출 **중간**" 과 "호출 **사이**" 라는 두 위험 구간을 구분해 말할 수 있다.
- 세 가지 수정 — **재진입 버전(`_r`)**, **뮤텍스 래핑**, **태스크별 컨텍스트(newlib `struct _reent`)** — 을 미니 커널에서 각각 구현하고, 각각의 비용(블로킹 tick, RAM, ISR 에서의 한계)을 숫자로 비교할 수 있다.
- 뮤텍스로 감쌀 때 **락 범위**를 "결과를 다 쓸 때까지 / 토큰화 세션 전체" 로 잡아야 하는 이유와, 호출 단위 락이 `strtok` 을 못 고치는 이유를 설명할 수 있다.
- newlib 의 `__malloc_lock` 이 왜 **재귀 락**이어야 하는지 소스(`mlock.c`, `_mallocr.c` 의 realloc)로 설명하고, FreeRTOS 에서 그 락을 스케줄러 정지로 만들지 뮤텍스로 만들지 판단할 수 있다.
- `configUSE_NEWLIB_REENTRANT` 가 FreeRTOS V11.3.2 소스에서 실제로 하는 일(TCB 의 `xTLSBlock`, 스위치 때 `_impure_ptr` 교체)과 **하지 않는 일**(힙 보호, ISR 보호, SMP)을 말할 수 있다.

## 1. 문제

펌웨어 면접의 단골 질문 "FreeRTOS 에서 newlib 의 `printf`/`malloc`/`strtok` 을 여러 태스크가 써도 되나요?" 는 결국 **선점형 커널 위에서 공유 라이브러리 코드를 어떻게 보호하느냐**는 질문이다. 그리고 이 버그는 테스트에서 거의 안 보인다 — tick 인터럽트가 *딱 그 몇 명령 사이*에 떨어질 때만 터진다.

장면은 이렇다. 텔레메트리 태스크 **L**(우선순위 1)이 설정 줄 `"rate=100,mode=fast,gain=3"` 을 `strtok` 으로 읽고, 배터리 전압 3300 mV 를 옛날식 `itoa` 처럼 **정적 버퍼를 돌려주는** 함수로 문자열로 만든다. UART RX ISR 이 명령 줄을 받으면 CLI 태스크 **H**(우선순위 3)를 깨우고(`xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR`), H 는 L 을 선점해 **같은 함수들**로 `"led=1,pwm=50"` 을 파싱하고 응답 값 7 을 포맷한다.

C-A04 가 `strtok` 의 의미와 "재진입 / 스레드 안전 / ISR 안전" 세 성질을 이미 다뤘다 (C-A04 참고). 거기서는 끼어드는 쪽이 **ISR** 이었다. 이 레슨의 새 부분은 끼어드는 쪽이 **더 높은 우선순위 태스크**라는 것 — 태스크는 블록할 수 있으므로 뮤텍스가 *가능해지고*, 커널이 문맥마다 상태를 *바꿔 끼울 수* 있게 된다. 그 두 수단과 그 한계가 핵심이다.

**미니 커널** (starter.c 에 주어짐). 단일 OS 스레드, tick 구동, 결정적:

- 태스크 = `ucontext` 코루틴, 각자 64 KB 스택. 진짜 호출 스택이 있어서 "함수 한가운데서 선점" 이 그대로 일어난다.
- 라이브러리 코드 곳곳의 `kpoint()` = "여기서 tick 인터럽트가 걸릴 수 있다". 스케줄러로 돌아가 tick 이 1 늘고, 그 tick 에 예정된 ISR 대역이 돌고, **가장 높은 우선순위 READY 태스크**가 다음 tick 을 받는다 (같으면 돌던 태스크 계속).
- `k_mutex`: 비재귀, 소유자 + 우선순위순 대기, 해제 시 직접 넘겨주고 더 높은 쪽이면 즉시 양보. **우선순위 상속 없음** (RTOS-B10, RTOS-M04).
- 타임라인 기호는 RTOS-M03/M04/M05 와 같다: `R` 실행, `r` READY 인데 못 돎, `B` 블록, `.` 출시 전/끝.

정적 버퍼 함수 `fmt_u32_static(3300)` 은 버퍼 끝에서부터 자릿수를 쓰고 자릿수마다 `kpoint()` 를 지난다. L 이 두 자리를 쓴 순간 H 가 깨어나면:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">static char buf[11] 하나를 L 과 H 가 공유 — 실측 결과 L 은 "3300" 대신 "3307"</text>
  <g font-family="monospace" fill="currentColor">
    <text x="150" y="48">[6]</text><text x="200" y="48">[7]</text><text x="250" y="48">[8]</text><text x="300" y="48">[9]</text><text x="347" y="48">[10]</text>
  </g>
  <text x="10" y="76" fill="currentColor">① tick 0–1 L</text>
  <g stroke="currentColor">
    <rect x="140" y="58" width="50" height="28" class="f-mut"/><rect x="190" y="58" width="50" height="28" class="f-mut"/>
    <rect x="240" y="58" width="50" height="28" class="f-acc"/><rect x="290" y="58" width="50" height="28" class="f-acc"/>
    <rect x="340" y="58" width="50" height="28" class="f-mut"/>
  </g>
  <g font-family="monospace" fill="currentColor"><text x="260" y="77">0</text><text x="310" y="77">0</text><text x="355" y="77">\0</text></g>
  <text x="410" y="77" fill="currentColor">L: 1의 자리, 10의 자리 → 선점</text>
  <text x="10" y="126" fill="currentColor">② tick 2–3 H</text>
  <g stroke="currentColor">
    <rect x="140" y="108" width="50" height="28" class="f-mut"/><rect x="190" y="108" width="50" height="28" class="f-mut"/>
    <rect x="240" y="108" width="50" height="28" class="f-acc"/><rect x="290" y="108" width="50" height="28" class="f-warn"/>
    <rect x="340" y="108" width="50" height="28" class="f-warn"/>
  </g>
  <g font-family="monospace" fill="currentColor"><text x="260" y="127">0</text><text x="310" y="127">7</text><text x="355" y="127">\0</text></g>
  <text x="410" y="127" fill="currentColor">H: fmt(7) 이 [9],[10] 을 덮고 "7" 을 받음</text>
  <text x="10" y="176" fill="currentColor">③ tick 4–6 L</text>
  <g stroke="currentColor">
    <rect x="140" y="158" width="50" height="28" class="f-acc"/><rect x="190" y="158" width="50" height="28" class="f-acc"/>
    <rect x="240" y="158" width="50" height="28" class="f-acc"/><rect x="290" y="158" width="50" height="28" class="f-warn"/>
    <rect x="340" y="158" width="50" height="28" class="f-warn"/>
  </g>
  <g font-family="monospace" fill="currentColor"><text x="160" y="177">3</text><text x="210" y="177">3</text><text x="260" y="177">0</text><text x="310" y="177">7</text><text x="355" y="177">\0</text></g>
  <text x="410" y="177" fill="currentColor">L: 나머지 두 자리, &amp;buf[6] 반환</text>
  <path d="M140 200 L390 200" stroke="currentColor" stroke-width="2"/>
  <text x="140" y="222" fill="currentColor" font-family="monospace">L 이 받은 문자열 = "3307"   (H 의 "7" 은 정상)</text>
  <rect x="20" y="240" width="700" height="46" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="32" y="260" fill="currentColor">p 는 지역 변수라 각자 것이지만 buf 는 하나 — 공유된 것은 "결과가 사는 곳" 이다.</text>
  <text x="32" y="278" fill="currentColor">반환 후 복사 전(tick 4, 5)에 선점돼도 같은 결과: 위험 구간 = 변환 시작 → 결과 사용 끝.</text>
</svg>
```

두 함수의 위험 구간은 모양이 다르다:

| 함수 | 숨은 공유 상태 | 언제 깨지나 | 이 레슨의 증상 |
|---|---|---|---|
| `fmt_u32_static` (itoa, `asctime`, `inet_ntoa`, `strerror` 꼴) | 결과 버퍼 | 호출 **중간** + 반환 후 **사용 끝까지** | L 이 `"3307"` 을 받음 |
| `lib_strtok` (옛 libc `strtok`) | "다음 위치" 포인터 | 호출과 호출 **사이** (세션 전체) | L 이 `mode`, `gain` 을 잃음 |
| toy `malloc` | 프리 리스트 | 리스트를 걷는 **중간** | 같은 블록을 두 태스크에 줌 |

### Part A — 재현과 `_r` 수정

- 주어진 시나리오로 정적 버전이 깨지는 것을 확인한다: H 를 tick 2 에 깨우면 L 은 `"3307"`, tick 3 에 깨우면 L 은 키/값 1쌍만. H 를 tick 0–7 **모든 시점**에 깨워 보는 sweep 을 돌린다.
- `char *fmt_u32_r(uint32_t v, char *buf, size_t n)` — 호출자 버퍼에 쓴다. 자릿수 + NUL 이 안 들어가면 아무것도 안 쓰고 `NULL`. 자릿수마다 `kpoint()` (같은 선점 지점에서 비교).
- `int parse_kv_r(char *line, kv_t *kv, int max)` — `parse_kv_static` 과 같은 결과·같은 `kpoint` 위치, 단 `lib_strtok_r` + 지역 `save`.
- 테스트: 경계값(0, `UINT32_MAX`, 버퍼 10/11바이트), 모든 해제 시점 8개에서 두 태스크 결과가 모두 맞음.

### Part B — 뮤텍스 래핑과 재귀 `malloc` 락

- 주어진 `lib_strtok_percall` (호출마다 lock/unlock) 이 **여전히 깨지는** 것을 확인한다.
- `fmt_u32_locked(v, dst, n)` — 락을 쥔 채 `fmt_u32_static` 을 부르고 결과를 **복사한 뒤** 푼다. `parse_kv_locked` — 세션 전체를 락으로.
- H 가 블록되는 tick 수를 타임라인에서 센다: "L 의 남은 임계 구역" 과 같아야 한다.
- `malloc_lock()` / `malloc_unlock()` — newlib `__malloc_lock` 의미의 **재귀 락**(같은 태스크면 깊이 +1). toy 힙: 두 태스크가 할당 중 선점돼도 다른 블록, `toy_realloc` (락을 쥔 채 `toy_malloc`/`toy_free`) 이 교착 없이 동작. 주어진 비재귀 모드는 커널이 **자기 교착**을 검출해야 한다.

### Part C — 태스크별 C 런타임 컨텍스트 (newlib `_reent` 모델)

- 커널의 TCB 에는 `k_reent_t reent` 가 있고 (FreeRTOS `xTLSBlock`), 라이브러리는 `REENT` (= `g_impure_ptr`, newlib `_REENT` = `_impure_ptr`) 로 상태를 찾는다.
- `port_switch_hook(next)` — 문맥 전환 때 `g_impure_ptr = &next->reent` (FreeRTOS `configSET_TLS_BLOCK`).
- `strtok_reent(s, d)`, `fmt_u32_reent(v)` — 옛 API 그대로, 상태만 `REENT` 안에 (newlib 의 `strtok` 구현과 같은 한 줄).
- 시나리오: UART RX ISR → CLI 태스크 H, 텔레메트리 태스크 L. 옵션 켬 → 모든 해제 시점에서 정확. 옵션 끔(전역 `_impure_data` 하나) → 정적 버전과 같은 깨짐. **ISR 이 `strtok_reent` 를 쓰면** 켜 놓아도 L 이 깨진다 — 그것도 정확히 어떻게 깨지는지 테스트로 고정한다.

## 2. Problem (English)

> **Protecting non-reentrant library code under a preemptive RTOS.**
>
> You are given a deterministic, single-threaded, tick-driven mini kernel (ucontext tasks, highest-priority-ready scheduling, a non-recursive mutex without priority inheritance) and a tiny "libc": a static-buffer `fmt_u32_static()`, a static-state `lib_strtok()`, and a toy heap. A low-priority telemetry task and a high-priority CLI task (woken by a UART RX ISR) share these functions.
>
> 1. Show that preemption at the wrong tick garbles results (`"3300"` becomes `"3307"`; the parser loses fields). Implement `char *fmt_u32_r(uint32_t v, char *buf, size_t n)` (returns NULL if it does not fit) and `int parse_kv_r(char *line, kv_t *kv, int max)`, and prove both tasks are correct for **every** release tick.
> 2. Fix the same code with a mutex instead. What must the critical section cover? Why does locking each `strtok` call separately not help? Measure how many ticks the high-priority task is blocked. Implement `malloc_lock()/malloc_unlock()` with newlib `__malloc_lock` semantics (recursive), and show a non-recursive lock self-deadlocks in `realloc`.
> 3. Fix it a third way, the newlib/FreeRTOS way: per-task reentrancy structures swapped by a context-switch hook (`_impure_ptr`). Show what this does **not** protect against (an ISR using the same functions).
>
> Follow-ups: What exactly does `configUSE_NEWLIB_REENTRANT` change in FreeRTOS? Does it make `malloc` safe? Scheduler suspension vs mutex for the heap lock? What breaks on SMP?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "11/32 passed" (주어진 정적 버전이 '정확히 이렇게' 깨지는지 확인하는 테스트들) — TODO 9곳을 채워 32/32
```

- `#define _XOPEN_SOURCE 600` 은 모든 include 앞에 둔다. macOS SDK 는 `<ucontext.h>` 를 이 매크로 없이 쓰면 막는다.
- macOS 는 `getcontext/makecontext/swapcontext` 를 deprecated 로 표시한다 (POSIX.1-2008 에서 삭제된 인터페이스). 결정적인 코루틴 커널을 만들려고 **일부러** 쓰는 것이라 파일 안에서 `-Wdeprecated-declarations` 하나만 끈다 (`#pragma GCC diagnostic ignored`, clang 도 따름). 다른 경고는 `-Werror` 그대로.
- 태스크 스택은 `static _Alignas(16) char g_stack[4][64 KB]` — 힙을 안 쓰고, arm64 의 16바이트 SP 정렬을 맞춘다.
- `k_run()` 은 끝나면 `g_cur = NULL` 로 "커널 밖" 표시를 한다. 이걸 빼먹으면 테스트가 커널 밖에서 부른 `kpoint()` 가 끝난 스케줄러 문맥으로 점프해 죽는다 (작성 중 실제로 겪은 SIGSEGV).

Cortex-M 비용 데모 (5. 풀이 Part C):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_reent_m4.c -o -
```

## 4. 힌트

<details>
<summary>힌트 1 — fmt_u32_r: 먼저 길이, 그다음 쓰기</summary>

`for (t = v; ; t /= 10) { len++; if (t < 10) break; }` 로 자릿수를 센다 (v = 0 도 1자리). `n < len + 1` 이면 `NULL`. 그다음 `buf[len] = '\0'` 을 쓰고 `i = len-1` 부터 거꾸로 채우며 자릿수마다 `kpoint()`. 결과는 `buf` 자체 — 정적인 것은 하나도 없다.

</details>

<details>
<summary>힌트 2 — 락 범위는 "공유 상태가 의미를 갖는 기간"</summary>

정적 버퍼의 의미는 "변환 시작 → 내가 결과를 다 읽을 때까지" 산다. 그래서 `lock → p = fmt_u32_static(v) → 복사 → unlock`. `strtok` 의 "다음 위치" 는 **첫 호출부터 마지막 `NULL` 까지** 산다. 그래서 `lock → parse_kv_static(...) → unlock`. 호출 하나만 감싸면 호출 *사이*가 열려 있다.

</details>

<details>
<summary>힌트 3 — 재귀 락은 "소유자 + 깊이"</summary>

`g_heap_mtx.owner == k_self()` 면 이미 내가 쥔 것 → `g_heap_depth++` 하고 끝. 아니면 `k_mutex_lock` (필요하면 블록) 후 `depth = 1`. unlock 은 `--depth == 0` 일 때만 진짜로 푼다. owner 필드는 소유자 자신만 자기 값으로 바꾸므로, 단일 코어에서 이 비교는 락 없이 해도 안전하다.

</details>

<details>
<summary>힌트 4 — Part C 는 세 줄</summary>

`g_impure_ptr = &next->reent;` / `return lib_strtok_r(s, d, &REENT->strtok_last);` / `return fmt_u32_core(v, REENT->fmt_buf);`. 마지막 ISR 테스트는 고치는 게 아니라 **"이렇게 깨진다"** 를 확인하는 것이다: ISR 은 문맥 전환을 일으키지 않으므로 `g_impure_ptr` 는 선점당한 L 의 것 그대로다.

</details>

## 5. 풀이

### Part A — 언제 깨지나: 타임라인과 sweep

solution.c 가 실제로 찍은 타임라인 (H 를 tick 2 에 깨움, 정적 버퍼):

```text
[A] static fmt, H released at tick 2
tick : 0 1 2 3 4 5 6
H    : . . R R . . .
L    : R R r r R R R
     L="3307" H="7"
```

tick 0, 1 에 L 이 `'0'`, `'0'` 을 쓰고, tick 2 시작의 ISR 대역이 H 를 READY 로 → H(3) > L(1) 이므로 그 tick 은 H 가 받는다. H 는 `'7'` 을 쓰고(tick 2) 결과를 내보내고(tick 3) 끝. L 이 tick 4, 5 에 `'3'`, `'3'` 을 쓰고 tick 6 에 결과를 내보낸다 → 그림의 `"3307"`.

**모든 해제 시점 sweep** (`해제tick:L/H`, 실측):

```text
[A] fmt static 0:3300/7 1:3307/7 2:3307/7 3:3307/7 4:3307/7 5:3307/7 6:3300/7 7:3300/7
[A] tok static 0:3/2 1:0/2 2:1/2 3:1/2 4:2/2 5:2/2 6:3/2 7:3/2
```

| H 해제 tick | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 정적 fmt: L 결과 | 3300 | **3307** | **3307** | **3307** | **3307** | **3307** | 3300 | 3300 |
| 정적 strtok: L 쌍 수 (정답 3) | 3 | **0** | **1** | **1** | **2** | **2** | 3 | 3 |

읽는 법:

- tick 0: H 가 L 보다 먼저 시작해 끝까지 돈다 → L 은 깨끗한 상태에서 시작. 6 이후: L 이 이미 끝났다. **위험 구간 = L 이 그 함수를 "쓰는 중" 인 tick 들** — 5/8 시점에서 깨졌다. 실제 칩에서는 tick 주기(1 ms)에 비해 이 구간이 수 µs 라 확률이 아주 작고, 그래서 출시 후에 터진다. C-A04 의 실측(80만 줄 중 17–30줄)과 같은 구조다.
- tick 4, 5 (fmt): L 은 변환을 이미 끝냈는데도 깨졌다. 반환된 포인터가 **아직 공유 버퍼를 가리키기** 때문이다 — 위험 구간은 "사용 끝" 까지다.
- tick 1 (strtok): L 은 `"rate"` 를 받은 직후 선점됐고, H 의 세션이 `last` 를 H 문자열 끝(NUL)으로 옮겨 놓았다. L 의 다음 `strtok(NULL)` 은 `NULL` → 값 없는 키라 0쌍. 아무 오류 표시도 없다.
- 데이터는 섞였지만 **H 는 매번 정상**이다. 이긴 쪽(높은 우선순위)은 멀쩡하고, 진 쪽이 조용히 틀린 값을 쓴다 — 로그에 찍힌 L 의 전압이 가끔 이상한 이유를 한참 찾게 되는 패턴.

**`_r` 수정.** 상태를 호출자에게 넘기면 공유할 것 자체가 없다:

```c
static char *fmt_u32_r(uint32_t v, char *buf, size_t n) {
    int len = 0;
    for (uint32_t t = v; ; t /= 10u) { len++; if (t < 10u) break; }
    if (buf == NULL || n < (size_t)len + 1) return NULL;   /* 안 들어가면 아무것도 안 쓴다 */
    buf[len] = '\0';
    for (int i = len - 1; i >= 0; i--) { buf[i] = (char)('0' + v % 10u); v /= 10u; kpoint(); }
    return buf;
}
```

`parse_kv_r` 은 `parse_kv_static` 의 `lib_strtok(...)` 세 곳을 `lib_strtok_r(..., &save)` 로 바꾸고 `char *save` 를 지역으로 두면 끝이다. 선점 지점(`kpoint`)은 그대로인데 8개 시점 모두 통과한다 — **선점을 막은 게 아니라, 선점돼도 잃을 게 없게 만든 것**이다. 지연 0, 락 0, RAM 은 호출자 스택의 11바이트 + 포인터 하나.

### Part B — 뮤텍스 래핑: 범위와 대가

**호출 단위 락은 안 된다.** `lib_strtok_percall` 은 호출 하나하나는 원자적으로 만들지만, tick 3 에 H 를 깨우면 결과는 정적 버전과 똑같이 L 1쌍이다 (테스트로 확인). 락이 풀린 호출 *사이*에 H 의 세션 전체가 들어온다. 정적 버퍼도 같다: `lock; p = fmt(); unlock; use(p);` 는 `unlock` 과 `use` 사이가 열려 있다.

**세션 락.** 실측 타임라인:

```text
[B] locked fmt, H released at tick 2
tick : 0 1 2 3 4 5 6
H    : . . B B R R .
L    : R R R R r r R
     L="3300" H="7"  H blocked ticks=2

[B] locked strtok session, H released at tick 3
tick : 0 1 2 3 4 5 6 7 8 9
H    : . . . B B B R R R R
L    : R R R R R R r r r r
     L pairs=3 H pairs=2  H blocked ticks=3
```

정확해졌다. 대신 H 는 **L 의 남은 임계 구역만큼** 기다린다 (2 tick = 남은 두 자리, 3 tick = 남은 세션). 해제 순간 커널은 H 에게 소유권을 넘기고 즉시 양보하므로 H 는 그 다음 tick 부터 돈다 (FreeRTOS 의 `xSemaphoreGive` → 대기자 깨움 → 더 높으면 yield 와 같은 모양).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">solution.c 가 찍은 타임라인 그대로 (H 해제 = tick 2)</text>
  <text x="60" y="48" fill="currentColor">정적 버퍼 (보호 없음)</text>
  <text x="420" y="48" fill="currentColor">fmt_u32_locked (복사까지 락)</text>
  <g font-family="monospace" fill="currentColor" font-size="12">
    <text x="73" y="66">0</text><text x="113" y="66">1</text><text x="153" y="66">2</text><text x="193" y="66">3</text><text x="233" y="66">4</text><text x="273" y="66">5</text><text x="313" y="66">6</text>
    <text x="433" y="66">0</text><text x="473" y="66">1</text><text x="513" y="66">2</text><text x="553" y="66">3</text><text x="593" y="66">4</text><text x="633" y="66">5</text><text x="673" y="66">6</text>
  </g>
  <text x="30" y="94" fill="currentColor">H</text><text x="30" y="134" fill="currentColor">L</text>
  <text x="390" y="94" fill="currentColor">H</text><text x="390" y="134" fill="currentColor">L</text>
  <g stroke="currentColor">
    <rect x="140" y="76" width="40" height="26" class="f-warn"/><rect x="180" y="76" width="40" height="26" class="f-warn"/>
    <rect x="60" y="116" width="40" height="26" class="f-acc"/><rect x="100" y="116" width="40" height="26" class="f-acc"/>
    <rect x="140" y="116" width="80" height="26" class="f-mut"/>
    <rect x="220" y="116" width="40" height="26" class="f-acc"/><rect x="260" y="116" width="40" height="26" class="f-acc"/><rect x="300" y="116" width="40" height="26" class="f-acc"/>
    <rect x="500" y="76" width="80" height="26" class="f-mut"/>
    <rect x="580" y="76" width="40" height="26" class="f-warn"/><rect x="620" y="76" width="40" height="26" class="f-warn"/>
    <rect x="420" y="116" width="40" height="26" class="f-acc"/><rect x="460" y="116" width="40" height="26" class="f-acc"/>
    <rect x="500" y="116" width="40" height="26" class="f-acc"/><rect x="540" y="116" width="40" height="26" class="f-acc"/>
    <rect x="580" y="116" width="80" height="26" class="f-mut"/>
    <rect x="660" y="116" width="40" height="26" class="f-acc"/>
  </g>
  <g font-family="monospace" fill="currentColor" font-size="12">
    <text x="153" y="94">'7'</text><text x="189" y="94">out</text>
    <text x="73" y="134">'0'</text><text x="113" y="134">'0'</text><text x="168" y="134">r r</text>
    <text x="233" y="134">'3'</text><text x="273" y="134">'3'</text><text x="309" y="134">out</text>
    <text x="510" y="94">B 블록</text><text x="593" y="94">'7'</text><text x="629" y="94">out</text>
    <text x="433" y="134">'0'</text><text x="473" y="134">'0'</text><text x="513" y="134">'3'</text><text x="553" y="134">'3'</text>
    <text x="608" y="134">r r</text><text x="669" y="134">out</text>
  </g>
  <text x="60" y="172" fill="currentColor">L 결과 "3307" ✗ / H "7"</text>
  <text x="420" y="172" fill="currentColor">L 결과 "3300" ✓ / H "7", H 블록 2 tick</text>
  <rect x="20" y="190" width="700" height="48" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="32" y="210" fill="currentColor">락은 데이터를 지키는 대가로 높은 우선순위 태스크의 응답 시간을 낮은 쪽 임계 구역에 묶는다.</text>
  <text x="32" y="228" fill="currentColor">그 사이에 중간 우선순위가 끼면 대기는 끝이 없다 → 연습 4, RTOS-M03 (역전) / RTOS-M04 (상속)</text>
</svg>
```

**뮤텍스 래핑의 비용 정리:**

| | `_r` | 세션 뮤텍스 |
|---|---|---|
| H 블록 (이 시나리오) | 0 tick | 남은 임계 구역 (2–3 tick) |
| 우선순위 역전 | 없음 (락이 없다) | 생김 → 상속 뮤텍스 필요 (연습 4: H 대기 = 4 + C_M) |
| API 변경 | 호출부 전부 | 없음 (래퍼만) |
| ISR 에서 | 사용 가능 (상태가 인자에) | **불가** — ISR 은 블록 못 함, 소유자 개념 없음 (RTOS-B10) |
| 같은 태스크 안 중첩 | 안전 | 비재귀 락이면 자기 교착 |

**newlib 의 malloc 락 — 왜 재귀인가.** `malloc`/`printf` 는 `_r` 로 바꿀 수 없는 "전역 자원" 을 쓴다: 힙은 시스템에 하나다. newlib 은 그래서 힙 접근 앞뒤에 훅을 부른다. 소스(newlib-cygwin git HEAD `b5a71199`, 2026-11-08 조회)에서 직접 확인한 것:

| 파일:줄 | 내용 |
|---|---|
| `stdlib/_mallocr.c:314–315` | `#define MALLOC_LOCK __malloc_lock(reent_ptr)` / `MALLOC_UNLOCK` |
| `stdlib/_mallocr.c:2818, 2851, 2980` | `rEALLOc` 이 `MALLOC_LOCK` 을 쥔 채 `mALLOc(...)`, `fREe(...)` 를 부른다 → **같은 태스크가 락을 두 번** |
| `stdlib/mlock.c:25–29` | "A call to malloc may call `__malloc_lock` recursively ... avoid causing a thread to wait for a lock that it already holds" |
| `stdlib/mlock.c:36–44` | 기본 구현 = `__lock_acquire_recursive(__malloc_recursive_mutex)` |
| `include/sys/lock.h:9–25` | `_RETARGETABLE_LOCKING` 이 아니면 모든 `__lock_*` 가 `((void) 0)` — **아무것도 안 한다** |
| `include/sys/lock.h:41–61`, `misc/lock.c:66–75, 92–99` | retargetable 빌드: `__retarget_lock_acquire_recursive` 등 10개 함수 + 정적 락 8개(`__lock___malloc_recursive_mutex`, `__lock___sfp_recursive_mutex`, `__lock___env_recursive_mutex`, `__lock___tz_mutex` ...). 기본은 빈 함수(single-thread 용), 멀티스레드면 **전부** 제공하라고 문서가 요구 |
| `stdlib/nano-mallocr.c:535–560` | newlib-nano 의 `realloc` 은 바깥 락 없이 `nano_malloc`/`nano_free` 를 부른다 (재귀 안 함) |

그래서 Part B 의 `toy_realloc` 은 `_mallocr.c` 의 모양을 그대로 흉내 냈고, 결과는:

```text
[B] toy heap, H released at tick 1
tick : 0 1 2 3
H    : . R R .
L    : R r r R
     L=block 0  H=block 1  used=2
[B] realloc with NON-recursive lock: k_run=-1 self_deadlock=1
```

starter(락 없음)에서는 같은 장면이 `L=block 0  H=block 0  used=1` — **같은 블록을 두 태스크에** 줬고, 나중에 쓴 L 의 `'L'` 이 H 의 데이터를 덮었다. 비재귀 락은 정확하지만 realloc 에서 자기 자신을 기다린다: 커널이 "READY 0, 미래 이벤트 0, BLOCKED 1" 로 교착을 검출했다 (`K_DEADLOCK`). newlib-nano 만 쓰면 이 재귀가 우연히 안 생기지만, 문서가 재귀를 허용하므로 **재귀 락이 정답**이다.

FreeRTOS 쪽에는 이 락을 만드는 방법이 두 가지 있다: ① 스케줄러 정지 `vTaskSuspendAll()/xTaskResumeAll()` — `heap_3.c:63–68` 의 `pvPortMalloc` 이 정확히 이렇게 `malloc()` 을 감싼다. 카운터라서 자연히 중첩된다. ② 재귀 뮤텍스 `xSemaphoreTakeRecursive`. 차이는 연습 3 에서 숫자로 본다. 주의: 앱이 `pvPortMalloc` 만 써도 newlib 내부(`printf` 의 버퍼 할당 등)는 `_malloc_r` 을 직접 부르므로 `heap_3` 의 보호를 우회한다 → `__malloc_lock` 을 따로 제공해야 한다 (FreeRTOS `newlib-freertos.h:32–40` 주석: "newlib design implements a system-wide malloc() that must be provided with locks").

### Part C — 태스크별 컨텍스트: newlib `_reent` 와 FreeRTOS

newlib 은 `strtok` 의 "다음 위치", `errno`, `rand` 시드, `asctime` 버퍼, stdio 상태 같은 것을 전부 `struct _reent` 하나에 모아 두고, 라이브러리는 `_REENT` 매크로로 그것을 찾는다. 소스로 확인한 것:

- `string/strtok.c`: `struct _reent *reent = _REENT; ... return __strtok_r(s, delim, &(_REENT_STRTOK_LAST(reent)), 1);` — API 는 그대로, 상태 위치만 `_reent` 안.
- `errno/errno.c`: `__errno() { return &_REENT_ERRNO(_REENT); }` — `errno` 도 `_reent` 안.
- `include/sys/reent.h:815–822`: `__DYNAMIC_REENT__` 가 없으면 `#define _REENT _impure_ptr`, 있으면 `__getreent()` 함수 호출. `reent/impure.c:14–18`: `_impure_data` 하나와 그것을 가리키는 `_impure_ptr`.
- (`_REENT_THREAD_LOCAL` 빌드에서는 `_reent` 대신 `_Thread_local` 변수들을 쓴다 — `strtok.c` 의 `_tls_strtok_last`, `reent.h:907–908`.)

전역 포인터 하나가 "지금의 C 런타임 상태" 를 가리키니, **문맥 전환 때 그 포인터만 바꾸면** 태스크마다 다른 상태를 갖게 된다. FreeRTOS V11.3.2 에서 `configUSE_NEWLIB_REENTRANT 1` 이 하는 일이 정확히 이것이다:

| 위치 (V11.3.2) | 하는 일 | 미니 커널 |
|---|---|---|
| `include/FreeRTOS.h:121–130` | 옵션이 1이면 `newlib-freertos.h` 포함 | `g_cfg_reent` |
| `include/newlib-freertos.h:44–60` | `configUSE_C_RUNTIME_TLS_SUPPORT 1`, `configTLS_BLOCK_TYPE = struct _reent`, `INIT = _REENT_INIT_PTR`, `SET = (_impure_ptr = &(xTLSBlock))`, `DEINIT = _reclaim_reent` | `k_reent_t`, `reent_init`, `port_switch_hook` |
| `tasks.c:433–435` | TCB 에 `configTLS_BLOCK_TYPE xTLSBlock;` | `tcb_t.reent` |
| `tasks.c:1943–1946` | 태스크 생성 때 `configINIT_TLS_BLOCK` | `k_create` 의 `reent_init` |
| `tasks.c:3789–3793` | `vTaskStartScheduler`: 첫 태스크로 SET | 첫 pick 때 훅 |
| `tasks.c:5288–5292` | `vTaskSwitchContext`: `taskSELECT_HIGHEST_PRIORITY_TASK()` 뒤 SET | `k_run` 의 `if (n != g_cur) port_switch_hook(n)` |
| `tasks.c:6575–6578` | 태스크 삭제 때 `configDEINIT_TLS_BLOCK` | (없음) |

(같은 함수 `tasks.c:5262–5266, 5282–5286` 은 `configUSE_POSIX_ERRNO` 일 때 `FreeRTOS_errno` 를 TCB 에 저장/복원한다 — 같은 발상의 축소판. picolibc 는 `picolibc-freertos.h:87` 에서 SET 이 `_set_tls(xTLSBlock)` — 포인터 대신 TLS 레지스터.)

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">configUSE_NEWLIB_REENTRANT: 스위치 때 포인터 하나만 바꾼다</text>
  <rect x="20" y="40" width="220" height="100" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="60" fill="currentColor">TCB L (prio 1)</text>
  <rect x="32" y="70" width="196" height="58" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="40" y="90" fill="currentColor" font-family="monospace">reent.strtok_last</text>
  <text x="40" y="110" fill="currentColor" font-family="monospace">reent.fmt_buf / err</text>
  <rect x="20" y="160" width="220" height="100" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="180" fill="currentColor">TCB H (prio 3)</text>
  <rect x="32" y="190" width="196" height="58" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="40" y="210" fill="currentColor" font-family="monospace">reent.strtok_last</text>
  <text x="40" y="230" fill="currentColor" font-family="monospace">reent.fmt_buf / err</text>
  <rect x="330" y="120" width="150" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="345" y="145" fill="currentColor" font-family="monospace">_impure_ptr</text>
  <path d="M330 140 L245 100" stroke="currentColor" stroke-width="2" stroke-dasharray="5 4"/>
  <path d="M330 140 L245 215" stroke="currentColor" stroke-width="2"/>
  <path d="M252 207 L245 215 L255 216" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="250" y="270" fill="currentColor">스위치 = 한 번의 str (demo_reent_m4.c)</text>
  <rect x="540" y="40" width="180" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="552" y="62" fill="currentColor" font-family="monospace">strtok(s, d)</text>
  <text x="552" y="84" fill="currentColor" font-size="12">→ &amp;_REENT-&gt;strtok_last</text>
  <path d="M540 80 L480 130" stroke="currentColor" stroke-width="2"/>
  <rect x="540" y="180" width="180" height="80" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="552" y="202" fill="currentColor">UART ISR 가 strtok</text>
  <text x="552" y="222" fill="currentColor" font-size="12">문맥 전환 없음 →</text>
  <text x="552" y="240" fill="currentColor" font-size="12">선점당한 태스크의 reent 를</text>
  <text x="552" y="256" fill="currentColor" font-size="12">그대로 덮는다 ✗</text>
  <path d="M540 220 L480 150" stroke="currentColor" stroke-width="2"/>
</svg>
```

결과 (실측):

```text
[C] newlib-style reent ON, UART RX ISR at tick 3
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6
H    : . . . R R R R R R . . . . . . . .
L    : R R R r r r r r r R R R R R R R R
     L pairs=3 "3300"  H pairs=2 "7"
[C] reent ON + ISR strtok at tick 2: L pairs=2  [1]=y=2
```

- 옵션 켬: 락이 없으니 H 는 **블록 없이** 바로 돈다 (B 가 하나도 없다). 12개 해제 시점 모두 정확. 끝난 뒤 L 의 `reent.strtok_last` 는 L 의 줄 안을, H 의 것은 H 의 줄 안을 가리킨다 (테스트로 확인).
- 옵션 끔: `g_impure_ptr` 가 전역 `g_impure_data` 하나에 머물러 tick 3 결과가 정적 버전과 똑같이 L 1쌍.
- **ISR**: L 이 `rate=100` 을 읽은 뒤 ISR 이 `"x=1,y=2"` 를 두 토큰 자르면, ISR 은 L 의 reent 를 쓰므로 L 의 다음 위치가 ISR 버퍼의 `"y=2"` 로 바뀐다. L 은 **`y=2` 를 자기 설정으로 받아들였다** — 잃는 것보다 나쁜, 엉뚱한 값을 정상처럼 쓰는 경우다. C-A04 Q3 의 결론("`_reent` 는 태스크 로컬일 뿐, ISR 은 선점한 태스크의 것을 쓴다")을 커널 위에서 확인한 것. ISR 이 자기 `save` 로 `lib_strtok_r` 을 쓰면 L 은 무사했다.

**Cortex-M 에서의 비용** — `demo_reent_m4.c` 를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 로 본 실제 출력 (Apple clang 21, 지시어·주석 줄만 생략):

<!-- code: demo_reent_m4.c -->

```text
switch_hook:
	movw	r1, :lower16:impure_ptr
	adds	r0, #8
	movt	r1, :upper16:impure_ptr
	str	r0, [r1]
	bx	lr
strtok_state_static:
	movw	r0, :lower16:strtok_state_static.last
	movt	r0, :upper16:strtok_state_static.last
	bx	lr
strtok_state_reent:
	movw	r0, :lower16:impure_ptr
	movt	r0, :upper16:impure_ptr
	ldr	r0, [r0]
	adds	r0, #4
	bx	lr
errno_addr:
	movw	r0, :lower16:impure_ptr
	movt	r0, :upper16:impure_ptr
	ldr	r0, [r0]
	bx	lr
```

스위치 비용은 `str` 한 번(+주소 계산, `adds r0, #8` 은 TCB 안 `reent` 의 오프셋), 라이브러리 쪽은 상태에 닿을 때마다 `ldr` 하나가 더 붙는다 (정적판은 주소가 링크 타임 상수). 진짜 비용은 RAM 이다: 태스크마다 `struct _reent` 가 하나씩 TCB 안에 생긴다 (크기는 newlib 구성, 특히 `_REENT_SMALL` 여부에 따라 크게 다르다 — 이 맥에는 arm-none-eabi newlib 이 없어 **측정 못 함**, 대표값으로 수백 바이트~1 KB 안팎이라고 알려져 있음, 미검증).

**모델 → 실제 API 대응:**

| 이 레슨 (호스트 모델) | FreeRTOS | Zephyr | newlib / CMSIS |
|---|---|---|---|
| `kpoint()` (tick 경계) | SysTick → `xTaskIncrementTick` → PendSV 문맥 전환 | 시스템 클럭 tick | `SysTick_Handler` |
| `isr_uart_rx()` → `k_release(H)` | `xSemaphoreGiveFromISR(s, &woken); portYIELD_FROM_ISR(woken);` | `k_sem_give(&s)` | `USARTx_IRQHandler` |
| `k_mutex_lock/unlock` (상속 없음) | `xSemaphoreTake(m, portMAX_DELAY)` / `xSemaphoreGive(m)` (FreeRTOS 뮤텍스는 상속 **있음**) | `k_mutex_lock(&m, K_FOREVER)` / `k_mutex_unlock` | — |
| `malloc_lock` (재귀) | `xSemaphoreTakeRecursive` 또는 `vTaskSuspendAll` | `k_mutex` (소유자 재잠금 허용, 문서 기준) | `__malloc_lock` / `__retarget_lock_acquire_recursive` |
| 연습 3 `k_sched_suspend` | `vTaskSuspendAll` / `xTaskResumeAll` | `k_sched_lock` / `k_sched_unlock` | — |
| `tcb_t.reent`, `reent_init` | `xTLSBlock`, `configINIT_TLS_BLOCK` | (TLS 기반, 문서 기준·미검증) | `struct _reent`, `_REENT_INIT_PTR` |
| `port_switch_hook` | `configSET_TLS_BLOCK` in `vTaskSwitchContext` | — | `_impure_ptr = ...` |
| ISR 에서 락 금지 (`g_isr_lock`) | take 는 블로킹 API 라 ISR 금지, 소유자 있는 뮤텍스를 ISR 에서 give 하면 `configASSERT` (`queue.c:1360`) | ISR 에서 `k_mutex_lock` 불가 | `__disable_irq()` 로만 보호 가능 |

**SMP 주의 (소스 읽기 기준, 실측 아님).** `_impure_ptr` 는 전역 **하나**다. 그런데 SMP 판 `vTaskSwitchContext(xCoreID)` (`tasks.c:5385–5389`)도 같은 `configSET_TLS_BLOCK` 을 부른다 — 기본 newlib 매크로라면 코어 0 이 바꾼 포인터를 코어 1 이 덮는다. 코어별 포인터(`__DYNAMIC_REENT__` + 코어 레지스터를 읽는 `__getreent()`)나 TLS 레지스터(picolibc `_set_tls`)가 필요하다. 이 부분은 RTOS-A08 이 다룬다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 32/32 passed (6회 반복 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 32/32, 보고 없음
```

ASan + `ucontext`: 이 맥(Apple clang 21)에서는 기본 옵션으로 경고 없이 깨끗했고, `ASAN_OPTIONS=detect_stack_use_after_return=1` 을 줘도 32/32, 보고 없음이었다. 스레드를 안 쓰는 단일 OS 스레드 프로그램이라 TSan 은 해당 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `strtok` 을 호출마다 lock/unlock | 상태가 호출 **사이**에 산다. 실측: tick 3 에서 정적 버전과 똑같이 L 1쌍 | 세션 전체를 락, 또는 `strtok_r` + 지역 save |
| `lock; p = fmt(v); unlock; uart_write(p);` | unlock 과 사용 사이에 다른 태스크가 버퍼를 덮는다 (sweep 의 tick 4, 5) | 락 안에서 **복사**한 뒤 unlock, 또는 `fmt_u32_r` |
| `__malloc_lock` 을 일반(비재귀) 뮤텍스로 | `_mallocr.c` 의 realloc 은 락을 쥔 채 malloc/free → 자기 교착 (`self_deadlock=1`) | 재귀 뮤텍스, 또는 카운터인 `vTaskSuspendAll` |
| `pvPortMalloc` 만 쓰니 newlib 은 안전하다 | `printf`/`strdup` 등 newlib 내부는 `_malloc_r` 을 직접 부른다 | `__malloc_lock/__malloc_unlock` (또는 retarget 락 전부) 제공 |
| `configUSE_NEWLIB_REENTRANT 1` 이면 ISR 에서도 `strtok`/`printf` OK | ISR 은 선점한 태스크의 `_reent` 를 쓴다. 실측: L 이 ISR 의 `y=2` 를 받음 | ISR 은 이벤트만, 파싱·포맷은 태스크에서 (C-A04) |
| reent 옵션을 켜고 같은 태스크 안에서 `strtok` 중첩 | 태스크당 상태가 **하나**일 뿐이다 | 중첩 단계마다 `strtok_r` save (C-A04) |
| retarget 락을 일부만 구현 | `misc/lock.c` 문서: 일부만 주면 중복 정의로 링크 실패, 전부 제공해야 함 | 10개 함수 + 정적 락 8개 전부 |
| 라이브러리 전체 락 하나로 다 해결 | H 가 L 의 임계 구역에 묶이고, M 이 끼면 무한정 (연습 4: 4 + C_M) | 락 없는 `_r` 우선, 락이 필요하면 상속 뮤텍스 (RTOS-M04) |

## 8. 면접 Q&A

<details>
<summary>Q1. 두 태스크가 같은 strtok 을 쓰는데 테스트에선 멀쩡하고 현장에서만 가끔 필드가 사라집니다. 원인은?</summary>

`strtok` 의 숨은 정적 "다음 위치" 를 두 태스크가 공유한다. 낮은 우선순위 태스크가 토큰화 세션 **중간**(첫 호출과 마지막 호출 사이)에 선점되고, 높은 쪽이 같은 `strtok` 으로 자기 세션을 돌리면 그 포인터가 높은 쪽 문자열 끝으로 옮겨진다. 낮은 쪽의 다음 `strtok(NULL)` 은 `NULL` → 남은 필드가 조용히 사라진다. 높은 쪽은 항상 정상이라 의심도 안 받는다. 깨지려면 선점이 그 **창** 안에 떨어져야 하므로 드물다 — 이 레슨의 sweep 에서도 8개 시점 중 5개에서만 깨졌고, 실제 칩에서는 창이 tick 주기에 비해 훨씬 짧다. 찾는 법은 반복 테스트가 아니라 "모든 선점 지점 주입" (이 레슨의 sweep, C-A04 Part C) 과 `nm` 으로 쓰기 가능한 정적 객체 감사다.

</details>

<details>
<summary>Q2. 고치는 방법 세 가지를 비용과 한계로 비교해 보세요.</summary>

① **`_r` / 상태를 호출자에게**: 락 0, 블록 0, 우선순위 역전 0, ISR 에서도 됨. 대가는 API 변경(호출부 전부). 가능하면 항상 이것. ② **뮤텍스 래핑**: API 그대로. 대가는 높은 쪽이 낮은 쪽 임계 구역만큼 블록(실측 2–3 tick), 우선순위 역전 위험(상속 뮤텍스 필요), ISR 에서 불가, 같은 태스크 중첩 시 재귀 필요. 락 범위를 세션/사용 끝까지 잡아야 한다. ③ **태스크별 컨텍스트**(newlib `_reent` + `configUSE_NEWLIB_REENTRANT`): API 그대로, 블록 0, 스위치 비용은 store 한 번. 대가는 태스크마다 `struct _reent` RAM, 그리고 ISR·같은 태스크 중첩·공유 자원(힙, 같은 UART)은 **못 지킨다**. 실무 정답은 섞어 쓰기: 파서·포매터는 ①, 힙과 stdio 는 ②(재귀 락), 레거시 libc 상태는 ③.

</details>

<details>
<summary>Q3. configUSE_NEWLIB_REENTRANT 를 켜면 malloc 과 printf 는 태스크 간에 안전해지나요?</summary>

아니다, 그 옵션은 **상태**를 태스크별로 나눌 뿐이다. V11.3.2 소스로 보면 TCB 에 `struct _reent xTLSBlock` 을 두고(`tasks.c:434`), 생성 때 `_REENT_INIT_PTR`, `vTaskSwitchContext` 에서 `_impure_ptr = &xTLSBlock`(`newlib-freertos.h:55`, `tasks.c:5292`), 삭제 때 `_reclaim_reent` 를 부르는 게 전부다. 힙은 시스템 전체에 하나라 `_reent` 를 나눠도 공유된다 — `newlib-freertos.h` 주석이 직접 "system-wide malloc() that must be provided with locks" 라고 경고한다. 그래서 `__malloc_lock/__malloc_unlock` 을 (재귀로) 직접 제공해야 한다. `printf` 는 `_reent` 별 stdout 을 쓰는 구성도 있지만 같은 UART 로 나가므로 줄이 섞일 수 있고(연습 1), stdio 의 FILE 락(`_newlib_flockfile_start`, `stdio/local.h`)도 retarget 락을 줘야 실제로 잠긴다. ISR 은 어떤 경우에도 안 된다.

</details>

<details>
<summary>Q4. __malloc_lock 은 왜 재귀여야 하고, FreeRTOS 에서는 무엇으로 구현하나요?</summary>

newlib `mlock.c` 문서가 "malloc 이 `__malloc_lock` 을 재귀적으로 부를 수 있다" 고 명시하고, 실제로 `_mallocr.c` 의 `rEALLOc` 은 `MALLOC_LOCK`(2818행)을 쥔 채 `mALLOc`(2851), `fREe`(2980)를 부른다. 비재귀 락이면 자기 자신을 기다린다 — 이 레슨에서 커널이 `self_deadlock=1`, `K_DEADLOCK` 으로 검출했다. 구현 선택지: ① `vTaskSuspendAll/xTaskResumeAll` — 카운터라 자연히 중첩되고 우선순위 역전도 없고 싸다(`heap_3.c` 가 이 방식). 대신 힙과 무관한 높은 우선순위 태스크까지 멈춘다(연습 3: H 완료 tick 4 → 7). ② 재귀 뮤텍스 `xSemaphoreTakeRecursive` — 경쟁하는 태스크만 막고 상속도 된다. 대신 뮤텍스를 만들기 전·스케줄러 시작 전의 malloc(초기화 코드, C++ 정적 생성자)을 따로 처리해야 하고 take/give 비용이 더 크다. 둘 다 **ISR 은 못 막는다** — ISR 에서 malloc 이 필요하면 `taskENTER_CRITICAL_FROM_ISR` 류 인터럽트 마스킹이 필요한데, 힙 탐색 시간만큼 인터럽트 지연이 생기므로 보통은 "ISR 에서 할당 금지 + 고정 블록 풀" 로 간다. newlib 이 retargetable locking 으로 빌드되었다면 `__retarget_lock_*` 10개와 정적 락 8개를 전부 제공하는 방식이 된다 (`misc/lock.c`).

</details>

<details>
<summary>Q5. 뮤텍스로 감쌀 때 락 범위는 어디까지여야 하나요? 정적 결과를 돌려주는 함수는?</summary>

"공유 상태가 의미를 갖는 기간 전체" 다. `strtok` 은 첫 호출부터 `NULL` 을 받을 때까지(세션), 정적 버퍼 함수는 변환 시작부터 **호출자가 결과를 다 읽을 때까지**. 그래서 래퍼는 `lock → p = f() → memcpy(dst, p) → unlock` 이어야 하고, 포인터를 그대로 돌려주는 래퍼(`lock; p = f(); unlock; return p;`)는 틀렸다 — sweep 에서 변환을 다 끝낸 뒤(tick 4, 5)의 선점으로도 `"3307"` 이 나왔다. 호출 단위 락(`lib_strtok_percall`)은 각 호출의 내부 레이스만 없앨 뿐 결과는 정적 버전과 똑같이 깨졌다. 락 범위가 길어질수록 높은 쪽 블록 시간이 길어지므로(세션 락: 3 tick), 이것 자체가 `_r` 을 쓰라는 신호다.

</details>

<details>
<summary>Q6. "재진입(reentrant)과 스레드 안전(thread-safe)은 같은 말인가요?" — RTOS 문맥에서 예를 들어 주세요.</summary>

아니다 (정의와 3축 분류는 C-A04). 스레드 안전 = 여러 태스크가 **동시에**(서로 기다릴 수 있는 상태로) 불러도 맞다. 재진입 = 한 호출이 끝나기 **전에** 같은 문맥에서 다시 불려도 맞다(재귀, 콜백, 그 태스크를 선점한 ISR). 이 레슨의 예: 세션 락으로 감싼 `parse_kv_locked` 와 `_reent` 판 `strtok` 은 태스크 간에는 안전하지만 재진입은 아니다 — 앞의 것은 ISR 이 락을 못 잡고 같은 태스크 중첩은 비재귀 락이면 교착, 뒤의 것은 ISR 이 L 의 상태를 덮었다(`y=2`). 비재귀 뮤텍스로 감싼 malloc 은 태스크 간 안전하지만 realloc 의 자기 재진입에서 교착했다. `fmt_u32_r`/`strtok_r` 처럼 상태를 전부 호출자에게 두면 둘 다 만족한다.

</details>

<details>
<summary>Q7. 라이브러리 전역 락을 넣었더니 고우선순위 태스크의 데드라인을 가끔 놓칩니다. 왜죠?</summary>

전역 락이 낮은 우선순위 L 의 임계 구역에 H 를 묶고, 그 사이에 락과 무관한 중간 우선순위 M 이 L 을 선점하면 H 는 M 이 끝날 때까지도 기다린다 — 무한정 우선순위 역전(RTOS-M03, Mars Pathfinder 와 같은 구조). 연습 4 실측: H 의 블록 = 4 + C_M (C_M = 0, 2, 4, 8 → 4, 6, 8, 12 tick). 처방은 ① 락을 없애는 것(`_r` 판은 C_M = 8 에서도 블록 0), ② 상속 뮤텍스(FreeRTOS 뮤텍스는 기본으로 상속, RTOS-M04), ③ 천장 프로토콜(RTOS-M05), ④ 임계 구역을 짧게(포맷은 락 밖에서 지역 버퍼로, 락 안에서는 복사만). 이 레슨의 `k_mutex` 는 일부러 상속이 없다.

</details>

<details>
<summary>Q8. 멀티코어(SMP) FreeRTOS 에서 configUSE_NEWLIB_REENTRANT 는 그대로 믿어도 되나요?</summary>

기본 매크로 그대로면 위험하다고 본다 (V11.3.2 소스 읽기 기준, 실측 아님). `_impure_ptr` 는 전역 하나인데, SMP 판 `vTaskSwitchContext(xCoreID)` (`tasks.c:5385–5389`) 도 그 코어의 TCB 로 같은 `configSET_TLS_BLOCK` 을 부른다 — 코어 1 의 스위치가 코어 0 이 쓰던 포인터를 덮는다. 필요한 것은 코어별 "현재 reent": newlib 을 `__DYNAMIC_REENT__` 로 빌드해 `__getreent()` 가 코어 ID 로 현재 TCB 의 블록을 돌려주게 하거나(`reent.h:815–819`), TLS 레지스터를 쓰는 picolibc 방식(`picolibc-freertos.h:87` 의 `_set_tls`). 그리고 SMP 에서는 "인터럽트 끄기 = 상호 배제" 가 깨지므로 malloc 락도 스핀락이 섞여야 한다 (RTOS-A08).

</details>

## 9. 추가 연습

practice.c 는 solution.c 의 미니 커널과 strtok 류를 그대로 가져오고, 커널에 **스케줄러 정지** (`k_sched_suspend/resume`, `vTaskSuspendAll` 대역: 카운터, 정지 중엔 지금 태스크가 계속 돌고 ISR 은 계속 실행)를 더했다.

### 연습 1 — UART 한 줄이 섞인다

L 은 `"L:3300\n"`, H 는 `"H:7\n"` 을 `uart_putc` (한 글자 = 한 tick, TX 레지스터 쓰기)로 내보낸다. H 를 tick 2 에 깨우면 UART 에 무엇이 찍히나? 한 줄 전체를 감싸는 `g_uart_mtx` 로 고치고 H 의 블록 tick 을 세라.

<details>
<summary>답 보기</summary>

락 없음: `"L:H:7\n3300\n"` — 함수는 각자 지역 상태만 쓰는데도 섞였다. 공유된 것은 **장치**(UART 데이터 레지스터)다. 줄 락: `"L:3300\nH:7\n"`, H 블록 5 tick (L 의 남은 5글자). newlib 의 `printf` 는 FILE 락(`_newlib_flockfile_start`)으로 **한 번의 printf 호출**을 원자적으로 만들 수 있지만 그것도 retarget 락이 있을 때뿐이고, 여러 `printf` 로 한 줄을 만들면 여전히 섞인다. 펌웨어의 정석은 **로거 태스크 하나 + 큐**: 다른 태스크는 메시지(또는 포맷 전 원시 값)를 큐에 넣고, UART 는 로거만 만진다 — 락도 없고 ISR 에서도 `xQueueSendFromISR` 로 넣을 수 있다.

</details>

### 연습 2 — errno 도 태스크별이어야 한다

`parse_u32(s, &out)` 은 넘치면 `REENT->err = ERANGE`, 성공하면 0. L 이 `"99999999999"` 를 파싱하고 결과를 확인하기 전에 선점되고, H 가 `"42"` 를 파싱한다. reent 옵션을 끈 경우와 켠 경우 L 이 보는 `err` 는?

<details>
<summary>답 보기</summary>

옵션 끔: `L err=0 H err=0` — H 의 성공이 L 의 `ERANGE` 를 지웠다. L 은 오버플로를 놓치고 `UINT32_MAX` 를 정상 값으로 쓴다. 옵션 켬: `L err=34 H err=0` (이 맥의 `ERANGE` = 34). 그래서 newlib 은 `errno` 를 `_reent` 안에 두고(`errno.c`: `return &_REENT_ERRNO(_REENT);`), FreeRTOS 는 newlib 없이도 `configUSE_POSIX_ERRNO` 로 `FreeRTOS_errno` 를 TCB 에 저장/복원한다(`tasks.c:5262–5266, 5282–5286`). ISR 이 errno 를 바꾸는 함수를 부르면? 선점한 태스크의 errno 가 바뀐다 — POSIX 가 시그널 핸들러에 errno 저장/복원을 요구하는 것과 같은 이유 (C-A04).

</details>

### 연습 3 — malloc 락: 스케줄러 정지 vs 뮤텍스

L 이 toy 힙에서 할당하며 4 tick 동안 리스트를 걷는다(락 보유). tick 1 에 깨는 H 는 **힙을 안 쓰는** 모터 제어(3 tick)다. 힙 락을 뮤텍스로 할 때와 스케줄러 정지로 할 때 H 가 끝나는 tick 은?

<details>
<summary>답 보기</summary>

```text
[3] heap lock = mutex                 [3] heap lock = scheduler suspend
tick : 0 1 2 3 4 5 6                  tick : 0 1 2 3 4 5 6
H    : . R R R . . .                  H    : . r r r R R R
L    : R r r r R R R                  L    : R R R R . . .
[3] H done tick: mutex=4 suspend=7
```

뮤텍스는 **경쟁자만** 막으므로 힙과 무관한 H 는 깨자마자 돈다(완료 4). 스케줄러 정지는 **모든 태스크 전환**을 막으므로 H 는 READY(`r`) 인 채 L 의 임계 구역 3 tick 을 기다린다(완료 7). 대신 정지는 카운터라 재귀가 공짜이고, 역전·상속 문제가 없고, 스케줄러 시작 전에도 의미가 있다. 결론: 임계 구역이 아주 짧으면 정지(`heap_3`, `heap_4` 의 선택), 길거나 실시간 태스크가 많으면 재귀 뮤텍스. 어느 쪽도 ISR 은 못 막는다 (FreeRTOS 의 `vTaskSuspendAll` 은 인터럽트를 끄지 않는다).

</details>

### 연습 4 — 라이브러리 락이 낳는 우선순위 역전

L 이 `parse_kv_locked` (세션 락)를 쥐고 있을 때 H(tick 2) 가 같은 락을 기다리고, tick 3 에 락과 무관한 M(우선순위 2, CPU 일 C_M tick)이 깬다. C_M = 0, 2, 4, 8 에 대해 H 의 블록 tick 을 재고, `_r` 판(태스크별 save)과 비교하라.

<details>
<summary>답 보기</summary>

```text
[4] lock, C_M=0: H blocked=4      [4] lock, C_M=4
[4] lock, C_M=2: H blocked=6      tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3
[4] lock, C_M=4: H blocked=8      H    : . . B B B B B B B B R R R R
[4] lock, C_M=8: H blocked=12     M    : . . . R R R R . . . . . . .
[4] _r,   C_M=8: H blocked=0      L    : R R R r r r r R R R r r r r
```

H 의 블록 = L 의 남은 세션 4 tick + C_M. M 은 락을 건드리지도 않는데 H 를 늦춘다 — 상속 없는 락에서의 무한정 역전(RTOS-M03 의 시나리오와 같은 모양). 상속 뮤텍스라면 L 이 H 의 우선순위를 빌려 M 보다 먼저 돌아 블록이 4 로 고정된다(RTOS-M04). 하지만 가장 좋은 답은 표의 마지막 줄: 상태를 호출자에게 두면 **락이 없으니 역전도 없다**. 면접에서 "왜 굳이 `_r`?" 의 가장 강한 대답이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 10/10 passed
```

## 10. 핵심 정리

- 공유 라이브러리 버그는 **선점이 위험 구간 안에 떨어질 때만** 터진다: 정적 버퍼는 호출 중간 ~ 결과 사용 끝, `strtok` 은 세션 전체(호출 사이), 힙은 리스트를 걷는 중간. 실측 sweep 8개 시점 중 5개에서 깨졌고, 이긴 쪽(H)은 항상 정상이다.
- 고치는 법 셋: **`_r`** (락 0, 블록 0, ISR 가능 — 1순위), **뮤텍스** (범위 = 세션/사용 끝까지, 높은 쪽이 남은 임계 구역만큼 블록, 역전 위험, ISR 불가), **태스크별 컨텍스트** (`_reent`, 스위치 때 포인터 하나, ISR·중첩·공유 장치는 못 지킴).
- 호출 단위 락은 `strtok` 을 못 고친다. 포인터를 돌려주는 래퍼도 틀렸다 — 락 안에서 복사.
- newlib `__malloc_lock` 은 **재귀**여야 한다 (`mlock.c` 문서, `_mallocr.c` realloc 이 락을 쥔 채 malloc/free). 기본 `sys/lock.h` 는 전부 `((void)0)` — 안 주면 락이 없다.
- `configUSE_NEWLIB_REENTRANT` = TCB 의 `xTLSBlock` + `vTaskSwitchContext` 의 `_impure_ptr = &xTLSBlock`. 힙은 보호하지 않고(락 따로), ISR 은 선점한 태스크의 것을 쓰고, SMP 에선 전역 포인터 하나가 문제.
- 힙 락: 스케줄러 정지(싸고 재귀 공짜, 무관한 태스크까지 멈춤) vs 재귀 뮤텍스(경쟁자만, 상속). 둘 다 ISR 은 못 막는다.
- 라이브러리 락은 우선순위 역전을 낳는다 (H 블록 = 4 + C_M). 락이 없는 설계가 최선.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `include/FreeRTOS.h:121–130, 149–170`, `include/newlib-freertos.h:32–60`, `include/picolibc-freertos.h:87`, `tasks.c:433–435, 1943–1946, 3789–3793, 5215–5292, 5300–5389, 6575–6578`, `portable/MemMang/heap_3.c:63–92` — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 설정 문서 `configUSE_NEWLIB_REENTRANT` — <https://www.freertos.org/a00110.html>
- newlib 소스 (newlib-cygwin git HEAD `b5a71199`, 2026-11-08 조회): `libc/string/strtok.c`, `libc/errno/errno.c`, `libc/reent/impure.c`, `libc/include/sys/reent.h`, `libc/include/sys/lock.h`, `libc/misc/lock.c`, `libc/stdlib/mlock.c`, `libc/stdlib/_mallocr.c`, `libc/stdlib/nano-mallocr.c`, `libc/stdio/local.h` — <https://sourceware.org/git/?p=newlib-cygwin.git>
- Dave Nadler, "newlib and FreeRTOS" (newlib-freertos.h 가 가리키는 글) — <http://www.nadler.com/embedded/newlibAndFreeRTOS.html>
- POSIX.1-2017 `strtok`/`strtok_r`, XBD 3.407 Thread-Safe — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/strtok.html>
- C11 (N1570) 7.24.5.8 strtok (p6 데이터 레이스 면제), 7.1.4 라이브러리 함수 사용 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Trans. Computers 39(9), 1990 — 연습 4 의 역전과 상속
- Zephyr `k_mutex`, `k_sched_lock` — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html>
- 관련 레슨: C-A04 (strtok 의미, 재진입/스레드/ISR 안전 3축, ISR 에서 malloc), C-M14 (ISR 공유 데이터, 임계 구역), C-M10 (재진입 가능한 포매터), RTOS-B10 (뮤텍스 vs 세마포어, ISR 에서 뮤텍스 불가), RTOS-M03 / RTOS-M04 / RTOS-M05 (역전·상속·천장), RTOS-A08 (SMP), RTOS-M10 (heap_1~5)
