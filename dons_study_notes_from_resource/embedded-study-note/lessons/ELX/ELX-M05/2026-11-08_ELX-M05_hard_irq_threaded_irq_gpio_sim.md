---
id: ELX-M05
time: 90분
updated: 2026-11-08
---
# ELX-M05 인터럽트 처리: hard IRQ와 threaded IRQ

## 학습 목표

- gpio-sim 라인을 `gpiod_to_irq` 로 진짜 IRQ 로 만들고, `request_threaded_irq` 로 **hard 핸들러(타임스탬프만) + 스레드 핸들러(처리, sleep 가능)** 를 나눠 붙인다.
- "edge K 개 = /proc/interrupts K = hard K = thread K" 를 sysfs 로 노출해 **정확히** 맞추고, 스레드 핸들러가 깨어나는 횟수가 왜 edge 수와 다를 수 있는지(`IRQTF_RUNTHREAD` 합치기)를 실제 카운터로 보인다.
- `IRQF_ONESHOT` 없이 `handler == NULL` 로 요청하면 왜 `-EINVAL` 인지 실제 커널 메시지로 확인하고, ONESHOT 이 실제로 무엇을 막아 주는지(레벨 트리거 폭풍) 소스 근거로 설명한다.
- `ps` / `/proc/<pid>/stat` 로 `irq/NN-이름` 스레드의 SCHED_FIFO 우선순위 50 을 확인하고, hard→thread 지연 분포를 부하별로 재서 "FIFO 50 이 왜 만능이 아닌지" 를 말한다.
- hard 핸들러에서 sleep 하면 **실제로** 무슨 일이 일어나는지(`scheduling while atomic`, 그 CPU 의 RCU 스톨)를 본 적이 있다.

## 1. 문제

임베디드 펌웨어 면접에서 "인터럽트에서 무엇을 하고 무엇을 미루나요?" 는 거의 항상 나오고, 실제로 I2C/SPI 뒤에 있는 센서·GPIO 익스팬더 드라이버를 짜 보면 답은 정해져 있다: **top-half 에서는 하드웨어를 조용히 시키고 시각만 찍고, 버스를 타는 일은 sleep 가능한 곳으로 미룬다.** STM32 HAL 의 EXTI 콜백에서 플래그만 세우고 메인 루프에서 처리하던 것(C-M14, RTOS-B10 의 deferred ISR 패턴)이 Linux 에서는 `request_threaded_irq` 다.

이 레슨은 실제 커널(6.18.55-dbg, QEMU TCG cortex-a57 `-smp 4`)에서 전부 돌린다. 버스도 칩도 없지만 `CONFIG_GPIO_SIM=y` 덕분에 **진짜 irq_desc·진짜 irq_thread** 가 움직인다. 라인은 sysfs `pull` 속성에 쓰는 것으로 흔든다. VM 자체 커널에는 절대 insmod 하지 않고 게스트 안에서만 돈다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 신호 경로 (실제 콜 스택에서 확인한 순서)</text>
  <rect x="10" y="34" width="150" height="50" class="f-mut" stroke="currentColor"/>
  <text x="85" y="55" fill="currentColor" text-anchor="middle">쓰기: pull-up</text>
  <text x="85" y="73" fill="currentColor" text-anchor="middle" font-size="12">sim_gpio0/pull</text>
  <line x1="160" y1="59" x2="196" y2="59" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="198" y="34" width="170" height="50" class="f-mut" stroke="currentColor"/>
  <text x="283" y="55" fill="currentColor" text-anchor="middle">gpio_sim_apply_pull</text>
  <text x="283" y="73" fill="currentColor" text-anchor="middle" font-size="12">chip-&gt;lock (mutex) 잡고 fire</text>
  <line x1="368" y1="59" x2="404" y2="59" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="406" y="34" width="150" height="50" class="f-mut" stroke="currentColor"/>
  <text x="481" y="55" fill="currentColor" text-anchor="middle">irq_work + IPI</text>
  <text x="481" y="73" fill="currentColor" text-anchor="middle" font-size="12">irq_sim_handle_irq</text>
  <line x1="481" y1="84" x2="481" y2="112" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="396" y="114" width="170" height="60" class="f-warn" stroke="currentColor"/>
  <text x="481" y="134" fill="currentColor" text-anchor="middle">handle_simple_irq</text>
  <text x="481" y="151" fill="currentColor" text-anchor="middle">m05_hard (hardirq)</text>
  <text x="481" y="167" fill="currentColor" text-anchor="middle" font-size="12">시각 + kfifo_put + 카운트</text>
  <line x1="396" y1="144" x2="330" y2="144" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="150" y="114" width="178" height="60" class="f-acc" stroke="currentColor"/>
  <text x="239" y="134" fill="currentColor" text-anchor="middle">IRQ_WAKE_THREAD</text>
  <text x="239" y="151" fill="currentColor" text-anchor="middle" font-size="12">test_and_set(RUNTHREAD)</text>
  <text x="239" y="167" fill="currentColor" text-anchor="middle" font-size="12">이미 1 이면 깨움 없음</text>
  <line x1="239" y1="174" x2="239" y2="202" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="120" y="204" width="238" height="60" class="f-ok" stroke="currentColor"/>
  <text x="239" y="224" fill="currentColor" text-anchor="middle">irq/22-elx_m05 (SCHED_FIFO 50)</text>
  <text x="239" y="241" fill="currentColor" text-anchor="middle" font-size="12">m05_thread: kfifo 다 비울 때까지</text>
  <text x="239" y="257" fill="currentColor" text-anchor="middle" font-size="12">usleep (I2C 흉내), 지연 기록</text>
  <line x1="358" y1="234" x2="404" y2="234" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="406" y="204" width="160" height="60" class="f-mut" stroke="currentColor"/>
  <text x="486" y="224" fill="currentColor" text-anchor="middle">sysfs stats / latency</text>
  <text x="486" y="241" fill="currentColor" text-anchor="middle" font-size="12">/proc/interrupts 와 대조</text>
  <text x="10" y="294" fill="currentColor">hard 쪽은 인터럽트가 꺼진 원자 컨텍스트, 스레드 쪽은 일반 태스크 — 그래서 sleep 가능 여부가 갈린다.</text>
  <defs><marker id="a1" markerWidth="8" markerHeight="6" refX="7" refY="3" orient="auto"><path d="M0,0 L8,3 L0,6 z" fill="currentColor"/></marker></defs>
