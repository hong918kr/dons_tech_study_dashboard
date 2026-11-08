---
id: RTOS-B10
time: 60분
updated: 2026-11-08
---
# RTOS-B10 뮤텍스 vs 세마포어: 소유권과 재귀 뮤텍스

## 학습 목표

- 바이너리 세마포어·뮤텍스·재귀 뮤텍스의 차이를 **객체가 기억하는 정보(count / holder / depth)** 로 설명하고, 그 차이에서 "누가 give 할 수 있나", "ISR 에서 쓸 수 있나", "우선순위 상속이 되나" 를 끌어낼 수 있다.
- FreeRTOS V11.3.2 에서 **소유자가 아닌 태스크가 뮤텍스를 give** 하면 어떤 코드 경로를 타는지(`queue.c` → `tasks.c:6861` 의 `configASSERT`)를 실제로 돌려 보고, assert 를 끈 빌드에서는 무엇이 조용히 깨지는지 말할 수 있다.
- 재귀 뮤텍스의 중첩 take/give 를 깊이 카운터로 구현하고, 비재귀 뮤텍스를 같은 태스크가 두 번 잡으면 **자기 자신과 교착** 한다는 것을 실측으로 보인다.
- 바이너리 세마포어로 상호배제를 하면 생기는 세 가지 문제 — 아무나 풀 수 있다, 우선순위 상속이 없다, 처음엔 비어 있다 — 를 재현하고, 뮤텍스가 **ISR 에서 쓸 수 없는** 이유(태스크 신원이 없다 → 소유·상속·블록이 모두 불가)를 말할 수 있다.

## 1. 문제

"뮤텍스랑 바이너리 세마포어 차이가 뭐예요?" 는 RTOS 펌웨어 면접의 단골 첫 질문이다. "값이 0/1 인 건 같다" 에서 멈추면 떨어진다. 정답의 핵심은 **소유권(ownership)** 이고, 소유권이 있어야 **우선순위 상속(priority inheritance)** 이 가능하며, 소유자가 **태스크** 여야 하므로 **ISR 에서는 쓸 수 없다** — 이 세 문장이 한 줄로 이어진다.

STM32 보드 하나를 생각하자. I2C1 버스에 IMU 와 전원 IC 가 붙어 있고, 세 태스크가 버스를 같이 쓴다: 센서 태스크 L (prio 1, IMU 를 100 Hz 로 읽음), 로거 M (prio 2, CPU 를 오래 씀), 텔레메트리 H (prio 3, 전원 IC 상태를 주기적으로 보고). 버스 락을 `xSemaphoreCreateBinary()` 로 만들었는지 `xSemaphoreCreateMutex()` 로 만들었는지에 따라, 로거의 잘못된 에러 처리 코드 한 줄(남의 락을 give)이나 우선순위 역전이 시스템을 망가뜨리는지 아닌지가 갈린다. 그리고 UART RX ISR 이 "버스 락을 잡고 바로 I2C 로 쓰자" 고 하면 안 되는 이유도 여기서 나온다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">세 객체가 "기억하는 것" 이 다르다 → 할 수 있는 일이 다르다</text>
  <rect x="10" y="40" width="230" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">바이너리 세마포어</text>
  <text x="22" y="86" fill="currentColor" font-family="monospace">count : 0 / 1</text>
  <text x="22" y="108" fill="currentColor" font-family="monospace">waiters</text>
  <text x="22" y="136" fill="currentColor">누가 give? 아무나 (ISR 포함)</text>
  <text x="22" y="156" fill="currentColor">우선순위 상속: 없음</text>
  <text x="22" y="176" fill="currentColor">초기값: 0 (비어 있음)</text>
  <rect x="255" y="40" width="230" height="150" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="267" y="62" fill="currentColor" font-weight="bold">뮤텍스</text>
  <text x="267" y="86" fill="currentColor" font-family="monospace">count : 0 / 1</text>
  <text x="267" y="108" fill="currentColor" font-family="monospace">holder: 태스크 (TCB)</text>
  <text x="267" y="136" fill="currentColor">누가 give? holder 만</text>
  <text x="267" y="156" fill="currentColor">우선순위 상속: 있음</text>
  <text x="267" y="176" fill="currentColor">초기값: 1 (사용 가능)</text>
  <rect x="500" y="40" width="230" height="150" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="512" y="62" fill="currentColor" font-weight="bold">재귀 뮤텍스</text>
  <text x="512" y="86" fill="currentColor" font-family="monospace">count, holder</text>
  <text x="512" y="108" fill="currentColor" font-family="monospace">depth : 중첩 횟수</text>
  <text x="512" y="136" fill="currentColor">holder 가 다시 take → depth+1</text>
  <text x="512" y="156" fill="currentColor">depth 가 0 이 될 때 진짜 해제</text>
  <text x="512" y="176" fill="currentColor">상속: 있음 (뮤텍스와 같음)</text>
  <g fill="currentColor">
    <text x="10" y="220">holder 칸이 있어야 → "남이 give" 를 거부할 수 있고, "누구의 우선순위를 올릴지" 알 수 있다.</text>
    <text x="10" y="244">holder 는 태스크여야 한다 → ISR 은 holder 가 될 수 없다 (TCB 가 없고, 블록할 수도 없다).</text>
    <text x="10" y="268">세마포어는 holder 가 없다 → 신호용(ISR→태스크, RTOS-B08)으로는 이게 장점, 락으로는 결함.</text>
  </g>
