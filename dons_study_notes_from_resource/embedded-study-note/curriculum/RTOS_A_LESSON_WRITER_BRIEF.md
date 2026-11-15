# Lesson writer brief — track RTOS (실시간 운영체제), **Advanced** level (RTOS-A01–A07, A09, A10)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, prepping for
embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...). He re-checks fundamentals by
DOING: every lesson = one interview-style coding problem. RTOS Advanced = staff-level: "tickless에서 시간이 왜 밀리나?",
"측정 WCET를 믿어도 되나?", "B 항은 어떻게 구하나?", "비주기 이벤트 응답을 줄이면서 RM 보장을 지키려면?",
"이 트레이스에서 데드라인 미스 원인은?", "10 kHz 전류 루프를 태스크로 돌리겠나 ISR로 돌리겠나?". Follow-ups probe
*why this design, what it misses, how you'd know it's working, what it costs*.

## Read first (required, in order)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium overrides)
   → ROOT/curriculum/RTOS_B_LESSON_WRITER_BRIEF.md (mini-kernel vocabulary, timeline format, ucontext-on-macOS notes,
   FreeRTOS POSIX-port lane) → ROOT/curriculum/RTOS_M_LESSON_WRITER_BRIEF.md (Medium overrides + Medium lanes) →
   **this file**. Everything applies unless overridden here.
3. ROOT/curriculum/tracks/RTOS.json — your item. The lesson MUST do the item's `task` completely (every function, test,
   measurement it names). Skim all RTOS items to stay in your lane.
4. Reference lessons (style + depth): ROOT/lessons/RTOS/RTOS-A08/ (the one Advanced RTOS lesson already written),
   ROOT/lessons/RTOS/RTOS-M11/ (RTA tool), RTOS-M12 (EDF sim), RTOS-M04/M05 (PI/PCP), RTOS-M13 (FreeRTOS internals +
   trace macros demo). Skim the md of every lesson you cite so the reference is accurate and you don't re-teach it.

## Already written (cite by id, don't re-teach)
RTOS-B01–B13, RTOS-M01–M14 (all), RTOS-A08 (SMP). Key owners: B03 ucontext switch · B04 bitmap+CLZ · B06 delay_until +
tick wrap · B07 FreeRTOS POSIX port · B08 ISR→task sem · B12 sw timers · M02 FromISR/yield at ISR exit · M03/M04/M05
inversion/PI/PCP-IPCP · M06 blocking queue · M07 delayed list vs timer wheel · M08 stack budget · M09 overflow detect ·
M11 Liu-Layland + basic RTA · M12 EDF sim vs RM · M13 FreeRTOS internals + trace macros. Also BM-B01–B14 (BM-B08 = real
first boot on QEMU mps2-an385 — A01 reuses its run_qemu.sh/semihosting pattern), BM-M11 (low-power modes — A02 cites),
ARM-M11 (LDREX/STREX on QEMU M3), ARCH-M09 (NVIC), COMM-M (CAN, A10 cites), TEST-A05 (deterministic simulation).
Planned, not written (mention as "다음 단계" only): ARM-A01 (PendSV switcher — A01 must therefore be self-contained and
build its own PendSV switch), BM-M/A, SYSD items.

## Lanes (written IN PARALLEL right now by other agents — stay in yours; every lesson self-contained, no shared lib)