</svg>
```

시험 환경 (이 레슨 전체 공통): Linux 6.18.55-dbg, QEMU TCG `-machine virt -cpu cortex-a57 -smp 4 -m 512M`, gpio-sim 은 빌트인(`CONFIG_GPIO_SIM=y`). `check.sh` 가 VM(`limactl shell elinux`) 안에서 모듈과 헬퍼를 빌드하고, 개인 initramfs 로 게스트를 부팅한 뒤 게스트 출력으로만 판정한다.

### Part A — 신호 경로: gpio-sim 라인 → threaded IRQ → 정확한 카운트

gpio-sim 칩은 게스트의 `guest.sh` 가 configfs 로 만든다(`/sys/kernel/config/gpio-sim/m05/bank0`, line0 이름 `m05-sig`). 모듈은 `gpiod_lookup` 테이블(key = 라인 이름, `U16_MAX`)로 `gpiod_get()` → `gpiod_to_irq()` 까지는 이미 되어 있다. 채울 것:

- `m05_hard(irq, dev_id)` — `ktime_get_ns()` 로 시각을 찍고 이벤트를 `kfifo` 에 넣는다. 이 함수는 원자 컨텍스트다: sleep 금지, 공유 데이터도 원자/kfifo 뿐. `IRQ_WAKE_THREAD` 반환.
- `m05_thread(irq, dev_id)` — 스레드 컨텍스트. fifo 를 **빌 때까지** 비우며, 이벤트마다 hard→thread 지연을 기록하고, `usleep_range()` 로 "느린 버스 트랜잭션" 을 흉내낸다.
- `m05_request_irq(d)` — `request_threaded_irq(irq, hard, thread, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "elx_m05", d)`.

검증(게스트 `m05_test`): 간격을 둔 상승 edge 200 개를 각각 처리될 때까지 기다린 뒤 `/proc/interrupts` 증분 = `hard` = `thread_runs` = `processed` = 200 이어야 한다.

### Part B — 깨움은 카운터가 아니다: edge 가 스레드 실행 중에 몰리면

`__irq_wake_thread()` 는 `test_and_set_bit(IRQTF_RUNTHREAD)` — **깨움은 비트 하나** 다. 스레드가 돌고 있는 동안 edge 가 여러 개 와도 스레드는 한 번만 더 돈다. 그러니 "edge 하나 = 스레드 호출 하나" 라고 가정한 코드는 이벤트를 잃는다. 강제 스케줄로 재현한다(§5 Part B 그림):

- sysfs `gate` 로 스레드를 핸들러 안에 묶어 둔다.
- edge 1 → 스레드 진입 대기 → 나머지 edge 63 개를 hard 카운트로 확인하며 발사.
- `gate` 해제 → 스레드가 kfifo 를 비운다.
- CHECK: `hard=64`, `thread_runs=2`(첫 실행 + 재실행 1번), `processed=64`, `dropped=0`, 그리고 `/proc/interrupts` 증분도 64.

### Part C — 스레드의 정체와 지연 통계

- `/proc` 에서 `comm` 이 `irq/` 로 시작하고 `elx_m05` 를 포함하는 스레드를 찾아 IRQ 번호, 스케줄 정책(`SCHED_FIFO`=1), rt_priority(50)를 확인.
- `latency` sysfs: `n / min / avg / p50 / p99 / max` (ns) + log2 히스토그램. TODO(4) 는 nearest-rank 퍼센타일. 타이밍과 무관하게 `lat_inject` 로 1..100 을 주입해 `p99=99, p50=50` 을 결정적으로 검증한다.

## 2. Problem (English)

Write a Linux kernel module that services a rising edge on a gpio-sim line with a threaded IRQ.

```c
static irqreturn_t m05_hard(int irq, void *dev_id);    /* hard IRQ context */
static irqreturn_t m05_thread(int irq, void *dev_id);  /* kthread context, may sleep */
static int m05_request_irq(struct m05 *d);             /* request_threaded_irq */
static u64 m05_percentile(const u64 *sorted, u32 n, u32 pct); /* nearest rank */
```

- The hard handler records a timestamp, enqueues one event, and returns `IRQ_WAKE_THREAD`. It must not sleep or take a mutex.
- The thread drains **all** queued events (one wakeup can cover many edges), records the hard-to-thread latency, and may sleep.
- Expose `/sys/devices/platform/elx_m05/{stats,latency,gate,reset,lat_inject,work_us}`.
- Constraints: after K rising edges fired with spacing, `hard == thread_runs == processed == K` and the `/proc/interrupts` delta equals K. With the thread parked at `gate`, K edges arriving must yield `processed == K` and `thread_runs == 2`.
- Example (real run): `K=200 /proc/interrupts delta=200 hard=200 thread_runs=200 processed=200`.

## 3. 시작 코드

파일 구성:

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` / `practice.c` | 모듈 (TODO 4개 / 정답 / 추가 연습 답) |
| `Makefile` | Kbuild: `MOD=starter` 등 |
| `m05_test.c` | 게스트 안의 CHECK 하네스 (정적 바이너리, 모든 대기는 시간 제한) |
| `guest.sh`, `lab_common.sh`, `check.sh`, `demo.sh` | 게스트 스크립트, 빌드·부팅, 판정 |
| `demo_bugs.c`, `demo_hog.c` | 실패 재현 데모 모듈, CPU 부하기 |

VM 안에서 어느 디렉터리에서든 실행한다:

```bash
limactl shell elinux -- bash -lc '.../lessons/ELX/ELX-M05/check.sh starter'
```

`check.sh` 는 `/tmp/ELX-M05_work/<이름>/` 에 복사해서 빌드 (`make -C /var/lab/out/linux-build-dbg M=$PWD MOD=starter modules`, Kbuild 는 `-Werror`), `m05_test` 를 `gcc -static -std=c11 -Wall -Wextra -Werror` 로 빌드, initramfs 를 만들어 부팅한다. 시작 코드에서는 TODO 가 비어 있어 `TESTS passed 9/30` 으로 끝나고 `VERDICT starter: FAIL` (exit 1, 약 22초) 이 나온다.

<!-- code: starter.c -->

