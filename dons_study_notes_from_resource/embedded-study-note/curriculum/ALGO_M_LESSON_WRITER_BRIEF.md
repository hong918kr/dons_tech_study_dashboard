# Lesson writer brief — track ALGO (알고리즘 · 코딩 면접), **Medium** level (ALGO-M01–M15)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

This is the ALGO Basic brief (ROOT/curriculum/ALGO_B_LESSON_WRITER_BRIEF.md) adapted for Medium. **Read that file
first** — everything in it applies (output files, section headings, svg rules, interview flavour, code rules,
verify commands, "never invent output", private /tmp/<ID>_work dir) EXCEPT where this file overrides it.

## Overrides for Medium
- Level: Medium = a real coding-round question at mid/senior firmware level (the kind asked in a 45-min round at
  Apple/NVIDIA/Anduril/Tesla embedded teams). ALGO-B01–B15 are written (ROOT/lessons/ALGO/ALGO-Bxx/) — assume Don
  knows them and cross-reference by id instead of re-teaching (B02 binary-search invariants, B06/B11 recursion &
  stack depth, B09 two pointers, B10 sliding window, B12/B13 bit tricks, B14 checksums, B15 tokenizing).
  DS-B/M/A are written too (DS-B12 binary heap, DS-M11 indexed PQ, DS-M05 LRU, ...).
- The other 14 ALGO-M items are being written IN PARALLEL right now. Read their title/task in ALGO.json and stay in
  your lane (see "Lanes" below). Every .c file must be self-contained — if you need a heap / queue / indexed PQ,
  write a compact static-storage one inline and say "(DS-B12 / DS-M11에서 자세히)".
