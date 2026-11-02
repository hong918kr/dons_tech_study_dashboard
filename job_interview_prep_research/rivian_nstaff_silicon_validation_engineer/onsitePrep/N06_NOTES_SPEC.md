# N06 notes spec (writer agents read this first)

Owner: Don (Rivian Staff Embedded SWE, Silicon & Sensing Systems — validates Rivian's in-house automotive AI SoC; onsite prep). The role (confirmed in the 10-30 screen) is **silicon validation + board validation, mostly bare-metal C; RTL not needed**. Onsite = 2 technical + 2 behavioral; Technical 2 is expected to be domain/system: "design a test plan for X", "how do you know you're done", "FPGA passes / silicon fails", debug methodology. Likely interviewer Manjiang Zhang: FPGA/HAPS prototyping, bring-up, post-si validation, PVT, production SLT; ex-Baidu Apollo HW architect (sensor timestamp/sync, clock sync, secure boot, multi-camera patents), Marvell Ethernet PHY ASIC, Cisco packet ASICs.

Don: ex-SSD-controller firmware (SK hynix 7 yrs: chamber reliability test platform automation, NAND/PCIe 3/4/5 interface reliability, shmoo; Solidigm Staff: PCIe Gen6 SSD controller, pre-silicon FPGA bring-up of Cortex-R8/R82/M0+, PCIe/NVMe subsystem IP, I2C/SPI/DMA/SRAM/DRAM SoC verification before RTL freeze, perf tuning with silicon validation team), now Apple wireless chipset integration (PCIe/I2C/SPMI/RFFE root-cause with DSO & protocol analyzer, factory test nodes for latent defects). Knows firmware and C well; **not** a board designer, and asked to "assume I know nothing about PCIe/Ethernet/MIPI" — so every block/acronym gets a plain-Korean 🔰 explanation + analogy before the precise version.

Index: `2026-11-01_N06_validation_test_plan_design.md` — §2 lists every page and its items, §3 the common block template, §5 the overlap map. Implement EXACTLY those item IDs/titles (you may sharpen wording and add a few items, never drop or renumber). EXPAND the index, don't contradict it. Don't edit the index, other pages, or this spec.

## Files
- md in the job dir ROOT: `/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/2026-11-01_N06X_<slug>.md`, fixed slugs:
  - N06A methodology_test_plan_template · N06B coverage_metrics_exit_criteria · N06C plans_peripherals_soc_infra · N06D plans_dma_memory · N06E plans_highspeed_io_sensor
  - N06F plans_npu_soc_safety · N06G platforms_regression_automation · N06H debug_methodology_triage · N06I mock_10min_qbank
- Code (if any): `onsitePrep/n06/<x>/` (lowercase page letter) + `run.sh` that builds/runs everything there. Link from md with relative path.
- Render ONLY your own md: `python3 ~/.claude/skills/job-research/scripts/render_html.py <md path>` (NEVER with no args — others run in parallel). Check it prints `rendered`; then grep your html for leftover raw markdown (`**`, `](`, stray `|`) and check every relative link target exists (`ls`) — links to other N06 pages are fine (they'll exist with the slugs above).

## Header
- Line 1 `# N06X · <title>`, line 2 `<!-- theme: pastel -->`, line 3 a `>` line: `**묶음**: … · **우선**: P0/P1 · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: prev/next N06 page html · **관련**: links to the overlapping existing notes from index §5 · **코드**: onsitePrep/n06/<x>/ (or 없음)`.

## Sections — methodology pages (N06A, N06B, N06G, N06H)
0. `## 0. TL;DR` — 5–8 Korean bullets: what this page lets you answer, the 3 things to memorize, a 30-second English answer to the headline question.
1. `## 1. 🔰 처음 보는 사람을 위한 설명` — analogy first, ASCII diagram, 용어 사전 표 (용어 · 풀네임 · 쉬운 설명 · 왜 중요한가).
2. `## 2. 핵심 개념 (정확한 버전)` — the index §2 items, each a `###`, with tables/checklists/ASCII. Concrete, not generic: real numbers where verifiable, worked examples labeled `[예시]`.
3. `## 3. 실전 적용 — 예시` — one fully worked example applying the page to a concrete automotive-AI-SoC block (e.g. N06A: full plan for a CSI-2 receiver or I2C; N06B: coverage report for a DMA block; N06G: one test running on FPGA and silicon; N06H: one debug case end-to-end). Label Rivian specifics `[추정]`.
4. `## 4. 꼬리 질문 체인` — 3–5 chains: English Q → 꼬리1 → 꼬리2…; each step **English spoken answer (2–5 sentences)** + 한국어 핵심 한 줄, answers inside `<details>`.
5. `## 5. Don 경험 연결` — 2–4 English sentences tied to resume facts above; numbers as `[숫자 채우기]`, never invented.
6. `## 6. 실습 / 코드` — only verified things (real output pasted). If none, a recipe marked `[미실행]`.
7. `## 7. 자주 틀리는 것` — 5–10 traps.
8. `## 8. 출처` — numbered URLs actually consulted.

## Sections — block-practice pages (N06C, N06D, N06E, N06F)
- `## 0. TL;DR · 사용법` — what's on the page, which blocks are ★, how to practice (set 10-min timer, say it out loud, then open the details).
- `## 1. 공통 틀 복습` — the 7-step 10-minute frame (from N06A; short, link to N06A) + the 12-category checklist as one compact table. Keep ≤ 40 lines.
- Then one `## <ID> · <block> (★…)` per block, with these `###` in order:
  - `### 0. 🔰 이 블록이 뭔가` — analogy → precise definition → ASCII block diagram (where it sits in the SoC, its interfaces: bus, IRQ, DMA, pins/PHY) → 용어 표.
  - `### 1. 먼저 물어볼 것 (clarify)` — 4–6 English questions + assumed answers (e.g. "Is this our IP or licensed (DesignWare etc.)? Which modes are in scope? Is there a reference model? FPGA image available?").
  - `### 2. 테스트 매트릭스` — ONE table: 카테고리 · 테스트 항목 · 조건/파라미터 · 플랫폼 (FPGA / Si / 둘 다) · 통과 기준 · 우선 (P0/P1/P2). 12–25 rows, cover all applicable categories (register, functional, config combos, boundary, interrupt, error/negative, reset/power, concurrency/stress, performance, PVT/margin, soak, system/interop). Pass criteria must be measurable (e.g. "0 CRC errors in 10^12 bits", "latency p99 < X [추정]").
  - `### 3. 우선순위 · A0 첫 주` — P0 bring-up list (3–5), then what P1/P2 add; what you'd deliberately NOT test (covered by DV) and why.
  - `### 4. 장비 · 셋업 · 관찰 지점` — instruments / partner devices / loopbacks / pattern generators, registers & counters & logs to read, Linux tools where relevant (i2ctools, spidev_test, ethtool, v4l2-ctl, perf counters), ASCII setup diagram.
  - `### 5. 잘 터지는 버그` — 5–8 realistic silicon/integration failure modes for this block (symptom → likely cause → how the plan catches it).
  - `### 6. 10분 영어 답 뼈대` — the spoken answer following the 7-step frame, in English, ~12–20 short lines.
  - `### 7. 꼬리 질문` — 3–5 follow-ups: English Q, then `<details><summary>답</summary>` 2–5 sentence English spoken answer + 한국어 핵심 한 줄 `</details>`.
  - `### 8. Don 연결` — 1–3 English sentences, `[숫자 채우기]` for numbers.
- `## 자주 틀리는 것` (page-wide, 5–10) and `## 출처` at the end.
- Length target: 120–250 md lines per block (★ blocks longer). Substance over padding.

## Section — N06I (Q bank)
`## 0. TL;DR · 사용법` → `## 1. 채점표` (rubric table) → `## 2. 질문 목록 한눈에` (table: ID · Q · 묶음 · 링크) → `## 3.`… one `##` per bundle (블록 테스트 플랜 12 · 방법론 8 · 디버그 6 · Staff/리더십 4) with `###` per prompt: English Q, 꼬리 3단, `<details>` 60–90 s English answer skeleton + 한국어 핵심 + link to the N06A–H page/section (and LP/BV notes) → `## 모의 세트 3개` → `## 연습 기록 표` → `## 출처`. Link sections in other N06 pages by page html (not anchors — the renderer's anchor ids are not guaranteed).

## Markdown subset (renderer = job-research render_html.py)
- Headings `##`/`###`/`####` only. A `##` containing `TL;DR` becomes the highlight card; a `##` containing `출처` makes its numbered list the target for `[n]` footnotes.
- Tables one line per row; NO literal `|` inside cells (write "또는"; `\|` inside backticks is OK). Cells starting ✅/🟡/❌ get colored.
- Lists `-` / `1.`, 2-space indent nesting. Fenced ```c ```text ```bash ```python.
- Inline **bold**, `code`, links, tags `[확인됨]` `[추정]` `[예시]` `[미검증]`, footnotes `[n]`.
- Only raw HTML: `<details><summary>…</summary>` … `</details>` (blank line after the summary line and before `</details>`) and `<br>`.
- **Never put inline code inside link text** (renderer crashes). Keep link text plain.

## Linking (don't rewrite existing material — link it)
Relative links from the job dir: `2026-10-31_LP14_validation_methodology.html`, `2026-10-31_LP15_debug_scenarios.html`, `2026-10-31_LP13_linux_pcie_tools.html`, `2026-10-31_LP03_link_training_ltssm.html`, `2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html`, `2026-11-01_BV01_si_board_validation_big_picture.html`, `2026-11-01_BV09_ddr_bringup_training_memtest.html`, `2026-11-01_BV10_board_interface_tests.html`, `2026-11-01_BV12_si_pi_margining_shmoo_pvt.html`, `2026-11-01_BV13_board_debug_scenarios.html`, `2026-11-01_BV14_board_validation_plan_automation.html`, `2026-11-01_BV15_automotive_board_considerations.html`, `2026-10-25_rivian_hm_screen_prep.html` (deck §5.1 Q1–Q10, §6, §7), `2026-10-30_perf_metrics_ssd_vs_soc.html`, `hmScreenPrep/problems/<V1_march_memtest|V3_dma_desc_ring|V4_perf_bench_stats|V5_test_runner_coverage|V6_prbs_crc_checker|V7_shmoo_margin|V8_i2c_scan_recover|V11_npu_int8_golden|L3_rfc2544_throughput|L4_pps_time_discipline|L5_spsc_sensor_ring>/index.html`, N05 pages `2026-11-01_N05*_*.html`. `ls` the job dir to confirm names before linking. Skim the overlapping note(s) for your page first so you add rather than repeat.

## Research
WebSearch/WebFetch allowed. Good sources: vendor IP docs / TRMs (Synopsys DesignWare public briefs, Arm GIC architecture spec, AMD Zynq UltraScale+ TRM UG1085), NXP/TI/ADI app notes (I2C UM10204, SPI, UART), MIPI public overviews, IEEE 802.3 / OPEN Alliance public material (100BASE-T1/1000BASE-T1 TC tests), RFC 2544, IEEE 1588/802.1AS overviews, JEDEC public LPDDR material, ISO 26262 public summaries, MLPerf Inference docs, Linux kernel docs (i2c, spi, v4l2, ethtool, perf). Verify spec numbers; otherwise tag `[추정]`. No invented vendor register names/addresses. Summarize in your own words; no long quotes.

## Code (optional except where index §2 names one)
- C on macOS: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined x.c -o $TMPDIR/x && $TMPDIR/x` (default SDK link is broken; don't put flags in a quoted variable).
- Python: `python3 -I script.py`, stdlib only. Paste REAL output into the note. Anything not run → `[미검증]`.

## Language
Korean prose; technical terms in English; interview-spoken parts (clarify, 10-min skeleton, follow-up answers, Don sentences) in English.

## Final reply to orchestrator (short)
Files + line counts, blocks/sections written, code + run output summary (PASS lines), [추정]/[미검증] items, broken-link check result, open issues.
