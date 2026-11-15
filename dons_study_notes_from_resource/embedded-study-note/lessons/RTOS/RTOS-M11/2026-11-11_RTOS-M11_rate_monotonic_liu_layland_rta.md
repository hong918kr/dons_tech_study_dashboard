---
id: RTOS-M11
time: 90분
updated: 2026-11-11
---
# RTOS-M11 Rate Monotonic 분석: Liu-Layland 경계와 RTA

## 학습 목표

- 태스크 세트 `(C, T, D)` 를 받아 **RM(주기 짧을수록 높음) / DM(데드라인 짧을수록 높음)** 우선순위를 할당하고, 이용률 경계 두 개(Liu–Layland, 쌍곡선)와 **응답 시간 분석(RTA, response time analysis)** 을 돌리는 도구를 C 로 짠다.
- 무리수 경계 `n(2^{1/n} − 1)` 을 **float 없이** 판정한다: 양변을 n 제곱해 `(P + nQ)^n ≤ 2(nQ)^n` 정수 비교로 바꾼다. double 로 하면 실제로 틀리는 세트 3개를 직접 돌려 본다.
- "경계는 실패, RTA 는 통과" 하는 세트와 "U ≤ 1 인데 RTA 실패" 하는 세트를 찾아 설명하고, 임계 순간(critical instant) 시뮬레이션으로 **최악 관측 응답 == R** 을 확인한다.
- 블로킹 항 B (IPCP, RTOS-M05) 와 릴리즈 지터 J 를 넣은 확장식 `w = C + B + Σ⌈(w + J_j)/T_j⌉C_j`, `R = w + J_i` 로 모터 1 kHz · IMU 500 Hz · 통신 · 로깅 펌웨어를 분석하고, 무엇을 고치면 통과하는지 숫자로 말할 수 있다.

## 1. 문제

"태스크 네 개의 주기와 실행 시간을 줄 테니 데드라인을 다 지키는지 말해 보세요. 이용률이 85 % 인데 Liu–Layland 경계 75.7 % 를 넘었으니 안 되는 건가요?" — 펌웨어 면접에서 RTOS 질문이 "스케줄러를 써 봤다" 수준을 넘어가면 바로 나오는 질문이다. 정답은 "경계는 **충분조건** 일 뿐이고, 정확한 답은 RTA 로 낸다" 이고, 그 RTA 를 화이트보드에서 손으로 반복 계산할 수 있어야 한다. 실무에서도 STM32 모터 컨트롤러에 로깅 태스크 하나 넣을 때 "CPU 여유가 있나?" 를 이 계산으로 답한다.

이 레슨의 도구는 RTOS-B·M 시리즈 미니 커널과 같은 가정을 쓴다: 단일 코어, 고정 우선순위 선점(preemptive), 우선순위는 **클수록 높다** (FreeRTOS 방향, Zephyr 는 반대). 단위는 µs 또는 tick, 전부 정수.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">판정 사다리 — 무작위 400 세트 (solution.c B9 의 실제 개수)</text>
  <rect x="20" y="34" width="700" height="264" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="32" y="54" fill="currentColor">U ≤ 1 — 필요조건일 뿐: 146</text>
  <rect x="40" y="70" width="500" height="218" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="52" y="90" fill="currentColor">RTA 통과 = 정확한 판정 (D ≤ T, 독립 태스크): 122</text>
  <rect x="60" y="106" width="300" height="172" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="72" y="126" fill="currentColor">쌍곡선 Π(U_i + 1) ≤ 2: 66</text>
  <rect x="80" y="142" width="170" height="126" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="92" y="162" fill="currentColor">LL U ≤ n(2^(1/n)−1)</text>
  <text x="92" y="180" fill="currentColor">: 58</text>
  <text x="92" y="214" fill="currentColor" font-size="12">A1 (20,100)(40,150)</text>
  <text x="92" y="230" fill="currentColor" font-size="12">(100,350) U=0.752</text>
  <text x="262" y="214" fill="currentColor" font-size="12">B2 (1,6)(5,7)</text>
  <text x="262" y="230" fill="currentColor" font-size="12">Π = 2 정확히</text>
  <text x="376" y="150" fill="currentColor" font-size="12">A3 펌웨어 U=0.85</text>
  <text x="376" y="168" fill="currentColor" font-size="12">A4 조화 U=1.00</text>
  <text x="376" y="186" fill="currentColor" font-size="12">경계 둘 다 실패,</text>
  <text x="376" y="202" fill="currentColor" font-size="12">RTA 는 통과</text>
  <text x="556" y="150" fill="currentColor" font-size="12">A5 (2,5)(4,7)</text>
  <text x="556" y="168" fill="currentColor" font-size="12">U = 0.971 ≤ 1</text>
  <text x="556" y="186" fill="currentColor" font-size="12">그래도 RM 미스</text>
  <text x="556" y="250" fill="currentColor" font-size="12">안쪽일수록 비관적</text>
  <text x="556" y="268" fill="currentColor" font-size="12">(빠르지만 거짓 "불가")</text>
</svg>
```

**입력 형식** (주어진 파서 `ts_parse`):

| 형식 | 의미 |
|---|---|
| `name C T` | D = T (암묵적 데드라인) |
| `name C T D` | 제약 데드라인(constrained), `C ≤ D ≤ T` 이어야 함 |
| `J=n`, `B=n` | 릴리즈 지터, 블로킹 (Part C). `J < T` |
| `;` 또는 줄바꿈, `#` 주석 | 태스크 구분 |
| 오류 | `PARSE_RANGE` (C=0, C>D, D>T, J≥T, T>10 s), `PARSE_SYNTAX`, `PARSE_TOO_MANY` (17개 이상) |

### Part A — 할당, 두 경계, RTA, 시뮬레이션 교차 확인

- `assign_prio(ts, POL_RM / POL_DM)` — **TODO 1.** 키(T 또는 D)가 작을수록 높게, 같으면 입력 순서가 앞선 쪽이 높게. `prio = n − 1 − rank`.
- `ll_test(ts)` — **TODO 2.** `U ≤ n(2^{1/n} − 1)` 을 정수로. 주어진 `util_frac()` 가 `U = P/Q` 를 bignum 으로 준다. `D ≠ T` 가 하나라도 있으면 `UT_NA`.
- `hyperbolic_test(ts)` — **TODO 3.** `Π(C_i + T_i) ≤ 2ΠT_i`.
- `rta(ts, i)` — **TODO 4.** 고정점 반복, 정수 ceil, `R > D` 면 멈춤. 반복 횟수도 센다.
- `sim_fp(ts, horizon, blocker, jit, &s)` — **TODO 5.** t = 0 동시 출시부터 1 tick 씩 고정 우선순위 선점 실행, 작업별 응답 시간 · 미스 · 선점 횟수 기록. RM vs EDF 전체 시뮬레이터는 RTOS-M12 몫이라 여기선 최소 기능만.
- 테스트: 교과서 세트(LL 통과), **펌웨어 세트 U = 0.85 (경계 둘 다 실패, RTA 통과)**, 조화 주기 U = 1 (RTA 통과, R = D 딱 맞음), **(2,5)(4,7): U = 0.971 ≤ 1 인데 RTA 실패**. 통과 세트는 하이퍼피리어드 동안 시뮬레이션의 최악 응답이 R 과 **같아야** 한다.

### Part B — 면접관이 파고드는 경계 사례

- 경계에 딱 붙은 세트: n=1 U=1, n=2 U=0.8284 / 0.8286, 쌍곡선 곱이 **정확히 2** 인 (1,6)(5,7). double 로는 `2.0000000000000004` 가 나와 틀린다 (`demo_float_trap.c`).
- 상위 태스크 이용률이 1 → 하위 태스크의 w 가 끝없이 커진다. `R > D` 에서 멈추는지.
- `D < T`: 이용률 경계는 **적용 불가**. RM 은 실패, DM 은 통과하는 세트.
- 같은 주기(동점), 파서 오류, 하이퍼피리어드 오버플로 가드, 16 태스크 bignum 크기.
- 무작위 400 세트로 함의 관계 `LL ⇒ 쌍곡선 ⇒ RTA ⇒ U ≤ 1` 와 "시뮬레이션 == RTA" 를 검사.