</svg>
```

- **실측 (demo_freertos_*.c)**: FreeRTOS V11.3.2 커널을 macOS POSIX 포트로 빌드해 비소유자 give(assert 켬/끔), 재귀 뮤텍스 중첩, 바이너리 세마포어 상호배제를 실제로 돌린다. POSIX 포트는 태스크마다 pthread 하나를 쓰고 한 번에 하나만 실행시키는 에뮬레이션이라 **실시간성은 없다** — 보는 것은 반환값과 순서다.
- **체크 코드 (starter/solution/practice.c)**: 커널 없이 빌드되는 작은 **의미 모델** 을 직접 구현한다. 호출자는 태스크 번호 또는 `CTX_ISR`, "블록" 은 대기 리스트에 넣고 `K_BLOCKED` 반환, give 는 대기자에게 직접 넘긴다(hand-off). 모델은 문맥 전환을 하지 않으므로 결정적이고, 실측 결과와 같은 결론을 내야 한다.

### Part A — 뮤텍스 = 소유권이 있는 락

- `mutex_take(o, caller, may_block)`: ISR → `K_EISR`. 이미 내가 holder → `K_EDEADLK` (일반 뮤텍스 재진입). 비어 있으면 holder 기록 + `T[caller].held++`. 아니면 `K_EAGAIN` 또는 대기 리스트 + `K_BLOCKED`.
- `mutex_give(o, caller)`: ISR → `K_EISR`, holder 가 아니면 `K_EPERM` (**상태는 그대로**). 내려놓을 때 대기자가 있으면 우선순위 최고(같으면 먼저 온 쪽)에게 소유권을 넘긴다.
- 바이너리 세마포어(`sem_take/sem_give`)는 주어진다 — 테스트 앞부분이 "세마포어는 아무나 give 해도 통과" 를 먼저 보여 준다.

### Part B — 재귀 뮤텍스 = 깊이를 세는 뮤텍스

- `rmutex_take`: holder 가 다시 take → `depth++`. 처음 얻을 때 `depth = 1`. 중첩해도 `held` 는 1 (뮤텍스 1개 보유).
- `rmutex_give`: 비소유자 → `K_EPERM`. `--depth > 0` 이면 아직 잠김. 0 이 되면 진짜 해제, 넘겨받은 태스크는 depth 1.

### Part C — I2C 버스 락: 상속, 잘못된 해제, 다중 보유, ISR

- `pi_on_block(o, waiter)`: 대기자가 holder 보다 높으면 holder 우선순위를 올린다. holder 가 없으면(ISR 이 가져간 경우 등) 아무것도 안 한다.
- `pi_on_release(owner)`: 쥔 뮤텍스가 0개가 되면 base 우선순위로 복원 — FreeRTOS 의 규칙 그대로 (더 정밀한 규칙은 RTOS-M04).
- 시나리오: L 이 버스 락 → H 대기 → (뮤텍스) L 이 prio 3 으로 올라 M 이 못 끼어듦 / (세마포어) L 은 prio 1 그대로라 M 이 먼저 돈다 + M 의 잘못된 give 가 통과해 L 과 H 가 동시에 버스 사용. 뮤텍스 2개를 쥔 L 의 복원 시점. UART RX ISR 은 버스 뮤텍스 대신 세마포어로 태스크를 깨운다.

## 2. Problem (English)

> **Mutex vs. binary semaphore: ownership, recursion, and priority inheritance.**
>
> Model three RTOS sync objects in a single-threaded, deterministic simulator. A caller is a task id `0..NTASK-1` or `CTX_ISR` (no task identity). Blocking means "append to the object's wait list and return `K_BLOCKED`"; a later give hands the object directly to the best waiter (highest priority, FIFO among equals). Binary semaphore `sem_take/sem_give` is provided.
>
> 1. `int mutex_take(kobj_t *o, int caller, int may_block)` / `int mutex_give(kobj_t *o, int caller)` — record the holder; reject `CTX_ISR` (`K_EISR`), a non-owner give (`K_EPERM`, state unchanged), and a re-take by the holder (`K_EDEADLK`). Track `T[id].held` (number of mutexes held).
> 2. `int rmutex_take(...)` / `int rmutex_give(...)` — recursive mutex: nested takes by the holder increment `depth`; only when `depth` returns to 0 is the mutex released (and handed off, new holder `depth == 1`).
> 3. `void pi_on_block(kobj_t *o, int waiter)` / `void pi_on_release(int owner)` — FreeRTOS-style inheritance: raise the holder to the waiter's priority; restore `base_prio` only when the owner holds no more mutexes.
>
> Example: L(1) holds the bus mutex, H(3) blocks on it → `T[L].prio == 3`, the scheduler picks L over M(2). With a binary semaphore instead, `T[L].prio == 1`, M runs first, and `k_give(&bus, M)` succeeds — letting H in while L is still inside.
>
> Follow-ups: what does FreeRTOS do when a non-owner calls `xSemaphoreGive()` on a mutex, with and without `configASSERT`? Why can't an ISR take a mutex? When is a recursive mutex a design smell?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "28/60 passed" (세마포어 기준 테스트 + 우연히 맞는 것) — TODO 를 채워 60/60 을 만든다
```

시작 코드의 뮤텍스 함수는 **세마포어처럼** 동작한다(holder 를 기록하지 않음). 즉 "바이너리 세마포어를 락으로 쓴 프로젝트" 에서 출발하는 셈이고, 실패하는 테스트 목록이 그대로 세마포어의 결함 목록이다.

실측 데모 (FreeRTOS V11.3.2 + POSIX 포트, 커널 소스는 `~/.esn_scratch/FreeRTOS-Kernel`, 이 디렉터리의 `FreeRTOSConfig.h` / `hooks.c` 사용):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
frb() { cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils "$@" hooks.c \
        $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c; }
