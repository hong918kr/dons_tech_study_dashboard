---
id: RTOS-M04
time: 90분
updated: 2026-11-08
---
# RTOS-M04 우선순위 상속 구현과 연쇄 상속

## 학습 목표

- 미니 커널 뮤텍스에 **우선순위 상속(priority inheritance, PI)** 을 직접 넣고, RTOS-M03 의 정준 역전 시나리오에서 H 의 대기 시간이 M 의 길이와 **무관하게 3 tick 으로 고정(bounded)** 됨을 타임라인으로 보인다.
- 상속 우선순위를 한 줄 공식으로 말할 수 있다: **prio(t) = max(base(t), t 가 가진 모든 뮤텍스의 대기자 prio)** — 그리고 이 공식이 "여러 뮤텍스를 쥔 채 하나만 풀 때" 의 정답 복원값임을 안다.
- 복원 정책 3가지(정확 / base 로 / 전부 풀 때만 = FreeRTOS)를 같은 시나리오로 돌려 비교하고, 어느 쪽이 역전을 다시 만들고 어느 쪽이 과상속(over-boost)을 만드는지 숫자로 설명한다.
- A→B→C **연쇄(transitive) 상속** 을 구현하고, 연쇄가 없으면 중간 우선순위 태스크가 사슬 끝을 선점해 다시 unbounded 가 됨을 재현한다.
- FreeRTOS(V11.3.2) · Zephyr · Linux `rt_mutex` 가 각각 어디까지 하는지 **소스 줄 번호로** 말할 수 있다 (FreeRTOS 는 비연쇄 + "전부 풀 때 복원", Linux 는 연쇄 + 정확 복원, Zephyr 는 v4.5.0 에서 바뀌었다).

## 1. 문제

"Mars Pathfinder 는 어떻게 고쳤나요? 그럼 우선순위 상속을 직접 짜 보세요. 뮤텍스를 두 개 쥐고 있다가 하나만 풀면 우선순위를 어디로 돌리죠? A 가 B 를, B 가 C 를 기다리면요?" — RTOS 면접에서 우선순위 역전(RTOS-M03) 다음에 바로 이어지는 질문이다. 펌웨어 현장에서는 FreeRTOS `xSemaphoreCreateMutex()` 를 쓰면 상속이 자동으로 켜지고 `xSemaphoreCreateBinary()` 를 쓰면 꺼진다 — 둘을 헷갈려 모터 제어 루프가 가끔 데드라인을 놓치는 버그가 실제로 많다. 그리고 "자동으로 켜진다" 의 범위(연쇄? 다중 뮤텍스?)가 커널마다 다르다는 것까지 아는 사람은 드물다.

RTOS-M03 에서 만든 상황을 다시 쓴다 (숫자 그대로, 재설명은 M03 참고):

| 태스크 | prio | 출발 | 하는 일 |
|---|---|---|---|
| H (자세 제어) | 3 | t2 | 첫 step 에 R 요청 → R 안에서 2 tick |
| M (텔레메트리) | 2 | t3 | R 없이 CPU 만 C_M tick |
| L (센서 로거) | 1 | t0 | 1 tick 일한 뒤 t1 에 R 획득, R 안에서 4 tick, 해제 후 1 tick |

일반 세마포어(상속 없음)로는 H 가 `3 + C_M` tick 막힌다 — M 이 길어지는 만큼 H 도 무한정 늘어난다. 이 레슨은 같은 커널에 상속을 넣어 그 숫자를 **3** 으로 만들고, 그다음 면접관이 파고드는 두 지점(다중 뮤텍스 복원, 연쇄)을 구현한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">대기 관계(wait-for) 사슬: A 가 D 를, D 의 주인 B 는 S 를, S 의 주인은 C</text>
  <line x1="370" y1="36" x2="370" y2="292" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="20" y="50" fill="currentColor" font-weight="bold">비연쇄 (FreeRTOS, Zephyr ≤ v4.4)</text>
  <text x="390" y="50" fill="currentColor" font-weight="bold">연쇄 (Linux rt_mutex, Zephyr v4.5, 이 레슨)</text>
  <rect x="40" y="64" width="150" height="34" rx="5" class="f-acc" stroke="currentColor"/><text x="52" y="86" fill="currentColor">A 모터 제어 (4)</text>
  <rect x="40" y="134" width="150" height="34" rx="5" class="f-ok" stroke="currentColor"/><text x="52" y="156" fill="currentColor">B 센서 2 → 4</text>
  <rect x="40" y="204" width="150" height="34" rx="5" class="f-warn" stroke="currentColor"/><text x="52" y="226" fill="currentColor">C 로거 1 → 2</text>
  <path d="M115 98 L115 134" stroke="currentColor" fill="none"/><text x="122" y="122" fill="currentColor" font-size="12">D 대기 (B 가 주인)</text>
  <path d="M115 168 L115 204" stroke="currentColor" fill="none"/><text x="122" y="192" fill="currentColor" font-size="12">S 대기 (C 가 주인)</text>
  <rect x="220" y="204" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/><text x="230" y="226" fill="currentColor">X 텔레메트리 (3)</text>
  <text x="20" y="262" fill="currentColor">C 는 2 에 머문다 → X(3) 가 C 를 선점</text>
  <text x="20" y="282" fill="currentColor">→ A 의 대기 = 6 + C_X (다시 unbounded)</text>
  <rect x="410" y="64" width="150" height="34" rx="5" class="f-acc" stroke="currentColor"/><text x="422" y="86" fill="currentColor">A 모터 제어 (4)</text>
  <rect x="410" y="134" width="150" height="34" rx="5" class="f-ok" stroke="currentColor"/><text x="422" y="156" fill="currentColor">B 센서 2 → 4</text>
  <rect x="410" y="204" width="150" height="34" rx="5" class="f-ok" stroke="currentColor"/><text x="422" y="226" fill="currentColor">C 로거 1 → 2 → 4</text>
  <path d="M485 98 L485 134" stroke="currentColor" fill="none"/><text x="492" y="122" fill="currentColor" font-size="12">hop 1: B ← 4</text>
  <path d="M485 168 L485 204" stroke="currentColor" fill="none"/><text x="492" y="192" fill="currentColor" font-size="12">hop 2: C ← 4</text>
  <rect x="590" y="204" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/><text x="600" y="226" fill="currentColor">X 텔레메트리 (3)</text>
  <text x="390" y="262" fill="currentColor">C 가 4 → X(3) 는 기다린다</text>
  <text x="390" y="282" fill="currentColor">→ A 의 대기 = 6 (C 남은 4 + B 의 2), C_X 무관</text>
</svg>
```

**미니 커널 규칙** (starter.c 머리 주석과 같다 — 레슨마다 커널을 따로 둔다, 공유 라이브러리 없음):

| 항목 | 규칙 |
|---|---|
| 실행 | OS 스레드 1개, tick 단위. 매 tick READY 중 prio 가 가장 높은 태스크 1개가 WORK 1단위 실행 |
| 동점 | 먼저 READY 가 된 쪽. 실행 중인 태스크는 같은 prio 에 밀리지 않는다 (time slicing 없음) |
| op | `WORK n` (n tick), `LOCK m`, `UNLOCK m`, `END` — LOCK/UNLOCK/END 는 0 tick, 도달하는 순간 실행 |
| 대기열 | prio 순, 같으면 FIFO (RTOS-B09 참고). UNLOCK 은 맨 앞 대기자에게 소유권을 바로 넘긴다(handoff) |
| 기록 | `R` 실행 · `B` 뮤텍스 대기 · `r` READY 인데 못 돎 · `.` 안 나왔거나 끝남. `Lp` 줄 = 그 tick 의 prio |

### Part A — 상속: 막히는 순간 소유자를 올리고, 풀 때 내린다

- `int inherited_prio(const kernel_t *k, int t)` — **TODO 1.** t 가 지금 가져야 할 prio = `max(base, t 가 소유한 모든 뮤텍스의 대기자 prio)`.
- `void pi_on_block(kernel_t *k, int w)` — **TODO 2.** w 가 막히는 순간 `w → (blocked_on) → owner` 를 따라가 소유자의 prio 를 `inherited_prio` 로 올린다 (절대 내리지 않는다). Part A 에선 1 단계면 충분하다.
- 정준 시나리오를 C_M ∈ {0, 2, 4, 8, 16} 으로 돌린다. 상속 없음은 H 대기 `3 + C_M` (주어진 baseline), 상속은 **항상 3**, H 는 항상 t7 에 끝나야 한다.

### Part B — 여러 뮤텍스를 쥔 채 하나만 풀 때

센서 하우스키핑 태스크 L(1) 이 I2C 버스 R1 과 설정 블록 R2 를 같이 쥐고 설정을 EEPROM 에 쓴다. 센서 퓨전 H(4) 는 R1 을, 통신 M(3) 은 R2 를 기다린다. L 은 R1 을 먼저 풀고 R2 로 3 tick 더 일한다. 그 사이 DSP 태스크 X(2) 가 C_X tick 짜리 CPU 일을 들고 온다.

- `pi_on_unlock()` 의 `RESTORE_EXACT` 분기 — **TODO 3.** R1 을 푼 뒤 L 은 몇이어야 하나? (`RESTORE_BASE`, `RESTORE_ALL_RELEASED` 분기는 비교용으로 주어진다.)
- 테스트: 정확 복원이면 R1 해제 직후 L = 3, M 대기 = 7 (C_X 무관). base 로 내리면 M 대기 = `7 + C_X`. FreeRTOS 모델(전부 풀 때만)이면 L 이 4 에 남아 H 가 3 tick 늦는다.

### Part C — 연쇄 상속 A→B→C (모터 · 센서 · 플래시 로거)

모터 제어 A(4) 가 센서 데이터 뮤텍스 D 를 원한다. D 의 주인 센서 태스크 B(2) 는 IMU 와 SPI 플래시가 같이 쓰는 SPI 버스 뮤텍스 S 를 기다린다. S 의 주인은 플래시 로거 C(1). 텔레메트리 X(3) 는 CPU 만 C_X tick.

- `pi_on_block()` 을 `k->chain == 1` 일 때 사슬 끝까지(최대 `k->max_hops`) 올라가게 완성한다.
- 테스트: 연쇄면 A 대기 = 6 (C_X 무관), t2 에 C 의 prio 가 4. 비연쇄(`chain = 0`)면 `6 + C_X`, C 는 2 에 머문다.
- 덤으로 무작위 태스크 세트 300개(뮤텍스는 번호 순으로 잡고, 해제 순서는 무작위)에서 **매 tick** "모든 소유자의 prio == 공식 값" 불변식을 검사한다.

## 2. Problem (English)

> **Priority inheritance with nested and transitive locking in a tick-driven mini kernel.**
>
> Tasks are scripts of `WORK n` / `LOCK m` / `UNLOCK m` / `END`. Each tick the highest-priority READY task runs one WORK unit (ties: earliest-ready first, no time slicing). `LOCK`/`UNLOCK` take zero time; `UNLOCK` hands the mutex to the highest-priority waiter (FIFO among equals). Implement:
>
> 1. `int inherited_prio(const kernel_t *k, int t)` — `max(base(t), max prio of tasks blocked on any mutex owned by t)`.
> 2. `void pi_on_block(kernel_t *k, int w)` — called right after `w` blocks. Raise the owner of the mutex `w` waits on to its inherited priority; if `k->chain` and that owner is itself blocked, continue to the next owner (at most `k->max_hops` hops). Never lower a priority here.
> 3. In `pi_on_unlock`, the `RESTORE_EXACT` case: after releasing one mutex, the releaser's priority must reflect only the mutexes it **still** holds.
>
> Example (canonical scenario, H=3/M=2/L=1, C_M=4): without inheritance H is blocked 7 ticks; with inheritance H is blocked 3 ticks for every C_M and finishes at t7. Timeline: `H ..BBBRR` · `M ...rrrrRRRR` · `L RRRRRrrrrrrR`, L's priority `1 1 3 3 3 1 ...`.
>
> Follow-ups: Why can't the owner just return to its base priority on unlock? What does FreeRTOS do when the owner holds two mutexes? Is priority inheritance transitive in FreeRTOS / Zephyr / Linux? What is the worst-case blocking under PI, and why is the priority ceiling protocol better there? What happens to the boosted owner when a waiter times out?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 20/51 passed, exit 1
./starter -v                                                                 # 비교용 타임라인(상속 없음, 다른 복원 정책, 비연쇄)까지 출력
```

