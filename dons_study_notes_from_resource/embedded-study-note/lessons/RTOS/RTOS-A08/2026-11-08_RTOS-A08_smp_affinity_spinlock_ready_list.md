---
id: RTOS-A08
time: 110분
updated: 2026-11-08
---
# RTOS-A08 SMP RTOS: 코어 친화성, 스핀락, 공유 ready list

## 학습 목표

- 단일 코어 RTOS 의 상호 배제 도구인 "인터럽트 끄기"(`__disable_irq()`, `taskENTER_CRITICAL()`)가 듀얼 코어에서 **왜 깨지는지** 결정적인 인터리빙으로 재현하고, "자기 코어 인터럽트 끄기 → 스핀락" 순서의 SMP 임계 구역으로 고칠 수 있다.
- 코어 단위 재귀를 허용하는 커널 스핀락을 C11 atomics 로 구현하고 (FreeRTOS RP2040 포트의 `vPortRecursiveLock` 구조), acquire/release 가 어디에 필요한지 말할 수 있다.
- 전역 ready list 에서 **코어 친화성(affinity) 마스크** 를 지키며 다음 태스크를 고르고, 새로 READY 된 태스크를 위해 **어느 코어를 선점할지** FreeRTOS `prvYieldForTask` 규칙대로 결정할 수 있다.
- IPI(Inter-Processor Interrupt) 대용 doorbell 로 **크로스 코어 yield** 를 구현하고, 진짜 스레드 2개에서 "IPI 유실 0, 이중 실행 0, 친화성 위반 0" 을 매 실행 성립하는 불변식으로 검사한다.
- RP2040 이 하드웨어 스핀락을 가진 이유(ARMv6-M 에는 LDREX/STREX 가 없다)를 실제 Cortex-M0+ / M4 어셈블리로 설명할 수 있다.

## 1. 문제

RP2040 (Cortex-M0+ ×2), ESP32 (Xtensa ×2), STM32H7 듀얼 코어 같은 칩이 흔해지면서 펌웨어 면접에 **"이 단일 코어 RTOS 코드를 두 번째 코어에서도 돌리면 뭐가 깨지나요?"** 가 나온다. 정답의 핵심은 한 문장이다 — **PRIMASK 는 자기 코어의 인터럽트만 막는다.** 다른 코어는 그 순간에도 같은 메모리를 읽고 쓰고, 다른 코어로 라우팅된 ISR 도 그대로 돈다 (C-M14 Q8 의 꼬리질문을 이번에는 직접 만든다).

SMP(Symmetric Multiprocessing) RTOS 는 그래서 세 가지가 달라진다. ① 임계 구역 = 인터럽트 끄기 **+ 스핀락**, ② ready list 는 코어들이 공유하고 태스크마다 **친화성 마스크** 가 있다, ③ 한 코어가 깨운 태스크를 다른 코어에서 돌려야 하면 그 코어에 **IPI** 를 보내 선점시킨다. FreeRTOS V11 의 SMP 커널(`configNUMBER_OF_CORES > 1`)이 정확히 이 구조다 — 이 레슨의 모델은 그 소스(`tasks.c`, RP2040 포트)를 읽고 축소한 것이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 SMP 모델 (RP2040 식 듀얼 코어)</text>
  <rect x="20" y="40" width="200" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">core0 (pthread)</text>
  <text x="32" y="86" fill="currentColor">PRIMASK: 자기 코어만</text>
  <text x="32" y="108" fill="currentColor">NVIC0 → PWM 타이머 IRQ</text>
  <text x="32" y="130" fill="currentColor">cur = trig / log / imu</text>
  <text x="32" y="152" fill="currentColor">crit_nesting[0]</text>
  <text x="32" y="174" fill="currentColor">ipi 펜딩 비트[0]</text>
  <rect x="520" y="40" width="200" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="532" y="62" fill="currentColor" font-weight="bold">core1 (pthread)</text>
  <text x="532" y="86" fill="currentColor">PRIMASK: 자기 코어만</text>
  <text x="532" y="108" fill="currentColor">NVIC1 → UART RX IRQ</text>
  <text x="532" y="130" fill="currentColor">cur = ctl / bg / imu</text>
  <text x="532" y="152" fill="currentColor">crit_nesting[1]</text>
  <text x="532" y="174" fill="currentColor">ipi 펜딩 비트[1]</text>
  <rect x="250" y="40" width="240" height="110" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="262" y="62" fill="currentColor" font-weight="bold">전역 ready list (공유)</text>
  <text x="262" y="84" fill="currentColor" font-family="monospace">p3: ctl{core1}</text>
  <text x="262" y="104" fill="currentColor" font-family="monospace">p2: trig{core0} imu{*}</text>
  <text x="262" y="124" fill="currentColor" font-family="monospace">p1: bg{core1} log{core0}</text>
  <text x="262" y="142" fill="currentColor" font-size="12">{ } = 친화성 마스크</text>
  <rect x="250" y="160" width="240" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="262" y="185" fill="currentColor">커널 스핀락 (SIO SPINLOCKn)</text>
  <line x1="220" y1="100" x2="250" y2="100" stroke="currentColor"/>
  <line x1="490" y1="100" x2="520" y2="100" stroke="currentColor"/>
  <line x1="220" y1="180" x2="250" y2="180" stroke="currentColor"/>
  <line x1="490" y1="180" x2="520" y2="180" stroke="currentColor"/>
  <path d="M 120 190 C 160 260, 560 260, 610 196" fill="none" stroke="currentColor" stroke-width="2"/>
  <polygon points="610,192 602,204 616,202" fill="currentColor"/>
  <text x="250" y="250" fill="currentColor">IPI: core0 → core1 "다시 스케줄해" (RP2040: SIO FIFO 쓰기)</text>
  <text x="10" y="290" fill="currentColor">단일 코어 가정 "__disable_irq() = 독점" — core0 이 꺼도 core1 의 UART ISR·태스크는 그대로 돈다.</text>
  <text x="10" y="312" fill="currentColor">SMP 규칙: 공유 상태(ready list, 링 버퍼 count)는 "자기 코어 IRQ 끄기 + 스핀락" 안에서만 만진다.</text>
</svg>
```

호스트 모델은 이렇다. `pthread` 1개가 코어 1개다. 코어마다 `irq_masked` 플래그(= PRIMASK), 임계 구역 깊이, IPI 펜딩 비트가 있고, ready list 와 커널 스핀락은 하나를 같이 쓴다. Part A·B 는 스레드 없이 **결정적으로**, Part C 는 진짜 스레드 2개로 검사한다.

### Part A — 커널 스핀락, 친화성 ready list, 선점 코어 선택

- `spin_trylock(l, core)` / `spin_unlock(l, core)` — C11 atomics 스핀락. 같은 코어가 다시 잡으면 재귀 카운트만 올린다. 소유자가 아닌 코어의 unlock 은 `-1` 로 거절한다. 각 코어는 `owned_by[core]` **자기 칸만** 읽고 쓴다.
- `rq_pick(core)` — 우선순위별 FIFO 인 전역 ready list 에서 **그 코어가 허용된** 태스크 중 가장 높은 우선순위, 같으면 먼저 온 것을 꺼낸다. 없으면 `NULL` (= idle).
- `pick_core_to_preempt(t)` — 방금 READY 가 된 `t` 를 위해 선점할 코어. 친화성 안에서, 이미 yield 요청이 걸린 코어는 빼고, 현재 우선순위(idle = −1)가 `t` 보다 낮은 코어 중 가장 낮은 것. 동률이면 번호가 큰 코어 (FreeRTOS 의 `<=` 비교 그대로).

### Part B — "인터럽트 끄기 ≠ 상호 배제": UART RX 링 버퍼

UART RX ISR 이 1바이트를 넣고(`count = count + 1`), 파서 태스크가 꺼낸다(`count = count - 1`). 단일 코어 RTOS 에서는 태스크가 `__disable_irq()` 로 감싸면 안전했다. 이제 칩 설정상 UART IRQ 가 **core1 의 NVIC** 로 가고, 파서 태스크는 core0 에서 돈다.

- (주어진 재현) 단일 코어식 임계 구역 `uni_enter/uni_exit` 로는 core0 이 `count` 를 읽은 직후 core1 의 ISR 이 들어와 **바이트 1개가 count 에서 사라진다.** 같은 ISR 을 core0 으로 라우팅하면 펜딩되어 안전하다 — 단일 코어 가정은 거기선 맞다.
- `crit_try_enter(c)` / `crit_exit(c)` — SMP 임계 구역. 중첩은 깊이 카운터, 처음 진입 시 **① 자기 코어 인터럽트 끄기 → ② 커널 락**, 마지막 탈출 시 **① 락 풀기 → ② 인터럽트 복원**. 락을 못 잡으면 인터럽트를 원상복구하고 `false` (주어진 `crit_enter` 가 재시도 루프를 돈다).

### Part C — 듀얼 코어 모터 컨트롤러: 깨우기와 IPI

| 태스크 | 우선순위 | 친화성 | 하는 일 |
|---|---|---|---|
| `ctl` 모터 전류 제어 | 3 | core1 | 주기마다 1 step: 통계++ 후 `trig` 깨우고 BLOCK, 100주기 후 DONE |
| `trig` PWM 주기 타이머 | 2 | core0 | `ctl` 이 잠들면 깨운다 (중첩 임계 구역 안에서 `make_ready`) |
| `bg` 보정 루프 | 1 | core1 | 절대 안 막힘. `ctl` 이 끝날 때까지 돈다 → IPI 없이는 core1 이 영영 안 비킨다 |
| `log` 텔레메트리 | 1 | core0 | 300번 통계++ |
| `imu` 센서 융합 | 2 | 아무 코어 | 400번 통계++ |

- `ipi_send(c)` — 펜딩 비트를 세우고 idle 로 잠든 코어를 **깨우기 유실 없이** 깨운다.
- `make_ready(t, from)` — BLOCKED 면 READY 로 ready list 에 넣고 `pick_core_to_preempt` 로 고른 코어에 yield 요청 (자기 코어면 `yield_pending`, 다른 코어면 IPI). RUNNING 이면 `wake_pending` 만.
- 진짜 스레드 2개로 돌린 뒤 검사: 전부 끝남, `ctl` 정확히 100 step, 두 코어가 같이 쓴 통계 유실 0, 친화성 위반 0, 한 태스크 이중 실행 0, **core1 로 간 IPI 송신 = 수신 = 100**.

## 2. Problem (English)

> **Dual-core SMP scheduler model.** Two pthreads act as two cores sharing one priority-ordered ready list (FIFO per priority) protected by a kernel spinlock. Each core has its own interrupt-mask flag (PRIMASK), its own critical-nesting counter, and an IPI pending bit.
>
> 1. `bool spin_trylock(kspin_t *l, int core)` / `int spin_unlock(kspin_t *l, int core)` — C11-atomics spinlock, recursive per core (`owned_by[core]` + `recursion`); acquire on lock, release on unlock; unlock by a non-owner returns −1 and changes nothing.
> 2. `task_t *rq_pick(int core)` — remove and return the highest-priority READY task whose affinity mask contains `core` (FIFO among equals), or NULL.
> 3. `int pick_core_to_preempt(const task_t *t)` — among cores in `t->affinity` that have no yield already requested/pending, choose the one running the lowest priority (idle = −1) strictly below `t->prio`; ties → highest core index; none → −1.
> 4. `bool crit_try_enter(int c)` / `void crit_exit(int c)` — SMP critical section: nesting counter; on first entry mask this core's IRQs *then* try the kernel lock (on failure restore the mask, return false); on last exit release the lock *then* restore the saved mask.
> 5. `void ipi_send(int c)` and `void make_ready(task_t *t, int from)` — wake a task from core `from`; request a yield on the chosen core (local `yield_pending` or remote IPI).
>
> Example (scripted interleaving, UART ISR routed to core1, injected right after core0 reads `count`=5): IRQ-mask-only → final `count` 4 with 5 bytes in the ring (one byte lost). SMP critical section → trace `c0:irqoff c0:lock c0:ldC c1:IRQ c1:irqoff c1:spin c1:irqon c0:stC c0:unlock c1:irqoff c1:lock c1:ldC c1:stC c1:unlock c1:irqon c0:irqon`, final `count` 5.
>
> Follow-ups: why must IRQs be masked *before* taking the spinlock? Why does RP2040 need hardware spinlocks? What does `configRUN_MULTIPLE_PRIORITIES = 0` protect? How is the IPI delivered on RP2040?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
# 처음엔 "17/49 passed", exit 1. Part C 는 아무 태스크도 안 돌아 3초 타임아웃 후 FAIL (멈추지 않는다)
```

