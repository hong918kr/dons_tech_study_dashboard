# Lesson writer brief — track DS (임베디드 자료구조), **Advanced** level (DS-A02, DS-A05–DS-A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/curriculum/DS_M_LESSON_WRITER_BRIEF.md (DS base rules) → **this file** (Advanced overrides).
Read both plus ROOT/NOTES_SPEC.md. Everything in the DS-M brief applies (output files, headings verbatim and in
order, svg rules, strict build, ASan/UBSan, fixed-seed PRNG, invariant check + reference model, no malloc in the
structure, demo_*.c for observations, "never invent output") unless overridden here.

## Overrides for Advanced
- Level: senior/staff firmware interview depth. The question is the item's `task`; follow-ups probe *design
  judgement* (why this layout, what the worst case really is, what breaks on another core/compiler/flash part, how
  you would test it). Assume DS-B01–B14 and DS-M01–M14 are known; cross-reference by id instead of re-teaching.
  Read the 1–2 lessons closest to your item (see your lane below) so you build on them. Don is an ex-SSD firmware
  engineer: FTL/NAND/flash content must survive scrutiny from someone who has shipped it.
- Reference lesson for depth/style: ROOT/lessons/DS/DS-A04/ (md + demos). Advanced md is **500–750 lines**.
- Parts: A = core implementation, B = the hard part / edge cases, C = firmware-realistic integration — each with
  checked tests. Q&A ≥ 6. 추가 연습 3–6.
- Output: ROOT/lessons/DS/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Front matter `updated: 2026-11-08`, `time:` realistic (75분–120분). Title `# <ID> <title exactly as in DS.json>`.
- Neighbouring items are being written IN PARALLEL: reference by id, don't depend on their files; every .c is
  self-contained.
- Test scale: meet the count the item's `task` names (e.g. 100만 회) in solution.c. Budget: whole program < 5 s at
  -O2, < 90 s under ASan/UBSan (if the full count blows the sanitizer budget, scale it with a documented
  `#if __has_feature(address_sanitizer)`, and say so in the md). Full invariant check after every op on small n;
  on large n every K ops + at end (state K).
- Checked tests must be deterministic. Timing never goes into a CHECK: benchmarks live in demo_bench.c (or
  demo_*.c), run ≥ 3 times, report the median + spread, built with -O2.
- Host is **Apple M6** (the item text may say M2 — report the real CPU; `hw.cachelinesize`=128). Timer facts:
  `cntvct_el0` / `CLOCK_UPTIME_RAW` tick ≈ 41.7 ns, so per-op timing of a sub-100 ns operation is quantised — when
  the item needs worst-case per-op cost, ALSO report a platform-independent work count (probes, list steps,
  rotations, bitmap ops), and be honest about what the wall-clock tail can and can't show (interrupts, E-core
  migration; no thread pinning available on macOS).
- Asm comparisons, where they help: real output from `cc -O2 -S` (arm64) and
  `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o -` on a freestanding demo_*.c. Never claim
  an instruction you didn't observe.
- Accuracy: don't cite a kernel/RTOS file, function, paper result or constant unless you've checked it (the Linux
  source isn't local; `WebFetch`/`WebSearch` are available via ToolSearch for papers and docs) — otherwise hedge
  or drop it. Paper-derived numbers need the paper named; measured numbers need your program's real output.

## Workspace
- Private scratch dir for temp binaries/helpers: `<SCRATCH>/<ID>/` (SCRATCH is in your prompt). Other writers are
  running at the same time and shared helper names clobber each other — never write to a shared name or to /tmp.
- Don't edit anything outside ROOT/lessons/DS/<your ID>/. Don't run build_site.py or git.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    B=<SCRATCH>/<ID>
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] starter.c  -o $B/st && $B/st   # some FAIL, exit 1, no hang/crash
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] solution.c -o $B/so && $B/so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] practice.c -o $B/pr && $B/pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer [-pthread] solution.c -o $B/san && $B/san
    (same for practice.c; plus -fsanitize=thread if threads are used)                # clean
- solution.c = starter.c with only TODO bodies filled (the site highlights the diff). Stubs are warning-free and
  return wrong-but-safe values.
- Bug-detection honesty: tests must actually catch wrong code. Before finishing, inject at least 2 plausible bugs
  into a private copy of solution.c (e.g. a wrong rotation case, skipped merge, off-by-one in a bitmap, a CRC not
  checked) and confirm the harness FAILs; say which ones in your reply. Don't leave the mutants in the lesson dir.
- Self-check the md: headings verbatim & in order, ≥2 ```svg with viewBox (width ≤ 760), Q&A ≥6 as
  `<details><summary>Q1. ...`, `### 한 번에 검증` + `<!-- code: practice.c -->`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6.