frb demo_freertos_owner.c -o demo_owner                       && ./demo_owner 1; ./demo_owner 2      # assert ON
frb -DDEMO_NO_ASSERT demo_freertos_owner.c -o demo_owner_na   && ./demo_owner_na 1; ./demo_owner_na 2
frb demo_freertos_recursive.c -o demo_rec                     && ./demo_rec
frb demo_freertos_binsem.c -o demo_binsem                     && ./demo_binsem 1; ./demo_binsem 2; ./demo_binsem 3
frb demo_freertos_isr.c -o demo_isr                           && ./demo_isr 1; ./demo_isr 2; ./demo_isr 3
frb -DDEMO_NO_ASSERT demo_freertos_isr.c -o demo_isr_na       && ./demo_isr_na 2
```

`FreeRTOSConfig.h` 는 오케스트레이터 템플릿에 두 가지를 더했다: `INCLUDE_xSemaphoreGetMutexHolder 1` (holder 를 출력하려고), 그리고 `-DDEMO_NO_ASSERT` 일 때 `configASSERT` 를 정의하지 않는 스위치 (정의가 없으면 `FreeRTOS.h` 가 빈 매크로로 만든다 = 양산 빌드에서 흔한 설정).

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 거부는 "상태를 바꾸기 전에"</summary>

`mutex_give` 의 첫 두 줄은 검사여야 한다: `caller == CTX_ISR`, `o->holder != caller`. 이 둘을 통과한 뒤에야 `held--`, 상속 복원, hand-off 를 한다. 검사보다 먼저 `held--` 를 하면 거부했는데 소유자의 카운터가 줄어드는 — FreeRTOS 를 assert 없이 돌렸을 때와 같은 — 오염이 생긴다.

</details>

<details>
<summary>힌트 2 — Part A: hand-off 때 바뀌는 것 세 가지</summary>

대기자에게 넘길 때: `o->holder = next`, `T[next].held++`, 그리고 `pop_best` 가 이미 `T[next].blocked_on = NULL` 로 만들어 준다. count 는 0 그대로 (빈 적이 없다). 대기자가 없을 때만 `holder = NO_TASK; count = 1`.

</details>

<details>
<summary>힌트 3 — Part B: depth 는 holder 만 건드린다</summary>

`rmutex_take` 는 "이미 내 것이면 depth++" 한 줄을 `mutex_take` 앞에 둔다. 처음 얻으면 `depth = 1`. `rmutex_give` 는 `--depth > 0` 이면 바로 OK, 0 이면 `mutex_give` 로 진짜 놓는다. 넘겨받은 태스크가 생겼다면 그 태스크의 depth 는 1 이다.

</details>

<details>
<summary>힌트 4 — Part C: 상속은 "블록하는 순간", 복원은 "마지막 뮤텍스를 놓는 순간"</summary>

`pi_on_block` 은 waiter 가 대기 리스트에 들어간 직후 호출된다: `T[h].prio = max(T[h].prio, T[waiter].prio)`. holder 가 `NO_TASK` 면 그냥 돌아간다. `pi_on_release` 는 `held` 가 이미 줄어든 뒤 호출되므로 `held == 0` 이면 `base_prio` 로. ISR 인 owner(`CTX_ISR` 은 음수)는 인덱스로 쓰면 안 된다.

</details>

## 5. 풀이

### 0. 실험 결과 표 (FreeRTOS V11.3.2 실측 + 모델)

환경: Apple M6, Apple clang 21.0.0, FreeRTOS-Kernel V11.3.2, POSIX/GCC 포트 + heap_3, `configTICK_RATE_HZ 1000`. "ret" 은 FreeRTOS 반환값 (`pdPASS = pdTRUE = 1`, `pdFAIL = errQUEUE_FULL = 0`, projdefs.h:59-62). tick 번호는 POSIX 포트 특성상 실행마다 ±1 흔들렸고(6회 중 1회), **순서와 반환값은 매번 같았다**. 16·17 행은 FromISR 함수를 태스크 문맥에서 직접 부른 것이다 (POSIX 포트에는 진짜 인터럽트가 없다) — 그 함수 안의 검사와 holder 기록 경로를 본 것이지 ISR 타이밍을 본 것은 아니다.

| # | 실험 | FreeRTOS, configASSERT ON | FreeRTOS, configASSERT OFF | 모델 (solution.c) |
|---|---|---|---|---|
| 1 | 뮤텍스: A 가 잡은 것을 B(비소유자) 가 give | `[configASSERT] tasks.c:6861` → abort, exit 134 | ret=1, holder→NULL, B 가 take 성공 → **in_cs=2** | `K_EPERM`, holder·count 불변 |
| 2 | 1 에 이어 A 가 give (holder 는 이제 B) | — | ret=1 — **A 가 B 의 락을 풀어 버림**. 이후 B 의 give ret=0 | — |
| 3 | 뮤텍스: L 이 잡고 H 가 대기(L 상속 3) 중 X 가 give | abort `tasks.c:6861` | L 의 우선순위가 **X 에 의해** 3→1 로 풀림, H 가 들어감 → in_cs=2, L 의 give ret=0 | `K_EPERM` |
| 4 | 재귀 뮤텍스 A 가 3중 take | ret 1,1,1 | (같음) | depth 3, `held == 1` |
| 5 | B 가 TakeRecursive(0) — depth 3 / depth 1 | ret 0 / 0 | | `K_EAGAIN` / `K_BLOCKED` |
| 6 | B(비소유자) 가 GiveRecursive | ret 0 (`pdFAIL`), **assert 없음** | | `K_EPERM` |
| 7 | A 가 depth 0 까지 give → B take | ret 1 (holder=B) | | B 에게 hand-off, depth 1 |
| 8 | A 의 초과 GiveRecursive | ret 0 | | `K_EPERM` |
| 9 | 일반 뮤텍스를 같은 태스크가 2번 take (timeout 5) | ret 0, **5 tick 기다린 뒤** (portMAX_DELAY 면 영원히) | | `K_EDEADLK` (즉시) |
| 10 | 일반 뮤텍스 give 2번 | ret 1, 0 | | `K_OK`, `K_EPERM` |
| 11 | 생성 직후 바이너리 세마포어 take(0) | ret 0 (비어 있음) | | `K_EAGAIN` |
| 12 | 바이너리 세마포어 give 2번 | ret 1, 0 (최대 1) | | `K_OK`, `K_EFULL` |
| 13 | 바이너리 세마포어 락: take 안 한 B 가 give → take | ret 1, 1 → **in_cs=2**, assert 없음 | (같음) | `K_OK` (설계상 허용) |
| 14 | L/M/H, 락 = 바이너리 세마포어 | L 최대 prio 1, 끝난 순서 **M H L**, H 대기 16 tick | | `T[L].prio == 1`, 스케줄러 → M |
| 15 | L/M/H, 락 = 뮤텍스 | L 최대 prio 3, 끝난 순서 **H M L**, H 대기 4 tick | | `T[L].prio == 3`, 스케줄러 → L |
| 16 | 빈 뮤텍스를 `xSemaphoreTakeFromISR` | ret 1, **holder=NULL**, assert 없음 → 아무 태스크 give ret=1 | | `K_EISR` |
| 17 | 태스크가 잡은 뮤텍스를 `xSemaphoreGiveFromISR` | abort `queue.c:1360` | ret=1, **count=1 인데 holder=A** (모순) | `K_EISR` |
| 18 | 바이너리 세마포어 `xSemaphoreGiveFromISR` | ret 1 → 태스크 take ret 1 | | `K_OK` |

### Part A — 소유권: 비소유자 give 는 어디서 걸리나

`xSemaphoreGive(m)` 은 `xQueueGenericSend(m, NULL, 0, queueSEND_TO_BACK)` 매크로다 (semphr.h:460). 뮤텍스도 "길이 1, 아이템 크기 0 인 큐" 이고, 차이는 `uxQueueType == queueQUEUE_IS_MUTEX` 와 `u.xSemaphore.xMutexHolder` 칸뿐이다 (queue.c:617-635 `prvInitialiseMutex`: holder=NULL, 그리고 생성 시 한 번 give 해서 **사용 가능 상태로 시작**).

비소유자 give 의 경로:

| 단계 | 위치 (V11.3.2) | 하는 일 |
|---|---|---|
| ① | queue.c:977 | `uxMessagesWaiting < uxLength` — 뮤텍스가 잡혀 있으면 0 &lt; 1 이라 "공간 있음" |
| ② | queue.c:1043 → 2395 `prvCopyDataToQueue` | 아이템 크기 0 + 뮤텍스면 |
| ③ | queue.c:2410-2414 | `xTaskPriorityDisinherit(xMutexHolder)` 호출 후 `xMutexHolder = NULL` |
| ④ | tasks.c:6861 | `configASSERT( pxTCB == pxCurrentTCB );` — holder 가 지금 실행 중인 태스크(=호출자)가 아니면 여기서 걸린다 |
| ⑤ | tasks.c:6862-6863 | (assert 없으면) **holder 의** `uxMutexesHeld--` |
| ⑥ | tasks.c:6866-6890 | holder 가 상속 중이고 `uxMutexesHeld == 0` 이면 holder 를 base 우선순위로 되돌려 ready 리스트에 다시 넣음 |

중요한 사실: **give 쪽에는 "소유자인가?" 를 검사해서 에러를 돌려주는 코드가 없다.** 유일한 방어선은 ④ 의 `configASSERT` 이고, 그 assert 의 원래 목적도 소유권 검사라기보다 "상속을 풀 때 대상은 실행 중인 태스크여야 한다" 는 내부 불변식 확인이다 (주석: "if a mutex is given by the holding task then it must be the running state task", tasks.c:6857-6860). assert 를 끈 양산 빌드에서는 give 가 `pdPASS` 를 돌려주며 조용히 통과한다.

<!-- code: demo_freertos_owner.c -->

`demo_freertos_owner.c` 실제 출력:

```text
build: configASSERT ON, scenario 1
t= 1 A  take (enter CS)                    ret=1  holder=A    in_cs=1
t= 3 B  sees A holding                     ret=0  holder=A    in_cs=1
[configASSERT] /Users/donh/.esn_scratch/FreeRTOS-Kernel/tasks.c:6861
exit=134

build: configASSERT OFF, scenario 1
t= 1 A  take (enter CS)                    ret=1  holder=A    in_cs=1
t= 3 B  sees A holding                     ret=0  holder=A    in_cs=1
t= 3 B  give WITHOUT owning                ret=1  holder=NULL in_cs=1
t= 3 B  take (enter CS)                    ret=1  holder=B    in_cs=2
t=11 A  still in CS, back from work        ret=0  holder=B    in_cs=2
t=11 A  give (leave CS)                    ret=1  holder=NULL in_cs=1
t=18 B  give (leave CS)                    ret=0  holder=NULL in_cs=0
done
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">configASSERT OFF: B 의 비소유자 give 한 번으로 상호배제가 깨진다 (demo_owner_na 1 실측)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="100" y="48">0</text><text x="186" y="48">3</text><text x="402" y="48">11</text><text x="591" y="48">18</text><text x="690" y="48">tick</text>
  </g>
  <line x1="100" y1="54" x2="720" y2="54" stroke="currentColor"/>
  <text x="10" y="84" fill="currentColor">A (prio 2)</text>
  <rect x="127" y="68" width="280" height="26" class="f-acc" stroke="currentColor"/>
  <text x="137" y="86" fill="currentColor">A 는 자기가 CS 안에 있다고 믿음</text>
  <text x="10" y="128" fill="currentColor">B (prio 3)</text>
  <rect x="181" y="112" width="416" height="26" class="f-acc" stroke="currentColor"/>
  <text x="430" y="130" fill="currentColor">B 도 CS 안</text>
  <rect x="181" y="150" width="226" height="26" class="f-warn" stroke="currentColor"/>
  <text x="191" y="168" fill="currentColor">in_cs = 2 (겹침)</text>
  <g fill="currentColor" font-size="12">
    <text x="104" y="200">t1 A take</text>
    <text x="190" y="200">t3 B give(ret1) → take(ret1)</text>
    <text x="410" y="200">t11 A give(ret1): B 의 락을 풂</text>
    <text x="560" y="218">t18 B give ret0</text>
  </g>
  <rect x="10" y="230" width="720" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="251" fill="currentColor">configASSERT ON 이면 t3 의 give 에서 tasks.c:6861 abort — 개발 빌드에선 바로 잡힌다.</text>
  <text x="22" y="272" fill="currentColor">assert 를 끈 양산 빌드에선 ret=1 로 조용히 통과하고, 장부(holder, uxMutexesHeld)가 한 칸씩 밀린다.</text>