TODO 는 8개 함수다: A1 `spin_trylock`/`spin_unlock`, A2 `rq_pick`, A3 `pick_core_to_preempt`, B1 `crit_try_enter`/`crit_exit`, C1 `ipi_send`, C2 `make_ready`. 코어 루프(`core_main`), idle 대기, 스크립트 훅(`sh_ld`/`sh_st`/`lock_try`/`lock_release`), 시나리오 태스크는 주어진 코드다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_uni_crit.c -o duc && ./duc uni && ./duc smp   # 진짜 스레드 2개
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_uni_crit.c -o duc_t && ./duc_t uni           # TSan 리포트
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_ipi_latency.c -o dil && ./dil               # IPI 대용 3종 지연
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4     -mthumb -O2 -S demo_m0_spinlock.c -o -    # M4 asm
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_m0_spinlock.c -o -    # M0+ (RP2040) asm
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: 재귀 스핀락은 "시도 → 실패하면 내가 주인인지" 순서</summary>

먼저 relaxed load 로 0 인지 보고(TTAS), 0 이면 `atomic_exchange(..., 1, acquire)`. 0 이 돌아오면 내가 잡은 것 — `recursion = 1`, `owned_by[core] = true`. 실패했으면 **내 칸** `owned_by[core]` 만 본다: 참이면 이미 내 락이니 `recursion++`. 다른 코어의 칸을 읽으면 그게 곧 데이터 레이스다. unlock 은 순서가 중요하다: 내 표시를 지우고 **마지막에** release store.

</details>

<details>
<summary>힌트 2 — A2/A3: 친화성은 "건너뛰기", 선점은 "가장 낮은 코어"</summary>

`rq_pick` 은 우선순위 높은 리스트부터 훑되, 허용되지 않은 태스크를 만나면 **멈추지 말고 건너뛴다** (그 뒤에 허용된 태스크가 있을 수 있다). 꺼낼 때 `prev->next`, `head`, `tail` 셋을 다 고친다. `pick_core_to_preempt` 는 `lowest = t->prio - 1` 로 시작해 `cur_prio <= lowest` 인 코어를 만날 때마다 갱신 — 이러면 "엄격히 낮은 것만", "같으면 뒤 번호" 가 한 번에 된다.

</details>

<details>
<summary>힌트 3 — B1: 들어갈 때 IRQ → 락, 나올 때 락 → IRQ</summary>

락을 먼저 잡고 인터럽트를 나중에 끄면, 그 틈에 **같은 코어** 의 ISR 이 같은 락을 잡으려고 돈다 — 락 주인(방금 끼어들림당한 태스크)은 ISR 이 끝나야 진행하니 영원히 못 푼다. 나올 때 인터럽트를 먼저 켜면 같은 틈이 반대쪽에 생긴다. 테스트 trace 가 이 순서를 그대로 검사한다. 락을 못 잡았을 때는 `irq_restore(c, was)` 로 되돌리고 `false`.

</details>

<details>
<summary>힌트 4 — C1: 깨우기 유실 (OS-B06 의 그 문제)</summary>

idle 코어는 `lock(m); while (!ipi) cond_wait; unlock(m)` 으로 잔다. 보내는 쪽이 플래그만 세우고 뮤텍스 없이 `cond_signal` 하면, 상대가 "플래그 확인(거짓) ↔ cond_wait 진입" 사이에 있을 때 신호가 허공에 사라진다. 플래그는 atomic 으로 세우고, `lock(m); signal; unlock(m)` 으로 상대의 확인~대기 구간과 직렬화한다.

</details>

<details>
<summary>힌트 5 — C2: 자기 코어면 IPI 대신 "나갈 때 yield"</summary>

`make_ready` 는 커널 임계 구역 안에서 돈다. 선점할 코어가 **자기 자신** 이면 지금은 임계 구역 안이라 바로 문맥 교환을 할 수 없다 → `yield_pending = true` (FreeRTOS `xYieldPendings[]`, `vTaskExitCritical` 이 나가면서 `portYIELD()`). 다른 코어면 `yield_requested = true` + IPI. RUNNING 중인 태스크를 깨우면 ready list 에 또 넣지 말고 `wake_pending` 만 — 안 그러면 한 태스크가 리스트에 두 번 들어가 두 코어에서 동시에 돈다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API 대응표

| 이 레슨 (호스트) | FreeRTOS SMP (V11.3.2) | RP2040 (pico-sdk / FreeRTOS 포트) | Zephyr SMP | Linux 커널 |
|---|---|---|---|---|
| `core_main` 스레드 1개 | 코어별 `vTaskSwitchContext(xCoreID)` | core0 / core1 (`multicore_launch_core1`) | CPU 별 스케줄러 | CPU 별 runqueue |
| `irq_save` / `irq_restore` | `portSET_INTERRUPT_MASK()` / `portCLEAR_INTERRUPT_MASK()` | `save_and_disable_interrupts()` / `restore_interrupts()` (PRIMASK) | `arch_irq_lock()` | `local_irq_save/restore` |
| `spin_trylock` / `spin_unlock` | `portGET_TASK_LOCK` / `portGET_ISR_LOCK` → `vPortRecursiveLock` | `spin_lock_unsafe_blocking` / `spin_unlock_unsafe` (SIO SPINLOCKn) | `k_spin_lock` 내부 | `arch_spin_lock` |
| `crit_enter` / `crit_exit` | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | `spin_lock_blocking()` / `spin_unlock()` | `k_spin_lock()` / `k_spin_unlock()` | `spin_lock_irqsave` / `spin_unlock_irqrestore` |
| ISR 안의 `crit_enter` | `taskENTER_CRITICAL_FROM_ISR()` (ISR 락만) | 같음 | `k_spin_lock()` | `spin_lock_irqsave` |
| `rq_pick(core)` | `prvSelectHighestPriorityTask(xCoreID)` | — | — | `pick_next_task` |
| `pick_core_to_preempt` | `prvYieldForTask` | — | — | `select_task_rq` + `check_preempt` 류 |
| `ipi_send` | `prvYieldCore` → `portYIELD_CORE` | `vYieldCore`: `sio_hw->fifo_wr = 0` → `SIO_IRQ_PROCn` | `arch_sched_ipi` 류 (버전마다 이름 다름) | `smp_send_reschedule` |
| IPI 처리 (`ipi_rx`) | FIFO ISR 의 `portYIELD_FROM_ISR(pdTRUE)` | `prvFIFOInterruptHandler` | IPI 핸들러 | reschedule IPI |
| `make_ready` | `xSemaphoreGiveFromISR` / `xTaskResumeFromISR` 안의 READY 전환 + `prvYieldForTask` | — | `k_sem_give` | `try_to_wake_up` |
| `yield_pending` / `yield_requested` | `xYieldPendings[core]` / `xTaskRunState = taskTASK_SCHEDULED_TO_YIELD` | — | — | `TIF_NEED_RESCHED` |
| `task_t.affinity` | `uxCoreAffinityMask`, `vTaskCoreAffinitySet` | — | `k_thread_cpu_mask_*` (CONFIG_SCHED_CPU_MASK) | `sched_setaffinity` |

FreeRTOS 열은 `~/.esn_scratch/FreeRTOS-Kernel` (V11.3.2) 소스로 확인했다. Zephyr·Linux 열의 이름은 문서 기준이고, 이 레슨에서 빌드해 보지는 않았다. 맥은 스레드를 코어에 고정할 수 없다 (`sched_setaffinity` 없음, OS-M02 참고) — 그래서 "코어" 는 OS 가 아무 물리 코어에나 올리는 스레드이고, 친화성은 **우리 스케줄러 안의** 규칙이다.

### Part A — 커널 스핀락: 코어 단위 재귀

```c
static bool spin_trylock(kspin_t *l, int core) {
    if (atomic_load_explicit(&l->locked, memory_order_relaxed) == 0 &&
        atomic_exchange_explicit(&l->locked, 1, memory_order_acquire) == 0) {
        l->recursion = 1;
        l->owned_by[core] = true;
        return true;
    }
    if (l->owned_by[core]) { l->recursion++; return true; }   /* 이미 내 락 */
    return false;
}
static int spin_unlock(kspin_t *l, int core) {
    if (!l->owned_by[core] || l->recursion == 0) return -1;
    if (--l->recursion == 0) {
        l->owned_by[core] = false;
        atomic_store_explicit(&l->locked, 0, memory_order_release);
    }
    return 0;
}
```

