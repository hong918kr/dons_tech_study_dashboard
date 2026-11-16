# Lesson writer brief — track MEM (메모리 시스템: DRAM · ECC · memtest · 측정 · NAND), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Rivian silicon validation, Anduril, Apple, NVIDIA,
Tesla...). He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
MEM-B is the **memory-system** track at Basic level: DRAM organisation/commands/timing/refresh as small C models,
parity/Hamming/SECDED ECC, memory-test patterns, real latency/bandwidth measurement on this Mac, and a NAND chip
model that MEM-M10..M14 (FTL) will build on. C Basic/Medium/Advanced, DS B/M/A, ALGO B/M/A, ARCH B/M/A, OS B/M,
COMM B/M/A, BM B/M, SIV B/M/A, TOOL-B lessons exist — assume he knows them and cross-reference by id
("(ARCH-M05 참고)") instead of re-teaching.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/MEM.json — find your item (title/goal/task/tags). The lesson must do the item's `task`
   completely (every function, every injected fault, every measurement it names). Skim the other MEM items
   (B, M, A) so you stay in your lane: the other 11 B items are being written in parallel right now — reference
   them by id, don't depend on their files. **Every .c file must be self-contained** (if you need another item's
   model, embed a compact version and say "(MEM-B01 모델 축약판)").
3. Reference lessons for depth/tone/harness: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed",
   return 1 on failure) and ROOT/lessons/COMM/COMM-B08/ (recent Basic lesson with a demo_*.c experiment and
   measured tables). Related existing lessons — read only what you need, cross-ref by id:
   SIV-B05 (walking 1/0 on registers), SIV-B07 (SRAM March C- engine + fault injection), SIV-M14 (ECC error
   injection, CE/UE reporting), COMM-B08 (CRC/checksum Hamming distance), ARCH-B10 (AMAT), ARCH-M04/M05/M06
   (cache simulators), ARCH-M07 (loop order/tiling), ARCH-A02/A03 (TLB model, real TLB-miss cost on this Mac),
   ARCH-M08 (false sharing), MEM-M04 (coherence traffic measured), DS-A09 (FTL mapping table + GC victim),
   OS-B10/B11 (paging). Not yet written (cite by id only): MEM-M01..M14, MEM-A01..A10, RF-M01, AIPERF-B04,
   TEST-M11, CAP-M05.

## Output
ROOT/lessons/MEM/<ID>/2026-11-15_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(+ demo_*.c where the lane says so). Front matter `updated: 2026-11-15`, `time:` realistic for Basic (40–70분).
- md title line: `# <ID> <title exactly as in MEM.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥6, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
  Never put a line starting with `## ` inside a code fence other than these headings (checker scans outside
  fences, but keep it clean anyway).
- Size: ~450–650 lines of md.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text). Draw the real thing: address bit-field layout with your exact
  bit positions, bank state machine + command timeline with tRCD/CL/tRP spans to scale, row-buffer hit/conflict
  timelines, Hamming parity-coverage matrix, March element sequence, latency-vs-working-set curve / bandwidth
  bars with YOUR measured numbers (say so in a caption), NAND plane/block/page/OOB layout.
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in interviews / bring-up. Tie to firmware reality: DDR bring-up and memtest on a new board, ECC
  CE/UE logs (EDAC), SoC memory controllers, Don's SSD firmware background (NAND, FTL, ECC, P/E cycles) where it
  fits.

