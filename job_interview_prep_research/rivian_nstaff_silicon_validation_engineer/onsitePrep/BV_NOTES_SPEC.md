# BV notes spec (writer agents read this first)

Owner: Don (Rivian Staff Embedded SWE, Silicon & Sensing validation onsite prep). Track index: `2026-11-01_BV00_board_validation_track.md` (§0.5 big picture, §2 has the outline + tail-question chains for every note — EXPAND it, don't contradict it). Track topic = **board validation / bring-up** (power, clock, reset/strap, first power-on, JTAG, boot FW, DDR, interface tests, lab instruments, SI/PI/shmoo/PVT, debug scenarios, validation plan/automation, automotive). The role (confirmed in the 10-30 screen) is silicon validation + board validation, mostly bare-metal C; RTL not needed. Hiring-side interviewer background: FPGA/HAPS prototyping, bring-up, post-si validation, PVT, production SLT, Ethernet PHY ASIC, sensor timestamp/sync.

## Reader assumption (most important — Don's explicit request 2026-10-31)
"Assume I know NOTHING about PCIe, Ethernet, MIPI" — apply the same to board-level EE topics (PMIC, PLL, strap pins, DDR training, eye diagrams, scope probing). Don is a firmware engineer (SSD controller FW, RF integration), not a board/hardware designer. Every note must be readable by a smart SW engineer who has never designed a board.
- Every acronym spelled out + explained in plain Korean on first use (e.g. "PGOOD (Power Good: 레귤레이터가 '내 출력 전압이 목표 범위에 들어왔다'고 알려주는 신호선)").
- Use analogies (택배/도로/아파트 주소 etc.) BEFORE the precise definition, then give the precise definition.
- Draw ASCII diagrams in ```text code blocks for power trees / timing sequences / signal waveforms / register layouts / flowcharts.
- Go from "why does this exist" → "what is it" → "how you measure/observe it on a board" → "how interviewers ask about it".

## Files
- Dir: `/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/` (md files MUST be in this dir root, not a subfolder — the renderer's breadcrumb assumes it).
- Name: `2026-11-01_BVxx_<slug>.md` with these fixed slugs (cross-links depend on them):
  - BV01 si_board_validation_big_picture · BV02 schematic_layout_reading · BV03 power_tree_sequencing_pmic · BV04 clocks_osc_pll_refclk
  - BV05 reset_strap_boot_mode · BV06 first_power_on_checklist · BV07 jtag_swd_boundary_scan · BV08 boot_flow_bringup_firmware
  - BV09 ddr_bringup_training_memtest · BV10 board_interface_tests · BV11 lab_instruments_measurement · BV12 si_pi_margining_shmoo_pvt
  - BV13 board_debug_scenarios · BV14 board_validation_plan_automation · BV15 automotive_board_considerations · BV16 mock_interview_qbank
- Related existing notes you may link (relative paths from the job dir): LP track `2026-10-31_LPxx_*.html` (esp. LP03 link training, LP14 validation methodology, LP15 debug scenarios, LP16 bare-metal/Zynq), primer `2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html`, `2026-10-29_c_basics_cram.html`, `2026-10-30_perf_metrics_ssd_vs_soc.html`, EmbeddedLinux notes `../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P1_board_bringup_order.html`, `.../site/P/2026-10-18_P4_schematic_to_dt.html`, `.../site/M/2026-10-17_M11_hw_linked_debugging.html`. Don't rewrite them; link.
- Code/scripts for a note: `onsitePrep/bvXX/` (C, Python, shell, kernel modules). Link from md with relative path.
- Render ONLY your own files: `python3 ~/.claude/skills/job-research/scripts/render_html.py <md path>` (do NOT run it with no args — other writers run in parallel). Check output says `rendered`, then grep the html for broken things.

## Markdown subset (renderer = job-research render_html.py)
- First line `# BVxx · <title>`, second line `<!-- theme: pastel -->`, then a `>` header line in the form `**키**: 값 · **키**: 값` (keys: 묶음, 우선, 선행 노트, 다음 노트, 목록, 코드). Include links: `[BV 트랙 목록](2026-11-01_BV00_board_validation_track.html)`, prev/next BV html, and related LP notes where relevant.
- Headings `##`, `###`, `####` only. `##` = section card + TOC entry. A `##` containing `TL;DR` becomes the highlight card. A `##` containing `출처` makes its numbered list the target for `[n]` footnotes.
- Tables: one line per row; no literal `|` inside cells (write "또는", or `\|` inside backticks is OK). Cells starting ✅/🟡/❌ get colored.
- Lists `-` / `1.`, 2-space indent for nesting. Fenced code blocks ```c / ```text / ```bash / ```python work.
- Inline: **bold**, `code`, links, tags `[확인됨]` `[추정]`, footnotes `[n]`.
- Only raw HTML allowed: `<details><summary>…</summary>` … `</details>` (put blank lines around inner md) and `<br>`.

## Required sections (in order)
0. `## 0. TL;DR` — 5–8 bullets Korean: what this note lets you answer, the 3 things to memorize, 30-second English answer to the headline question.
1. `## 1. 🔰 처음 보는 사람을 위한 설명` — analogy-first, zero-knowledge explanation of the topic, ASCII diagram, **용어 사전 표** (용어 · 풀네임 · 한 줄 쉬운 설명 · 왜 중요한가). This section should be substantial (it's the main fix Don asked for).
2. `## 2. 핵심 개념 (정확한 버전)` — precise spec-level content (register offsets, bit positions, timing numbers, state machines). Cite sources.
3. `## 3. 보드에서 볼 것 · 잴 것` — what to measure and where (test point, instrument, probe/trigger setup, pass criterion), registers/logs to read (PMIC fault regs, reset reason, PLL lock, training results, JTAG output, UART boot log), checklists as tables, **example readings/logs with line-by-line 해석**. Any example not captured from real hardware must be labeled `[예시 출력]` / `[예시 파형]`. Where Linux is relevant (e.g. `i2cdetect`, `ethtool`, `memtester`, `/sys/class/hwmon`, `dmesg` boot log), include it.
4. `## 4. 꼬리 질문 체인` — for each chain from BV00 §2 (and add 1–3 more): Q (English) → 꼬리1 → 꼬리2 …; each step: **English model answer (2–5 sentences, spoken style)** + 한국어 핵심 한 줄. Put each chain's answers inside `<details>` so Don can self-quiz.
5. `## 5. Don 경험 연결` — 2–4 English sentences Don can say, tied to resume facts: SK hynix (chamber reliability test platform automation, NAND/PCIe 3/4/5 high-speed interface reliability, shmoo), Solidigm (Staff, PCIe Gen6 SSD controller, pre-silicon FPGA bring-up of Cortex-R8/R82/M0+, PCIe/NVMe subsystem IP, I2C/SPI/DMA/SRAM/DRAM SoC verification before RTL freeze, perf tuning with silicon validation team), Apple (wireless chipset integration, PCIe/I2C/SPMI/RFFE root-cause with DSO & protocol analyzer). Don't invent numbers — use `[숫자 채우기]` blanks.
6. `## 6. 실습 / 코드` — only verified things (see Verification). If none, give a lab recipe clearly marked `[미실행]`.
7. `## 7. 자주 틀리는 것` — 5–10 traps.
8. `## 8. 출처` — numbered list of URLs actually consulted.
The Rivian JD lists JTAG, I2C, UART, SPI, Ethernet, DDR, PCIe, MIPI CSI-2/ISP, AMD Zynq/Petalinux, Python benchmarking, test coverage. Tie examples to an automotive AI SoC (camera-heavy, LPDDR, PCIe, automotive Ethernet) where natural, tagging Rivian-specific guesses `[추정]`.

Language: Korean prose, technical terms in English, English for interview answers. Length target: 400–900 lines of md per note (substance over padding).

## Research
- Use WebSearch/WebFetch. Good sources: TI/ADI/NXP/Renesas/Infineon app notes (power sequencing, PMIC, crystal oscillator design, I2C bus recovery, scope probing), Tektronix/Keysight/R&S/LeCroy app notes (probing, ripple, eye diagrams, jitter), JEDEC/Micron/Synopsys/Cadence public DDR training material, Arm CoreSight/ADIv5/ADIv6 docs, IEEE 1149.1 explanations (XJTAG/JTAG Technologies/Corelis), AMD Zynq UltraScale+ TRM UG1085 / boot UG1137, Memtest86/U-Boot mtest/Barr Group memory test articles, AEC/ISO public summaries, Linux docs (i2c, hwmon, regulator, ethtool).
- Spec numbers / register offsets / sysfs names: only state verified facts; otherwise tag `[추정]`. Don't invent vendor-specific register names/addresses; use generic descriptions or cite a specific public datasheet/TRM. Standard facts (e.g. IEEE 1149.1 TAP has 16 states, Ethernet PHY ID registers 2/3 per IEEE 802.3 clause 22, I2C 9-clock recovery) should be cross-checked.
- Do not reproduce long copyrighted passages; summarize in your own words.

## Lab (optional but valuable)
- There is no real board. Real, runnable artifacts only: C programs (memory tests with fault injection, TAP state machine, I2C bit-bang recovery on a simulated bus, PLL calculator, reset-reason decode), Python scripts (power-sequence checker on mock CSV, shmoo generator/plotter, board-farm orchestrator with mock power/UART). Put them in `onsitePrep/bvXX/`, run them, and paste the REAL output into the note.
- Optional: Lima VM `elinux` (Ubuntu aarch64) — e.g. `limactl shell elinux -- ...` for `i2cdetect`/`memtester`/`/sys/class/hwmon` if present; read-only, use /tmp/BVxx (VM rootfs may be read-only → /tmp). Don't apt install. Not required.
## Verification
- User-space C on macOS: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined x.c -o /tmp/x && /tmp/x` (default SDK link is broken; don't put flags in a quoted variable). Linux-only C (sysfs/mmap) → build+run in the VM.
- Python: `python3 -I script.py` (stdlib only; matplotlib only if already installed — check with `python3 -c 'import matplotlib'`, else text output).
- Anything not compiled/run → label `[미검증]`.

## Final report (your reply to the orchestrator, keep it short)
Files written, line counts, what was verified (compile/run/real output), anything tagged [추정]/[미검증], open issues.

## Known gotchas (from the LP track run)
- The renderer crashes on inline `code` inside link text (`[`foo`](x.html)`) — keep link text plain.
- Links to BV notes that other writers haven't finished yet are fine (they will exist); use the exact slugs above.
- Other writers run in parallel: touch ONLY your own md/html and your own `onsitePrep/bvXX/` dir. Never edit BV00, other notes, or the spec.
- BV16 (Q bank) exception: sections are `## 0. TL;DR · 사용법` → `## 1. 질문 목록 한눈에` (table) → `## 2.`… one `##` per bundle A–F with `###` per question (English Q, 꼬리 3단, `<details>` with 30–60 s English answer + 한국어 핵심 + 링크 to the BV note) → `## 출처`.