테스트 하네스(게스트에서 실행):

<!-- code: m05_test.c -->

## 4. 힌트

<details><summary>힌트 1 — hard 핸들러에서 해도 되는 것</summary>

`ktime_get_ns()`, `atomic_inc()`, `kfifo_put()` 는 sleep 하지 않는다. mutex, `msleep`, `kmalloc(GFP_KERNEL)`, `i2c_transfer` 는 안 된다. 이벤트 하나당 kfifo 슬롯 하나, 꽉 차면 `dropped++`.

</details>

<details><summary>힌트 2 — 스레드에서 한 번에 몇 개를 처리해야 하나</summary>

`while (kfifo_get(&d->fifo, &ev))` 처럼 **빌 때까지** 돈다. 스레드가 호출된 횟수와 처리한 이벤트 수는 다를 수 있다. 재실행은 RUNTHREAD 비트 하나로 예약되며, 재실행 시 fifo 가 비어 있으면 그냥 끝난다.

</details>

<details><summary>힌트 3 — request_threaded_irq 인자</summary>

`request_threaded_irq(irq, hard, thread, flags, name, dev_id)` 이고, 이 레슨의 플래그는 `IRQF_TRIGGER_RISING | IRQF_ONESHOT`, name 은 `M05_NAME`. 성공하면 `d->irq_requested = true` 로 해야 `free_irq` 가 짝을 이룬다.

</details>

<details><summary>힌트 4 — nearest-rank 퍼센타일</summary>

정렬된 배열에서 1-based 순위 = ⌈pct·n/100⌉. `DIV_ROUND_UP(pct * n, 100)` 그리고 0-based 로 빼기 1. n=100, pct=99 → rank 99 → 99번째 값.

</details>

## 5. 풀이

### Part A 풀이 — hard 와 thread 를 가르는 기준

| | hard handler (`m05_hard`) | thread handler (`m05_thread`) |
|---|---|---|
| 실행 컨텍스트 | hardirq, 해당 CPU 인터럽트 꺼짐 (`in_hardirq()` true) | 일반 kthread `irq/22-elx_m05` (`in_task()` true) |
| sleep / mutex | **금지** | 가능 (`usleep_range`, `mutex_lock`) |
| 할 일 | 시각 찍기, 장치 조용히 시키기(실제 HW 라면 ack), 이벤트 큐잉 | 버스 트랜잭션, 데이터 처리, 사용자 공간 알림 |
| 다른 작업 지연 | CPU 전체 (같은 CPU 의 다른 IRQ·태스크 모두) | SCHED_FIFO 50 에서 스케줄됨 → 더 높은 RT 태스크에게 양보 |

코드 포인트 세 개:

1. **dev_id** 로 드라이버 상태를 넘긴다. `free_irq(irq, d)` 도 같은 d — 공유 IRQ 에서 어느 action 을 지울지 구분하는 값이다.
2. **lookup key = 라인 이름**: `GPIO_LOOKUP("m05-sig", U16_MAX, "sig", GPIO_ACTIVE_HIGH)`. `gpiod_find()` 는 `chip_hwnum == U16_MAX` 면 `gpio_name_to_desc()` 로 찾는다(소스 확인). 칩 라벨 대신 configfs 의 `line0/name` 에 쓴 이름을 쓰는 이유다.
3. `GPIOD_IN` 으로 요청해야 gpio-sim 이 pull 쓰기를 입력 변화로 보고 irq 를 쏜다 (`gpio_sim_apply_pull` 의 `request_map`/`direction_map` 검사).

실제 결과 (solution, 게스트):

```text
GUEST_START dev=gpio-sim.0 chip=gpiochip1 pull=/sys/devices/platform/gpio-sim.0/gpiochip1/sim_gpio0/pull kernel=6.18.55-dbg cpus=4
== Part A: K spaced edges, each processed before the next ==
K=200 /proc/interrupts delta=200 hard=200 thread_runs=200 processed=200
```

hard 핸들러까지의 실제 호출 경로 (`mode=sleep_hard` 데모의 콜 스택에서 그대로 발췌, 아래가 먼저 호출된 쪽):

```text
[   11.704242]  demo_hard+0x90/0xc0 [demo_bugs]
[   11.704866]  __handle_irq_event_percpu+0xb0/0x3a0
[   11.704907]  handle_irq_event+0x4c/0xc0
[   11.704930]  handle_simple_irq+0xd8/0x100
[   11.704949]  irq_sim_handle_irq+0x94/0x100
[   11.704986]  irq_work_single+0x34/0xa8
[   11.705046]  ipi_handler+0x2a0/0x320
[   11.705082]  gic_handle_irq+0x40/0xe0
…
[   11.705204]  irq_set_irqchip_state+0xa4/0x128
[   11.705212]  gpio_sim_apply_pull.isra.0+0xf8/0x240
[   11.705222]  gpio_sim_sysfs_pull_store+0x54/0x98
```

읽는 법: `pull` 쓰기(`gpio_sim_sysfs_pull_store`) → `irq_set_irqchip_state` 가 irq_work 를 건다 → IPI 로 hard IRQ 경로 진입(`gic_handle_irq` → `ipi_handler` → `irq_work_run`) → `irq_sim_handle_irq` 가 진짜 IRQ 흐름 핸들러 `handle_simple_irq` 를 부른다. 즉 **가짜 입력, 진짜 IRQ 경로** 다. 실제 칩이었다면 `gic_handle_irq` 아래가 GPIO 컨트롤러의 체인 핸들러가 된다.

카운터가 각각 무엇을 세는가:

| 값 | 어디서 증가 | 뜻 |
|---|---|---|
| `/proc/interrupts` 합계 | `handle_simple_irq` 의 `kstat_incr_irqs_this_cpu` | irq 흐름이 hard 핸들러까지 간 횟수 |
| `hard` | `m05_hard` | hard 핸들러 호출 횟수 (위와 같아야 함) |
| `thread_runs` | `m05_thread` 진입 | 스레드 핸들러 호출 횟수 (깨움 합치면 적어짐) |
| `processed` | fifo 에서 꺼낸 이벤트마다 | 실제로 처리한 이벤트 수 (반드시 = hard - dropped) |
| `dropped` | `kfifo_put` 실패 | fifo 가 꽉 차서 잃은 이벤트 |

