# Lesson writer brief — track BM (베어메탈 펌웨어), **Medium** level (BM-M01–M10, M12, M13)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/BM_B_LESSON_WRITER_BRIEF.md (BM track base: shared
mock vocabulary, RM0090 addresses as mock definition + hedging, code rules, QEMU/VM rules, quality bar) →
ROOT/curriculum/COMM_M_LESSON_WRITER_BRIEF.md "Overrides for Medium" + "Accuracy rules" sections (ignore its COMM
lanes) → **this file** (BM-M overrides + per-item lanes). Read all of them; this file wins on conflicts.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics roles (Anduril, Rivian, Tesla, Apple, NVIDIA). BM Medium = the "real driver" layer of a
firmware interview: "NVIC_SetPriority(37, 5) 는 어느 레지스터 어느 비트?", "ICER 에 |= 쓰면?", "ISR 끝에서 플래그
지웠는데 왜 두 번 들어오나?", "모터 PWM 과 UART 중 누가 높은 우선순위?", "버튼 바운스 어떻게 잡나?", "TXE 인터럽트
언제 켜고 끄나?", "center-aligned 면 주파수가?", "캡처랑 오버플로가 동시에 pending 이면?", "DMA HT/TC 로 더블 버퍼링",
"윈도 워치독을 너무 일찍 킥하면?", "RTC TR 읽고 DR 읽는 사이 자정이 지나면?", "fault 났을 때 모터 출력은?".
Every lesson = one interview-style coding problem built as a deterministic C mock/model with checked tests
(M02 and M13 = real Cortex-M firmware on QEMU mps2-an385 in the Lima VM).

## Overrides for Medium
- Output: ROOT/lessons/BM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, *.h, *.ld, run_qemu.sh, captured *.txt). Front matter `updated: 2026-11-11`, `time:` 60분–90분.
  Title line `# <ID> <title exactly as in BM.json>`.
