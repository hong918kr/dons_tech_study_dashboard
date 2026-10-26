# E3 · Data-Ready ISR → Task Overheat Pipeline

> **ID**: E3 · **Priority**: P1 · **Pattern**: ISR → SPSC ring → task + hysteresis + critical section · **Time**: 35 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh E3` / `./run.sh E3 sol`

## 1. Problem

A temperature ADC raises a **DATA-READY interrupt** every 10 ms. Build the firmware pipeline from that interrupt to the cooling fan:

- **ISR** `TEMP_DRDY_IRQHandler()` captures `{ millis(), ADC_DATA }` and pushes it into an **SPSC ring buffer**. If the ring is full, it **drops the new sample and increments `g_overrun_count`**. Nothing else happens in the ISR.
- **Task** `temp_task_poll()` runs from the main loop, whenever it gets CPU time. In this order it:
  1. **reads and clears** `g_overrun_count` with interrupts masked (`irq_disable()` / `irq_enable()`), and adds the value to `g_task.overruns_total`. *Simulation rule:* call `SIM_PREEMPT()` between the read and the clear. That is the point where a real IRQ could arrive, and the harness injects one there.
  2. **drains** the ring. For each sample: if a previous sample exists and `t - last_t > STALE_MS` (50 ms, wrap-safe), reset the detector streak; convert with `adc_to_c10()`; call `det_update()`. On `EV_OVERHEAT` call `fan_set(true)`, on `EV_RECOVERED` call `fan_set(false)`, and append `{t_ms, c10, ev}` to `g_task.log` (write at most `LOG_CAP`, always count `n_log`).
- **Conversion** (TMP36-style sensor, 12-bit ADC, 3.3 V reference): use the low 12 bits only. `mV = code * 3300 / 4096` (truncating), then `c10 = mV - 500`.
- The ring has `RB_SIZE = 8` slots (power of two, all usable) and free-running `uint32_t` C11 atomic `head`/`tail`. The ISR writes only `head`, the task writes only `tail`.
- Already provided: the fake hardware (`ADC_DATA`, `millis()`, `irq_disable/enable` with a one-bit pending latch, `fan_set`), the hysteresis detector from [T1-2](../T1-2_streaming_hysteresis/index.html) (on at `>= 850` ×3, off at `<= 800` ×3), `temp_task_init()`, and the harness. No malloc, no real threads. The tests call the ISR at scripted points.

```c
#define RB_SIZE 8u
typedef struct { uint32_t t_ms; uint16_t code; } RawSample;
typedef struct {
    RawSample buf[RB_SIZE];
    _Atomic uint32_t head;           /* written only by the ISR (producer)  */
    _Atomic uint32_t tail;           /* written only by the task (consumer) */
} SampleRing;

extern SampleRing        g_ring;
extern volatile uint32_t g_overrun_count;   /* ISR ++, task read-and-clear */

int16_t adc_to_c10(uint16_t code);
bool    ring_push(SampleRing *r, RawSample s);     /* ISR side  */
bool    ring_pop (SampleRing *r, RawSample *out);  /* task side */
void    TEMP_DRDY_IRQHandler(void);
void    temp_task_poll(void);
```

### Example

```
high = 850, low = 800, n_confirm = 3, one IRQ every 10 ms from t = 1000,
task polls after every 2 IRQs

c10 per IRQ : 820 840 852 860 870 880 | 830 790 780 770 760 760
t_ms        : 1000 ...                  1040 ...              1090
-> EV_OVERHEAT  at t = 1040 (c10 870): fan_set(true)
-> EV_RECOVERED at t = 1090 (c10 770): fan_set(false)

burst: 20 IRQs while the task is blocked -> 8 queued, g_overrun_count = 12;
       next poll: processed 8, overruns_total = 12, g_overrun_count = 0
