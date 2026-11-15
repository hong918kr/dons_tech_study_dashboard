---
id: ELX-M06
time: 90분
updated: 2026-11-08
---
# ELX-M06 Bottom half 비교: tasklet vs workqueue vs threaded IRQ

## 학습 목표

- 같은 작업(체크섬 계산 + 통계 기록)을 **tasklet, 시스템 workqueue, 전용 `WQ_HIGHPRI` workqueue, threaded IRQ, `WQ_BH` workqueue** 다섯 가지로 구현하고, 각각이 실행되는 컨텍스트를 코드 안에서 (`in_hardirq()` / `in_serving_softirq()` / `in_task()` / `preemptible()`) 직접 보고할 수 있다.
- "tasklet 에서는 왜 sleep 하면 안 되나" 를 실제 `BUG: scheduling while atomic` + Oops 로 재현해 설명하고, 어떤 메커니즘이 sleep 가능한지 말할 수 있다.
- IRQ → 처리 시작 지연을 **두 가지 방법**(모듈 내부 `ktime` 스탬프, ftrace 이벤트 파싱)으로 재고, p50/p99/max 표를 만들고 "TCG 숫자에서 믿을 수 있는 것은 순서뿐" 이라는 한계를 설명할 수 있다.
- 면접 질문 "hard IRQ / threaded IRQ / workqueue 중 무엇을 고르나" 에 sleep 가능 여부와 지연을 근거로 답할 수 있다.

## 1. 문제

드라이버를 쓰는 펌웨어 엔지니어는 인터럽트 핸들러에 "어디까지 넣고, 나머지를 어디로 넘길지" 를 매번 결정한다. 하드 IRQ 핸들러는 짧아야 하고 sleep 할 수 없다. 그러면 나머지 일은 어디서 하나? MCU 라면 ISR 에서 태스크로 넘기는 패턴(RTOS-B10, RTOS-M03)이고, 리눅스에서는 그 넘기는 곳의 후보가 여러 개다: tasklet, softirq, workqueue, threaded IRQ, 그리고 최근 tasklet 을 대체하는 BH workqueue. 면접에서 "이 드라이버의 bottom half 를 어떻게 설계하시겠어요?" 라고 물을 때 답의 핵심은 **컨텍스트(sleep 가능 여부)와 지연의 교환** 이다.

실험 환경과 IRQ 소스를 먼저 밝힌다.

- **커널**: Linux 6.18.55-dbg (PROVE_LOCKING, DEBUG_ATOMIC_SLEEP, FTRACE). **QEMU TCG cortex-a57 -smp 4**. 
- **IRQ 소스**: `gpio-sim` 라인 1개 (CONFIG_GPIO_SIM=y). hrtimer 대신 고른 이유: 진짜 irq 번호가 있어서 `irq_handler_entry` tracepoint 와 `/proc/interrupts` 카운트를 그대로 쓸 수 있고, threaded IRQ 를 요청할 수 있다. 사용자 공간 도우미 `fire` 가 `pull` 속성에 값을 써서 rising edge 를 하나씩 만든다.
- **모든 지연 숫자는 TCG 숫자다.** QEMU 소프트웨어 CPU 위의 측정이라 실제 하드웨어 지연이 아니다. 믿을 수 있는 건 메커니즘 사이의 **순서** 뿐이다.

ELX-M05 가 threaded IRQ 자체(IRQF_ONESHOT, sysfs 히스토그램)를 다룬다. 여기서는 threaded IRQ 를 다섯 후보 중 하나로만 쓴다. 락 선택은 ELX-M04 이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">IRQ 하나가 처리되는 길: 어느 층(컨텍스트)까지 내려가나</text>
  <rect x="10" y="36" width="720" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-weight="bold">hard IRQ (in_hardirq)</text>
  <text x="20" y="78" fill="currentColor">bhc_hard(): t0 = ktime_get_ns() → tasklet_schedule / queue_work / return IRQ_WAKE_THREAD</text>
  <path d="M130 96 L130 118" stroke="currentColor" stroke-width="1.5"/>
  <path d="M520 96 L520 118" stroke="currentColor" stroke-width="1.5"/>
  <rect x="10" y="120" width="350" height="86" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="142" fill="currentColor" font-weight="bold">softirq (in_serving_softirq) — sleep 불가</text>
  <text x="20" y="162" fill="currentColor">tasklet: vec=6 TASKLET_SOFTIRQ</text>
  <text x="20" y="182" fill="currentColor">WQ_BH work: tasklet_action 안의 work 처리</text>
  <text x="20" y="198" fill="currentColor" font-size="12">current 는 끼어든 프로세스(fire 등), 내 것이 아님</text>
  <rect x="375" y="120" width="355" height="86" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="385" y="142" fill="currentColor" font-weight="bold">task (in_task + preemptible) — sleep 가능</text>
  <text x="385" y="162" fill="currentColor">system_percpu_wq: kworker/N:M</text>
  <text x="385" y="182" fill="currentColor">WQ_HIGHPRI: kworker/N:MH (nice -20 풀)</text>
  <text x="385" y="198" fill="currentColor">threaded IRQ: irq/22-bh_cmp (SCHED_FIFO 50)</text>
  <rect x="10" y="222" width="720" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="242" fill="currentColor">측정: t1 - t0 (모듈 ktime) + ftrace 짝 맞추기 (아래 그림)</text>
  <text x="20" y="258" fill="currentColor">지연이 커지는 쪽 ← softirq는 IRQ 복귀 직후 / task는 스케줄러를 거친다 → sleep 가능</text>
  <text x="10" y="294" fill="currentColor">핵심 교환: 아래로 내려갈수록 자유(sleep, 큰 작업)가 늘고, 스케줄링 지연도 는다.</text>
  <text x="10" y="314" fill="currentColor">tasklet은 소스에서 deprecated 표시 → 새 코드는 threaded IRQ 또는 WQ_BH를 고른다.</text>
