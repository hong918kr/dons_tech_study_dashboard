# Lesson writer brief — track ALGO (알고리즘 · 코딩 면접), **Advanced** level (ALGO-A01–A09, A11, A12)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/curriculum/ALGO_B_LESSON_WRITER_BRIEF.md (ALGO base rules) → ROOT/curriculum/ALGO_M_LESSON_WRITER_BRIEF.md
(Medium overrides) → **this file** (Advanced overrides). Read all three plus ROOT/NOTES_SPEC.md. Everything in the
B/M briefs applies (output files, section headings verbatim and in order, svg rules, "면접에서 말하는 순서" table,
invariant/recurrence/correctness argument, trace table, brute-force/reference tests on fixed-seed xorshift32,
no malloc in the algorithm, demo_*.c for benchmarks, "never invent output") unless overridden here.
ALGO-A10 is already written — it is your **reference lesson for depth/style** (ROOT/lessons/ALGO/ALGO-A10/).

## Overrides for Advanced
- Level: senior/staff firmware coding-round + design follow-ups. The question is the item's `task` in
  ROOT/curriculum/tracks/ALGO.json; follow-ups probe *judgement*: why this variant, what the true worst case is,
  what breaks with integer widths / fixed RAM / another core, how you'd test it. Assume ALGO-B01–B15, ALGO-M01–M15,
  DS-B/M/A are known — cross-reference by id (e.g. "M06 다익스트라", "DS-B12 힙") instead of re-teaching. Read the
  1–2 neighbour lessons named in your lane before writing so you build on them.
- Don is a math major and ex-SSD firmware engineer: proofs must be right (admissibility/consistency, optimal
  substructure, invariants, error bounds), and firmware claims (RAM, flash, ISR context, Cortex-M codegen) must
  survive scrutiny. Anything from memory that you could not verify → hedge "(확인 권장)" or drop it.
- Advanced md is **500–750 lines**. Front matter `id`, `time:` realistic (90분–130분), `updated: 2026-11-11`.
  Files: ROOT/lessons/ALGO/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Title line `# <ID> <title exactly as in ALGO.json>`.
- Parts: A = core algorithm, B = the hard part / variant / edge cases / proof interviewers probe, C = the
  firmware-realistic scenario named in the task — each with checked tests. 면접 Q&A ≥ **7**. 추가 연습 3–6.
- The other 10 ALGO-A items are being written IN PARALLEL right now. Stay in your lane (below); every .c file is
  self-contained (inline a compact heap / bit I/O / PRNG if you need one and say "(DS-B12에서 자세히)" etc.).
- Test budget: solution.c / practice.c < 2 s at -O2 (A12: < 5 s), < 90 s under ASan/UBSan (if a big count blows
  the sanitizer budget, scale it with a documented `#if defined(__has_feature)` / `__has_feature(address_sanitizer)`
  guard and say so in the md). Checked tests are deterministic; timing never goes into a CHECK.
- Benchmarks/long runs in demo_*.c: -O2, warm-up, ≥3 runs, report median + spread. Host is **Apple M6**
  (`sysctl -n machdep.cpu.brand_string`); other writers are running at the same time, so load is high → report
  ratios and note the load (`uptime`) next to the numbers.
- Where codegen matters (bit tricks, divides, tables in flash), real asm only:
  `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_x.c -o -` on a freestanding file
  (only <stdint.h>/<stddef.h>/<stdbool.h>). Never claim an instruction you didn't observe.
- Accuracy: don't cite a paper, RFC, library internal, LeetCode number or constant unless you've checked it
  (`WebFetch`/`WebSearch` are available via ToolSearch). Measured numbers need your program's real output.
- **Bug-detection honesty**: before finishing, inject ≥ 2 plausible bugs into a PRIVATE copy of solution.c (e.g.
  heuristic overestimates, relax loop runs n−2 times, KMP fallback off-by-one, LZ overlap copy via memcpy, Huffman
  bit order flipped, haszero off by a byte, Welford update order swapped, token refill rounding, merge heap
  comparator) and confirm the harness FAILs. Report which ones. Don't leave mutants in the lesson dir.

## Workspace
- Private scratch dir: `<SCRATCH>/<ID>/` (SCRATCH is given in your prompt) for binaries, mutants, temp data files.
  Never write to /tmp or to a shared name. Don't edit anything outside ROOT/lessons/ALGO/<your ID>/.
  Don't run build_site.py or git.