## Interview flavour
- 2. Problem (English) reads like a real firmware / silicon-validation / systems coding-round prompt
  (signatures, constraints, examples with I/O — e.g. "decode 0x1_2345_6740 under mapping M → ch=0 rank=0 bg=1
  bank=2 row=0x48D1 col=0x0E8").
- 면접 Q&A = real follow-ups: why XOR bank hashing, why tRAS exists, what dominates a row-miss latency, open vs
  close page on a GPU vs CPU, why DDR5 split the channel, how much bandwidth refresh costs at 16 Gb and why it
  gets worse, why SECDED needs the extra parity bit, why 72/64, what a memtest that passes still misses, why
  walking-1 on the address bus uses power-of-two offsets, why March needs both ⇑ and ⇓, why pointer chasing
  needs a random cyclic permutation, why STREAM arrays must be ≥4×LLC, why NAND programs pages in order, what
  erase-before-write does to an FTL.

## Shared definitions (FIXED — parallel lessons must agree; label them "이 레슨의 정의 (JESD79-4 / 데이터시트
## 대표값 — 실제 부품은 데이터시트 확인)")
- **DDR4-3200AA (22-22-22)** timing set used by B02/B03/B05: tCK = 0.625 ns (1600 MHz clock, 3200 MT/s),
  CL = 22, tRCD = 13.75 ns (22 cyc), tRP = 13.75 ns (22 cyc), tRAS = 32 ns (51.2 → **52 cyc**),
  tRC = tRAS + tRP = 45.75 ns (73.2 → **74 cyc**), tWR = 15 ns (**24 cyc**), CWL = 16, BL8 = 4 tCK of data,
  tRTP = 7.5 ns (12 cyc). **ns → cycles = ceil(ns / tCK) computed in integer picoseconds** (no float ceil
  surprises: 13.75 ns / 0.625 ns must give exactly 22). tREFI = 7.8 µs (≤85 °C), 3.9 µs (85–95 °C, "2x").
  tRFC1 by density: 4 Gb 260 ns, 8 Gb 350 ns, 16 Gb 550 ns (DDR4 tRFC1; 2 Gb 160 ns if you need it).
  DDR5 (only B04/B05 discussion, hedged): tREFI 3.9 µs, tRFC1 16 Gb ≈ 295 ns, same-bank tRFCsb ≈ 130 ns.
- Latency vocabulary for B03/B05 (in DDR4-3200 cycles, data return = last data beat): row hit = CL + 4;
  row empty (closed) = tRCD + CL + 4; row conflict = tRP + tRCD + CL + 4, **plus** any wait for tRAS to expire
  since the conflicting row's ACT (B03 must model that tRAS wait in its "exact" mode or explicitly state it is
  ignored in the simple mode — pick one, say which, and keep the numbers consistent with that choice).
- DRAM geometry vocabulary (B01 owns the decoder): channel, rank, bank group (BG), bank, row, column.
  A DDR4 x8 8 Gb device: 4 BG × 4 banks, 64 K rows, 1 K columns; 64-bit channel → column addresses 8-byte units,
  a 64 B cache line = one BL8 burst. B03/B05 embed a compact "flat bank index + row" mapping and say so.
- Memory-test vocabulary for B08/B09: fault models SAF (stuck-at-0/1 cell), TF (transition ↑/↓ fail),
  CFin (inversion coupling: aggressor transition flips victim), AF (address decoder: alias / no access / multiple
  access), data-line short (wired-AND / wired-OR between two data lines), address-line stuck/short/open → alias.
  Failure report format (same as SIV lessons): `FAIL addr=0x.. exp=0x.. act=0x.. diff=0x.. bits=[..]`.
- NAND vocabulary for B12 (MEM-M10..M14 build on it, keep the API small and clean): planes → blocks → pages,
  page = data + OOB (spare) bytes; erase whole block → all 0xFF; program only an erased page, pages in a block
  programmed **in increasing order** (no skipping back; skipping forward is allowed only if you state the rule —
  real TLC/QLC parts often require strictly sequential — pick and justify), NOP = 1 (one program per page per
  erase cycle, MLC/TLC); P/E count per block; endurance per cell type (SLC ~50–100 K, MLC ~3–10 K, TLC ~1–3 K,
  QLC ~0.1–1 K, tR/tPROG/tBERS typical values — all 대표값, hedged). Return codes, not asserts.