</svg>
```

- **체크 코드 (starter/solution/practice.c + check.sh)**: QEMU 게스트 안에서 커널 모듈로 돈다. PASS/FAIL 은 **개수와 컨텍스트 플래그** 로만 결정한다 (타이밍은 절대 판정에 쓰지 않는다).
- **측정 데모**: `check.sh solution measure 300` 이 내부 ktime + ftrace 지연표를 찍는다. `check.sh solution splat` 은 tasklet 에서 sleep 하는 잘못을 일부러 재현한다.

### Part A — 다섯 메커니즘을 같은 작업으로 구현

`bhc_dispatch(mech)` 는 하드 IRQ 에서 불려 선택된 메커니즘으로 일을 넘긴다. `bhc_body()` 는 모든 메커니즘이 실행하는 **같은 작업** 이다 (t1 스탬프, 컨텍스트 기록, 체크섬). `bhc_context_flags()` 가 현재 컨텍스트를 플래그로 돌려준다. 세 TODO 를 채운다: 컨텍스트 플래그, 디스패치, 설치/해제 (tasklet_setup, INIT_WORK, alloc_workqueue, 해제 순서).

### Part B — 컨텍스트와 sleep 가능 여부를 증명

게스트에서 메커니즘별로 K 개의 edge 를 쏘고 다음을 확인한다: 각 메커니즘이 K 번 실행, 컨텍스트 플래그가 기대값(`SOFT` 또는 `TASK+SLEEP`)으로 안정적, `/proc/interrupts` 카운트 정확히 일치, dmesg 에 BUG/WARNING/lockdep 없음. sleep 가능한 세 메커니즘은 `usleep_range()` 를 실제로 호출해서 sleep 이 합법임을 보인다. `bad_tasklet_sleep=1` 로 tasklet 에서 `msleep()` 을 부르면 무슨 일이 생기는지 (`splat` 데모)도 본다.

### Part C — 지연 분포 측정과 판단 가이드

idle 과 CPU 부하(게스트 4코어에 busy loop 4개) 두 조건에서 메커니즘별 IRQ→처리 시작 지연의 p50/p99/max 를 내부 ktime 과 ftrace 파싱 두 방법으로 표로 낸다. 결과를 읽고 "어떤 경우 무엇을 고르나" 판단 가이드를 쓴다.

## 2. Problem (English)

You have a device IRQ (here a `gpio-sim` line) and one piece of deferred work: record a timestamp delta, run a small checksum. Implement that same work item using five Linux deferral mechanisms and compare them.

```c
enum bhc_mech { MECH_TASKLET, MECH_WQ_SYS, MECH_WQ_HI, MECH_THREADED, MECH_WQ_BH, MECH_NR };

static u32 bhc_context_flags(void);           /* CTX_HARDIRQ/SOFTIRQ/TASK/CAN_SLEEP */
static irqreturn_t bhc_dispatch(enum bhc_mech m);   /* called from the hard IRQ handler */
static int bhc_setup_bottom_halves(void);
static void bhc_teardown_bottom_halves(void);       /* after free_irq() */
```

Requirements:

- Hard handler stamps `t0`, the deferred body computes `t1 - t0`, records its context and increments a done counter (`/sys/kernel/bh_cmp/done`).
- Mechanisms: `tasklet_schedule`, `queue_work(system_percpu_wq)`, `queue_work` on a dedicated `WQ_HIGHPRI` workqueue, `IRQ_WAKE_THREAD` into a threaded handler, `queue_work` on a `WQ_BH` workqueue.
- For each mechanism, K edges → exactly K bodies, stable context flags as expected, `/proc/interrupts` delta exactly equals the fired edge count, no `BUG:/WARNING:/lockdep` in dmesg.
- Show that the sleeping-capable mechanisms really may sleep, and what happens when a tasklet sleeps.
- Measure IRQ→handler latency via (a) internal ktime stamps and (b) ftrace events (`irq_handler_entry`, `softirq_entry`, `workqueue_execute_start`, `sched_switch`) parsed by a script; report p50/p99/max for idle and CPU-load runs. Numbers are TCG numbers: compare only ordering.

Example (check mode): `tasklet n=40 ctx=SOFT`, `wq_hi n=40 ctx=TASK+SLEEP`, `/proc/interrupts delta=200 expected=200`.

## 3. 시작 코드

세 TODO(컨텍스트 플래그, 디스패치, 설치/해제)를 채운다. 파일: `starter.c` (모듈), `fire.c` (edge 를 하나씩 쏘고 처리 완료를 기다리는 게스트용 정적 바이너리), `guest.sh` (게스트 안에서 도는 시험 스크립트), `parse_trace.awk` (ftrace 파서), `check.sh` (빌드 → QEMU → 판정, 2분 안에 끝남).

<!-- code: starter.c -->

빌드와 실행 (전부 Lima VM 안에서. 맥의 레슨 폴더는 VM 에서 읽기 전용이라 `check.sh` 가 `/tmp/ELX-M06_work/` 로 복사해 빌드한다):

```bash
limactl shell elinux -- bash -lc '/Users/donh/.../lessons/ELX/ELX-M06/check.sh starter'     # FAIL, exit 1
limactl shell elinux -- bash -lc '/Users/donh/.../lessons/ELX/ELX-M06/check.sh solution'    # PASS, exit 0
limactl shell elinux -- bash -lc '/Users/donh/.../lessons/ELX/ELX-M06/check.sh solution measure 300'  # 지연표
limactl shell elinux -- bash -lc '/Users/donh/.../lessons/ELX/ELX-M06/check.sh solution splat'        # tasklet msleep 재현
```

**모듈을 VM 의 실행 중 커널에 insmod 하지 않는다.** `check.sh` 는 항상 게스트 QEMU 안에서 띄운다. 게스트 커널 `6.18.55-dbg` 전용 빌드 트리(`/var/lab/out/linux-build-dbg`)를 쓴다.

시작 코드 실행 결과 (실제 출력, 일부 생략):

```text
  wq_sys: n=0 ctx=NONE stable=1 comm=
CHECK wq_sys_context FAIL
…
  /proc/interrupts delta=1 expected=15
CHECK proc_interrupts_exact FAIL
CHECK hard_irq_count_exact FAIL
CHECK irq_thread_exists PASS
…
VERDICT: FAIL (5 passed, 18 failed)
```

`bhc_dispatch` 가 비어 있으니 하드 핸들러는 첫 edge 를 받고(`/proc/interrupts delta=1`), bottom half 는 실행되지 않아 `fire` 가 메커니즘별 1.5초 타임아웃으로 끝나고 다음 메커니즘으로 넘어간다. 무한 대기가 없다.

## 4. 힌트

<details><summary>힌트 1 — 컨텍스트 플래그</summary>

`include/linux/preempt.h` 의 매크로 네 개면 충분하다. `in_hardirq()`, `in_serving_softirq()`, `in_task()`, 그리고 sleep 가능 여부는 `in_task() && preemptible()`. `preemptible()` 은 `preempt_count == 0` 이고 IRQ 가 켜져 있음을 본다. CONFIG_PREEMPT_COUNT 가 켜져 있어야 의미가 있다(이 커널은 CONFIG_PREEMPT=y).

</details>

<details><summary>힌트 2 — 디스패치의 반환값</summary>

tasklet / workqueue 경로는 일을 큐에 넣고 `IRQ_HANDLED` 를 돌려준다. threaded 경로는 `IRQ_WAKE_THREAD` 를 돌려줘야 코어가 thread 핸들러를 깨운다. `request_threaded_irq` 를 한 번만 호출하고 모드에 따라 반환값만 바꿔도 된다.

</details>

<details><summary>힌트 3 — WQ_BH 와 WQ_PERCPU</summary>

`alloc_workqueue("bhc_bh", WQ_BH | WQ_PERCPU, 0)`. `max_active` 는 0 이어야 한다 (`kernel/workqueue.c` 의 `WARN_ON_ONCE(max_active)`). `include/linux/workqueue.h` 에서 `__WQ_BH_ALLOWS = WQ_BH | WQ_HIGHPRI | WQ_PERCPU` 를 확인하라. 6.18 에서는 시스템 큐 이름도 `system_percpu_wq` 쪽이다 (`system_wq` 는 주석에 "use system_percpu_wq, this will be removed").

</details>

<details><summary>힌트 4 — 해제 순서</summary>

`free_irq()` 가 먼저다. 그 뒤 새 일이 안 들어오는 상태에서 `tasklet_kill`, `cancel_work_sync`, `destroy_workqueue` 순서로 닫는다. 이미 `solution.c` 의 `bhc_exit` 가 `free_irq` 를 먼저 호출한다.

</details>

## 5. 풀이

### Part A — 컨텍스트는 코드가 보고한다

`bhc_context_flags()` 네 줄로 플래그를 만들었다. 세 번 돌린 `check.sh solution` 의 실제 출력 (K=40, 6.18.55-dbg, QEMU TCG cortex-a57 -smp 4). 아래는 세 번째 run:

```text
  tasklet: n=40 ctx=SOFT stable=1 comm=fire
  wq_sys: n=40 ctx=TASK+SLEEP stable=1 comm=kworker/0:1
  wq_hi: n=40 ctx=TASK+SLEEP stable=1 comm=kworker/2:1H
  threaded: n=40 ctx=TASK+SLEEP stable=1 comm=irq/22-bh_cmp
  wq_bh: n=40 ctx=SOFT stable=1 comm=fire
  /proc/interrupts delta=200 expected=200