처음 상태(stub)는 "상속을 전혀 안 하는 뮤텍스" 와 같다 — 그래서 상속 없음 baseline 테스트와 Part B 의 `RESTORE_BASE` 몇 개만 통과한다. pthread 를 쓰지 않는 단일 스레드 시뮬레이터라 `-pthread` 도, TSan 도 필요 없다 (그래도 돌려 봤고 경고 0).

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 대기열을 따로 만들 필요 없다</summary>

커널에는 대기열 자료구조가 없다. "뮤텍스 m 의 대기자" = `st == T_BLOCKED && blocked_on == m` 인 태스크다. 소유한 뮤텍스는 `k->m[m].owner == t` 로 찾는다. 이중 루프로 최대값을 구하면 된다. 대기자의 `prio` 는 그 대기자가 상속받은 값까지 포함한다는 점이 Part C 에서 중요하다.

</details>

<details>
<summary>힌트 2 — TODO 2: 한 단계 → 루프</summary>

`m = t[cur].blocked_on`, `o = m 의 owner`, `o 의 prio = max(현재, inherited_prio(o))`. Part A 는 여기서 끝. Part C 는 `o` 가 또 막혀 있으면 `cur = o` 로 반복한다. 멈출 조건 셋: 사슬이 끝남(`blocked_on < 0` 또는 주인 없음), 더 올릴 게 없음, 홉 수 초과. 교착 사이클(A↔B)에서도 무한 루프가 되면 안 된다.

</details>

<details>
<summary>힌트 3 — TODO 3: "base 로" 가 왜 틀리나</summary>

L 이 R1 을 푸는 순간 H 는 R1 을 넘겨받아 대기자 목록에서 빠진다. 하지만 R2 에는 아직 M(3) 이 서 있다. 지금 L 이 base(1)로 내려가면 M 은 다시 X(2) 에게 역전당한다. 해제 후의 값도 결국 **TODO 1 의 공식** 이다 — 풀린 뮤텍스는 이제 L 소유가 아니니 공식에 저절로 안 들어간다.

</details>

<details>
<summary>힌트 4 — 타임라인 읽기</summary>

`snapshot` 은 그 tick 의 WORK 직후, 뒤따르는 0-tick op(UNLOCK 등) **전** 에 찍는다. 그래서 t4 의 마지막 임계 구역 tick 에서 L 의 prio 는 아직 3 이고, t5 부터 1 이다. t1 끝에 막힌 B 는 t1 칸에 `R`, t2 칸부터 `B` 로 나온다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 미니 커널 | FreeRTOS V11.3.2 | Zephyr | POSIX / Linux |
|---|---|---|---|
| `PROTO_PI` 뮤텍스 | `xSemaphoreCreateMutex()` → `queueQUEUE_TYPE_MUTEX` (semphr.h:735). 상속은 항상 켜짐 | `k_mutex_init` / `K_MUTEX_DEFINE` (상속 내장, `CONFIG_PRIORITY_CEILING` = 상속 상한) | `pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT)`; 커널 쪽은 `rt_mutex` / `FUTEX_LOCK_PI` |
| `PROTO_NONE` (M03) | `xSemaphoreCreateBinary()` (semphr.h:167) — 상속 경로는 `uxQueueType == queueQUEUE_IS_MUTEX` 일 때만 (queue.c:1791) | `k_sem` | `sem_t`, `PTHREAD_PRIO_NONE` |
| `LOCK(m)` / `LOCK_TO(m,n)` | `xSemaphoreTake(m, portMAX_DELAY)` / `(m, n)` | `k_mutex_lock(&m, K_FOREVER)` / `K_MSEC(n)` | `pthread_mutex_lock` / `pthread_mutex_timedlock` |
| `UNLOCK(m)` | `xSemaphoreGive(m)` | `k_mutex_unlock(&m)` | `pthread_mutex_unlock` |
| `base` | `uxBasePriority` (tasks.c:417), `uxTaskBasePriorityGet()` | v4.5: 스레드별 `orig_prio` / ≤ v4.4: 뮤텍스별 `owner_orig_prio` | `task->normal_prio` |
| `nheld` | `uxMutexesHeld` (tasks.c:418) | v4.5: `held_mutexes` 리스트 | 태스크별 `pi_waiters` 트리(각 락의 최고 대기자) |
| `pi_on_block` | `xTaskPriorityInherit()` (tasks.c:6745) | `z_impl_k_mutex_lock` 의 chain walk (v4.5) | `task_blocks_on_rt_mutex()` → `rt_mutex_adjust_prio_chain()` |
| `pi_on_unlock` | `xTaskPriorityDisinherit()` (tasks.c:6848), give 경로 queue.c:2413 | `k_mutex_unlock` → `held_mutexes_highest_waiter_prio()` (v4.5) | `mark_wakeup_next_waiter()` → `rt_mutex_adjust_prio()` |
| 대기자 타임아웃 (연습 2) | `vTaskPriorityDisinheritAfterTimeout()` (tasks.c:6942), 호출 queue.c:1863 | 깨어난 대기자 문맥에서 지연 처리 (v4.5 주석) | `remove_waiter()` |
| `pick()` 동점 규칙 | 깨운 태스크가 **더 높을 때만** 즉시 전환 (tasks.c:5555 `>`) | — | — |

FreeRTOS 의 숫자는 클수록 높은 우선순위(이 레슨과 같다). Zephyr 는 **작을수록** 높다(음수 = 협조형). Linux 커널 내부 `prio` 도 작을수록 높다. 면접에서 숫자를 말할 땐 방향을 먼저 밝히자.

### Part A — 공식 하나, 호출 지점 둘

```c
static int inherited_prio(const kernel_t *k, int t) {
    int p = k->t[t].base;
    for (int m = 0; m < k->nm; m++) {
        if (k->m[m].owner != t) continue;
        for (int w = 0; w < k->nt; w++)
            if (k->t[w].st == T_BLOCKED && k->t[w].blocked_on == m) p = max_i(p, k->t[w].prio);
    }
    return p;
}
```

상속은 "누가 누구를 올렸는지" 를 기억하는 문제가 아니라, **지금 대기 관계에서 계산되는 값** 이다. 이렇게 보면 구현이 단순해진다:

| 사건 | 할 일 | 이 레슨의 함수 |
|---|---|---|
| w 가 m 에서 막힘 | m 의 주인을 공식 값으로 **올림** (+ 연쇄) | `pi_on_block` |
| c 가 m 을 풀고 w 에게 넘김 | c 를 공식 값으로 **재계산** (내려갈 수 있음), w 는 남은 대기자를 상속 | `pi_on_unlock` |
| 대기자가 타임아웃 | 주인을 재계산 | 연습 2 |

정준 시나리오 C_M=4, `./solution -v` 의 실제 출력:

```text
canonical C_M=4, plain (no PI)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 H    : . . B B B B B B B R R . . . . .
 M    : . . . R R R R . . . . . . . . .
 L    : R R R r r r r R R r r R . . . .
 Lp   : 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
canonical C_M=4, PI
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 H    : . . B B B R R . . . . . . . . .
 M    : . . . r r r r R R R R . . . . .
 L    : R R R R R r r r r r r R . . . .
 Lp   : 1 1 3 3 3 1 1 1 1 1 1 1 1 1 1 1
```

(tick 머리줄은 `tick % 10`.) 한 tick 씩 읽으면:

1. t0: L 이 1 tick 일하고, 바로 이어지는 `LOCK R` 를 실행 → R 획득 (t1 부터 임계 구역).
2. t2: H 출발 → 최고 우선순위라 선택 → 첫 op `LOCK R` 에서 막힘 → `pi_on_block` 이 L 을 3 으로. 같은 tick 안에서 다시 고르면 L(3) → 임계 구역 2/4.
3. t3: M(2) 출발. 상속이 없으면 M > L(1) 이라 M 이 C_M tick 동안 돈다 — 이것이 unbounded 의 원인. 상속이 있으면 L(3) > M(2) 이라 M 은 `r`.
4. t4 끝: L 이 4/4 를 마치고 `UNLOCK R` → H 에게 넘김 → L 재계산 = base 1. H(3) 가 선점.
5. t5–t6: H. t7–t10: M. t11: L 의 꼬리 1 tick.

```svg
<svg viewBox="0 0 740 372" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정준 시나리오 C_M=4 — solution.c -v 의 실제 타임라인</text> <text x="10" y="40" fill="currentColor" font-weight="bold">일반 세마포어 (상속 없음, M03): H 대기 7 tick</text> <g fill="currentColor" font-size="12"> <text x="83" y="62">0</text> <text x="117" y="62">1</text> <text x="151" y="62">2</text> <text x="185" y="62">3</text> <text x="219" y="62">4</text>
  <text x="253" y="62">5</text> <text x="287" y="62">6</text> <text x="321" y="62">7</text> <text x="355" y="62">8</text> <text x="389" y="62">9</text> <text x="420" y="62">10</text> <text x="454" y="62">11</text> <text x="488" y="62">12</text>
  <text x="522" y="62">13</text> <text x="556" y="62">14</text> <text x="590" y="62">15</text> </g> <text x="16" y="85" fill="currentColor">H</text> <rect x="139" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="151" y="85" fill="currentColor" font-size="12">B</text> <rect x="173" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="185" y="85" fill="currentColor" font-size="12">B</text> <rect x="207" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="219" y="85" fill="currentColor" font-size="12">B</text> <rect x="241" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="253" y="85" fill="currentColor" font-size="12">B</text> <rect x="275" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="287" y="85" fill="currentColor" font-size="12">B</text> <rect x="309" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="321" y="85" fill="currentColor" font-size="12">B</text> <rect x="343" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="355" y="85" fill="currentColor" font-size="12">B</text> <rect x="377" y="68" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="389" y="85" fill="currentColor" font-size="12">R</text> <rect x="411" y="68" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="423" y="85" fill="currentColor" font-size="12">R</text> <text x="16" y="115" fill="currentColor">M</text>
  <rect x="173" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="185" y="115" fill="currentColor" font-size="12">R</text> <rect x="207" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="219" y="115" fill="currentColor" font-size="12">R</text> <rect x="241" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="253" y="115" fill="currentColor" font-size="12">R</text> <rect x="275" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="287" y="115" fill="currentColor" font-size="12">R</text>
  <text x="16" y="145" fill="currentColor">L</text> <rect x="71" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="83" y="145" fill="currentColor" font-size="12">R</text> <rect x="105" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="117" y="145" fill="currentColor" font-size="12">R</text> <rect x="139" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="151" y="145" fill="currentColor" font-size="12">R</text> <rect x="173" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/>
  <text x="185" y="145" fill="currentColor" font-size="12">r</text> <rect x="207" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="219" y="145" fill="currentColor" font-size="12">r</text> <rect x="241" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="253" y="145" fill="currentColor" font-size="12">r</text> <rect x="275" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="287" y="145" fill="currentColor" font-size="12">r</text> <rect x="309" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="321" y="145" fill="currentColor" font-size="12">R</text> <rect x="343" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="355" y="145" fill="currentColor" font-size="12">R</text> <rect x="377" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="389" y="145" fill="currentColor" font-size="12">r</text> <rect x="411" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="423" y="145" fill="currentColor" font-size="12">r</text> <rect x="445" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="457" y="145" fill="currentColor" font-size="12">R</text> <text x="10" y="172" fill="currentColor" font-weight="bold">우선순위 상속 뮤텍스: H 대기 3 tick (아래 숫자 = L 의 prio)</text> <g fill="currentColor" font-size="12"> <text x="83" y="194">0</text> <text x="117" y="194">1</text> <text x="151" y="194">2</text> <text x="185" y="194">3</text> <text x="219" y="194">4</text>
  <text x="253" y="194">5</text> <text x="287" y="194">6</text> <text x="321" y="194">7</text> <text x="355" y="194">8</text> <text x="389" y="194">9</text> <text x="420" y="194">10</text> <text x="454" y="194">11</text> <text x="488" y="194">12</text>
  <text x="522" y="194">13</text> <text x="556" y="194">14</text> <text x="590" y="194">15</text> </g> <text x="16" y="217" fill="currentColor">H</text> <rect x="139" y="200" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="151" y="217" fill="currentColor" font-size="12">B</text> <rect x="173" y="200" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="185" y="217" fill="currentColor" font-size="12">B</text> <rect x="207" y="200" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="219" y="217" fill="currentColor" font-size="12">B</text> <rect x="241" y="200" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="253" y="217" fill="currentColor" font-size="12">R</text> <rect x="275" y="200" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="287" y="217" fill="currentColor" font-size="12">R</text> <text x="16" y="247" fill="currentColor">M</text>
  <rect x="173" y="230" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="185" y="247" fill="currentColor" font-size="12">r</text> <rect x="207" y="230" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="219" y="247" fill="currentColor" font-size="12">r</text> <rect x="241" y="230" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="253" y="247" fill="currentColor" font-size="12">r</text> <rect x="275" y="230" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="287" y="247" fill="currentColor" font-size="12">r</text>
  <rect x="309" y="230" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="321" y="247" fill="currentColor" font-size="12">R</text> <rect x="343" y="230" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="355" y="247" fill="currentColor" font-size="12">R</text> <rect x="377" y="230" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="389" y="247" fill="currentColor" font-size="12">R</text> <rect x="411" y="230" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="423" y="247" fill="currentColor" font-size="12">R</text>
  <text x="16" y="277" fill="currentColor">L</text> <rect x="71" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="83" y="277" fill="currentColor" font-size="12">R</text> <rect x="105" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="117" y="277" fill="currentColor" font-size="12">R</text> <rect x="139" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="151" y="277" fill="currentColor" font-size="12">R</text> <rect x="173" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="185" y="277" fill="currentColor" font-size="12">R</text> <rect x="207" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="219" y="277" fill="currentColor" font-size="12">R</text> <rect x="241" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="253" y="277" fill="currentColor" font-size="12">r</text> <rect x="275" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="287" y="277" fill="currentColor" font-size="12">r</text> <rect x="309" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/>
  <text x="321" y="277" fill="currentColor" font-size="12">r</text> <rect x="343" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="355" y="277" fill="currentColor" font-size="12">r</text> <rect x="377" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="389" y="277" fill="currentColor" font-size="12">r</text> <rect x="411" y="260" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="423" y="277" fill="currentColor" font-size="12">r</text> <rect x="445" y="260" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="457" y="277" fill="currentColor" font-size="12">R</text> <text x="16" y="304" fill="currentColor" font-size="12">L prio</text> <text x="83" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="117" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="151" y="304" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="185" y="304" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="219" y="304" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="253" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="287" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="321" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="355" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="389" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="423" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="457" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="491" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="525" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="559" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="593" y="304" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="10" y="332" fill="currentColor">R=실행  B=뮤텍스 대기  r=READY 인데 못 돎.  위: M 이 L 을 선점해 H 가 M 만큼 더 기다린다.</text> <text x="10" y="352" fill="currentColor">아래: t2 에 H 가 막히는 순간 L 이 3 으로 올라 M(2) 을 이긴다. t4 끝 해제와 함께 1 로 복원.</text>
</svg>
```

C_M 을 바꿔 가며 (`./solution` 첫 표, 실제 출력):

```text
Part A  C_M | H blocked (none)  H blocked (PI)  H finish (PI)
          0 |                 3               3              7
          2 |                 5               3              7
          4 |                 7               3              7
          8 |                11               3              7
         16 |                19               3              7
```

H 의 대기 3 = **H 가 요청한 순간 L 에게 남아 있던 임계 구역 길이** (L 은 t1 에 4 tick 짜리 구역에 들어갔고 H 가 t2 에 왔으니 3). 상속은 역전을 "없애는" 게 아니라 **하위 태스크의 임계 구역 하나로 묶는다**. 그래서 임계 구역은 짧아야 한다 — 여기서 UART `printf` 를 하면 그 시간이 그대로 H 의 최악 대기에 더해진다.

(RTOS-M03 은 같은 시나리오를 자기 커널로 돌리는데, 커널 규칙(0-tick 커널 호출, 1 tick = 일 1단위, 같은 prio FIFO·slicing 없음, give 가 최고 대기자에게 바로 넘김)이 같아서 숫자도 같다: 상속 없음 `3 + C_M` = 3, 5, 7, 11, 19, C_M=4 타임라인 `H ..BBBBBBBRR` · `M ...RRRR` · `L RRRrrrrRRrrR`. 이 레슨의 첫 표와 위 `plain` 타임라인이 그대로 그 값이다. 달라진 것은 상속을 켠 아래 줄뿐이다.)

### Part B — 해제 후 복원: 세 가지 정책

```c
case RESTORE_EXACT:
    t->prio = inherited_prio(k, c);       /* 아직 가진 뮤텍스의 대기자만 남는다 */
    break;
```

`mutex_unlock` 이 이미 `owner` 를 바꾸고 `nheld` 를 줄였으므로, 같은 공식을 다시 부르기만 하면 "아직 쥔 뮤텍스" 만 반영된다. 실제 출력(C_X=6, `-v`):