## Platform facts (this Mac)
- Host CPU = **Apple M6** (the JSON says "M2" because it was written earlier — measure on this machine, write
  "M6 (이 맥)" and keep the item title unchanged). Apple M6 has **3 performance levels** (hw.perflevel0/1/2:
  Super 2 + Performance 4 + Efficiency 6) — `sysctl hw.perflevel0` alone misses cores. Read cache sizes with
  `sysctl -a | grep -E 'perflevel[0-9].*(l1d|l2)cache|cachelinesize|pagesize'` and paste the real output.
  hw.cachelinesize = 128, page size 16 KB. cntvct_el0 ticks ≈ 41.67 ns (too coarse alone) → time with
  `clock_gettime(CLOCK_MONOTONIC_RAW)` / `mach_absolute_time` over many iterations.
- macOS cannot pin threads to cores (thread_policy_set affinity → KERN_NOT_SUPPORTED). Use QoS
  (`pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)`) to prefer fast cores and say so.
- The machine is shared: other agents run builds in parallel (load average 10–20 right now). Benchmarks will be
  noisy → report min-of-N or median-of-N, print `uptime` load average next to the table, and hedge in the md
  ("측정 시 load avg X — 한가할 때 demo 재실행 권장"). Never invent or "clean up" numbers.
- Unified memory spec (LPDDR type / bus width / peak GB/s) for M6: only state it if you verified it from Apple's
  page or a reputable source you actually fetched — otherwise compute peak from an explicitly labelled assumption
  and hedge "공식 수치 확인 권장".
- Build C: `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` (27.2 SDK link broken).
  Disassembly: /usr/bin/objdump or /Library/Developer/CommandLineTools/usr/bin/llvm-objdump (the
  /usr/local/bin/llvm-* tools are LLVM 3.2 and broken). `clang -S -O2` to confirm a loop wasn't deleted.

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # compiles, 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as   # clean
    (same ASan/UBSan run for practice.c; add -lpthread if you use threads, and then also run -fsanitize=thread)
- Use unique temp names (/tmp/<ID>_*) and a PRIVATE scratch dir /tmp/<ID>_work/ for helpers — 11 other agents
  run in parallel and have clobbered shared helper files before.
- solution.c and practice.c each finish in < 2 s and are **deterministic** (no wall-clock-dependent PASS/FAIL).
  For the measurement items (B10/B11) the timing experiment lives in demo_*.c; solution.c tests the
  deterministic building blocks (see lanes).
- Tests are real: exhaustive where cheap (all 16 data × 7 positions, all single-bit positions of 72, all
  addresses of a small memory × all injected faults), otherwise fixed-seed xorshift32/64 random tests with enough
  cases to mean something + named edge cases. Every fault the task names must be injected and the expected
  detection/flag asserted.
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure where possible) so
  a diff highlights just the changed lines. Header comment same in both. starter.c TODO stubs compile
  warning-free ((void)param;) and return a wrong-but-safe value (no crash, no UB, no hang — a stub inside a loop
  must still let the loop terminate).
- Static buffers / caller-provided buffers with explicit capacities in "firmware-style" code; malloc is fine in
  benchmarks (B10/B11 need hundreds of MB — use mmap or malloc and check failure).
- Long experiments (1 M random 2-bit errors, refresh-schedule latency distributions, latency/bandwidth sweeps,
  coverage matrices over many seeds) go in demo_*.c in the lesson dir; run them and paste the REAL output into
  the md as tables (with the command used). Never invent output.

## Lanes (stay in yours; cross-ref the others)
- **B01** Configurable address mapping: a mapping = ordered list of bit fields (e.g. `{COL_LO 3..9}`), plus
  optional XOR hashing of bank/BG bits with selected row bits. `decode(pa) → {ch,rank,bg,bank,row,col}` and
  `encode(loc) → pa`, both total and inverse on the valid range (offset bits [2:0] = byte within the 8 B bus
  beat — say so). Tests: exhaustive round trip on a small config, random round trip on the realistic DDR4
  config, invalid/overflow inputs. Part B: distribution of N consecutive 64 B lines / stride-2^k accesses over
  banks with and without XOR hashing (print the histogram; show the power-of-two-stride bank camping that XOR
  fixes). Part C: two named "real-ish" schemes (RoBaRaCoCh-style vs a cache-line-interleaved one) and one
  Intel/AMD-style XOR example — hedge vendor specifics ("공개 리버스 엔지니어링 논문 기준, 예: DRAMA"). Scheduling
  impact = MEM-M02 (cite).