### Part C — 펌웨어 시나리오: 블로킹 B 와 지터 J

STM32 계열 모터 컨트롤러. 모든 시간은 µs.

| 태스크 | C | T | D | J | 자원 |
|---|---|---|---|---|---|
| motor (속도 루프 1 kHz) | 300 | 1000 | 1000 | 0 | setpoint 뮤텍스 20 µs |
| imu (500 Hz, DRDY ISR 이 깨움) | 400 | 2000 | 2000 | 100 | SPI 버스 80 µs |
| comms (CAN 명령, 4 ms 안에 응답) | 1500 | 10000 | 4000 | 300 | setpoint 뮤텍스 50 µs |
| log (SPI 플래시 기록) | 20000 | 100000 | 100000 | 0 | SPI 버스 **1200 µs** (페이지 쓰기) |

- `blocking_ipcp(ts, uses, n)` — **TODO 6.** 자원별 천장(ceiling) → `B_i = max{cs : prio(쓰는 태스크) < prio(i), ceiling ≥ prio(i)}`.
- RTA 가 B, J 를 반영하면 어느 태스크가 미스하나? 플래시 쓰기를 200 µs 조각으로 나누면? 시뮬레이션(블로커 = B_i, 지터 최악 출시 패턴)의 첫 작업 응답이 `R_i − J_i` 와 같은지.

## 2. Problem (English)

> **Fixed-priority schedulability analyzer.**
>
> Input: up to 16 periodic tasks `name C T [D] [J=..] [B=..]` (integers, µs). Implement:
>
> 1. `void assign_prio(taskset_t *ts, policy_t pol)` — Rate Monotonic (shorter T = higher priority) or Deadline Monotonic (shorter D). Ties: lower input index wins. Larger number = higher priority.
> 2. `ut_t ll_test(const taskset_t *ts)` — Liu & Layland test `U ≤ n(2^{1/n} − 1)`. **No floating point**: decide it exactly with integers. Return `UT_NA` if any `D ≠ T`.
> 3. `ut_t hyperbolic_test(const taskset_t *ts)` — `Π(U_i + 1) ≤ 2`, exactly.
> 4. `rta_t rta(const taskset_t *ts, int i)` — iterate `w = C_i + B_i + Σ_{j∈hp(i)} ⌈(w + J_j)/T_j⌉·C_j` from `w = C_i + B_i`; stop as soon as `w + J_i > D_i` (unschedulable) or `w` stops changing; `R_i = w + J_i`. hp(i) = other tasks with priority ≥ prio(i).
> 5. `void sim_fp(...)` — discrete-time fixed-priority preemptive simulation from the synchronous release; record per-task worst/first response, misses, preemptions.
> 6. `void blocking_ipcp(taskset_t *ts, const use_t *u, int nu)` — blocking terms under the immediate priority ceiling protocol.
>
> Examples: `{(20,100),(40,150),(100,350)}` → U = 0.752, LL pass, R = 20, 60, 240. `{(300,1000),(400,2000),(1500,10000),(20000,100000)}` → U = 0.85, LL **fail**, RTA pass with R = 300, 700, 3500, 58000. `{(2,5),(4,7)}` → U = 0.971 ≤ 1 but R_2 = 8 > 7.
>
> Follow-ups: Why is LL only sufficient? What does ln 2 mean here? Why does the iteration terminate? Is RTA exact? How do you get B and J on a real MCU, and where do ISRs and context-switch cost go? What changes if D < T, or if two tasks share a priority with time slicing?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter       # 처음엔 15/51 passed, exit 1
./starter -v                                                                 # 추가 출력 (시뮬레이션 요약, C 단계 확인)
```

처음 상태에서는 파서 · 하이퍼피리어드 가드 · bignum 크기 같은 **주어진 코드** 테스트 15개만 통과한다. 스텁 RTA 는 `ok = 0` 을 돌려주고 스텁 시뮬레이션은 아무것도 실행하지 않으므로 무한 루프 없이 끝난다. 단일 스레드 · 결정적 프로그램이라 `-pthread`, TSan 은 필요 없다.

## 4. 힌트

<details>
<summary>힌트 1 — TODO 1: 정렬하지 말고 rank 를 세라</summary>

n ≤ 16 이니 O(n²) 이 가장 읽기 쉽다. 태스크 i 에 대해 "키가 나보다 작거나, 같은데 인덱스가 작은 j" 의 수가 rank. `prio = n − 1 − rank` 면 모두 다르고 0..n−1 이다. 정렬(qsort)은 동점 처리가 비결정적일 수 있다 (C 표준은 qsort 의 안정성을 보장하지 않는다).

</details>

<details>
<summary>힌트 2 — TODO 2: 무리수를 계산하지 말고 없애라</summary>

`U ≤ n(2^{1/n} − 1)` ⇔ `U/n + 1 ≤ 2^{1/n}` ⇔ `(U/n + 1)^n ≤ 2` (양변 양수, x^n 은 단조 증가). U = P/Q 를 넣고 분모 `(nQ)^n` 을 곱하면 `(P + nQ)^n ≤ 2·(nQ)^n`. 필요한 연산은 `big_mul_u32`, `big_add`, `big_pow`, `big_cmp` 뿐이다.

</details>

<details>
<summary>힌트 3 — TODO 4: 반복이 왜 끝나나</summary>

오른쪽 식은 w 에 대해 단조 증가 계단 함수다. w0 = C + B 에서 시작하면 w_k 는 줄지 않고, 바뀔 때마다 최소 1 (어떤 C_j) 이상 커진다. 그러니 "같아지거나(수렴) D 를 넘거나" 둘 중 하나가 유한 번 안에 일어난다. ceil 은 `(a + b − 1) / b`. `R > D` 검사를 수렴 검사보다 **먼저** 하면 지터 J_i 까지 포함해 깔끔하다.

</details>

<details>
<summary>힌트 4 — TODO 5: 작업 큐 없이 시뮬레이션하기</summary>

태스크마다 `rel` (출시한 작업 수), `done` (끝낸 작업 수), `rem` (지금 작업의 남은 일) 세 개면 된다. 미완료 작업이 있다 = `rel > done`. 끝낸 작업의 출시 시각은 `rel_time(t, done)` 으로 다시 계산할 수 있으니 큐가 필요 없다. 응답 = 완료 시각(t + 1) − 출시 시각.

</details>

<details>
<summary>힌트 5 — TODO 6: 천장은 "쓰는 사람 중 최고"</summary>

먼저 자원마다 ceiling 을 구한다 (사용 표를 한 번 훑는다). 그다음 태스크 i 마다 "나보다 낮은 태스크가 쥐는, 천장이 나 이상인 임계 구역" 중 최댓값 하나. IPCP 에서는 **하나만** 막힌다는 게 RTOS-M05 의 결론이라 합이 아니라 max 다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 RTOS 대응표

| 도구의 가정 / 이름 | FreeRTOS V11.3.2 | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준) |
|---|---|---|---|
| 우선순위 클수록 높음, 서로 다름 | `uxPriority`, 0 = `tskIDLE_PRIORITY` (task.h:210) ~ `configMAX_PRIORITIES − 1` | 숫자가 **작을수록** 높음, 음수 = 협조형 | `osPriority_t`, `osPriorityHigh` > `osPriorityNormal` |
| 선점: 더 높은 태스크가 깨면 즉시 전환 | tick 에서 깬 태스크가 **더 높을 때만** 전환 (tasks.c:4944 `>`) | 선점형 스레드 | 커널 구현 (RTX5 등) 에 따름 |
| 주기 T 로 출시 | `xTaskDelayUntil(&last, pdMS_TO_TICKS(T))` (tasks.c:2414, projdefs.h:42) | `k_timer` 주기 / 절대 시각 sleep | `osDelayUntil()` |
| 같은 우선순위 = hp 로 간주(보수적) | `configUSE_TIME_SLICING` 기본 1 (FreeRTOS.h:2771–2772) → tick 마다 같은 우선순위끼리 전환 (tasks.c:4967–4998) | 같은 우선순위 time slice 옵션 | — |
| B (IPCP) | 천장 뮤텍스 없음 → 뮤텍스는 상속(PI) 만. IPCP 흉내는 `vTaskPrioritySet` (RTOS-M05) | `k_mutex` 상속 | `osMutexPrioInherit` 속성 |
| J (릴리즈 지터) | ISR → `xSemaphoreGiveFromISR` + `portYIELD_FROM_ISR` 지연 (RTOS-M02) | ISR → `k_sem_give` | ISR → `osSemaphoreRelease` |
| C (WCET) | 측정: DWT CYCCNT (RTOS-A03) | 같음 | 같음 |

FreeRTOS 의 준비 리스트 선택은 "비어 있지 않은 가장 높은 우선순위 리스트" (tasks.c:197–212 `taskSELECT_HIGHEST_PRIORITY_TASK`) — 즉 **고정 우선순위 선점 스케줄러** 그 자체다. RM/DM 은 커널 기능이 아니라 **설계자가 우선순위 숫자를 고르는 규칙** 이고, RTA 는 그 선택이 맞는지 오프라인에서 확인하는 계산이다. EDF 는 FreeRTOS 에 없다 (RTOS-M12).

### Part A — 할당과 두 경계

**RM 할당** (TODO 1). 주기가 짧을수록 높게. Liu & Layland (1973) 는 독립 · 주기적 · D = T · 선점 · 단일 코어 조건에서 RM 이 **고정 우선순위 중 최적** 임을 보였다 — RM 으로 안 되면 어떤 고정 우선순위로도 안 된다. 입력 순서와 상관없이 정해진다 (테스트 A2: `log, imu, motor, comms` 순서로 넣어도 motor 3, imu 2, comms 1, log 0).

**Liu–Layland 경계를 정수로** (TODO 2). n 태스크면 `U ≤ n(2^{1/n} − 1)` 일 때 RM 으로 스케줄 가능 (충분조건). n 이 커지면 `ln 2 ≈ 0.693` 으로 내려간다:

| n | 1 | 2 | 3 | 4 | 5 | 8 | 16 | ∞ |
|---|---|---|---|---|---|---|---|---|
| 경계 | 1.000 | 0.8284 | 0.7798 | 0.7568 | 0.7435 | 0.7241 | 0.7084 | 0.6931 |

n ≥ 2 에서 이 값은 무리수다. double 로 `n * (pow(2, 1.0/n) - 1)` 을 계산하고 U 를 double 로 더해 비교하면, U 가 경계 근처일 때 반올림에 판정이 맡겨진다. 그래서 무리수를 **계산하지 않는다**:

```c
/* U = P/Q (util_frac: Q = ΠT, P = Σ C_i Π_{j≠i} T_j)
 * U ≤ n(2^(1/n) − 1)  ⇔  (U/n + 1)^n ≤ 2  ⇔  (P + nQ)^n ≤ 2 (nQ)^n */