hard ctx=HARD irqs=200
```

(같은 출력의 `stats` 줄에는 지연도 찍히지만 K=40 이고 판정에 쓰지 않으므로 Part C 측정표에서 따로 본다.)

`request_threaded_irq(irq, bhc_hard, bhc_thread_fn, IRQF_TRIGGER_RISING, "bh_cmp", &g)` 호출을 한 번만 하고, `bhc_hard` 가 모드에 따라 `IRQ_WAKE_THREAD` 또는 `IRQ_HANDLED` 를 돌려준다. 하드 핸들러를 직접 주었기 때문에 이 레슨에서는 `IRQF_ONESHOT` 없이 등록할 수 있다 (왜 hard handler 없는 threaded-only 등록에 ONESHOT 이 필요한지는 ELX-M05). `ps` 의 `[irq/22-bh_cmp]` 는 `request_threaded_irq` 시점에 이미 생긴다.

`fire.c` (게스트용 도우미)는 rising edge 를 하나 쏜 뒤 `/sys/kernel/bh_cmp/done` 이 오를 때까지 기다리고 (타임아웃 있음) falling edge 를 쏜다. 이 handshake 가 두 가지를 보장한다: 지연 측정 표본이 겹치지 않고, K개의 edge 가 정확히 K번의 실행이 되어 **개수로 PASS/FAIL 을 판정할 수 있다**. 대신 실제 드라이버처럼 이벤트가 몰리는 상황을 재현하지 않는다. 그건 추가 연습 1, 2 에서 합침(coalescing)을 따로 본다.

- `comm` 열이 재미있다. tasklet 과 `wq_bh` 는 `fire` 다. softirq 에서는 **`current` 가 끼어든 프로세스** 일 뿐이다. 그래서 softirq 안에서 `current` 를 쓰는 것은 이상하다. workqueue 는 `kworker/1:1`, 고우선순위 큐는 `kworker/3:1H` (이름의 `H` 는 `kernel/workqueue.c` 에서 `pool->attrs->nice < 0 ? "H" : ""`). threaded IRQ 는 `irq/22-bh_cmp`.
- 판별 기준은 `include/linux/preempt.h` 에 있다: `in_task()` 는 `preempt_count & (NMI|HARDIRQ|SOFTIRQ_OFFSET)` 가 0 일 때 참, `in_serving_softirq()` 는 softirq **처리 중**을 말한다.

**WQ_BH 는 정말 있나? (소스로 확인)** `/var/lab/src/linux-6.18.55` 에서:

- `include/linux/workqueue.h`: `WQ_BH = 1 << 0, /* execute in bottom half (softirq) context */`, `__WQ_BH_ALLOWS = WQ_BH | WQ_HIGHPRI | WQ_PERCPU`.
- `kernel/softirq.c`: `tasklet_action()` 첫 줄이 `workqueue_softirq_action(false)`, `tasklet_hi_action()` 은 `workqueue_softirq_action(true)` — BH work 가 tasklet softirq 벡터 안에서 돈다.
- `kernel/workqueue.c:7915`: `system_bh_wq = alloc_workqueue("events_bh", WQ_BH | WQ_PERCPU, 0)`.
- `Documentation/core-api/workqueue.rst`: "BH workqueues can be considered a convenience interface to softirq … BH work items cannot sleep."
- tasklet 쪽: `include/linux/interrupt.h:667` 주석이 "This API is deprecated. Please consider using threaded IRQs instead". 다만 tasklet 코드는 6.18 에도 그대로 있고 이 레슨에서 실제로 동작했다. 도입 시점은 "v6.9" 라고 알려져 있으나 이 tarball 에 git 이력이 없어 직접 확인하진 못했다.

그리고 실행 결과로도 확인된다: ftrace 샘플에서 `wq_bh` 는 `softirq_entry: vec=6 [action=TASKLET]` 다음에 `workqueue_execute_start` 가 나온다 (아래 Part C).

### Part B — sleep 가능 여부: 합법과 불법을 둘 다 본다

**합법 쪽.** `check.sh` 는 `sleep_us=200` 을 설정한다. `bhc_body` 는 `ctx & CTX_CAN_SLEEP` 일 때만 `usleep_range(200, 250)` 을 부른다. `wq_sys`, `wq_hi`, `threaded` 가 K 번씩 잠들었는데 dmesg 에 아무 것도 없다 (`CHECK dmesg_clean PASS`). BH work 와 tasklet 은 `CTX_CAN_SLEEP` 가 꺼져 있어 sleep 경로로 들어가지 않는다.

**불법 쪽.** `check.sh solution splat` 은 `bad_tasklet_sleep=1` 로 올린 모듈의 tasklet 에서 `msleep(1)` 을 부른다. `/var/lab/out/Image-dbg` 에서 받은 실제 콘솔 (loglevel=7, 일부 생략):

```text
[    3.963390] bh_cmp: ready irq=22
[    3.992870] BUG: scheduling while atomic: fire/93/0x00000102
[    3.992940] 4 locks held by fire/93:
[    3.992962]  #0: ffff00000623b428 (sb_writers#5){.+.+}-{0:0}, at: vfs_write+0x2f0/0x390
…
[    3.994013] CPU: 2 UID: 0 PID: 93 Comm: fire Tainted: G           O        6.18.55-dbg #1 PREEMPT
[    3.994189] Call trace:
…
[    3.994347]  dump_stack+0x18/0x24
[    3.994356]  __schedule_bug+0x74/0xa0
[    3.994365]  __schedule+0xdac/0x1088
[    3.994377]  schedule+0x48/0x140
[    3.994387]  schedule_timeout+0x94/0x140
[    3.994395]  msleep+0x50/0x80
[    3.994405]  bhc_body+0x170/0x180 [solution]
[    3.994557]  bhc_tasklet_fn+0x1c/0x30 [solution]
[    3.994570]  tasklet_action_common+0x140/0x1c0
…
[    3.994624]  do_softirq_own_stack+0x24/0x50
…
[    4.001027] Unable to handle kernel paging request at virtual address 2d83113efd88e508
[    4.001819] Internal error: Oops: 0000000096000044 [#1]  SMP
[    4.003045] pc : __run_timer_base+0x294/0x3e0
…
[    4.005948] Kernel panic - not syncing: Oops: Fatal exception in interrupt
```

읽는 법:

- `fire/93/0x00000102`: 프로세스 `fire` 위에서 preempt_count = 0x102. `SOFTIRQ_OFFSET` 은 `1 << 8` = 0x100 이고 (preempt.h), 낮은 바이트 2 는 preempt 비활성 중첩이다. 0 이 아니라서 `schedule()` 호출이 불법이다.
- **먼저 잡은 것은 스케줄러 쪽 검사다.** 소스 확인: `kernel/time/sleep_timeout.c` 의 `msleep()` 에는 `might_sleep()` 이 없어서, `DEBUG_ATOMIC_SLEEP` 의 "sleeping function called from invalid context" 대신 `__schedule_bug` 의 메시지가 나왔다. `mutex_lock`, `kmalloc(GFP_KERNEL)` 처럼 `might_sleep()` 을 부르는 호출이었으면 다른 메시지였을 것이다 (ELX-M04 참고).
- **그 다음이 더 무섭다: 바로 Oops.** BUG 메시지 다음, 타이머 소프트IRQ 안(`__run_timer_base`)에서 임의 주소 접근으로 패닉이 났다 (붙인 로그는 BUG 와 같은 CPU 2). `schedule_timeout()` 은 **스택 위에** `struct process_timer` 를 두고 (`sleep_timeout.c:63`, `timer_setup_on_stack`), softirq 는 IRQ 스택에서 돌았다 (콜트레이스의 `do_softirq_own_stack`). schedule() 로 CPU 를 내주면 그 IRQ 스택 위의 타이머가 다음 인터럽트에 덮어써지고, 깨진 타이머를 타이머 코드가 건드렸다고 **추정** 한다 (레지스터의 `dead000000000122` 는 리스트 poison 값이다). 소스와 콜트레이스로 그럴듯하지만 이 경로를 단계별로 증명하지는 않았다.
- 정리: `scheduling while atomic` 한 번으로 끝난 게 아니라 시스템이 죽었다. 실제 제품에서는 "가끔 패닉" 이다. 반드시 `CTX_CAN_SLEEP` 없는 곳에서는 절대 sleep 하지 않는다.

### Part C — IRQ→처리 지연: 두 방법의 측정

**방법 1 (내부)**: 하드 핸들러의 `t0 = ktime_get_ns()` 와 bottom half 시작의 `t1 = ktime_get_ns()` 의 차이. 모듈 `stats` 가 메커니즘별 샘플을 정렬해 p50/p99/max (ns) 를 낸다.

**방법 2 (ftrace)**: tracepoint 로 같은 구간을 외부에서 잰다. 게스트에서 `/sys/kernel/tracing` 을 마운트해 `irq_handler_entry`, `softirq_entry` (vec==6 필터), `workqueue_execute_start`, `sched_switch` (next_comm ~ `irq/*bh_cmp` 필터)를 켜고, `trace_clock` 은 `mono` 로 두어 `ktime_get_ns()` 와 같은 시계를 쓴다. `parse_trace.awk` 가 메커니즘별로 짝을 맞춘다. BusyBox awk 라 게스트 안에서 바로 돈다.

<!-- code: parse_trace.awk -->

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ftrace 로그에서 지연 구간 짝 맞추기 (parse_trace.awk)</text>
  <rect x="10" y="32" width="350" height="26" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="50" fill="currentColor" font-family="monospace">irq_handler_entry: name=bh_cmp   = t0</text>
  <path d="M185 58 L185 76" stroke="currentColor" stroke-width="1.5"/>
  <rect x="10" y="78" width="350" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="96" fill="currentColor" font-family="monospace">tasklet: softirq_entry vec=6 [TASKLET]</text>
  <rect x="10" y="108" width="350" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="126" fill="currentColor" font-family="monospace">wq_bh  : workqueue_execute_start</text>
  <rect x="10" y="138" width="350" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="156" fill="currentColor" font-family="monospace">wq_*   : workqueue_execute_start</text>
  <rect x="10" y="168" width="350" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="186" fill="currentColor" font-family="monospace">thread : sched_switch next=irq/22-bh_cmp</text>
  <text x="380" y="50" fill="currentColor">텍스트 ftrace 타임스탬프 = 1 µs 단위 (trace_clock mono)</text>
  <text x="380" y="72" fill="currentColor">tasklet/wq_bh: 같은 CPU 안에서 짝 (softirq 는 그 CPU)</text>
  <text x="380" y="94" fill="currentColor">wq/thread: 다음 이벤트 (다른 CPU 가능)</text>
  <text x="380" y="116" fill="currentColor">fire 가 edge 를 하나씩 쏴서 겹치지 않는다</text>
  <text x="380" y="138" fill="currentColor">단, threaded 는 thread 가 이미 돌고 있으면</text>
  <text x="380" y="154" fill="currentColor">sched_switch 가 없어 n이 줄어든다 (아래 293)</text>
  <text x="10" y="230" fill="currentColor">내부 ktime과 ftrace가 같은 경향을 보이면, 측정 자체를 믿을 수 있다는 교차 검증이 된다.</text>
</svg>
```

실제 trace 줄 (`d.h1.` 의 `h` = hardirq, `..s1.` 의 `s` = softirq; ftrace 문서의 `hardirq/softirq` 열):

```text
            fire-102     [003] d.h1.     6.578732: irq_handler_entry: irq=22 name=bh_cmp
            fire-102     [003] ..s1.     6.579242: softirq_entry: vec=6 [action=TASKLET]
            fire-108     [003] d.h1.     6.187174: irq_handler_entry: irq=22 name=bh_cmp
     kworker/3:0-33      [003] .....     6.187535: workqueue_execute_start: work struct 00000000e7b378ad: function bhc_work_fn [solution]
            fire-118     [000] d.h1.     9.739410: irq_handler_entry: irq=22 name=bh_cmp
          <idle>-0       [003] d..2.     9.740221: sched_switch: prev_comm=swapper/3 prev_pid=0 prev_prio=120 prev_state=R ==> next_comm=irq/22-bh_cmp next_pid=90 next_prio=49
            fire-123     [001] d.h1.    11.301218: irq_handler_entry: irq=22 name=bh_cmp
            fire-123     [001] ..s1.    11.301458: softirq_entry: vec=6 [action=TASKLET]
            fire-123     [001] ..s1.    11.301923: workqueue_execute_start: work struct 00000000987111e2: function bhc_work_fn [solution]
```

네 번째 그룹이 BH workqueue 다: softirq 안에서(`..s1.`) workqueue 이벤트가 찍힌다. threaded IRQ 는 하드 IRQ 가 CPU 0, thread 는 CPU 3 에서 돌았다(`[003]`). `next_prio=49` 는 ftrace 표기이고, 게스트 `/proc/<pid>/stat` 에서 실제 값을 읽었다: `irq-thread comm=(irq/22-bh_cmp) prio=-51 nice=0 rt_priority=50 policy=1` (policy 1 = SCHED_FIFO).

**결과 표 (실제 숫자, 단위 µs, 6.18.55-dbg, QEMU TCG cortex-a57 -smp 4, K=300, gpio-sim 1 edge씩 handshake)**. 게스트 안 부하는 표의 idle/load 조건이고, 게스트 바깥 VM 부하는 `uptime` load average 4.76 (시작) → 4.56 (끝): 다른 작업들이 VM 을 공유 중이다. 머신: Apple M6 (`sysctl -n` perflevel0/1/2 = 2/4/6; Mac `uptime` load 17.39), `cc --version` 1행 = Apple clang 21.0.0, VM = `uname -r` 7.0.0-38-generic, `nproc` 6, gcc 15.2.0. TCG 숫자이므로 순서만 믿는다.

| 메커니즘 | 컨텍스트 | sleep? | idle 내부 p50/p99/max | idle ftrace p50/p99/max | load 내부 p50/p99/max | load ftrace p50/p99/max |
|---|---|---|---|---|---|---|
| tasklet | softirq | 불가 | 26 / 274 / 572 | 20 / 388 / 565 | 45 / 288 / 352 | 38 / 277 / 369 |
| WQ_BH | softirq | 불가 | 114 / 716 / 1310 | 111 / 764 / 1309 | 101 / 551 / 616 | 98 / 551 / 605 |
| wq_sys | task | 가능 | 209 / 2243 / 2942 | 197 / 2173 / 2662 | 257 / 1006 / 1250 | 247 / 1002 / 1240 |
| wq_hi (WQ_HIGHPRI) | task | 가능 | 282 / 2716 / 8373 | 279 / 2716 / 8358 | 225 / 3663 / 8448 | 223 / 3641 / 8356 |
| threaded IRQ | task | 가능 | 262 / 2421 / 5722 | 241 / 2386 / 5684 | 163 / 580 / 800 | 143 / 458 / 522 |

**세 번 돌린 p50 편차 (내부 ktime, µs, 러닝마다 값이 크게 다르다)**: run 2, 3 의 부하 지표 `uptime` 은 4.56 → 4.62, 4.33 → 6.79.

| 메커니즘 | idle p50 (run1 / run2 / run3) | load p50 (run1 / run2 / run3) |
|---|---|---|
| tasklet | 26 / 58 / 48 | 45 / 50 / 33 |
| WQ_BH | 114 / 110 / 100 | 101 / 110 / 83 |
| wq_sys | 209 / 354 / 206 | 257 / 265 / 201 |
| wq_hi | 282 / 311 / 137 | 225 / 264 / 175 |
| threaded | 262 / 315 / 239 | 163 / 225 / 769 |

읽는 법 — 믿을 수 있는 것과 믿으면 안 되는 것:

1. **믿어도 되는 것**: 세 run 모두 `tasklet`, `WQ_BH` (softirq) 의 p50 이 task 컨텍스트 쪽보다 작다. 가장 좁은 경우가 run 3 idle 의 `WQ_BH` 100 vs `wq_hi` 137, 가장 넓은 경우는 tasklet 쪽 몇 배 이상. softirq 는 하드 IRQ 복귀 직후 같은 CPU 에서 돌고, task 쪽은 스케줄러를 거쳐 다른 스레드로 넘어가기 때문이다. 내부 ktime 과 ftrace 의 p50 이 거의 일치하는 것도 측정이 믿을 만하다는 근거다 (예: `wq_hi` 282 vs 279, `tasklet` 26 vs 20).
2. **믿으면 안 되는 것**: `wq_sys` vs `wq_hi` vs `threaded` 사이의 순서는 run 마다 뒤집힌다 (`wq_hi` idle p50 이 137 ~ 311). 이 VM 은 다른 QEMU 들과 CPU 를 공유하고, TCG 는 게스트의 짧은 지연을 재현하지 못한다. "WQ_HIGHPRI 가 일반 workqueue 보다 빠르다", "threaded FIFO 가 workqueue 보다 빠르다" 는 실제 하드웨어 부하 상황의 이야기지, 이 표가 보여 주는 것이 아니다. 진짜 RT 지연을 재려면 `Image-rt` + 실제 하드웨어 또는 `cyclictest`/rtla 가 필요하다.
3. `WQ_BH` 가 tasklet 보다 p50 이 큰 것(세 run 모두)은 **tasklet 위에서 도는 일반 work 처리 경로** 때문으로 보인다 (work 큐 처리 추가 코드). 측정 잡음을 넘는 것 같지만 "softirq 그룹 안에서의 순서" 정도로 읽어야 한다.
4. 이 실험의 CPU 부하는 busy loop 4개라 CFS 스레드끼리의 경쟁이다. threaded IRQ 가 SCHED_FIFO 라 이론상 유리한데, TCG 에서는 p99 가 run 마다 들쭉날쭉하다. **트러블슈팅 습관**: 숫자 표를 그대로 복사해 "A 가 B 보다 2배 빠르다" 고 말하지 말 것.
5. run 3 의 threaded 부하 ftrace 는 `n=293` 이었다 (300 이 아님). `sched_switch` 는 이미 도는 thread 에는 안 찍히므로, thread 가 복귀 전에 다음 edge 가 오면 짝이 줄어든다. 내부 ktime 은 300 전부 찍힌다.

왜 이런 차이가 나는지 실행 경로로 보면: tasklet/`WQ_BH` 는 하드 IRQ 복귀 직전 `__irq_exit_rcu` → `handle_softirqs` 로 **같은 CPU, 같은 프로세스 컨텍스트에서** 곧바로 돈다 (Part B 콜트레이스와 `comm=fire` 가 그 증거). workqueue 는 `queue_work` 가 워커를 깨우고 스케줄러가 kworker 를 올려야 하고, threaded IRQ 는 irq 스레드를 깨운다 (여기서는 다른 CPU 의 `<idle>` 에서 전환됐다). 그래서 지연이 스케줄러 왕복 하나만큼 더 든다. 대신 스케줄러를 거치기 때문에 sleep 이 합법이다 — 이것이 첫 그림의 교환이다.

**판단 가이드 (면접 체크 "hard IRQ / threaded IRQ / workqueue 중 무엇을 고르나")**

| 할 일 | 고를 것 | 이유 |
|---|---|---|
| 레지스터 클리어, 타임스탬프, 큐에 넣기 | **hard IRQ** | 가장 짧고 빠르다. sleep 불가, 인터럽트 끈 채 오래 돌면 다른 IRQ 지연. |
| sleep 없이 짧은 후처리, per-CPU, 저지연 | **BH (`WQ_BH` / `system_bh_wq`)** | tasklet 의 현대판. softirq 컨텍스트, sleep 불가. 새 코드에서 tasklet 대신. |
| I2C/SPI 버스 접근, mutex, GFP_KERNEL | **threaded IRQ** | 그 IRQ 전용 스레드, RT 우선순위, sleep 가능. "이 인터럽트의 처리" 에 가장 맞다. |
| 오래 걸리거나 드물고 IRQ 와 무관한 일 | **workqueue** (`system_percpu_wq`, `WQ_UNBOUND`) | 공유 워커 풀. sleep 가능. 우선순위 민감하면 `WQ_HIGHPRI`, 메모리 회수 경로면 `WQ_MEM_RECLAIM`. |
| 기존 코드의 tasklet | 유지 또는 BH work 로 이전 | 소스가 deprecated 라고 적는다. |

요약 한 줄: **Sleep 이 필요한가?** 필요 없으면 softirq 계열, 필요하면 task 계열. Task 계열 안에서 **"이 IRQ 의 처리"** 면 threaded IRQ, 그 외 나중에 할 일은 workqueue.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

정답 실행 결과 (실제 출력, K=40 세 번 연속 PASS. 일부 생략):

```text
CHECK gpio_sim_setup PASS
CHECK insmod PASS
CHECK tasklet_handled_all PASS
CHECK tasklet_count_eq_K PASS
  tasklet: n=40 ctx=SOFT stable=1 comm=fire
CHECK tasklet_context PASS
…
  /proc/interrupts delta=200 expected=200
CHECK proc_interrupts_exact PASS
CHECK hard_irq_count_exact PASS
CHECK hard_handler_context PASS
CHECK irq_thread_exists PASS
CHECK rmmod PASS
CHECK dmesg_clean PASS
FAILS=0
GUEST-END
VERDICT: PASS (23/23 checks)
```

`/proc/interrupts` 합계가 시도한 edge 수와 정확히 같다. `fire` 가 edge 하나마다 `done` 카운터가 오르길 기다렸다가 다음을 쏘기 때문에 (handshake) 통합(coalescing)이 일어나지 않아 결정적으로 판정할 수 있다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| tasklet 에서 `msleep()` / `mutex_lock()` / `GFP_KERNEL` | softirq 컨텍스트는 sleep 불가. 실측: `BUG: scheduling while atomic` 에 이어 Oops, 패닉 | sleep 필요하면 workqueue 또는 threaded IRQ |
| 하드 핸들러에서 `queue_work()` 를 부르고 반환값을 무시한 채 "이벤트마다 한 번씩 돈다" 고 가정 | 이미 pending 이면 `queue_work` 는 false 를 돌려주고 합쳐진다. 추가 연습 2 에서 측정 | 이벤트별 데이터는 큐/링에 쌓고, bottom half 가 전부 비운다 |
| 메커니즘마다 `flush_workqueue()` 만 믿고 해제 | 그 큐에 없는 work (시스템 큐, BH 큐)는 기다리지 않고, tasklet 은 `flush` 로 잡히지 않는다 | 먼저 `free_irq()`, 그다음 `tasklet_kill()` + `cancel_work_sync()` + `destroy_workqueue()` |
| `free_irq()` 전에 `cancel_work_sync()` | 해제 중에 IRQ 가 다시 work 를 큐에 넣는다 (경쟁) | **먼저 IRQ 끄기** |
| 시스템 큐에 오래 도는 일을 넣음 | 공유 풀의 다른 work 를 막아 시스템 전체 지연을 만든다 | 전용 `alloc_workqueue` 또는 `system_long_wq` |
| `in_interrupt()` 로 "sleep 해도 되나" 판단 | `in_interrupt()` 는 BH 비활성(`local_bh_disable`) 구간도 참이고, 스핀락을 잡은 task 컨텍스트는 거짓이다. 또 CONFIG_PREEMPT_COUNT 없는 커널에서는 `preemptible()` 도 항상 0 | 컨텍스트로 분기하는 코드를 쓰지 말고 호출자별로 함수를 분리. 디버그에 `might_sleep()` |
| `WQ_BH` 큐를 `max_active` 지정해서 만들기 | `alloc_workqueue` 가 `WARN_ON_ONCE(max_active)` 로 거부 | `alloc_workqueue("x", WQ_BH \| WQ_PERCPU, 0)` |
| gpio-sim 라인을 request 없이 IRQ 소스로 사용 | `gpio_sim_apply_pull` 은 `request_map` 비트가 켜진 라인에서만 IRQ 를 낸다. 실제로 첫 시도에서 카운트 0 이었다 | `gpiochip_request_own_desc(...)` 로 라인을 요청 |

## 8. 면접 Q&A

<details><summary>Q1. hard IRQ, threaded IRQ, workqueue 중 무엇을 고르나요?</summary>

**답**: 일이 sleep 해야 하는가부터 본다. 레지스터 읽고 ack 하고 타임스탬프 찍고 큐에 넣는 정도는 하드 IRQ 에서 끝낸다. sleep 없이 조금 더 처리해야 하면 softirq 계열(지금이라면 BH work), I2C 같은 버스 접근이나 mutex 가 필요하면 그 IRQ 의 threaded 핸들러, 인터럽트와 느슨하게 연결된 큰 일은 workqueue. 지연은 일반적으로 하드 < softirq < task 스케줄 대상 순서이고, 이 레슨 표에서도 softirq 계열이 task 계열보다 p50 이 몇 배 작았다 (TCG 숫자라 순서만).

꼬리질문 **"threaded IRQ 와 workqueue 는 뭐가 다르죠?"** — threaded IRQ 는 그 IRQ 전용 스레드(`irq/NN-이름`, SCHED_FIFO 50)이고 핸들러가 끝날 때까지 그 IRQ 처리와 묶여 있다. workqueue 는 공유 워커 풀에서 도는 비동기 작업이고 IRQ 와 무관하게 언제 어디서든 쌓을 수 있다.
</details>

<details><summary>Q2. tasklet 에서 왜 sleep 하면 안 되나요?</summary>

**답**: tasklet 은 softirq 안에서 돌고, `preempt_count` 에 `SOFTIRQ_OFFSET` 이 켜져 있어 스케줄러가 그 문맥을 다른 태스크로 바꿀 수 없다. 이 레슨에서 실제로 `msleep(1)` 을 부르니 `BUG: scheduling while atomic: fire/93/0x00000102` 이 나왔고(0x100 비트가 softirq), 바로 뒤에 타이머 softirq 에서 Oops 로 패닉이 났다. `0x100` 은 "일을 처리 중인 softirq 안에 있다" 는 뜻이다.

꼬리질문 **"왜 Oops 까지 갔죠?"** — `schedule_timeout` 은 스택 위에 타이머를 두는데, softirq 는 IRQ 스택(`do_softirq_own_stack`)에서 돌았다. 스케줄이 일어나 그 스택이 재사용되면 타이머가 깨졌다고 추정할 수 있다 (콜트레이스와 소스 기반의 추정).
</details>

<details><summary>Q3. in_interrupt() 와 in_hardirq() 와 in_serving_softirq() 는 어떻게 다른가요?</summary>

**답**: `in_hardirq()` 는 하드 IRQ 핸들러 안, `in_serving_softirq()` 는 softirq 처리 중. `in_interrupt()` 는 넓은 의미로 hardirq 또는 NMI 또는 softirq **또는 BH 비활성 구간**까지 포함하기 때문에 "지금 인터럽트를 처리 중이다" 와 "BH 가 잠겨 있다" 를 구분하지 못한다. 그래서 새 코드에는 `in_task()` / `in_serving_softirq()` / `in_hardirq()` 처럼 구체적인 것을 쓰고, 애초에 컨텍스트로 분기하지 않도록 함수를 나눈다.

꼬리질문 **"`preemptible()` 은 믿을 수 있나요?"** — CONFIG_PREEMPT_COUNT 켜진 커널에서만 의미가 있다. 꺼져 있으면 preempt.h 의 `#define preemptible() 0` 쪽이 선택된다. 이 커널은 PREEMPT=y.
</details>

<details><summary>Q4. WQ_BH 는 tasklet 과 뭐가 같고 뭐가 다른가요?</summary>

**답**: 같은 softirq 컨텍스트에서 돌아 sleep 불가이고, 지연도 task 계열보다 작은 쪽이다 (이 레슨 표에서 `WQ_BH` p50 은 tasklet 의 약 2~4배, TCG 잡음 포함. `tasklet_action()` 안에서 `workqueue_softirq_action()` 이 돈다, 소스로 확인). 다른 점은 work API 전체를 쓴다는 것: `queue_work`, `flush`, `cancel_work_sync`, delayed work, 그리고 `disable_work_sync` / `enable_work` 로 tasklet_disable 을 대체한다. 추가 연습에서 `disable_work_sync` + `enable_and_queue_work` 가 실제로 동작하는 걸 확인했다. 소스의 tasklet 주석은 deprecated 라고 적고 있다.

꼬리질문 **"왜 tasklet 을 없애려고 하나요?"** — 이 레슨에서 직접 다룬 범위가 아니지만, 실행 컨텍스트와 플러시/취소 API 가 work 쪽에 비해 빈약하다는 것이 자주 언급되는 이유다. 깊은 근거는 확인하지 못했으니 "소스에 deprecated 로 적혀 있다" 까지만 확정이다.
</details>

<details><summary>Q5. 하드 IRQ 핸들러 하나에 큰 일을 다 넣으면 왜 안 되나요?</summary>

**답**: 핸들러가 돌 동안 해당 CPU 는 다른 IRQ 를 못 받거나(그 라인은 마스크되고, 레벨에 따라 CPU 차원 지연), 시스템 전체의 인터럽트 지연이 늘어난다. 또 sleep 불가라 mutex, GFP_KERNEL 이 안 되고 버스 접근 같은 흔한 작업을 할 수 없다. 그래서 하드 핸들러는 ack + 최소한의 기록 후 bottom half 로 넘긴다.

꼬리질문 **"bottom half 도 CPU 를 먹는데 왜 낫나요?"** — softirq 는 인터럽트가 켜진 상태로 처리되고, task 계열은 preempt 되며 우선순위를 줄 수 있다. 실행 자체는 줄지 않아도 "다른 인터럽트를 막는 시간" 이 준다.
</details>

<details><summary>Q6. workqueue 를 쓰는 드라이버를 안전하게 내리려면?</summary>

**답**: 새로운 일이 못 들어오게 **IRQ 를 먼저 끈다** (`free_irq` 는 실행 중인 핸들러와 threaded 핸들러의 끝까지 기다린다). 그다음 `cancel_work_sync` 로 대기·실행 중인 work 를 끝내고, 전용 큐면 `destroy_workqueue`. 추가 연습 4 에서 실행 중인 work 에 `cancel_work_sync` 를 걸면 끝날 때까지 기다리는 것을 확인한다.

꼬리질문 **"`flush_work` 와 `cancel_work_sync` 는 어떻게 다른가요?"** — `flush_work` 는 대기 중인 work 를 끝까지 **실행시키고** 기다린다. `cancel_work_sync` 는 pending 이면 **실행하지 않고 취소** 하고, 이미 실행 중이면 끝날 때까지 기다린다.
</details>

<details><summary>Q7. IRQ→처리 지연을 어떻게 측정하세요? 어떤 점을 조심하나요?</summary>

**답**: 하드 핸들러에서 타임스탬프를 찍고 처리 시작에서 다시 찍어 분포(p50/p99/max)를 낸다. 교차 검증으로 ftrace 의 `irq_handler_entry` 와 `softirq_entry`/`workqueue_execute_start`/`sched_switch` 를 파싱한다. 평균이 아니라 분포를 보는 이유는 꼬리 지연이 문제이기 때문이다. 시뮬레이션 또는 에뮬레이션에서는 절대값을 믿지 말고 상대 순서만 본다.

꼬리질문 **"측정이 대상에 영향을 주지 않나요?"** — 준다. ftrace 활성화 자체가 비용이고, 이 레슨의 `fire` 도 edge 마다 `done` 을 폴링한다. 실제 제품 측정은 GPIO 토글 + 로직 분석기가 일반적이다.
</details>

<details><summary>Q8. PREEMPT_RT 커널에서는 이 표가 어떻게 바뀌나요?</summary>

**답**: 이 레슨에서 RT 커널로 돌려 보지는 않았다. 일반적으로 알려진 내용으로는 RT 에서 대부분의 하드 IRQ 핸들러가 강제 threaded 되고 softirq 도 스레드 컨텍스트에서 돌아, "softirq 는 sleep 불가" 같은 전제가 RT 의 락 동작과 함께 달라진다. 구체 동작은 커널 설정과 버전에 따라 다르니 실측이 필요하다 (`/var/lab/out/Image-rt` 가 있다).

꼬리질문 **"RT 라도 IRQF_NO_THREAD 는 왜 있나요?"** — 타이머 같은 일부 하드 IRQ 는 스레드로 내리면 안 되는 경우가 있어 그렇게 표시된다 (ELX-M05 범위).
</details>

## 9. 추가 연습

`practice.c` 는 IRQ 없이 `module_init` 안의 프로세스 컨텍스트에서 API 의미를 결정적으로 검사한다.

### 연습 1 — tasklet 의 합침과 자기 재스케줄

`tasklet_disable` 해 놓고 `tasklet_schedule` 을 5번 부르면 몇 번 실행될까? tasklet 안에서 자기 자신을 다시 schedule 하면?

<details><summary>답</summary>

`tasklet_schedule` 5번 → 실행 1번 (`p1_coalesced_once PASS`). 스케줄 상태 비트가 이미 서 있으면 더 쌓이지 않는다. 그래서 "이벤트 N개 = 실행 N번" 을 tasklet 에 기대하면 안 된다. 카운터나 링으로 이벤트를 쌓아야 한다. tasklet 안에서 다시 schedule 하면 현재 실행이 끝난 뒤 또 돈다 (`p1b_exactly_three PASS`, 재스케줄을 2번 해 3번 실행).

</details>

### 연습 2 — queue_work 의 true/false 의미

전용 순서형(ordered) workqueue 에서: 처음 `queue_work` → ?, 실행 중일 때 다시 `queue_work` → ?, 그 직후 또 `queue_work` → ?, 최종 실행 횟수는?

<details><summary>답</summary>

첫 호출 `true` (pending 진입). 실행 시작 후 work 는 pending 이 아니므로 다시 queue 하면 `true` (실행 중 재큐, 실행이 끝나면 또 돈다). 그 직후 한 번 더는 이미 pending 이라 `false`. 총 2번 실행 (`p2_exactly_two_runs PASS`). queue_work 의 반환값이 "새로 걸었는가" 임을 이벤트 드롭 감지에 쓸 수 있다.

</details>

### 연습 3 — BH work 로 tasklet_disable 대체

`disable_work_sync()` 로 BH work 를 막으면 `queue_work(system_bh_wq, ...)` 는 뭘 돌려주나? `enable_and_queue_work()` 후엔 실행 컨텍스트는?

<details><summary>답</summary>

`disable_work_sync` 는 `false` (이전에 대기 중이 아님). 막은 동안 `queue_work` 는 `false` 이고 실행되지 않는다 (`p3_disabled_queue_false`, `p3_no_run_while_disabled`). `enable_and_queue_work` 는 `true`, 1번 실행, `in_serving_softirq()` 참 (`p3_softirq_context PASS`). 6.18 에서 work API 로 tasklet_disable 패턴을 대체할 수 있다는 근거다.

</details>

### 연습 4 — cancel_work_sync 는 실행 중인 work 를 기다린다

`msleep(100)` 하는 work 가 도는 도중 `cancel_work_sync` 를 부르면?

<details><summary>답</summary>

대기 중이 아니었으므로 반환값은 `false` 이지만, 끝날 때까지 기다린 뒤 돌아온다 (`p4_finished_before_cancel_returned PASS`). 그래서 모듈 해제 경로에서 `cancel_work_sync` 다음에 자원을 해제해도 안전하다. 단, cancel 뒤에 다른 경로가 work 를 다시 큐에 넣지 않도록 IRQ 를 먼저 꺼야 한다.

</details>

### 연습 5 — 설계 판단 (코드 없음)

STM32MP1 같은 Cortex-A 리눅스 게이트웨이를 상상하자: CAN 수신 IRQ 는 20 µs 안에 메일박스를 비워야 하고, 수신 프레임을 I2C EEPROM 에 로그로 저장해야 한다. 각 단계를 어디에 넣을까?

<details><summary>답</summary>

1. 메일박스 비우기, 타임스탬프, 링에 넣기 = **하드 IRQ**.
2. 링에서 꺼내서 프레임 파싱 = sleep 불필요하면 softirq 계열(BH work), 다만 일이 커지면 threaded IRQ.
3. EEPROM I2C 쓰기 = I2C 는 잠드니 **threaded IRQ 또는 workqueue**. 로그는 IRQ 와 느슨하게 연결되므로 보통 workqueue 쪽이 맞고, 적체 시 드롭 정책이 필수다.
메일박스 비우기를 bottom half 로 미루면 20 µs 제약을 못 지킨다. RTOS 의 deferred ISR (RTOS-B10) 과 구조가 같다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc '/Users/donh/.../lessons/ELX/ELX-M06/check.sh practice'
```

실제 결과:

```text
[    5.032030] PRACTICE RESULT pass=20 fail=0
CHECK practice_all PASS
CHECK practice_rmmod PASS
CHECK dmesg_clean PASS
VERDICT: PASS (4/4 checks)
```

## 10. 핵심 정리

- 선택의 축은 두 개다: **sleep 필요?** 와 **지연**. 하드 IRQ > softirq 계열(tasklet, WQ_BH) > task 계열(workqueue, threaded IRQ) 순으로 제약이 풀리고 지연이 는다.
- `in_hardirq()` / `in_serving_softirq()` / `in_task()` / `preemptible()` 로 실행 컨텍스트를 코드에서 직접 볼 수 있다. softirq 의 `current` 는 끼어든 프로세스다.
- tasklet 에서 sleep 하면 `BUG: scheduling while atomic` + Oops + 패닉까지 간다 (실측). `msleep` 에는 `might_sleep()` 이 없어서 먼저 잡는 것은 스케줄러다.
- **tasklet 은 소스 주석에서 deprecated** 이고 대안은 threaded IRQ 또는 `WQ_BH` (`system_bh_wq`). BH work 는 tasklet softirq 벡터 안에서 돈다.
- `queue_work` 는 pending 이면 합쳐진다 (`false` 반환). `tasklet_schedule` 도 합쳐진다. 이벤트 수를 세려면 카운터/링이 필요하다.
- 해제 순서는 항상 **`free_irq` 먼저**, 그다음 `tasklet_kill` / `cancel_work_sync` / `destroy_workqueue`.
- 지연 측정은 내부 타임스탬프와 ftrace 파싱으로 교차 검증하고, TCG 숫자의 절대값은 믿지 않는다 (순서만).

## 참고자료

- Linux 소스 `/var/lab/src/linux-6.18.55`: `include/linux/interrupt.h` (tasklet, deprecated 주석), `kernel/softirq.c` (`tasklet_action`, `workqueue_softirq_action` 호출), `kernel/workqueue.c`, `include/linux/workqueue.h` (`WQ_BH`, `__WQ_BH_ALLOWS`), `include/linux/preempt.h`, `kernel/time/sleep_timeout.c`, `drivers/gpio/gpio-sim.c`.
- Linux 문서: `Documentation/core-api/workqueue.rst` (WQ_BH, BH workqueues), `Documentation/trace/ftrace.rst` (trace 출력 형식).
- 관련 레슨: ELX-M04 (락 선택), ELX-M05 (threaded IRQ 기초), RTOS-B10 / RTOS-M03 (RTOS deferred ISR), OS-M07 (spinlock).