- **B02** Single-bank state machine IDLE/ACTIVE(+row) with a cycle-stamped command trace (ACT r / RD / WR / PRE,
  optional PREA/REF not needed). Timing checker reports every violation with rule name, cycles required vs
  actual: tRCD (ACT→RD/WR), tRAS (ACT→PRE), tRP (PRE→ACT), tRC (ACT→ACT), tWR (end of write data → PRE, i.e.
  WR + CWL + 4 + tWR), tRTP (RD→PRE), plus illegal-state errors (RD on IDLE, ACT on ACTIVE). Report CL as the
  data-return time of each RD (not a "violation"). ns↔cycle conversion in integer ps with the DDR4-3200AA table
  (verify 22/22/22/52/74/24/12). Tests: clean trace passes, one trace per violation type is caught with the exact
  rule, multiple violations in one trace all reported, boundary (exactly tRCD = OK, tRCD−1 = violation). Part C:
  "fix the trace" — earliest-legal-issue function that delays commands to the minimum legal cycle; show the
  resulting command timeline figure. Bank groups/tFAW/tRRD = MEM-A01 (cite).
- **B03** Row-buffer classifier + open-page vs close-page latency simulator on multiple banks (compact mapping
  embedded). Workloads generated deterministically: sequential, stride (several strides incl. row-sized and
  bank-camping strides), random (xorshift), and a "two rows ping-pong in one bank" worst case. For each policy:
  counts hit/empty/conflict, mean latency in cycles and ns, and a recommendation table (which workload favours
  which policy, and the break-even hit rate where open = close — derive it algebraically and confirm with the
  sim). Requests serviced in order, one at a time (no scheduling — FR-FCFS = MEM-M01). Optionally an "adaptive
  timeout" policy as 추가 연습.
- **B04** Generation comparison table + calculator: DDR4 (64-bit channel, BL8), DDR5 (2 × 32-bit independent
  subchannels per DIMM, BL16, on-die ECC note), LPDDR4/4X (x16 channel, BL16/32), LPDDR5/5X (x16 channel, BL16/32,
  WCK:CK 4:1/2:1 — hedged), plus HBM2e/HBM3 one row only as preview (HBM details = MEM-A03). Functions: peak
  bandwidth = MT/s × bus bytes × channels (exact integer MB/s), bursts needed for a 64 B line (and the
  "LPDDR x16 BL32 = 64 B" / "DDR5 subchannel BL16 = 64 B" point), burst duration in ns, and a "system" calculator
  (e.g. 2-DIMM DDR5-5600 desktop, 128-bit LPDDR5X-8533 laptop, 4-stack HBM3 accelerator) — every config value
  labelled 대표값 unless you checked the source. Tests: exact known values (DDR4-3200 64-bit = 25 600 MB/s,
  DDR5-4800 DIMM = 38 400 MB/s, LPDDR5-6400 x16 = 12 800 MB/s), burst counts, invalid configs rejected.
  Pin/energy/prefetch-n (8n vs 16n) explained in 풀이.
- **B05** Refresh: Part A overhead calculator = tRFC / tREFI per density and temperature (4/8/16 Gb, 1x vs 2x
  rate; DDR5 per-bank / same-bank refresh row hedged) with exact rational output (e.g. 350/7800 = 4.487 %).
  Part B: simulator on a few banks with a request stream (fixed seed, Poisson-ish arrivals) — all-bank refresh
  blocks every bank for tRFC each tREFI; per-bank (same-bank) refresh blocks one bank for a shorter tRFCpb on a
  rotating schedule; measure the request latency distribution (mean, p50, p99, max) for no-refresh / all-bank /
  per-bank and paste the table. Part C: postponement (DDR4 allows up to 8 REF postponed / pulled in — hedged) and
  why refresh cost grows with density; retention (64 ms / 8192 REF) arithmetic. Deterministic: solution.c asserts
  exact overhead fractions and simple-schedule latencies; the large distribution run is demo_refresh.c.
