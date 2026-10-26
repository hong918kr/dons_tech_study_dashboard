# Rivian HM screen practice set — authoring spec

Audience: Don (Korean, ex-Staff SSD firmware engineer at Solidigm — pre-silicon FPGA SoC bring-up, PCIe/NVMe, Cortex-R;
now Apple RF chipset integration) preparing for a 45-min technical screen on Wed 2026-10-28 with
**Manjiang Zhang, Sr Manager Silicon Validation at Rivian** (Rivian since 2022-12: autonomy silicon — architecture review,
FPGA/HAPS prototyping, silicon bring-up, post-silicon validation, PVT characterization, production SLT; ex-Baidu Apollo
HW architect: FPGA sensor-data processing, camera, time sync; ex-Brocade FPGA packet/crypto; ex-Marvell Ethernet PHY ASIC;
ex-Cisco packet-processor ASICs). Role: Staff Embedded SWE, Silicon & Sensing Systems — bare-metal C to validate Rivian's
in-house AI accelerator SoC (RAP1 family): enable blocks/peripherals, automatic test tools + coverage reports, benchmark
tools (throughput, latency, utilization, IO/compute frequency). Preferred: Zynq Petalinux, AI accel, JTAG/I2C/UART/SPI/
Ethernet/DDR/PCIe, MIPI/ISP/video encoder. The interviewer is a HARDWARE person: problems should look like real
validation code a silicon-validation SW engineer writes, with a mock HW model in C (register file / fake memory /
fake bus / fault injection), not abstract LeetCode.

**Exemplar to copy exactly in structure and quality:**
/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/scout_ai_embedded/technicalScreenPrep/problems/T1-1_overheat_intervals/
(README.md, stub.c, solution.c, chart1.svg, chart2.svg) and, for a mock-HAL style problem,
.../technicalScreenPrep/problems/E1_i2c_sensor_driver/. Read them before writing anything. Do NOT modify anything in scout_ai_embedded/.

## Language rule (strict)
- Everything the candidate sees as the *problem* is ENGLISH: title, §1 Problem text, signatures, examples,
  constraints, clarifying questions, all comments and test descriptions inside stub.c / solution.c,
  Follow-up questions (left column).
- All *explanation* is KOREAN (with English technical terms): hints, 풀이, trace commentary, 임베디드 관점,
  Follow-up answers, 자주 하는 실수, chart captions. Korean style: plain, concise, 합니다체, no fluff.

## Per-problem folder: problems/<DIR>/
- `stub.c`   — compiles cleanly with `cc -std=c11 -Wall -Wextra` (cast unused params to void), function bodies
               are TODO returning a dummy value, full test `main()` using the CHECK macro (normal case first,
               then 3–6 edge cases). Tests must FAIL on the stub (not crash). Top comment = English problem summary.
- `solution.c` — identical to stub except the implementations (and header says "Reference solution").
               Must print ALL PASS under `-fsanitize=address,undefined` with `-Werror`.
               Idiomatic embedded C: fixed-width ints, no malloc (unless the problem is about allocators),
               caller-provided buffers, wrap-safe time compare, integer math. Short focused comments.
- `chart*.svg` — 1–3 hand-written SVG diagrams that genuinely aid understanding (time-series plot with
               threshold, state machine, ring buffer index diagram, bit layout, deque evolution, etc.).
               White background, pastel palette only:
               blue line #5677c9 · hot/red #c4554e bg #fce5e2 · good/green #2f8a5f bg #e2f4e9 ·
               warn/amber #a9700f bg #fcf0d6 · violet #8a5cc0 bg #f0e7fb · axis #b9b0c6 · muted text #8a8297 ·
               ink #2e2939. font-family="-apple-system,'Apple SD Gothic Neo',sans-serif", font-size 11–14.
               Compute coordinates carefully; no overlapping labels; text inside viewBox.
               Chart text may be English or Korean (short).
- `README.md` — same sections as the exemplar:
  `# <ID> · <English title>` then the `> **ID**: … · **Priority**: P0|P1|P2 · **Pattern**: … · **Time**: N min`
  quote line (keep these exact keys — build.py parses them) + the 파일/실행 line;
  ## 1. Problem (English; code block with signature; ### Example; ### Constraints; ### Clarifying questions)
  ## 2. Your workspace  → `<div class="code" data-src="stub.c"></div>`
  ## 3. 힌트 → 2–3 `<details><summary>Hint N — …</summary>` blocks (blank line after <summary> line and before </details>)
  ## 4. 풀이 → 4.1 핵심 아이디어 with chart `<div class="chart" data-src="chart1.svg" data-caption="…"></div>`,
     state/diagram, ### trace table (손으로 따라가기), ### 정답 코드 inside
     `<details><summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>` … `<div class="code" data-src="solution.c"></div>` … `</details>`
     (the 💡 prefix makes it a spoiler box), 코드 읽는 포인트, ### 복잡도
  ## 5. 임베디드 관점으로 말할 것
  ## 6. Follow-up 질문과 답 (table: English question | Korean answer)
  ## 7. 자주 하는 실수
  Markdown subset only: headings, lists, tables, fenced code, **bold**, `code`, [links](x), <details>.
  No raw HTML other than the two <div> placeholders and <details>/<summary>. Cross-link related problems
  with relative links like `[T1-2](../T1-2_streaming_hysteresis/index.html)`.

## Directory names (fixed — use exactly)
V1_march_memtest · V2_reg_field_rmw · V3_dma_desc_ring · V4_perf_bench_stats · V5_test_runner_coverage ·
V6_prbs_crc_checker · V7_shmoo_margin · V8_i2c_scan_recover · V9_csi2_packet_parse · V10_eth_frame_fcs ·
V11_npu_int8_golden · V12_frame_timestamp_check · D_debug_drills
Cross-link with relative links like `[V2](../V2_reg_field_rmw/index.html)`.

## Extra README section (Rivian-specific, add after §5)
`## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트` — 3–5 bullets: what a silicon-validation manager would push on
(e.g. "how would this run on first silicon vs FPGA", "how do you know your test itself is correct", coverage, runtime,
how results feed PVT/SLT), each with a 1–2 line Korean answer + one English sentence Don can say.

## Verify before finishing (from hmScreenPrep/ — run.sh/check_all.sh live there)
1. `./run.sh <ID>` → compiles, tests fail, no crash.   2. `./run.sh <ID> sol` → ALL PASS.
3. `cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined problems/<DIR>/solution.c && ./a.out`
4. `python3 build.py problems/<DIR>` renders without "⚠ missing".
5. Every numeric value quoted in README (examples, trace tables, chart labels) must match what solution.c computes.
Do not touch files outside your assigned folders.