```

### Constraints

- Single core. The ISR can preempt the task at any instruction unless it is masked. The task never preempts the ISR.
- `millis()` is `uint32_t` and **wraps** (after about 49.7 days).
- While masked, a second DRDY edge on the same line is merged into the first (hardware pending latch). Keep the masked section short.

### Clarifying questions to ask (first 3 minutes)

1. When the ring is full, drop the newest or overwrite the oldest? Must the loss be reported?
2. What goes in the ISR, and what can wait for the task? Is there an RTOS, or a bare-metal super-loop?
3. Is the timestamp taken at capture (ISR) or at processing (task)?
4. Is it single-core? Can another, higher-priority ISR also touch the counter?
5. What is the sample rate, and what is the worst-case task latency? (This sizes the ring.)
6. What should happen after a gap in the data: keep the confirmation streak or restart it?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh E3`로 바로 테스트하세요. 테스트 7개가 들어 있습니다. 가짜 하드웨어, detector, harness는 이미 있고 **TODO 5개**(`adc_to_c10`, `ring_push`, `ring_pop`, ISR, `temp_task_poll`)만 채우면 됩니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — ISR에는 무엇만 넣나?</summary>

ISR은 **"레지스터 읽기 + 시각 찍기 + push + 실패 시 카운트"** 네 가지만 합니다. 변환, 비교, 로그, `printf`, 팬 제어는 모두 task에서 합니다. ISR을 짧게 만들어야 다른 인터럽트의 지연이 줄고, 재진입이나 락 문제도 생기지 않습니다. `ADC_DATA`를 읽는 동작이 DRDY 플래그를 지우는 경우가 많아서, 이 읽기는 ISR에서 **꼭 한 번** 해야 합니다.

</details>

<details>
<summary>Hint 2 — 링버퍼는 락이 필요 없다, 카운터는 필요하다</summary>

SPSC 링은 각 인덱스를 **한쪽만 씁니다**(head는 ISR, tail은 task). 그래서 "데이터 쓰기 → head 공개(release)"와 "head 읽기(acquire) → 데이터 읽기" 순서만 지키면 됩니다. 반면 `g_overrun_count`는 **양쪽이 다 씁니다**(ISR은 `++`, task는 `= 0`). 그래서 `n = cnt; cnt = 0;` 사이에 ISR이 끼어들면 그 사이에 늘어난 값이 사라집니다. 이 두 줄을 `irq_disable()`과 `irq_enable()`로 감싸세요.

</details>

<details>
<summary>Hint 3 — 시간 비교는 뺄셈으로</summary>

stale 검사는 `s.t_ms - last_t_ms > STALE_MS`처럼 **부호 없는 뺄셈 결과**를 비교합니다. `s.t_ms > last_t_ms + STALE_MS`는 `last_t_ms + 50`이 먼저 랩되면 거짓 stale이 됩니다. 테스트 6번이 이 실수를 잡습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

일을 **두 문맥으로 나누는 것**이 핵심입니다. ISR(µs 단위, 절대 막히면 안 됨)은 캡처만 하고, task(ms 단위, 늦어도 됨)는 판단을 합니다. 둘 사이 데이터는 SPSC 링으로 넘기고, 손실은 카운터로 넘깁니다.

<div class="chart" data-src="chart1.svg" data-caption="구조: ISR은 캡처·push·카운트만, task가 변환·히스테리시스·팬 제어. 공유 상태는 링(lock-free)과 overrun 카운터(마스킹 필요) 두 개"></div>

공유 상태가 두 종류이고 보호 방법도 다릅니다.

| 공유 상태 | 누가 쓰나 | 보호 방법 | 이유 |
|---|---|---|---|
| `g_ring.head` | ISR만 | `_Atomic` + release store | 쓰는 쪽이 하나라 락이 필요 없고, 순서만 보장하면 됨 |
| `g_ring.tail` | task만 | `_Atomic` + release store | 위와 같음 |
| `g_ring.buf[]` | ISR이 쓰고 task가 읽음 | head/tail의 acquire/release 쌍 | 데이터가 인덱스보다 먼저 보여야 함 |
| `g_overrun_count` | **ISR `++`, task `= 0`** | `volatile` + **critical section** | 읽기-지우기 두 단계가 끊기면 안 됨 |