- **RTOS-A01 Cortex-M 미니 RTOS 완성** (env qemu): real firmware for `qemu-system-arm -M mps2-an385` (Cortex-M3), built and
  run in Lima VM `elinux` (arm-none-eabi-gcc 14.2.1, qemu-system-arm, gdb-multiarch present). Copy BM-B08's pattern:
  `run_qemu.sh starter|solution|practice|gdb|...` copies sources to /tmp/RTOS-A01_work in the VM (Mac home is read-only
  there), builds with `-mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib`, own linker script + vector table, semihosting
  output, exits via SYS_EXIT(_EXTENDED) with code 0 = all pass / 1 = fail, `timeout` guard. Must contain: initial task
  stack frame (xPSR 0x01000000, PC|1, LR = task-exit trap, R0 = arg, R4–R11 slots), first-task start via SVC (or
  documented alternative: set PSP + CONTROL + exception return 0xFFFFFFFD — show WHY FreeRTOS uses SVC, quote
  portable/GCC/ARM_CM3/port.c prvPortStartFirstTask/vPortSVCHandler), PendSV switch (save R4–R11 on PSP, store PSP in TCB,
  pick next, restore — naked function with inline asm), PendSV + SysTick at lowest priority, bitmap scheduler (M3 has CLZ),
  counting semaphore, mutex with priority inheritance, task_delay, priority-inversion demo (with vs without PI, assert
  completion ticks), BASEPRI critical section: a "syscall-class" IRQ masked during the critical section vs a
  higher-priority IRQ above the threshold that still runs (trigger with NVIC ISPR/STIR on an unused IRQ), assert both.
  gdb session (batch, in VM) showing PSP/MSP/CONTROL/stacked frame of a switched-out task — paste real output. Use
  `-icount shift=...` if needed for determinism and say so. Starter must FAIL (exit 1) without hanging: HardFault handler
  prints + exits 1, SysTick watchdog exits 1 after N ticks. practice.c may be firmware (ENV: qemu) or host C.
  Files that are firmware start with the exact first line `/* ENV: qemu */`.
- **RTOS-A02 Tickless idle** (mac): host C, simulated time (no real sleeping). Model a down-counting hardware timer
  (24-bit reload like SysTick, configurable clock), tick period, idle hook that computes expected idle ticks
  (next unblock − now, FreeRTOS prvGetExpectedIdleTime), skips if < configEXPECTED_IDLE_TIME_BEFORE_SLEEP, caps at
  max suppressible ticks (reload width), programs a one-shot, "sleeps", then on wake: (a) timer expiry → step
  expected−1/expected ticks correctly, (b) early wake by another IRQ → compute elapsed WHOLE ticks from the counter,
  step them, reload the remaining fraction so no drift, (c) abort-before-sleep when a task became ready (eTaskConfirm
  SleepModeStatus). Tests: zero cumulative drift over long random runs vs a naive implementation that drifts (show the
  drift number), early wake, max-suppression cap, abort path, wakeup count / estimated energy vs ticked idle. Quote
  portable/GCC/ARM_CM4F/port.c vPortSuppressTicksAndSleep + tasks.c vTaskStepTick / prvGetExpectedIdleTime (file:line).
  Mention the 'stopped timer while computing' error compensation (ulStoppedTimerCompensation) honestly. Cite BM-M11.