</svg>
```

assert 를 끄면 깨지는 것은 "동시 진입" 하나가 아니다. ⑤ 에서 **A 의** `uxMutexesHeld` 가 줄었고, t=11 에 A 가 give 할 때 holder 는 B 이므로 이번엔 ⑤ 가 **B 의** 카운터를 줄인다. 마지막 B 의 give 는 이미 "사용 가능" 상태라 ① 에서 공간이 없어 `errQUEUE_FULL`(0) 로 돌아온다 (queue.c:1084-1096). 장부가 한 칸씩 밀려 있다.

시나리오 2 는 더 나쁘다 — 상속이 걸린 상태에서 제3자가 give 하면 ⑥ 이 **남의 상속을 풀어 버린다**:

```text
build: configASSERT OFF, scenario 2
t= 1 L  take (enter CS)                    ret=1  holder=L    in_cs=1
t= 3 H  take -> blocks                     ret=0  holder=L    in_cs=1
t= 5 X  L prio before = 3 (inherited from H)
t= 5 X  give WITHOUT owning                ret=1  holder=NULL in_cs=1
t= 5 X  L prio after  = 1
t= 5 H  got it (enter CS)                  ret=1  holder=H    in_cs=2
t= 8 H  give (leave CS)                    ret=1  holder=NULL in_cs=1
t= 9 L  my prio=1 (base 1)
t= 9 L  give (leave CS)                    ret=0  holder=NULL in_cs=0
```

assert ON 빌드는 `t= 4 X L prio before = 3` 까지 찍고 같은 `tasks.c:6861` 에서 abort 했다. OFF 빌드에서는 X 는 L 이 아닌데, `xTaskPriorityDisinherit(L)` 이 L 을 ready 리스트에서 빼서 prio 1 로 다시 넣었다. "holder 는 지금 실행 중인 태스크" 라는 전제(④)가 깨진 채로 리스트 조작이 일어난 것이다. 소스상으로는 L 이 락을 쥔 채 `vTaskDelay` 로 지연 리스트에 있었다면 `uxListRemove` + `prvAddTaskToReadyList` 가 L 을 지연이 끝나기 전에 ready 로 옮기게 된다 (소스 읽기로 추론, 이 데모에서 재현하지 않음).

**모델의 선택:** solution.c 는 비소유자 give 를 `K_EPERM` 으로 거부하고 상태를 바꾸지 않는다. 이것은 FreeRTOS 가 아니라 POSIX `PTHREAD_MUTEX_ERRORCHECK` (`EPERM`) 와 Zephyr `k_mutex_unlock()` (`-EPERM`, 문서 기준) 의 동작이다. FreeRTOS 는 "검사하지 않고, 디버그 빌드에서만 assert" 이고, 그 차이가 표의 1~3 행이다.

hand-off 규칙도 모델에 넣었다: give 할 때 대기자가 있으면 **우선순위 최고, 같으면 먼저 온 쪽** 에게 바로 소유권을 넘긴다 (테스트: A 가 give → C(3), C → B(2, 먼저 옴), B → D(2)). FreeRTOS 이벤트 리스트도 우선순위 순 + 같은 우선순위는 삽입 순이다. 다만 FreeRTOS 는 깨운 태스크가 다시 실행되어 take 를 **재시도** 하는 구조라 그 사이에 더 높은 태스크가 가로챌 수 있다 — 모델의 "직접 넘김" 은 그 틈을 없앤 단순화다.

### Part B — 재귀 뮤텍스: 깊이를 센다

<!-- code: demo_freertos_recursive.c -->

실제 출력 (configASSERT ON, 5회 동일):

```text
A  TakeRecursive #1                     ret=1  holder=A
A  TakeRecursive #2                     ret=1  holder=A
A  TakeRecursive #3                     ret=1  holder=A
B  TakeRecursive(0) while depth 3       ret=0  holder=A
B  GiveRecursive WITHOUT owning         ret=0  holder=A
A  GiveRecursive (depth 3->2)           ret=1  holder=A
A  GiveRecursive (depth 2->1)           ret=1  holder=A
B  TakeRecursive(0) while depth 1       ret=0  holder=A
A  GiveRecursive (depth 1->0)           ret=1  holder=NULL
B  TakeRecursive(0) after depth 0       ret=1  holder=B
B  GiveRecursive (B is holder)          ret=1  holder=NULL
A  GiveRecursive (extra, not holder)    ret=0  holder=NULL
A  plain mutex take #1 ret=1
A  plain mutex take #2 (timeout 5) ret=0 after 5 ticks  <- 자기 자신을 기다림
A  plain mutex give  ret=1, give again ret=0
done
```

소스와 대조 (queue.c):

| 함수 | 줄 | 동작 |
|---|---|---|
| `xQueueTakeMutexRecursive` | 833-838 | holder == 현재 태스크 → `uxRecursiveCallCount++`, `pdPASS` (큐를 건드리지 않음) |
| | 844-851 | 아니면 `xQueueSemaphoreTake` 로 진짜 얻고, 성공하면 count++ |
| `xQueueGiveMutexRecursive` | 774-783 | holder == 현재 태스크일 때만 `uxRecursiveCallCount--` |
| | 786-790 | 0 이 되면 `xQueueGenericSend` 로 진짜 해제 (대기자 깨움) |
| | 795-800 | 아니면 `pdFAIL` — **비소유자 give 를 정상적으로 거부하는 유일한 FreeRTOS API** |

재미있는 비대칭: 재귀 뮤텍스는 소유권 검사가 "에러 반환" 으로 들어 있고(6행), 일반 뮤텍스는 assert 뿐이다(1행). 재귀 깊이를 holder 만 바꿀 수 있어야 하니 검사가 필수였던 것이다.

중첩해도 `uxMutexesHeld` 는 1 이다 — 진짜 take 는 첫 번째 한 번뿐이고(844행 경로), 나머지는 카운터만 올리기 때문이다. 모델도 `T[A].held == 1` 을 확인한다. 이 값이 상속 복원 시점(Part C)을 결정한다.

**일반 뮤텍스의 자기 교착:** 같은 태스크가 두 번 take 하면 FreeRTOS 는 검사 없이 그냥 블록한다 — 자기가 give 해야 풀리는 것을 자기가 기다린다. 데모는 timeout 5 tick 으로 돌려 "5 tick 뒤 실패" 를 봤고, `portMAX_DELAY` 였다면 그 태스크는 영원히 멈춘다 (우선순위 상속도 자기 자신에게는 의미가 없다). 모델은 `K_EDEADLK` 로 즉시 알려 준다 (POSIX ERRORCHECK 의 `EDEADLK` 와 같다).

**언제 재귀 뮤텍스를 쓰나:** 공개 API 가 각자 락을 잡는 드라이버에서 API 가 API 를 부를 때 (예: `i2c_rmw()` 가 `i2c_read()`/`i2c_write()` 를 호출 — 추가 연습 4). 하지만 많은 팀이 "재귀 뮤텍스 = 락 설계를 안 했다는 신호" 로 본다: 락을 잡은 채 어느 깊이에서 무엇이 불리는지 숨기고, 조건 변수와 섞으면(대기 중에 한 단계만 풀림) 교착한다. 보통의 대안은 "락을 잡는 공개 함수 + 락 없이 도는 내부 `_locked` 함수" 로 나누는 것이다.

### Part C — 세마포어로 상호배제를 하면: 세 가지 문제

<!-- code: demo_freertos_binsem.c -->

**문제 1·2 — 처음엔 비어 있고, 아무나 풀 수 있다** (`./demo_binsem 1`, assert ON):

```text
A  take on fresh binary sem (timeout 0)  ret=0   <- 처음엔 비어 있다
A  give (make it 'unlocked')             ret=1
A  give again (already 1)                ret=0   <- 최대 1, 넘치면 실패
A  take = lock                           ret=1   in_cs=1
B  give WITHOUT having taken             ret=1   <- 세마포어엔 소유자가 없다 (assert 도 없음)
B  take = lock                           ret=1   in_cs=2   <- 상호배제 깨짐
A  back from work, still in CS                    in_cs=2
A  give = unlock                         ret=1
B  give = unlock                         ret=0
done
```

`xSemaphoreCreateBinary()` 는 길이 1 짜리 빈 큐다 (semphr.h:167). 락으로 쓰려면 생성 직후 한 번 give 해야 하고, 잊으면 첫 take 가 영원히 블록한다 — 실무에서 실제로 자주 보는 부팅 행(hang)이다. 뮤텍스는 `prvInitialiseMutex` 가 생성 시 give 해 둔다 (queue.c:633-634). 그리고 B 의 give 는 **assert 를 켜 둔 빌드에서도** 통과했다. 세마포어에는 검사할 holder 칸이 아예 없다.

**문제 3 — 우선순위 상속이 없다** (`./demo_binsem 2` vs `3`, 각 5회 같은 순서):

```text
$ ./demo_binsem 2
lock = binary semaphore
t= 1 L takes lock
t= 3 H requests lock
t= 4 M starts 15 ticks of CPU work
t=19 M done
t=19 H got lock after 16 ticks
t=19 L gives lock (max prio seen while holding = 1)
finish order: M H L
$ ./demo_binsem 3
lock = mutex
t= 1 L takes lock
t= 3 H requests lock
t= 7 H got lock after 4 ticks
t= 7 M starts 15 ticks of CPU work
t=22 M done
t=22 L gives lock (max prio seen while holding = 3)
finish order: H M L
```

("L gives lock" 줄이 H 보다 늦게 찍힌 것은, L 의 give 가 H 를 깨우자 H 가 즉시 선점해 먼저 출력했기 때문이다. 뮤텍스 쪽의 `t=22` 는 L 이 M 이 끝난 뒤에야 다시 실행되어 출력한 시각이다. 실제 give 는 t=7.)

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">demo_binsem 2 / 3 실측 타임라인 (1 칸 = 1 tick, ±1 tick 흔들림)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="96" y="46">0</text><text x="148" y="46">2</text><text x="200" y="46">4</text><text x="278" y="46">7</text>
    <text x="388" y="46">11</text><text x="588" y="46">19</text><text x="666" y="46">22</text>
  </g>
  <text x="10" y="70" fill="currentColor" font-weight="bold">락 = 세마포어</text>
  <text x="10" y="92" fill="currentColor">L (1)</text>
  <rect x="126" y="78" width="78" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="204" y="78" width="390" height="20" class="f-mut" stroke="currentColor"/>
  <text x="300" y="93" fill="currentColor" font-size="12">락을 쥔 채 선점당함 (prio 1 그대로)</text>
  <text x="10" y="118" fill="currentColor">M (2)</text>
  <rect x="204" y="104" width="390" height="20" class="f-acc" stroke="currentColor"/>
  <text x="330" y="119" fill="currentColor" font-size="12">M 15 tick 실행</text>
  <text x="10" y="144" fill="currentColor">H (3)</text>
  <rect x="178" y="130" width="416" height="20" class="f-warn" stroke="currentColor"/>
  <text x="270" y="145" fill="currentColor" font-size="12">B: 락 대기 16 tick (M 길이에 비례)</text>
  <rect x="594" y="130" width="14" height="20" class="f-acc" stroke="currentColor"/>
  <text x="10" y="196" fill="currentColor" font-weight="bold">락 = 뮤텍스</text>
  <text x="10" y="218" fill="currentColor">L (1→3)</text>
  <rect x="126" y="204" width="156" height="20" class="f-acc" stroke="currentColor"/>
  <text x="182" y="219" fill="currentColor" font-size="12">prio 3</text>
  <rect x="282" y="204" width="390" height="20" class="f-mut" stroke="currentColor"/>
  <text x="380" y="219" fill="currentColor" font-size="12">락 반납 후 ready (prio 1)</text>
  <text x="10" y="244" fill="currentColor">M (2)</text>
  <rect x="178" y="230" width="104" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="282" y="230" width="390" height="20" class="f-acc" stroke="currentColor"/>
  <text x="400" y="245" fill="currentColor" font-size="12">M 15 tick 실행</text>
  <text x="10" y="270" fill="currentColor">H (3)</text>
  <rect x="178" y="256" width="104" height="20" class="f-warn" stroke="currentColor"/>
  <text x="186" y="271" fill="currentColor" font-size="12">B 4 tick</text>
  <rect x="282" y="256" width="14" height="20" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <rect x="10" y="292" width="14" height="14" class="f-acc" stroke="currentColor"/><text x="30" y="304">실행</text>
    <rect x="80" y="292" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="100" y="304">락 대기(blocked)</text>
    <rect x="220" y="292" width="14" height="14" class="f-mut" stroke="currentColor"/><text x="240" y="304">ready, 실행 못 함</text>
    <text x="10" y="330">H 대기: 세마포어 16 tick (M 의 실행 길이만큼 늘어남) vs 뮤텍스 4 tick (= L 의 남은 임계 구역)</text>
  </g>
</svg>
```