nQ = Q;   big_mul_u32(&nQ, n);
base = P; big_add(&base, &nQ);
big_pow(&lhs, &base, n); big_pow(&rhs, &nQ, n); big_mul_u32(&rhs, 2);
return big_cmp(&lhs, &rhs) <= 0 ? UT_PASS : UT_FAIL;
```

크기: n ≤ 16, T ≤ 2^24 이면 Q ≤ 2^384, P + nQ ≤ 2nQ (C ≤ T 라 U ≤ n), 16 제곱해도 2^6224 < 8192 비트 (`BIGL 256` limb). 테스트 B8 이 16 태스크 · 주기 약 10^7 로 이 상한 근처를 돌린다. 표시용 U (`U~0.7523`) 와 경계 (`LL_PPM` 표) 는 ppm 정수로 **출력에만** 쓴다.

**쌍곡선 경계** (TODO 3). Bini · Buttazzo · Buttazzo (2003) 의 `Π(U_i + 1) ≤ 2`. 역시 충분조건이지만 LL 보다 덜 비관적이다 — LL 을 통과하는 세트는 항상 쌍곡선도 통과한다 (산술–기하 평균으로 보일 수 있다: 같은 U 합에서 곱 `Π(1 + U_i)` 는 모든 U_i 가 같을 때 최대이고, 그때 쌍곡선 조건이 정확히 LL 조건이 된다). 정수로는 `Π(C_i + T_i) ≤ 2ΠT_i` — 곱셈만 있어서 더 쉽다.

### Part A — RTA: 계단 함수의 고정점

태스크 i 의 첫 작업이 모든 상위 태스크와 **동시에** 출시되면 (임계 순간, Liu & Layland 1973 정리 1) 응답이 가장 길다. 그 응답 w 는 "w 동안 들어오는 상위 작업 전부 + 자기 일" 이다:

`w = C_i + Σ_{j∈hp(i)} ⌈w / T_j⌉ · C_j`

w 가 양변에 있으니 반복으로 푼다 (Joseph & Pandya 1986). A1 의 t3 (C=100, T=350) 를 손으로:

| k | w_k | ⌈w/100⌉·20 | ⌈w/150⌉·40 | w_{k+1} |
|---|---|---|---|---|
| 0 | 100 | 1·20 | 1·40 | 160 |
| 1 | 160 | 2·20 | 2·40 | 220 |
| 2 | 220 | 3·20 | 2·40 | 240 |
| 3 | 240 | 3·20 | 2·40 | **240** (수렴, iters = 4) |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">A1 t3: 요구 함수 W(t) = 100 + ⌈t/100⌉·20 + ⌈t/150⌉·40 와 y = t 의 첫 교점 = R</text>
  <line x1="60" y1="280" x2="680" y2="280" stroke="currentColor"/>
  <line x1="60" y1="280" x2="60" y2="36" stroke="currentColor"/>
  <line x1="60" y1="280" x2="660" y2="40" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="600" y="48" fill="currentColor" font-size="12">y = t</text>
  <path d="M60 152 L260 152 M260 136 L360 136 M360 104 L460 104 M460 88 L660 88" stroke="currentColor" stroke-width="3" fill="none"/>
  <text x="560" y="80" fill="currentColor" font-size="12">W(t) (계단)</text>
  <path d="M260 152 L380 152 L380 104 L500 104 L500 88 L540 88" stroke="currentColor" stroke-width="1.5" fill="none" stroke-dasharray="3 3"/>
  <circle cx="260" cy="152" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="380" cy="104" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="500" cy="88" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="540" cy="88" r="7" class="f-ok" stroke="currentColor"/>
  <text x="200" y="172" fill="currentColor" font-size="12">w0=100 → 160</text>
  <text x="330" y="124" fill="currentColor" font-size="12">160 → 220</text>
  <text x="430" y="76" fill="currentColor" font-size="12">220 → 240</text>
  <text x="520" y="114" fill="currentColor" font-size="12">R = 240 (고정점)</text>
  <g fill="currentColor" font-size="12">
    <text x="56" y="298">0</text><text x="248" y="298">100</text><text x="348" y="298">150</text><text x="448" y="298">200</text><text x="528" y="298">240</text><text x="648" y="298">300</text>
    <text x="24" y="156">160</text><text x="24" y="108">220</text><text x="24" y="92">240</text><text x="24" y="204">100</text>
  </g>
  <text x="300" y="320" fill="currentColor" font-size="12">t (µs 단위가 아닌 tick, 원점 = 임계 순간)</text>
</svg>
```