## Lanes (what each item owns — don't drift into a neighbour)
- **A01 A\***: read M04 (BFS grid) + M06 (Dijkstra). 8-connected grid with integer costs 10/14 (state that 14 ≈ 10√2
  and what that does to optimality claims — optimal *for the 10/14 metric*). Octile heuristic (admissible +
  consistent for 10/14 — prove), Manhattan: admissible on 4-connected but **NOT admissible on 8-connected** — show a
  concrete grid where Manhattan-A* returns a costlier path than Dijkstra (measured), and say when people use it
  anyway (weighted/greedy). Diagonal corner-cutting rule (no squeezing between two blocked cells) — state it,
  apply it identically in A*, Dijkstra and brute reference. Compare A* vs Dijkstra: equal cost on random maps (many
  seeds), expanded-node counts (table), tie-breaking effect (h-tie-break larger g) measured. Closed set +
  consistency ⇒ no reopen (prove). Obstacle inflation by robot radius r cells (Euclidean disk; check vs brute
  stamping; mention distance-transform as 추가 연습) then path on the inflated map, ASCII render. AUTON-B06 owns
  layered costmaps with cost decay, AUTON-M06 owns Hybrid A* — one-line mentions.
- **A02 벨만-포드/플로이드-워셜**: read M06. Bellman-Ford with early exit, n−1 rounds proof (path ≤ n−1 edges),
  negative-cycle detection on round n AND extraction of the actual cycle (walk pred n times to land inside the
  cycle — prove why). Currency arbitrage as −log(rate): state the floating-point issue (rounding can create/hide
  tiny cycles) and handle it explicitly (epsilon, or integer fixed-point log with documented error bound); test with
  hand-made rate tables with/without arbitrage. Floyd–Warshall latency matrix + `next[i][j]` path reconstruction,
  k-loop-outermost correctness argument, INF overflow guard, negative cycle via dist[i][i] < 0. Cross-check:
  FW vs Dijkstra-from-every-source on non-negative graphs and vs Bellman-Ford-from-every-source with negative edges
  (no negative cycles), random graphs many seeds. Complexity table incl. memory for n=256 on an MCU. SPFA: one
  paragraph + its exponential worst case only if you can cite/show it; Johnson's = 추가 연습 option.
