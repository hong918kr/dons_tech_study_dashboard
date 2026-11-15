# Lesson writer brief — track BM (베어메탈 펌웨어), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
BM-B is the **bare-metal bring-up** track: register headers, host-side mock peripherals, RCC/GPIO/UART/
SysTick/timer drivers, memory map, startup code, and the first real Cortex-M boot in QEMU.

Already written (assume he knows them, cross-reference by id, don't re-teach): C Basic/Medium/Advanced,
DS B/M/A, CPP B/M/A, ALGO-B/M, ARCH-B/M, OS-B, COMM-B01–B14 (COMM-B03 = baud divisor math + clock-tolerance
sweep, COMM-B01/B02 = UART framing / 16x RX), RTOS-B, SIV-B/M/A (SIV-B01 = register header generation),
HWLAB-B/M, C-M14 (ISR↔main shared data, PRIMASK), ARM-M11 (LDREX/STREX on QEMU mps2-an385), BM-M11 (low power).
Not written yet (cite by id only): BM-M01…M13, BM-A01…A11, TOOL-B06 (size), TOOL-M01 (linker script basics),
TOOL-A07 (advanced linker script), ARM-M08 (asm vector table + Thumb bit), ARM-M10 (PRIMASK/BASEPRI).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) and ROOT/curriculum/COMM_B_LESSON_WRITER_BRIEF.md (latest
   Basic brief: its "Output", "Code rules" and "Quality bar" apply to you unless overridden below; ignore its
   COMM lanes).
3. ROOT/curriculum/tracks/BM.json — your item (title/goal/task/tags/env/env_note). The lesson must do the
   item's `task` completely (every function, test, measurement it names). Skim all BM items (B/M/A) to stay
   in your lane.
4. Reference lessons: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed") and
   ROOT/lessons/BM/BM-M11/ (existing BM lesson: tone, mock style). QEMU lanes also read
   ROOT/lessons/ARM/ARM-M11/run_qemu.sh + its md section "실기 — 진짜 Thumb-2 를 QEMU 에서" (VM recipe).