W(t) 는 "길이 t 구간에 들어오는 최악 일감", 대각선은 "그 구간에 CPU 가 줄 수 있는 시간". 처음으로 공급이 요구를 따라잡는 지점이 응답 시간이다. 반복은 그 교점을 **아래에서** 계단을 타고 올라간다 — 그래서 수렴값은 가장 작은 고정점이고, 시작값을 `C_i` 대신 `C_i + Σ C_j` 로 잡아도 (그 값도 R 이하이므로) 같은 답이 나온다.

**실제 출력** (`./solution -v`, `sim over …` 줄은 `-v` 에서만):

```text
== A1 textbook (LL passes)  (n=3, U~0.7523, LL bound~0.7797)
   LL PASS | hyperbolic PASS | U<=1 yes
   t1           20     100     100     0     0    2 |      20     1 ok
   t2           40     150     150     0     0    1 |      60     2 ok
   t3          100     350     350     0     0    0 |     240     4 ok
== A3 firmware set, D = T (LL fails, RTA passes)  (n=4, U~0.8500, LL bound~0.7568)
   LL FAIL | hyperbolic FAIL | U<=1 yes
   motor       300    1000    1000     0     0    3 |     300     1 ok
   imu         400    2000    2000     0     0    2 |     700     2 ok
   comms      1500   10000   10000     0     0    1 |    3500     4 ok
   log       20000  100000  100000     0     0    0 |   58000     9 ok
   sim over hyperperiod 100000: max resp 300 700 3500 58000, preemptions 69
== A4 harmonic, U = 1  (n=3, U~1.0000, LL bound~0.7797)
   LL FAIL | hyperbolic FAIL | U<=1 yes
   c             5      20      20     0     0    2 |       5     1 ok
   b            10      40      40     0     0    1 |      15     2 ok
   a            40      80      80     0     0    0 |      80     4 ok
```

(표의 머리줄 `task C T D J B prio | R iter` 는 생략.) A3 이 이 레슨의 첫 번째 답이다: **U = 0.85 > 0.7568 이라 LL 은 "모른다" 고 하지만, RTA 는 모든 태스크가 넉넉히 통과한다** (log 는 58 ms / 100 ms). 쌍곡선도 `1.3·1.2·1.15·1.2 = 2.15 > 2` 로 실패. A4 는 주기가 서로 나누어떨어지는 조화(harmonic) 세트라 U = 1 까지 통과하고, t3 의 R = 80 = D 로 딱 맞는다. 여기서 `⌈80/40⌉` 은 정확히 2 여야 한다 — "floor + 1" 로 ceil 을 흉내 내면 3 이 되어 R = 90 > 80 으로 거짓 실패한다 (§7).

### Part A — U ≤ 1 인데 RM 이 놓치는 세트, 그리고 시뮬레이션

```text
== A5 U <= 1 but RM misses  (n=2, U~0.9714, LL bound~0.8284)
   LL FAIL | hyperbolic FAIL | U<=1 yes
   t1            2       5       5     0     0    1 |       2     1 ok
   t2            4       7       7     0     0    0 |       8     2 MISS (stopped at R > D)
-- A5 critical instant, RM (t2 first job misses D=7 at t7, finishes t8)
 tick  : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4
 t1    : R R . . . R R . . . R R . . .
 t2    : r r R R R r r R R R r r R R R
```

```svg
<svg viewBox="0 0 740 206" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">A5 (2,5)(4,7) RM — sim_fp 의 실제 타임라인 (위 출력)</text>
  <g fill="currentColor" font-size="12">
    <text x="94" y="44">0</text><text x="134" y="44">1</text><text x="174" y="44">2</text><text x="214" y="44">3</text><text x="254" y="44">4</text>
    <text x="294" y="44">5</text><text x="334" y="44">6</text><text x="374" y="44">7</text><text x="414" y="44">8</text><text x="454" y="44">9</text>
    <text x="490" y="44">10</text><text x="530" y="44">11</text><text x="570" y="44">12</text><text x="610" y="44">13</text><text x="650" y="44">14</text>
  </g>
  <text x="20" y="72" fill="currentColor">t1 (2,5)</text>
  <rect x="80" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/><rect x="120" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="280" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/><rect x="320" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="480" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/><rect x="520" y="54" width="40" height="26" class="f-acc" stroke="currentColor"/>
  <text x="20" y="112" fill="currentColor">t2 (4,7)</text>
  <rect x="80" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/><rect x="120" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="160" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/><rect x="200" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/><rect x="240" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="280" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/><rect x="320" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="360" y="94" width="40" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="400" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/><rect x="440" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/><rect x="520" y="94" width="40" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="560" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/><rect x="600" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/><rect x="640" y="94" width="40" height="26" class="f-ok" stroke="currentColor"/>
  <line x1="360" y1="88" x2="360" y2="130" stroke="currentColor" stroke-width="3"/>
  <text x="300" y="148" fill="currentColor" font-size="12">D = 7: 첫 작업은 아직 1 tick 남음 → t8 완료 (R = 8)</text>
  <text x="20" y="176" fill="currentColor" font-size="12">강조 칸 = t1 실행, 초록 칸 = t2 실행, 흐린 칸 = 출시됐지만 대기(r), 경고 칸 = 데드라인 넘긴 실행.</text>
  <text x="20" y="196" fill="currentColor" font-size="12">t2 의 둘째 작업(t7 출시)은 t8–t9, t12–t13 실행 → t14 완료 (응답 7 = D, 통과).</text>
</svg>
```

U = 0.4 + 0.571 = 0.971 이라 "CPU 는 남는다". 하지만 t2 의 첫 7 tick 창 안에 t1 의 작업이 **두 번** (t0, t5) 들어와 4 tick 을 먹고, t2 에게는 3 tick 만 남는다. RM 은 고정 우선순위라 데드라인이 급한 t2 를 올려 줄 수 없다 — EDF 는 이 세트를 통과시킨다 (RTOS-M12).

**시뮬레이션 교차 확인** (TODO 5). `sim_agrees()` 는 하이퍼피리어드(lcm, 2,000,000 tick 넘으면 거부) 동안 돌려서:

- RTA 통과 태스크: `first_resp == max_resp == R`, 미스 0. A3 에서 100,000 tick 동안 최악 응답 300 · 700 · 3500 · 58000 = RTA 와 같다.
- RTA 실패 태스크: 첫 작업이 D 안에 못 끝난다 (A5: first_resp = 8 > 7).

"같다" 가 성립하는 이유: D ≤ T 이고 독립 태스크면 동시 출시가 최악이고(임계 순간), 하이퍼피리어드 안의 다른 작업은 그보다 나쁠 수 없다. 이 검사가 의미 있으려면 시뮬레이터는 RTA 코드를 **하나도 공유하지 않아야** 한다 — `sim_fp` 는 공식 없이 tick 마다 "가장 높은 미완료 태스크 1 tick" 만 한다.

### Part B — 경계에 딱 붙은 값과 double 의 함정

정수 판정 테스트 (B1, B2):

| 세트 | U | LL | 쌍곡선 | 메모 |
|---|---|---|---|---|
| (1,1) | 1 | PASS | PASS | n=1 경계 = 1 정확히 |
| (4142,10000)×2 | 0.8284 | PASS | PASS | 경계 0.828427… 바로 아래 |
| (4143,10000)×2 | 0.8286 | FAIL | FAIL | 바로 위. U_i 가 같으면 두 경계가 같은 조건 |
| (1,6)(5,7) | 0.881 | FAIL | **PASS** | `(7/6)(12/7) = 2` 정확히, RTA R = 6 ≤ 7 |

같은 판정을 double 로 하면 (`demo_float_trap.c`, 실제 출력):

<!-- code: demo_float_trap.c -->