이 구조는 FreeRTOS RP2040 포트 `portable/ThirdParty/GCC/RP2040/include/portmacro.h:219-257` 의 `vPortRecursiveLock` 을 그대로 줄인 것이다. 그쪽 코드:

```c
if (!spin_try_lock_unsafe(pxSpinLock)) {
    if( ucOwnedByCore[ xCoreID ][ ulLockNum ] ) { ucRecursionCountByLock[ ulLockNum ]++; return; }
    spin_lock_unsafe_blocking(pxSpinLock);
}
ucRecursionCountByLock[ ulLockNum ] = 1;
ucOwnedByCore[ xCoreID ][ ulLockNum ] = 1;
```

| 설계 포인트 | 왜 |
|---|---|
| `owned_by[core]` 를 코어별 칸으로 | "누가 주인이냐" 를 하나의 `owner` 변수로 두면 다른 코어가 그걸 **읽어야** 한다 — 락 밖에서 읽는 공유 변수 = 레이스. 자기 칸만 보면 "내가 주인인가" 는 나만 아는 사실이라 동기화가 필요 없다 |
| `recursion` 은 소유자만 | 락을 잡은 뒤에만 건드리므로 락이 보호한다. 다음 주인은 release→acquire 로 이전 값을 본다 |
| 잡을 때 acquire, 풀 때 release | 임계 구역 안의 읽기/쓰기가 락 밖으로 새지 않게 (C-A03 Part A). Cortex-M4 asm 에서는 `ldrexb/strexb` 뒤 `dmb sy`, 풀기 전 `dmb sy` 로 보였다 (아래 asm 절) |
| relaxed load 먼저 (TTAS) | 잡힌 락에 `exchange` 를 계속 쏘면 캐시 라인을 계속 뺏는다. 기다리는 동안은 읽기만 — TAS/TTAS/ticket 비교는 OS-M07 |
| 재귀가 왜 필요한가 | FreeRTOS SMP 에서 `vTaskSuspendAll()` 이 TASK 락을 잡은 채로 있고 그 안에서 `taskENTER_CRITICAL()` 이 TASK 락을 또 잡는다 (`tasks.c:3960`, `7141`). 같은 코어가 같은 락을 두 번 잡는 경로가 실제로 있다 |

테스트가 확인하는 것: 다른 코어는 못 잡음, 같은 코어 재귀 2단, 소유자 아닌 unlock 거절(락 그대로), 1단 남았을 때 아직 잠김, 안 잡은 락 풀기 거절.

### Part A — 친화성 ready list 와 "어느 코어를 깨울까"

`rq_pick` 은 C-B05/DS 의 연결 리스트 삭제 그대로지만 함정이 하나 있다 — **허용되지 않은 태스크에서 멈추면 안 된다.** 테스트 `push C{core0}, E{*}, A{*}` 후 `rq_pick(1)` 은 C 를 건너뛰고 E, 그다음 A 를 꺼내야 하고, 리스트에는 C 하나가 head=tail 로 남아야 한다.

FreeRTOS SMP 는 여기서 한 가지가 다르다. **실행 중인 태스크도 ready list 에 남아 있고** `xTaskRunState` (코어 번호, 또는 `taskTASK_NOT_RUNNING` −1 / `taskTASK_SCHEDULED_TO_YIELD` −2, `tasks.c:321-324`)로 "누가 돌리는 중" 을 표시한다. 그래서 `prvSelectHighestPriorityTask` (`tasks.c:994-1257`)는 리스트를 훑으며 "NOT_RUNNING 이고 친화성이 맞는 첫 태스크" 를 찾는다. 이 레슨은 Linux 식으로 실행 중 태스크를 리스트에서 빼서 더 단순하게 했다 — 결과(누가 어디서 도나)는 같다.

`pick_core_to_preempt` 는 `prvYieldForTask` (`tasks.c:878-988`, `configRUN_MULTIPLE_PRIORITIES == 1` 경로)의 핵심을 옮긴 것이다 (원문 요약, 매크로 일부 생략):

```c
xLowestPriorityToPreempt = pxTCB->uxPriority - 1;
for (xCoreID = 0; xCoreID < configNUMBER_OF_CORES; xCoreID++) {
    xCurrentCoreTaskPriority = pxCurrentTCBs[xCoreID]->uxPriority;   /* idle 태스크면 -1 */
    if (taskTASK_IS_RUNNING(pxCurrentTCBs[xCoreID]) && xYieldPendings[xCoreID] == pdFALSE)
        if (xCurrentCoreTaskPriority <= xLowestPriorityToPreempt)
            if (pxTCB->uxCoreAffinityMask & (1U << xCoreID)) { xLowestPriorityToPreempt = ...; xLowestPriorityCore = xCoreID; }
}
if (xLowestPriorityCore >= 0) prvYieldCore(xLowestPriorityCore);
```

| 테스트 상황 (core0, core1 현재 우선순위) | t | 답 | 규칙 |
|---|---|---|---|
| idle, idle | p1 * | 1 | idle = −1, 동률 → `<=` 라 뒤 번호 |
| 2, 1 | p3 * | 1 | 가장 낮은 우선순위를 돌리는 코어 |
| 2, 1 | p2 * | 1 | 엄격히 낮은 쪽만 |
| 2, 1 | p1 * | −1 | 같은 우선순위는 선점 안 함 (시간 분할 없음) |
| 2, 1 | p3 {core0} | 0 | 친화성 |
| 2, 1 + core1 이미 IPI 받음 | p3 * | 0 | 이미 비키는 중인 코어는 후보 아님 |
| + core0 yield_pending | p3 * | −1 | |

"이미 IPI 받은 코어는 건너뛴다" 는 FreeRTOS 에서 `taskTASK_IS_RUNNING()` 이 `SCHEDULED_TO_YIELD`(−2)를 실행 중으로 치지 않는 것으로 구현된다. 그 코어는 어차피 곧 스케줄러를 돌면서 ready list 에서 가장 높은 것을 고를 것이므로, IPI 를 또 보내는 대신 다른 코어를 찾는 게 맞다.

### Part B — 재현: 단일 코어식 임계 구역이 두 코어에서

스크립트 모드는 C-M14 의 `rd32` 주입 방식을 코어 두 개로 넓힌 것이다. 공유 `count` 접근(`sh_ld`/`sh_st`)이 n번째일 때 "지금 `g_fire_core` 에 UART 인터럽트가 온다" 를 실행한다. 그 코어가 **현재 코어이고 마스크되어 있으면** 펜딩(복원 순간 실행), **다른 코어면** 그 코어가 지금 ISR 을 실행한다 — 진짜 하드웨어에서 두 코어가 동시에 도는 것을 한 스레드로 결정적으로 흉내 낸 것이다. 다른 코어가 락을 못 잡으면 "spin 중" 으로 남았다가, 락이 풀리는 순간(`lock_release` 훅) 다시 시도한다.

실제 출력 (solution 실행):

```text
UNI : c0:irqoff c0:ldC c1:IRQ c1:irqoff c1:ldC c1:stC c1:irqon c0:stC c0:irqon          → count 4, 링에는 5바이트
SMP : c0:irqoff c0:lock c0:ldC c1:IRQ c1:irqoff c1:spin c1:irqon c0:stC c0:unlock
      c1:irqoff c1:lock c1:ldC c1:stC c1:unlock c1:irqon c0:irqon                        → count 5
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">실제 trace 그대로: UNI (인터럽트만 끔) vs SMP (인터럽트 끔 + 스핀락), count 시작값 5</text>
  <text x="10" y="52" fill="currentColor" font-size="13">UNI c0</text>
  <text x="10" y="86" fill="currentColor" font-size="13">UNI c1</text>
  <rect x="60" y="36" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="68" y="52" fill="currentColor">off</text>
  <rect x="102" y="36" width="38" height="24" stroke="currentColor" fill="none"/><text x="106" y="52" fill="currentColor">ld 5</text>
  <rect x="144" y="70" width="38" height="24" class="f-warn" stroke="currentColor"/><text x="150" y="86" fill="currentColor">IRQ</text>
  <rect x="186" y="70" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="194" y="86" fill="currentColor">off</text>
  <rect x="228" y="70" width="38" height="24" stroke="currentColor" fill="none"/><text x="232" y="86" fill="currentColor">ld 5</text>
  <rect x="270" y="70" width="38" height="24" stroke="currentColor" fill="none"/><text x="274" y="86" fill="currentColor">st 6</text>
  <rect x="312" y="70" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="322" y="86" fill="currentColor">on</text>
  <rect x="354" y="36" width="38" height="24" class="f-warn" stroke="currentColor"/><text x="358" y="52" fill="currentColor">st 4</text>
  <rect x="396" y="36" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="406" y="52" fill="currentColor">on</text>
  <line x1="60" y1="64" x2="434" y2="64" stroke="currentColor" stroke-width="2"/>
  <line x1="186" y1="98" x2="350" y2="98" stroke="currentColor" stroke-width="2"/>
  <text x="450" y="52" fill="currentColor">c0 "임계 구역" (밑줄)</text>
  <text x="450" y="86" fill="currentColor">c1 도 동시에 "임계 구역" 안</text>
  <text x="450" y="110" fill="currentColor">→ in_cs_max = 2, 6 이 4 로 덮임</text>
  <text x="10" y="166" fill="currentColor" font-size="13">SMP c0</text>
  <text x="10" y="200" fill="currentColor" font-size="13">SMP c1</text>
  <rect x="60" y="150" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="68" y="166" fill="currentColor">off</text>
  <rect x="102" y="150" width="38" height="24" class="f-acc" stroke="currentColor"/><text x="106" y="166" fill="currentColor">lock</text>
  <rect x="144" y="150" width="38" height="24" stroke="currentColor" fill="none"/><text x="148" y="166" fill="currentColor">ld 5</text>
  <rect x="186" y="184" width="38" height="24" class="f-warn" stroke="currentColor"/><text x="192" y="200" fill="currentColor">IRQ</text>
  <rect x="228" y="184" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="236" y="200" fill="currentColor">off</text>
  <rect x="270" y="184" width="38" height="24" class="f-warn" stroke="currentColor"/><text x="274" y="200" fill="currentColor">spin</text>
  <rect x="312" y="184" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="322" y="200" fill="currentColor">on</text>
  <rect x="354" y="150" width="38" height="24" class="f-ok" stroke="currentColor"/><text x="358" y="166" fill="currentColor">st 4</text>
  <rect x="396" y="150" width="38" height="24" class="f-acc" stroke="currentColor"/><text x="400" y="166" fill="currentColor">unlk</text>
  <rect x="438" y="184" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="446" y="200" fill="currentColor">off</text>
  <rect x="480" y="184" width="38" height="24" class="f-acc" stroke="currentColor"/><text x="484" y="200" fill="currentColor">lock</text>
  <rect x="522" y="184" width="38" height="24" stroke="currentColor" fill="none"/><text x="526" y="200" fill="currentColor">ld 4</text>
  <rect x="564" y="184" width="38" height="24" class="f-ok" stroke="currentColor"/><text x="568" y="200" fill="currentColor">st 5</text>
  <rect x="606" y="184" width="38" height="24" class="f-acc" stroke="currentColor"/><text x="610" y="200" fill="currentColor">unlk</text>
  <rect x="648" y="184" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="658" y="200" fill="currentColor">on</text>
  <rect x="690" y="150" width="38" height="24" class="f-mut" stroke="currentColor"/><text x="700" y="166" fill="currentColor">on</text>
  <line x1="102" y1="178" x2="434" y2="178" stroke="currentColor" stroke-width="2"/>
  <line x1="480" y1="212" x2="644" y2="212" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="246" fill="currentColor" font-size="13">밑줄 = 락 보유 구간. SMP 에서는 두 밑줄이 겹치지 않는다 → c1 의 ISR 은 c0 이 다 쓴 4 를 읽어 5 를 만든다.</text>
  <text x="10" y="268" fill="currentColor" font-size="13">c1 은 spin 하는 동안 자기 인터럽트를 잠깐 다시 연다 (재시도 루프 모양). c0 은 unlk 뒤에야 on.</text>
  <text x="10" y="290" fill="currentColor" font-size="13">같은 ISR 을 c0 으로 라우팅하면 UNI 에서도 "pend" → c0:irqon 직후 실행 (단일 코어 가정은 참).</text>
  <text x="10" y="312" fill="currentColor" font-size="13">한 칸 = trace 이벤트 1개 (시간 비례 아님).</text>
</svg>
```