- **RTOS-A03 WCET 측정과 지터 분석** (mac): function under test = a realistic control-loop step (PI controller with
  saturation/anti-windup + a branchy filter/table lookup) with input-dependent paths. Checked files are DETERMINISTIC:
  statistics code (histogram with fixed bins, nearest-rank percentiles p50/p99/p99.9/max, high-water mark, jitter =
  max−min and stddev) tested on synthetic samples, plus an instrumented operation counter (cost model per path) that
  shows random-input testing misses the worst path and a targeted input finds it. Real timing ONLY in demo_*.c:
  mach_absolute_time (+ mach_timebase_info), tens of thousands of runs, cold vs warm cache (evict by sweeping a large
  buffer), input classes, pinned to a QoS class if helpful, report histogram + percentiles + `uptime` load. Show a
  board-side DWT->CYCCNT snippet (DEMCR TRCENA, DWT_CTRL CYCCNTENA) as reference code, clearly marked not run here
  (don't claim QEMU models CYCCNT unless you test it in the VM). Discuss MBTA vs static analysis (aiT, OTAWA — hedge),
  extreme-value/pWCET only as "다음 단계".
- **RTOS-A04 확장 RTA** (mac): builds on M11. C tool: R_i = C_i + B_i + Σ_{j∈hp(i)} ceil((R_i + J_j)/T_j)·C_j,
  worst response = R_i + J_i, ISRs as highest-priority sporadic tasks (min inter-arrival), B_i from IPCP ceilings =
  longest critical section of a lower-prio task on a resource whose ceiling ≥ prio_i. Discrete-time simulator with
  release jitter, IPCP resources, ISRs; many seeded random runs + adversarial release patterns (critical instant with
  max jitter); assert analysis ≥ every observed response (safe) and show a task set where the naive M11 RTA (no B, no J,
  no ISR) is UNSAFE (observed > naive bound). Cite Audsley/Burns/Richardson/Tindell/Wellings 1993 and Sha/Rajkumar/
  Lehoczky 1990 by author/year/title (hedge exact titles if unsure). FreeRTOS tie-in: ISR above
  configMAX_SYSCALL_INTERRUPT_PRIORITY never blocked by critical sections vs below it (taskENTER_CRITICAL = blocking term).
- **RTOS-A05 비주기 작업 서버** (mac): scheduling simulator (fixed-priority, integer time) with background service,
  polling server (budget lost if no pending request at its activation), sporadic server (Sprunt/Sha/Lehoczky 1989
  replenishment: amount consumed, replenished at activation time + Ts), optionally deferrable server and its RM-bound
  violation (back-to-back execution) as a demonstrated counterexample. Compare aperiodic avg/max response and periodic
  deadline misses across the same seeded workloads; assert periodic tasks never miss with PS/SS at their RM-feasible
  budget. Mention POSIX SCHED_SPORADIC and its known defects (Stanovich et al. 2010 — hedge) and Linux SCHED_DEADLINE/CBS
  as reservation-based alternative (hedge). Cite M11/M12.
- **RTOS-A06 RTOS 트레이싱** (mac): OWNS the shared trace format below. Checked C: trace recorder ring buffer (single
  writer, overwrite-oldest vs stop-when-full modes, sequence numbers, dropped-event count) + decoder that rebuilds per-task
  run intervals and CPU % from SWITCH_IN events, handles 32-bit timestamp wrap, missing first event, dropped events
  (mark gap, don't attribute time). Demo: real FreeRTOS on the POSIX port with trace macros in the lesson's
  FreeRTOSConfig.h (traceTASK_SWITCHED_IN/OUT, traceTASK_INCREMENT_TICK, traceBLOCKING_ON_QUEUE_RECEIVE,
  traceTASK_DELAY_UNTIL … — check FreeRTOS.h V11.3.2 for exact names, quote lines) writing the shared format, dumped to a
  text file; `timeline.py` (stdlib only, run with `python3 -I`) renders an SVG/HTML timeline + CPU table — embed the
  real generated SVG (adapted to the svg rules: currentColor + f-* classes) in the md. Compare to SEGGER SystemView /
  Percepio Tracealyzer (principle level, hedge product details). Timestamp source on the POSIX port = tick count ×
  period + sub-tick from clock_gettime — say it's not real-time.
- **RTOS-A07 Zephyr 기초** (env qemu): install a MINIMAL Zephyr workspace in the Lima VM `elinux` (disk: ~11 GB free —
  budget ≤ 4 GB, stop and fall back if exceeded): python venv with west, Zephyr **v4.5.0** shallow, only the modules the
  build needs (e.g. `west config manifest.project-filter -- '-.*,+cmsis_6'` then add what's missing, e.g. picolibc, or
  use CONFIG_MINIMAL_LIBC), toolchain = system arm-none-eabi-gcc via ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
  CROSS_COMPILE=/usr/bin/arm-none-eabi- (no Zephyr SDK download unless unavoidable — say what you did). Location:
  ~/zephyrproject in the VM home. Record every install command + versions + disk used in the md (환경 준비 box) so Don
  can repeat it. App for qemu_cortex_m3: producer/consumer threads with k_msgq, k_sem signalling, k_timer periodic
  producer; self-checking output (`N/M passed` on the serial console). Same app on FreeRTOS (portable/GCC/ARM_CM3 port
  from ~/.esn_scratch/FreeRTOS-Kernel — readable from the VM at the same path, read-only; copy to /tmp/RTOS-A07_work)
  on mps2-an385 or lm3s6965evb → compare API table, config style (Kconfig/devicetree vs FreeRTOSConfig.h), and image
  size (arm-none-eabi-size / west build -t rom_report output, real numbers). run_qemu.sh starter|solution|practice
  runs in the VM with timeout and returns exit 0/1 from the PASS line. Checked starter/solution may be Zephyr app
  sources (first line `/* ENV: qemu */`); practice.c may be host C (e.g. a k_msgq semantics model) or Zephyr. If the
  install truly fails after a fair try (~30 min), fall back: checked files = host C model of k_msgq/k_sem/k_timer
  semantics, Zephyr parts marked "외부환경필요 — 미실행", and report exactly what failed.
- **RTOS-A09 데드라인 미스 사후 분석** (mac): 4 seeded buggy systems — priority inversion, ISR overload, queue-full
  blocking, wrong priority assignment (RM order violated) — plus 1 healthy control. Produce traces in the SHARED
  FORMAT (below; embed a compact copy of the parser, say "RTOS-A06 포맷"). Checked C: a deterministic mini-kernel
  simulator that generates the 5 traces + an analyzer that, given only a trace, diagnoses the root cause with evidence
  (rules: high task blocked on mutex owned by low while a medium task runs; ISR time share in the window before the miss
  over threshold; sender blocked on full queue while consumer starved/preempted; RM inversion: longer-period task with
  higher prio preempting the missed task). Starter analyzer stubs → FAIL; solution diagnoses all 4, no false positive on
  the control, and each FIX (PI / ISR work deferred / queue depth or consumer prio / RM reassign) re-simulated → zero
  misses. Demo: real FreeRTOS POSIX-port runs of at least 2 of the bugs with trace macros dumping the same format →
  run the analyzer on them (paste real output). md §5 = 4 short incident reports: 증상 → 트레이스 근거 → 원인 → 수정 → 재검증.
- **RTOS-A10 RTOS 시스템 설계: 모터 제어 + 통신 + 로깅** (mac): interview-format design lesson WITH checked code.
  System: 10 kHz current loop (ISR), 1 kHz speed loop, CAN command receive, flash logging (erase blocks for ms!),
  watchdog. Deliver: task/ISR table (what runs where and why — current loop in ISR vs task tradeoff), priorities
  (RM/DM justified), IPC choices (notification from ISR every 10th period, CAN ISR → queue, log ring buffer +
  low-prio flush task, double buffer during erase), queue depths derived from burst arrival vs service rate,
  stack budgets (cite M08 method, numbers estimated and labelled), RTA incl. ISR interference and blocking (embed a
  compact RTA — A04 is parallel; say "RTOS-A04 확장 RTA 축약"), and a mini-kernel implementation of the critical path
  in a deterministic tick simulator: current ISR → speed task notify, CAN burst → queue no overflow at chosen depth,
  log flush never delays the speed loop past its deadline, watchdog feeds only when every monitored task checked in
  (task-alive bitmask) and bites within the bound when one task stalls. Include "면접에서 말하는 순서" table (요구사항
  확인 → 타이밍 예산 → 분할 → 우선순위 → IPC → 자원·스택 → 검증 → 실패 모드) and a "질문받으면 바꿀 결정" list.

## Shared trace format (A06 owns; A09 and A10 may use it)
Text, one event per line, `#` comments allowed, header line `# rtos-trace v1 unit=us`:
`<t_us> <EVENT> <task_or_isr_name> [arg]` where EVENT ∈ {RELEASE, SWITCH_IN, SWITCH_OUT, BLOCK, UNBLOCK, ISR_ENTER,
ISR_EXIT, MUTEX_TAKE, MUTEX_GIVE, Q_SEND_BLOCK, Q_RECV_BLOCK, DEADLINE_MISS, DROP}. arg = object name / deadline / count.
Names are [A-Za-z0-9_]+, IDLE task is `IDLE`. t_us is unsigned 32-bit (wraps). Times strictly non-decreasing modulo wrap.

## Shared conventions (same as Basic/Medium)
- Mini-kernel vocabulary from RTOS_B brief (task_state_t, tcb_t, kernel_tick(), sched_pick_next(); bigger prio = more
  urgent — note Zephyr/µC/OS opposite). Timeline format `tick : 0 1 2 ...` with R/r/B/S/. rows; svg Gantt drawn from the
  REAL output. Deterministic single-thread tick-driven simulators preferred.
- Benchmarks (ns/cycles) only in demo_*.c, host ratios with `uptime` load caveat (machine is shared, load ≈ 6–8);
  checked files assert deterministic quantities (ticks, op counts, bytes, sequences).

## Overrides for this set
- Output: ROOT/lessons/RTOS/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, FreeRTOSConfig.h, hooks.c, run_qemu.sh, *.ld, *.py as needed). Front matter `updated: 2026-11-11`,
  `time:` 90–120분. Title line `# <ID> <title exactly as in RTOS.json>`. md 520–750 lines. **Q&A ≥7 (aim 8–9)**, with at
  least 2 staff-level follow-ups ("…라면 어떻게 바꾸겠나?", "이 분석이 틀리는 경우는?").
- Firmware tie-in: 1. 문제 first paragraph = one line on why interviewers ask this; Part C = realistic MCU/RTOS
  scenario. Include a table mapping your model calls to the real API (FreeRTOS / Zephyr / CMSIS-RTOS2 where sure).
- Accuracy: quote FreeRTOS V11.3.2 source (file:line from ~/.esn_scratch/FreeRTOS-Kernel) for every FreeRTOS behaviour
  claim; papers by author/year/title; Zephyr / µC/OS / Linux / commercial-tool claims hedged ("문서 기준, 미검증") unless
  you verified them (A07 verifies Zephyr by running it). Don't claim an asm instruction you didn't observe. Never invent
  output — paste what you ran.
- Tests deterministic; starter FAILs (exit 1) without hanging or crashing (step limits / watchdogs); solution +
  practice < 2 s each on the host (QEMU runs < 15 s each).
- PRIVATE scratch: /tmp/<ID>_work/ and /tmp/<ID>_* (Mac and VM). Never write helpers into a shared folder or another
  lesson's dir. Lima VM is shared with other writers: don't install system packages without need (A07's venv + Zephyr
  workspace in VM home only if Don has approved it — the orchestrator tells A07 in its prompt); never touch /var/lab.
- Build/verify (you MUST run) for host-C files:
      export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
      cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] starter.c -o /tmp/<ID>_st && /tmp/<ID>_st   # FAILs, exit 1
      same for solution.c (all PASS, exit 0, run 5×) and practice.c (all PASS, exit 0)
      cc -std=c11 -g -O1 -fsanitize=address,undefined [-pthread] solution.c / practice.c → clean. NEVER run -fsanitize=thread (TSan): it hangs on the FreeRTOS POSIX port and leaves 100%-CPU zombie processes. Use ASan/UBSan only.
  For `/* ENV: qemu */` files: `limactl shell elinux -- bash -lc 'bash <dir>/run_qemu.sh starter|solution|practice'`
  → starter exit ≠ 0, solution/practice exit 0 (the orchestrator's checker runs exactly this). Harness prints a final
  `N/M passed` line. Checked files must NOT need the FreeRTOS kernel (only demo_freertos_*.c do; build those with the
  RTOS_B brief's POSIX-port recipe, `BUILD: freertos` header comment). *.py: stdlib only, `python3 -I` runnable.
- md self-check before reporting: headings verbatim & ordered, `### Part A — / B — / C —`, ≥2 ```svg with viewBox
  width ≤760, currentColor + f-acc/f-ok/f-warn/f-mut only, &lt; &gt; &amp; escaped in svg text, Q&A ≥7, 3–6 추가 연습 +
  `### 한 번에 검증` + `<!-- code: practice.c -->`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, demo markers `<!-- code: demo_x.c -->` where shown.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan results, demo
files + key numbers, VM/QEMU results, anything unverified/unsure (list). Do NOT edit files outside
ROOT/lessons/RTOS/<ID>/ (and your /tmp/<ID>_* paths; A07 also ~/zephyrproject in the VM). Don't run build_site.py,
don't git commit.