```text
Part B, RESTORE_EXACT (C_X=6)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
 H    : . . B R R . . . . . . . . . . . . .
 M    : . B B B B B B B R R . . . . . . . .
 X    : . . . r r r r r r r R R R R R R . .
 L    : R R R r r R R R r r r r r r r r R .
 Lp   : 1 3 4 3 3 3 3 3 1 1 1 1 1 1 1 1 1 1
Part B, RESTORE_ALL_RELEASED (FreeRTOS model, C_X=6)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
 H    : . . B r r r R R . . . . . . . . . .
 M    : . B B B B B r r R R . . . . . . . .
 X    : . . . r r r r r r r R R R R R R . .
 L    : R R R R R R r r r r r r r r r r R .
 Lp   : 1 3 4 4 4 4 1 1 1 1 1 1 1 1 1 1 1 1
```

```svg
<svg viewBox="0 0 740 454" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part B — 뮤텍스 2개 중 하나만 풀 때의 복원 (C_X=6, 실제 출력)</text> <text x="10" y="40" fill="currentColor" font-weight="bold">RESTORE_EXACT: R1 해제 후 L = 3 (R2 에 M 대기) → M 대기 7</text> <g fill="currentColor" font-size="12"> <text x="83" y="62">0</text> <text x="117" y="62">1</text> <text x="151" y="62">2</text> <text x="185" y="62">3</text> <text x="219" y="62">4</text>
  <text x="253" y="62">5</text> <text x="287" y="62">6</text> <text x="321" y="62">7</text> <text x="355" y="62">8</text> <text x="389" y="62">9</text> <text x="420" y="62">10</text> <text x="454" y="62">11</text> <text x="488" y="62">12</text>
  <text x="522" y="62">13</text> <text x="556" y="62">14</text> <text x="590" y="62">15</text> <text x="624" y="62">16</text> <text x="658" y="62">17</text> </g> <text x="16" y="85" fill="currentColor">H</text> <rect x="139" y="68" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="151" y="85" fill="currentColor" font-size="12">B</text> <rect x="173" y="68" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="185" y="85" fill="currentColor" font-size="12">R</text> <rect x="207" y="68" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="219" y="85" fill="currentColor" font-size="12">R</text> <text x="16" y="115" fill="currentColor">M</text> <rect x="105" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="117" y="115" fill="currentColor" font-size="12">B</text>
  <rect x="139" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="151" y="115" fill="currentColor" font-size="12">B</text> <rect x="173" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="185" y="115" fill="currentColor" font-size="12">B</text> <rect x="207" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="219" y="115" fill="currentColor" font-size="12">B</text> <rect x="241" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="253" y="115" fill="currentColor" font-size="12">B</text>
  <rect x="275" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="287" y="115" fill="currentColor" font-size="12">B</text> <rect x="309" y="98" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="321" y="115" fill="currentColor" font-size="12">B</text> <rect x="343" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="355" y="115" fill="currentColor" font-size="12">R</text> <rect x="377" y="98" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="389" y="115" fill="currentColor" font-size="12">R</text>
  <text x="16" y="145" fill="currentColor">X</text> <rect x="173" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="185" y="145" fill="currentColor" font-size="12">r</text> <rect x="207" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="219" y="145" fill="currentColor" font-size="12">r</text> <rect x="241" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="253" y="145" fill="currentColor" font-size="12">r</text> <rect x="275" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/>
  <text x="287" y="145" fill="currentColor" font-size="12">r</text> <rect x="309" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="321" y="145" fill="currentColor" font-size="12">r</text> <rect x="343" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="355" y="145" fill="currentColor" font-size="12">r</text> <rect x="377" y="128" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="389" y="145" fill="currentColor" font-size="12">r</text> <rect x="411" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="423" y="145" fill="currentColor" font-size="12">R</text> <rect x="445" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="457" y="145" fill="currentColor" font-size="12">R</text> <rect x="479" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="491" y="145" fill="currentColor" font-size="12">R</text> <rect x="513" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="525" y="145" fill="currentColor" font-size="12">R</text> <rect x="547" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="559" y="145" fill="currentColor" font-size="12">R</text> <rect x="581" y="128" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="593" y="145" fill="currentColor" font-size="12">R</text> <text x="16" y="175" fill="currentColor">L</text> <rect x="71" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="83" y="175" fill="currentColor" font-size="12">R</text> <rect x="105" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="117" y="175" fill="currentColor" font-size="12">R</text>
  <rect x="139" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="151" y="175" fill="currentColor" font-size="12">R</text> <rect x="173" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="185" y="175" fill="currentColor" font-size="12">r</text> <rect x="207" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="219" y="175" fill="currentColor" font-size="12">r</text> <rect x="241" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="253" y="175" fill="currentColor" font-size="12">R</text>
  <rect x="275" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="287" y="175" fill="currentColor" font-size="12">R</text> <rect x="309" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="321" y="175" fill="currentColor" font-size="12">R</text> <rect x="343" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="355" y="175" fill="currentColor" font-size="12">r</text> <rect x="377" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="389" y="175" fill="currentColor" font-size="12">r</text>
  <rect x="411" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="423" y="175" fill="currentColor" font-size="12">r</text> <rect x="445" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="457" y="175" fill="currentColor" font-size="12">r</text> <rect x="479" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="491" y="175" fill="currentColor" font-size="12">r</text> <rect x="513" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="525" y="175" fill="currentColor" font-size="12">r</text>
  <rect x="547" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="559" y="175" fill="currentColor" font-size="12">r</text> <rect x="581" y="158" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="593" y="175" fill="currentColor" font-size="12">r</text> <rect x="615" y="158" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="627" y="175" fill="currentColor" font-size="12">R</text> <text x="16" y="202" fill="currentColor" font-size="12">L prio</text> <text x="83" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="117" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="151" y="202" fill="currentColor" font-size="12" font-weight="bold">4</text> <text x="185" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="219" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="253" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="287" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="321" y="202" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="355" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="389" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="423" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="457" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="491" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="525" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="559" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="593" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="627" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="661" y="202" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="10" y="224" fill="currentColor" font-weight="bold">RESTORE_BASE: R1 해제 후 L = 1 → X 가 끼어들어 M 대기 13</text> <g fill="currentColor" font-size="12"> <text x="83" y="246">0</text> <text x="117" y="246">1</text> <text x="151" y="246">2</text> <text x="185" y="246">3</text> <text x="219" y="246">4</text>
  <text x="253" y="246">5</text> <text x="287" y="246">6</text> <text x="321" y="246">7</text> <text x="355" y="246">8</text> <text x="389" y="246">9</text> <text x="420" y="246">10</text> <text x="454" y="246">11</text> <text x="488" y="246">12</text>
  <text x="522" y="246">13</text> <text x="556" y="246">14</text> <text x="590" y="246">15</text> <text x="624" y="246">16</text> <text x="658" y="246">17</text> </g> <text x="16" y="269" fill="currentColor">H</text> <rect x="139" y="252" width="32" height="24" class="f-warn" stroke="currentColor"/>
  <text x="151" y="269" fill="currentColor" font-size="12">B</text> <rect x="173" y="252" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="185" y="269" fill="currentColor" font-size="12">R</text> <rect x="207" y="252" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="219" y="269" fill="currentColor" font-size="12">R</text> <text x="16" y="299" fill="currentColor">M</text> <rect x="105" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="117" y="299" fill="currentColor" font-size="12">B</text>
  <rect x="139" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="151" y="299" fill="currentColor" font-size="12">B</text> <rect x="173" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="185" y="299" fill="currentColor" font-size="12">B</text> <rect x="207" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="219" y="299" fill="currentColor" font-size="12">B</text> <rect x="241" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="253" y="299" fill="currentColor" font-size="12">B</text>
  <rect x="275" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="287" y="299" fill="currentColor" font-size="12">B</text> <rect x="309" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="321" y="299" fill="currentColor" font-size="12">B</text> <rect x="343" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="355" y="299" fill="currentColor" font-size="12">B</text> <rect x="377" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="389" y="299" fill="currentColor" font-size="12">B</text>
  <rect x="411" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="423" y="299" fill="currentColor" font-size="12">B</text> <rect x="445" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="457" y="299" fill="currentColor" font-size="12">B</text> <rect x="479" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="491" y="299" fill="currentColor" font-size="12">B</text> <rect x="513" y="282" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="525" y="299" fill="currentColor" font-size="12">B</text>
  <rect x="547" y="282" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="559" y="299" fill="currentColor" font-size="12">R</text> <rect x="581" y="282" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="593" y="299" fill="currentColor" font-size="12">R</text> <text x="16" y="329" fill="currentColor">X</text> <rect x="173" y="312" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="185" y="329" fill="currentColor" font-size="12">r</text> <rect x="207" y="312" width="32" height="24" class="f-mut" stroke="currentColor"/>
  <text x="219" y="329" fill="currentColor" font-size="12">r</text> <rect x="241" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="253" y="329" fill="currentColor" font-size="12">R</text> <rect x="275" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="287" y="329" fill="currentColor" font-size="12">R</text> <rect x="309" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="321" y="329" fill="currentColor" font-size="12">R</text> <rect x="343" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/>
  <text x="355" y="329" fill="currentColor" font-size="12">R</text> <rect x="377" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="389" y="329" fill="currentColor" font-size="12">R</text> <rect x="411" y="312" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="423" y="329" fill="currentColor" font-size="12">R</text> <text x="16" y="359" fill="currentColor">L</text> <rect x="71" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="83" y="359" fill="currentColor" font-size="12">R</text>
  <rect x="105" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="117" y="359" fill="currentColor" font-size="12">R</text> <rect x="139" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="151" y="359" fill="currentColor" font-size="12">R</text> <rect x="173" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="185" y="359" fill="currentColor" font-size="12">r</text> <rect x="207" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="219" y="359" fill="currentColor" font-size="12">r</text>
  <rect x="241" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="253" y="359" fill="currentColor" font-size="12">r</text> <rect x="275" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="287" y="359" fill="currentColor" font-size="12">r</text> <rect x="309" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="321" y="359" fill="currentColor" font-size="12">r</text> <rect x="343" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="355" y="359" fill="currentColor" font-size="12">r</text>
  <rect x="377" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="389" y="359" fill="currentColor" font-size="12">r</text> <rect x="411" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="423" y="359" fill="currentColor" font-size="12">r</text> <rect x="445" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="457" y="359" fill="currentColor" font-size="12">R</text> <rect x="479" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="491" y="359" fill="currentColor" font-size="12">R</text>
  <rect x="513" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="525" y="359" fill="currentColor" font-size="12">R</text> <rect x="547" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="559" y="359" fill="currentColor" font-size="12">r</text> <rect x="581" y="342" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="593" y="359" fill="currentColor" font-size="12">r</text> <rect x="615" y="342" width="32" height="24" class="f-acc" stroke="currentColor"/> <text x="627" y="359" fill="currentColor" font-size="12">R</text>
  <text x="16" y="386" fill="currentColor" font-size="12">L prio</text> <text x="83" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="117" y="386" fill="currentColor" font-size="12" font-weight="bold">3</text> <text x="151" y="386" fill="currentColor" font-size="12" font-weight="bold">4</text> <text x="185" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="219" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="253" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="287" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="321" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="355" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="389" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="423" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="457" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="491" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="525" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="559" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text>
  <text x="593" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="627" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="661" y="386" fill="currentColor" font-size="12" font-weight="normal">1</text> <text x="10" y="414" fill="currentColor">t2 끝에 L 이 R1 을 풀어 H 에게 넘긴다. 아직 R2 를 쥐고 있고 거기엔 M(3) 이 기다린다.</text> <text x="10" y="434" fill="currentColor">base 로 내리면 X(2) 가 L(1) 을 선점 → M 이 X 의 길이만큼 다시 무한정 역전.</text>
</svg>
```