UNI 결과를 말로: core0 이 `count` 5 를 레지스터로 읽었다. core0 의 PRIMASK 는 core1 과 아무 상관이 없으므로 core1 의 UART ISR 이 그대로 들어와 6 을 쓴다. core0 은 자기가 읽은 5 에서 1 을 빼 4 를 쓴다. 링에는 head−tail 기준 5바이트가 있는데 `count` 는 4 — 파서는 앞으로 영원히 1바이트를 덜 읽는다 (UART 프로토콜이면 프레임 하나가 꼬인다). `g_in_cs_max == 2` 가 "두 코어가 동시에 임계 구역 안" 을 숫자로 보여 준다.

**진짜 스레드로도** — `demo_uni_crit.c` (core1 = ISR 이 200만 번 push, core0 = 파서가 200만 번 pop 시도):

<!-- code: demo_uni_crit.c -->

실제 출력 3회씩:

```text
uni: pushed=2000000 popped=1506110 count=0 expected=493890  -> CORRUPTED (diff -493890)
uni: pushed=2000000 popped=1992423 count=252528 expected=7577  -> CORRUPTED (diff 244951)
uni: pushed=2000000 popped=1995209 count=1012073 expected=4791  -> CORRUPTED (diff 1007282)
smp: pushed=2000000 popped=1669565 count=330435 expected=330435  -> OK (diff 0)
smp: pushed=2000000 popped=1731289 count=268711 expected=268711  -> OK (diff 0)
smp: pushed=2000000 popped=1771195 count=228805 expected=228805  -> OK (diff 0)
```

TSan (`./duc_t uni`, exit 134) — 이번 실행은 결과가 우연히 맞았는데도(파서가 ISR 시작 전에 끝남) 레이스를 잡았다:

```text
uni: pushed=2000000 popped=0 count=2000000 expected=2000000  -> OK (diff 0)
WARNING: ThreadSanitizer: data race (pid=61572)
  Write of size 4 at 0x0001026c8008 by thread T2:
    #0 uart_isr_core1 demo_uni_crit.c:39 (duc_t:arm64+0x100000984)
  Previous read of size 4 at 0x0001026c8008 by thread T1:
    #0 parser_core0 demo_uni_crit.c:46 (duc_t:arm64+0x10000087c)
  Location is global 'g_count' at 0x0001026c8008 (duc_t+0x100008008)
  …
SUMMARY: ThreadSanitizer: data race demo_uni_crit.c:39 in uart_isr_core1
```

`./duc_t smp` 는 보고 0, exit 0. (TSan 리포트 읽는 법은 TOOL-M09.)

### Part B — 고침: 인터럽트 끄기 **그리고** 스핀락, 순서까지

```c
static bool crit_try_enter(int c) {
    if (g_core[c].crit_nesting > 0) { g_core[c].crit_nesting++; return true; }
    bool was = irq_save(c);                  /* ① 자기 코어 ISR 차단 */
    if (!lock_try(c)) { irq_restore(c, was); return false; }   /* ② 다른 코어 차단 */
    g_core[c].saved_irq = was;
    g_core[c].crit_nesting = 1;
    return true;
}
static void crit_exit(int c) {
    if (g_core[c].crit_nesting == 0) return;
    if (--g_core[c].crit_nesting == 0) {
        lock_release(c);                     /* ① 락 먼저 */
        irq_restore(c, g_core[c].saved_irq); /* ② 인터럽트는 마지막 */
    }
}
```

두 개가 각각 막는 상대가 다르다:

| 막을 상대 | 인터럽트 끄기 | 스핀락 |
|---|---|---|
| 같은 코어의 ISR | 막음 | 못 막음 (아래 교착) |
| 같은 코어의 다른 태스크 (선점) | 막음 (tick/PendSV 도 못 들어옴) | 못 막음 |
| 다른 코어의 태스크 | 못 막음 | 막음 |
| 다른 코어의 ISR | 못 막음 | 막음 (ISR 도 같은 락을 잡는다면) |

**왜 "끄고 → 잡고", "풀고 → 켜고" 인가.** 락을 먼저 잡고 인터럽트를 나중에 끄면, 그 사이 같은 코어의 ISR 이 같은 락을 잡으려 돈다. 락 주인은 바로 그 ISR 에게 끼어들림당한 태스크라 ISR 이 끝나기 전엔 절대 못 푼다 → **한 코어 위의 교착**. 재귀 락이면 교착 대신 ISR 이 "통과" 해서 태스크의 반쯤 쓴 상태를 본다 — 더 조용한 버그다. 테스트 B2 trace (`c0:pend … c0:unlock c0:irqon c0:IRQ`)가 "펜딩된 같은 코어 ISR 은 락이 풀린 **뒤** 들어온다" 를 검사한다. 순서를 뒤집으면 trace 가 `c0:irqon c0:IRQ c0:irqoff c0:lock …` 으로 바뀌어 FAIL 한다.

FreeRTOS SMP 의 실제 순서 (`tasks.c:7129-7174`, `7260-7316`):

```c
void vTaskEnterCritical( void ) {
    portDISABLE_INTERRUPTS();                               /* ① */
    if( portGET_CRITICAL_NESTING_COUNT( xCoreID ) == 0U ) {
        portGET_TASK_LOCK( xCoreID );                       /* ② 태스크 락 */
        portGET_ISR_LOCK( xCoreID );                        /* ③ ISR 락 */
    }
    portINCREMENT_CRITICAL_NESTING_COUNT( xCoreID );
    ...
void vTaskExitCritical( void ) {
    ... if( portGET_CRITICAL_NESTING_COUNT( xCoreID ) == 0U ) {
        xYieldCurrentTask = xYieldPendings[ xCoreID ];
        portRELEASE_ISR_LOCK( xCoreID );
        portRELEASE_TASK_LOCK( xCoreID );
        portENABLE_INTERRUPTS();                            /* 마지막 */
        if( xYieldCurrentTask != pdFALSE ) portYIELD();     /* 임계 구역 중 미뤄 둔 yield */
```

- FreeRTOS 는 락이 **두 개** 다. TASK 락은 태스크끼리(스케줄러 일시 정지 포함), ISR 락은 ISR 까지 포함해 ready list 를 보호한다. `vTaskEnterCriticalFromISR` (`tasks.c:7182-7208`)는 ISR 락만 잡는다 — ISR 까지 TASK 락을 잡으려 하면, 다른 코어에서 `vTaskSuspendAll()` 한 태스크가 `xTaskResumeAll()` 할 때까지 ISR 이 묶여 버리기 때문이다 (이 이유는 소스 구조에서 추론). 모든 경로가 "TASK 락 먼저" 를 지켜 락 순서 교착을 막는다 (`tasks.c:5312` 주석 "Must always acquire the task lock first").
- RP2040 포트에서 두 락은 SIO 하드웨어 스핀락 `PICO_SPINLOCK_ID_OS1/OS2` 다 (`rp2040_config.h:80-85`, `portmacro.h:264-267`). 인터럽트 끄기는 `cpsid i` / `cpsie i` (`portmacro.h:189-190`).
- 차이 하나: FreeRTOS 는 인터럽트를 끈 **채로** 스핀한다. 이 레슨의 `crit_enter` 는 재시도 사이에 자기 코어 인터럽트를 잠깐 연다 (trace 의 `c1:irqoff c1:spin c1:irqon`). 기다리는 동안 이 코어의 ISR 지연을 줄이는 모양이고, 정확성은 같다 (잡는 순간에는 반드시 꺼져 있다). Linux 의 일부 구성에서 `spin_lock_irqsave` 가 이렇게 돈다고 알려져 있지만 이 레슨에서 커널 소스로 확인하지는 않았다.
- 중첩·저장 테스트 B3: 깊이 2 에서 한 번 나와도 마스크·락 유지, 호출자가 이미 막아 둔 상태(부트 코드, HardFault)에서 들어갔다 나오면 **막힌 채로** 돌아온다 (C-M14 Part B 의 저장/복원과 같은 이유).

### Part C — 깨우기 → 코어 선택 → IPI