- **A03 비트마스크 DP TSP**: Held-Karp dp[mask][last], state/transition/base/order explicitly, O(2^n·n²) time,
  O(2^n·n) memory — compute real bytes for n=16 (and why that's not MCU-friendly; what width dp entries need).
  Probe on a board: define clearly whether the tour returns home (closed) or ends anywhere (open path) — implement
  both and say which the task means. Distance metric: Euclidean (rounded int) vs Manhattan vs **Chebyshev** (XY
  gantry with independent axes moving simultaneously) — pick, justify, test. Checks: permutation brute force for
  n ≤ 9 (cost equal, tour valid), tour backtracking from parent table, n=16 run time. Compare against
  nearest-neighbour + 2-opt heuristics: measured optimality gap over many random boards. A07 owns submask
  enumeration as a bit trick — one-line cross-ref only.
- **A04 KMP/Rabin-Karp 스트리밍**: read B10, M12 (byte-at-a-time FSM). KMP failure function (prefix function) with
  the amortized O(n) proof (potential argument), streaming push(byte) API keeping only `matched` state (+ pattern
  + table), overlapping matches (report all end positions), matches spanning chunk boundaries (feed random chunk
  splits — same result as one-shot), KMP DFA (256×m table) vs failure-function trade-off (RAM vs worst-case per-byte
  cost: show the per-byte fallback loop count worst case, measured). Rabin–Karp: rolling hash over an m-byte ring
  (streaming needs m bytes of history — say so), modulus choice (2^32 wrap vs prime), collision → always verify;
  build an adversarial collision for your hash if feasible and show verify catches it. Checks vs naive on
  binary/small alphabets (many matches), random patterns, preamble scenario (e.g. 0x55 0x55 0x7E sync word in a
  noisy UART stream). QPROB-M02 Penney's game uses the KMP automaton — mention. Aho–Corasick = 추가 연습 option.
- **A05 LZ77/LZSS**: read COMM-B11/B12 only to cross-ref framing. LZSS token format (flag bit/byte, offset/length
  bit widths derived from window W and max length), parameters W ∈ {256…4096}, min match (why 3 for typical
  formats — show break-even math for your token format), encoder: naive window search AND hash-chain (or hash-head)
  search, same output or same decodability + compare ratio/speed; decoder: overlap copy (distance < length) byte by
  byte (memcpy/memmove are WRONG here — show why), decoder RAM = window (or the output buffer itself) — state it.
  Malformed-input safety: decoder never reads/writes out of bounds on corrupted/truncated streams (deterministic
  fuzz under ASan). Round-trip tests: empty, 1 byte, all-same, incompressible random, period-k data, sizes around
  W boundaries. demo: real data — e.g. this Mac's own binaries (an ARM64 Mach-O like /bin/ls or a compiled object)
  as "firmware image" and a synthetic log text; ratio vs W/min-match table; optional zlib comparison in demo only
  (`-lz`). heatshrink etc. only hedged. A06 owns Huffman/varint.
- **A06 델타·RLE·허프만**: deterministic synthetic logs (temperature: slow drift + noise, int16 0.01°C; 3-axis accel:
  int16 with noise + occasional shocks) — describe the generator. Pipeline stages each measured (bytes after each
  stage, table): delta → zigzag (prove bijection, edge INT16_MIN/overflow handling: deltas of int16 need 17 bits —
  say how you handle) → LEB128-style varint → RLE (define the format, esp. how runs vs literals are escaped) →
  static canonical Huffman (code lengths from frequencies via heap/two-queue, length limit e.g. ≤ 15 bits with a
  correct limiting method, canonical codes, header = code lengths, MSB-first bit writer/reader). Shannon entropy
  of each stage's symbols vs achieved bits/symbol. Round-trip on all stages and the composed pipeline, random +
  edge inputs; decoder robustness on corrupted input (no OOB). Kraft inequality check on lengths. DIST-B06
  (varint/zigzag basics), MEDIA-B05 (JPEG Huffman), A05 (LZ) — one-line mentions; DRONE-M10 predictive encoding.
- **A07 고급 비트 트릭**: read B12/B13. Gray: g = b ^ (b>>1), inverse via prefix-xor shifts (log-steps for 32-bit),
  exhaustive 16-bit check + "adjacent codes differ in exactly one bit" + why encoders use it (absolute rotary
  encoder: misread during transition ≤ 1 LSB vs binary; simulate a transition misread). ARCH-A07 owns Gray-pointer
  async FIFO — mention. SWAR: haszero(v) = (v − 0x01..) & ~v & 0x80.. — prove "nonzero iff some byte is 0", and
  show/measure that the *per-byte* flags can be wrong above a zero byte (e.g. 0x01 after 0x00) and how to get the
  exact first-zero index (ctz on little-endian); hasvalue/hasless; word-at-a-time strlen with the alignment /
  page-boundary argument (why aligned over-read is safe in practice but UB in ISO C — say it honestly). Subset
  enumeration `sub = (sub − 1) & mask` incl. empty, count = 2^popcount, Σ over all masks = 3^n (prove, check).
  Gosper's hack next-same-popcount, termination/overflow at the top, check vs brute over n ≤ 20 against C(n,k).
  Cortex-M4 asm for one or two of these (real output). A03 uses submasks — mention.
- **A08 Welford·EMA·저수지**: naive Σx²−(Σx)²/n catastrophic cancellation shown with float and double on data with
  large mean/small variance (real numbers), Welford update (derive), Chan et al. parallel merge of two
  accumulators (cite only what you verify), compared with a two-pass exact reference (long double or exact integer
  math on integer data). Integer EMA y += (x − y) >> k: prove the truncation dead-band (steady-state offset up to
  2^k−1 / one-sided for negative with arithmetic shift — careful, implementation-defined >> on negatives in C11),
  fix with a fixed-point accumulator, step-response tests. Reservoir sampling Algorithm R for k items with an
  unbiased bounded random index (rejection, no modulo bias — prove), inclusion probability k/n proof by induction,
  chi-square uniformity test with a fixed seed (state df and the critical value from a source you checked, or
  compute the p-value yourself). Algorithm L = 추가 연습 option. NUM-B02 owns cancellation basics — mention.
- **A09 스위프 라인**: read M09 (it owns min-channels = max overlap count; don't redo it). Own: (1) all overlapping
  pairs in a firmware memory map (regions [base, base+size), named), output-sensitive O(n log n + k) sweep with an
  active set, report pairs sorted deterministically, plus containment vs partial overlap classification; check vs
  O(n²) brute force, + edge cases (touching = not overlapping, zero-size regions, region at 0xFFFFFFFF end —
  overflow of base+size in uint32: handle with uint64 or end-inclusive); (2) interrupt-interval log: max simultaneous
  active ISRs AND the first time instant (and the interval) where it occurs; tie order at equal timestamps
  (end before start for half-open) proven; nested-ISR realism note; (3) rectangle union area via sweep over x with
  merged y-coverage per slab (O(n² log n)) then a segment-tree O(n log n) version (or as 추가 연습), checked vs
  grid brute force on small integer coords. Linker-script/MPU-region use cases in Part C.
- **A11 Rate limiter**: token bucket with integer ticks: exact rational refill (tokens scaled by a fixed-point
  factor or numerator/denominator accumulation — no drift over 10^9 ticks, prove), uint32 tick wraparound via
  unsigned subtraction (test across the wrap), capacity b and rate r. Prove and check exhaustively on the test
  traffic: for every window [t, t+T], accepted ≤ b + r·T (sliding check over all windows). Fixed window counter's 2×
  boundary burst (show), sliding window log (ring of the last N timestamps — exact, memory N), sliding window
  counter (weighted previous bucket — approximation; quantify max over-admission on adversarial traffic, measured +
  bound). Test traffic generators: steady, bursty, adversarial at window edges, idle then burst. ISR/thread context
  (single-writer, what needs a critical section) in one paragraph. Memory/accuracy trade-off table. SYSD-B07
  (system design), NETHW-M10 (egress shaper), NET-M12 (tbf) — one-line mentions; leaky bucket vs token bucket
  distinction stated correctly.
- **A12 외부 정렬 (4KB RAM)**: RAM budget enforced in code: one static `uint8_t arena[4096]`, every buffer/heap
  carved from it with an allocator that aborts if exceeded; NO other heap/stack arrays for data (state stack usage
  honestly). Flash modeled by a file in the private scratch dir (ROOT lesson dir must not get data files) accessed
  with explicit block reads/writes counted (use unbuffered I/O, e.g. open/pread/pwrite or setvbuf _IONBF — stdio's
  own buffer would cheat the budget; say it). Records: fixed size (e.g. 16 B: uint32 key + payload) × 1M. Run
  generation: load-sort-write (runs = N / records-per-RAM) AND replacement selection (runs ≈ 2× RAM on random input —
  measure; sorted input → 1 run; reverse sorted → worst). k-way merge with a heap of run heads; k bounded by
  RAM / block size → passes = ⌈log_k(runs)⌉ — measured vs theory, bytes read/written = 2·N·(1 + passes) style
  formula (derive yours). Correctness: output sorted AND a permutation of input (count + order-independent multiset
  hash, keys with duplicates) + stability claim if any. Scaled sizes in solution.c tests must stay < 5 s; the full
  1M-record run can be in solution.c if it fits, else demo_*.c with real output. Flash realism note: NAND page/erase
  granularity, write endurance cost of extra passes (DS-A09/MEM-M ref).

## AlgoMonster cross-ref
ROOT/2026-11-07_mapping_coding_pattern.md maps Don's AlgoMonster notes to ALGO ids (A02, A03, A04, A09 appear).
If your item appears, cite the matching section in 참고자료 as "AlgoMonster 노트 Mxx §n (Don 정리본)". Don't copy text.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    B=<SCRATCH>/<ID>; mkdir -p $B; cd ROOT/lessons/ALGO/<ID>
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o $B/st -lm && $B/st   # builds, some FAIL, exit≠0, no hang/crash
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o $B/so -lm && $B/so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o $B/pr -lm && $B/pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o $B/san -lm && $B/san
    (same sanitizer run for practice.c)                                          # clean
    every demo_*.c: build with the command printed in the md, run it, paste the real output.
- solution.c = starter.c with only TODO bodies filled (the site highlights the diff). Stubs are warning-free and
  return wrong-but-safe values (no hang, no crash).
- If a program needs a runtime argument (e.g. scratch path for A12), default to a path under the current dir is NOT
  allowed — take it from argv/env with a documented default like `$TMPDIR`, and print the exact command in the md.
- Self-check the md: headings verbatim & in order (skip ``` fences when scanning), ≥2 ```svg with viewBox
  (width ≤ 760), Q&A ≥ 7 as `<details><summary>Q1. ...`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, `### 한 번에 검증` + `<!-- code: practice.c -->` at the end of §9.

## When done
Reply (short) with: files written, md line count, PASS counts starter/solution/practice, sanitizer result,
injected bugs + caught?, demo files + key real numbers, and anything from memory you were unsure about
(hedged in md as "확인 권장").