세마포어 쪽이 **우선순위 역전(priority inversion)** 이다: H 는 L 을 기다리는데 L 보다 높은 M 이 L 을 선점하므로, H 는 자기와 상관없는 M 의 실행 길이만큼 더 기다린다. M 이 길어지면 H 의 대기도 그만큼 늘어난다 (unbounded) — 그 비례 관계를 C_M 값별로 재는 것과 Mars Pathfinder 사례는 RTOS-M03, 상속 구현 자체(다중 뮤텍스·연쇄)는 RTOS-M04 의 몫이다. FreeRTOS 의 바이너리 세마포어 문서도 이유를 그대로 적어 둔다: "the semaphore need not be given back once obtained ... For this reason this type of semaphore does not use a priority inheritance mechanism" (semphr.h:68-71).

뮤텍스 쪽에서는 H 가 블록하는 순간 `xQueueSemaphoreTake` 가 `xTaskPriorityInherit(xMutexHolder)` 를 부른다 (queue.c:1791-1795). holder 가 있어야 "누구를 올릴지" 안다. 모델의 `pi_on_block` 이 이 한 줄이고, 테스트 `T[L].prio == 3` → `pick_ready(L|M|H) == L` 이 타임라인의 "M 이 끼어들지 못함" 을 상태로 확인한다.

**복원 시점 — FreeRTOS 의 보수적 규칙:** tasks.c:6870 `if( pxTCB->uxMutexesHeld == 0 )` — 상속받은 우선순위는 **쥔 뮤텍스를 모두 놓을 때** 풀린다. 모델 테스트 (3): L 이 bus+spi 를 쥔 채 H 가 bus 를 기다림 → L 이 bus 를 놓아도 prio 3 유지 → spi 까지 놓아야 1. 정확히는 "남은 뮤텍스의 대기자 중 최고 우선순위" 로 내려가야 하지만, FreeRTOS 는 이 경우 필요 이상으로 오래 높게 둔다 (안전한 쪽 오차). 정확한 계산은 RTOS-M04.

### ISR 에서 뮤텍스를 쓸 수 없는 이유

<!-- code: demo_freertos_isr.c -->

```text
$ ./demo_isr 1
"ISR" TakeFromISR(mutex)  ret=1  holder=NULL
A     Take(mutex, 0)        ret=0  (mutex is taken, but by nobody)
A     Give(mutex) as non-owner ret=1  holder=NULL   <- 검사할 소유자가 없음
done
$ ./demo_isr 2                                   # configASSERT ON
A     Take(mutex)           ret=1  holder=A
"ISR" GiveFromISR(mutex) ...
[configASSERT] /Users/donh/.esn_scratch/FreeRTOS-Kernel/queue.c:1360
$ ./demo_isr_na 2                                # configASSERT OFF
A     Take(mutex)           ret=1  holder=A
"ISR" GiveFromISR(mutex) ...
"ISR" GiveFromISR(mutex)  ret=1  holder=A  count=1  <- 비었다는데 소유자는 A
done
$ ./demo_isr 3
"ISR" GiveFromISR(binary) ret=1  woken=0
A     Take(binary, 0)       ret=1
done
```