```c
static void ipi_send(int c) {
    atomic_store_explicit(&g_core[c].ipi, true, memory_order_release);
    pthread_mutex_lock(&g_core[c].m);
    pthread_cond_signal(&g_core[c].cv);
    pthread_mutex_unlock(&g_core[c].m);
}
static void make_ready(task_t *t, int from) {
    crit_enter(from);
    if (t->state == T_BLOCKED) {
        t->state = T_READY;
        rq_push(t);
        int c = pick_core_to_preempt(t);
        if (c == from)  g_core[c].yield_pending = true;                         /* 나갈 때 yield */
        else if (c >= 0) { g_core[c].yield_requested = true; g_core[c].ipi_sent++; ipi_send(c); }
    } else if (t->state != T_DONE) {
        t->wake_pending = true;                                                  /* 실행 중 깨우기 */
    }
    crit_exit(from);
}
```

이것이 FreeRTOS `prvYieldCore` (`tasks.c:347-368`)의 두 갈래와 같다: 대상이 **자기 코어면** `xYieldPendings[core] = pdTRUE` (임계 구역 안이라 지금 바꿀 수 없다), **다른 코어면** 이미 요청하지 않았을 때만 `portYIELD_CORE(core)` + `taskTASK_SCHEDULED_TO_YIELD`. RP2040 에서 `portYIELD_CORE` 는 `vYieldCore` (`port.c:500-514`) — SIO FIFO 에 `sio_hw->fifo_wr = 0` 한 워드를 쓰는 것뿐이고, 상대 코어의 `SIO_IRQ_PROCn` 핸들러 `prvFIFOInterruptHandler` (`port.c:274-284`)가 FIFO 를 비우고 `portYIELD_FROM_ISR(pdTRUE)` 로 PendSV 를 건다. 즉 "IPI" 는 **상대 코어에 거는 평범한 인터럽트** 다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C 한 제어 주기 (시간 →). 화살표 = IPI</text>
  <text x="10" y="64" fill="currentColor">core0</text>
  <text x="10" y="144" fill="currentColor">core1</text>
  <rect x="70" y="44" width="110" height="30" class="f-acc" stroke="currentColor"/><text x="78" y="64" fill="currentColor">trig: 깨우기</text>
  <rect x="180" y="44" width="250" height="30" class="f-mut" stroke="currentColor"/><text x="190" y="64" fill="currentColor">log / imu (trig 는 BLOCKED)</text>
  <rect x="440" y="44" width="110" height="30" class="f-acc" stroke="currentColor"/><text x="448" y="64" fill="currentColor">trig: 깨우기</text>
  <rect x="550" y="44" width="180" height="30" class="f-mut" stroke="currentColor"/><text x="560" y="64" fill="currentColor">log / imu …</text>
  <rect x="70" y="124" width="140" height="30" class="f-mut" stroke="currentColor"/><text x="80" y="144" fill="currentColor">bg (p1, 계속 돈다)</text>
  <rect x="210" y="124" width="150" height="30" class="f-warn" stroke="currentColor"/><text x="220" y="144" fill="currentColor">ctl (p3) 1 step</text>
  <rect x="360" y="124" width="200" height="30" class="f-mut" stroke="currentColor"/><text x="370" y="144" fill="currentColor">bg 재개 (선점됐던 자리)</text>
  <rect x="560" y="124" width="170" height="30" class="f-warn" stroke="currentColor"/><text x="570" y="144" fill="currentColor">ctl 1 step …</text>
  <line x1="170" y1="76" x2="206" y2="120" stroke="currentColor" stroke-width="2"/>
  <polygon points="208,124 200,114 212,114" fill="currentColor"/>
  <line x1="350" y1="122" x2="436" y2="78" stroke="currentColor" stroke-width="2"/>
  <polygon points="440,76 428,76 434,86" fill="currentColor"/>
  <line x1="540" y1="76" x2="556" y2="120" stroke="currentColor" stroke-width="2"/>
  <polygon points="558,124 550,114 562,114" fill="currentColor"/>
  <g fill="currentColor">
    <text x="10" y="190">① trig 가 crit 안에서 ctl.state == BLOCKED 확인 → make_ready(ctl): core1 은 bg(1) 실행 중 &lt; 3 → IPI</text>
    <text x="10" y="212">② core1 은 bg 의 다음 step 경계에서 펜딩 비트를 보고 bg 를 READY 로 돌려놓고 ctl 을 고른다</text>
    <text x="10" y="234">③ ctl 이 make_ready(trig): core0 이 log(1)/idle 이면 IPI, imu(2) 면 자리 없음 → trig 는 READY 로 대기</text>
    <text x="10" y="256">bg 는 절대 막히지 않는다 → IPI 가 없으면 ctl 은 core1 에서 영영 못 돈다 (시작 코드 타임아웃 이유 중 하나)</text>
  </g>
</svg>
```

**코어 루프 (주어진 코드)의 세 지점.** ① 스케줄 포인트(커널 락 안): yield 플래그를 지우고, 펜딩 IPI 를 소비하고, 선점당한 현재 태스크를 꼬리에 돌려놓고, `rq_pick` 으로 다음을 고른다. ② 돌릴 게 없으면 doorbell 에서 잔다. ③ step 사이 = "명령어 경계": 여기서 IPI 펜딩 비트를 `atomic_exchange` 로 보고 서 있으면 ① 로 간다. 실제 코어에서 ③ 은 하드웨어가 하는 일이다 (인터럽트는 명령어 사이에 들어온다).

**ipi_send 의 함정 — 깨우기 유실.** 뮤텍스 없이 `store; cond_signal` 만 하면: idle 코어가 `lock(m)`, 플래그 확인(거짓) → [보내는 쪽 store + signal, 아무도 안 기다림] → `cond_wait` 진입 → 영원히 잠. 플래그를 세운 뒤 **같은 뮤텍스를 잡고** signal 하면, 상대는 "확인 전" 이거나(그러면 참을 본다) "이미 cond_wait 안" 이다(그러면 신호를 받는다). OS-B06 의 lost wakeup 과 같은 구조이고, 실제 하드웨어 IPI 에는 이 문제가 없다 — 펜딩 비트가 하드웨어에 남아 있다가 WFI 에서 깨어나므로 (BM-M11 의 슬립 진입 레이스 참고).

**왜 `ipi_sent[1] == ipi_rx[1] == 100` 이 매 실행 성립하나 (결정적 검사의 근거).**

1. core1 로 가는 IPI 는 `make_ready(ctl)` 뿐이다 (`trig` 의 친화성은 core0, 다른 태스크는 처음부터 READY).
2. `trig` 는 crit 안에서 `ctl.state == T_BLOCKED` 를 확인한 그 구역 안에서 깨운다 → 매번 BLOCKED 경로 → 매번 코어 선택.
3. 그때 core1 은 `bg`(1) 또는 `imu`(2) 를 돌리거나 잠깐 `cur == NULL` 이다 — `bg` 가 `ctl` 끝까지 항상 READY 이므로 idle 로 잠들 일은 없다. 어느 경우든 우선순위 &lt; 3 → core1 선택 → IPI (+1).
4. 이전 IPI 는 반드시 소비됐다: `ctl` 이 READY 가 된 바로 그 임계 구역 뒤에야 core1 이 스케줄 포인트에서 `ctl` 을 고를 수 있고, 그 스케줄 포인트(또는 그 전의 step 경계)에서 펜딩 비트를 `exchange` 로 지운다. 다음 깨우기는 `ctl` 이 다시 BLOCKED 가 된 뒤다 → 두 IPI 가 합쳐질 수 없다.

반대로 core0 쪽 IPI 수는 실행마다 다르다 (`ctl` 이 `trig` 를 깨울 때 `trig` 가 아직 RUNNING 이면 `wake_pending` 경로, core0 이 `imu`(2) 를 돌리면 자리 없음) — 그래서 CHECK 하지 않고 info 로만 찍는다.

solution 실제 출력 (info 줄, 실행마다 다름):

```text
info: ipi core0 sent/rx=98/98  core1=100/100  imu ran_on=0x1 migrations=0  bg steps=164714 preempted=99  imu preempted=0
info: ipi core0 sent/rx=100/100  core1=100/100  imu ran_on=0x2 migrations=0  bg steps=378470 preempted=100  imu preempted=0
info: ipi core0 sent/rx=94/94  core1=100/100  imu ran_on=0x1 migrations=0  bg steps=4926 preempted=98  imu preempted=0   ← TSan 빌드
```

- `bg preempted` 가 99~100 이고 100 이 아닐 때가 있는 이유: `ctl` 이 막힌 직후(`cur == NULL`) IPI 가 오면 core1 은 `bg` 를 선점할 필요 없이 바로 `ctl` 을 고른다.
- `imu` 는 이 실행들에서 한 코어에만 있었다 (migrations 0) — 같은 우선순위끼리는 선점하지 않고 시간 분할도 없으니, 처음 잡은 코어에서 끝까지 간다. 친화성 `*` 라도 옮겨 다닐 이유가 없으면 안 옮긴다.
- 검사 항목 전체: 5개 태스크 모두 DONE, `ctl.steps == 100`, `g_stat == 100 + 300 + 400` (두 코어의 crit 가 진짜 상호 배제), `ran_on` 비트맵이 친화성의 부분집합 (`ctl`/`bg` = 0x2, `trig`/`log` = 0x1), `g_double_run == 0` (태스크마다 `on_cpu` 를 `exchange(1)` 해서 이미 1 이면 이중 실행).

**IPI 대용의 비용** — `demo_ipi_latency.c`, 한쪽 방향 지연 4000회 (Apple M6, 2 P + 4 E 코어, Apple clang 21, load average 약 5 — 다른 작업 동시 실행 중이라 꼬리는 상대 비교만):

| 방식 | 받는 쪽 상태 | min | median | p99 |
|---|---|---|---|---|
| doorbell (atomic + 조건 변수) | idle, cond_wait 로 잠듦 | 1.9–2.0 µs | 5.3–8.7 µs | 69–71 µs |
| poll (atomic 펜딩 비트) | 바쁘게 실행 중 | 0 | 42 ns | 5.9–15 µs |
| signal (`pthread_kill` + 핸들러) | 바쁘게 실행 중 | 1.5–2.2 µs | 2.4–2.7 µs | 9–27 µs |

(3회 실행의 범위. median 42 ns = `CLOCK_UPTIME_RAW` 의 24 MHz 틱 한 칸 ≈ 41.7 ns 라 "측정 한계" 다.) 잠든 코어를 깨우는 비용은 OS 스케줄러 깨우기 비용이다. 하드웨어 IPI 는 이것과 전혀 다르다 — RP2040 에서 FIFO 쓰기는 레지스터 쓰기 1번이고 지연은 상대 코어의 인터럽트 진입 시간 수준이다 (수십 사이클 대표값, 미측정). 호스트 숫자는 **모델의 구조** 를 보여 줄 뿐 MCU 지연이 아니다.

### Cortex-M 에서 스핀락은 무엇이 되나 — `demo_m0_spinlock.c` 실제 asm

<!-- code: demo_m0_spinlock.c -->

```text
@ Cortex-M4 (ARMv7E-M) c11_lock                 @ Cortex-M0+ (ARMv6-M, RP2040) c11_lock
.LBB0_1: ldrexb r2, [r0]                         .LBB0_1: movs r1, #1
         strexb r3, r1, [r0]                              movs r2, #2
         cmp    r3, #0                                    mov  r0, r4
         bne    .LBB0_1                                   bl   __atomic_exchange_1     ← 라이브러리 호출
         lsls   r2, r2, #31                               lsls r0, r0, #31
         dmb    sy                ← acquire               beq  .LBB0_5
         beq    .LBB0_9                          warning: large atomic operation may incur significant