```text
1) U as double = 1.0000000000000002 -> (u <= 1.0) is FALSE | integer: sum C = 1000 vs T = 1000 -> U <= 1
2) hyperbolic product as double = 2.0000000000000004 -> (<= 2.0) is FALSE | integer: 84 <= 84 -> PASS
3) ceil(2.1 / 0.3) = ceil(7.0000000000000009) = 8 | integer µs: ceil(2100 / 300) = 7
   double iter 1: w = 1.8999999999999999
   double iter 2: w = 2.1000000000000001
   double iter 3: w = 2.2000000000000002
   double RTA: R = 2.2000000000000002 > D = 2.1 -> MISS
   integer RTA (µs): R = 2100 -> ok (R == D)
```

```bash
cc -std=c11 -Wall -Wextra -O2 demo_float_trap.c -o demo_float_trap && ./demo_float_trap
```

1. C = 200, 400, 300, 100 / T = 1000 은 정확히 U = 1 이고 RTA 도 통과(연습 3)하는 조화 세트인데, 이 순서로 double 을 더하면 `1.0000000000000002` 라 "U ≤ 1 위반 = 불가능" 이 된다. (같은 값을 다른 순서로 더하면 결과가 달라질 수 있다 — 부동소수 덧셈은 결합법칙이 성립하지 않는다.)
2. 쌍곡선 곱이 정확히 2 인 세트를 거부.
3. 시간을 ms 소수로 쓰면 `2.1/0.3` 이 7 이 아니라 7.0000000000000009 → ceil 8. 그 결과 R = D 로 딱 맞는 세트를 **미스** 로 판정한다. 정수 µs 로는 `ceil(2100/300) = 7`, R = 2100.

결정이 경계에서 갈리는 도구는 "보수적이니 괜찮다" 로 넘길 수 없다 — 1, 2, 3 모두 실제로 안전한 설계를 거부한다. 그래서 이 도구는 시간을 정수(µs 또는 tick)로만 받고, 판정은 정수 비교로만 한다.

**발산하는 세트** (B3). h1 (5,10), h2 (5,10) 이 CPU 를 100 % 쓰면 low 는 영원히 못 돈다. RTA 는 `w = 11, 21, 31, …, 101` 로 커지다가 D = 100 을 넘는 순간 멈춘다: `low stops after 10 iterations at R=101`. 이때의 R 은 **진짜 응답 시간이 아니다** — "D 를 넘었다" 는 증거일 뿐이다 (출력에 `MISS (stopped at R > D)` 로 표시).

**D < T 와 DM** (B4). 데드라인이 주기보다 짧으면 RM 은 더 이상 최적이 아니고, **DM (Deadline Monotonic)** 이 고정 우선순위 중 최적이다 (Leung & Whitehead 1982). 이용률 경계는 D = T 를 가정하므로 적용 불가(`N/A`) — U = 0.583 이라 LL 을 "통과" 한 것처럼 보여도 RM 은 실제로 놓친다:

```text
== B4 D < T under RM  (n=2, U~0.5833, LL bound~0.8284)
   LL N/A | hyperbolic N/A | U<=1 yes
   A             1       4       4     0     0    1 |       1     1 ok
   B             2       6       2     0     0    0 |       3     1 MISS (stopped at R > D)
== B4 D < T under DM  (n=2, U~0.5833, LL bound~0.8284)
   LL N/A | hyperbolic N/A | U<=1 yes
   A             1       4       4     0     0    0 |       3     2 ok
   B             2       6       2     0     0    1 |       2     1 ok
```

RTA 자체는 우선순위가 어떻게 정해졌든(RM, DM, 손으로) 그대로 쓸 수 있다 — hp(i) 만 바뀐다. D > T (데드라인이 다음 출시 뒤) 는 같은 태스크의 이전 작업이 밀려 있을 수 있어 첫 작업만 보면 안 되고, busy period 안의 여러 작업을 봐야 한다 (Lehoczky 1990). 이 도구는 파서에서 `D > T` 를 거부한다.

**그 밖의 B 테스트**: 같은 주기 세 개(B5)는 입력 순서로 우선순위가 정해지고 R = 1, 3, 6. 파서는 C > T, D > T, C = 0, J ≥ T, 토큰 오류, 17번째 태스크를 거부(B6). 서로소 큰 주기 999983 · 999979 · 999961 의 lcm 은 약 10^18 이라 `hyperperiod()` 가 limit 에서 0 을 돌려준다 — 시뮬레이션은 못 하지만 LL/RTA 는 문제없다(B7). 16 태스크, 주기 약 10^7 에서도 bignum 이 넘치지 않는다(B8).

**무작위 400 세트** (B9, 주기는 {4,…,60} 메뉴 → 하이퍼피리어드 ≤ 120):

```text
random 400 sets: LL pass 58, hyperbolic pass 66, RTA pass 122, U<=1 146 | implication violations 0, sim mismatches 0
```

| 판정 | 통과 | 성격 |
|---|---|---|
| Liu–Layland | 58 | 충분조건, O(n), 가장 비관적 |
| 쌍곡선 | 66 | 충분조건, O(n), LL 을 포함 |
| RTA | 122 | **필요충분** (D ≤ T, 독립, 동시 출시 가능할 때), 의사 다항 시간 |
| U ≤ 1 | 146 | 필요조건 (실패면 무조건 불가) |

58 → 122: 이 표본에서는 이용률 경계만 믿었다면 **스케줄 가능한 세트의 절반 이상** 을 버렸을 것이다. 반대로 U ≤ 1 은 24 세트를 잘못 "가능" 으로 본다.

### Part C — 블로킹과 지터를 넣은 펌웨어 분석

**B: 한 번은 막힐 수 있다.** IPCP 에서 높은 태스크는 "자기보다 낮은 태스크가 쥔, 천장이 자기 이상인 임계 구역" 중 **최대 하나** 만큼 막힌다 (RTOS-M05, Sha · Rajkumar · Lehoczky 1990). 천장: SPI = max(imu 2, log 0) = 2, setpoint = max(motor 3, comms 1) = 3.

| 태스크 (prio) | 막을 수 있는 임계 구역 | B |
|---|---|---|
| motor (3) | comms 의 setpoint 50 (천장 3 ≥ 3) | 50 |
| imu (2) | comms setpoint 50, log SPI 1200 | **1200** |
| comms (1) | log SPI 1200 (천장 2 ≥ 1) | **1200** |
| log (0) | 없음 | 0 |

comms 는 SPI 를 쓰지도 않는데 log 의 SPI 구간에 막힌다 — 천장 2 의 log 가 comms(1) 보다 높게 돌기 때문. 이것이 IPCP 의 대가다.

**J: 출시가 늦어질 수 있다.** imu 는 DRDY 인터럽트 → ISR 이 세마포어를 주고 → 태스크가 READY. 이벤트(주기의 시작)와 READY 사이 지연이 J 다. 그 상위 태스크의 작업은 "늦게 출시된 첫 작업 + 제 시간에 출시된 다음 작업" 이 붙어서 한 창에 더 많이 들어올 수 있다 → `⌈(w + J_j)/T_j⌉`. 자기 지터는 끝에 더한다: `R_i = w_i + J_i` (Audsley · Burns · Richardson · Tindell · Wellings 1993 형태). FreeRTOS 에서 J 의 출처: ISR 진입 지연, 다른 ISR, 그리고 ISR 이 `portYIELD_FROM_ISR` 를 빼먹으면 **다음 tick 까지** (최대 1 tick) — RTOS-M02.

`./solution -v` 의 Part C 부분 (실제 출력):

```text
== C1 firmware + IPCP blocking + jitter (flash page write holds SPI 1200 us)  (n=4, U~0.8500, LL bound~0.7568)
   LL N/A | hyperbolic N/A | U<=1 yes
   motor       300    1000    1000     0    50    3 |     350     1 ok
   imu         400    2000    2000   100  1200    2 |    2300     1 MISS (stopped at R > D)
   comms      1500   10000    4000   300  1200    1 |    4700     1 MISS (stopped at R > D)
   log       20000  100000  100000     0     0    0 |   58700    11 ok
   sim (blocker 1200, jitter pattern): imu first response 2500 (+J=100 → 2600 > D=2000)
== C2 fix: flash write split into 200 us chunks  (n=4, U~0.8500, LL bound~0.7568)
   LL N/A | hyperbolic N/A | U<=1 yes
   motor       300    1000    1000     0    50    3 |     350     1 ok
   imu         400    2000    2000   100   200    2 |    1000     2 ok
   comms      1500   10000    4000   300   200    1 |    4000     4 ok
   log       20000  100000  100000     0     0    0 |   58700    11 ok
   sim check motor  blocker=  50: first response    350, RTA w=   350
   sim check imu    blocker= 200: first response    900, RTA w=   900
   sim check comms  blocker= 200: first response   3700, RTA w=  3700
   sim check log    blocker=   0: first response  58700, RTA w= 58700
```