## Lanes
- **DS-A02** ISR↔태스크 링 + Bip Buffer. Builds on DS-B04, DS-M10, DS-A01 (DS-A01 owns SMP SPSC + acquire/release;
  don't re-derive it). Own: single-core ISR producer barrier analysis — what a compiler barrier suffices for, what
  needs `dmb` / what changes on Cortex-M7 + cache / multi-core, annotated line by line, backed by real thumbv7em asm.
  Then bip buffer reserve/commit/read/release + every wrap scenario (including reserve that can't fit at the tail,
  the watermark/"invalidate" region, release that crosses to region A). Part C: UART RX DMA / TX scatter-free
  transmit using contiguous regions; a deterministic single-thread interleaver that fires the "ISR" at every step
  is the checked test. RUST-M07 is the Rust analogue; one-line link.
- **DS-A05** 침입형 RB 트리. Builds on DS-M01 (container_of), DS-M07 (AVL). Own: Linux-style `rb_node` with colour in
  the parent pointer's low bit (state the alignment assumption with _Static_assert), insert/erase/next, all fixup
  cases with figures, invariant checker (root black, no red-red, equal black height, BST order, parent links), 1M
  random ops against a reference model. Part C: a firmware-flavoured use (timeout queue or CFS-like run queue with
  a cached leftmost; OS-A04 owns CFS itself). Compare RB vs AVL height/rotation counts with measured numbers.
- **DS-A06** TLSF. Builds on DS-M02/M03/M04, DS-M09. Own: FL/SL mapping (rounding up on malloc search; off-by-one
  between insert mapping and search mapping is the classic bug), bitmaps via clz/ctz, boundary tags + physical
  neighbour coalescing, O(1) proof by counted work. Heap invariant checker (physical walk vs free lists vs bitmaps).
  demo_bench.c: worst-case distribution and fragmentation vs fixed pool vs system malloc (malloc only there).
  Cite the Masmano et al. paper only for what you verified.
- **DS-A07** Cuckoo. Builds on DS-M08. NETHW-M05 owns bucketed cuckoo + stash; STOR-B07 owns cuckoo filter. Own: two
  tables / two hashes / 1 slot per bucket, eviction chain, cycle detection, rehash with a new seed, no-malloc rehash
  strategy, invariant "every key is in one of its 2 slots". Measure: probe count ≤ 2 for every lookup; load-factor
  ceiling (expected near 50% for this variant — report what you measure over many seeds, not the theory alone);
  eviction chain length distribution; worst-case insert cost honesty (lookup is O(1), insert isn't).
- **DS-A08** B+ 트리. Builds on DS-A05/M07 for contrast. STOR-M01/M02 own buffer pool, on-disk format, bulk load,
  prefix compression; STOR-M10 owns latching. Own: 4KB page nodes in a static in-memory page pool, page-id links (no
  raw pointers), insert with split, delete with redistribute + merge + root collapse, range scan on linked leaves.
  Invariant checker: sorted, min/max fill, uniform depth, separator correctness, leaf chain = in-order. Random ops
  vs a reference model. Report fanout, height vs n, page fill, page-touches per op.
- **DS-A09** FTL 매핑 + GC. Builds on DS-M11 (indexed heap). MEM-M10/M11/M12/M14 own the FTL course (L2P lifecycle,
  wear leveling, power loss); CAP-M05 is the capstone. Own: the *data structures*: L2P array, per-block valid bitmap
  + count, P2L/OOB-equivalent for GC relocation, indexed heap keyed by valid count (greedy) and cost-benefit score
  (define age and formula precisely, and handle that the cost-benefit key drifts with time — say how your heap copes),
  free-block pool, GC trigger watermarks. Checks: L2P↔P2L↔valid-bitmap↔count consistency every K ops; data integrity
  via a reference array. Measure WAF vs over-provisioning and policy, uniform vs hot/cold skew, with real tables. Say
  plainly when greedy ≈ cost-benefit and when not, per your measurements. Any analytic WAF formula must name its source.
- **DS-A10** 플래시 로그 KV. BM-A02/BM-A06 own the flash driver sim and update power-cut; STOR-B05/A03/A05 own Bitcask,
  LittleFS, and selection; PLAT-M08 owns schema migration. Own: NOR model (sector erase → 0xFF, program only 1→0,
  erase/program counters, fault injector), append-only records with CRC, sector state header, compaction with
  live-data copy, mount-time recovery. Power-cut: exhaustive injection at EVERY program/erase op of a scripted
  workload (not just sampled), including a torn write (partial bytes programmed) and a partial erase if you model
  it. Invariant: after every remount, each key = last committed value or the in-flight one, never garbage; compaction
  never loses data at any cut point. State the assumed write-atomicity and what real NOR parts/datasheets actually
  guarantee (hedge what isn't verified).
- **DS-A11** 레이아웃 측정. ARCH-M07 (loop order/tiling), HPC-B06 (vectorization), GPGPU-B07, MEM-B10 (pointer chase
  latency) are neighbours: stay on data-structure layout. Item is measurement-first, so starter/solution implement
  the AoS / SoA / AoSoA containers + accessors + partial-field and full-update kernels with deterministic CHECKs
  (all layouts compute identical results, same checksum, alignment/stride _Static_asserts, sizeof/padding reasoning);
  demo_bench.c does the measuring across working-set sizes crossing L1/L2/SLC (get real sizes from sysctl), plus list
  vs array traversal in allocation-order vs shuffled node order. Report whether loops auto-vectorized
  (`-Rpass=loop-vectorize` real output) so "SoA is faster" is attributed correctly (cache vs SIMD vs alignment). End
  with design rules drawn only from what you measured, and note what changes on a Cortex-M with no D-cache.

## When done
Reply (short) with: files written, md line count, PASS counts starter/solution/practice, sanitizer results, injected
bugs and whether they were caught, demo files + what they showed, and anything you were unsure about.