c11_unlock: dmb sy; strb r1, [r0] ← release        performance penalty; ... exceeds the max lock-free size (0 bytes)
```

```text
@ RP2040 SIO 하드웨어 스핀락 (M0+)                 @ smp_crit_inc (M0+): FreeRTOS SMP 식 순서
sio_lock:   ldr r1, [r0]      ; 읽기 = 잡기 시도    mrs   r0, primask   ; ① 저장
            cmp r1, #0                              cpsid i             ;    자기 코어 IRQ 끄기
            beq .LBB2_1       ; 0 이면 다시          ldr r2,[r1]; cmp; beq … ; ② SIO 락 잡기 (spin)
            dmb sy                                  dmb sy
sio_unlock: dmb sy                                  ldr/adds/str        ; 임계 구역
            str r1, [r0]      ; 쓰기 = 해제           dmb sy; str r2,[r1] ; ③ 락 해제
                                                    msr   primask, r0   ; ④ IRQ 복원
```

(M4 의 `c11_lock` 루프는 clang 이 4번 펼쳤다. 위는 한 번분. SIO 주소는 `0xd0000100 + 4*31 = 0xd000017c` 로 `movw #380 / movt #53248` 이 나왔다.)

- **ARMv6-M (M0/M0+) 에는 LDREX/STREX 가 없다** → C11 atomic 이 lock-free 가 아니고 `__atomic_exchange_1` 호출이 된다. 단일 코어 M0+ 의 그 런타임 함수는 보통 인터럽트를 잠깐 꺼서 구현되는데, 그건 **두 코어 사이에서는 원자적이지 않다** — 바로 이 레슨의 Part B 버그다. 그래서 RP2040 은 SIO 블록에 32개의 하드웨어 스핀락(읽으면 잡기 시도: 0 이면 실패, 0 이 아니면 성공 / 쓰면 해제, RP2040 데이터시트 2.3.1 기준)과 코어 간 FIFO 를 둔다. 툴체인/SDK 가 `__atomic_*` 를 어떻게 제공하는지는 빌드마다 확인해야 한다.
- 하드웨어 스핀락은 "원자적 판정" 만 하고 메모리 순서는 주지 않는다 → 잡은 뒤, 풀기 전에 `dmb` 를 직접 넣는다 (pico-sdk 의 `spin_lock_unsafe_blocking` / `spin_unlock_unsafe` 도 메모리 펜스를 넣는 구조로 알고 있다 — 로컬에 SDK 소스가 없어 미확인).
- M4 의 `dmb sy` 는 C11 acquire/release 가 만든 것이고, `smp_crit_inc` 의 `dmb` 는 inline asm 으로 넣은 것이다. `"memory"` 클로버 덕분에 `g_shared` 접근이 `cpsid`~`msr primask` 사이에 머문다 (C-M14 참고).

### 정리: 단일 코어 가정 vs SMP

