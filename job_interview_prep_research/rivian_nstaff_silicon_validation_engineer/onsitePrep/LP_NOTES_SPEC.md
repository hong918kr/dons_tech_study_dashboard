# LP notes spec (writer agents read this first)

Owner: Don (Rivian Staff Embedded SWE, Silicon & Sensing validation onsite prep). Track index: `2026-10-31_LP00_linux_pcie_track.md` (§2 has the outline + tail-question chains for every note — EXPAND it, don't contradict it).

## Reader assumption (most important — Don's explicit request 2026-10-31)
"Assume I know NOTHING about PCIe, Ethernet, MIPI." Every note must be readable by a smart engineer who has never studied PCIe.
- Every acronym spelled out + explained in plain Korean on first use (e.g. "BAR (Base Address Register: 장치가 '내 레지스터를 이 크기만큼 메모리 주소에 매핑해 달라'고 요청하는 칸)").
- Use analogies (택배/도로/아파트 주소 etc.) BEFORE the precise definition, then give the precise definition.
- Draw ASCII diagrams in ```text code blocks for topology / sequences / register layouts.
- Go from "why does this exist" → "what is it" → "how Linux shows it" → "how interviewers ask about it".

## Files
- Dir: `/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/` (md files MUST be in this dir root, not a subfolder — the renderer's breadcrumb assumes it).
- Name: `2026-10-31_LPxx_<slug>.md` with these fixed slugs (cross-links depend on them):
  - LP00A io_primer_pcie_ethernet_mipi
  - LP01 pcie_topology_linux_model · LP02 config_space_capabilities · LP03 link_training_ltssm · LP04 enumeration_resources_hotplug
  - LP05 soc_host_controller_integration · LP06 endpoint_driver_dev · LP07 interrupts_intx_msi_msix · LP08 dma_iommu_coherency
  - LP09 aer_dpc_error_handling · LP10 power_mgmt_aspm · LP11 reset_types · LP12 sriov_vfio_acs_p2p
  - LP13 linux_pcie_tools · LP14 validation_methodology · LP15 debug_scenarios · LP16 baremetal_vs_linux_zynq · LP17 gen5_gen6_retimer_automotive
- Code/scripts for a note: `onsitePrep/lpXX/` (C, Python, shell, kernel modules). Link from md with relative path.
- Render ONLY your own files: `python3 ~/.claude/skills/job-research/scripts/render_html.py <md path>` (do NOT run it with no args — other writers run in parallel). Check output says `rendered`, then grep the html for broken things.

## Markdown subset (renderer = job-research render_html.py)
- First line `# LPxx · <title>`, second line `<!-- theme: pastel -->`, then a `>` header line in the form `**키**: 값 · **키**: 값` (keys: 묶음, 우선, 선행 노트, 다음 노트, 목록, 코드). Include links: `[LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html)`, `[입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html)`, prev/next LP html.
- Headings `##`, `###`, `####` only. `##` = section card + TOC entry. A `##` containing `TL;DR` becomes the highlight card. A `##` containing `출처` makes its numbered list the target for `[n]` footnotes.
- Tables: one line per row; no literal `|` inside cells (write "또는", or `\|` inside backticks is OK). Cells starting ✅/🟡/❌ get colored.
- Lists `-` / `1.`, 2-space indent for nesting. Fenced code blocks ```c / ```text / ```bash / ```python work.
- Inline: **bold**, `code`, links, tags `[확인됨]` `[추정]`, footnotes `[n]`.
- Only raw HTML allowed: `<details><summary>…</summary>` … `</details>` (put blank lines around inner md) and `<br>`.

## Required sections (in order)
0. `## 0. TL;DR` — 5–8 bullets Korean: what this note lets you answer, the 3 things to memorize, 30-second English answer to the headline question.
1. `## 1. 🔰 처음 보는 사람을 위한 설명` — analogy-first, zero-knowledge explanation of the topic, ASCII diagram, **용어 사전 표** (용어 · 풀네임 · 한 줄 쉬운 설명 · 왜 중요한가). This section should be substantial (it's the main fix Don asked for).
2. `## 2. 핵심 개념 (정확한 버전)` — precise spec-level content (register offsets, bit positions, timing numbers, state machines). Cite sources.
3. `## 3. Linux에서 볼 것` — commands, sysfs paths, dmesg patterns, **example output with line-by-line 해석**. Prefer REAL output captured from the Lima VM (see Lab). Mark synthetic examples as `[예시 출력]`.
4. `## 4. 꼬리 질문 체인` — for each chain from LP00 §2 (and add 1–3 more): Q (English) → 꼬리1 → 꼬리2 …; each step: **English model answer (2–5 sentences, spoken style)** + 한국어 핵심 한 줄. Put each chain's answers inside `<details>` so Don can self-quiz.
5. `## 5. Don 경험 연결` — 2–4 English sentences Don can say, tied to resume facts: SK hynix (chamber reliability test platform automation, NAND/PCIe 3/4/5 high-speed interface reliability, shmoo), Solidigm (Staff, PCIe Gen6 SSD controller, pre-silicon FPGA bring-up of Cortex-R8/R82/M0+, PCIe/NVMe subsystem IP, I2C/SPI/DMA/SRAM/DRAM SoC verification before RTL freeze, perf tuning with silicon validation team), Apple (wireless chipset integration, PCIe/I2C/SPMI/RFFE root-cause with DSO & protocol analyzer). Don't invent numbers — use `[숫자 채우기]` blanks.
6. `## 6. 실습 / 코드` — only verified things (see Verification). If none, give a lab recipe clearly marked `[미실행]`.
7. `## 7. 자주 틀리는 것` — 5–10 traps.
8. `## 8. 출처` — numbered list of URLs actually consulted.
Optional `## Ethernet/MIPI 비교` subsection where a parallel helps (e.g. PCIe AER vs Ethernet FCS/CRC counters, PCIe link training vs Ethernet auto-negotiation vs MIPI D-PHY HS/LP) — the Rivian JD lists PCIe, Ethernet, MIPI CSI-2, DDR, I2C/SPI/UART, JTAG. Keep short.

Language: Korean prose, technical terms in English, English for interview answers. Length target: 400–900 lines of md per note (substance over padding).

## Research
- Use WebSearch/WebFetch. Good sources: docs.kernel.org (PCI, PCI/pciebus-howto, PCI/pcieaer-howto, PCI/endpoint/*, PCI/msi-howto, PCI/sysfs-pci, power/pci, driver-api/pci), kernel source on elixir.bootlin.com / github torvalds/linux (drivers/pci/...), pciutils man pages, PCI-SIG public material, Synopsys/Xilinx (AMD) public docs (PG194/PG195 XDMA, PG213 PCIe, Zynq UltraScale+ TRM UG1085), MindShare-style explanations, vendor app notes.
- Spec numbers / register offsets / sysfs names: only state verified facts; otherwise tag `[추정]`. Cross-check register offsets against `pciutils` `lib/header.h` or kernel `include/uapi/linux/pci_regs.h` (fetch it — it is the ground truth for bit names/offsets).
- Do not reproduce long copyrighted passages; summarize in your own words.

## Lab (optional but valuable — real output beats invented output)
- Lima VM `elinux` (Ubuntu, aarch64, kernel 7.0.0-38-generic, Apple VZ): `limactl shell elinux -- sudo lspci -vvv` etc. Devices are virtio on an Apple host bridge (conventional PCI-ish, no real link). Read-only commands only; don't change VM system config; don't apt install unless trivial and needed.
- QEMU in the VM: `qemu-system-aarch64 -machine virt` has a generic ECAM PCIe host (pci-host-ecam-generic). You can add `-device pcie-root-port,id=rp1,chassis=1 -device edu,bus=rp1` or `-device e1000e`, `-device nvme,...` to get real PCIe capability output (LnkCap/LnkSta are emulated). Lab kernel + BusyBox initramfs + module build recipe: read `/var/lab/README.txt` in the VM. BusyBox lspci is minimal — read `/sys/bus/pci/devices/*/config` with hexdump/od, or copy a static tool. Use your own scratch dir `/var/tmp/LPxx/` in the VM; never modify `/var/lab`.
- Home dir `/Users/donh` is mounted READ-ONLY in the VM: copy sources into /var/tmp/LPxx, build there, copy results back out with `limactl shell elinux -- cat file > local` or `limactl copy elinux:/path local`.
- No KVM (TCG) → boots take 5–30 s; always use `timeout`.

## Verification
- User-space C on macOS: `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined x.c -o /tmp/x && /tmp/x` (default SDK link is broken; don't put flags in a quoted variable). Linux-only C (sysfs/mmap) → build+run in the VM.
- Kernel modules → `make -C /var/lab/out/linux-build M=$PWD modules` in the VM; if you run it in QEMU, paste real dmesg.
- Anything not compiled/run → label `[미검증]`.

## Final report (your reply to the orchestrator, keep it short)
Files written, line counts, what was verified (compile/run/real output), anything tagged [추정]/[미검증], open issues.
