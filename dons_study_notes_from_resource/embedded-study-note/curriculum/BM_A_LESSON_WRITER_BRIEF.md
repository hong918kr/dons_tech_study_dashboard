# Lesson writer brief — track BM (베어메탈 펌웨어), **Advanced** level (BM-A01–A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering (read ALL, this file wins on conflicts):
1. ROOT/NOTES_SPEC.md — lesson rules (front matter, headings verbatim and in order, svg rules, markers, `<details>`).
2. ROOT/curriculum/BM_B_LESSON_WRITER_BRIEF.md — BM base: shared mock vocabulary (REG_READ/REG_WRITE → mock_*),
   "Code rules", QEMU/VM rules, quality bar.
3. ROOT/curriculum/BM_M_LESSON_WRITER_BRIEF.md — Medium overrides, NVIC-lite sim convention, **address table**
   (NVIC/SCB/EXTI/USART/TIM/ADC/DMA/IWDG/WWDG/RTC — reuse it), M02/M13 QEMU lanes.
4. ROOT/curriculum/COMM_A_LESSON_WRITER_BRIEF.md sections "Overrides for Advanced" + "Verify" (ignore its lanes).
5. **This file** (BM-A overrides + per-item lanes).

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics roles (Anduril, Rivian, Tesla, Apple, NVIDIA). BM Advanced = the "you own the boot +
update + post-mortem path" layer of a senior firmware interview: "main 전에 뭘 하나? FPU 안 켜고 float 쓰면?",
"부트로더에서 앱으로 점프할 때 뭘 정리하나?", "플래시에 erase 없이 두 번 쓰면?", "업데이트 중 전원이 나가면?",
"이미지가 진짜인지 어떻게 아나?", "스택 얼마나 쓰는지 어떻게 재나?", "NULL 역참조를 어떻게 fault 로 바꾸나?",
"현장에서 죽은 장비 원인을 어떻게 회수하나?", "인터럽트 지연은 몇 사이클?". Don did SSD FW (power-loss
protection, FTL metadata) — tie flash/power-cut lessons to that experience.

## Overrides for Advanced
- Output: ROOT/lessons/BM/<ID>/**2026-11-15**_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, *.h, *.ld, *.py, run_qemu.sh, captured *.txt). Front matter exactly
  `---\nid: <ID>\ntime: <90분–130분>\nupdated: 2026-11-15\n---`. Title `# <ID> <title exactly as in BM.json>`.
- md **500–760 lines**, **≥3 svg** figures, **면접 Q&A ≥ 7** (aim 8–9, ≥2 staff-level follow-ups: "…라면 어떻게
  바꾸겠나?", "이 설계가 틀리는 경우는?"), 추가 연습 3–6 ending with `### 한 번에 검증` + `<!-- code: practice.c -->`
  + build/run cmd. §3 `<!-- code: starter.c -->`, §6 `<!-- code: solution.c diff=starter.c -->`.
- Parts: A = core mechanic, B = faults / corner cases interviewers probe (every fault the task names is INJECTED and
  its outcome ASSERTED), C = firmware-realistic integration (boot log / field report a bring-up engineer would read).
- Tests: exhaustive where cheap, else fixed-seed xorshift32 (≥ a few thousand cases) + named edge/fault cases. Each
  harness prints `N/M passed`, exit 1 on any failure. Host solution/practice < 2 s at -O2 (< 90 s ASan/UBSan); QEMU
  runs < 20 s each. No wall-clock numbers in CHECKs. Sweeps → demo_*.c with REAL output pasted.
- solution.c = starter.c with ONLY TODO bodies filled. Starters compile warning-free, FAIL (exit ≠ 0), never hang
  (bounded loops, step caps, QEMU `timeout`).
- **Bug-detection honesty**: inject ≥ 2 plausible mutants into a PRIVATE copy of solution.c (scratch dir) and confirm
  FAIL for each; report which. No mutants left in the lesson dir.
- **Self-contained**: no #include across lessons. Embed compact copies and label them: "(BM-A02 플래시 모델 축약판)",
  "(BM-A04 헤더 축약판)", "(BM-B02 mock 축약판)", "(BM-B08 startup 축약판)".
- Accuracy: anything computable (CRC/SHA values, sizes, cycle sums from your model) is printed by your code before it
  appears in the md. Spec facts you can't open (ARMv7-M ARM, Cortex-M3/M4 TRM, RM0090, MCUboot docs, YMODEM spec)
  → state as you know them + "(문서 확인 권장)"; in tests they are YOUR model's definition. WebFetch/WebSearch via
  ToolSearch are allowed for reading docs (cite URL in 참고자료). QEMU behaviour claims must be OBSERVED (probe it) or
  quoted from QEMU source with file:line — never assumed.

## Shared vocabularies (owners define; others embed a compact copy with the same names)
### Flash model (A02 owns; A05, A06 embed)
```c
typedef struct {
    uint32_t base;            /* e.g. 0x08000000 */
    uint32_t sector_size;     /* uniform in the compact copy (A02 may also model F4-style 16/64/128 KB sectors) */
    uint32_t n_sectors;
    uint32_t prog_unit;       /* bytes per program operation (1, 4 or 8); address must be aligned to it */
    uint8_t  mem[FLASH_MAX];  /* erased = 0xFF; program does mem &= data (1→0 only) */
    uint32_t n_erase, n_prog; /* op counters (wear, cost) */
    uint32_t op_index;        /* incremented per erase/program call — power-cut hook */
    uint32_t cut_at;          /* 0 = never; when op_index == cut_at the op is torn and FLASH_POWER_CUT returned */
    uint32_t rng;             /* xorshift32 state for undefined bits after a torn op */
} flash_t;
typedef enum { FLASH_OK=0, FLASH_ERR_ALIGN, FLASH_ERR_RANGE, FLASH_ERR_NOT_ERASED, FLASH_ERR_VERIFY,
               FLASH_POWER_CUT } flash_rc_t;
flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector);
flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len); /* raw: AND semantics */
void       flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len);
```
Torn erase: the sector ends with random bits (some 0xFF, some old, some random) from `rng`. Torn program: a random
subset of the 1→0 transitions of the unit happened. After FLASH_POWER_CUT every later op returns FLASH_POWER_CUT
until the test "reboots" (`flash_power_on(f)` clears cut_at).
### Image header (A04 owns; A05/A06/A11 embed a compact copy)
Little-endian, 64 bytes, at the start of the slot, payload follows immediately:
```c
typedef struct {
    uint32_t magic;          /* 0x474D4942 = "BIMG" bytes 'B','I','M','G' in memory */
    uint16_t hdr_version;    /* 1 */
    uint16_t hdr_size;       /* 64 */
    uint32_t img_version;    /* major<<24 | minor<<16 | patch (no build field) */
    uint32_t payload_size;   /* bytes after the header */
    uint32_t load_addr;      /* where the payload (vector table) must live */
    uint32_t flags;          /* bit0 = SHA256 present, others 0 */
    uint32_t payload_crc32;  /* CRC-32/ISO-HDLC (zlib) of payload */
    uint8_t  sha256[32];     /* SHA-256 of payload (A04 only; compact copies may skip it and set flags bit0 = 0) */
    uint32_t hdr_crc32;      /* CRC-32 of bytes [0, 60) */
} img_hdr_t;                 /* _Static_assert(sizeof == 64) */
```
### mps2 firmware (A01, A03, A07, A08, A09, A11)
B08/M13 pattern: own linker script, own startup, `-ffreestanding -nostdlib -fno-tree-loop-distribute-patterns`,
semihosting SYS_WRITE0 + SYS_EXIT_EXTENDED (or ADP_Stopped_ApplicationExit) to end the run, run_qemu.sh copies to VM
/tmp/<ID>_work, `timeout`, prints `qemu exit=<n>`. Read ROOT/lessons/BM/BM-B08 and BM-M13 run_qemu.sh + .ld first.
Reuse the M13 facts (memory: SYSRESETREQ resets the machine, RAM survives, ELF segments with FileSiz>0 are reloaded on
reset → crash data in NOLOAD .noinit; `-no-reboot` exits 0 on reset; QEMU mps2-an385 has 8 priority bits).

## Cross-references
- Written (cite by id, don't re-teach): all BM-B01–B14, BM-M01–M13 (M02 priority on QEMU, M10 watchdog HW, M13 fault
  policy + .noinit reset record on QEMU, M11 low power), ARM-B01–B14 (asm conventions, B13/B14 QEMU), ARM-M04/M05/M11,
  RTOS-M08 (task stack measurement, -fstack-usage .su, callgraph), RTOS-A01 (real mini-RTOS on mps2-an385), COMM-B/M/A
  (COMM-B08/B12 CRC, COMM-A06 SPI NOR program/erase protocol), ALGO-M01/M02 (CRC bitwise / table), C-A*, DS-*, SIV-*.
- Not written (cite as "다음 단계" only): ARM-M08 (asm vector table), ARM-M10 (PRIMASK/BASEPRI), ARM-M12 (CFSR/HFSR
  decoder + unwinding), ARM-A08 (interrupt latency deep dive), TOOL-M01 / TOOL-A07 (linker scripts), SEC-* (signature
  verification, secure boot), PLAT-M07 (A/B slot policy), TEST-B09 (watchdog task check-in).
- The other 10 BM-A items are being written IN PARALLEL right now. Stay in your lane.

## Lanes (stay in yours)
- **A01 SystemInit order (qemu, mps2-an386 = Cortex-M4F)**. FIRST verify on QEMU 10.2.1: `-M mps2-an386` boots your
  image, CPACR (0xE000ED88, CP10/CP11 bits [23:20]) reset value, a VFP instruction with CPACR=0 → UsageFault NOCP
  (CFSR bit 19) — or HardFault FORCED if UsageFault not enabled (show both), FPCCR (0xE000EF34) ASPEN/LSPEN reset values.
  Firmware: Reset_Handler order = (1) set CPACR full access + DSB/ISB, (2) copy .data / zero .bss, (3) copy vector
  table to a RAM array aligned per VTOR rules (table size rounded up to power of two ≥ 128 B; alignment rule — state
  it and the reason, hedge), write SCB->VTOR, DSB, (4) "clock switch" — mps2 has no RCC/PLL, so Part C = a HOST model
  (practice.c may be host C) of the STM32F4 sequence: HSE on → wait HSERDY (bounded) → PLL M/N/P/Q math for 168 MHz
  from 8 MHz (exhaustive search for valid M/N/P with VCO input 1–2 MHz, VCO 100–432 MHz — RM0090, hedge) → FLASH_ACR
  LATENCY raised BEFORE switching SW (and lowered AFTER when slowing down) → wait SWS; inject "latency set after
  switch" → model reads garbage → caught. Tests in firmware: float op after CPACR works; handler swap at runtime
  through the RAM table (replace SysTick or a free IRQ handler, pend it, assert the new handler ran); inject the
  wrong order (FPU enabled after a float in .data init / constructor-like code; VTOR not aligned) and show the fault.
  starter = CPACR + VTOR TODO → NOCP fault path recorded → exit 1. Lane: no full fault decoder (ARM-M12), no
  bootloader jump (A03).
- **A02 flash driver simulator (mac)**. Owns the flash model above (full version: optional F4-style non-uniform
  sectors, prog_unit 1/4/8, `FLASH_ERR_NOT_ERASED` policy when a program would need 0→1 — driver must detect BEFORE
  writing by checking `(old & new) == new`). Driver API: `drv_erase_range(addr,len)` (sector-aligned expansion rules
  stated), `drv_write(addr, buf, len)` handling unaligned head/tail via read-merge into an erased unit (only legal if
  the unit's untouched bytes stay 0xFF — explain), crossing sector boundaries, verify after program (read-back
  compare → FLASH_ERR_VERIFY). Tests: AND semantics exhaustive over byte pairs; reprogram without erase; unaligned
  head/tail; boundary crossing; torn erase / torn program at every op index of a scripted sequence → the driver's
  verify or a post-boot blank-check detects it (state what can and cannot be detected: a torn erase can READ as
  0xFF but be weakly erased — hedge, real NOR "partially erased" cells; model it with a `weak` bitmap that flips on
  later reads if you can keep it simple, else mention only). Part C: wear counter per sector + "erase count
  histogram" for a log-structured append workload vs naive rewrite (ties to Don's FTL experience). Lane: no YMODEM
  (A05), no swap/update (A06), no image format (A04). Cite COMM-A06 (SPI NOR protocol).
- **A03 bootloader → app jump (qemu, mps2-an385)**. Two images, two linker scripts (bootloader at 0x00000000 size
  e.g. 16 KB, app at 0x00004000 — your layout, defended; mps2-an385 code memory is 0x00000000 4 MB "ZBT SSRAM1" —
  verify in QEMU hw/arm/mps2.c or by probing), merged into ONE image (objcopy -O binary + padding, then
  `-kernel merged.elf` built by a third tiny link or `-device loader,file=app.bin,addr=0x4000` — choose and verify
  what QEMU actually loads; document). Bootloader: check app vector table sanity (SP in RAM range, reset vector odd/
  Thumb and inside app region), then: disable IRQs (cpsid i), stop SysTick (CTRL=0, clear pending via ICSR PENDSTCLR),
  disable + clear pending all NVIC lines (ICER/ICPR all words), (optionally) reset peripherals it used (CMSDK UART
  ctrl=0), set VTOR = app base, set MSP = app[0], DSB/ISB, `cpsie i` decision (who re-enables? state the contract),
  branch to app[1]. Tests in firmware: app asserts on entry that VTOR==its base, MSP==its top, no pending IRQ,
  SysTick off, PRIMASK as contracted; then app enables its own SysTick and checks ITS handler runs (not the
  bootloader's). Inject: forgetting VTOR (SysTick goes to the bootloader handler — detected by a marker), leaving
  SysTick running (fires before app set up → detected), jumping with bit0 clear (show what QEMU does — observe; real
  HW INVSTATE UsageFault), corrupt app vector (bootloader refuses, stays in bootloader). starter = jump TODO → exit 1.
  Lane: header/CRC verification is A04 (just a sanity check here; cite).
- **A04 image header + CRC/SHA-256 (mac + python)**. Owns the header format above. `tools/mkimg.py` (or `mkimg.py`,
  stdlib only: struct/zlib/hashlib; `python3 -I`) packs header + payload; C side: CRC-32 (table or bitwise; check
  value 0xCBF43926 for "123456789"), SHA-256 from scratch in C (FIPS 180-4; test vectors "abc", "" and the 448-bit
  message; cross-check vs python hashlib on random fixed-seed buffers of every length 0–300 and a few big ones),
  `img_verify(slot, slot_len, min_version, &out)` returning distinct error codes: BAD_MAGIC, BAD_HDR_VERSION,
  BAD_HDR_CRC, BAD_SIZE (payload_size > slot_len − hdr), BAD_LOAD_ADDR, BAD_CRC, BAD_SHA, ROLLBACK
  (img_version < min_version, anti-rollback counter stored separately — hedge where it lives: OTP/eFuse/flash),
  integer-overflow-safe size checks (payload_size near UINT32_MAX). Tests: every single-bit flip of a small image
  (header and payload) rejected with the right code; truncation at every length; rollback; python-made images
  verified by C and vice versa (C writes a test image file under /tmp/BM-A04_*, python verifies — or python
  generates fixtures checked in as small .bin files). Part C: boot decision log for two slots. Explain CRC vs
  SHA-256 vs signature (integrity vs authenticity) — signatures = SEC (cite). Optional demo: SHA-256 cycles/byte
  estimate on Cortex-M (hedge) vs host.
- **A05 serial bootloader: YMODEM receive + flash write (mac)**. Embed compact A02 flash model + compact A04 header
  check (CRC only). YMODEM receiver state machine (block 0 filename+size, 128-byte SOH / 1024-byte STX blocks,
  block number + complement, CRC-16/XMODEM (poly 0x1021 init 0, check 0x31C3), 'C' to start CRC mode, ACK/NAK,
  EOT → NAK → EOT → ACK (state the variant you implement, hedge), final null block 0, CAN CAN abort, timeouts with
  bounded retries). Byte-level API fed by a simulated UART (`rx_byte(t)` with timeouts in model ticks). Flash path:
  erase each sector just before first write into it (or erase whole slot up front — compare and defend), program
  each block, verify, keep the last-good state; duplicate block (sender didn't see ACK) must be ACKed and NOT
  re-written; file size from block 0 trims the 0x1A padding. Sender = a C model in the harness (plus optional
  python `send_ymodem.py`); if `sz`/`lsz` exists in the VM, cross-check against it — do NOT install packages.
  Fault injection (fixed seed): bit flips, dropped bytes, duplicated block, lost ACK, sender timeout, CAN mid-file,
  wrong block number, power cut mid-program (A02 cut hook) → image left invalid and bootloader refuses it at next
  boot. Tests assert final flash == file or a clean, detected failure. Lane: A/B slot policy (PLAT-M07, cite), swap
  (A06).
- **A06 power-fail-safe update (mac)**. Embed compact A02 flash model (cut_at hook) + compact A04 header (CRC).
  Implement ONE of: (a) MCUboot-style swap using scratch (primary/secondary/scratch, per-sector status trailer,
  resumable after power loss), or (b) dual metadata copies with sequence numbers + CRC (newest valid wins) driving an
  image copy/swap — pick, defend; mention the other + MCUboot "swap-move" and "overwrite-only" (hedge, docs).
  Exhaustive test: run a full update with cut_at = 1, 2, …, N (every flash op, both torn-erase and torn-program
  variants and a few RNG seeds), then "reboot" (resume logic) — invariant: after every cut + any number of
  further cuts during recovery (also test double cuts: cut during recovery at every op), the device boots a VALID
  image (old or new, never neither) and eventually the update completes or is cleanly reverted. Report N, number of
  scenarios, and also test-confirm/revert ("image must confirm itself after boot, else revert" — MCUboot test/
  confirm idea, hedge). Mutants: write status before data, single metadata copy, missing CRC on metadata. Part C:
  cost table (erase count per sector per update, time estimate with representative erase/program times — hedged).
- **A07 stack painting + high-water mark (qemu, mps2-an385)**. Bare-metal MSP stack (not RTOS tasks — RTOS-M08
  owns task stacks; cite). Startup paints the stack region with 0xDEADBEEF (from the stack LIMIT up to the current
  SP minus a safety margin — explain why you can't paint under your own frame), `stack_hwm()` scans from the bottom
  for the first non-pattern word; also a canary word at the bottom checked from SysTick/main loop. Tests: functions
  with known frames (measure with `-fstack-usage` .su files from arm-none-eabi-gcc in the VM — paste them) called at
  different depths → HWM ≥ static estimate along the call path, and explain gaps (ISR stacking 32 B / 104 B with FP,
  alignment, -O2 inlining); interrupt nesting adds frames (pend a free IRQ at depth). Overflow: recursion past the
  limit with a .bss guard/canary placed right below the stack (linker script) → detected (canary destroyed) — show
  what silently gets corrupted when undetected (a variable in .bss). Mention MSPLIM (v8-M only), MPU guard = A08
  (cite), -fstack-protector (per-function canary — optionally demonstrate with __stack_chk_guard/__stack_chk_fail
  in freestanding). Part C: report "stack used / reserved / margin" table like a release checklist; callgraph worst
  case (manual or a small script parsing .su + objdump call edges — keep simple, RTOS-M08 has callgraph.sh; cite).
- **A08 MPU (qemu, mps2-an385)**. FIRST probe MPU_TYPE (0xE000ED90) DREGION on QEMU (expect 8 for v7-M — verify).
  Read BM-M13 first: it already does ONE MPU fault (32 B RO guard). Your lane goes deeper: an `mpu_region_t` encoder
  `mpu_encode(base, size, ap, xn, tex_s_c_b, srd)` that validates the v7-M rules (size power of two ≥ 32 B, base
  aligned to size, SIZE field = log2(size)−1, subregions only for size ≥ 256 B, 8 subregions of size/8) — exhaustive
  host tests (practice.c may be host C) incl. rejected inputs; region priority (higher region number wins on
  overlap); PRIVDEFENA vs background region; HFNMIENA. Firmware: (1) NULL trap: region covering 0x0–0xFF no-access
  while the vector table stays readable — explain the conflict (vector table at 0!) and your resolution (VTOR moved
  to RAM/elsewhere, or subregion disable, or relying on PRIVDEFENA semantics for vector fetches — VERIFY on QEMU what
  happens to exception vector fetch, document), NULL read and NULL write → MemManage with MMARVALID and MMFAR = the
  address (DACCVIOL); (2) stack guard: 32 B no-access region at the stack bottom → recursion overflow → MemManage
  (MSTKERR if the fault happens during exception stacking — try to trigger that too: overflow inside an ISR entry);
  (3) XN RAM: copy a tiny Thumb function to RAM, call it → IACCVIOL (MMFAR not valid — show), then allow exec via a
  different region and it runs. Each fault: handler records CFSR/MMFAR/stacked PC, then skips/recovers (adjust stacked
  PC, or longjmp-like flag) and continues to the next test in ONE boot (contrast with M13's reset-per-fault).
  starter = encoder/region setup TODO → no fault → exit 1.
- **A09 crash dump in .noinit (qemu, mps2-an385)**. Read BM-M13 first: it already stores {magic, reason, CFSR, HFSR,
  BFAR/MMFAR, stacked PC} + FNV-1a in .noinit and resets. Your lane = a real POST-MORTEM format and tooling:
  versioned `crash_dump_t` (magic, version, size, boot_count, reset reason, EXC_RETURN, which stack (MSP/PSP from
  EXC_RETURN bit2), full register set: stacked r0–r3, r12, lr, pc, xpsr + r4–r11 captured by a naked asm entry stub,
  CFSR/HFSR/MMFAR/BFAR/SHCSR, N words of stack above the frame, CRC-32 over the struct), robust capture when SP is bad
  (stack overflow → switch to a small dedicated fault stack before calling C — inject and prove it), fault-in-fault
  guard (a second fault while dumping → the handler detects re-entry via a flag and resets immediately; QEMU lockup
  behaviour if any — observe), cold boot vs warm reset: QEMU RAM starts zeroed, so ALSO host-test `dump_valid()`
  against ≥ 1e6 random garbage fills (fixed seed) + the "valid magic, bad CRC" and "stale dump from an older version"
  cases; report false-accept count (expect 0) and reason about the probability. Reporting: next boot prints the dump
  via CMSDK UART0 (0x40004000; capture with `-serial file:` like BM-B09) as a single parseable line block; host
  `decode_dump.py` (stdlib) parses it, decodes CFSR bits you trigger, and symbolizes PC/LR using the ELF's `nm`
  output (run nm in the VM, save as a .txt). Inject ≥ 4 different faults across boots (one boot each, bounded).
  starter = capture/validate TODO → report missing → exit 1. Lane: full CFSR decoder/unwinder = ARM-M12 (cite).
- **A10 interrupt latency measurement (env: board → no board here)**. QEMU cannot do this (verified earlier: QEMU mps2
  DWT CYCCNT reads 0; QEMU has no cycle model). Do it honestly as THREE parts and say so up front in 1. 문제:
  (A) mac cycle MODEL of Cortex-M3/M4 exception timing with parameters from ARM documentation (hedged, cite the
  Cortex-M4 TRM / ARM app notes you rely on): entry 12 cycles at zero wait state, tail-chaining 6 cycles, late
  arrival (higher-priority IRQ arriving during stacking takes over the vector fetch), pop pre-emption, return 12
  cycles-ish (hedge), lazy FP stacking (FPCCR.LSPEN/ASPEN: extra cost only if the ISR touches the FPU; full FP frame
  26 words → state your assumed cycle cost), flash wait states adding to the vector fetch / first instruction fetch
  (ART accelerator hit vs miss — hedge), and the ISR prologue (push) cost. `latency_model(scenario) → cycles` with a
  table of named scenarios; tests assert the model's own arithmetic & orderings (tail-chain < back-to-back return+
  entry, late arrival latency of the high IRQ ≤ normal entry, etc.). (B) measurement firmware for a real STM32F4
  board in `board/` (files start `/* ENV: board */`, compiled in the VM with arm-none-eabi-gcc -mcpu=cortex-m4 to
  prove it builds; NOT run): DEMCR TRCENA, DWT_CTRL CYCCNTENA, DWT_LAR on M7 (hedge), timestamp at the GPIO/SWIER
  trigger write and at ISR first instruction, overhead calibration (back-to-back CYCCNT reads), N=1000 samples into
  a RAM buffer, dumped via UART. (C) host analysis tool (checked): parse such a dump (use a SYNTHETIC dump generated
  by your model + fixed-seed jitter, clearly labelled synthetic), subtract calibration, histogram/min/max/p99,
  detect bimodality (flash cache miss), and print the comparison table model vs "measured". Also run the
  measurement firmware's CYCCNT read on QEMU mps2-an386 once to SHOW it reads 0 (paste). Never present model numbers
  as measured. Cite ARM-A08 as the deep dive.
- **A11 capstone: bare-metal data logger (qemu, mps2-an385)**. Integrate (compact copies, labelled): own startup +
  linker script (B08), CMSDK APB timer0 (0x40000000, IRQ 8 — verify) periodic sampling at e.g. 100 Hz of a fake
  sensor (deterministic function of tick), ring buffer → interrupt-driven CMSDK UART0 TX (TX IRQ — verify the IRQ
  number and CMSDK UART TXINT behaviour in QEMU source), watchdog: CMSDK APB watchdog on mps2 if QEMU models it
  (verify address 0x40008000 and that it actually resets the machine; else a software watchdog via timer1 + reset
  via SYSRESETREQ — document which), HardFault dump to .noinit (compact A09 idea, simple), image header check at
  boot (compact A04 header; a post-link python step patches CRC into the ELF/bin — or the firmware checks a CRC
  over its own .text range computed by a host step; choose, document). Scenario script in ONE QEMU run (bounded):
  boot → log N samples (UART capture diffed vs expected_*.txt generated by a host model) → injected hang → watchdog
  reset → reset cause reported → injected fault → dump reported next boot → final summary + SYS_EXIT 0. Part C:
  memory usage table from `arm-none-eabi-size` + map (paste real), module/dependency diagram (svg), CPU load estimate.
  starter = integration glue TODO (e.g. ISR wiring/ring hand-off/watchdog kick policy) → exit 1.

## Code rules / QEMU rules
Exactly as in BM_B brief "Code rules" + COMM_A "Verify": SDKROOT=MacOSX26.4.sdk, strict -Werror, ASan/UBSan for host
solution + practice, deterministic, real output only. Private scratch: `<SCRATCH>/<ID>/` given in your prompt (Mac)
and VM `/tmp/<ID>_work/` only. Lima VM `elinux` (shared; never stop it; check `limactl list`; Mac home READ-ONLY in
VM). Toolchain in VM: arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch, python3. QEMU source: if you
need hw/arm/mps2.c etc., check whether a source tree exists in the VM (`/usr/share/doc`, apt source not allowed)
— otherwise WebFetch the v10.2.x file from gitlab/github and cite the URL, or probe behaviour directly.
Firmware files start `/* ENV: qemu */` (A10 board files `/* ENV: board */`). For qemu lanes the checker runs
`limactl shell elinux -- bash -lc 'bash <dir>/run_qemu.sh starter|solution|practice'` (starter exit ≠ 0, solution/
practice 0); if practice.c is host C, it must NOT start with `/* ENV: qemu */`. Give the exact Mac one-liner in the md.
Don't install packages. Don't run build_site.py or git. Don't edit outside ROOT/lessons/BM/<ID>/.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice (+ QEMU results), ASan/UBSan,
mutants injected + caught, demo files + key real numbers, every QEMU behaviour you verified (and how), and every
from-memory spec fact you were unsure about.