- md **450–700 lines**, ≥2 svg figures (≥3 encouraged), **면접 Q&A ≥ 6** (aim 7–9), 추가 연습 3–6 ending with
  `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd. §3 `<!-- code: starter.c -->`,
  §6 `<!-- code: solution.c diff=starter.c -->`.
- Parts escalate: A = core register mechanic / formula, B = faults & edge cases interviewers probe (every fault the
  task names is injected and its outcome asserted), C = firmware-realistic use (driver API used from ISR + main loop,
  event-driven simulation, timing budget, a log a bring-up engineer would read).
- Tests: exhaustive where cheap, else fixed-seed xorshift32 (≥ a few thousand cases) + named edge/fault cases.
  "N/M passed", exit 1 on any failure, solution/practice < 2 s, no wall-clock timing in CHECKs. Heavy sweeps go in
  demo_*.c with REAL output pasted.
- solution.c = starter.c with ONLY TODO bodies filled. Starters FAIL (exit 1), never hang (bounded loops / step caps).
- **Self-contained**: no #include across lessons. Embed a compact copy of what you need and label it:
  "(BM-B02 mock 축약판)", "(BM-B14 타이머 모델 축약판)", "(BM-M08 DMA 모델 축약판)" etc.
- **Interrupt simulation convention** (so the M lessons read as one system): the mock has a tiny "NVIC-lite":
  peripheral models raise a line (`irq_line_set(IRQn, level)`), and the test's event loop `sim_step()` advances
  one mock clock / tick and, if a line is enabled+pending and not masked, calls the registered handler
  synchronously (single-threaded, deterministic; no pthreads/signals). Interleaving tests inject "ISR fires
  between these two accesses" via an access-count hook (BM-B04 style). M01 owns the full NVIC register model;
  others only need the lite version.

## Cross-references
- BM Basic B01–B14 ALL written (ROOT/lessons/BM/BM-Bxx/) — read the one(s) you build on and cite by id:
  B02 mock framework, B03 RCC gating + dummy read, B04 W1C / RMW-vs-ISR race + BSRR, B08 real QEMU boot
  (run_qemu.sh pattern, bm_b08.ld, handlers.c), B09 UART/semihosting printf on mps2, B11 GPIO, B12 polling UART +
  BRR + TXE vs TC, B13 SysTick + wrap-safe time, B14 timer PSC/ARR + UG/preload. BM-M11 (low power) written 11-08.
- Also written: C-M14 (ISR↔main shared data, PRIMASK), ARM-M04/M05/M11, COMM-B01–B14 + COMM-M01–M15 (COMM-M11 =
  UART DMA circular + IDLE), DS ring-buffer lessons (DS-B*), ARCH-M12 (DMA cache coherency), RTOS-B/M, HWLAB-B/M,
  SIV-B/M/A, TOOL-M09. Check `ls ROOT/lessons/<TRACK>` before citing.
- Not written yet (cite by id as "다음 단계" only): ARM-M08 (asm vector table), ARM-M10 (PRIMASK/BASEPRI critical
  section), ARM-M12 (HardFault CFSR/HFSR decoder + PC recovery), ARM-A08 (interrupt latency), TEST-B09 (watchdog
  task check-in), BM-A01…A11.
- The other 11 BM-M items are being written IN PARALLEL right now. Stay in your lane.

## Addresses (RM0090 / ARMv7-M 기준 대표값 — hedge "RM/ARM ARM 확인 권장"; tests' truth = your mock's definition)
- NVIC: ISER0 0xE000E100 (+4·n), ICER0 0xE000E180, ISPR0 0xE000E200, ICPR0 0xE000E280, IABR0 0xE000E300,
  IPR0 0xE000E400 (one byte per IRQ, byte-accessible on HW), STIR 0xE000EF00. SCB: ICSR 0xE000ED04, VTOR 0xE000ED08,
  AIRCR 0xE000ED0C (VECTKEY 0x05FA in [31:16], PRIGROUP [10:8], SYSRESETREQ bit2), SCR 0xE000ED10,
  CCR 0xE000ED14 (UNALIGN_TRP bit3, DIV_0_TRP bit4), SHPR1–3 0xE000ED18/1C/20, SHCSR 0xE000ED24 (MEMFAULTENA 16,
  BUSFAULTENA 17, USGFAULTENA 18), CFSR 0xE000ED28, HFSR 0xE000ED2C, MMFAR 0xE000ED34, BFAR 0xE000ED38.
  STM32F4 `__NVIC_PRIO_BITS` = 4 (priority value lives in IPR bits [7:4]).
- EXTI 0x40013C00 (IMR 0x00, EMR 0x04, RTSR 0x08, FTSR 0x0C, SWIER 0x10, PR 0x14 = rc_w1); SYSCFG 0x40013800
  (EXTICR1–4 at 0x08–0x14, 4 bits per line).
- USART CR1 interrupt enables: PEIE 8, TXEIE 7, TCIE 6, RXNEIE 5, IDLEIE 4. F4 ORE/IDLE/NF/FE clear = read SR then
  read DR.
- TIMx: CR1 (CEN 0, UDIS 1, URS 2, OPM 3, DIR 4, CMS [6:5], ARPE 7), SMCR 0x08, DIER 0x0C (UIE 0, CC1IE 1…),
  SR 0x10 (UIF 0, CC1IF 1…, CC1OF 9…; rc_w0), EGR 0x14, CCMR1 0x18 (OC1M [6:4], OC1PE 3; input: CC1S [1:0],
  IC1PSC [3:2], IC1F [7:4]), CCER 0x20 (CC1E 0, CC1P 1, CC1NE 2, CC1NP 3), CNT 0x24, PSC 0x28, ARR 0x2C,
  CCR1 0x34…, BDTR 0x44 (TIM1/8: DTG [7:0], MOE 15).
- ADC1 0x40012000: SR 0x00 (AWD 0, EOC 1, JEOC 2, JSTRT 3, STRT 4, OVR 5), CR1 0x04 (SCAN 8, RES [25:24], OVRIE 26),
  CR2 0x08 (ADON 0, CONT 1, DMA 8, DDS 9, EOCS 10, SWSTART 30), SMPR1 0x0C (ch10–18), SMPR2 0x10 (ch0–9) 3 bits each:
  3/15/28/56/84/112/144/480 cycles, SQR1 0x2C (L [23:20] = n−1), SQR2 0x30, SQR3 0x34 (SQ1 [4:0]…), DR 0x4C.
  T_conv = T_sample + 12 ADCCLK (12-bit). VREFINT_CAL @ 0x1FFF7A2A (F4, measured at VDDA = 3.3 V, 30 °C — hedge).
- DMA2 0x40026400 (DMA1 0x40026000): LISR 0x00, HISR 0x04, LIFCR 0x08, HIFCR 0x0C; stream x at 0x10 + 0x18·x:
  SxCR (EN 0, DMEIE 1, TEIE 2, HTIE 3, TCIE 4, PFCTRL 5, DIR [7:6], CIRC 8, PINC 9, MINC 10, PSIZE [12:11],
  MSIZE [14:13], PL [17:16], DBM 18, CT 19, CHSEL [27:25]), SxNDTR +0x04, SxPAR +0x08, SxM0AR +0x0C, SxM1AR +0x10,
  SxFCR +0x14. ISR flag groups per stream at bit offsets 0, 6, 16, 22 (streams 0–3 in LISR, 4–7 in HISR); in a group:
  FEIF 0, DMEIF 2, TEIF 3, HTIF 4, TCIF 5.
- IWDG 0x40003000: KR 0x00 (0x5555 unlock, 0xAAAA reload, 0xCCCC start), PR 0x04 (/4·2^PR, PR 0..6), RLR 0x08
  (12-bit), SR 0x0C (PVU 0, RVU 1). LSI nominal 32 kHz (F4 datasheet range ~17–47 kHz — hedge).
  WWDG 0x40002C00: CR (T [6:0], WDGA 7), CFR (W [6:0], WDGTB [8:7], EWI 9), SR (EWIF 0). Counter clock =
  PCLK1/4096/2^WDGTB; reset when T6 clears (0x40→0x3F) or CR written while counter > W.
  DBGMCU_APB1_FZ 0xE0042008 (DBG_WWDG_STOP 11, DBG_IWDG_STOP 12). RCC_CSR 0x40023874 (RMVF 24, BORRSTF 25,
  PINRSTF 26, PORRSTF 27, SFTRSTF 28, IWDGRSTF 29, WWDGRSTF 30, LPWRRSTF 31).
- RTC 0x40002800: TR 0x00, DR 0x04, CR 0x08, ISR 0x0C (ALRAF 8, INIT 7, INITF 6, RSF 5), PRER 0x10 (PREDIV_A [22:16],
  PREDIV_S [14:0]), ALRMAR 0x1C (MSK1 7, MSK2 15, MSK3 23, MSK4 31), WPR 0x24 (0xCA then 0x53), SSR 0x28.
  PWR_CR 0x40007000 DBP bit 8. ck_spre = RTCCLK / ((PREDIV_A+1)(PREDIV_S+1)).

## Lanes (stay in yours)
- **M01 NVIC basics (mac)**. Owns the full NVIC register mock: ISER/ICER (write-1-to-set / write-1-to-clear, read =
  enable state, writing 0 = no effect), ISPR/ICPR same for pending, IABR read-only, IPR bytes with only the top
  `__NVIC_PRIO_BITS` implemented (low bits read 0), STIR (write IRQ number → pending). Implement
  `nvic_enable_irq/disable_irq/set_priority/get_priority/set_pending/clear_pending/get_pending` for IRQ 0–95
  (index = irq>>5, bit = irq&31; IPR word = irq>>2, byte lane = irq&3, value = prio << (8−PRIO_BITS)), negative
  IRQn → SHPR (SysTick −1, PendSV −2, SVCall −5 …) as Part B/C. The mock is 32-bit-access only → IPR updates are a
  word RMW; explain HW allows byte access (CMSIS uses `NVIC->IP[n]`). Exhaustive test IRQ 0–95 × prio 0–15 that ONLY
  the target bit/byte changed. Classic bugs injected and caught: `ICER |= bit` (disables every enabled IRQ),
  `prio` written without the shift (lands in unimplemented bits → reads back 0), using IRQ number as word index.
  Part C: "late flag clear" — model a peripheral whose interrupt line deasserts N bus cycles after the flag-clear
  write (write buffer/bus bridge latency, hedged), NVIC samples the line at exception return → ISR re-entered
  (count runs); fixes: clear first in the ISR, or read-back of the peripheral register / DSB before return.
  Cite ARM-M10 (masking) / ARM-A08 (latency) as next steps. Lane: no PRIGROUP preemption (M02).
- **M02 priority design + preemption on QEMU (qemu)**. Real firmware on `qemu-system-arm -M mps2-an385`
  (Cortex-M3) in the Lima VM, B08-style run_qemu.sh (copy to VM /tmp/BM-M02_work, freestanding, semihosting exit).
  FIRST verify on QEMU: how many priority bits are implemented (write 0xFF to an IPR byte, read back), how many
  external IRQs exist, and which IRQ numbers are not wired to an mps2 device (read QEMU hw/arm/mps2.c of v10.2.x if
  available, or probe) — use free lines for your fake MOTOR_PWM / ADC / UART IRQs; PendSV/SysTick via SHPR3 and
  ICSR PENDSVSET/PENDSTSET. Design a priority table with reasons (motor PWM update = hard deadline highest, ADC, UART
  RX before overrun, SysTick, PendSV lowest — your table, defended) and a PRIGROUP setting (AIRCR with VECTKEY).
  Tests in firmware: an ISR pends another IRQ via NVIC ISPR/STIR and logs enter/exit into a RAM trace buffer;
  expected traces asserted: higher group priority preempts (nested), same group-prio different sub-priority does NOT
  preempt but is ordered by sub-priority when both pending, equal priority → lower IRQ number first, PendSV runs last
  (tail-chained). Firmware prints the trace via semihosting and exits 0/1. starter = TODO priority/PRIGROUP setup
  (all 0) → traces mismatch → exit 1. Paste real QEMU output. PRIMASK/BASEPRI critical sections = ARM-M10 (cite
  only). Show `info`-style evidence where useful (gdb-multiarch batch reading NVIC regs) — optional.
- **M03 EXTI + button debounce (mac)**. Compact B02 mock + EXTI model (RTSR/FTSR edge detect on an input-pin
  timeline, PR rc_w1, IMR mask, SWIER) + SYSCFG_EXTICR line→port mapping. Inputs = sample arrays at 0.1 ms or 1 ms
  resolution with injected bounce bursts (fixed-seed), glitches, stuck. Two debouncers: (1) EXTI edge → mask line,
  start a timer (one-shot, compact B14/B13 style) → recheck level after T_db → emit event → unmask (also clear PR
  before unmask — show the spurious-event bug if you don't); (2) periodic sampling (1 ms tick) with integrator /
  shift-register (Ganssle-style, hedged citation). Events: PRESS, RELEASE, LONG_PRESS (≥ e.g. 800 ms, once per
  press), maybe REPEAT. Tests: glitch shorter than threshold ignored, bounce on press and on release → exactly one
  event each, long press fires once, press during masked window, both methods agree on clean+bouncy fixtures; count
  ISR invocations (edge storm cost) vs sampling CPU cost in a small table. Lane: GPIO init = B11 (cite).
- **M04 interrupt-driven UART (mac)**. Compact B02 mock + USART model (DR/SR, RXNE/TXE/TC/ORE, CR1 RXNEIE/TXEIE/TCIE,
  shift register N ticks per byte like B12) + NVIC-lite. Driver: `uart_write(buf,n)` non-blocking into TX ring (SPSC,
  power-of-2, free-running indices, `volatile`), ISR: RXNE → RX ring (full policy: drop-newest + counter; state the
  alternative), ORE → clear via SR-then-DR read + counter; TXE → pop TX ring to DR, disable TXEIE when ring empty.
  Main enables TXEIE after pushing. Tests: echo stream of thousands of bytes via sim_step, RX burst faster than main
  drains → drop counter exact, ORE injected when ISR "late" (mask line for K ticks) → counter, TXE storm if TXEIE
  left on with empty ring (count ISR calls — bug demo), the lost-wakeup race: main checks "ring empty" → ISR runs and
  disables TXEIE → main pushes but thinks TX is active → data stuck; reproduce with access-count interleave hook, fix
  (always enable TXEIE after push; enabling is idempotent). Part C: `uart_flush()` waits TC (cite B12) and
  printf-style log through the ring. Cite DS ring buffer lesson, COMM-M11 (DMA+IDLE), C-M14.
- **M05 PWM (mac)**. Compact B14 timer model extended with CCR1–CCR2 compare outputs: per timer clock compute CNT
  (up / down / center-aligned CMS=01..11) and OCxREF for PWM mode 1 (110) / mode 2 (111), polarity CCxP, output
  enable. Generate one-period waveform arrays and assert: f_pwm = f_tim / ((PSC+1)(ARR+1)) edge-aligned and
  f_tim / ((PSC+1)·2·ARR) center-aligned (verify with your model, state the exact formula your model obeys), duty
  0% (CCR=0) and 100% (CCR ≥ ARR+1) edge cases, CCR preload (OCxPE) → duty change applied at update (no glitch) vs
  immediate (glitch demo: a short/long pulse in the transition period). Complementary output with dead time:
  `dtg_encode(dead_ns, f_dts)` / `dtg_decode` for the 4 DTG ranges (DTG[7:5]=0xx: DTG·tDTS; 10x: (64+DTG[5:0])·2·tDTS;
  110: (32+DTG[4:0])·8·tDTS; 111: (32+DTG[4:0])·16·tDTS — RM0090 TIM1 BDTR, hedge) — exhaustive round-trip, pick
  nearest ≥ requested; waveform shows CH and CHN never high together; duty shorter than dead time → pulse vanishes;
  MOE bit. Part C: 3-phase/half-bridge style duty update from a control loop at 20 kHz, ns-level table.
- **M06 input capture (mac)**. Model: 16-bit CNT with PSC, capture of CNT into CCR1 on an input edge (CC1IF, over-
  capture CC1OF when CC1IF still set), UIF on overflow; event list = (time_in_timer_clocks, edge). Driver from ISR:
  extend to 32-bit with an overflow counter; period = Δ(extended captures); frequency + duty (PWM input: CH1 rising /
  CH2 falling, or two edges on one channel — your choice, stated). The key edge case: capture and update both pending
  in the same ISR entry — decide whether the overflow happened before the captured value (capture value small ⇒
  overflow before capture ⇒ count it; large ⇒ after) — prove with exhaustive tests around the wrap; naive version
  (always add overflows first) shown failing. Also: very low frequency (multiple overflows), frequency > what you can
  measure (overcapture), glitch filter IC1F mention, PSC trade-off resolution vs range, measuring with input
  prescaler IC1PSC. Report error in ppm vs ground truth.
- **M07 ADC driver (mac)**. Mock ADC model: per-channel input voltage + source impedance, SMPR/SQR registers,
  SWSTART → converts the SQR sequence (SCAN) one channel at a time over model ADCCLK cycles, EOC per conversion
  (EOCS=1) or per sequence, DR, OVR if DR not read before the next result. Sampling model: the sample cap settles as
  RC (state your C_ADC / R_ADC model values as "대표값, 데이터시트 확인 권장"); too-short sampling time with high
  impedance gives a low reading — show the error vs SMPR table. Driver: `adc_config_scan(chs[], n, smp[])` (SQR/SMPR
  field packing — exhaustive bit-field tests), `adc_read_scan_polled()` (reads DR before next EOC; show OVR when
  too slow), raw → mV with VREFINT calibration (VDDA = 3300·VREFINT_CAL/VREFINT_DATA; integer math, rounding stated),
  oversampling 4^n average → +n bits (noise model fixed-seed; show the gain only appears with dither/noise). Tests vs
  model ground truth with explicit tolerance. Lane: DMA = M08/M09.
- **M08 DMA driver (mac)**. Mock DMA2 stream model per the RM0090 layout (SxCR/NDTR/PAR/M0AR, LISR/HISR with the
  irregular 0/6/16/22 group offsets, LIFCR/HIFCR W1C): on each "request" from a peripheral model (or free-running
  for mem-to-mem) transfer one data item of PSIZE/MSIZE (keep equal unless you model the FIFO), increment per
  PINC/MINC, NDTR−−, HTIF at half, TCIF at 0; normal mode → EN cleared by HW at 0; circular → reload NDTR and
  addresses. Driver: `dma_stream_config(...)` (must check EN==0 / disable and wait for EN to read 0 before writing,
  clear all flags of that stream first — injected bug: leftover TCIF fires immediately), `dma_flag_pos(stream, flag)`
  exhaustive vs table, IRQ handler with HT/TC callbacks. Tests: mem→periph (UART TX model), periph→mem, mem→mem,
  byte/halfword/word, circular wrap callback order across many laps, reconfiguring while enabled rejected, TEIF on a
  bad address (model). Explain cache coherency only by citation (ARCH-M12). Lane: ADC pipeline = M09.
- **M09 ADC + DMA double buffering (mac)**. Embed compact ADC (M07) + DMA circular (M08) models. Circular buffer of
  2·N samples, HT → process first half, TC → second half (average/filter into an output stream). Processing time is a
  parameter (in ADC sample periods); detect overrun: when processing of half h finishes (or starts), check the DMA's
  write position (from NDTR) or a lap counter to see if DMA has already re-entered half h → flag + count + resync.
  Tests: output equals reference filter over a long fixed-seed signal when budget OK; budget just under/at/over the
  half-period → overrun detected exactly at the predicted sample; no false positives; also show the race of reading
  NDTR vs the flag. Part C: budget table (sample rate × N × processing cost → CPU load and max latency), and F4
  double-buffer mode (DBM/CT, M0AR/M1AR) as the alternative (describe; optional model). Cite COMM-M11 (UART RX DMA).
- **M10 watchdog hardware (mac)**. IWDG: timeout = 4·2^PR·(RLR+1)/f_LSI; `iwdg_pick(min_ms, max_ms, lsi_min, lsi_max)`
  chooses PR/RLR so the WORST-CASE LSI still satisfies the window (exhaustive vs brute force, impossible cases).
  WWDG: counter T from 0x7F down at PCLK1/4096/2^WDGTB, reset at 0x40→0x3F, too-early refresh (counter > W) resets,
  EWI at 0x40; `wwdg_window_ms(...)` returns earliest/latest legal refresh time. Mock watchdog models with KR key
  sequence (writes to PR/RLR ignored unless 0x5555 first; PVU/RVU busy flags — model a few LSI cycles of busy),
  reset → recorded reset cause bits in a mock RCC_CSR, RMVF clears them. Tests: starvation reset, kick too early
  (WWDG) reset, kick in window OK, IWDG cannot be stopped once started, DBGMCU freeze stops counting while
  "halted", reset-cause decoding at boot + clear, 2 resets in a row distinguished. Part C: boot-time reset-cause
  report + window choice for a 10 ms control loop. Task check-in design = TEST-B09 (cite only).
- **M12 RTC + low-power timekeeping (mac)**. Mock RTC model with BCD TR/DR, PRER, WPR key (0xCA, 0x53; writes
  ignored otherwise), INIT/INITF handshake (INITF after a few model ticks), PWR_CR.DBP gate, ck_spre ticking from
  32.768 kHz, SSR down-counter, ALRMAR with MSK bits + ALRAF, RSF. Model the shadow-register rule (reading TR locks
  DR until DR is read, RM0090 — hedge) AND a "no shadow" naive model to show the midnight tear: naive driver reads
  TR then DR while the date rolls → time 00:00:00 with yesterday's date (or reverse) → 1 day error; fix = shadow
  lock order or read-twice-until-equal. Driver: `rtc_set/get`, BCD helpers, `rtc_to_epoch/epoch_to_rtc` (civil-days
  algorithm, e.g. H. Hinnant's days_from_civil — cite), weekday, alarm set for "next 07:30" and wake-up event.
  Prescaler calc: PREDIV_A/S for 32.768 kHz (127/255) and LSI ~32 kHz, preferring max PREDIV_A for power (hedge why).
  Tests: BCD round trip all values, epoch vs brute-force day counting 1970–2099 incl. 2000 leap / 2100 not, rollover
  23:59:59 31 Dec, write-protect enforcement, alarm masks, subsecond reading.
- **M13 fault policy on QEMU (qemu)**. Real firmware on mps2-an385 (B08 pattern, own run_qemu.sh, copy to VM
  /tmp/BM-M13_work). Enable MemManage/BusFault/UsageFault via SHCSR and DIV_0_TRP/UNALIGN_TRP via CCR; inject faults
  one per boot: integer divide by zero (UDIV), unaligned LDR (word access to odd address), bus fault (load from an
  unmapped address — find one QEMU really faults on), undefined instruction (UDF), and optionally escalation (fault
  with handler disabled → HardFault FORCED). Each handler: put "outputs" into a safe state (write a safe value to a
  RAM-mapped or mps2 FPGAIO LED register — verify the address in QEMU source/probing, hedge), record {magic, reason,
  CFSR, HFSR, BFAR/MMFAR, stacked PC} in a `.noinit` (NOLOAD) RAM struct with a check value, then reset via AIRCR
  SYSRESETREQ. FIRST verify for real what QEMU does on SYSRESETREQ (machine reset? does RAM survive? does -kernel
  reload overwrite your struct? does `-no-reboot` make QEMU exit instead?) and design the run around the truth;
  document what you observed. Next boot reads the record, reports via semihosting, advances to the next fault;
  final boot prints the summary table and SYS_EXIT 0 (bounded: max N boots, `timeout`). starter = handlers/SHCSR TODO
  → everything arrives as HardFault (FORCED) and the per-fault reason check fails → exit 1. Decoding every CFSR
  bit / PC unwinding = ARM-M12 (cite); keep decode to the bits you trigger. Paste real QEMU output.

## Code rules / QEMU rules
Exactly as in BM_B_LESSON_WRITER_BRIEF.md "Code rules" (SDKROOT=MacOSX26.4.sdk, strict -Werror build, ASan/UBSan
for solution + practice, private /tmp/<ID>_* names, deterministic, real output only) and its QEMU/VM section (Lima VM
`elinux` — `limactl list`, start if stopped, never stop it; home mount read-only → work in VM /tmp/<ID>_work;
`/* ENV: qemu */` first line in firmware files; strict cross flags; QEMU must exit on its own with `timeout`; final
line `qemu exit=<n>`; exact Mac one-liner in the md). For M02/M13 the checker runs
`limactl shell elinux -- bash -lc 'bash <dir>/run_qemu.sh starter|solution|practice'` (starter must exit ≠ 0,
solution/practice 0). practice.c may be host C instead — if so, do NOT start it with `/* ENV: qemu */`.

## When done
Reply with: files written, md line count, PASS counts starter/solution/practice (+ QEMU results), ASan/UBSan result,
demo files + key real numbers, every QEMU behaviour you verified (and how), and every from-memory spec fact you were
unsure about. Do NOT edit files outside ROOT/lessons/BM/<your ID>/ (plus private /tmp paths).