읽는 법:

1. **C1**: 같은 CPU 부하(U = 0.85)인데 플래시 페이지 쓰기 1200 µs 동안 SPI 를 쥐는 설계 하나 때문에 imu 와 comms 가 미스. imu: `w0 = 400 + 1200 = 1600 → 1600 + ⌈1600/1000⌉·300 = 2200`, `2200 + 100 > 2000` 에서 멈춤. 시뮬레이션(t=0 에 1200 tick 블로커, 지터 최악 패턴)으로 끝까지 돌리면 실제 첫 응답은 2500 + J = 2600 — RTA 가 멈춘 2300 보다 크다 (멈춘 값은 하한일 뿐).
2. **C2**: 플래시 쓰기를 256 B 조각(200 µs)으로 나누고 조각 사이에 SPI 를 놓는다. B 가 200 이 되자 imu R = 1000, comms R = **4000 = D** (여유 0 µs). 연습 1 에서 보듯 이 상태에서는 imu 의 C 를 1 µs 도 늘릴 수 없다.
3. **교차 확인**: 태스크마다 블로커 = B_i, 상위 태스크는 지터 최악 패턴(0, T−J, 2T−J, …)으로 출시해 시뮬레이션 → 첫 응답이 `R_i − J_i` 와 정확히 같다.
4. B, J 를 빼고 분석하면 imu 700, comms 3500 — "여유 많다" 는 **착시**. 실제 펌웨어의 미스는 대부분 이 두 항에서 온다.

**실무 체크리스트** (분석이 현실과 맞으려면):