**왜 이렇게 나누나 (실제 칩)**: 이 lesson 의 가짜 "버스" 는 `usleep_range` 다. 실제로는 hard 핸들러가 레벨 트리거 소스를 그대로 두고 나가면 IRQ 가 다시 뜬다 (Part B 뒤의 ONESHOT 절 참고). 그러니 hard 에서 "칩의 INT 상태를 끄거나 라인을 마스크" 까지만 하고 나머지는 스레드로 미룬다. I2C 익스팬더라면 hard 는 INT 를 레벨로 잡아 마스크만 하고 thread 가 I2C 로 원인 읽고 라인을 해제한다. 이것이 대표적인 ONESHOT + threaded handler 조합이다.

### Part B 풀이 — 깨움은 비트, 이벤트는 fifo

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part B 강제 스케줄 (실제 카운터 값)</text>
  <text x="10" y="50" fill="currentColor">edge</text>
  <rect x="70" y="36" width="10" height="20" class="f-warn" stroke="currentColor"/>
  <rect x="130" y="36" width="10" height="20" class="f-warn" stroke="currentColor"/><rect x="170" y="36" width="10" height="20" class="f-warn" stroke="currentColor"/>
  <rect x="210" y="36" width="10" height="20" class="f-warn" stroke="currentColor"/><rect x="250" y="36" width="10" height="20" class="f-warn" stroke="currentColor"/>
  <text x="290" y="50" fill="currentColor">... (edge 2~64, gate 동안)</text>
  <text x="10" y="92" fill="currentColor">thread</text>
  <rect x="74" y="76" width="300" height="24" class="f-mut" stroke="currentColor"/>
  <text x="224" y="93" fill="currentColor" text-anchor="middle">run#1: gate 대기 (RUNTHREAD=1)</text>
  <rect x="378" y="76" width="150" height="24" class="f-acc" stroke="currentColor"/>
  <text x="453" y="93" fill="currentColor" text-anchor="middle">fifo 64개 처리</text>
  <rect x="532" y="76" width="60" height="24" class="f-ok" stroke="currentColor"/>
  <text x="562" y="93" fill="currentColor" text-anchor="middle">run#2</text>
  <text x="600" y="93" fill="currentColor">fifo 비어 끝</text>
  <text x="10" y="134" fill="currentColor">kfifo</text>
  <text x="70" y="134" fill="currentColor">쌓임 → 64</text>
  <text x="240" y="134" fill="currentColor">(hard 가 넣음)</text>
  <text x="400" y="134" fill="currentColor">비움 → 0</text>
  <line x1="10" y1="150" x2="730" y2="150" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="10" y="172" fill="currentColor" font-weight="bold">단일 슬롯(버그)에서는:</text>
  <text x="10" y="194" fill="currentColor">edge 20개 → hard 가 같은 슬롯에 20번 덮어씀 → gate 해제 뒤 thread 2회 실행, 소비 1회</text>
  <text x="10" y="216" fill="currentColor" font-weight="bold">데모 실측: hard=20 runs=2 handled=1</text>
  <text x="10" y="248" fill="currentColor">결론: "깨움 1회 = edge 1개" 는 거짓. edge 수를 세려면 hard 쪽 카운터, 데이터를 안 잃으려면 큐.</text>
</svg>
```

`__irq_wake_thread()` 의 핵심 (kernel/irq/handle.c, 6.18.55):

```c
	/*
	 * Wake up the handler thread for this action. If the
	 * RUNTHREAD bit is already set, nothing to do.
	 */
	if (test_and_set_bit(IRQTF_RUNTHREAD, &action->thread_flags))
		return;
```

`irq_wait_for_interrupt()` 는 핸들러 호출 **전에** `test_and_clear_bit(IRQTF_RUNTHREAD)` 를 하므로, 스레드가 핸들러 안에 있는 동안 도착한 edge 는 RUNTHREAD 를 다시 1로 만들고, 핸들러가 끝나면 **한 번 더** 돈다. 그래서 강제 스케줄의 `thread_runs == 2` 가 결정적이다 (edge 2..64 는 스레드가 run #1 안에 묶인 뒤 발사됨, 하네스가 thread_runs==1 을 확인 후 쏜다).

실제 출력:

```text
== Part B: thread parked at a gate, K edges pile up ==
while parked: hard=64 thread_runs=1 processed=0
after release: /proc/interrupts delta=64 hard=64 thread_runs=2 processed=64 dropped=0
```

**버그 재현 → 탐지 → 수정** (이 레슨의 Layer 8 핵심):

| 단계 | 내용 | 도구 / 실측 |
|---|---|---|
| 버그 | `demo_bugs.c mode=lost` — hard 가 단일 슬롯(`g_slot`, `g_slot_valid`)에 덮어쓰고 스레드는 깨어날 때마다 1개만 소비 | 같은 gate 강제 스케줄 (edge 20) |
| 탐지 | sysfs 카운터 비교: hard vs handled | `after release: hard=20 runs=2 handled=1` |
| 수정 | fifo 로 이벤트마다 슬롯, 스레드는 빌 때까지 드레인 (solution) | `hard=64 ... processed=64 dropped=0` |

`kfifo` 를 쓴 이유: writer 는 hard 핸들러뿐(IRQ 흐름이 직렬화 — 같은 IRQ 의 hard 핸들러는 동시에 두 CPU 에서 안 돈다, `irq_can_handle_pm` / INPROGRESS), reader 는 IRQ 스레드뿐이다. `include/linux/kfifo.h` 의 주석이 정확히 이 조건을 허용한다: *"There is no locking required until only one reader and one writer is using the fifo"*. 그래서 lock 없이 SPSC 로 쓸 수 있고, 이 파일의 `kfifo_reset()` 주의도 지켜야 한다 (`reset` 경로는 `disable_irq()` 로 hard+thread 를 멈춘 뒤에만 큐를 비운다).

**`/proc/interrupts` 가 hard 카운트와 정확히 일치하는 이유**: `kstat_incr_irqs_this_cpu()` 는 `handle_simple_irq()` 에서 핸들러 호출 직전에 증가한다. 이 카운터는 **스레드 실행 횟수가 아니라 hard 레벨 흐름 처리** 를 센다. 그래서 스레드 호출 수는 sysfs 에서 따로 세야 한다.

#### IRQF_ONESHOT — 무엇을 막고, gpio-sim 에서 왜 안 보이나

`handler == NULL` 로 요청하면 커널이 기본 hard 핸들러(`irq_default_primary_handler`, 하는 일은 스레드 깨우기뿐)를 붙인다. 이때 ONESHOT 없이 요청하면 실제로:

```text
[    3.949093] genirq: Threaded irq requested with handler=NULL and !ONESHOT for elx_m05_demo (irq 22)
[    3.956672] demo mode=nooneshot irq=22 request_threaded_irq() returned -22
```

`__setup_irq()` 의 주석(kernel/irq/manage.c): *"The interrupt was requested with handler = NULL, so we use the default primary handler for it. But it does not have the oneshot flag set. In combination with level interrupts this is deadly, because the default primary handler just wakes the thread, then the irq lines is reenabled, but the device still has the level irq asserted. Rinse and repeat..."* — 그래서 커널은 **트리거 종류와 무관하게 무조건 거부**한다(`-EINVAL`). 커널은 "실제로 레벨인지 믿을 수 없다"고 주석에 적고 있다.

같은 호출에 `IRQF_ONESHOT` 만 더하면 (`mode=fixoneshot`, 실제 결과):

```text
request_threaded_irq ret: 0
runs=1 handled=0
 22:          1          0          0          0  irq_sim   0 Edge      elx_m05_demo