| 정책 | R1 해제 직후 L | H 대기 (B / r) | M 대기 | C_X 를 늘리면 | 누가 쓰나 |
|---|---|---|---|---|---|
| `RESTORE_EXACT` | 3 | 1 / 0 | 7 | M 그대로 7 | Linux `rt_mutex`, Zephyr v4.5.0 |
| `RESTORE_BASE` | 1 | 1 / 0 | `7 + C_X` (C_X=6 → 13) | **M unbounded** | 흔한 오답, 단순한 교재 구현 |
| `RESTORE_ALL_RELEASED` | 4 (유지) | 1 / **3** | 5 | M 그대로 | FreeRTOS `xTaskPriorityDisinherit` |

- **base 로 내리기** 는 Part A 의 문제를 그대로 되살린다. 상속을 "켰는데도" 역전이 난다 — 다중 뮤텍스 코드에서만 나타나 테스트에서 놓치기 쉽다.
- **FreeRTOS 방식** 은 안전 쪽으로 틀린다(과상속). tasks.c:6869–6870:

  ```c
  /* Only disinherit if no other mutexes are held. */
  if( pxTCB->uxMutexesHeld == ( UBaseType_t ) 0 )
  ```

  L 은 R2 를 놓을 때까지 4 에 머문다. M 은 오히려 빨리 끝나지만(5), H 는 이미 R1 을 넘겨받았는데도 L 과 **같은 우선순위 4** 라 기다린다. FreeRTOS 는 깨운 태스크가 현재 태스크보다 **엄격히 높을 때만** 즉시 전환한다 (tasks.c:5555 `if( pxUnblockedTCB->uxPriority > pxCurrentTCB->uxPriority )`). 이 모델은 time slicing 이 없어서 H 가 L 의 R2 구간 3 tick 을 통째로 기다린다. `configUSE_TIME_SLICING == 1` 이면 다음 tick 인터럽트에서 같은 우선순위끼리 돌아가므로(tasks.c:4967–4971) 지연이 약 1 tick 으로 줄 것이다 — **소스 읽기 기준 추론, FreeRTOS 를 실행해 확인하지는 않았다.**
- FreeRTOS 도 이 단순화를 알고 있다: 순서를 바꿔 반납하는 경우를 위한 특별 경로가 있다 (queue.c:1062–1068, "the task was holding multiple mutexes and the mutexes were given back in an order that is different to that in which they were taken") — 마지막 뮤텍스를 반납할 때 대기자가 없어도 yield 한다.
- Zephyr ≤ v4.4.x 는 **뮤텍스마다** `owner_orig_prio` 를 저장하고 해제 시 그 값으로 되돌렸다 (v4.4.2 kernel/mutex.c 의 `adjust_owner_prio(mutex, mutex->owner_orig_prio)`). 2026-07-13 커밋 1b60add1ac "kernel: mutex: fix priority inheritance with multiple held mutexes" 가 "unconditionally restores owner_orig_prio on unlock, ignoring other held mutexes" 를 버그로 고치고 `held_mutexes` 재계산을 넣었다 → v4.5.0 (2026-10-28 릴리스)부터 정확 복원.

### Part C — 연쇄: 루프 하나

```c
static void pi_on_block(kernel_t *k, int w) {
    int cur = w;
    for (int hops = 0; hops < k->max_hops; hops++) {
        int m = k->t[cur].blocked_on;
        if (m < 0) break;                     /* 사슬 끝: cur 는 실행 가능 */
        int o = k->m[m].owner;
        if (o < 0) break;
        int np = inherited_prio(k, o);
        if (np <= k->t[o].prio) break;        /* 이미 충분 → 위쪽도 이미 충분 */
        k->t[o].prio = np;
        if (!k->chain || o == w) break;       /* 비연쇄 or 사이클 */
        cur = o;
    }
}
```

`np <= prio` 에서 멈춰도 되는 이유: 사슬 위쪽의 각 주인은 이전 사건 때 이미 "자기 대기자 이상" 으로 맞춰져 있었다. o 가 안 올라갔으면 o 를 기다리는 쪽 기준 값도 안 바뀌었으니 그 위도 그대로다. 무작위 테스트의 불변식이 이 논증을 매 tick 확인한다.

실제 출력 (C_X=4):

```text
Part C, chain (C_X=4)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
 A    : . . B B B B B B R R . . . . . . . .
 X    : . . . r r r r r r r R R R R . . . .
 B    : . R B B B B R R r r r r r r . . . .
 C    : R r R R R R r r r r r r r r R . . .
 Cp   : 1 1 4 4 4 4 1 1 1 1 1 1 1 1 1 1 1 1
Part C, no chain (C_X=4)
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7
 A    : . . B B B B B B B B B B R R . . . .
 X    : . . . R R R R . . . . . . . . . . .
 B    : . R B B B B B B B B R R r r . . . .
 C    : R r R r r r r R R R r r r r R . . .
 Cp   : 1 1 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1
```

1. t1: B 출발 → D 획득 → 1 tick → 끝에 `LOCK S` 에서 막힘 → C 를 2 로.
2. t2: A 출발 → `LOCK D` 에서 막힘. hop 1: B ← 4. 연쇄면 B 가 S 에서 막혀 있으니 hop 2: C ← 4. 비연쇄면 여기서 멈춤 — C 는 2.
3. t3: X(3) 출발. 연쇄: C(4) > X → C 가 S 구간을 끝냄(t5) → S 를 B 에게. B 는 여전히 A 때문에 4 (`pi_on_unlock` 의 new_owner 상속) → t6–t7 → D 를 A 에게 → t8 A. 비연쇄: X(3) > C(2) → X 가 4 tick 다 돈 뒤에야 C 가 진행.

```text
Part C  C_X | A blocked (chain)  A blocked (no chain)
          0 |                 6                     6
          2 |                 6                     8
          4 |                 6                    10
          8 |                 6                    14
         16 |                 6                    22
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">대기 tick 수 vs 끼어드는 CPU 태스크 길이 (solution.c 출력값)</text> <line x1="70" y1="270" x2="560" y2="270" stroke="currentColor"/> <line x1="70" y1="270" x2="70" y2="50" stroke="currentColor"/> <line x1="66" y1="270" x2="560" y2="270" stroke="currentColor" stroke-opacity="0.15"/> <text x="42" y="274" fill="currentColor" font-size="12">0</text> <line x1="66" y1="215" x2="560" y2="215" stroke="currentColor" stroke-opacity="0.15"/> <text x="42" y="219" fill="currentColor" font-size="12">6</text> <line x1="66" y1="160" x2="560" y2="160" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="42" y="164" fill="currentColor" font-size="12">12</text> <line x1="66" y1="105" x2="560" y2="105" stroke="currentColor" stroke-opacity="0.15"/> <text x="42" y="109" fill="currentColor" font-size="12">18</text> <line x1="66" y1="50" x2="560" y2="50" stroke="currentColor" stroke-opacity="0.15"/> <text x="42" y="54" fill="currentColor" font-size="12">24</text> <text x="66" y="288" fill="currentColor" font-size="12">0</text> <text x="127" y="288" fill="currentColor" font-size="12">2</text> <text x="188" y="288" fill="currentColor" font-size="12">4</text>
  <text x="311" y="288" fill="currentColor" font-size="12">8</text> <text x="556" y="288" fill="currentColor" font-size="12">16</text> <text x="190" y="310" fill="currentColor" font-size="12">C_M (Part A의 M) / C_X (Part C의 X) — tick</text> <text x="14" y="38" fill="currentColor" font-size="12">대기 tick</text> <polyline points="70,242 131,224 192,206 315,169 560,96" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/> <circle cx="70" cy="242" r="4" class="f-warn" stroke="currentColor"/> <circle cx="131" cy="224" r="4" class="f-warn" stroke="currentColor"/> <circle cx="192" cy="206" r="4" class="f-warn" stroke="currentColor"/>
  <circle cx="315" cy="169" r="4" class="f-warn" stroke="currentColor"/> <circle cx="560" cy="96" r="4" class="f-warn" stroke="currentColor"/> <line x1="572" y1="66" x2="596" y2="66" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/> <circle cx="584" cy="66" r="5" class="f-warn" stroke="currentColor"/> <text x="604" y="70" fill="currentColor">일반 세마포어: H</text> <polyline points="70,242 131,242 192,242 315,242 560,242" fill="none" stroke="currentColor" stroke-width="3"/> <circle cx="70" cy="242" r="4" class="f-acc" stroke="currentColor"/> <circle cx="131" cy="242" r="4" class="f-acc" stroke="currentColor"/>
  <circle cx="192" cy="242" r="4" class="f-acc" stroke="currentColor"/> <circle cx="315" cy="242" r="4" class="f-acc" stroke="currentColor"/> <circle cx="560" cy="242" r="4" class="f-acc" stroke="currentColor"/> <line x1="572" y1="92" x2="596" y2="92" stroke="currentColor" stroke-width="3"/> <circle cx="584" cy="92" r="5" class="f-acc" stroke="currentColor"/> <text x="604" y="96" fill="currentColor">PI: H</text> <polyline points="70,215 131,197 192,178 315,142 560,68" fill="none" stroke="currentColor" stroke-width="2"/> <circle cx="70" cy="215" r="4" class="f-warn" stroke="currentColor"/>
  <circle cx="131" cy="197" r="4" class="f-warn" stroke="currentColor"/> <circle cx="192" cy="178" r="4" class="f-warn" stroke="currentColor"/> <circle cx="315" cy="142" r="4" class="f-warn" stroke="currentColor"/> <circle cx="560" cy="68" r="4" class="f-warn" stroke="currentColor"/> <line x1="572" y1="118" x2="596" y2="118" stroke="currentColor" stroke-width="2"/> <circle cx="584" cy="118" r="5" class="f-warn" stroke="currentColor"/> <text x="604" y="122" fill="currentColor">비연쇄 PI: A</text> <polyline points="70,215 131,215 192,215 315,215 560,215" fill="none" stroke="currentColor" stroke-width="3"/>
  <circle cx="70" cy="215" r="4" class="f-ok" stroke="currentColor"/> <circle cx="131" cy="215" r="4" class="f-ok" stroke="currentColor"/> <circle cx="192" cy="215" r="4" class="f-ok" stroke="currentColor"/> <circle cx="315" cy="215" r="4" class="f-ok" stroke="currentColor"/> <circle cx="560" cy="215" r="4" class="f-ok" stroke="currentColor"/> <line x1="572" y1="144" x2="596" y2="144" stroke="currentColor" stroke-width="3"/> <circle cx="584" cy="144" r="5" class="f-ok" stroke="currentColor"/> <text x="604" y="148" fill="currentColor">연쇄 PI: A</text>
  <text x="530" y="86" fill="currentColor" font-size="12">3+C_M</text> <text x="530" y="58" fill="currentColor" font-size="12">6+C_X</text> <text x="584" y="190" fill="currentColor" font-size="12">PI: 3 (L 의 남은 CS)</text> <text x="584" y="210" fill="currentColor" font-size="12">연쇄: 6 (C 4 + B 2)</text> <text x="584" y="240" fill="currentColor" font-size="12">기울기 1 = unbounded</text> <text x="584" y="260" fill="currentColor" font-size="12">기울기 0 = bounded</text>
</svg>
```