세 가지 이유가 모두 "ISR 에는 태스크 신원(TCB)이 없다" 에서 나온다:

| 뮤텍스에 필요한 것 | ISR 에서 | 실측 / 소스 |
|---|---|---|
| **소유자** 기록 | ISR 은 태스크가 아니다. `xQueueReceiveFromISR` 은 count 만 줄이고 holder 를 기록하지 않는다 | demo_isr 1: holder=NULL (queue.c:2082-2083). 이후 아무 태스크의 give 가 통과 |
| **우선순위 상속** | ISR 의 "우선순위" 는 NVIC 우선순위이지 태스크 우선순위가 아니다. 올려 줄 대상도 없다 | tasks.c:6752 주석 "If the mutex is taken by an interrupt, the mutex holder is NULL. Priority inheritance is not applied" |
| **블록** (잡혀 있으면 기다림) | ISR 은 기다릴 수 없다. 기다리는 동안 그 ISR 아래의 모든 것이 멈춘다 | FromISR API 에는 timeout 인자 자체가 없다 |
| 소유자만 give | ISR 은 소유자가 될 수 없으니 ISR 의 give 는 항상 "남의 give" | demo_isr 2: `configASSERT` queue.c:1360. assert 를 끄면 count=1 인데 holder=A — "비었다면서 주인이 있는" 모순 상태 |

FreeRTOS 문서도 명시한다: "Mutex type semaphores cannot be used from within interrupt service routines" (semphr.h:703), FromISR 매크로 설명 "Mutex type semaphores ... must not be used with this macro" (semphr.h:562, 655). 주의할 점은 **TakeFromISR 에는 assert 가 없다** 는 것 — 실측 1 처럼 조용히 "주인 없는 잠긴 뮤텍스" 가 된다. 문서를 믿고 코드 리뷰로 막아야 하는 부분이다.

**그럼 ISR 의 데이터는 어떻게 보호하나:** ① ISR 은 바이너리/카운팅 세마포어나 태스크 알림으로 태스크를 깨우기만 하고 (RTOS-B08, RTOS-M01), 공유 자원(I2C, 로그 버퍼)은 그 태스크가 뮤텍스로 보호한다 — 추가 연습 3. ② ISR 과 태스크가 직접 공유하는 짧은 데이터는 뮤텍스가 아니라 인터럽트 마스킹 임계 구역(`taskENTER_CRITICAL()` / ISR 쪽 `taskENTER_CRITICAL_FROM_ISR()`) 이나 lock-free 링 버퍼로 (C-M14).

### 모델 ↔ 실제 API 대응표

