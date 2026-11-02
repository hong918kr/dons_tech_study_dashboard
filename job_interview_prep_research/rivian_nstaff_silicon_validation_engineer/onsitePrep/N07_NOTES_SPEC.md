# N07 notes spec (writer agents read this first)

Owner: Don (Rivian Staff Embedded SWE, Silicon & Sensing Systems — validates Rivian's in-house automotive AI SoC; onsite prep). The role (confirmed in the 10-30 screen) is **silicon validation + board validation, mostly bare-metal C; RTL not needed**. Onsite = 2 technical + 2 behavioral; Technical 2 is expected to be domain/system, including "this interface fails — debug it" whiteboard questions. Likely interviewer Manjiang Zhang: FPGA/HAPS prototyping, bring-up, post-si validation, PVT, production SLT; ex-Baidu Apollo HW architect (sensor timestamp/sync, clock sync, secure boot, multi-camera patents), Brocade FPGA, Marvell Ethernet PHY ASIC, Cisco packet ASICs → expect depth on Ethernet PHY and camera/sensor sync.

Don: ex-SSD-controller firmware (SK hynix 7 yrs: chamber reliability test platform automation, NAND/PCIe 3/4/5 interface reliability, shmoo; Solidigm Staff: PCIe Gen6 SSD controller, pre-silicon FPGA bring-up of Cortex-R8/R82/M0+, PCIe/NVMe subsystem IP, I2C/SPI/DMA/SRAM/DRAM SoC verification before RTL freeze, perf tuning with silicon validation team), now Apple wireless chipset integration (PCIe/I2C/SPMI/RFFE root-cause with DSO & protocol analyzer, factory test nodes for latent defects). Knows firmware and C well; **not** a board designer, and asked to "assume I know nothing about PCIe/Ethernet/MIPI" — so every interface/acronym gets a plain-Korean 🔰 explanation + analogy before the precise version.

Index: `2026-11-01_N07_interface_debug_scenarios.md` — §2 lists every page and its items (scenario lists, symptom-table rows, code), §3 the common page template, §5 the overlap map. Implement EXACTLY those items/scenario numbers (you may sharpen wording and add a few, never drop or renumber). EXPAND the index, don't contradict it. Don't edit the index, other pages, or this spec.

## Files
- md in the job dir ROOT: `/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/2026-11-01_N07X_<slug>.md`, fixed slugs:
  - N07A debug_framework_instruments · N07B i2c_smbus_debug · N07C spi_qspi_debug · N07D uart_gpio_irq_debug
  - N07E ddr_memory_debug · N07F ethernet_debug · N07G mipi_csi2_camera_debug · N07H mock_debug_drills
- Code: `onsitePrep/n07/<x>/` (lowercase page letter) + `run.sh` that builds/runs everything there and writes `run_output.txt`. Link from md with relative path.
- Render ONLY your own md: `python3 ~/.claude/skills/job-research/scripts/render_html.py <md path>` (NEVER with no args — others run in parallel). Check it prints `rendered`; then grep your html for leftover raw markdown (`**`, `](`, stray `|`) and check every relative link target exists (`ls`) — links to other N07 pages are fine (they'll exist with the slugs above).

## Header
- Line 1 `# N07X · <title>`, line 2 `<!-- theme: pastel -->`, line 3 a `>` line: `**묶음**: … · **우선**: P0/P1 · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: prev/next N07 page html · **관련**: links to the overlapping existing notes from index §5 · **코드**: onsitePrep/n07/<x>/ (or 없음)`.
- Renderer gotcha: in the header `>` line, link text must not contain " · " (it splits the link) — use commas inside link text.

## Sections — interface pages (N07B–N07G), in this order
0. `## 0. TL;DR` — 5–8 Korean bullets: what this page lets you answer, the 3 things to memorize, a 30-second English answer to the page's headline question.
1. `## 1. 🔰 1분 설명` — analogy → precise definition → ASCII connection diagram (SoC controller ↔ pins/PHY ↔ device; pull-ups, level shifters, SerDes where relevant) → 용어 표 (용어 · 풀네임 · 쉬운 설명 · 디버그에서 왜 중요). Short if LP00A/BV10 already covers basics — link them.
2. `## 2. 정상 기준선` — what NORMAL looks like: ASCII waveforms/timing diagrams, the key timing numbers (verified against public specs, cite), a normal log / tool output / counter snapshot. Label pasted tool output `[예시 출력]` unless you really ran it.
3. `## 3. 관찰 지점 지도` — table: 무엇 · 어디서(핀/레지스터/카운터/파일) · 도구 · 정상값 · 이상하면 의미. Then "첫 5분 증거 스냅샷" (ordered command/register list). Linux tools with real flags (check man pages / kernel docs). Never invent vendor register names/addresses — use generic concept names ("NACK status bit", "RX FIFO overrun flag") or cite a public IP doc (e.g. 16550 LSR, AMD PG232 CSI-2 RX, Synopsys public brief) by name.
4. `## 4. 증상 → 원인 한 장 표` — ONE table: 증상 · 계층(전기/타이밍/프로토콜/설정·드라이버/시스템) · 의심 원인 (순위) · 첫 확인 · 확정 방법. Row count per index §2.
5. `## 5. 시나리오 드릴` — one `### <P><n> · <symptom sentence>` per scenario in the index (P = page letter, e.g. `### B3 · 400 kHz에서만 간헐 실패`), each with `####`: 증상 (what you see, concrete: log line / analyzer view / counter) → 먼저 물어볼 것 (3–4 English clarify questions) → 가설 순위 (table: 가설 · 왜 의심 · 확인 방법 · 맞으면 보이는 것) → 측정 · 계측 (what to probe where, trigger setup, ASCII waveform of the bad case where useful) → root cause · fix → 회귀로 굳히기 (the test you add) → 30초 영어 답 (spoken, 4–7 sentences) → 꼬리 질문 (2–3, each English Q then `<details><summary>답</summary>` 2–4 sentence English answer + 한국어 핵심 한 줄 `</details>`). Scenarios the index marks "요약+링크" are shorter: symptom, 3-line answer, what's new here, link.
6. `## 6. 꼬리 질문 체인` — 3–4 chains crossing scenarios: English Q → 꼬리1 → 꼬리2…; each step English spoken answer (2–5 sentences) + 한국어 핵심 한 줄, answers inside `<details>`.
7. `## 7. Don 경험 연결` — 2–4 English sentences tied to resume facts above; numbers as `[숫자 채우기]`, never invented.
8. `## 8. 실습 / 코드` — the code named in index §2: what it does, how to run, REAL output pasted (from run_output.txt), and "출력 읽는 법" per line/case. Anything not run → `[미검증]`.
9. `## 9. 자주 틀리는 것` — 5–10 traps.
10. `## 10. 출처` — numbered URLs actually consulted.
- Length target: 600–1000 md lines per page. Substance over padding; ★ scenarios deeper.

## Sections — N07A (framework page)
`## 0. TL;DR` → `## 1. 🔰 처음 보는 사람을 위한 설명` (analogy, 5-layer diagram, 용어 사전) → `## 2. 핵심 개념 (정확한 버전)` with `###` per index §2 bullet (5-layer × interface matrix; N06H 6 steps applied to interfaces; bisect-knob table per interface; instrument selection table; scope practice incl. 0.35/tr bandwidth rule, probe ground, trigger types, segmented memory/persistence; LA vs protocol analyzer incl. sample-rate rule; golden baseline; first-5-minute snapshot per interface linking to N07B–G §3) → `## 3. 마스터 증상 색인` ★ — one big table: 증상 문장 · 인터페이스 · 계층 · 어디 (link: page html + scenario ID). Include ALL N07B–G scenarios (take titles from the index §2), BV13 S1–S12, LP15 S1–S10 + B1–B2, deck §7 #1–16, N06H N1–N6, BV10 체인 B–F, BV09 Chain 1 and 6. Read those files' headings to get exact titles (`grep -n '^###' file`). Sort by interface, then layer. → `## 4. 꼬리 질문 체인` → `## 5. Don 경험 연결` → `## 6. 실습 / 코드` (none required; a recipe `[미실행]` is fine, e.g. how to set a scope for segmented capture) → `## 7. 자주 틀리는 것` → `## 8. 출처`.

## Section — N07H (Q bank)
`## 0. TL;DR · 사용법` → `## 1. 채점표` (rubric table: 계층 순서 · clarify · 측정 지점·장비 · 배제 논리 · 간헐/통계 · fix 후 회귀 · 시간) → `## 2. 질문 목록 한눈에` (table: ID · Q · 인터페이스 · 링크) → one `##` per bundle (I2C 4 · SPI 3 · UART/GPIO 3 · DDR 5 · Ethernet 5 · CSI-2 5 · PCIe 3 · 혼합/시스템 2) with `###` per prompt: English Q, 꼬리 3단, `<details>` 60–90 s English answer skeleton + 한국어 핵심 + links to N07B–G page (and LP15/BV13) → `## 플래시카드 50장` (table: # · 증상 · 첫 확인 · 이유) → `## 모의 세트 3개` → `## 연습 기록 표` → `## 출처`. Link other pages by page html (not anchors). Read the N07B–G md files if they exist when you write; otherwise base on the index §2 and keep scenario IDs consistent with it.

## Markdown subset (renderer = job-research render_html.py)
- Headings `##`/`###`/`####` only. A `##` containing `TL;DR` becomes the highlight card; a `##` containing `출처` makes its numbered list the target for `[n]` footnotes.
- Tables one line per row; NO literal `|` inside cells (write "또는"; `\|` inside backticks is OK). Cells starting ✅/🟡/❌ get colored.
- Lists `-` / `1.`, 2-space indent nesting. Fenced ```c ```text ```bash ```python.
- Inline **bold**, `code`, links, tags `[확인됨]` `[추정]` `[예시]` `[예시 출력]` `[미검증]`, footnotes `[n]`.
- Only raw HTML: `<details><summary>…</summary>` … `</details>` (blank line after the summary line and before `</details>`) and `<br>`.
- **Never put inline code inside link text** (renderer crashes). Keep link text plain.

## Linking (don't rewrite existing material — link it)
Relative links from the job dir. Confirm names with `ls` first. Key ones: `2026-10-30_final_skim.html` (§5 one-line bus table), `2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html` (§4 Ethernet, §5 MIPI), `2026-10-31_LP15_debug_scenarios.html`, `2026-10-31_LP13_linux_pcie_tools.html`, `2026-10-31_N01_pcie_lspci_ltssm.html`, `2026-11-01_BV09_ddr_bringup_training_memtest.html`, `2026-11-01_BV10_board_interface_tests.html`, `2026-11-01_BV11_lab_instruments_measurement.html`, `2026-11-01_BV12_si_pi_margining_shmoo_pvt.html`, `2026-11-01_BV13_board_debug_scenarios.html`, `2026-11-01_N06E_plans_highspeed_io_sensor.html`, `2026-11-01_N06C_plans_peripherals_soc_infra.html`, `2026-11-01_N06D_plans_dma_memory.html`, `2026-11-01_N06H_debug_methodology_triage.html`, `2026-10-25_rivian_hm_screen_prep.html` (deck §7 scenarios 1–16), `hmScreenPrep/problems/<V1_march_memtest|V6_prbs_crc_checker|V8_i2c_scan_recover|V9_csi2_packet_parse|V10_eth_frame_fcs|V12_frame_timestamp_check|L3_rfc2544_throughput|L4_pps_time_discipline|L5_spsc_sensor_ring|D_debug_drills>/index.html`, N05 pages `2026-11-01_N05*_*.html`. **Skim the overlapping note(s) for your page first** (index §5) so you add rather than repeat — especially BV10 (interface board tests), BV13 (board debug), N06E/N06C (test plans with "잘 터지는 버그"), BV11 (instruments, for N07A).

## Research
WebSearch/WebFetch allowed. Good sources: NXP UM10204 (I2C), SMBus spec, TI/ADI/Analog app notes (I2C pull-up, SPI modes, UART baud tolerance), 16550 UART datasheet (TI PC16550D), JEDEC public SPI-NOR SFDP/flash datasheets (Winbond/Macronix public), Linux kernel docs (i2c, spi, spi-nor, serial, gpio, ethtool, phylib, v4l2, media-ctl, EDAC), IEEE 802.3 public summaries / OPEN Alliance TC (100BASE-T1/1000BASE-T1, SQI), RGMII v2.0 spec (2 ns delay), MIPI public overviews (CSI-2, D-PHY), AMD PG232 CSI-2 RX subsystem, Keysight/Tektronix/Rohde app notes (scope bandwidth, probing, segmented memory), memtest86/memtester docs. Verify spec numbers; otherwise tag `[추정]`. Summarize in your own words; no long quotes.

## Code
- C on macOS: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined x.c -o $TMPDIR/x && $TMPDIR/x` (default SDK link is broken; don't put flags in a quoted variable). Write binaries to `$TMPDIR`, not the repo.
- Python: `python3 -I script.py`, stdlib only.
- Mock inputs (captures, logs, snapshots) live as small text files next to the code. Each case should have an expected diagnosis and the program should print PASS/FAIL against it (self-checking). Paste REAL output into the note.
- Keep code new — don't duplicate V8 (I2C scan/recover), V6 (PRBS/CRC), V9 (CSI-2 packet ECC/CRC), V10 (Ethernet FCS), V1 (march test), BV10 §6.1 (stuck-bus recovery sim), BV09 §6.1 (fault-injection matrix).

## Language
Korean prose; technical terms in English; interview-spoken parts (clarify, 30-second answers, follow-up answers, Don sentences) in English.

## Final reply to orchestrator (short)
Files + line counts, sections/scenarios written, code + run output summary (PASS lines), [추정]/[미검증] items, broken-link check result, open issues.