A 의 대기 6 = C 의 남은 S 구간 4 + B 의 S·D 구간 2. 연쇄 상속에서 최악 대기는 **사슬 위 임계 구역들의 합** 이다.

**각 커널은 연쇄를 하나?** (소스로 확인한 것만)

| 커널 | 연쇄(transitive) | 근거 |
|---|---|---|
| FreeRTOS V11.3.2 | **안 함** | `xTaskPriorityInherit` 는 주인 1명만 바꾼다. 주인이 READY 가 아니면 (=다른 것에 막혀 있으면) tasks.c:6805 "Just inherit the priority." — 그 주인이 기다리는 뮤텍스는 보지 않는다 |
| Zephyr ≤ v4.4.2 | 안 함 | `adjust_owner_prio(mutex, new_prio)` 한 번 (v4.4.2 kernel/mutex.c) |
| Zephyr v4.5.0 (2026-10-28) | **함**, 최대 16 홉 | 같은 커밋 1b60add1ac: "Chained priority inheritance through the full ownership chain", `MUTEX_CHAIN_WALK_MAX_HOPS 16`, `CONFIG_MUTEX_DEADLOCK_DETECT` 로 사이클 assert |
| Linux `rt_mutex` | **함** | `rt_mutex_adjust_prio_chain()` (kernel/locking/rtmutex.c), 깊이 상한 `max_lock_depth = 1024` (rtmutex_api.c, sysctl) |

주의: "Zephyr 는 연쇄 상속을 안 한다" 는 널리 퍼진 설명은 **v4.4 까지만** 맞다. 이 레슨을 쓴 시점(2026-11-08) 최신 릴리스 v4.5.0 은 한다. 면접에서는 "버전에 따라 다르다, 최근에 바뀌었다" 고 말할 수 있으면 가장 좋다.

### 무작위 태스크 세트로 불변식 검사

정해진 시나리오 몇 개는 우연히 통과할 수 있다. 그래서 `random_set()` 이 xorshift32 (고정 시드) 로 태스크 4~7개, 뮤텍스 3개짜리 세트를 300개 만들고(절반은 낮은 prio 부터 출발시켜 상속이 자주 생기게), 매 tick 끝에 하네스가 검사한다:

- 아래쪽: 모든 대기자 w 에 대해 `prio(주인) >= prio(w)` (상속 부족 없음)
- 위쪽: `prio(t)` 는 base 이거나 어떤 대기자의 prio 와 같다 (과상속 없음)
- 아무도 **빈 뮤텍스** 를 기다리지 않고, 모든 태스크가 끝난다 (handoff 가 맞다)

```text
random: 19200 ticks checked (385 with a 2+ level chain), invariant violations=0, waits-on-free-mutex=0
```

이 검사가 진짜 버그를 잡는지 돌연변이(mutation)로 확인했다 (`/tmp` 의 임시 사본, 레슨 파일 아님): `pi_on_block` 을 1단계로 자르면 정해진 테스트 7개 실패 + 불변식 위반 77회, `RESTORE_EXACT` 를 FreeRTOS 식으로 바꾸면 7개 실패 + 위반 281회. `np <= prio` 를 `np < prio` 로 바꾸는 것은 의미가 같아 51/51 그대로였다.

### 상속의 한계 — 그래서 RTOS-M05 가 있다

- **연쇄 대기(chained blocking)**: H 가 R1, R2 를 차례로 쓰고, 두 개를 서로 다른 하위 태스크가 쥐고 있으면 H 는 **두 번 따로** 막힌다 (연습 1: 3 + 3 = 6 tick, 'B' 구간 2개). Sha·Rajkumar·Lehoczky (1990) 의 결과: 기본 상속 프로토콜에서 한 작업은 하위 작업 n 개, 자신을 막을 수 있는 세마포어 m 개일 때 최대 **min(n, m) 개의 임계 구역** 만큼 막힐 수 있다. 우선순위 천장(PCP/IPCP)은 이를 **1 개** 로 줄인다.
- **교착(deadlock)** 은 막지 못한다. 반대 순서로 잡으면 상속이 있어도 둘 다 영원히 막힌다 (연습 3). 감지(Linux `-EDEADLK`, Zephyr v4.5 assert)는 가능하지만 예방은 천장 프로토콜이나 잠금 순서 규칙의 몫 → RTOS-M05.
- **비용**: 연쇄 탐색은 스케줄러 락(=인터럽트 지연) 안에서 돈다. Zephyr 가 16 홉에서 끊고, 타임아웃 시의 "내리기" 는 사슬 아래로 전파하지 않는다고 주석에 적은 이유다 (v4.5 kernel/mutex.c 머리 주석 "Known limitations"). 실무 답: 중첩 락을 피하고 사슬을 짧게.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 51/51 passed (5회 반복 동일)
./solution -v                                                                 # 위 풀이의 타임라인 전부
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 51/51, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o sol_tsan && ./sol_tsan     # 51/51, 경고 없음 (스레드 없음 — 참고용)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 해제 시 `prio = base` | 다른 뮤텍스에 아직 대기자가 있으면 역전 재발 (Part B: M 대기 `7 + C_X`) | `prio = inherited_prio(k, c)` — 해제 후 재계산 |
| "올린 값을 스택에 push, 해제 때 pop" | 해제 순서가 잡은 순서와 다르면(비 LIFO) 엉뚱한 값으로 복원. 대기자가 타임아웃으로 빠져도 스택은 모른다 | 기억하지 말고 **현재 대기 관계에서 계산** |
| 상속을 대기자 1명 기준으로: `owner.prio = waiter.prio` | 더 높은 대기자가 이미 있으면 **내려 버린다** | `max(현재, 공식)` — 막힐 때는 절대 내리지 않는다 |
| 연쇄 없이 주인만 올림 | 주인이 또 막혀 있으면 사슬 끝이 중간 태스크에게 선점 (Part C: `6 + C_X`) | `blocked_on → owner` 루프 |
| 연쇄 루프에 종료 조건 없음 | 교착 사이클(A↔B)에서 무한 루프 — 스케줄러 락 안이면 시스템 정지 | 홉 상한 + "안 올랐으면 멈춤" + 사이클 감지(연습 3) |
| 새 주인(handoff 받은 쪽)의 상속을 잊음 | 남은 대기자가 있는데 새 주인이 base 로 돌면 다시 역전 | `new_owner.prio = max(new_owner.prio, inherited_prio(new_owner))` |
| FreeRTOS 에서 `xSemaphoreCreateBinary()` 로 자원 보호 | 바이너리 세마포어는 상속 경로를 타지 않는다 (queue.c:1791 은 `queueQUEUE_IS_MUTEX` 일 때만) | 자원 보호는 `xSemaphoreCreateMutex()`, 신호는 세마포어/알림 (RTOS-B10, M01) |
| 상속이 있으니 임계 구역 안에서 느린 일을 해도 된다 | 상속은 대기를 **하위 임계 구역 길이로** 묶을 뿐 — 그 길이가 곧 상위 태스크의 최악 대기 | 임계 구역 안에서는 복사만, `printf`/플래시 쓰기는 밖에서 |
| 숫자 방향 혼동 | FreeRTOS 는 클수록 높음, Zephyr·Linux 커널 내부는 작을수록 높음 | 비교 연산을 커널의 매크로(`z_is_prio_higher` 등)로 |