| 이 레슨 모델 | FreeRTOS | Zephyr (문서 기준) | POSIX | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|---|
| `OBJ_BINSEM` take/give | `xSemaphoreCreateBinary` / `xSemaphoreTake` / `xSemaphoreGive` | `k_sem_init(&s,0,1)` / `k_sem_take` / `k_sem_give` | `sem_wait`/`sem_post` (macOS 는 unnamed 미지원) | `osSemaphoreNew(1,0,..)` |
| ISR 의 give | `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR(woken)` | `k_sem_give` (ISR 가능) | `sem_post` 는 async-signal-safe | `osSemaphoreRelease` (ISR 가능) |
| `OBJ_MUTEX` | `xSemaphoreCreateMutex` (상속 포함) | `k_mutex` (상속 포함, **기본이 재귀**) | `PTHREAD_MUTEX_ERRORCHECK` (+`PTHREAD_PRIO_INHERIT`) | `osMutexNew` + `osMutexPrioInherit` |
| `OBJ_RMUTEX` | `xSemaphoreCreateRecursiveMutex` + `...TakeRecursive/GiveRecursive` | `k_mutex` 그대로 (lock_count) | `PTHREAD_MUTEX_RECURSIVE` | `osMutexRecursive` 속성 |
| `K_EPERM` (비소유자 give) | 일반: 검사 없음, `configASSERT` 만 (tasks.c:6861). 재귀: `pdFAIL` | `-EPERM` | `EPERM` (ERRORCHECK/RECURSIVE), NORMAL/DEFAULT 는 UB | `osErrorResource` |
| `K_EDEADLK` (재진입) | 검사 없음 → timeout 까지 블록 | 해당 없음 (재귀 허용) | `EDEADLK` (ERRORCHECK) | 구현 의존 (미검증) |
| `K_EISR` | 문서상 금지, GiveFromISR 만 assert (queue.c:1360) | 문서상 ISR 금지 | 시그널 핸들러에서 mutex 는 async-signal-safe 아님 | `osErrorISR` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 60/60 passed (5회 반복 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 60/60, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o sol_tsan && ./sol_tsan       # 60/60, 보고 없음 (단일 스레드)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 버스 락을 `xSemaphoreCreateBinary()` 로 | 소유자 없음 → 아무나 풀고(13행), 상속 없음 → 우선순위 역전(14행) | `xSemaphoreCreateMutex()` |
| 바이너리 세마포어 락을 만들고 초기 give 를 잊음 | 생성 직후 비어 있다(11행) → 첫 take 에서 부팅 행 | 뮤텍스를 쓰거나 생성 직후 `xSemaphoreGive()` |
| 에러 처리 경로에서 "혹시 모르니" 락을 give | 내가 안 잡았으면 남의 락을 푼다. FreeRTOS 는 디버그 빌드에서만 assert, 양산 빌드에선 in_cs=2 (1~3행) | 락을 잡은 함수 안에서만, goto 정리 경로로 정확히 1번 (추가 연습 4) |
| `configASSERT` 를 양산 빌드에서 끄고 끝 | 비소유자 give, ISR give 같은 계약 위반이 조용히 통과해 장부가 밀린다 | 개발·테스트 빌드는 반드시 assert ON. 양산에서도 가벼운 assert 핸들러(로그 + 리셋)를 고려 |
| 같은 태스크가 일반 뮤텍스를 다시 take | 자기 자신과 교착 (9행: timeout 이 없으면 영원히) | 재귀 뮤텍스, 또는 `_locked` 내부 함수로 분리 |
| `xSemaphoreTake` / `xSemaphoreGive` 를 재귀 뮤텍스에 섞어 씀 | 재귀 카운트를 우회 — 문서상 금지 (semphr.h:824 "xSemaphoreTake() and xSemaphoreGive() macros must not be used") | 재귀 뮤텍스는 `...TakeRecursive/GiveRecursive` 만 |
| ISR 에서 `xSemaphoreTakeFromISR(mutex)` | assert 없이 통과해 holder=NULL 인 잠긴 뮤텍스가 된다 (16행) | ISR 은 세마포어/알림으로 태스크를 깨우고, 태스크가 뮤텍스 |
| 재귀 뮤텍스의 에러 경로에서 give 누락 | depth 가 1 남아 다른 태스크가 영원히 못 잡는다 (추가 연습 4) | 모든 경로가 하나의 정리 지점을 지나게 |

## 8. 면접 Q&A

<details>
<summary>Q1. 뮤텍스와 바이너리 세마포어의 차이를 설명해 보세요.</summary>

값이 0/1 인 것은 같지만 뮤텍스는 **소유자(holder)** 를 기억한다. 그래서 ① 잡은 태스크만 풀 수 있고(소유권), ② 높은 태스크가 기다리면 소유자의 우선순위를 올릴 수 있으며(우선순위 상속), ③ 소유자는 태스크여야 하므로 ISR 에서는 쓸 수 없다. 세마포어는 소유자가 없어서 ISR 이 give 하고 태스크가 take 하는 **신호** 용으로 쓴다. 용도로 요약하면 "뮤텍스 = 상호배제(lock), 세마포어 = 동기화(signal)". 꼬리: "그래도 바이너리 세마포어로 락을 만들면 동작은 하지 않나?" → 정상 경로에선 동작하지만 남이 풀어도 막지 못하고, 상속이 없어 우선순위 역전에 무방비하다 (실측: 같은 시나리오에서 H 대기 16 tick vs 4 tick).

</details>

<details>
<summary>Q2. FreeRTOS 에서 뮤텍스를 잡지 않은 태스크가 xSemaphoreGive 를 부르면 어떻게 되나요?</summary>

give 경로에는 소유자 검사 후 에러를 돌려주는 코드가 없다. `prvCopyDataToQueue` 가 `xTaskPriorityDisinherit(holder)` 를 부르고 holder 를 NULL 로 만든다 (queue.c:2410-2414). 그 안의 `configASSERT( pxTCB == pxCurrentTCB )` (tasks.c:6861) 가 유일한 방어선이라, configASSERT 를 정의한 빌드에서는 거기서 멈추고, 정의하지 않은 빌드에서는 `pdPASS` 를 돌려주며 통과한다 — 실측으로 다른 태스크가 바로 take 해 두 태스크가 동시에 임계 구역에 들어갔고, 상속 중이었다면 제3자가 소유자의 상속을 풀어 버렸다. 꼬리: "재귀 뮤텍스는?" → `xSemaphoreGiveRecursive` 는 holder 를 검사해 `pdFAIL` 을 돌려준다 (queue.c:774, 795-800).

</details>

<details>
<summary>Q3. 왜 ISR 에서 뮤텍스를 쓸 수 없나요? 그럼 ISR 과 공유하는 자원은 어떻게 보호하죠?</summary>

뮤텍스의 세 기능이 모두 "태스크" 를 전제한다. 소유자로 기록할 TCB 가 ISR 에는 없고, 상속으로 올려 줄 태스크 우선순위도 없으며(ISR 우선순위는 NVIC 레벨), 잡혀 있을 때 기다리는 것(블록)은 ISR 에서 불가능하다. FreeRTOS 는 `xSemaphoreGiveFromISR` 에 holder 가 있으면 assert (queue.c:1360) 하고, TakeFromISR 는 assert 없이 holder=NULL 로 남긴다(실측). 보호 방법: ① ISR 은 세마포어/태스크 알림으로 태스크를 깨우고 실제 자원 접근은 태스크가 뮤텍스로 ② ISR 과 직접 공유하는 짧은 데이터는 인터럽트 마스킹 임계 구역(`taskENTER_CRITICAL_FROM_ISR`, PRIMASK/BASEPRI) 이나 lock-free 링 버퍼 (C-M14). 꼬리: "임계 구역도 결국 락 아닌가?" → 소유자·대기가 없는 "선점 자체를 막는" 방식이라 ISR 에서도 쓸 수 있지만, 길면 인터럽트 지연이 늘어난다.

</details>

<details>
<summary>Q4. 재귀 뮤텍스는 언제 쓰고, 왜 경계하나요?</summary>

같은 태스크가 이미 잡은 락을 다시 잡아야 할 때 — 공개 API 마다 락을 잡는 드라이버에서 `i2c_rmw()` 가 `i2c_read()`/`i2c_write()` 를 부르는 식. 깊이를 세서 마지막 give 에서만 풀린다. 일반 뮤텍스였다면 두 번째 take 에서 자기 자신을 기다리며 멈춘다(실측: timeout 5 → 5 tick 후 실패). 경계하는 이유: 락을 잡은 채 어디까지 불리는지가 숨겨지고, 깊이가 하나라도 새면(에러 경로의 give 누락) 다른 태스크가 영원히 못 잡으며, 조건 변수와 같이 쓰면 대기 중 한 단계만 풀려 교착한다. 대안: 락 없이 도는 `_locked` 내부 함수 + 락을 잡는 얇은 공개 함수.

</details>

<details>
<summary>Q5. FreeRTOS 의 우선순위 상속은 언제 풀리나요? 뮤텍스를 두 개 쥐고 있으면?</summary>

`xTaskPriorityDisinherit` 은 `uxMutexesHeld` 가 0 이 될 때만 base 우선순위로 되돌린다 (tasks.c:6870). 두 개를 쥔 상태에서 하나를 놓으면, 놓은 뮤텍스의 대기자 때문에 올라간 것이라도 나머지를 다 놓을 때까지 높은 우선순위로 남는다 — 필요 이상으로 오래 높지만 낮아서 생기는 역전은 없는 보수적 선택이다. 대기자가 timeout 으로 포기하면 `vTaskPriorityDisinheritAfterTimeout` 이 다시 계산한다 (queue.c:1863). 정확한 복원(남은 뮤텍스 대기자 중 최고)과 연쇄 상속은 RTOS-M04.

</details>

<details>
<summary>Q6. 그럼 바이너리 세마포어는 언제 쓰나요? 더 가벼운 대안은?</summary>

ISR → 태스크 신호처럼 "주는 쪽과 받는 쪽이 다른" 동기화. UART RX ISR 이 give, 파서 태스크가 take. 소유자가 없는 것이 여기서는 장점이다. 단, 처리보다 빨리 오는 신호는 합쳐진다(최대 1) — 개수가 중요하면 카운팅 세마포어 (RTOS-B08, B09). 받는 태스크가 하나뿐이면 FreeRTOS 태스크 알림(`vTaskNotifyGiveFromISR` / `ulTaskNotifyTake`)이 별도 객체 없이 더 가볍다 (RTOS-M01).

</details>

<details>
<summary>Q7. "뮤텍스는 상속이 있으니 우선순위 역전은 해결됐다" 고 해도 되나요?</summary>

아니다. 상속은 역전을 **유한하게(bounded)** 만들 뿐 없애지 않는다 — 실측에서도 H 는 L 의 남은 임계 구역(4 tick)만큼은 기다렸다. 또 FreeRTOS 상속은 위처럼 복원이 보수적이고, 상속이 중첩된 체인(H 가 L1 을, L1 이 L2 를 기다림)에서의 동작은 구현마다 다르다 (RTOS-M04). 교착과 체인 문제를 같이 막으려면 우선순위 천장(PCP/IPCP, RTOS-M05). 그리고 가장 확실한 방법은 임계 구역을 짧게 유지하는 것이다 — 락 안에서 I2C 전송 완료를 폴링하지 말고 DMA 완료 알림을 기다리는 식.

</details>

## 9. 추가 연습

### 연습 1 — 진짜 POSIX 뮤텍스는 소유권을 어떻게 보고하나

`PTHREAD_MUTEX_ERRORCHECK` 와 `PTHREAD_MUTEX_RECURSIVE` 뮤텍스를 만들어 ① 아무도 안 잡은 것을 unlock ② 같은 스레드 재잠금 ③ 다른 스레드의 trylock / unlock ④ 재귀 3회 lock 후 2회 unlock 상태에서 다른 스레드 trylock ⑤ 초과 unlock 의 반환값을 확인하라. 모델의 `K_EPERM` / `K_EDEADLK` 와 비교한다.

<details>
<summary>답 보기</summary>

이 맥(macOS libpthread)에서 실측: ① `EPERM` ② `EDEADLK` ③ trylock `EBUSY`, unlock `EPERM` ④ `EBUSY` (깊이 1 남음) ⑤ `EPERM`. POSIX.1-2017 `pthread_mutex_lock` 설명: ERRORCHECK/RECURSIVE 는 "잠그지 않은 뮤텍스나 잠기지 않은 뮤텍스를 unlock 하면 에러" 를 돌려줘야 하고, NORMAL/DEFAULT 에서는 같은 행위가 **정의되지 않은 동작** 이다. 그래서 "소유권을 검사하는 뮤텍스" 를 원하면 타입을 ERRORCHECK 로 명시해야 한다. `-std=c11` 에서 `pthread_mutexattr_settype` 이 보이려면 include 전에 `_XOPEN_SOURCE 700`. 다른 스레드의 실패한 unlock 은 실제로 아무것도 풀지 않으므로 TSan 도 조용했다.

</details>

### 연습 2 — 바이너리 세마포어 + owner 필드 = 뮤텍스?

바이너리 세마포어에 `owner` 필드를 붙여 비소유자 give 를 `K_EPERM` 으로 거부하는 `olock_take/olock_give` 를 만들어라. Part C 시나리오(L 이 잡고 H 대기)에서 이 락은 무엇을 막고 무엇을 못 막나?

<details>
<summary>답 보기</summary>

`olock_give` 첫 줄 `if (l->owner != caller) return K_EPERM;`, 대기자에게 넘길 때 `owner = next`. 이걸로 M 의 잘못된 give 는 막힌다. 하지만 H 가 블록해도 L 의 우선순위는 1 그대로이고 `pick_ready` 는 M 을 고른다 — 우선순위 역전은 그대로다. 소유권 **검사** 와 소유권 **활용**(상속)은 다른 기능이다. 커널 뮤텍스는 소유자를 스케줄러가 아는 곳(TCB 연결)에 두어서 블록 순간 소유자의 우선순위를 바꿀 수 있다. 응용 코드에서 owner 필드를 달아도 그 연결은 생기지 않는다.

</details>

### 연습 3 — ISR → 세마포어 → 태스크 → 뮤텍스

UART RX ISR 이 바이트를 링 버퍼에 넣고 바이너리 세마포어로 파서 태스크를 깨운다. 파서는 깨어나면 링을 **전부** 비우며 뮤텍스로 보호되는 로그 버퍼에 쓴다. ISR 이 3바이트를 연달아 넣은 뒤 파서가 한 번 돌면 로그가 `"ok!"` 여야 한다. (ISR 이 실수로 로그 뮤텍스를 잡으려 하면 모델은 매번 `K_EISR` 로 거부한다 — 그 횟수도 센다.)

<details>
<summary>답 보기</summary>

ISR 세 번의 give 중 두 번은 `K_EFULL` 로 합쳐져 세마포어는 1 이다 (RTOS-B08 의 신호 합쳐짐). 그래도 데이터는 링에 다 있으므로 "깨어나면 전부 비운다" 로 한 번의 take 에 3바이트를 처리해 손실이 없다. 신호의 **개수** 에 의존하지 말고 **데이터 자체** 를 보는 것이 이 패턴의 핵심이다. 로그 버퍼는 태스크만 만지므로 뮤텍스로 충분하다. ISR 쪽의 링 쓰기는 단일 생산자/단일 소비자 lock-free 링이라 ISR 이 블록하지 않는다 (실제 펌웨어에선 인덱스를 `volatile`/원자로 — C-M14).

</details>

### 연습 4 — 재귀 뮤텍스와 에러 경로

`i2c_rmw()` 가 재귀 뮤텍스를 잡고 `i2c_read()`/`i2c_write()` (각자 락을 잡는 공개 API)를 부른다. read 실패(fail_at=1), write 실패(2), 성공(0) 세 경로 모두에서 반환 후 `holder == NO_TASK && depth == 0` 이 되게 하라. 조기 `return` 버전은 무엇을 남기나?

<details>
<summary>답 보기</summary>

모든 경로가 `out:` 레이블의 give 한 번을 지나게 한다 (goto 정리 패턴 — C-M15). 최대 깊이는 2 (rmw 의 1 + read/write 의 1) 이고, 일반 뮤텍스였다면 그 두 번째 take 에서 자기 교착이다. 버그 버전은 read 실패 시 give 없이 돌아가 holder=A, depth=1 이 남고, 다른 태스크의 take 는 영원히 `K_EAGAIN`/블록이다. 실제 FreeRTOS 에선 이 상태가 assert 없이 조용히 유지된다 — "가끔 I2C 가 영원히 멈춘다" 류 버그의 전형이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice    # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san   # 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan     # 보고 없음
```

## 10. 핵심 정리

- 뮤텍스 = 세마포어 + **holder**. holder 가 있어서 ① 소유자만 풀 수 있고 ② 대기자가 높으면 소유자를 올릴 수 있고(상속) ③ holder 는 태스크여야 하니 **ISR 에서 못 쓴다**.
- 바이너리 세마포어는 신호용(ISR → 태스크). 락으로 쓰면 아무나 풀고, 상속이 없어 역전되고(실측 H 대기 16 vs 4 tick), 처음엔 비어 있다.
- FreeRTOS 일반 뮤텍스의 비소유자 give: 에러 반환 없음, `configASSERT` (tasks.c:6861) 가 유일한 방어선. assert 를 끄면 pdPASS 로 통과해 동시 진입·남의 상속 해제·장부 오염.
- 재귀 뮤텍스: holder 의 재 take 는 depth+1, depth 0 에서만 해제. `GiveRecursive` 는 비소유자에 `pdFAIL`. 일반 뮤텍스 재 take 는 자기 교착 (timeout 없으면 영원히).
- FreeRTOS 상속은 쥔 뮤텍스를 **모두** 놓을 때 풀린다 (보수적). 정확한 복원·연쇄는 RTOS-M04, 역전 재현은 RTOS-M03, 천장 프로토콜은 RTOS-M05.
- ISR 데이터 보호: ISR 은 세마포어/알림으로 깨우기만, 자원은 태스크가 뮤텍스로. ISR 과 직접 공유하는 짧은 데이터는 인터럽트 마스킹 임계 구역.
- 개발 빌드는 configASSERT ON — 계약 위반을 즉시 잡는 유일한 장치다.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (이 레슨의 모든 줄 번호): `queue.c` (617 `prvInitialiseMutex`, 759/818 재귀 give/take, 977·1043·1084-1096 `xQueueGenericSend`, 1338·1360 `xQueueGiveFromISR`, 1659·1706-1710·1791-1795 `xQueueSemaphoreTake`, 2039·2082-2083 `xQueueReceiveFromISR`, 2395·2410-2414 `prvCopyDataToQueue`), `tasks.c` (6745·6752 `xTaskPriorityInherit`, 6848·6861-6870 `xTaskPriorityDisinherit`, 7772 `pvTaskIncrementMutexHeldCount`), `include/semphr.h` (68-71, 167, 460, 562-563, 655-656, 703, 824) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — Mutexes, Recursive Mutexes, Binary Semaphores, configASSERT — <https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html>, <https://www.freertos.org/a00110.html#configASSERT>
- FreeRTOS POSIX/Linux 포트 (실시간성 없음, 시뮬레이션용) — <https://www.freertos.org/FreeRTOS-simulator-for-Linux.html>
- POSIX.1-2017 `pthread_mutex_lock` / `pthread_mutex_unlock` (mutex type 별 EDEADLK·EPERM·UB 표), `pthread_mutexattr_settype` — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html>
- Zephyr — Mutexes (소유권, 재귀 잠금, 우선순위 상속, ISR 사용 불가; 이 레슨에서 소스 미검증) — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html>
- CMSIS-RTOS2 — Mutex Management (`osMutexRecursive`, `osMutexPrioInherit`, `osErrorResource`, `osErrorISR`; 문서 기준) — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__MutexMgmt.html>
- Linux kernel — Generic Mutex Subsystem (소유자만 unlock, 재귀 금지, 인터럽트 문맥 금지) — <https://docs.kernel.org/locking/mutex-design.html>
- L. Sha, R. Rajkumar, J. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization," IEEE Trans. Computers 39(9), 1990.
- 관련 레슨: RTOS-B08 (바이너리 세마포어 신호·합쳐짐), RTOS-B09 (카운팅 세마포어), RTOS-M01 (태스크 알림), RTOS-M03 (우선순위 역전 재현), RTOS-M04 (상속 구현·연쇄), RTOS-M05 (천장 프로토콜), C-M14 (ISR 공유 데이터·임계 구역), C-M15 (goto 정리), OS-B05 (pthread 뮤텍스 기본)