## Output
ROOT/lessons/BM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(+ optional demo_*.c, *.h, *.ld, run_qemu.sh, captured *.txt/*.map data files).
Front matter `updated: 2026-11-11`, realistic `time:` (40분–70분). Title line `# <ID> <title exactly as in BM.json>`.
Size ~400–600 md lines. Q&A ≥ 5 (aim 6–8). ≥ 2 inline ```svg figures (rules in the COMM-B brief: viewBox,
width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut, no font-family except monospace, font-size 12–14,
escape &lt; &gt; &amp;). Good figures for this track: register bit-field layouts (MODER 2-bit fields per pin,
BRR mantissa/fraction), memory maps (Flash/RAM bars with section boxes, LMA→VMA arrow for .data), boot
sequence flow (reset → vector[0]=MSP, vector[1]=Reset_Handler → copy/zero → main), timer counter sawtooth
with UIF marks, SysTick down-counter with reload, mock framework block diagram (driver → REG_* → hook table).
Numbers in figures must match your code / real output.

Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why it
matters in firmware interviews / board bring-up. Tie to Don's reality (SSD controller bring-up, RF module
boards, STM32/nRF/Cortex-M).

## Interview flavour
- 2. Problem (English) = real firmware-round prompt: signatures, constraints, example I/O
  (e.g. "gpio_init(GPIOA, 5, MODE_OUTPUT, PULL_NONE, SPEED_LOW) → MODER bits [11:10] = 01, others unchanged").
- 면접 Q&A = real follow-ups: why volatile, why the dummy read after clock enable, why BSRR exists, W1C vs
  rc_w0, what happens before main, why .data costs Flash AND RAM, what the map file tells you, why semihosting
  hangs without a debugger, TXE vs TC before sleep/power-off, wrap-safe compare, PSC buffering / first period.

## Shared mock vocabulary (BM-B02 owns it; everyone else embeds a COMPACT copy — "(BM-B02 mock 축약판)")
All lessons are written IN PARALLEL; every .c must be self-contained (no shared lib between lessons). To make
the lessons read as one system, use these names when you need a behavioural mock:

```c
/* driver code only ever touches registers through these two macros */
#ifdef UNIT_TEST                      /* host build: hooks */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else                                 /* target build: real MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif
/* mock core: sparse register table + per-address hooks + access log */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* returns value seen by CPU */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* returns new stored value */
int  mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx);
void mock_reset(void);
/* access log: {op 'R'/'W', addr, value} entries, fixed capacity, for asserting order */
```
- Unknown address → test failure (log + counter), never a crash. Static arrays only (no malloc).
- Lessons define `UNIT_TEST` at the top of starter/solution/practice (host build), and say how the same driver
  compiles for target. Use real STM32F4-family (RM0090) addresses/offsets as the mock's definition, e.g.
  RCC 0x40023800 (AHB1RSTR +0x10, APB1RSTR +0x20, APB2RSTR +0x24, AHB1ENR +0x30, APB1ENR +0x40, APB2ENR +0x44),
  GPIOA 0x40020000 (+0x400 per port; MODER 0x00, OTYPER 0x04, OSPEEDR 0x08, PUPDR 0x0C, IDR 0x10, ODR 0x14,
  BSRR 0x18, LCKR 0x1C, AFRL 0x20, AFRH 0x24), USART2 0x40004400 (SR 0x00, DR 0x04, BRR 0x08, CR1 0x0C,
  CR2 0x10, CR3 0x14; SR bits PE0 FE1 NF2 ORE3 IDLE4 RXNE5 TC6 TXE7; CR1 RE2 TE3 M12 UE13 OVER8 15),
  TIM2–5 (CR1 0x00, DIER 0x0C, SR 0x10, EGR 0x14, CNT 0x24, PSC 0x28, ARR 0x2C; UIF = SR bit0, UG = EGR bit0,
  UIE = DIER bit0, CEN = CR1 bit0, ARPE = CR1 bit7), SysTick 0xE000E010 (CSR/RVR/CVR/CALIB; CSR ENABLE0
  TICKINT1 CLKSOURCE2 COUNTFLAG16). These are from memory → in the md label "RM0090 기준 대표값 — RM 확인 권장"
  where you state them; the TESTS' ground truth is your mock's own definition.
- Access-type vocabulary in the md: rw, r, w, rc_r (clear on read), rc_w0 (write 0 to clear — STM32F4
  TIM SR / USART SR TC), rc_w1 = W1C (write 1 to clear — e.g. EXTI_PR on F4, ICR registers on newer STM32,
  hedged), rs (set only).

## Lanes (stay in yours; cross-ref the others by id)
- **B01** CMSIS-style device header reading. Write your own SHORT excerpt in CMSIS style (device_excerpt.h:
  `__IO`/`__I`/`__O` qualifiers, `GPIO_TypeDef`/`USART_TypeDef`/`RCC_TypeDef` (only the fields you need +
  `RESERVEDn` padding), `PERIPH_BASE`/`AHB1PERIPH_BASE` chain, `GPIOA` cast macro, `GPIO_MODER_MODER5_Pos/_Msk`,
  `USART_CR1_UE_Pos/_Msk` …) — don't paste ST's file. `_Static_assert(offsetof(...))` against an RM offset
  table (state the table; hedge). Config functions using ONLY the macros (`MODIFY_REG`-style clear+set,
  `_VAL2FLD`-style helpers), verified against a fake struct instance in RAM (test build redefines `GPIOA` to a
  RAM struct). Show the classic bugs: `_Pos` used where `_Msk` was needed, missing clear before set, wrong
  RESERVED padding caught by static_assert (demonstrate with a #ifdef that fails to compile — show the real
  compiler error text). No hook framework (that's B02). Header generation = SIV-B01 (cite).
- **B02** Owns the mock framework above (full version): register table, hooks, access log, reset values,
  "unexpected access" detection, plus two example peripheral models built on it: a UART whose TXE goes 0 on DR
  write and back to 1 after N "ticks" (mock_tick()) with TX capture buffer, and RX injection queue that sets
  RXNE and clears it on DR read (rc_r on read of DR). Tests that a tiny driver (uart_putc/getc) works against
  it and that the log shows the exact access order. Part C: same driver source compiled for target with the
  MMIO macros (compile with clang --target=thumbv7em-none-eabi -c and show the real `ldr/str` in objdump —
  `xcrun llvm-objdump` or check what's available; paste real output). Explain link-time vs macro vs
  function-pointer seams (cite CPP-A08 link seam if useful).
- **B03** RCC clock gating + reset: mock RCC + GPIOA where writes to GPIO registers are ignored (and reads
  return 0) while its AHB1ENR bit is 0; peripheral reset (xRSTR bit pulse) restores reset values. API
  `periph_clock_enable(periph_id)`, `periph_reset_pulse(periph_id)` with a periph table (bus, bit). Model the
  "enable then immediately access" hazard: the mock makes the clock effective only after N bus cycles unless
  the driver does a read-back of the ENR register (dummy read) — say clearly this is a MODEL of the STM32
  errata/RM note "delay after RCC peripheral clock enabling" (hedge, cite errata sheet ES0182-style as 확인 권장),
  and on real HW the read-back works because the bus write completes before the read. Tests: write-without-
  clock ignored, enable+dummy-read works, reset pulse, enable doesn't disturb other ENR bits (RMW with
  read-back), disable then access.
- **B04** RMW traps: (a) W1C status register (rc_w1) — `SR |= FLAG_A` clears every pending flag that was 1;
  correct = write only the bit (`REG_WRITE(SR, FLAG_A)`); also show rc_w0 variant (STM32 TIM SR: write ~UIF) and
  why `&=` on rc_w0 is racy-but-common. (b) ODR RMW race between main and an ISR: deterministic interleaving —
  the mock lets the test "fire the ISR" between the read and the write of main's RMW (e.g. a hook on the n-th
  access), lost update reproduced; fix with BSRR (atomic set/reset, set wins if both — RM0090, hedge) and
  alternatively a critical section (cite C-M14 / ARM-M10). Show the instruction-level view (LDR/ORR/STR) with
  real thumbv7em asm from clang. Bit-banding mention only (BM-M/A lanes).
- **B05** Memory map + map file. Build a REAL small firmware in the Lima VM (`arm-none-eabi-gcc` 14.2,
  Cortex-M4, your own tiny .ld or newlib nano + `-Wl,-Map`) with a big lookup table, initialised globals,
  zero globals, stack/heap reservations; capture `arm-none-eabi-size -A`, `size -B` and the relevant map
  excerpt into data files in the lesson dir (e.g. fw_bad.map / fw_good.map / size_*.txt — keep them small,
  trim to the needed parts, keep it under ~60 KB total). starter/solution/practice.c (Mac-runnable) parse those
  captured files and build the budget table (Flash = text + rodata + data(LMA); RAM = data + bss + stack +
  heap; % of a 512 KB/128 KB part) and find the "table missing const → lands in .data → costs Flash+RAM" bug by
  diffing two builds. A run_vm.sh (`/* ENV: qemu */`-style comment) reproduces the captures. Cross-ref TOOL-B06
  (size) and TOOL-M01 (linker scripts). Exact sizes must be the real captured ones.
- **B06** env qemu (no execution needed, just arm-none-eabi-gcc/ld/objdump in the Lima VM). layout.ld
  (MEMORY regions + symbols for BOOT / SLOT_A / SLOT_B / META (+ shared .noinit RAM, image header address)),
  boot.ld and app.ld that `INCLUDE layout.ld`, tiny boot.c / app.c with an image header struct placed by
  section at the slot start, a `.noinit` boot-reason struct at a fixed RAM address in BOTH images, linker
  `ASSERT()`s for overlap/size. Build both in the VM (script run_vm.sh, copies to VM /tmp because the Lima
  home mount is read-only), capture `objdump -h` + map excerpts into txt files. starter/solution/practice.c
  (Mac-runnable) = a layout checker: parse the captured objdump -h output (and/or a layout table), verify no
  overlap, sector alignment (state your flash sector map, e.g. STM32F4 16/16/16/16/64/128… KB — hedged),
  header at slot start, .noinit address equal in both images; inject a bad layout (overlapping slot / header
  not at start / misaligned) and assert detection. Show the real ld ASSERT failure message from the VM.
  Running the jump = BM-A03 (cite).
- **B07** Host simulation of crt0: fake FLASH image array + RAM array + "linker symbol" pointers/offsets
  (`_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss`, `__init_array_start/_end`), `reset_init()` copies .data,
  zeroes .bss, calls init_array in order. Tests: length 0, odd byte lengths (if you copy by words, how do you
  handle the tail — state the real-world rule: linker script ALIGN(4) makes them word multiples; test both your
  word loop with aligned sizes and a byte-tail variant), bss right after data, guard bytes around regions
  untouched (canaries), init_array order + empty array, a constructor that reads a .data value (must see the
  copied value — order matters). Explain why this can't be written in C that itself uses .data/.bss before
  init (and why the real Reset_Handler is careful). Real QEMU boot = B08 (cite).
- **B08** env qemu, REAL boot on `qemu-system-arm -M mps2-an385` (Cortex-M3) in the Lima VM: C-array vector
  table in `.isr_vector` (`[0]` = &_estack, `[1]` = Reset_Handler …), weak alias default handlers
  (`__attribute__((weak, alias("Default_Handler")))`), your own linker script, reset_init from B07 (embedded
  copy), main that checks .data initial values and .bss zero and reports via semihosting (SYS_WRITE0 / SYS_EXIT;
  QEMU `-semihosting-config enable=on,target=native`), exit code to the host. starter.c/solution.c/practice.c
  are FIRMWARE (`/* ENV: qemu */` first line) built+run by run_qemu.sh (pattern of ARM-M11: copy to VM /tmp,
  build with `-ffreestanding -nostdlib` or nano specs, run, print qemu exit). starter boots but its TODO
  reset_init leaves .data/.bss wrong → its self-test prints FAIL and exits 1; solution passes. Also a gdb
  session (gdb-multiarch -batch, `-s -S`) showing: break at Reset_Handler, `x/2wx 0` (vector table), watch a
  .data variable before/after copy — paste REAL output. Show what happens with the Thumb bit missing / wrong
  _estack (brief, real observation). Compare with CMSIS startup_*.s + system_*.c (SystemInit, __libc_init_array,
  hedged). Asm vector table = ARM-M08 (cite). Also make sure a Mac-host fallback exists so the lesson still
  "teaches" without the VM (e.g. practice.c can be host C that validates a vector-table image) — your call, but
  the checker will run starter/solution in the VM via run_qemu.sh, so document the exact commands.
- **B09** env qemu: printf retargeting on mps2-an385. (1) semihosting: `--specs=rdimon.specs` (or own
  SYS_WRITE0) + `-semihosting-config enable=on`; (2) UART: `--specs=nano.specs` + your own `_write` polling the
  CMSDK APB UART0 (base 0x40004000; DATA 0x00, STATE 0x04 (bit0 TX full), CTRL 0x08 (bit0 TX enable),
  BAUDDIV 0x10 — verify against QEMU's hw/char/cmsdk-apb-uart.c in v10.2.x) + minimal syscalls
  (_sbrk/_close/_fstat/_isatty/_lseek/_read), QEMU `-serial stdio`. Measure real code size of each
  (arm-none-eabi-size; also printf vs iprintf / nano vs full newlib). Reproduce "semihosting without a
  debugger/host": run the semihosting image WITHOUT `-semihosting` → BKPT 0xAB → what QEMU does (observe it
  for real: HardFault? hang? — install a HardFault handler that blinks a message out the UART so you can see
  it) and explain the real-HW rule (BKPT with no debugger attached escalates to HardFault; with debugger
  attached but no semihosting host it halts). starter/solution/practice = firmware (`/* ENV: qemu */`) +
  run_qemu.sh; starter's `_write` TODO prints nothing → host-side check in run_qemu.sh compares captured
  stdout with expected text and reports FAIL/PASS with exit code. Cross-ref C-M10 (mini printf).
- **B10** Clock-tree calculator (C, Mac). STM32F4-style constraints as a PARAMETER STRUCT (the test truth is the
  struct): VCO input = HSE/M in [1,2] MHz (2 MHz recommended), M 2..63, N 50..432 (some parts 192..432 —
  hedge), VCO out 100..432 MHz, P ∈ {2,4,6,8}, Q 2..15 for 48 MHz USB (optional), SYSCLK ≤ 168 MHz, AHB
  prescaler {1,2,4,…,512}, APB1 ≤ 42 MHz, APB2 ≤ 84 MHz, APB timer clock ×2 when APB prescaler ≠ 1, flash wait
  states from the RM0090 table for 2.7–3.6 V (30 MHz per WS step — hedge). Enumerate ALL valid (M,N,P[,Q])
  for a target exactly, rank (prefer exact SYSCLK, VCO input 2 MHz, exact 48 MHz Q), pick prescalers, compute
  WS. Integer arithmetic in Hz (no float equality). Test vectors: HSE 8 MHz → 168 MHz (M=8,N=336,P=2,Q=7 is the
  classic), HSE 25 MHz → 168, impossible targets, HSE 8 → 180 violating limits. Count of solutions as a
  property test vs brute force. Cross-ref COMM-B03 (baud from PCLK). Optional python cross-check not required.
- **B11** GPIO driver on mock (compact B02 core + GPIO model): `gpio_init(port, pin, mode, pull, speed[, otype,
  af])`, write (via BSRR), read (IDR — mock drives IDR from an "external pin level" array + pull model for
  floating inputs), toggle (ODR read + BSRR write, explain why not ODR ^=). Tests: exhaustive over pins 0–15 ×
  modes that ONLY the target pin's 2-bit/1-bit fields changed (random pre-filled registers), AFRL vs AFRH
  split at pin 8, clock-enable precondition (mock ignores writes when RCC bit off → driver must enable first;
  compact B03 rule), pin 15 / pin 0 edges, invalid args rejected. Lane: no EXTI (BM-M03), no RMW race (B04).
- **B12** Polling UART driver on mock (compact B02 core + USART model with a TX shift register: DR→TDR empties
  (TXE=1) immediately into shift register, TC=1 only after the shift register finishes N ticks later).
  BRR: F4 USARTDIV = f_ck / (8·(2−OVER8)·baud); BRR = mantissa<<4 | fraction (4 bits, OVER8=0; 3 bits and bit3
  must be 0 when OVER8=1); rounding to nearest, fraction carry into mantissa (e.g. fraction rounds to 16) —
  test that carry case explicitly; actual baud + % error. putc (wait TXE), getc (wait RXNE, with timeout →
  error, ORE handling), write(buf,n), flush (wait TC). Part C: "send then power off" — show that waiting only
  for TXE loses the last byte (mock cuts power → captured TX lacks last byte), TC wait fixes it. Baud tolerance
  theory = COMM-B03 (cite, don't redo).
- **B13** SysTick: `systick_reload(core_hz, tick_hz, *reload)` (reload = core_hz/tick_hz − 1; 24-bit limit
  0xFFFFFF → error; non-integer division → report error ppm), mock SysTick model (CVR counts down each mock
  cycle, reload on 0, COUNTFLAG set and clear-on-read, TICKINT → calls your SysTick_Handler), `volatile uint32_t
  g_ticks`, `millis()`, `delay_ms()`, `timeout_expired(start, ms)` / `time_after(now, deadline)` with
  `(int32_t)(now - deadline) >= 0`. Start g_ticks at 0xFFFFFFF0 and test across the wrap; show the naive
  `now >= deadline` bug failing; limit: works for intervals < 2^31 ticks (test near the limit). Also the
  "delay_ms(1) can return after 0.x ms" off-by-one (tick phase) — test and explain the +1 rule. Optional: 64-bit
  tick read with two-read consistency (cite C-M14). Lane: no RTOS tick (RTOS-B), no low-power tickless (BM-M11).
- **B14** Timer PSC/ARR: f_update = f_tim / ((PSC+1)(ARR+1)), PSC, ARR 16-bit (0..65535). Search function that
  returns the minimum-error pair (tie-break: largest ARR for resolution, or smallest PSC — state yours) and
  reports exact/inexact + error ppm; test vectors (84 MHz → 1 kHz, 1 Hz, 50 Hz; impossible too-slow/too-fast;
  prime-ish ratios), property test vs brute force over all PSC for random targets. Mock timer model: CNT
  increments every (PSC+1) timer clocks, overflow at ARR → UIF set + ISR callback if UIE; PSC is ALWAYS
  preloaded (takes effect at the next update event) and ARR preloaded only if ARPE — reproduce the "first
  period is wrong unless you set EGR.UG after programming PSC" gotcha, and that UG itself sets UIF (fake first
  interrupt — clear UIF before enabling UIE; hedge the URS detail). Verify ISR period by counting mock clocks.
  Lane: no PWM / input capture (BM-M05/M06), APB×2 timer clock rule cross-ref B10.

## Code rules (all must hold — you MUST actually run these)
Mac-host lessons (everything except the firmware parts of B06/B08/B09):
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as
    (same ASan/UBSan for practice.c; demo_*.c must at least compile -Wall -Wextra -Werror and you run them)
- Build from the lesson dir if you #include a local .h or read data files (say so in the md build cmd).
- Private temp names only: /tmp/<ID>_* and /tmp/<ID>_work/ (Mac) and /tmp/<ID>_work/ (VM). 13 other writers run
  in parallel and have clobbered shared files before.
- No malloc in "firmware" code; static buffers; explicit capacities. Mock = small and readable.
- Deterministic tests: exhaustive where cheap, fixed-seed xorshift32 otherwise, named edge/fault cases; every
  fault the task names is injected and asserted. solution/practice < 2 s.
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure) so the diff
  highlights only changed lines. TODO stubs compile warning-free ((void)param), return wrong-but-safe values,
  never hang (any polling loop needs a bounded timeout so a stub can't spin forever).
- Paste only REAL output (tables, objdump, gdb, size, compiler errors). Never invent numbers.

QEMU/VM lanes (B05 captures, B06, B08, B09):
- Lima VM `elinux` (check `limactl list`; if Stopped, `limactl start elinux` — other sessions may also use it;
  do NOT stop it). Toolchain in VM: arm-none-eabi-gcc 14.2.1 (newlib + nano + rdimon specs),
  qemu-system-arm 10.2.1, gdb-multiarch. Run: `limactl shell elinux -- bash -lc '...'`.
- Lima mounts the Mac home READ-ONLY → scripts copy sources to VM `/tmp/<ID>_work` and build there; copy
  captured outputs back by printing them (or `limactl copy elinux:/tmp/... <lesson dir>/`).
- Firmware files start with `/* ENV: qemu */`. Strict flags in VM: `-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g
  -Wall -Wextra -Werror` (+ `-ffreestanding -nostdlib -fno-tree-loop-distribute-patterns` when not using newlib —
  GCC otherwise turns copy/zero loops into memcpy/memset calls, which is itself a teachable point for B07/B08).
- QEMU run must terminate on its own (semihosting SYS_EXIT, or `timeout 10 qemu-system-arm ...`); never leave
  QEMU processes running. The run script prints a final line `qemu exit=<n>` and returns non-zero on FAIL.
- In the md, give the exact one-line command Don runs from the Mac:
  `limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/<ID>/run_qemu.sh [solution|starter|practice|gdb]'`.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = fault/edge cases (injected bugs), C = firmware-realistic use
  (driver API used like real code, boot on QEMU, power-off flush, timeout handling).
- 풀이 detailed: register rule stated explicitly (bit positions, access type), step-by-step trace table on a
  small example (register values before/after each access, or boot steps with addresses), why each design
  choice, RAM/Flash cost where relevant.
- Accuracy over volume. Every register offset, bit, formula, limit double-checked; from-memory datasheet facts
  hedged ("RM0090 기준, 확인 권장"). Don (math major, has brought up real boards) will notice.

## When done
Reply with: files written (paths), md line count, PASS counts for starter/solution/practice (and QEMU run
results for qemu lanes), ASan/UBSan result, demo files + key real numbers, and every from-memory spec fact you
were unsure about. Do NOT edit files outside ROOT/lessons/BM/<your ID>/ (plus your private /tmp paths).