## 8. 면접 Q&A

<details>
<summary>Q1. Mars Pathfinder 를 30초로 설명하고, 상속과 천장 프로토콜을 비교해 보세요.</summary>

1997년 Pathfinder 착륙 후 VxWorks 가 반복적으로 전체 리셋됐다. 저우선 기상 데이터 태스크(ASI/MET)가 정보 버스 뮤텍스를 쥔 채, 중간 우선순위 통신 태스크들에게 선점당했고, 그 뮤텍스를 기다리던 고우선 버스 관리 태스크(bc_dist)가 주기를 놓쳐 워치독이 리셋을 걸었다. 뮤텍스의 우선순위 상속 옵션이 꺼져 있었고, JPL 은 지상에서 재현한 뒤 그 옵션을 켜는 패치를 업링크했다 (Glenn Reeves 의 1997년 설명, 상세는 RTOS-M03). 비교: **상속(PI)** 은 막히는 순간에만 주인을 올린다 — 동적, 설정 불필요, 하지만 연쇄 대기로 여러 번 막힐 수 있고(min(n, m) 구역) 교착을 못 막는다. **천장(IPCP)** 은 잠그는 순간 미리 정한 천장으로 올린다 — 최대 1 구역만 막히고 교착도 막지만, 각 뮤텍스를 누가 쓰는지 미리 알아야 하고 경합이 없어도 우선순위를 바꾼다 (RTOS-M05).

</details>

<details>
<summary>Q2. 우선순위 상속을 커널에 넣는다면 TCB 와 뮤텍스에 무엇을 추가하고, 어디서 무엇을 하나요?</summary>

TCB: `base_prio` (원래 값), 현재 `prio`, `blocked_on` (기다리는 뮤텍스), 가진 뮤텍스 목록 또는 개수. 뮤텍스: `owner` + 우선순위 순 대기열. 동작: ① lock 이 막히면 대기열에 넣고 주인을 `max(현재, 대기자)` 로 올린다. 주인이 READY 리스트에 있으면 **새 우선순위의 리스트로 옮겨야** 한다 (FreeRTOS `xTaskPriorityInherit` 가 `uxListRemove` 후 `prvAddTaskToReadyList` 하는 이유). 주인이 막혀 있으면 연쇄로 위로. ② unlock 은 최고 대기자에게 넘기고 자신은 `max(base, 남은 뮤텍스들의 최고 대기자)` 로 재계산. ③ 대기자 타임아웃 시 주인을 재계산. 꼬리: "대기열이 우선순위 순이면 대기자 prio 가 바뀔 때는?" → 대기열 안 위치도 다시 정렬해야 한다 (Linux 는 `rt_mutex_adjust_prio_chain` 에서 requeue, FreeRTOS 는 이벤트 리스트 item 값만 갱신).

</details>

<details>
<summary>Q3. 뮤텍스 두 개를 쥐고 하나만 풀면 우선순위를 어디로 돌려야 하나요? FreeRTOS 는 어떻게 하나요?</summary>

정답은 `max(base, 아직 쥔 뮤텍스들의 대기자 최고 prio)`. base 로 돌리면 남은 뮤텍스의 대기자가 다시 역전당한다 (Part B: M 대기 `7 + C_X`). FreeRTOS V11.3.2 는 단순화한다: `uxMutexesHeld` 가 0 이 될 때만 base 로 돌린다 (tasks.c:6869–6870 "Only disinherit if no other mutexes are held"). 그래서 하나를 풀어도 계속 높은 값에 머문다 — 역전은 안 생기지만 **과상속**: 이미 뮤텍스를 넘겨받은 상위 태스크도 같은 우선순위라 기다리고(Part B: H 가 3 tick 더), 그 사이 우선순위 태스크들도 밀린다. Linux `rt_mutex` 는 태스크별 `pi_waiters` 트리로 정확히 계산하고, Zephyr 는 v4.5.0 에서 정확 계산으로 바뀌었다.

</details>

<details>
<summary>Q4. 상속은 연쇄(transitive)로 전파되나요? FreeRTOS, Zephyr, Linux 는?</summary>

정의상 전파돼야 한다: A 가 B 를, B 가 C 를 기다리면 C 도 A 의 우선순위가 필요하다 — 아니면 중간 우선순위 X 가 C 를 선점해 A 가 다시 무한정 기다린다 (Part C: `6 + C_X`). FreeRTOS V11.3.2 는 **안 한다**: `xTaskPriorityInherit` 는 주인 1명의 우선순위만 바꾸고, 주인이 막혀 있으면 "Just inherit the priority." (tasks.c:6805) 로 끝낸다. Zephyr 는 v4.4 까지 안 했고 v4.5.0 부터 최대 16 홉까지 한다. Linux `rt_mutex` 는 `rt_mutex_adjust_prio_chain()` 으로 사슬을 따라가며 깊이 `max_lock_depth`(기본 1024) 에서 멈춘다. 꼬리: "FreeRTOS 에서 그럼 어떻게 하나?" → 중첩 락을 설계로 피한다 (한 번에 한 뮤텍스, 공유 자원은 전담 태스크 + 큐), 또는 사슬을 이루는 태스크들의 우선순위를 맞춘다.

</details>

<details>
<summary>Q5. 상속을 켜면 상위 태스크의 최악 블로킹 시간은 얼마인가요? 왜 천장 프로토콜이 더 낫다고 하나요?</summary>

상속은 막힘을 **하위 태스크의 임계 구역 길이** 로 묶는다 (Part A: 3 tick, C_M 무관). 하지만 한 번만이 아니다: Sha·Rajkumar·Lehoczky (1990) — 하위 작업 n 개, 자신을 막을 수 있는 세마포어 m 개면 최대 min(n, m) 개 임계 구역만큼 막힐 수 있다 (연습 1: 두 구역, 6 tick). 연쇄 상속에서는 사슬의 구역들이 더해진다 (Part C: 4 + 2). 천장 프로토콜(PCP/IPCP)은 최대 **임계 구역 1개** 로 줄이고 교착도 없앤다. 응답 시간 분석(RTA)에서 블로킹 항 B_i 를 계산할 때 이 차이가 그대로 들어간다 (RTOS-M05, RTA 는 스케줄 가능성 레슨).

</details>

<details>
<summary>Q6. 바이너리 세마포어에는 왜 상속이 없나요? ISR 이 뮤텍스를 잡으면?</summary>

상속은 "누가 주인인가" 가 있어야 한다. 바이너리 세마포어는 주인이 없다 — 아무나 give 할 수 있고, 보통 ISR → 태스크 **신호** 용이라 "누구를 올릴지" 가 정의되지 않는다. FreeRTOS 는 그래서 상속 경로를 `uxQueueType == queueQUEUE_IS_MUTEX` 일 때만 탄다 (queue.c:1791, 1706 에서 `xMutexHolder = pvTaskIncrementMutexHeldCount()`). ISR 은 태스크가 아니라 올릴 우선순위도, 막혀 기다릴 문맥도 없다. FreeRTOS 소스 주석도 "If the mutex is taken by an interrupt, the mutex holder is NULL. Priority inheritance is not applied" (tasks.c:6752 부근), Zephyr 는 `__ASSERT(!arch_is_in_isr(), "mutexes cannot be used inside ISRs")`. 정리는 RTOS-B10.

</details>

<details>
<summary>Q7. 상속을 일으킨 대기자가 타임아웃으로 포기하면 주인의 우선순위는?</summary>

내려야 한다 — 하지만 base 가 아니라 **남은 대기자 중 최고** 까지만. FreeRTOS 는 대기자가 타임아웃되면 대기자 쪽 코드에서 `prvGetHighestPriorityOfWaitToReceiveList()` 로 남은 최고 대기자를 구해 `vTaskPriorityDisinheritAfterTimeout()` 을 부른다 (queue.c:1854–1863). 단 주인이 뮤텍스를 **딱 1개** 쥐고 있을 때만 내린다 (tasks.c:6977, 주석 "This is a simplification") — 2개 이상이면 그대로 둔다 (연습 2 에서 재현: 4 에 머묾). Zephyr v4.5 는 이 내리기를 대기자 문맥에서 지연 처리하고 사슬 아래로는 전파하지 않는다고 문서화했다. 꼬리: "왜 타이머 ISR 에서 바로 안 하나?" → 그러려면 ISR 에서 뮤텍스 내부 락을 잡아야 하고, 사슬이 길면 ISR 지연이 무한정 늘어난다.

</details>

<details>
<summary>Q8. POSIX 에서 상속 뮤텍스는 어떻게 쓰고, Linux 에서는 어디서 구현되나요?</summary>

`pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT)` 로 만든 뮤텍스. 의미 있으려면 스레드가 `SCHED_FIFO`/`SCHED_RR` 실시간 정책이어야 한다. glibc 는 경합 시 `FUTEX_LOCK_PI` 로 커널에 넘기고, 커널은 `rt_mutex` 로 주인을 올린다 — 사슬 탐색(`rt_mutex_adjust_prio_chain`)과 교착 감지(`-EDEADLK`) 포함. PREEMPT_RT 커널에서는 대부분의 spinlock 이 rt_mutex 기반으로 바뀌어 커널 내부도 상속을 받는다. `PTHREAD_PRIO_PROTECT` 는 천장 방식 (RTOS-M05). 맥(이 레슨의 호스트)은 실시간 스케줄링 정책이 달라 이런 실험을 그대로 재현하기 어렵다 — 그래서 결정적 시뮬레이터를 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — chained blocking: 상속이 있어도 두 번 막힌다

H(3) 가 R1 → R2 를 차례로 쓴다. L1(1) 이 R1 을, L2(2) 가 R2 를 쥐고 각각 4 tick 일한다. H 는 몇 번, 몇 tick 막히나? 'B' 구간 수를 세는 `block_episodes(row)` 를 작성하라.

<details>
<summary>답 보기</summary>