```

**ONESHOT 이 실제로 하는 일** (소스 확인):

| 흐름 핸들러 | ONESHOT 일 때 |
|---|---|
| `handle_level_irq` | `mask_ack_irq()` 후 처리, `cond_unmask_irq()` 에서 `threads_oneshot` 이 0 이 될 때까지 unmask 안 함 |
| `handle_fasteoi_irq` | `if (desc->istate & IRQS_ONESHOT) mask_irq(desc);` → `cond_unmask_eoi_irq` |
| **`handle_simple_irq` (이 레슨의 irq_sim)** | **마스크 코드 없음** — 그냥 핸들러 호출 |

즉, gpio-sim 은 `irq_sim_set_type()` 이 엣지만 허용(`IRQF_TRIGGER_HIGH` 로 요청하면 실제로 `genirq: Setting trigger mode 4 for irq 22 failed (irq_sim_set_type+0x0/0x60)` + `-22`)하고 흐름이 `handle_simple_irq` 라서 **ONESHOT 의 "라인을 스레드가 끝날 때까지 마스크" 동작 자체는 이 환경에서 관찰할 수 없다.** 이 레슨에서 실제로 돌려 확인한 것은 `-EINVAL` 경로뿐이다. 레벨 트리거 폭풍의 실측은 실제 레벨 IRQ 를 가진 환경이 필요하다고 표시해 둔다(§7, 면접 Q2).

### Part C 풀이 — 스레드의 정체, 지연 분포

```text
irq thread: pid=91 comm=irq/22-elx_m05 policy=1 rtprio=50 (sysfs irq=22)
```

- 이름 `irq/%d-%s` 는 `setup_irq_thread()`, 15자 초과는 TASK_COMM_LEN 때문에 잘린다 (데모에서 `elx_m05_demo` → `[irq/22-elx_m05_]`).
- `irq_thread()` 맨 앞의 `sched_set_fifo(current)` 가 SCHED_FIFO, rt_priority = `MAX_RT_PRIO/2` = 50 을 준다(kernel/sched/syscalls.c 확인). 모든 threaded IRQ 가 기본 같은 50 이다. 조정은 `chrt -p`/`sched_setscheduler` (이 게스트의 BusyBox 에 chrt 는 없어 실행 못 함 — 면접 답변용 지식, Q6 에서 다시).

지연 측정: `ktime_get_ns()` 를 hard 에서 찍고 스레드가 이벤트를 꺼낸 시점과의 차이. **TCG 숫자는 실제 HW 지연이 아니다**: MTTCG 의 IPI·타이머·스케줄 경로가 전부 수십~수백 µs 로 부풀려져 있다. 상대 비교만 믿는다. 아래는 `demo.sh latency` 실측 (`work_us=0`, edge 200개, 5ms 간격, VM load 5~8 — 다른 작업들과 VM 공유):

| 부하 (게스트) | 실행 | min | p50 | p99 | max | thread_runs |
|---|---|---|---|---|---|---|
| idle | 1회차 | 170.6 µs | 295.8 µs | 2.47 ms | 5.05 ms | 200 |
| idle | 2회차 | 179.9 µs | 492.6 µs | 3.33 ms | 4.51 ms | 200 |
| idle | 3회차 | 120.4 µs | 596.0 µs | 4.94 ms | 5.81 ms | 200 |
| 4 hogs SCHED_OTHER | 1회차 | 118.2 µs | 346.8 µs | 5.09 ms | 9.81 ms | 200 |
| 4 hogs SCHED_OTHER | 2회차 | 131.8 µs | 452.8 µs | 85.8 ms | 131.7 ms | 189 |
| 4 hogs SCHED_OTHER | 3회차 | 127.3 µs | 962.9 µs | 49.1 ms | 84.1 ms | 195 |
| 4 hogs SCHED_FIFO 98 | 1회차 | 112.5 µs | 225.9 ms | 1.99 s | 2.03 s | 89 |
| 4 hogs SCHED_FIFO 98 | 2회차 | 165.3 µs | 1.05 s | 2.84 s | 2.88 s | 55 |
| 4 hogs SCHED_FIFO 98 | 3회차 | 216.1 µs | 0.99 s | 2.95 s | 2.98 s | 50 |

(게스트 안 `/proc/loadavg` 는 부팅 직후라 0.x — 의미 있는 부하 표시는 VM 쪽 `uptime` 에서 본 값 4.6~8.2, 다른 QEMU 작업들과 공유. **비율만** 믿는다.)

읽는 법 (상대 비교):

- SCHED_OTHER 부하는 p50 을 1.2~1.6배 정도만 흔든다 — FIFO 50 스레드는 CFS 태스크를 항상 선점한다. p99 만 튀는데(TCG vCPU 를 VM 커널이 스케줄하는 잡음으로 추정) 숫자 자체보다 "p50 은 거의 그대로" 를 본다.
- SCHED_FIFO 98 hog 를 CPU 마다 돌리면 p50 이 약 1000배. **FIFO 50 은 FIFO 98 을 못 이긴다**: "threaded IRQ = 항상 빠르다" 가 아니다. 애플리케이션 RT 태스크를 IRQ 스레드보다 높은 우선순위로 올리면 드라이버 처리가 굶는다. 그리고 그 동안 `thread_runs`(50~89)는 edge 수(200)에 훨씬 못 미친다 — Part B 의 코얼레싱이 실제로 일어나는 현장이다. 이 표에서 `processed` 는 항상 200: fifo 덕분에 데이터는 안 잃었다.

재현 명령: `.../demo.sh latency` (VM 안에서 실행, 약 20초).

#### 이 환경에서 확인하지 못한 것

| 항목 | 상태 |
|---|---|
| 레벨 트리거에서 ONESHOT 없이 생기는 IRQ 폭풍 | 재현 불가 (irq_sim 은 엣지만). `-EINVAL` 경로와 소스 주석까지만 확인 |
| 실제 HW 의 hard→thread 지연 절댓값 | TCG 값은 절대 수치로 쓰지 않음 |
| `chrt` 로 IRQ 스레드 우선순위 올린 비교 | 게스트 BusyBox 에 chrt 없음, 미실행 |
| `threadirqs` / PREEMPT_RT 에서의 동작 | 파라미터 존재만 확인, 실행 안 함 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

1. **hard 핸들러에서 sleep** — 데모 `mode=sleep_hard` (`msleep(1)`)의 실제 결과는 경고가 아니라 **그 CPU 의 사망** 이다:

```text
[   11.698250] BUG: scheduling while atomic: guest.sh/86/0x00010002
[   11.703179] CPU: 0 UID: 0 PID: 86 Comm: guest.sh Tainted: G           O        6.18.55-dbg #1 PREEMPT
…
[   11.704221]  msleep+0x50/0x80
[   11.704242]  demo_hard+0x90/0xc0 [demo_bugs]
[   11.704866]  __handle_irq_event_percpu+0xb0/0x3a0
[   11.704907]  handle_irq_event+0x4c/0xc0
[   11.704930]  handle_simple_irq+0xd8/0x100
[   11.704949]  irq_sim_handle_irq+0x94/0x100
…
[   11.705572] DEBUG_LOCKS_WARN_ON(lockdep_hardirq_context())
[   11.705756] WARNING: CPU: 0 PID: 0 at kernel/locking/lockdep.c:4432 lockdep_hardirqs_on_prepare+0x1a8/0x210
…
[   37.704952] rcu: INFO: rcu_preempt detected stalls on CPUs/tasks:
[   37.705636] rcu: 	0-...!: (1 GPs behind) idle=5f88/0/0x0 softirq=320/321 fqs=1 (false positive?)
```

게스트는 끝까지 돌아오지 못했고 데모는 `timeout` 으로 끊었다 (첫 시도는 180초 timeout, 이후 `demo.sh` 는 sleep_hard 를 70초로 줄임). 왜: hard 컨텍스트에서 `schedule()` 이 불렸고, 뒤로 돌아올 인터럽트 프레임이 사라진 CPU0 은 복구되지 않는다. 고친 코드: sleep 은 스레드 핸들러로, hard 는 `IRQ_WAKE_THREAD` 만.

2. **sleeping GPIO 칩의 라인을 hard 에서 읽기** — gpio-sim 은 `gc->can_sleep = true` (mutex 기반). `gpiod_get_value()` 를 hard 에서 부르면(`mode=getvalue`) 경고 뒤 **자기 자신이 든 뮤텍스** 를 다시 잡는다 — `pull` 쓰기 경로가 `chip->lock` 을 든 채 irq_work 를 맞기 때문이다:

```text
[    2.697082] WARNING: CPU: 0 PID: 85 at drivers/gpio/gpiolib.c:3502 gpiod_get_value+0x64/0xa0
…
[    2.700976]  demo_hard+0xa0/0xc0 [demo_bugs]
[    2.701196]  handle_simple_irq+0xd8/0x100
…
[    2.702745]  gpio_sim_apply_pull.isra.0+0xf8/0x240
…
[    2.704554] BUG: sleeping function called from invalid context at kernel/locking/mutex.c:575
[    2.704567] in_atomic(): 1, irqs_disabled(): 1, non_block: 0, pid: 85, name: guest.sh
[    2.704575] preempt_count: 10001, expected: 0
…
[    2.704789]  #3: ffff00000533f718 (mutex_){+.+.}-{4:4}, at: gpio_sim_apply_pull.isra.0+0x44/0x240
…
[    2.706964]  mutex_lock_nested+0x2c/0x40
[    2.706971]  gpio_sim_get+0x38/0x90
[    2.707003]  demo_hard+0xa0/0xc0 [demo_bugs]
```

줄 `#3` 이 원인이다: pull 쓰기가 `chip->lock` 을 쥔 채 irq_work 가 같은 CPU 의 인터럽트로 들어와, hard 핸들러 안에서 `gpio_sim_get` 이 같은 mutex 를 다시 잡으려 한다. 앞선 첫 run 에서 lockdep 은 같은 자리에 `[ BUG: Invalid wait context ]` 도 찍었다. 이 게스트는 그 뒤 응답을 멈추고 `timeout` 으로 끝났다.