| 항목 | 이 도구에서 | 실제 MCU 에서 |
|---|---|---|
| C | 주어진 값 | 측정(DWT CYCCNT) 최댓값 + 여유, 캐시·플래시 대기 상태 포함 (RTOS-A03) |
| ISR | 없음 | 최상위 우선순위 "태스크" 로 넣는다 — SysTick, DRDY, CAN RX 모두 (RTOS-A04) |
| 문맥 전환 | 없음 | 작업당 2회 → `C + 2·cs` (연습 2: A3 세트는 cs ≤ 46 µs) |
| tick 양자화 | µs 연속 | `xTaskDelayUntil` 출시는 tick 경계 — T 를 tick 배수로, J 에 tick ISR 처리 시간 |
| 같은 우선순위 | hp 로 간주 | FreeRTOS 기본 time slicing (FreeRTOS.h:2771) — 가능하면 우선순위를 모두 다르게 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 51/51 passed (5회 반복 동일)
./solution -v                                                                 # B3 반복 수, C 단계 시뮬레이션 비교까지
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 51/51, 보고 없음
```

테스트가 진짜 버그를 잡는지 돌연변이로 확인했다 (`/tmp` 의 사본, 레슨 파일 아님): ceil 을 `w/T + 1` 로 바꾸면 6개 실패, 상위 태스크 지터 `+ J_j` 를 빼면 2개 실패, 천장 비교 `>=` 를 `>` 로 바꾸면 6개 실패.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ceil 을 `w / T + 1` 로 | w 가 T 의 배수일 때 1 많이 센다 (A4: ⌈80/40⌉ = 2 인데 3 → R = 90 거짓 실패) | `(w + T − 1) / T` |
| `ceil((double)w / T)` 또는 ms 소수 | `2.1/0.3 = 7.0000000000000009` → 8 (demo 3) | 정수 µs / tick 으로만 |
| LL 실패 = 스케줄 불가 | 충분조건일 뿐. A3 U = 0.85 는 통과 | 경계 실패면 RTA 로 |
| D < T 에 LL 적용 | 경계는 D = T 가정. B4 는 U = 0.58 인데 RM 미스 | `UT_NA` 반환, DM + RTA |
| hp(i) 에 i 자신을 넣음 | `⌈w/T_i⌉·C_i` 가 더해져 자기 일을 두 번 센다 → 거짓 실패 | `if (j == i) continue;` |
| 수렴 검사만 하고 `R > D` 검사 없음 | 상위 이용률 ≥ 1 이면 무한 루프 (B3) | 매 반복 `nw + J > D` 면 멈춤 |
| hp(i) 에서 같은 우선순위 제외 | time slicing 이면 같은 우선순위도 간섭 | `prio_j >= prio_i` (j ≠ i) |
| 지터를 자기 응답에 안 더함 | `w` 는 출시부터, 데드라인은 도착부터 | `R = w + J_i` |
| B 를 임계 구역 합으로 (IPCP 에서) | IPCP 는 최대 하나 (M05). 합은 PI 에서의 보수적 상한 | `max`, 프로토콜에 맞게 |
| MISS 일 때 출력된 R 을 "응답 시간" 으로 보고 | 멈춘 지점일 뿐. C1 imu: 2300 출력, 실제 2600 | "R > D" 라고만 보고, 실제 값은 시뮬레이션/추가 반복 |
| 시뮬레이션 몇 주기만 돌려 보고 OK | 임계 순간을 안 만들면 최악을 못 본다. 위상이 엇갈린 출시는 낙관적 | t = 0 동시 출시 + 하이퍼피리어드, 또는 RTA |

## 8. 면접 Q&A

<details>
<summary>Q1. "RM 이 최적이다" 는 정확히 무슨 뜻이고, 어떤 가정이 필요한가요?</summary>

**고정 우선순위 할당 방식 중에서** 최적이라는 뜻이다: 어떤 고정 우선순위로 스케줄 가능한 세트는 RM 으로도 가능하다 (Liu & Layland 1973). 가정: 단일 코어, 선점, 독립 태스크(공유 자원·자발적 대기 없음), 주기적(또는 최소 간격 T 의 산발적), D = T, 문맥 전환 비용 0. D < T 면 DM 이 최적(Leung & Whitehead 1982), 지터·블로킹이 있으면 DM 도 최적이 아닐 수 있고 Audsley 의 OPA 가 필요하다 (연습 4). 동적 우선순위까지 넓히면 EDF 가 단일 코어에서 최적이고 U ≤ 1 이면 된다 (RTOS-M12). 꼬리: "그럼 왜 EDF 를 안 쓰나?" → 과부하 시 연쇄 미스, 구현 비용, 상용 RTOS(FreeRTOS)가 고정 우선순위만 지원.

</details>

<details>
<summary>Q2. LL 경계 69 % 는 어디서 오고, 실제 시스템은 왜 85~90 % 로도 돌아가나요?</summary>

`n(2^{1/n} − 1)` 은 n 태스크 세트 중 "가장 운 나쁜" 주기 비율(인접 주기 비가 2^{1/n})에서 CPU 를 꽉 채우는 이용률이다. n → ∞ 극한이 ln 2 ≈ 0.693. 그 최악 비율이 아니면 훨씬 높게 가능하다: 조화 주기면 U = 1 까지 (A4, 연습 3). Lehoczky · Sha · Ding (1989) 은 무작위 세트의 평균 한계 이용률이 약 88 % 라고 보고했다 (문서 기준). 그래서 실무 순서: ① U ≤ 1 확인(필요조건), ② 빠른 충분조건(LL/쌍곡선) 통과면 끝, ③ 아니면 RTA. 꼬리: "주기를 조화로 맞추는 게 왜 좋나?" → 1 kHz / 500 Hz / 100 Hz 처럼 맞추면 경계가 1 이 되고 하이퍼피리어드도 짧아 테스트·트레이스가 쉬워진다.

</details>

<details>
<summary>Q3. RTA 반복이 끝난다는 보장은? 결과는 정확한가요?</summary>

`W(w) = C + B + Σ⌈(w + J_j)/T_j⌉C_j` 는 w 에 대해 단조 증가 계단 함수. `w0 = C + B ≤ W(w0)` 에서 시작하면 수열이 줄지 않고, 바뀔 때마다 적어도 min C_j 만큼 커지므로 "수렴(가장 작은 고정점)" 또는 "D 초과" 가 유한 번에 온다 (B3: 10번). 정확성: 독립 태스크, D ≤ T, 모든 태스크가 동시에 출시될 수 있으면(임계 순간이 실제로 생기면) **필요충분** 이다 — 시뮬레이션의 최악 응답과 같다 (A3, B9). 오프셋이 고정돼 동시 출시가 절대 안 일어나는 시스템에서는 충분조건(비관적)이 되고, B·J 를 넣으면 그 최악이 실제로 겹치지 않을 수 있어 역시 충분조건이다. D > T 면 첫 작업만 보면 안 된다 (Lehoczky 1990, busy period).

</details>

<details>
<summary>Q4. 블로킹 항 B 는 어떻게 구하나요? 우선순위 상속(PI) 이면 달라지나요?</summary>

IPCP/PCP: 태스크 i 는 "자기보다 낮은 태스크의, 천장이 prio(i) 이상인 임계 구역" 중 **최대 1개** 만큼 막힌다 → `B_i = max cs` (Part C, M05). PI: 더 낮은 태스크마다 또는 i 를 막을 수 있는 세마포어마다 최대 한 번씩 막힐 수 있어 min(n, m) 개 구역의 합까지 (Sha · Rajkumar · Lehoczky 1990, RTOS-M04 연습 1). FreeRTOS 뮤텍스는 PI 만 있으므로 FreeRTOS 위에서 Part C 를 분석하면 B 를 PI 규칙으로(합으로) 잡아야 안전하다. 실무 답: 임계 구역 길이를 측정해 표로 관리, 긴 구역(플래시 쓰기, printf)은 조각내거나 전담 태스크 + 큐로 빼서 뮤텍스를 없앤다 (C2).

</details>

<details>
<summary>Q5. 릴리즈 지터 J 는 어디서 오고, 왜 식에 ⌈(w + J_j)/T_j⌉ 로 들어가나요?</summary>

J = "도착(이벤트)부터 READY 까지" 의 변동. 출처: 인터럽트 진입 지연, 더 높은 ISR, ISR → 태스크 지연 처리 경로, tick 기반 출시(최대 1 tick), 폴링. 상위 태스크 j 의 첫 작업이 J_j 늦게 출시되고 다음 작업이 제때 출시되면, 두 작업 사이 간격이 `T_j − J_j` 로 줄어 길이 w 창에 `⌈(w + J_j)/T_j⌉` 개까지 들어온다. 자기 지터는 데드라인이 도착 기준이므로 `R_i = w_i + J_i`. Part C 시뮬레이션의 출시 패턴 `0, T−J, 2T−J, …` 이 바로 그 최악이고, 첫 응답이 w_i 와 같았다. FreeRTOS 에서 ISR 이 `portYIELD_FROM_ISR` 를 빠뜨리면 J 가 최대 1 tick 늘어난다 (RTOS-M02).

</details>

<details>
<summary>Q6. ISR 과 문맥 전환 비용은 분석에 어떻게 넣나요?</summary>

ISR 은 어떤 태스크보다 높으니 **최상위 우선순위 태스크** 로 모델링한다: C = ISR 최악 실행 시간, T = 최소 인터럽트 간격 (SysTick 이면 tick 주기, UART RX 면 바이트 간격). FreeRTOS tick ISR(xTaskIncrementTick, tasks.c:4831) 의 비용도 여기 들어간다 — 상세는 RTOS-A04. 문맥 전환: 선점은 더 높은 작업이 시작할 때 한 번, 끝나고 돌아올 때 한 번 생기므로, 전환 2회를 **그 높은 작업의** C 에 붙이면 모든 전환이 빠짐없이 세어진다 → 모든 태스크에 `C + 2·cs` (연습 2). 꼬리: "PendSV 는?" → Cortex-M 에서 전환은 최하위 우선순위 예외 PendSV 에서 하고 그 시간이 cs 다 (RTOS-A01).

</details>

<details>
<summary>Q7. 같은 우선순위에 태스크 둘을 두면 RTA 는 어떻게 하나요?</summary>

FreeRTOS 는 `configUSE_TIME_SLICING` 기본 1 (FreeRTOS.h:2771–2772) 이라 tick 마다 같은 우선순위 READY 태스크끼리 번갈아 돈다 (tasks.c:4967–4998). 서로가 서로를 간섭하므로 분석에서는 같은 우선순위 태스크를 **hp 에 포함** 해야 안전하다 (이 도구의 `prio_j >= prio_i`). 그 결과 둘 다 "상대 전체를 기다린다" 로 보수적이 된다. 그래서 분석 가능한 설계는 보통 우선순위를 모두 다르게 둔다. time slicing 이 꺼져 있으면 같은 우선순위는 FIFO 지만, 순서가 출시 타이밍에 따라 달라지므로 여전히 hp 로 보는 것이 안전하다.

</details>

<details>
<summary>Q8. U ≤ 1 인데 놓치는 예를 들어 보세요. 시스템이 이미 돌고 있는데 태스크를 하나 더 넣고 싶다면?</summary>

(2,5)(4,7): U = 0.971, t2 는 첫 7 tick 창에 t1 작업 두 개(4 tick)를 견뎌야 해서 R = 8 > 7 (A5). 추가 질문에는 **민감도 분석** 으로 답한다: 새 태스크를 넣고 RTA 를 다시 돌리거나, 기존 태스크의 C 여유를 이분 탐색으로 구한다 (연습 1: Part C 의 log 는 20 ms → 34.7 ms 까지 가능하지만 imu 와 comms 는 0 µs 여유). 다른 축: 주기를 조화로 맞추기, 긴 임계 구역 쪼개기, 로깅처럼 덜 급한 일을 최하위로 내리고 D 를 늘리기.

</details>

## 9. 추가 연습

### 연습 1 — 민감도 분석: C 를 얼마나 늘릴 수 있나

Part C 를 고친 세트(C2)에서 log, comms, imu 의 C 를 각각 최대 얼마까지 늘려도 **모든** 태스크가 통과하나? `max_c(ts, i)` 를 작성하라.

<details>
<summary>답 보기</summary>

C_i 가 커지면 i 와 그 아래 모든 태스크의 R 이 단조 증가하므로 `[C_i, D_i]` 에서 이분 탐색 (`all_ok` 가 판정 함수).

```text
ex1 max C: log 34700 (now 20000), comms 1500 (now 1500), imu 400 (now 400)
ex1 with log C=34700: U~0.9970 (LL bound for n=4 ~0.7568, not applicable: D < T)
```

log 는 34,700 µs 까지 (U = 0.997!) — 이때 R = 99,700, 1 µs 늘리면 계단이 하나 넘어가 R = 101,201 로 뛴다 (응답 시간은 C 에 대해 불연속). comms 와 imu 는 **0 µs 여유**: comms 의 R = 4000 = D 이므로, comms 자신이든 그 위의 imu 든 1 µs 만 늘어도 comms 가 미스한다. 리뷰에서 "imu 필터 계산 조금 늘려도 되죠?" 의 답이 여기 있다.

</details>

### 연습 2 — 문맥 전환 비용

A3 세트(B, J 없음)에서 전환 1회 비용 cs 를 C 에 `2·cs` 로 더할 때 견딜 수 있는 최대 cs 는? 원래 실패하는 세트면 −1.

<details>
<summary>답 보기</summary>

```text
ex2 max context-switch cost: 46 us per switch (2 per job)
```

cs = 47 이면 log 의 R = 100,134 > 100,000 (motor 394, imu 888, comms 5440 은 여유). Cortex-M4 급 MCU 에서 FreeRTOS 전환은 보통 수 µs 이하로 알려져 있으니(이 레슨에서 측정하지 않음) 여유는 크지만, cs 는 C 가 작은 태스크(1 kHz 루프)일수록 비율이 커진다. 측정: 전환 전후 GPIO 토글 + 로직 애널라이저, 또는 DWT CYCCNT 를 trace 훅에서.

</details>

### 연습 3 — 조화 주기면 U ≤ 1 이 정확한 판정

`is_harmonic(ts)` 를 작성하고, 주기를 {1, 2, 4, 8, 16} ms 에서 뽑은 무작위 300 세트에서 "RTA 통과 ⇔ U ≤ 1" 을 확인하라. LL 은 그중 몇 개를 통과시키나?

<details>
<summary>답 보기</summary>

모든 쌍 (a ≤ b) 에 대해 `b % a == 0`.

```text
ex3 harmonic 300 sets: U<=1 146, RTA pass == U<=1 for all: yes, LL pass only 69
```

조화 주기에서는 RM 의 이용률 한계가 1 이라는 잘 알려진 결과 (Kuo & Mok 1991 등, 문서 기준) 를 300 세트로 확인했다. LL 은 146 개 중 69 개만 통과 — 조화 세트에서 LL 은 특히 비관적이다. 본문 demo 의 (200,400,300,100)/1000 세트도 조화이고 U = 1 정확히, R = 1000 = D 로 통과한다 (테스트).

</details>

### 연습 4 — 지터가 있으면 DM 도 최적이 아니다: Audsley OPA

A (C=1, T=17, D=11, J=10), B (C=5, T=6, D=6). DM 은 통과하나? 가장 낮은 우선순위부터 "남은 것 전부가 나보다 높다" 고 두고 통과하는 태스크를 고정해 가는 `opa(ts)` 를 작성하라.

<details>
<summary>답 보기</summary>

```text
ex4 DM: A R=16 (D=11) MISS | OPA: found, prio A=1 B=0, R_A=11 R_B=6
ex4 no jitter, 500 random D<=T sets: DM ok 134, OPA agrees with DM: yes
```

DM 은 D 가 짧은 B 를 위에 둔다 → A 는 `1 + 5 = 6`, 지터 10 을 더해 16 > 11. 거꾸로 A 를 위에 두면 A = 1 + 10 = 11 ≤ 11, B = `5 + ⌈(5+10)/17⌉·1 = 6` ≤ 6 — 둘 다 통과. OPA 는 n(n+1)/2 번의 RTA 로 이런 할당을 찾고, 찾지 못하면 어떤 고정 우선순위로도 불가능하다 (Audsley 1991/2001, "태스크의 판정이 상위 집합에만 의존하고 그 순서엔 무관" 한 테스트일 때 — RTA 가 그렇다). 지터가 없고 D ≤ T 면 DM 이 최적이라 무작위 500 세트에서 OPA 성공 ⇔ DM 성공이 일치했다. 지터 최악 패턴 시뮬레이션도 A 첫 응답 1 (+J 10 = 11), B 6 으로 같다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 14/14 passed
./practice -v                                                                # 연습 4 시뮬레이션 줄까지
```