| 단일 코어에서 참이던 것 | SMP 에서 | 고침 |
|---|---|---|
| `__disable_irq()` = 상호 배제 | 다른 코어·다른 코어의 ISR 은 계속 돈다 | IRQ 끄기 + 스핀락 (`taskENTER_CRITICAL` SMP 판) |
| "높은 우선순위가 도는 동안 낮은 건 안 돈다" | 다른 코어에서 동시에 돈다 | 진짜 락. FreeRTOS 는 기본값 `configRUN_MULTIPLE_PRIORITIES = 0` (`FreeRTOS.h:419-420`)으로 이 가정을 **일부러 지켜 준다** |
| `vTaskSuspendAll()` 하면 아무 태스크도 안 끼어든다 | SMP 판은 TASK 락을 쥔 채로 둔다 (`tasks.c:3960`, 해제는 `xTaskResumeAll` `4089`) → 다른 코어의 태스크 임계 구역은 기다리지만, 그 코어의 ISR 은 돈다 | 짧은 임계 구역 또는 뮤텍스 |
| 깨운 태스크는 나 다음에 바로 돈다 | 다른 코어에 IPI → 그 코어의 다음 경계에서 | `prvYieldForTask` / `prvYieldCore` |
| C11 atomic = ISR 에 안전 | M0+ 에서는 libcall (인터럽트 끄기 구현일 수 있음) → 코어 간 안전 아님 | 하드웨어 스핀락 (SIO), 또는 LDREX/STREX 있는 코어 |
| 전역 "현재 태스크" 변수 1개 | 코어마다 다르다 | `pxCurrentTCBs[core]`, 코어별 중첩 카운트 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution      # 49/49 passed (약 0.1 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_a && ./sol_a   # 49/49, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_t && ./sol_t             # 49/49, ThreadSanitizer 경고 0
```

O2 빌드 8회, TSan 빌드 5회 연속 49/49 를 확인했다. `(void)lock_try; (void)lock_release; (void)ipi_send;` 한 줄은 시작 코드에서 그 함수들이 아직 안 쓰여 `-Werror` 의 unused 경고가 나는 것을 막기 위한 것이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 단일 코어 RTOS 의 `taskENTER_CRITICAL()` (PRIMASK/BASEPRI 만) 을 SMP 에서 그대로 | 다른 코어와 그 코어의 ISR 을 못 막는다 (Part B 재현: count 6 → 4) | SMP 커널의 crit: IRQ 끄기 + 스핀락 |
| 스핀락만 잡고 인터럽트는 그대로 | 같은 코어의 ISR 이 같은 락에서 돌면 주인(끼어들림당한 태스크)이 영영 못 푼다 | `irq_save` → lock … unlock → `irq_restore` (Linux `spin_lock_irqsave` 와 같은 짝) |
| 잡고 나서 끄기 / 켜고 나서 풀기 | 그 한 틈에 같은 코어 ISR 이 들어온다 (B2 trace 가 FAIL) | 끄고→잡고, 풀고→켜고 |
| 락을 못 잡았는데 마스크를 안 되돌림 | 실패한 시도가 그 코어 인터럽트를 계속 막는다 (B3 `g_core[1].irq_masked == false` 검사) | `irq_restore(c, was); return false;` |
| `owner` 변수 하나로 재귀 판정 | 다른 코어가 락 밖에서 `owner` 를 읽음 = 데이터 레이스, TSan 보고 | 코어별 `owned_by[core]` 자기 칸만 |
| 소유자 아닌 unlock 을 그냥 허용 | 다른 코어의 임계 구역이 한가운데서 열린다 | 주인 확인 후 거절 (FreeRTOS 는 `configASSERT`) |
| `rq_pick` 이 허용 안 된 태스크에서 `break` | 같은 우선순위 뒤쪽의 허용된 태스크가 굶는다 | `continue` 로 건너뛰기 |
| `pick_core_to_preempt` 에서 `<` 대신 `<=` 로 시작값 `t->prio` | 같은 우선순위를 선점 → 끝없는 핑퐁 | `lowest = t->prio - 1` 로 시작, `<=` 비교 |
| 이미 IPI 보낸 코어를 또 고름 | 두 번째 태스크가 갈 다른 빈 코어를 놓친다 | `yield_requested/pending` 코어는 건너뛰기 |
| 자기 코어를 선점 대상으로 골랐는데 IPI 를 보냄 / 바로 문맥 교환 | 지금은 임계 구역 안 — 바꾸면 락을 쥔 채 다른 태스크로 간다 | `yield_pending = true`, crit 를 나갈 때 yield |
| RUNNING 태스크를 깨울 때 `rq_push` | 리스트에 이미 실행 중인 태스크가 들어가 다른 코어가 같이 돌린다 (`g_double_run`) | `wake_pending = true` 만 |
| `ipi_send` 에서 뮤텍스 없이 `cond_signal` | idle 코어의 "확인~대기" 틈에 신호 유실 → 코어가 안 깨어남 | store 후 `lock; signal; unlock` |
| IPI 플래그를 `volatile bool` 로 | 코어 간 순서 보장 없음, TSan 레이스 | `atomic_bool` + release/acquire |
| M0+ 에서 `_Atomic` 이 lock-free 라고 가정 | 실제 asm 은 `__atomic_exchange_1` 호출, 코어 간 원자성은 런타임 구현에 달림 | RP2040 SIO 스핀락 |

## 8. 면접 Q&A

<details>
<summary>Q1. 단일 코어에서 잘 돌던 "__disable_irq() 로 감싼 링 버퍼" 를 듀얼 코어 MCU 로 옮겼더니 가끔 바이트가 빠집니다. 왜죠?</summary>

PRIMASK/BASEPRI 는 **자기 코어** 의 인터럽트만 막는다. UART IRQ 가 다른 코어로 라우팅되어 있거나, 다른 코어의 태스크가 같은 링 버퍼를 만지면, `count` 의 읽기-수정-쓰기가 겹쳐 갱신이 사라진다 (이 레슨 Part B: core0 이 5 를 읽은 뒤 core1 이 6 을 쓰고 core0 이 4 를 씀). 고침은 "자기 코어 IRQ 끄기 + 스핀락" — FreeRTOS SMP 의 `taskENTER_CRITICAL()`, Linux `spin_lock_irqsave()`, Zephyr `k_spin_lock()`, pico-sdk `spin_lock_blocking()`. 꼬리: "IRQ 를 한 코어로만 라우팅하고 링 버퍼도 그 코어 태스크만 쓰면?" → 그러면 사실상 단일 코어 문제로 돌아가 PRIMASK 로 충분하다. **친화성으로 공유를 없애는 것** 도 정당한 설계다.

</details>

<details>
<summary>Q2. 스핀락을 잡을 때 왜 인터럽트도 꺼야 하나요? 순서는요?</summary>

같은 코어의 ISR 이 같은 락을 잡으려 하면, 락 주인은 그 ISR 에게 끼어들림당한 태스크라 ISR 이 끝나기 전에 못 푼다 → 한 코어 위 교착. 그래서 **끄고 → 잡고**, **풀고 → 켜고**. 반대 순서는 그 한 틈이 문제다. 인터럽트를 끄는 이유가 하나 더 있다 — 락을 쥔 태스크가 tick 으로 선점되면 다른 코어가 그동안 헛돈다. 꼬리: "ISR 에서는?" → ISR 도 공유 데이터를 만지면 같은 락을 잡아야 한다 (FreeRTOS `taskENTER_CRITICAL_FROM_ISR`, 저장/복원형). ISR 안에서 다른 우선순위 ISR 이 중첩될 수 있으면 ISR 에서도 마스크가 필요하다.

</details>

<details>
<summary>Q3. RP2040 에는 왜 하드웨어 스핀락이 있나요? 그냥 C11 atomic 을 쓰면 안 되나요?</summary>

Cortex-M0+ 는 ARMv6-M 이라 LDREX/STREX 가 없다. clang 으로 `atomic_flag_test_and_set` 을 M0+ 로 컴파일하면 `bl __atomic_exchange_1` 라이브러리 호출과 "max lock-free size (0 bytes)" 경고가 나온다 (실측). 그 런타임 함수가 인터럽트 끄기로 구현돼 있다면 한 코어 안에서만 원자적이고 두 코어 사이에서는 아니다. 그래서 RP2040 의 SIO 블록이 32개 스핀락 레지스터(읽기 = 잡기 시도, 쓰기 = 해제)를 제공하고, FreeRTOS RP2040 포트도 커널 락 두 개를 그걸로 만든다 (`PICO_SPINLOCK_ID_OS1/OS2`). 하드웨어 스핀락은 순서를 보장하지 않으므로 잡은 뒤/풀기 전에 `DMB` 를 같이 쓴다. 꼬리: "M4/M7 듀얼 코어(STM32H7)는?" → LDREX/STREX 가 있지만 두 코어가 같은 캐시 일관성 도메인이 아니어서 LDREX 기반 락에 기대지 않고, 칩의 하드웨어 세마포어(HSEM)를 쓰는 게 표준이다 (RM 기준, 이 레슨에서 미검증).

</details>

<details>
<summary>Q4. SMP RTOS 에서 IPI 는 무엇이고, RP2040 에서는 어떻게 구현되나요?</summary>

다른 코어에 거는 인터럽트다. 용도는 "다시 스케줄해(reschedule)" — 이 코어가 깨운 높은 우선순위 태스크가 다른 코어에서만 돌 수 있거나 그쪽이 더 낮은 걸 돌리고 있을 때. FreeRTOS 는 `prvYieldCore` → `portYIELD_CORE(core)`. RP2040 포트의 `vYieldCore` 는 SIO FIFO 에 한 워드(`sio_hw->fifo_wr = 0`)를 쓰고, 상대 코어의 `SIO_IRQ_PROCn` 핸들러가 FIFO 를 비우고 `portYIELD_FROM_ISR(pdTRUE)` 로 PendSV 를 건다. Cortex-A/R 은 GIC 의 SGI(Software Generated Interrupt), Linux 는 `smp_send_reschedule()`. 꼬리: "자기 코어를 깨워야 하면?" → IPI 대신 `xYieldPendings[core]` 를 세우고 임계 구역을 나갈 때 `portYIELD()`. "IPI 를 두 번 보내면?" → FreeRTOS 는 `taskTASK_SCHEDULED_TO_YIELD` 로 표시해 이미 요청한 코어에는 다시 안 보낸다. 하드웨어 펜딩 비트도 어차피 합쳐진다.

</details>

<details>
<summary>Q5. 새로 READY 가 된 태스크를 어느 코어에서 돌릴지 어떻게 정하나요?</summary>

FreeRTOS `prvYieldForTask` 기준: 그 태스크의 친화성 마스크 안의 코어 중, 이미 yield 가 걸리지 않았고, 현재 돌리는 태스크의 우선순위(idle = −1)가 **엄격히 낮은** 코어들 가운데 가장 낮은 코어를 선점한다. 루프가 `<=` 라 동률이면 번호가 큰 코어가 이긴다. 같은 우선순위는 선점하지 않는다. 덤으로, 선점당해 쫓겨난 태스크가 다른 코어에서 돌 수 있는지도 다시 본다 (`prvSelectHighestPriorityTask` 끝의 친화성 블록 — 추가 연습 2). 꼬리: "이 방식의 약점?" → 전역 ready list + 전역 락이라 코어가 늘면 락 경합이 커진다. Linux 는 코어별 runqueue + 로드 밸런싱을 쓴다. 2~4코어 MCU 에는 전역 리스트가 단순하고 예측 가능해서 맞는다.

</details>

<details>
<summary>Q6. 코어 친화성은 언제 쓰고, 무엇이 위험한가요?</summary>

쓰는 곳: ① 지터에 민감한 제어 루프를 한 코어에 고정하고 나머지를 다른 코어로 (이 레슨의 `ctl` 은 core1), ② 그 코어의 TCM/캐시·그 코어로 라우팅된 IRQ 와 같이 두기, ③ "단일 코어 가정" 으로 짠 레거시 코드를 한 코어에 묶어 안전하게 재사용, ④ 무선 스택처럼 벤더가 특정 코어를 요구할 때 (ESP32 의 Wi-Fi/BT 태스크 고정 등, 문서 기준). 위험: 부하 불균형(고정한 코어가 꽉 차도 다른 코어로 못 간다), 코어 간 우선순위 역전(높은 태스크가 친화성 때문에 바쁜 코어에서 기다리는 동안 다른 코어는 낮은 걸 돌림), 친화성 변경 순간의 처리 (`vTaskCoreAffinitySet` 은 실행 중인데 그 코어가 금지되면 그 코어를 yield 시킨다 — 추가 연습 1). 꼬리: "맥에서 스레드 고정은?" → 안 된다 (OS-M02). Linux 는 `sched_setaffinity` / `isolcpus` (LLAT-M07).

</details>

<details>
<summary>Q7. FreeRTOS SMP 의 configRUN_MULTIPLE_PRIORITIES 는 무엇이고 왜 기본값이 0 인가요?</summary>

`FreeRTOS.h:419-420` 에서 기본 0. 0 이면 "어떤 코어에서 높은 우선순위 태스크가 돌고 있으면, 그보다 낮은 우선순위(idle 제외)는 다른 코어에서도 돌리지 않는다" (`prvSelectHighestPriorityTask` 의 `uxCurrentPriority < uxTopReadyPriority` 분기, `prvYieldForTask` 가 낮은 우선순위 코어들을 모두 yield). 단일 코어 코드가 몰래 기대던 "내가 높은 우선순위로 도는 동안 낮은 태스크는 멈춰 있다 = 상호 배제" 가정을 SMP 에서도 지켜 주는 호환 모드다. 대신 코어를 놀리게 된다. 1 로 바꾸면 이 레슨의 모델처럼 우선순위가 달라도 동시에 돌고, 그런 암묵적 상호 배제에 기대던 코드는 진짜 락으로 고쳐야 한다. 꼬리: "우선순위 기반 상호 배제가 단일 코어에서도 위험한 이유?" → 우선순위가 바뀌거나(상속, RTOS-M04) 태스크가 블록하면 깨진다.

</details>

<details>
<summary>Q8. SMP 에서 스핀락과 뮤텍스는 어떻게 나눠 쓰나요? 단일 코어에서 스핀락은 의미가 있나요?</summary>

스핀락: 수 µs 이하의 짧은 구간, ISR 과 공유하는 데이터, 커널 내부(ready list). 쥔 동안 블록·sleep·긴 루프 금지 (다른 코어가 헛돌고, 인터럽트가 꺼져 있으니 지연이 늘어난다). 뮤텍스: 길거나 블록할 수 있는 구간, 태스크끼리만, 우선순위 상속 필요 시 (RTOS-B10/M04). 단일 코어에서 스핀락은 무의미하거나 위험하다 — 인터럽트를 끈 채 돌면 주인이 절대 못 돌아와 교착이고, 인터럽트를 켜고 돌면 그냥 CPU 낭비다. 그래서 단일 코어 FreeRTOS 의 `portGET_ISR_LOCK` 은 빈 매크로다 (RP2040 `portmacro.h:258-262`, `configNUMBER_OF_CORES == 1` 분기). 꼬리: "ISR 에서 뮤텍스는?" → 불가 (소유할 태스크 정체성이 없다, RTOS-B10).

</details>

<details>
<summary>Q9. 이런 스케줄러를 호스트에서 어떻게 테스트하나요? 스레드 테스트가 비결정적이지 않나요?</summary>

두 층으로 나눈다. ① 결정 로직(`rq_pick`, `pick_core_to_preempt`, `make_ready` 의 분기)과 인터리빙 버그는 **단일 스레드 + 주입 훅** 으로 — "n번째 공유 접근 직후 다른 코어가 ISR 실행" 을 스크립트로 만들면 매번 같은 trace 가 나온다 (Part B). ② 진짜 스레드 테스트에는 **스케줄과 무관하게 성립하는 불변식** 만 CHECK 한다 — 총합 보존(`g_stat`), 친화성 부분집합, 이중 실행 0, 그리고 시나리오 구조로 정확히 셀 수 있는 것(`ipi_sent[1] == 100`). 그리고 TSan 으로 동기화 누락을 잡는다 (결과가 우연히 맞아도 잡는다 — demo 의 TSan 실행). 실제 칩에서는 GPIO 토글 + 로직 분석기, FreeRTOS trace 훅(RTOS-A06), SEGGER SystemView 의 멀티코어 뷰를 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 친화성 바꾸기: `task_set_affinity(t, mask, from)`

FreeRTOS `vTaskCoreAffinitySet` (`tasks.c:3058-3111`) 처럼, 실행 중인 태스크의 지금 코어가 새 마스크에서 빠지면 그 코어를 yield 시키고, READY 태스크면 새 마스크로 다시 선점 코어를 찾는다. 코어 4개 모델로 검사.

<details>
<summary>답 보기</summary>

```c
t->affinity = mask;
if (t->state == T_RUNNING) {
    if (!(mask & (1u << t->run_core))) request_yield(t->run_core, from);
} else if (t->state == T_READY) {
    int c = pick_core_to_preempt(t);
    if (c >= 0) request_yield(c, from);
}
```

실행 중인데 지금 코어가 여전히 허용되면 아무 일도 하지 않는다 — 더 낮은 코어가 있어도 옮기지 않는다 (FreeRTOS 도 같다). `request_yield` 는 `prvYieldCore` 처럼 자기 코어면 `yield_pending`, 아니면 IPI. 실제 코드는 이 전체를 `taskENTER_CRITICAL()` 안에서 한다.

</details>

### 연습 2 — 쫓겨난 태스크 다시 올리기: `replace_evicted(prev, c)`

core `c` 가 더 높은 태스크를 올리면서 `prev` 를 ready list 로 돌려보냈다. `prev` 가 갈 수 있는 다른 코어(엄격히 낮은 우선순위를 돌리는)를 찾아 yield 시켜라. `prev` 가 `c` 에서 돌 수 있었다면 새 태스크의 친화성 코어는 후보에서 뺀다. 높은 번호 코어부터, 엄격히 `<` 일 때만 갱신.

<details>
<summary>답 보기</summary>

`prvSelectHighestPriorityTask` 끝의 `configUSE_CORE_AFFINITY` 블록 (`tasks.c:1169-1256`) 그대로다. 후보에서 새 태스크 `nw` 의 친화성 코어를 빼는 근거(FreeRTOS 주석): `nw` 는 자기가 갈 수 있는 코어 중 **가장 낮은** 코어(`c`)를 골랐으므로, `nw` 가 갈 수 있는 나머지 코어는 모두 `prev` 이상의 우선순위를 돌리고 있다 → 볼 필요가 없다. `prev` 의 친화성이 바뀌어 `c` 가 금지된 경우는 이 논증이 안 맞으므로 자기 마스크 전체를 본다. 테스트: 후보 {2,3} 중 0 순위인 2 / 2·3 동률이면 3 (내림차순 + `<`) / 3 이 이미 IPI 받았으면 2 / 같은 우선순위만 있으면 −1.

</details>

### 연습 3 — RP2040 SIO 하드웨어 스핀락 모델

`sio_spinlock_read(n)` (읽기 = 잡기 시도, 성공이면 0 이 아닌 값), `sio_spinlock_write(n)` (해제) 가 주어진다. pico-sdk `spin_lock_blocking` / `spin_unlock` 모양으로 `hw_spin_lock_blocking(n)` (인터럽트 저장·끄기 → 잡힐 때까지 읽기, 저장값 반환) / `hw_spin_unlock(n, saved)` 를 작성하고, 스레드 2개가 20만 번씩 공유 카운터를 올려 정확히 40만이 되는지 확인하라.

<details>
<summary>답 보기</summary>

```c
uint32_t saved = save_and_disable_interrupts();   /* ① 이 코어 ISR 차단 */
while (sio_spinlock_read(n) == 0u) { }            /* ② 하드웨어가 원자적으로 판정 */
return saved;
/* unlock */ sio_spinlock_write(n); restore_interrupts(saved);
```

모델에서 "레지스터 읽기의 부작용" 은 `atomic_exchange(acquire)`, 쓰기는 `atomic_store(release)` 로 표현했다 — 실제 칩에서 순서를 맡는 건 SDK 가 넣는 `DMB` 다 (demo asm 의 `dmb sy`). 인터럽트 상태는 스레드(=코어)별 `_Thread_local` 이다. 이미 막혀 있던 호출자가 쓰면 해제 후에도 막혀 있어야 한다 (저장/복원). TSan 깨끗.

</details>

### 연습 4 — `pthread_kill` 을 IPI 로

본문은 doorbell(조건 변수)을 썼다. 이번엔 진짜 "비동기 인터럽트" 처럼 `pthread_kill(core, SIGUSR1)` 로 IPI 를 보내고, 핸들러는 lock-free `atomic_bool` 펜딩 비트만 세우며, 코어 루프가 경계에서 `atomic_exchange` 로 처리한다. 200번 보내고(하나씩 처리 확인) 200번 처리됐는지 확인하라.

<details>
<summary>답 보기</summary>

핸들러에서 할 수 있는 건 C11 7.14.1.1p5 가 허용하는 것 — lock-free 원자 객체 접근 — 뿐이라 `atomic_is_lock_free` 를 먼저 CHECK 한다. 실제 IPI 핸들러도 같다: FIFO 를 비우고 "다시 스케줄" 만 요청한다 (`prvFIFOInterruptHandler`). 하나 보내고 처리될 때까지 기다리는 이유: 펜딩 비트는 1개라 처리 전에 두 번 오면 합쳐진다 (NVIC 펜딩과 같은 정보량, C-M14). 대기에는 상한(약 2 s)을 둬서 틀린 구현이 멈추지 않고 FAIL 하게 했다. `demo_ipi_latency.c` 실측으로 바쁜 코어에 대한 signal 방식 median 은 약 2.4–2.7 µs 였다 (호스트 값).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice   # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t            # 16/16, TSan 경고 0
```