답: 스레드에서 `gpiod_get_value_cansleep()` (practice 연습 1). 실제 I2C/SPI GPIO 칩은 전부 이 범주다.

3. **"스레드 1회 = edge 1개" 라고 세기** — Part B. `thread_runs` 로 이벤트 수를 세면 틀린다. 카운트는 hard 쪽, 데이터는 fifo.

4. **`thread_fn` 만 쓰고 ONESHOT 빼기** — 실제 `-EINVAL`, 위 §5. 고침: `IRQF_ONESHOT` 또는 진짜 hard 핸들러를 준다(irq_chip 이 `IRQCHIP_ONESHOT_SAFE` 인 경우만 예외).

5. **`free_irq` 전에 드라이버 상태를 지움** — `free_irq()` 는 hard 핸들러와 스레드가 끝날 때까지 기다린다 (`__free_irq` 가 `kthread_stop`). 정답 코드의 `m05_exit()` 는 sysfs 먼저, `free_irq` 다음, `gpiod_put` 마지막 순서를 지킨다. 반대로 하면 스레드가 치워진 `gpio`/메모리를 만진다.

6. **`/proc/interrupts` 숫자만 보고 "처리됐다" 고 믿기** — 이 숫자는 hard 레벨 카운트다. 스레드가 돌았는지, fifo 가 넘쳤는지(`dropped`)는 드라이버가 노출해야 한다 (sysfs 한 파일에 여러 값 쓰기는 sysfs 규칙 위반이지만 이 레슨은 스냅샷 편의상 그렇게 했다 — 실제 드라이버라면 debugfs).