## 10. 핵심 정리

- RM = 주기 짧을수록 높게 (D = T 에서 고정 우선순위 최적), DM = 데드라인 짧을수록 (D ≤ T 에서 최적). 우선순위 숫자를 고르는 **설계 규칙** 이고, FreeRTOS 는 그냥 고정 우선순위 선점 스케줄러다.
- 판정 사다리: LL `n(2^{1/n}−1)` (→ ln 2) ⊂ 쌍곡선 `Π(U_i+1) ≤ 2` ⊂ **RTA (정확)** ⊂ U ≤ 1 (필요). 경계 실패 ≠ 불가능 (U 0.85 펌웨어 세트 통과), U ≤ 1 ≠ 가능 ((2,5)(4,7) 미스).
- RTA: `w = C + B + Σ_{hp} ⌈(w + J_j)/T_j⌉ C_j`, `w0 = C + B`, 수렴하거나 `w + J_i > D` 면 멈춤, `R = w + J_i`. 계단 함수와 y = t 의 첫 교점.
- 판정은 정수로만: 무리수 경계는 n 제곱해 `(P + nQ)^n ≤ 2(nQ)^n`, ceil 은 `(a+b−1)/b`. double 은 U = 1, Π = 2, ceil(2.1/0.3) 에서 실제로 틀린다.
- 검증: 동시 출시(임계 순간)부터 하이퍼피리어드 시뮬레이션의 최악 응답 == R.
- 펌웨어에서 미스는 대개 U 가 아니라 **B (긴 임계 구역)** 와 **J (ISR → 태스크 지연)** 에서 온다: 1200 µs SPI 플래시 쓰기 하나가 U 0.85 세트를 깨고, 200 µs 로 쪼개면 통과.
- 남은 여유는 민감도 분석으로: R = D 인 태스크가 있으면 그 위 모든 태스크의 C 여유는 0.

## 참고자료

- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", Journal of the ACM 20(1), 1973 — RM 최적성, 임계 순간 정리, 이용률 경계
- E. Bini, G. C. Buttazzo, G. M. Buttazzo, "Rate Monotonic Analysis: The Hyperbolic Bound", IEEE Transactions on Computers 52(7), 2003
- M. Joseph, P. Pandya, "Finding Response Times in a Real-Time System", The Computer Journal 29(5), 1986 — 응답 시간 반복식
- N. Audsley, A. Burns, M. Richardson, K. Tindell, A. Wellings, "Applying New Scheduling Theory to Static Priority Pre-emptive Scheduling", Software Engineering Journal 8(5), 1993 — 블로킹·지터를 넣은 RTA
- J. Y.-T. Leung, J. Whitehead, "On the Complexity of Fixed-Priority Scheduling of Periodic, Real-Time Tasks", Performance Evaluation 2(4), 1982 — DM 최적성
- J. Lehoczky, L. Sha, Y. Ding, "The Rate Monotonic Scheduling Algorithm: Exact Characterization and Average Case Behavior", RTSS 1989 / J. Lehoczky, "Fixed Priority Scheduling of Periodic Task Sets with Arbitrary Deadlines", RTSS 1990
- N. Audsley, "On Priority Assignment in Fixed Priority Scheduling", Information Processing Letters 79(1), 2001 (원래 1991 York 기술 보고서) — OPA
- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", IEEE Transactions on Computers 39(9), 1990 — 블로킹 상한
- G. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 4장 주기 태스크 스케줄링, 7장 자원 접근 프로토콜 / A. Burns, A. Wellings, *Real-Time Systems and Programming Languages* — RTA 장
- FreeRTOS-Kernel V11.3.2 소스 — tasks.c (`taskSELECT_HIGHEST_PRIORITY_TASK` 197–212, `xTaskDelayUntil` 2414, `xTaskIncrementTick` 4831, 선점 비교 4944, time slicing 4967–4998), include/FreeRTOS.h (`configUSE_TIME_SLICING` 기본 1, 2771–2772), include/projdefs.h:42 (`pdMS_TO_TICKS`), include/task.h:210 (`tskIDLE_PRIORITY`) — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- 관련 레슨: RTOS-M04 (상속, 연쇄 블로킹), RTOS-M05 (IPCP, B_i), RTOS-M02 (ISR 지연 처리 → 지터), RTOS-M12 (EDF 시뮬레이터, RM 비교), RTOS-A03 (WCET 측정), RTOS-A04 (확장 RTA: ISR 부하), RTOS-A10 (모터 제어 시스템 설계), RTOS-B06 (delay_until), RTOS-B04 (우선순위 비트맵)
