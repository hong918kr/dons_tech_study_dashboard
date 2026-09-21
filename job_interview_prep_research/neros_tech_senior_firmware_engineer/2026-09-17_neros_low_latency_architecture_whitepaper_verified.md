# Technical Whitepaper (Verified Edition): Low-Latency Embedded Architecture & System Design for High-Throughput Unmanned Platforms

> **Base**: Don's Gemini research (`2026-09-17_neros_low_latency_architecture_whitepaper_gemini.md`, kept unchanged) · **Reviewed & revised**: 2026-09-17 by Claude
> **Korean edition**: `2026-09-17_neros_low_latency_architecture_whitepaper_verified_kor.md`
> Every claim carries a tag — ✅ **Verified** (standard fact or confirmed source) · ✏️ **Modified** (corrected; original shown as *was:*) · ⚠️ **Unverified** (not confirmed; qualify before quoting) · 🧪 **Compile-tested** (code built with `-std=c11 -Wall -Wextra -Wpedantic`, run under ASan/UBSan)

---

## 0. Change Log

| # | Section | Status | What changed | Why it matters in the interview |
|---|---|---|---|---|
| 1 | 3.2 ring buffer code | ✏️ Modified · 🧪 | Replaced the macro `__asm__ volatile("dmb ish" ::: "memory")` and `volatile` indices with a compile-tested C11 `<stdatomic.h>` implementation | The original does not compile. Ring buffer is a **confirmed onsite topic** |
| 2 | 3.2 / 3.1 | ✏️ Modified | `volatile` does not make code lock-free; stated explicitly | "Is volatile enough?" — the correct answer is **no** |
| 3 | 3.3 overwrite policy | ✏️ Modified · 🧪 | Producer writing `tail` breaks SPSC ownership → replaced with drop-new + consumer skip, or a latest-value seqlock slot | Classic follow-up: "what if the producer overwrites?" |
| 4 | 3.1 cache-line padding | ✏️ Modified | Scoped to multi-core targets only; Cortex-M7 is single-core | Avoid over-engineering answers for an MCU |
| 5 | 1.1 FCC cores, companion link | ✏️ Modified | Typical FC = STM32 F4/F7/H7 (Cortex-M4/M7); MCU↔companion usually UART/SPI/USB/Ethernet, not PCIe | Cortex-R8 / Xtensa are SSD-controller cores (Don's background leaking in) |
| 6 | 2.1 latency budget | ✏️ Modified | Inner rate loop runs on gyro + PID; attitude estimation/EKF runs at a lower rate, not every inner-loop cycle | Shows real flight-stack structure (Betaflight-style) |
| 7 | 2.2 DMA double buffer | ✏️ Modified | Added: each half must be processed before DMA wraps back (deadline); UART RX of variable-length frames uses IDLE-line + DMA position, not fixed halves | "Zero contention" still has a timing contract |
| 8 | Bandit > 250 km/h, Board A/B, 5 s flashing | ⚠️ Unverified | Tagged; Board A/B came from a video summary Don received | Quote with "in an interview I watched…" |
| 9 | 4. Adam focus matrix | ✏️ Modified · ⚠️ | ISO 26262 is **not** in Adam's LinkedIn — moved to "inferred"; added confirmed keywords (IPC protocol, EOL test, platform reuse, OTA/PKI, BSP+CI) | Don't assert things about the interviewer |
| 10 | 4.1 #2 talking point | ✏️ Modified | Original asked Don to describe decoupling FW from silicon "during past high-volume ramps" — not on his résumé. Reframed to his real multi-architecture bring-up experience | Invented experience collapses under a Director deep-dive |
| 11 | 5. FreeRTOS heap | ✏️ Modified | `heap_4` is not "required"; flight-critical code prefers static allocation (`xTaskCreateStatic`, `heap_1` or no heap) | Precise answer on determinism |
| 12 | 5. Serialization | ✏️ Modified | Protocol Buffers need a no-heap runtime (e.g. nanopb) on MCUs; FlatBuffers read without allocation | Avoid "protobuf on an MCU" without qualification |
| 13 | Wording | ✏️ Modified | "1 clock cycle" → removes the division and makes timing constant; "cache trashing" → thrashing | Accuracy |

---

## 1. Neros Target Hardware & Firmware Architecture Analysis

Modern attritable unmanned aerial systems (UAS) operating in contested environments require a departure from traditional monolithic aerospace avionics. Platforms like Neros **Archer** (FPV strike) ✅, **Archer AI** (terminal guidance and GPS-denied position hold) ✅, and **Bandit** (counter-UAS interceptor for Class 2/3 threats) ✅ demand an architecture optimized for low latency, high mechanical stress, strong electromagnetic interference (EMI), and rapid mass-manufacturability.
- Bandit's speed "> 250 km/h" ⚠️ **Unverified** — appears only in a video summary; not in Neros press releases.

```
+-----------------------------------------------------------------------------------+
|                               SYSTEM ARCHITECTURE                                 |
+-----------------------------------------------------------------------------------+

 [Vision AI / Payload]          [RF Module / Ground Comm]         [GNSS / Magnetometer]
  (Companion SoC + NPU,           (C2 + video radio,               (GNSS: UART,
   embedded Linux)                 in-house at Neros)               baro/mag: I2C/SPI)
          │                                  │                               │
          │ UART / SPI / USB / Ethernet      │ UART (e.g. CRSF) / SPI        │
          ▼                                  ▼                               ▼
+───────────────────────────────────────────────────────────────────────────────────+
|                         FLIGHT CONTROLLER (FC)                                    |
|  - Real-time MCU: typically STM32 F4/F7/H7 (Cortex-M4/M7)                         |
|  - IMU on SPI; inner rate loop (gyro → filters → PID → mixer) at 1–8 kHz          |
|  - Attitude estimation (complementary / EKF) at a lower rate                      |
|  - BSP / HAL, scheduler, failsafe, blackbox logging                               |
+───────────────────────────────────────────────────────────────────────────────────+
          │                                  │
          │ DShot (incl. bidirectional)      │ ADC / GPIO / I2C
          ▼                                  ▼
   [4-in-1 ESC + BLDC motors]        [Power & Safety Management]
   (RPM telemetry back to FC)        (current/voltage sense, arming / safe-arm)
```
✏️ **Modified** — *was:* FCC core "Cortex-M7 / Cortex-R8 / Xtensa", companion link "PCIe / USB / High-Speed UART", EKF inside the 1–8 kHz inner loop.

### 1.1 Subsystem Decomposition & Interconnect Topologies

- **Flight Controller (FC):** Built around a deterministic real-time microcontroller, typically **STM32 F4/F7/H7 (Cortex-M4/M7, roughly 168–480 MHz)**. The **inner rate loop** (gyro → filtering → PID → mixer) runs at **1–8 kHz**; attitude estimation (complementary filter or EKF) typically runs at a lower rate. ✏️ **Modified** — *was:* "Cortex-M7 at 400–600 MHz or Cortex-R series … EKF + PID at 1–8 kHz". Cortex-R8 and Xtensa are storage/modem controller cores, not typical flight controllers.
- **Radio & Anti-Jam Transceiver:** Connected over UART (e.g. CRSF at 420 kbaud on ExpressLRS-class links) or SPI. Requires deterministic framing, CRC, and link-quality monitoring to adapt power/modes under jamming. ✅ **Verified** (general practice). Neros designs its C2 and video radios in-house ✅ **Verified** (neros.tech). Specific rates "921.6 kbps–3 Mbps" ⚠️ **Unverified** for Neros.
- **Electronic Speed Controllers (ESCs):** Drive BLDC motors with DShot300/600 generated by DMA-driven timer PWM; **bidirectional DShot** returns eRPM for RPM notch filtering. ✅ **Verified**. CAN/CAN-FD ESCs exist on larger platforms but are uncommon on small FPV craft. ✏️ **Modified** (scope note added).
- **Companion Compute / Vision (Archer AI):** An embedded-Linux SoC with an accelerator runs target tracking for terminal guidance. The MCU↔companion link is **usually UART, SPI, USB, or Ethernet**; PCIe to a microcontroller FC is rare. ✏️ **Modified**. When the RF link degrades near the target, command authority must hand over deterministically from pilot link to vision guidance. ✅ **Verified** (consistent with Neros's Archer AI description).

### 1.2 Multi-Silicon Portability & "Board A / Board B" Redundancy

Neros targets **1 million drones per year by 2028** ✅ **Verified** (Series C release). At that scale, dual-sourcing critical parts is a realistic need. The specific "Board A / Board B" practice ⚠️ **Unverified** — from a CTO-interview summary Don received.

- **Strict HAL / driver isolation** ✅ **Verified** (standard practice): control and state-machine code never touches register addresses; drivers present a common interface.

```c
typedef struct { int32_t x, y, z; } axis3_t;

typedef struct {
    int (*init)(void);
    int (*read_accel)(axis3_t *data);
    int (*read_gyro)(axis3_t *data);
} imu_driver_t;
```
✏️ **Modified** — added the `axis3_t` definition so the snippet is self-contained.

- **Build-time board configuration** ✅ **Verified** (standard practice): pin maps, bus assignments, and driver selection come from board configuration data and build flags, so swapping an MCU family (e.g. STM32 → NXP) does not touch control code.
- **Cross-board regression** ✅ **Verified** (standard practice): replay captured bus traces or run the same HIL suite on both boards to prove behavioral parity. *Wording modified* — "bit-for-bit validation nodes" was an unusual term.

### 1.3 Manufacturing-Aware Firmware & Silicon Bring-Up

- **In-line flashing & factory test modes** ✅ **Verified** (standard practice): flash MCU and external QSPI at the station; expose a factory test mode over SWD/JTAG or a secondary UART. Target "under five seconds" ⚠️ **Unverified** (illustrative figure).
- **Automated end-of-line (EOL) tests** ✅ **Verified** (standard practice): power-rail/shunt measurement, oscillator check, bus loopback on SPI/I2C, gate-driver continuity, sensor presence and calibration. *Term modified:* "Factory Acceptance Test (FAT)" usually means acceptance of factory equipment; **EOL / functional test (FCT)** is the precise term here.
- **In-field black-box logging** ✅ **Verified** (Betaflight blackbox is the common reference): circular logging to SPI flash of gyro, motor outputs, bus errors, and brownout/reset causes.

---

## 2. Low-Latency Deterministic Data Pipeline Architecture

To keep sensor-to-actuator latency low and predictable during aggressive maneuvers, the data path from IMU sample to motor command should be deterministic and avoid unnecessary copies. ✏️ **Modified** — *was:* "sub-millisecond … (>250 km/h)"; the speed figure is unverified and the goal stands on its own.

### 2.1 Latency Budget Breakdown (illustrative)

```
τ_total = τ_sensor + τ_bus + τ_ingest + τ_rate_loop + τ_dispatch
```

| Segment | Typical Timing | Determinism | Mitigation | Status |
|---|---|---|---|---|
| **Sensor ODR & internal filter** | 125–250 µs | Hardware bound | High-ODR gyro (≥ 8 kHz), minimal on-chip DLPF | ✅ Verified (typical) |
| **SPI transfer (burst read)** | 20–50 µs | Jitter-prone | SPI ≥ 10–20 MHz, burst read, DMA | ✅ Verified (typical) |
| **Ingest & buffering** | 5–15 µs | Locking risk | SPSC ring buffer (see §3); avoid copies in the hot path | ✅ Verified (typical) |
| **Inner rate loop** (filters + PID + mixer) | tens to low hundreds of µs | Must be bounded | Fixed-point or FPU, no dynamic allocation, bounded filter chain | ✏️ Modified |
| **Attitude estimation** (complementary / EKF) | runs at a lower rate | Varies | Decouple from inner loop; FPU; precompute | ✏️ Modified |
| **Actuator dispatch (DShot600)** | 16-bit frame ≈ 26.7 µs | Hardware bound | DMA → timer compare, no CPU per bit | ✅ Verified |

✏️ **Modified** — *was:* "State Estimation (EKF) 100–300 µs" inside the per-cycle sum with "Cache line pre-alignment". On a flight controller the rate loop acts on gyro directly each cycle; running a full EKF every inner-loop cycle is not the typical structure, and "cache lines" are not a meaningful lever on a Cortex-M4.

### 2.2 Interrupt Ingestion vs. Circular DMA Double-Buffering

Per-byte or per-sample interrupts at multi-kHz rates add context-switch overhead and jitter; on cores with caches they can also cause cache thrashing. ✏️ **Modified** — *was:* "cache trashing".

```
       Circular DMA Ping-Pong Buffer Architecture
   ┌──────────────────────┬──────────────────────┐
   │    Half-Buffer 0     │    Half-Buffer 1     │
   │    (Bytes 0..N-1)    │    (Bytes N..2N-1)   │
   └──────────────────────┴──────────────────────┘
              ▲                      ▲
              │                      │
       DMA Half-Transfer      DMA Transfer-Complete
        Interrupt (HT)            Interrupt (TC)
              │                      │
              ▼                      ▼
    [Process Buffer 0]     [Process Buffer 1]
    (While DMA fills 1)    (While DMA fills 0)
```

1. **Circular DMA, buffer size 2N** ✅ **Verified** (STM32 HAL/LL pattern).
2. **Half-Transfer (HT):** process half 0 while DMA writes half 1. ✅ **Verified**
3. **Transfer-Complete (TC):** process half 1 while DMA wraps to half 0. ✅ **Verified**
4. **No lock needed between DMA and software** ✅ **Verified** — *but with a timing contract:* each half must be consumed (or copied out) **before DMA wraps back into it**, otherwise data is silently overwritten. ✏️ **Modified** (contract added).
5. **Variable-length UART frames** (e.g. radio/telemetry): fixed halves don't align with frame boundaries. Use **circular DMA + UART IDLE-line interrupt**, read the DMA write position (`NDTR`), and push the new bytes into a ring buffer. ✏️ **Modified** (added).

---

## 3. Lock-Free SPSC Ring Buffer Design

Between an ISR and a task (or between two cores in AMP), mutexes cannot be taken inside the ISR. A single-producer single-consumer (SPSC) ring buffer passes data without locks and without blocking. ✅ **Verified**

### 3.1 Design Principles

- **Power-of-two capacity** ✅ **Verified**: index with `& (CAPACITY - 1)` instead of `%`, removing a division from the hot path and making timing constant. ✏️ **Modified** — *was:* "reducing index calculation to 1 clock cycle".
- **Free-running indices** ✅ **Verified**: head and tail only increase; `head - tail` (unsigned) is the fill level; mask only when indexing the array. Correct because a power-of-two capacity divides 2^32.
- **Ownership rule — the reason it is lock-free** ✏️ **Modified** (made explicit): **producer writes only `head`, consumer writes only `tail`.** Each side reads the other's index but never writes it.
- **Publish order** ✅ **Verified**: write the data first, then advance the index — the other side can never see an index that points at unwritten data.
- **`volatile` is not enough** ✏️ **Modified** (added): `volatile` stops the compiler from caching a value; it does **not** provide atomicity or cross-core memory ordering. Single-core ISR↔main loop: correct write order is sufficient on typical MCUs. Multi-core: use C11 atomics (below).
- **Cache-line padding** ✏️ **Modified**: only relevant on **multi-core cores with caches** (e.g. Cortex-A, heterogeneous SoCs) to avoid false sharing between producer and consumer cores. A single-core Cortex-M4/M7 doesn't need it. *Was:* stated as a general rule including "dual Cortex-R or M-core".

### 3.2 Embedded C Implementation 🧪 Compile-tested

✏️ **Modified** — replaces the original, which did not compile (`__asm__ volatile("dmb ish" ::: "memory")` is not C) and relied on `volatile` for lock-freedom.

```c
#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define RING_CAPACITY 512u               /* MUST be a power of two */
#define RING_MASK     (RING_CAPACITY - 1u)

/* Ownership rule that makes this lock-free:
 *   producer (e.g. UART RX ISR) writes ONLY head
 *   consumer (e.g. main loop)   writes ONLY tail
 * Indices run free (never wrapped); unsigned subtraction gives the fill level
 * because RING_CAPACITY divides 2^32.                                          */
typedef struct {
    uint8_t buf[RING_CAPACITY];
    atomic_uint_fast32_t head;           /* written by producer only */
    atomic_uint_fast32_t tail;           /* written by consumer only */
    uint32_t dropped;                    /* producer-owned overflow counter */
} spsc_ring_t;

static inline void ring_init(spsc_ring_t *r) {
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
    r->dropped = 0u;
}

/* Producer side. Drop-new policy: never touches tail. */
static inline bool ring_push(spsc_ring_t *r, uint8_t byte) {
    uint_fast32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);  /* own index */
    uint_fast32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);  /* peer index */
    if ((uint32_t)(h - t) >= RING_CAPACITY) {
        r->dropped++;
        return false;
    }
    r->buf[h & RING_MASK] = byte;                                             /* 1) data first  */
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);            /* 2) then publish */
    return true;
}

/* Consumer side. */
static inline bool ring_pop(spsc_ring_t *r, uint8_t *out) {
    uint_fast32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint_fast32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return false;
    *out = r->buf[t & RING_MASK];                                             /* 1) read first  */
    atomic_store_explicit(&r->tail, t + 1u, memory_order_release);            /* 2) then consume */
    return true;
}

static inline uint32_t ring_count(spsc_ring_t *r) {
    return (uint32_t)(atomic_load_explicit(&r->head, memory_order_acquire) -
                      atomic_load_explicit(&r->tail, memory_order_acquire));
}

#endif /* SPSC_RING_H */
```
🧪 Tested: empty pop, fill to capacity, drop on full (`dropped == 1`), FIFO order, 5,000 push/pop cycles across index wrap — `-Wall -Wextra -Wpedantic -O2` clean, ASan/UBSan clean.

**Interview note:** on a single-core MCU where the ISR is the producer, you can present the same code with plain `uint32_t` indices and explain why the write order alone is sufficient; then mention atomics for multi-core. That sequence shows you understand *why*, not just *what*.

### 3.3 Overflow Policies

- **Drop-new (telemetry, commands, logs)** ✅ **Verified**: when full, the producer discards the new item and increments `dropped`. It never touches `tail`.
- **"Overwrite oldest" by moving `tail` from the producer** ✏️ **Modified** — *was:* recommended (`tail = head - CAPACITY + 1`). **This breaks the SPSC ownership rule**: the consumer also writes `tail`, so producer and consumer race on the same variable.
- **When only the newest value matters (IMU, odometry)** — two correct options:
  1. **Consumer-side skip** ✅ **Verified**: producer stays drop-new; the consumer drains everything available and keeps only the last element.
  2. **Latest-value slot (seqlock)** 🧪 **Compile-tested**: a single slot plus a sequence counter; the writer never blocks, the reader retries if it raced a write.

```c
typedef struct { int32_t gx, gy, gz; uint32_t t_us; } imu_sample_t;

typedef struct {
    atomic_uint_fast32_t seq;            /* odd = write in progress */
    imu_sample_t v;
} latest_imu_t;

static inline void latest_write(latest_imu_t *s, const imu_sample_t *in) {   /* single writer */
    uint_fast32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);   /* odd: writing */
    s->v = *in;
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);   /* even: stable */
}

static inline void latest_read(latest_imu_t *s, imu_sample_t *out) {
    uint_fast32_t a, b;
    do {
        a = atomic_load_explicit(&s->seq, memory_order_acquire);
        *out = s->v;
        b = atomic_load_explicit(&s->seq, memory_order_acquire);
    } while ((a & 1u) || a != b);
}
```
Caveat: if the reader is a *lower* priority than the writer on a single core it can retry; if the reader can preempt the writer mid-write (reader in a higher-priority ISR), briefly mask the writer's interrupt instead.

---

## 4. Architectural Dialogue Strategy for the Director Interview

Adam Kibit is a Director of Firmware with a long automotive embedded background. Focus on scalability, determinism, manufacturing, and lifecycle reliability rather than drone aerodynamics. ✅ **Verified** (LinkedIn experience page)

```
       ADAM KIBIT — FOCUS MATRIX
       ┌────────────────────────────────────────────────────────────┐
       │ CONFIRMED from his experience (✅):                         │
       │ - Inter-processor communication protocol design (JCI)       │
       │ - End-of-line test app & automated test systems (JCI)       │
       │ - Modular platform reused across product tiers (Visteon)    │
       │ - OTA pipeline, secure update, PKI (Aeris, Visteon, FF)     │
       │ - BSP integrated into CI with unit tests (JCI)              │
       │ INFERRED, not stated (⚠️):                                  │
       │ - Functional safety mindset (e.g. ISO 26262)                │
       │ - HV/LV noise integration concerns from EV work             │
       └────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
        DON'S VALUE ALIGNMENT (from résumé ✅)
       ┌────────────────────────────────────────────────────────────┐
       │ - New-silicon bring-up → NPI → MP (Apple, Solidigm)         │
       │ - Interface root cause: PCIe, I2C, SPMI, RFFE (Apple)       │
       │ - Factory test-node architecture (Apple)                    │
       │ - Chamber reliability test platform & SDK (SK hynix)        │
       │ - Telemetry-based debug feature (SK hynix)                  │
       └────────────────────────────────────────────────────────────┘
```
✏️ **Modified** — *was:* ISO 26262 listed as Adam's focus without qualification; Don's column listed "Bare-Metal / RTOS Deterministic Scheduling", which is not explicit on the résumé.

### 4.1 Key Discussion Vectors

#### 1. Real-time determinism vs. system interconnects ✅ Verified (fits Don's résumé)
- *Frame:* Moving between bare-metal loops, an RTOS, and a Linux companion computer.
- *Talking point:* High-speed buses lose signal margin under shock, vibration, and temperature. Don's experience with oscilloscopes and protocol analyzers separates physical-layer problems (jitter, framing errors) from firmware timeouts — the same discipline applies to a vibrating airframe.

#### 2. Abstracting firmware for component agility (Board A / Board B) ✏️ Modified
- *Frame:* Scaling production while supply chains shift.
- *Talking point (revised to Don's real experience):* "I've brought firmware up across different architectures — Cortex-R, Cortex-M, Xtensa — and across FPGA prototypes to production silicon. That taught me where the hardware boundary has to sit so the rest of the code doesn't chase silicon changes. I'd apply the same principle to a second-source board: functional HAL, board configuration as data, and both boards in the same CI and HIL runs."
- *Was:* "Detail how you decoupled production firmware from specific silicon registers during past high-volume ramps… zero architectural rewrites." — **not on Don's résumé; do not claim it.**

#### 3. Manufacturing calibration & diagnostics ✅ Verified (strongest match with Adam)
- *Frame:* Connecting a 12-engineer firmware team ✅ (recruiter) with a factory where over half of ~300 employees are operators/technicians ✅ (recruiter). ✏️ **Modified** — *was:* "hundreds of production technicians".
- *Talking point:* Factory cycle time drives firmware design — fast boot, bus self-test, automatic calibration, diagnostics without a debug rig, and results fed back to design. Tie to Don's Apple test-node work and Adam's EOL test background.

---

## 5. Technical Study Roadmap

### Domain 1: FreeRTOS Internals & Concurrency
- **Scheduling** ✅ **Verified**: preemptive fixed-priority vs. cooperative/time-slicing; tick interrupt cost; tickless idle (`vPortSuppressTicksAndSleep`).
- **Memory management** ✏️ **Modified**: `heap_1` (allocate only), `heap_2` (no coalescing), `heap_3` (wraps malloc), `heap_4` (first-fit with coalescing), `heap_5` (multiple regions). For flight-critical code the preferred answer is **static allocation** (`xTaskCreateStatic`, `xQueueCreateStatic`) or `heap_1` allocate-at-boot; `heap_4` reduces but does not eliminate fragmentation. *Was:* "`heap_4` … required for zero-fragmentation avionics".
- **Priority inversion** ✅ **Verified**: low-priority task holds a mutex, medium-priority task preempts, high-priority task waits. FreeRTOS mutexes implement priority inheritance; binary semaphores do not.
- **Task notifications** ✅ **Verified**: `xTaskNotifyGive` / `ulTaskNotifyTake` (ISR variant `xTaskNotifyGiveFromISR`) are lighter than semaphores for ISR→task wakeups.

### Domain 2: MCU Memory Models & Buses
- **TCM on Cortex-M7** ✅ **Verified**: ITCM for time-critical code, DTCM for deterministic data/stack. Note: on **STM32H7, DTCM is not reachable by DMA** — DMA buffers must live in AXI/AHB SRAM; check the memory map for the exact part. ✏️ **Modified** (*was:* "DTCM for deterministic stack/DMA data").
- **Cache coherence with DMA** ✅ **Verified**: CMSIS `SCB_CleanDCache_by_Addr` before DMA transmit, `SCB_InvalidateDCache_by_Addr` after DMA receive — or place DMA buffers in a non-cached region.
- **MPU** ✅ **Verified**: stack guard regions, null-pointer page, separating privileged and unprivileged regions. *Wording modified:* the MPU turns these bugs into a **MemManage fault you can log and recover from**, rather than "without crashing".

### Domain 3: Low-Level Communication Protocols
- **SPI** ✅ **Verified**: CPOL/CPHA modes, chip-select setup/hold timing, trace capacitance limits, noise near motor currents.
- **I2C** ✅ **Verified**: bus lockup when a slave holds SDA low after a master reset; recover by clocking SCL up to 9 times, then generating STOP.
- **PCIe** ✅ **Verified** (relevant to Don's background; less common on MCU flight controllers): Root Complex vs. Endpoint, MSI/MSI-X, address translation (iATU), Lane Margining at the Receiver (PCIe 4.0+).
- **DShot** ✅ **Verified**: DShot300 bit time ≈ 3.33 µs, DShot600 ≈ 1.67 µs, 16-bit frame (11-bit throttle + telemetry bit + 4-bit CRC); generated with DMA into timer compare registers.

### Domain 4: Control Loops & Flight Dynamics
- **Rate-monotonic priorities** ✅ **Verified**: shorter period → higher priority. Example (illustrative): gyro/rate loop 125–250 µs → radio 2–10 ms → telemetry 50 ms+.
- **PID details** ✅ **Verified**: integrator clamping (anti-windup), derivative on measurement to avoid derivative kick, D-term low-pass filtering.
- **Vibration rejection** ✅ **Verified** / ⚠️ range: prop-induced vibration scales with motor RPM; dynamic and RPM notch filters track it. The "200–800 Hz" range is ⚠️ **illustrative** and depends on frame size and RPM. Complementary filter vs. EKF: the EKF handles multi-sensor fusion and sensor faults better at higher compute cost.

### Domain 5: Edge IPC & Vision Handoff
- **Shared-memory ring buffers** ✅ **Verified** for a companion SoC with shared RAM (e.g. heterogeneous SoC). ✏️ **Modified** scope: between a **separate** MCU and a companion computer there is no shared memory — use a framed link (UART/SPI/Ethernet) with COBS + CRC or similar.
- **Serialization** ✏️ **Modified**: FlatBuffers can be read without allocation ✅; Protocol Buffers on an MCU need a static-allocation runtime such as **nanopb** ✅. *Was:* grouped as heap-free without qualification.
- **Command-authority FSM** ✅ **Verified** (design pattern): pilot RF link → autonomous terminal guidance → failsafe (hover / disarm per policy), with explicit entry/exit conditions and timeouts.

---

## 6. Sources & Cross-References

- Neros Series C release (Archer AI, Bandit, 1M units/yr by 2028) — https://www.prnewswire.com/news-releases/neros-raises-250m-series-c-at-2-5b-valuation-to-scale-autonomous-and-interceptor-drone-programs-302848736.html
- Neros website (in-house radios, flight computer) — https://www.neros.tech/
- Adam Kibit LinkedIn experience page (viewed 2026-09-16) — summarized in `neros_hm_adam_technical_prep_2026-09-18.md` §2
- Recruiter call 2026-09-16 (team size, company size, onsite format) — `neros_tech_senior_firmware_engineer_context.md` §5
- Compile-tested code — mirrors `practice/solutions/01_ring_logging.c` (SPSC) and `practice/solutions/06_isr_timing.c` (retry snapshot pattern)
- Architecture background — `practice/notes/00_drone_architecture.md`