### 4.2 volatile vs atomic vs critical section

- **`volatile`**: 컴파일러가 매번 메모리에서 다시 읽고 쓰게 합니다. 하드웨어 레지스터(`ADC_DATA`)나 ISR이 바꾸는 플래그에 필요합니다. 하지만 **원자성도, 다른 변수와의 순서도 보장하지 않습니다.** `cnt++`는 여전히 load-add-store 세 단계입니다.
- **C11 atomic + memory order**: `atomic_store_explicit(&head, h+1, memory_order_release)`는 "이 store 전에 쓴 `buf[]` 내용이 먼저 보인다"를 보장하고, 소비자의 `memory_order_acquire` load와 짝을 이룹니다. 단일 코어 MCU에서는 사실상 **컴파일러 재배치 방지**이고, 멀티코어나 캐시가 있는 SoC에서는 `DMB` 같은 배리어로 바뀝니다. 자기 인덱스를 읽을 때는 `relaxed`로 충분합니다(자기만 씀).
- **critical section**(`irq_disable/enable`, Cortex-M이면 `__disable_irq()`·`PRIMASK` 또는 `BASEPRI`): **여러 단계를 하나로 묶을 때** 씁니다. 읽기-지우기(read-and-clear)가 대표 예입니다. 대안은 `atomic_exchange(&cnt, 0)`인데, Cortex-M3/M4에서는 `LDREX/STREX`로 구현됩니다. Cortex-M0에는 LDREX/STREX가 없어서 결국 마스킹을 씁니다.

<div class="chart" data-src="chart3.svg" data-caption="read-and-clear race: 마스킹이 없으면 사이에 끼어든 overrun++가 cnt = 0에 지워짐. 마스킹하면 IRQ가 pending 됐다가 enable 직후 실행되어 다음 poll에서 회수됨 (테스트 4)"></div>

마스킹을 해도 IRQ는 **사라지지 않고 미뤄집니다**(NVIC pending bit). 그래서 테스트 4에서 마스킹한 버전은 첫 poll이 2를 가져가고, enable 직후 실행된 ISR이 링이 꽉 차 있어 `cnt = 1`로 만들고, 두 번째 poll이 1을 더 가져가 `overruns_total = 3`이 됩니다. 마스킹하지 않은 버전은 3이 0으로 지워져 total이 2에서 멈춥니다. harness의 보존 법칙 `raised == processed + ring + overruns_total + g_overrun_count + coalesced`가 깨집니다(11 ≠ 10).

### 4.3 버스트와 overrun — 타임라인

<div class="chart" data-src="chart2.svg" data-caption="테스트 3: task가 200 ms 막힌 동안 IRQ 20개. 링 8칸이 70 ms에 가득 차고, #9–20(80–190 ms)은 버려지며 overrun 12. t=200 poll이 8개를 처리하고 total=12"></div>