## 10. 핵심 정리

- **PRIMASK 는 자기 코어만 막는다.** SMP 임계 구역 = 자기 코어 IRQ 끄기 + 스핀락, 순서는 "끄고→잡고 / 풀고→켜고" (FreeRTOS SMP `vTaskEnterCritical`, Linux `spin_lock_irqsave`).
- 둘이 막는 상대가 다르다: IRQ 끄기 = 같은 코어의 ISR·선점, 스핀락 = 다른 코어(와 그 코어의 ISR). 락만 잡고 IRQ 를 안 끄면 같은 코어 ISR 과 교착.
- 커널 스핀락은 코어 단위 재귀 + 코어별 `owned_by[core]` (RP2040 포트 `vPortRecursiveLock`), 잡을 때 acquire / 풀 때 release.
- 전역 ready list 에서 고르기 = 높은 우선순위부터, 친화성 안 맞으면 **건너뛰기**. 깨운 태스크의 자리 = 친화성 안에서 엄격히 낮은 우선순위 중 가장 낮은 코어 (`prvYieldForTask`, 동률 → 뒤 번호, 이미 yield 중인 코어 제외).
- 대상이 자기 코어면 `yield_pending`(나갈 때 yield), 다른 코어면 IPI. RP2040 IPI = SIO FIFO 쓰기 → 상대 코어 `SIO_IRQ_PROCn` → `portYIELD_FROM_ISR`.
- M0+ (ARMv6-M) 에는 LDREX/STREX 가 없어 C11 atomic 이 libcall → 코어 간 원자성은 하드웨어 스핀락(SIO)으로.
- 단일 코어 가정 3종 — "IRQ 끄면 독점", "높은 우선순위가 돌면 낮은 건 멈춤", "스케줄러 정지 = 독점" — 은 SMP 에서 모두 다시 따져야 한다 (`configRUN_MULTIPLE_PRIORITIES = 0` 은 두 번째를 지켜 주는 호환 모드).

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` — `prvYieldCore` 347-368, `xYieldPendings` 510, `prvCheckForRunStateChange` 811-874, `prvYieldForTask` 878-988, `prvSelectHighestPriorityTask` 994-1257, `vTaskCoreAffinitySet` 3058-3111, `vTaskSuspendAll` (SMP) 3935-3990, `xTaskResumeAll` 4064-4089, `vTaskSwitchContext` (SMP) 5300-5325, `vTaskEnterCritical` 7129-7174, `vTaskEnterCriticalFromISR` 7182-7208, `vTaskExitCritical` 7260-7316; `include/FreeRTOS.h` 419-420 (`configRUN_MULTIPLE_PRIORITIES`), 515-521 (`configUSE_CORE_AFFINITY`); `include/task.h` 217 (`tskNO_AFFINITY`) — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS RP2040 포트: `portable/ThirdParty/GCC/RP2040/include/portmacro.h` (158 `portYIELD_CORE`, 189-190 `cpsid/cpsie`, 219-257 `vPortRecursiveLock`, 258-267 락 매크로), `port.c` (274-284 `prvFIFOInterruptHandler`, 324-330 FIFO IRQ 설치, 500-514 `vYieldCore`), `rp2040_config.h` 80-85
- FreeRTOS 문서 — Symmetric Multiprocessing (SMP) with FreeRTOS — <https://www.freertos.org/>
- Raspberry Pi, RP2040 Datasheet — 2.3.1 SIO (Hardware Spinlocks, Inter-processor FIFOs) — <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- ARMv7-M Architecture Reference Manual (DDI0403) A3.4 Synchronization and semaphores (LDREX/STREX), ARMv6-M Architecture Reference Manual (DDI0419) — 배타적 접근 명령 없음 — <https://developer.arm.com/documentation/ddi0403/latest/>
- C11 (N1570) 7.17 Atomics, 7.14.1.1p5 (핸들러에서 lock-free 원자 객체) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- POSIX.1-2017 `pthread_cond_wait` (뮤텍스와 조건 확인), `pthread_kill` — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_wait.html>
- Linux kernel docs — Lesson 1–3: Spin locks (`spin_lock_irqsave`) — <https://docs.kernel.org/locking/spinlocks.html>
- Zephyr — Symmetric Multiprocessing (`k_spin_lock`, IPI, CPU mask) — <https://docs.zephyrproject.org/latest/kernel/services/smp/smp.html>
- 관련 레슨: C-M14 (PRIMASK 저장/복원, Q8 듀얼 코어), C-A03 (acquire/release 스핀락), OS-M07 (TAS/TTAS/ticket), OS-B06 (조건 변수와 깨우기 유실), OS-M02 (맥의 코어 고정 불가), TOOL-M09 (TSan 리포트), RTOS-B08/B10/M01 (세마포어·뮤텍스·알림), RTOS-M03/M04/M05 (우선순위 역전·상속·천장), RTOS-A06 (트레이싱), BM-M11 (WFI 와 펜딩 인터럽트), ELX-M04 (드라이버의 spinlock vs mutex), PLAT-M05 (메일박스 + doorbell), SOCX-B10 (GIC affinity 라우팅), RISCV-A02 (IPI), LLAT-M07 (CPU 격리)