```text
ex1 chained blocking
 tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 H    : . . B B B R B B B R . . . .
 L2   : . R r r r r R R R r . . . .
 L1   : R r R R R r r r r r . . . .
```

2 번, 3 + 3 = 6 tick. R1 에서 막혀 L1 을 3 으로 올리고(t2–t4), 풀려서 1 tick 일한 뒤 R2 에서 또 막혀 L2 를 3 으로 올린다(t6–t8). 각각은 bounded 지만 **합** 이 최악값이다 — min(n, m) = min(2, 2) = 2 구역. IPCP 였다면 L1 이 R1 을 잡는 순간 천장 3 으로 올라 L2 가 아예 R2 를 잡지 못하므로 1 구역으로 끝난다 (RTOS-M05). `block_episodes` 는 `row[i] == 'B' && (i == 0 || row[i-1] != 'B')` 를 센다.

</details>

### 연습 2 — 대기자 타임아웃 후 되돌리기

커널에 `LOCK_TO(m, n)` (최대 n tick 대기)을 넣었다. L(1) 이 R1 을 쥐고 6 tick 일하는 동안 H1(3) 은 무한 대기, H2(4) 는 2 tick 만 기다린다. H2 가 포기한 순간 L 의 우선순위를 정하는 `pi_on_timeout()` 을 작성하라. FreeRTOS 모델에서, L 이 뮤텍스를 하나 더(R2, 대기자 없음) 쥐고 있으면 어떻게 되나?

<details>
<summary>답 보기</summary>

정확 모델: `prio = inherited_prio(owner)` — 타임아웃된 대기자는 이미 빠졌으니 H1 의 3 이 남는다. FreeRTOS 모델: `max(base, 그 뮤텍스의 최고 대기자)` 를 `nheld == 1` 일 때만 적용 (tasks.c:6961–6977).

```text
ex2 timeout, EXACT, L holds 1 mutex                ex2 timeout, FreeRTOS model, L holds 2 mutexes
 H2   : . . . B B R . . . . . . . .                  H2   : . . . B B r r R . . . . . .
 H1   : . . B B B B B B R . . . . .                  H1   : . . B B B B B r R . . . . .
 L    : R R R R R r R R r . . . . .                  L    : R R R R R R R r r . . . . .
 Lp   : 1 1 3 4 4 3 3 3 1 1 1 1 1 1                  Lp   : 1 1 3 4 4 4 4 1 1 1 1 1 1 1
```

왼쪽: t5 에 3 으로 내려가 포기한 H2 가 자기 대체 작업을 바로 한다. 오른쪽: L 이 뮤텍스 2개라 4 에 머물고, 포기한 H2 마저 같은 우선순위 L 뒤에서 2 tick 기다린다. 사슬 아래(L 이 또 막혀 있을 때 그 주인)까지 내리는 것은 FreeRTOS·Zephyr 모두 하지 않는다 — 이 연습도 주인 1명만 다룬다.

</details>

### 연습 3 — 사슬을 따라가 교착 감지 (EDEADLK)

A(1) 는 R1 → R2, B(2) 는 R2 → R1 순서로 잡는다. 감지가 없으면 무슨 일이 생기나? lock 하기 전에 소유자 사슬을 따라가 자기 자신이 나오면 실패를 돌려주는 `would_deadlock(k, c, m)` 을 작성하라. 에러를 받은 쪽은 가진 것을 모두 놓고 포기한다.

<details>
<summary>답 보기</summary>

```c
int o = k->m[m].owner;
for (int hops = 0; o >= 0 && hops < MAXT; hops++) {
    if (o == c) return 1;                 /* 사슬이 나에게 돌아옴 = 사이클 */
    int m2 = k->t[o].blocked_on;
    if (k->t[o].st != T_BLOCKED || m2 < 0) return 0;
    o = k->m[m2].owner;
}
return 0;
```

```text
ex3 opposite order, no detection        ex3 opposite order, detection
 A    : R r R B B B B B B B B B           A    : R r R . . . . . . . . .
 B    : . R B B B B B B B B B B           B    : . R B R . . . . . . . .
```

감지가 없으면 "끝나지 않은 태스크가 모두 BLOCKED" — 상속은 교착을 전혀 돕지 못한다. 감지하면 사이클을 **닫으려던** A 가 에러를 받고 R1 을 놓아 B 가 끝난다. 상속 사슬 탐색과 같은 루프라 비용이 거의 없다 — Linux `rt_mutex` 가 사슬 탐색 중 `-EDEADLK` 를 돌려주고, Zephyr v4.5 가 `CONFIG_MUTEX_DEADLOCK_DETECT` 에서 assert 하는 방식이다. 감지는 사후 처리일 뿐, 예방(IPCP·잠금 순서)은 RTOS-M05.

</details>

### 연습 4 — 홉 수 제한이 있으면

T4(5) → T3(3) → T2(2) → T1(1) 의 4단 사슬에 X(4) 가 CPU 일을 C_X tick 들고 온다. `max_hops = 2` 로 제한하면 T4 의 대기는? (Zephyr v4.5 의 `MUTEX_CHAIN_WALK_MAX_HOPS` 는 16.)

<details>
<summary>답 보기</summary>

T4 가 막힐 때 hop 1 T3 ← 5, hop 2 T2 ← 5 에서 멈춘다. 사슬 끝 T1 은 3 (T3 이 막힐 때 받은 값)에 머물러 X(4) 에게 선점된다:

```text
ex4 chain of 4, hops=2
 T4   : . . . B B B B B B B B B R . . . . . . .
 X    : . . . . R R R R . . . . . . . . . . . .
 T1   : R R R R r r r r R R r r r . . . . . . .
 T1p  : 1 2 3 3 3 3 3 3 3 3 1 1 1 1 1 1 1 1 1 1
```

제한 없음: T4 대기 5 (C_X 무관). `hops = 2`: `5 + C_X` (테스트: C_X = 4, 8). 홉 상한은 "사슬 탐색이 스케줄러 락을 오래 잡지 않게" 하는 비용 상한이지 정확성 보장이 아니다 — 상한보다 긴 사슬이 생기면 그 부분은 다시 unbounded 가 된다. Zephyr 소스 주석도 "This is a deliberate bound on the work done under mutex_lock, not a claim that chains longer than the limit cannot legally form" 이라고 적는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 19/19 passed
./practice -v                                                                # 위 타임라인들
```

## 10. 핵심 정리

- 상속 우선순위는 기억하는 값이 아니라 **계산하는 값**: `prio(t) = max(base(t), t 가 가진 모든 뮤텍스의 대기자 prio)`. 막힐 때 올리고(절대 내리지 않음), 풀 때·타임아웃 때 재계산.
- 정준 시나리오: 상속 없음 H 대기 `3 + C_M` → 상속 **3** (L 의 남은 임계 구역). 상속은 역전을 없애는 게 아니라 **하위 임계 구역 길이로 묶는다** — 임계 구역은 짧게.
- 여러 뮤텍스: 해제 시 base 로 내리면 역전 재발(`7 + C_X`). FreeRTOS 는 "전부 풀 때만 base" (tasks.c:6870) → 안전하지만 과상속.
- 연쇄 A→B→C: `blocked_on → owner` 를 따라 올라가는 루프. 없으면 중간 태스크가 사슬 끝을 선점(`6 + C_X`). FreeRTOS V11.3.2 는 비연쇄(tasks.c:6805), Zephyr 는 v4.5.0 부터 16 홉, Linux rt_mutex 는 `max_lock_depth` 1024.
- 상속의 한계: chained blocking (min(n, m) 구역), 교착 못 막음 → 천장 프로토콜(RTOS-M05). 사슬 탐색 = 스케줄러 락 안의 비용 → 홉 상한.
- 펌웨어 실무: 자원 보호는 `xSemaphoreCreateMutex()` (상속 O), ISR→태스크 신호는 세마포어/알림 (상속 X). 중첩 락을 피하는 설계가 최선.

## 참고자료

- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers 39(9), 1990 — 기본 상속 프로토콜, 블로킹 상한, PCP
- Glenn Reeves (JPL), "What really happened on Mars?" (1997, Mike Jones 의 정리 페이지) — <https://www.cs.cornell.edu/courses/cs614/1999sp/papers/pathfinder.html>
- FreeRTOS-Kernel V11.3.2 소스 — tasks.c (`xTaskPriorityInherit` 6745, `xTaskPriorityDisinherit` 6848, `vTaskPriorityDisinheritAfterTimeout` 6942, 동점 규칙 5555), queue.c (`xQueueSemaphoreTake` 1659, 상속 호출 1791–1795, 타임아웃 1854–1863, give 시 disinherit 2410–2413, 순서 바꾼 반납 1062–1068), include/semphr.h — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS 문서 — Mutexes (priority inheritance) — <https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html>
- Zephyr kernel/mutex.c — v4.4.2 (뮤텍스별 `owner_orig_prio`) 와 v4.5.0 (`held_mutexes`, chain walk, `MUTEX_CHAIN_WALK_MAX_HOPS`), 커밋 1b60add1ac (2026-07-13) — <https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/kernel/mutex.c>
- Linux kernel/locking/rtmutex.c (`rt_mutex_adjust_prio_chain`, `rt_mutex_adjust_prio`, `remove_waiter`), rtmutex_api.c (`max_lock_depth = 1024`), Documentation/locking/rt-mutex-design.rst — <https://docs.kernel.org/locking/rt-mutex-design.html>
- POSIX.1-2017 `pthread_mutexattr_setprotocol` (PTHREAD_PRIO_NONE / INHERIT / PROTECT) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutexattr_getprotocol.html>
- G. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 7장 자원 접근 프로토콜 (PI, PCP, SRP)
- 관련 레슨: RTOS-M03 (역전 재현, Pathfinder), RTOS-M05 (IPCP, 교착 회피), RTOS-B10 (뮤텍스 vs 세마포어, ISR 에서 못 쓰는 이유), RTOS-B09 (대기열 순서), RTOS-M01 (태스크 알림), RTOS-A08 (SMP), C-M14 (ISR 공유 데이터·임계 구역)