- **drop-newest**라서 살아남는 건 가장 오래된 0–70 ms 샘플 8개입니다. 그래서 drain 후 `last_t_ms == 70`입니다.
- 다음 IRQ(#21, t=200)는 직전 처리 샘플(70)과 130 ms 떨어져 있어 **stale 규칙으로 streak가 리셋**됩니다. 데이터가 빠진 구간을 건너서 "연속 3회"를 이어 붙이면 안 되기 때문입니다.

### 4.4 손으로 따라가기 (trace) — 테스트 1

`high=850, low=800, n_confirm=3`, IRQ 10 ms 간격, task는 IRQ 2개마다 poll합니다.

| # | t_ms | code | c10 | 상태 | streak 후 | 이벤트 | poll |
|---|---|---|---|---|---|---|---|
| 0 | 1000 | 1639 | 820 | NORMAL | 0 | – | |
| 1 | 1010 | 1664 | 840 | NORMAL | 0 | – | poll 1 (2개 처리) |
| 2 | 1020 | 1679 | 852 | NORMAL | 1 | – | |
| 3 | 1030 | 1689 | 860 | NORMAL | 2 | – | poll 2 → 팬 아직 off |
| 4 | 1040 | 1701 | 870 | → OVERHEATED | 0 | **EV_OVERHEAT** | |
| 5 | 1050 | 1713 | 880 | OVERHEATED | 0 | – | poll 3 → `fan_set(true)`, log `{1040, 870}` |
| 6 | 1060 | 1651 | 830 | OVERHEATED | 0 (band) | – | |
| 7 | 1070 | 1602 | 790 | OVERHEATED | 1 | – | poll 4 |
| 8 | 1080 | 1589 | 780 | OVERHEATED | 2 | – | |
| 9 | 1090 | 1577 | 770 | → NORMAL | 0 | **EV_RECOVERED** | poll 5 → `fan_set(false)`, log `{1090, 770}` |
| 10 | 1100 | 1564 | 760 | NORMAL | 0 | – | |
| 11 | 1110 | 1564 | 760 | NORMAL | 0 | – | poll 6 |

포인트: 과열 이벤트는 poll 3(실제 시각 1060)에 처리되지만, 로그 시각은 **ISR이 찍은 캡처 시각 1040**입니다. 변환 예: `1701 * 3300 / 4096 = 1370` mV → `1370 - 500 = 870`.

나머지 테스트에서 나오는 숫자:
- 변환: `adc_to_c10(0) = -500`, `adc_to_c10(4095) = 2799`, `adc_to_c10(620) = -1` (499.5 mV를 499로 버림), `adc_to_c10(0xF000 | 1676) = 850` (상위 4비트 무시)
- 랩: 시작 `UINT32_MAX - 14` → 샘플 시각 4294967281, 4294967291, **5** → 이벤트 t=5
- stale: 0, 10, (60 ms 공백) 70, 80, 90 → streak가 70에서 다시 시작하므로 이벤트는 70이 아니라 **90**

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `ring_push`: 자기 인덱스 `head`는 `relaxed`, 상대 인덱스 `tail`은 `acquire`로 읽습니다(task가 슬롯을 다 읽고 돌려줬는지 확인). 데이터를 쓴 **다음에** `head`를 `release`로 공개합니다. `ring_pop`은 이를 거울처럼 뒤집은 형태입니다.
- ISR은 `ring_push` 실패 시 `g_overrun_count++`만 합니다. 단일 코어에서 task는 ISR을 선점하지 못하므로 ISR 쪽 `++`는 안전합니다. 같은 카운터를 **더 높은 우선순위 ISR**도 건드리면 그때는 ISR 쪽도 보호해야 합니다.
- critical section 안에는 **세 줄만** 있습니다. 마스킹 시간이 샘플 주기(10 ms)를 넘으면 두 번째 DRDY가 pending bit에 합쳐져(`g_irq_coalesced`) **카운트조차 안 되는 손실**이 생깁니다.
- 변환, stale 검사, detector, 로그는 모두 **마스킹 밖**에서 합니다. 링은 lock-free이기 때문입니다.
- 로그는 T1-1과 같은 `if (n < CAP) write; n++;` 패턴을 씁니다.

### 4.6 복잡도

- ISR **O(1)**: 명령어 수십 개. task poll **O(k)**: k는 그동안 쌓인 샘플 수(≤ `RB_SIZE`).
- 메모리 **O(RB_SIZE)**: 정적 할당. 샘플 하나가 8바이트(패딩 포함)라 링은 64바이트입니다.

---

## 5. 임베디드 관점으로 말할 것

- **ISR vs task 분리 기준**: ISR에는 "지금 안 하면 데이터가 사라지는 일"만 넣습니다(레지스터 읽기, 타임스탬프, 플래그 ack). "늦어도 되는 일"(변환, 필터, 판단, 로그, I/O)은 task로 보냅니다. ISR이 길면 **다른 IRQ의 지연(jitter)**이 늘고, 같은 IRQ의 다음 edge를 놓칩니다.
- **ISR 지연 예산**: Cortex-M3/M4는 IRQ 진입에 약 12 cycle이 들고, 이 ISR 본문은 수십 cycle입니다. 64 MHz에서 1 µs 미만이고, 100 Hz면 CPU 점유율은 0.01% 미만입니다. 예산이 빡빡해지는 건 샘플레이트가 수십 kHz로 올라갈 때입니다. 그때는 **DMA + half/full-transfer IRQ**로 바꿔 IRQ 횟수 자체를 줄입니다.
- **링 크기 정하기**: `N ≥ 샘플레이트 × 최악 task 지연 + 여유`, 그리고 2의 거듭제곱으로 올림합니다. 100 Hz에 최악 지연 60 ms면 6개, 여유를 더해 **8**입니다. 테스트 3처럼 200 ms 막히는 경우(예: flash sector erase)가 정상 동작 범위라면 20개 → **32**가 필요합니다. 크기는 "추측"이 아니라 **측정한 최악 지연**(GPIO 토글 + 로직 분석기, 또는 DWT cycle counter)으로 정합니다.
- **손실은 세고, 보고한다**: drop-newest + `overrun_count`는 "얼마나 잃었는지"를 알 수 있게 해 줍니다. 텔레메트리로 올리고, overrun이 0이 아니면 링 크기나 task 우선순위를 다시 봅니다. 과열 판단 쪽에서는 **stale 규칙**으로 빠진 구간을 넘어 streak를 이어 붙이지 않게 합니다.
- **타임스탬프는 캡처 시점에서**: task가 늦게 돌아도 이벤트 시각이 정확합니다. `millis()` 랩은 뺄셈 비교로 처리합니다.

### Linux로 옮기면 (Q27·Q28 연결)

같은 구조가 Linux 드라이버에도 그대로 있습니다. 개념 정리는 [Embedded Linux R1 Q27 — 인터럽트 처리: GIC부터 threaded IRQ까지](../../../../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html#q27-인터럽트-처리-gic부터-threaded-irq까지)와 [Q28 — 지연 작업 고르기](../../../../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html#q28-지연-작업-고르기-softirq-tasklet-workqueue-threaded-irq)를 보세요.

| 이 문제 (MCU) | Linux 대응 | 한 줄 설명 |
|---|---|---|
| `TEMP_DRDY_IRQHandler` (짧게) | `request_threaded_irq()`의 **hard handler** | 하드 IRQ 문맥이라 잠들 수 없음. 플래그 ack, 타임스탬프만 하고 `IRQ_WAKE_THREAD` 반환 |
| `temp_task_poll` | **threaded handler** / workqueue | 프로세스 문맥이라 I2C/SPI 읽기처럼 잠드는 일이 가능. 센서 드라이버는 보통 여기서 레지스터를 읽음 |
| SPSC ring + overrun | **`kfifo`** (단일 생산자·단일 소비자면 락 불필요), IIO **triggered buffer** | IIO는 `iio_triggered_buffer_setup()` + `iio_push_to_buffers_with_timestamp()`로 샘플과 타임스탬프를 kfifo 기반 버퍼에 넣음 |
| ISR에서 찍는 `millis()` | `iio_get_time_ns()`를 **hard handler에서** 기록 | threaded handler에서 찍으면 스케줄링 지연만큼 늦은 시각이 됨 |
| `irq_disable()` read-and-clear | `spin_lock_irqsave()` 또는 `atomic_xchg()` | 프로세스 문맥과 IRQ 문맥이 공유하는 변수는 `_irqsave` 계열로 보호 |
| `fan_set()` | hwmon / thermal zone + cooling device | 커널 thermal 프레임워크가 trip point(히스테리시스 포함)와 팬 제어를 대신 해 줌 |

---

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why not do the conversion and detection inside the ISR? | 계산 자체는 짧지만, ISR이 길어질수록 다른 IRQ 지연이 늘고 나중에 기능(로그, 통신)을 붙이기 어려워집니다. ISR은 캡처만 하고 판단은 task에서 하는 게 표준 구조입니다. 또 `fan_set`이 I2C 팬 컨트롤러라면 ISR에서는 아예 못 씁니다(블로킹). |
| Is `volatile` enough for the counter? | 아닙니다. `volatile`은 매번 메모리에 접근하게 할 뿐, `n = cnt; cnt = 0;`의 두 단계를 묶어 주지 않습니다. critical section이나 `atomic_exchange`가 필요합니다. |
| Replace the critical section with C11 atomics. | `n = atomic_exchange_explicit(&cnt, 0, memory_order_relaxed);`, ISR은 `atomic_fetch_add`를 씁니다. M3/M4에서는 LDREX/STREX 루프라 IRQ를 막지 않습니다. M0처럼 LDREX가 없으면 컴파일러가 라이브러리 호출(내부적으로 마스킹)을 넣습니다. |
| Drop-newest or overwrite-oldest? | 과열 판단은 **최신 값**이 더 중요하므로 overwrite-oldest가 나을 수 있습니다. 다만 SPSC에서 생산자가 `tail`을 건드리게 되어 lock-free가 깨집니다. 시퀀스 번호를 붙이거나 마스킹이 필요합니다. 이 문제는 단순성과 "손실을 센다"를 우선해서 drop-newest를 택했습니다. |
| What if a higher-priority ISR also increments the counter? | 그러면 낮은 ISR의 `cnt++`도 선점될 수 있으니 ISR 쪽도 atomic 또는 `BASEPRI` 마스킹이 필요합니다. 아니면 **카운터를 ISR별로 나눠서** 각자 한 명만 쓰게 만듭니다(SPSC 원칙). |
| How do you size the ring and prove it? | `rate × worst-case latency + margin`, 2의 거듭제곱으로 올림합니다. 최악 지연은 GPIO 토글 + 로직 분석기, 또는 링 **high-water mark**(최대 `head - tail`)를 기록해서 측정합니다. |
| Multi-core (e.g. ISR on M4, task on A-core)? | acquire/release가 실제 `DMB` 배리어가 됩니다. 공유 메모리의 캐시 일관성(non-cacheable 영역 또는 clean/invalidate)과 head/tail을 서로 다른 캐시라인에 두는 것(false sharing 방지)까지 챙겨야 합니다. |
| How would you test this on real hardware? | 신호 발생기로 센서 전압을 램프시키고, 팬 GPIO와 DRDY를 로직 분석기로 같이 찍어 이벤트 지연을 측정합니다. task에 인위적 지연을 넣어 overrun 카운터가 기대값만큼 오르는지 봅니다(fault injection). |

## 7. 자주 하는 실수

- `n = cnt; cnt = 0;`을 **마스킹 없이** 씀 → 테스트 4 실패(overrun 1개 증발)
- `ring_push`에서 **head를 먼저 올리고** 데이터를 씀 → 실제 HW에서는 task가 쓰레기 슬롯을 읽음(시뮬레이션에서는 안 보이지만 면접관은 봄)
- ISR에서 변환이나 detector까지 다 돌리고, task를 비워 둠 → 동작은 해도 구조 질문에서 감점
- 링이 꽉 찼을 때 **조용히 버림**(카운트 안 함), 또는 `RB_SIZE - 1`칸만 쓰는 구현인데 테스트는 8칸 기대 → 테스트 3 실패
- stale 검사를 `t > last + STALE_MS`로 씀 → 랩 근처에서 거짓 stale → 테스트 6 실패
- 타임스탬프를 task에서 `millis()`로 찍음 → 로그 시각이 1040이 아니라 1060 → 테스트 1 실패
- critical section 안에서 drain/변환까지 다 함 → 마스킹 시간이 길어져 DRDY edge가 pending bit에 합쳐짐(**세지도 못하는 손실**)
- 링 관련 연습은 [T2-1](../T2-1_ring_buffer/index.html), detector는 [T1-2](../T1-2_streaming_hysteresis/index.html), 구간 계산은 [T1-1](../T1-1_overheat_intervals/index.html)