7. **latency 를 "처리 완료" 시점에서 재기** — 정답은 스레드가 이벤트를 **꺼낸** 시점에서 재고, 그다음 슬립한다. `work_us` 가 지연에 섞이면 "깨어나는 데 걸린 시간" 을 잴 수 없다.

## 8. 면접 Q&A

<details><summary>Q1. request_threaded_irq 에서 hard 와 thread 를 어떻게 나누나요?</summary>

hard 는 인터럽트 꺼진 원자 컨텍스트라 빠르고 sleep 금지: 시각 찍기, 장치 ack/라인 마스크, 이벤트 큐잉, `IRQ_WAKE_THREAD`. thread 는 SCHED_FIFO kthread 라 sleep 가능: I2C/SPI 트랜잭션, 처리, 사용자 공간 알림.

꼬리질문: *hard 를 NULL 로 하면?* 커널이 `irq_default_primary_handler`(스레드 깨우기만)를 넣는다. 그러면 ONESHOT 이 필수이며 없으면 `-EINVAL` — 이 레슨에서 실제로 봤다.

</details>

<details><summary>Q2. IRQF_ONESHOT 은 왜 있나요?</summary>

레벨 트리거 소스는 hard 핸들러가 장치를 ack 안 하고 나가면 unmask 순간 다시 뜬다(소스 주석의 "Rinse and repeat"). ONESHOT 은 `handle_level_irq`/`handle_fasteoi_irq` 흐름에서 라인을 `mask` 한 채로 유지하고, 스레드가 끝난 뒤 `irq_finalize_oneshot` 에서 unmask 한다.

꼬리질문: *엣지면 필요 없지 않나?* 엣지에서는 흐름상 없어도 동작하지만 커널은 `handler==NULL` 이면 엣지여도 거부한다(소스 주석: 트리거 타입을 믿을 수 없어서). 실제로 내가 확인한 건 `-EINVAL` 까지이며, 이 레슨의 irq_sim 은 `handle_simple_irq` 라 마스크 동작 자체는 측정 못 했다.

</details>

<details><summary>Q3. 이벤트 100개가 짧은 시간에 왔는데 thread 는 3번만 돌았어요. 정상인가요?</summary>

정상이다. `__irq_wake_thread` 가 `test_and_set_bit(IRQTF_RUNTHREAD)` 로 깨움을 비트 1개로 합친다. 데이터를 잃지 않으려면 hard 에서 이벤트마다 fifo/ring 에 넣고 thread 가 빌 때까지 비운다. 실측 강제 스케줄: `hard=64 thread_runs=2 processed=64`.

꼬리질문: *fifo 를 꽉 채우면?* `dropped` 를 세고 반드시 사용자 공간에 노출한다(연습 2: K=300 → `dropped=44` 정확히).

</details>

<details><summary>Q4. hard 핸들러에서 mutex 나 sleep 을 쓰면 어떻게 되나요?</summary>

실제 결과: `msleep` 은 `BUG: scheduling while atomic` 후 그 CPU 가 복구 못 되어 RCU 스톨까지 갔다(§7). mutex 도 같은 범주(`BUG: sleeping function called from invalid context`, 빠른 경로가 성공해도 lockdep 이 잡음). IRQ 컨텍스트 전용 락 규칙은 ELX-M04 에서 다룬다.

꼬리질문: *왜 sleep 이 안 되나?* 깨울 태스크 문맥이 없고, 인터럽트 프레임 위에서 스케줄러로 들어가면 되돌아올 곳이 사라진다.

</details>

<details><summary>Q5. tasklet, workqueue, threaded IRQ 중 무엇을 고르나요?</summary>

sleep 이 필요하면 workqueue 나 threaded IRQ. **스레드에서 드라이버 고유 RT 우선순위로 인터럽트별 처리가 필요하면 threaded IRQ**, 재사용 작업/지연 잡이면 workqueue. tasklet 은 softirq 컨텍스트라 sleep 불가(레거시). 수치·컨텍스트 비교는 ELX-M06 이 담당한다.

꼬리질문: *왜 threaded IRQ 가 RT 패치와 잘 맞나?* `threadirqs` 부트 파라미터/PREEMPT_RT 는 일반 IRQ 도 스레드로 밀어 우선순위 제어를 준다(이 커널에서 `threadirqs` 파라미터 존재까지만 확인; 실행 비교는 안 함).

</details>

<details><summary>Q6. IRQ 스레드의 우선순위와 starvation 은 어떻게 보나요?</summary>

`ps` 로 `[irq/22-elx_m05]`, `/proc/<pid>/stat` 의 41번째 필드 정책=1(SCHED_FIFO), 40번째 rt_priority=50. FIFO 50 은 CFS 태스크는 선점하지만 FIFO 98 태스크는 못 이긴다 — §5 표에서 p50 이 약 1000배 증가. 해결: 튜닝 대상 IRQ 스레드의 우선순위를 올리거나(`chrt`), 앱 RT 우선순위 설계를 다시 한다.

꼬리질문: *우선순위를 올리면 부작용은?* 다른 태스크·드라이버가 굶을 수 있으므로 시스템 전체 RT 설계 문제다.

</details>

<details><summary>Q7. 공유 IRQ (IRQF_SHARED) 와 ONESHOT 이 섞이면?</summary>

같은 라인의 action 끼리 ONESHOT 설정이 안 맞으면 `__setup_irq()` 에서 `mismatch`(소스 line 1592 근처, `IRQF_COND_ONESHOT` 예외 있음)로 실패한다. 각 드라이버가 자기 장치 때문인지 레지스터로 확인해 아니면 `IRQ_NONE` 을 돌려야 한다.