- **B06** Even parity (detects odd-weight errors) + Hamming(7,4) with the classic positions (p1 p2 d1 p4 d2 d3
  d4 = bit positions 1..7, syndrome = error position). Bit-operations only, encoder/decoder/syndrome. Exhaustive:
  16 data × {no error, 7 single-bit} → 100 % correct; all 16 × C(7,2)=21 double errors → show they are ALL
  miscorrected to a wrong codeword (count them, explain why HD = 3). Part C: extended Hamming(8,4) SECDED preview
  (overall parity) only as 추가 연습 — the 72/64 build is B07. BER/BLER over a channel = RF-M01 (cite).
- **B07** SECDED (72,64): 64 data + 7 Hamming check bits + 1 overall parity. Pick and document a concrete
  layout (classic positional Hamming with check bits at powers of two, OR a systematic H-matrix with 8-bit check
  byte; Hsiao odd-weight-column code only as discussion/추가 연습). Encoder, decoder returning {NO_ERR, CE data
  bit k, CE check bit, UE}, syndrome → bit-position table built at init (show its size). Tests: all 72
  single-bit positions × several data words corrected; 1 M random 2-bit errors (fixed seed) → all detected as UE,
  0 miscorrection (this can live in solution.c if < 2 s, else demo); 3-bit errors → measure and show some are
  miscorrected (honest limit). Part C: "memory controller read path" tiny API (ecc_read returns data + status,
  CE counter) — CE/UE reporting policy = SIV-M14 / MEM-A06 (cite, don't repeat).
- **B08** Barr-style bring-up tests ("Fast Accurate Memory Test Code in C", Michael Barr, Embedded Systems
  Programming 2000 — cite, hedged): `memTestDataBus` (walking 1 at ONE address), `memTestAddressBus`
  (power-of-two offsets, write pattern/antipattern, check aliases both ways), `memTestDevice` (increment
  pattern + inverse across the whole device). Fault-injectable memory model (word-addressed, e.g. 32-bit × 4096):
  stuck-at-0/1 data line, data-line short (wired-AND and wired-OR between two lines), stuck-at address line
  (high/low), shorted address lines, open address line (→ alias), single dead cell. Output a detection matrix
  (fault × test → detected / missed) and explain every "missed". SIV-B05 did walking-1/0 on registers — cite it;
  your lane is the 3-test bring-up sequence on a RAM with address faults. March = B09.
- **B09** March notation interpreter: parse a text string like
  `"{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}"` (accept ASCII aliases too: `^`/`up`, `v`/`down`,
  `*`/`any` — define the grammar in the md) into an element list, then run it on a bit-oriented fault-injectable
  memory. Algorithms: MATS+, March C-, March B (+ March X or MATS++ optional), from the string — no hand-coded
  loops per algorithm. Fault models SAF0/1, TF↑/↓, CFin (aggressor < victim and aggressor > victim, ↑ and ↓), AF
  (alias / no-access / multi-access). Inject every instance of each fault over a small memory (e.g. n = 16 or
  32 cells × all positions / pairs) and print a coverage table (algorithm × fault model → %), plus complexity
  (operations = k·n). Compare with the literature claims (van de Goor) and say where your model differs.
  SIV-B07 has an element-table engine for March C- on word SRAM — cite it; your lane is the parser + multiple
  algorithms + fault-model coverage matrix.
- **B10** Pointer chasing: build a random **single-cycle** permutation (Sattolo's algorithm) over cache-line-sized
  nodes (use 128 B = hw.cachelinesize on M6, and also show 64 B), so every load depends on the previous one.
  solution.c tests the deterministic parts: Sattolo produces exactly one cycle of length n for many n/seeds,
  stride/page-walk chain construction, a "plateau/boundary finder" that detects level changes in a latency curve
  (tested on synthetic curves), units conversion. demo_chase.c does the real sweep 4 KB … 1 GB (powers of 2 and
  √2 steps), ns/load (median of N), defeats the optimiser (sum result printed / volatile sink, check with
  `clang -S`), and a page-stride variant (one line per 16 KB page) to expose TLB reach. Paste the real table and
  draw the curve; map plateaus to sysctl L1/L2/SLC(not in sysctl — hedge)/DRAM. Compare to ARCH-A03's TLB numbers
  (cite) and explain why a sequential chain is fast (prefetch) — show it measured.
- **B11** STREAM Copy/Scale/Add/Triad implemented by you (not the official stream.c, but same kernel
  definitions and the same byte-counting convention: Copy/Scale 16 B/iter, Add/Triad 24 B/iter of double, NOT
  counting write-allocate). solution.c tests deterministic parts: kernels produce exact results (validation like
  STREAM's checker), byte accounting, array-size rule (≥ 4 × LLC or ≥ 1 M elements — define LLC assumption),
  thread work partitioning (contiguous chunks cover [0,n) exactly once for t = 1..8 and awkward n).
  demo_stream.c: pthreads 1–8 (and up to the core count, say which), best-of-N GB/s per kernel, confirm the loops
  are vectorised/not deleted with `clang -S`, and a write-allocate experiment (STREAM-reported vs "actual
  traffic incl. RFO" = 3/2 or 4/3 factor; optionally non-temporal stores via `stnp` inline asm or
  memset/`__builtin_nontemporal_store` — report what you saw). Efficiency vs peak with the peak's source/assumption
  labelled. Roofline use of the number = AIPERF-B04 / MEM-A08 (cite).
- **B12** NAND chip model in C (no FTL): geometry struct (planes, blocks/plane, pages/block, page bytes, OOB
  bytes, cell type SLC/MLC/TLC with endurance and tR/tPROG/tBERS 대표값), operations `nand_read(page)`,
  `nand_program(page, data, oob)`, `nand_erase(block)` with return codes: ERR_NOT_ERASED (program twice /
  partial-program beyond NOP), ERR_OUT_OF_ORDER (program page < last programmed page in block), ERR_BAD_BLOCK,
  ERR_WORN_OUT (P/E ≥ endurance → erase/program fail status), ERR_RANGE. Erased read = 0xFF in data + OOB.
  Factory-bad marker in OOB byte 0 of page 0 (≠ 0xFF) — hedged ("ONFI/vendor 관례, 데이터시트 확인"). Busy-time
  accounting (accumulated µs per plane) for a later performance discussion. Tests for every rule + a "naive
  in-place update" workload that fails, motivating out-of-place writes (FTL = MEM-M10..M14, DS-A09 cite). Keep
  the API small so MEM-M10 can embed it.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = fault/edge cases or the comparison experiment, C = firmware /
  bring-up realistic use (decoder for a crash-dump physical address, bring-up test sequence, ECC read path,
  "which page policy for this SoC", NAND driver contract).
- 풀이 is detailed: rule stated explicitly, step-by-step trace table on a small example, derivations
  (break-even hit rate, refresh fraction, Hamming syndrome algebra, why HD=3/4), complexity/RAM where relevant.
- Accuracy over volume. Double-check every timing number, formula, spec claim. JEDEC documents are paywalled —
  label numbers you couldn't check "대표값 (JESD79-4 / 데이터시트 확인 권장)". Don (math major, ex-SSD FW) will
  notice wrong NAND or ECC facts.

## When done
Reply with: files written (paths), md line count, PASS counts for starter/solution/practice, ASan/UBSan result,
demo files + key measured numbers (with load avg), and anything you were unsure about (from-memory spec facts).
Do NOT edit files outside ROOT/lessons/MEM/<your ID>/ (and your private /tmp/<ID>_* paths).