- Reference lessons: ROOT/lessons/ALGO/ALGO-B02/ and ROOT/lessons/ALGO/ALGO-B14/ (this track's Basic tone/harness)
  and ROOT/lessons/ALGO/ALGO-A10/ (deeper). Medium md is **450–650 lines**.
- Front matter: `updated: 2026-11-11`, `time:` realistic for Medium (60분–90분).
  Files: ROOT/lessons/ALGO/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Title line `# <ID> <title exactly as in ALGO.json>`.
- 면접 Q&A ≥ **6** (each `<details><summary>Q1. ...</summary>`). 추가 연습 3–6.
- Parts escalate: A = core algorithm, B = the variant / edge cases / proof interviewers probe,
  C = firmware-realistic use (the scenario named in the task: driver init order, mesh network, DMA channels,
  UART NMEA stream, Flash module budget, integral image on a sensor frame, ...).
- 풀이 must include: the "면접에서 말하는 순서" table (Basic brief), the invariant / recurrence / correctness
  argument stated explicitly (for greedy: the exchange argument; for DP: state + transition + base + order;
  for graphs: what the queue/heap holds and why it's correct), a step-by-step trace table on a small example,
  and complexity (time, extra memory, stack depth, worst case). Don is a math major — proofs must be right.
- Tests: every function checked against a brute-force / reference implementation on fixed-seed xorshift32 random
  inputs (enough cases to mean something, whole program < 2 s) + exhaustive small sizes where feasible + named
  edge cases + published values (CRC catalogue check values, OEIS counts, LeetCode-style examples).
  Benchmarks / long runs (full 2^32 sweeps, MB/s, ns/sample, node counts on big n) go in demo_*.c, run them,
  paste REAL output (Apple Silicon M-series, `cc --version`, -O2, warm-up + median; ratios matter).
- No malloc in the algorithm itself (static arrays sized by a MAX_* constant or caller-provided buffers), like
  firmware. Recursion is allowed where natural, but state the max depth and give the iterative alternative where
  stack matters (B11 참고).
- Cross-target observations where they matter (÷ on M0 without divider, CRC instructions, table size in flash):
  `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_x.c -o -` (or -mcpu=cortex-m0 /
  thumbv6m-none-eabi) works for files that include only <stdint.h>/<stddef.h>/<stdbool.h>. Show real asm only.

## Lanes (what each item owns — don't drift into a neighbour)
- M01 CRC 원리: generic bitwise engine (width ≤ 32, poly/init/refin/refout/xorout), CRC-8/SMBUS, CRC-16/CCITT-FALSE
  (RevEng now lists it as CRC-16/IBM-3740 — verify name and say so), CRC-16/MODBUS; "123456789" check values from
  the RevEng catalogue; polynomial-division view (hand-trace one byte), why reflection exists (UART LSB-first);
  error-detection property measured (e.g. all single/double-bit errors and all bursts ≤ width on a short message
  detected — exhaustive count). NO lookup tables (M02 owns tables). B14 owns simple checksums.
- M02 CRC 가속: CRC-32/ISO-HDLC (zlib) bitwise → 256-entry table (+ table generator) → slicing-by-4 and -by-8;
  compare vs zlib `crc32()` on the TEST side (macOS has libz: link `-lz`; if it fails, say so and use the
  catalogue check value 0xCBF43926 + bitwise reference); MB/s + table bytes in demo_bench.c; optionally the ARMv8
  CRC32 instructions (`__crc32b/__crc32d` via <arm_acle.h>, `-march=armv8-a+crc`) on this Mac as a demo — only if
  it actually builds and matches; mention STM32-style CRC peripheral only hedged. Also crc combine/streaming
  (update in chunks == one shot).
- M03 백트래킹: subset sum (all subsets that hit target, + count), permutations of a multiset (duplicates → unique
  perms, compare count with n!/∏k!), N-Queens with bitmask (counts n=1..12 vs OEIS A000170); visited-node counts
  with vs without pruning (table); recursion depth = n. Choose–explore–unchoose template.
- M04 BFS/DFS 격자: occupancy grid (static arrays, e.g. MAX 64×64), BFS shortest path + path reconstruction
  (parent array), obstacle blob count (DFS iterative with explicit stack AND BFS — 4- vs 8-connectivity
  difference), reachable area from start; 10 hand-made maps with expected answers written in the test table +
  random grids vs a simple reference (e.g. Bellman-style relaxation until no change). A* is ALGO-A01 — mention only.
- M05 위상 정렬: Kahn with a deterministic tie-break (smallest id first → say how: min-heap or ordered scan, and the
  complexity cost), DFS 3-color cycle detection that reports the actual cycle path; driver names (clock, pmic,
  i2c1, imu, ...) mapped to ids; verify output is a valid topological order (check every edge) on random DAGs +
  injected cycles. Linux deferred probe / devicetree dependencies as context (hedged). SCC = ALGO-A10.
- M06 다익스트라: adjacency list in static arrays, inline compact indexed min-PQ with decrease-key (DS-M11 ref) AND
  the lazy-deletion binary-heap variant (compare pushes/pops), path reconstruction, Floyd–Warshall reference on
  random graphs (dist matrix equal, path cost equals dist), unreachable nodes, a negative-edge counterexample where
  Dijkstra is wrong (Bellman-Ford = ALGO-A02, mention only). Latency-in-ms comms network framing.
- M07 DP 1D: stairs variant (steps {1,2,3} or blocked stairs), coin change min-count AND number-of-ways (order
  matters vs not — the loop-order trap), LIS O(n²) and O(n log n) (patience/tails + reconstruct one LIS);
  top-down memo AND bottom-up for each; space-optimized versions; brute force by enumeration for small n.
  Overflow of the "ways" count (use uint64_t, state the limit).
- M08 DP 2D: edit distance with the edit script (firmware config diff lines/strings), LCS with traceback, 0/1
  knapsack (Flash budget W bytes, modules with size/value) with chosen-item traceback; O(W) 1-D version (reverse
  loop — why) giving the same optimum; exhaustive brute force for small n. Note grid-path DP as one 추가 연습
  (unique paths / min path sum) since the track has no grid-DP item.
- M09 그리디 구간: max non-overlapping intervals (earliest end — exchange argument), minimum channels for DMA
  transfers = max overlap (sort + min-heap of end times, or sweep; prove lower bound = depth), merge intervals;
  counterexamples for wrong greedies (earliest start, shortest first); exhaustive brute force for n ≤ 10–12.
  Half-open [s, e) convention stated. Sweep-line memory-map overlaps = ALGO-A09 (mention only).
- M10 스케줄링: task scheduler with cooldown n (LeetCode 621 Task Scheduler) — formula answer AND a heap simulation
  that emits the actual sequence (with idle slots) and a validator for the sequence; job sequencing with
  deadlines & profit (greedy by profit + latest free slot via DSU or by deadline-sorted min-heap — prove), print
  the schedule; brute force over permutations/subsets for small n. RTOS link (see:RTOS) in one paragraph: this is
  offline scheduling, not RMS/EDF schedulability.
- M11 힙 활용: running median with max-heap + min-heap (balance invariant), sliding-window median with deletion
  (state your method: indexed heaps or lazy deletion; prove size bookkeeping), top-K outliers (|x-median| or
  largest values) with a size-K min-heap; compare with sort-based reference on random streams; ns/sample in
  demo_bench.c vs naive sort-per-sample, window sizes e.g. 5/31/255.
- M12 NMEA FSM: byte-at-a-time parser (states: IDLE, BODY, CK1, CK2, CR/LF), XOR checksum of bytes between '$' and
  '*', field split (empty fields!), max sentence length (NMEA 0183 = 82 chars incl. $ and CRLF — verify/hedge),
  resync on garbage / truncated / overlong sentences, parse GGA (time, lat/lon to fixed-point, fix quality, sats).
  Test with real sample sentences (compute their checksums yourself and confirm the published *XX), a stream with
  garbage injected, and a deterministic fuzz (xorshift) proving: never crashes (ASan), never accepts a sentence
  whose checksum doesn't match, recovers on the next valid '$'. B15 = tokenizing basics, cite.
- M13 누적합/차분: 1D prefix sum range query, difference array for many range-adds then rebuild, 2D prefix sum
  (integral image, inclusion–exclusion) on a e.g. 640×480 8-bit/12-bit frame (or smaller to keep < 2 s);
  brute-force comparison; overflow analysis with real bounds (max sum = n·max → needed bit width; uint32_t for
  640×480×255? compute it) and the "wraparound still works for differences with unsigned" observation (prove it).
- M14 Union-Find: path compression (or halving) + union by rank/size; incremental mesh-network link adds with
  component count; cycle detection in an undirected edge list; Kruskal MST (sort edges + DSU) checked against a
  Prim O(V²) reference on random weighted graphs (equal total weight; disconnected → forest); BFS reference for
  connectivity queries; find-depth stats with vs without the optimizations (demo). α(n) claim stated correctly.
- M15 정수 산술: bitwise isqrt (floor, uint32 → uint16, digit-by-digit), modular fast exponentiation (uint32 mod
  with uint64 intermediate; and a mulmod without 128-bit for 64-bit mod if you add it), binary GCD (Stein) vs
  Euclid, ÷10 by multiply+shift magic (0xCCCCCCCD, >>35 for uint32 — derive it, and the general magic-number
  computation for a constant d) + %10 from it; solution/practice use boundary + random tests; FULL 2^32 sweeps go
  in demo_exhaustive.c (run it, paste time + result). Show the compiler's own ÷10 code for cortex-m4 (umull) and
  cortex-m0 (no divider → __aeabi_uidiv call or multiply sequence — show what you actually get).

## AlgoMonster cross-ref
ROOT/2026-11-07_mapping_coding_pattern.md maps Don's AlgoMonster notes
(/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/algomaster-coding-pattern/original-md/)
to ALGO ids. If your item appears there, cite the matching section in 참고자료 as "AlgoMonster 노트 Mxx §n (Don 정리본)"
and, in 9. 추가 연습, you may add one problem from a gap the mapping notes for your item. Don't copy text.

## Verify (you MUST run, own temp names)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cd ROOT/lessons/ALGO/<ID>
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0, < 2 s
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0, < 2 s
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/<ID>_san && /tmp/<ID>_san
    (same sanitizer run for practice.c)                                                   # clean
    every demo_*.c: build with the command you print in the md, run it, paste the real output.
(If a test file needs an extra lib, e.g. M02 `-lz`, put the exact build line in the md and in the file header.)
Self-check the md: headings verbatim & in order (skip ``` fences when scanning), ≥2 ```svg with viewBox
(width ≤ 760), Q&A ≥6, `<!-- code: starter.c -->` in §3, `<!-- code: solution.c diff=starter.c -->` in §6,
`### 한 번에 검증` + `<!-- code: practice.c -->` at the end of §9.

## When done
Reply (short) with: files written, md line count, PASS counts starter/solution/practice, sanitizer result,
demo files + key real numbers, and anything from memory you were unsure about (hedged in md as "확인 권장").
Do NOT edit files outside ROOT/lessons/ALGO/<your ID>/ (and your private /tmp/<ID>_* paths).