꼬리질문: *IRQ_NONE 은 왜 중요한가?* 아무도 처리하지 않은 인터럽트가 쌓이면 spurious 판정(`nobody cared`)으로 라인이 꺼진다.

</details>

<details><summary>Q8. disable_irq 와 synchronize_irq, free_irq 는 어떻게 다른가요?</summary>

`disable_irq()` 는 라인을 끄고 실행 중인 hard/thread 가 끝날 때까지 기다린다 (그래서 hard 안에서 호출하면 데드락 — 그때는 `disable_irq_nosync`). `free_irq()` 는 스레드까지 멈추고 action 을 지운다. 이 레슨의 `m05_reset_counts()` 가 `disable_irq()` → 큐 비우기 → `enable_irq()` 패턴을 쓴다.

꼬리질문: *disable_irq 중 edge 10개는?* 연습 4 의 실측: `hard during=0 after enable=1` — 엣지는 큐가 아니라 "pending 1개" 로 합쳐져 재발사된다. 카운트가 필요한 장치라면 이 구간을 피해야 한다.

</details>

## 9. 추가 연습

각 연습의 답은 접어 두었고, `practice.c` + `m05_test.c`(practice 모드)로 한 번에 검증한다.

**연습 1. 양쪽 엣지 + 라인 값.** `both_edges=1` 로 `IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING` 을 요청하고, 스레드에서 `gpiod_get_value_cansleep()` 로 라인 값을 읽어 `level_high/level_low` 를 센다. K 번 up/down 후 `hard=2K, level_high=K, level_low=K`?

<details><summary>답</summary>

트리거 플래그만 바꾸고, 읽기는 스레드에서 `_cansleep`. 하네스는 각 edge 를 처리 완료까지 기다리므로 레벨이 결정적이다. 실측: `hard=100 level_high=50 level_low=50`. 간격을 두지 않으면 스레드가 읽을 때 라인이 이미 바뀌어 있을 수 있다 — 그래서 실제 하드웨어는 hard 시점의 INT 상태 레지스터를 큐에 넣는 것이다 (can_sleep 칩은 hard 에서 못 읽으니 칩이 지원하는 hard-context 방법을 쓴다).

</details>

**연습 2. fifo 오버플로.** 스레드를 gate 에 묶은 채 K=300 edge 를 쏘면 (fifo 256) `hard/dropped/processed` 는?

<details><summary>답</summary>

`hard=300 dropped=44 processed=256` — fifo 는 2의 거듭제곱 크기이고 `kfifo_put` 이 실패하면 드롭을 센다. 실측 동일. 실무 교훈: 드롭 카운터가 없으면 "신호가 사라졌는데 로그는 깨끗한" 버그가 된다.

</details>

**연습 3. thread-only 핸들러.** `thread_only=1` 이면 `handler=NULL`+`IRQF_ONESHOT` 으로 요청한다. 무엇이 달라지나?

<details><summary>답</summary>

hard 카운터/타임스탬프가 없다. 실측: `/proc/interrupts delta=50 hard=0 thread_runs=50 processed=50 latency: n=0`. /proc 은 기본 핸들러 호출을 세므로 정상. "지연을 재려면 hard 핸들러가 필요하다" 가 교훈이다.

</details>

**연습 4. disable_irq 동안의 edge.** `irq_off=1` 로 끄고 edge 10개, `irq_off=0` 으로 켠다. `hard` 값은?

<details><summary>답</summary>

실측 `hard during=0 after enable=1`. 비활성 구간의 엣지는 합쳐져 `check_irq_resend()`(소프트웨어 재발사) 한 번으로 재생된다. edge 10개 == 이벤트 10개가 필요한 시스템이라면 disable 구간을 피하거나 장치 쪽 카운터로 맞춘다.

</details>

### 한 번에 검증

```bash
limactl shell elinux -- bash -lc '.../lessons/ELX/ELX-M05/check.sh practice'
```

실측: `TESTS passed 50/50 (module practice)`, `VERDICT practice: PASS`.

<!-- code: practice.c -->

## 10. 핵심 정리

- hard 핸들러: 원자 컨텍스트, 시각+ack+큐잉만, `IRQ_WAKE_THREAD`. thread: SCHED_FIFO 50 kthread, sleep 가능.
- 깨움은 비트(`IRQTF_RUNTHREAD`) 하나 — 스레드 호출 수 ≠ 이벤트 수. 데이터 손실을 막으려면 hard→thread SPSC 큐 + 드레인 루프.
- `handler=NULL` 인데 ONESHOT 없으면 실제 `-EINVAL`. ONESHOT 의 마스킹은 레벨/fasteoi 흐름에서만 동작 (`handle_simple_irq` 의 irq_sim 에선 관찰 불가).
- `/proc/interrupts` 는 hard 레벨 카운트. "처리됨" 은 드라이버가 노출한 카운터로 확인.
- `gpiod_get_value` vs `_cansleep`: can_sleep 칩은 hard 에서 읽으면 안 된다. 스레드에서 `_cansleep`.
- IRQ 스레드 기본 우선순위는 FIFO 50 — 앱 RT 태스크가 위면 드라이버가 굶는다. 지연은 TCG 절댓값 말고 비율로.
- 재현 방법: 강제 스케줄(gate)로 결정적으로 코얼레싱을 만들고, 카운터로 검증하고, 데드라인 수치는 절대 CHECK 에 넣지 않는다.

## 참고자료

- `Documentation/admin-guide/gpio/gpio-sim.rst` (6.18.55), `drivers/gpio/gpio-sim.c`, `kernel/irq/irq_sim.c`
- `kernel/irq/manage.c` (`__setup_irq`, `request_threaded_irq`, `irq_thread`), `kernel/irq/handle.c` (`__irq_wake_thread`), `kernel/irq/chip.c` (flow handlers)
- `include/linux/kfifo.h` — 단일 reader/writer 무락 조건 주석
- `kernel/sched/syscalls.c` (`sched_set_fifo`)
- 관련 레슨: ELX-M04(컨텍스트별 락 선택), ELX-M06(tasklet/workqueue/threaded IRQ 비교), RTOS-B10·RTOS-M01(deferred ISR), C-M14(ISR 공유 변수), LLAT-M02/DS-A01(SPSC·큐), SIV-M09(스트레스 테스트)
