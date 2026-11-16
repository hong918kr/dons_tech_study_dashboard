# Lesson writer brief — track PCIE (PCIe · NVMe), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Rivian silicon validation: his HM screen asked
lspci / LTSSM / "what is L0" + a C bit puzzle; also Apple, NVIDIA, Anduril, Tesla).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
PCIE-B is the **"decode it yourself"** track: layers/TLP bytes, bandwidth math, config space (Type 0/1, caps, BARs),
ECAM, enumeration, TLP headers, ordering, then the NVMe controller registers and init sequence.
C Basic/Medium/Advanced, DS, ALGO, ARCH, OS, COMM (B/M/A), SIV (B/M/A), HWLAB (B/M), BM (B/M), ARM-B lessons exist —
assume he knows them and cross-reference by id ("(COMM-B09 참고)") instead of re-teaching.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/PCIE.json — find your item (title/goal/task/tags). The lesson must do the item's `task`
   completely (every sub-function, every case it names). Skim the other PCIE items (B, M, A) so you stay in your
   lane: the other 12 B items are being written in parallel right now — reference them by id, don't depend on
   their files. **Every code file must be self-contained** (if you need a neighbour's model, embed a compact
   version and say "(PCIE-B08 모델 축약판)").
3. Reference lessons for depth/tone/harness: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed",
   return 1 on failure), ROOT/lessons/COMM/COMM-B10/ (recent Basic C lesson, parser state machine) and
   ROOT/lessons/SIV/SIV-B01/ (a **Python** lesson: starter.py/solution.py/practice.py, `<!-- code: x.py -->`).
   Related lessons to cross-ref only where they fit: COMM-B08/COMM-B09 + ALGO-M01/M02 (CRC), SIV-M06 (SerDes PRBS/BER),
   HWLAB-M09/M12 (eye, equalization), BM-B04 (W1C/RMW traps), BM-M08 + ARCH-M12 (DMA, cache coherency),
   OS-M01 (page tables), ELX-B lessons (sysfs). Not yet written (cite by id only): PCIE-M01..M15, PCIE-A01..A10.

## Ground truth for register facts (USE IT — don't trust memory)
The Lima VM has the Linux headers; read them (read-only) for every offset/bit you put in code:
    limactl shell elinux -- cat /usr/include/linux/pci_regs.h                     # config space, caps, ext caps, LnkCap/LnkSta
    limactl shell elinux -- cat /usr/src/linux-headers-7.0.0-38-generic/include/linux/nvme.h   # NVMe BAR0 regs, CAP/CC/CSTS bits
Cite them in the md ("Linux include/uapi/linux/pci_regs.h 기준"). Facts you can't check against a header or a spec
text (exact ordering-table cells, Gen6 FLIT byte split, framing token bit layout, timing values) → write them from
the best source you know, label "스펙 확인 권장" when unsure, and keep them out of test ground truth unless they are
your own model's stated definition. Don (math major, ex-SSD FW) will notice wrong numbers.

## Environments
**Mac** (all items except B06): Apple Silicon, macOS, Apple clang, Python 3.9 stdlib only.
`export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` before any cc (27.2 SDK link is broken).
You MAY capture real reference data from the VM for your own lesson (e.g. a real 256B/4KB config dump:
`limactl shell elinux -- sudo lspci -xxxx -s 00:06.0`) and save it as a data file in your lesson dir — but the VM's
devices are virtio **conventional-PCI-style endpoints on a host bridge (no PCIe capability, no bridges)**. If your
lesson needs a PCIe capability / extended caps / a bridge, either hand-build a fictional dump (say so, "가상 덤프") or
reuse the QEMU recipe below. Mark every real capture with where it came from.

**Lima VM `elinux`** (B06; optional captures for others): aarch64 Ubuntu 26.04, kernel 7.0, running — other sessions
use it: never stop/restart it. `limactl shell elinux -- bash -lc '...'`. Passwordless sudo. The Mac home is mounted
READ-ONLY at the same path → copy to `/tmp/<ID>_work/` in the VM. Has lspci/setpci (pciutils), gcc, clang,
qemu-system-aarch64 (TCG only). **No web installs. Never insmod into the VM's own kernel.**
QEMU lab `/var/lab` (read-only; read `/var/lab/README.txt` first): `out/Image` (Linux 6.18.55 defconfig:
PCI_HOST_GENERIC=y, PCIEPORTBUS=y, PCIEAER=y, PCIEASPM=y, BLK_DEV_NVME=**m** → modules under /var/lab/build/linux),
`out/initramfs.cpio.gz` / `build/rootfs/` (BusyBox static; `LAB_CMD=cmd~with~tildes` runs cmd then powers off).
A PCIe topology with real bridges + an NVMe endpoint: `qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1
-m 512M -nographic -kernel /var/lab/out/Image -initrd i.gz -device pcie-root-port,id=rp1,chassis=1,slot=1
-drive if=none,id=d0,file=/var/tmp/<ID>_work/disk.img,format=raw -device nvme,serial=esn01,drive=d0,bus=rp1
-append "console=ttyAMA0 LAB_CMD=..."` (verify; adjust). In the guest, `/sys/bus/pci/devices/*/config` gives the raw
config bytes (hexdump). 13 writers share 6 vCPUs: `-smp 1`, `timeout`, small runs.

## Output
ROOT/lessons/PCIE/<ID>/2026-11-15_<ID>_<english_snake_slug>.md + starter.X + solution.X + practice.X
(front matter `updated: 2026-11-15`, `time:` realistic for Basic, e.g. 40분/50분/60분).
- md title line: `# <ID> <title exactly as in PCIE.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥6, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.X -->` + run cmd), ## 10. 핵심 정리, ## 참고자료.
- Size: ~450–650 lines of md.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text). This track lives on **byte/bit layout diagrams**: DW-by-DW TLP
  header bit fields (DW0 Fmt/Type/TC/Attr/TH/TD/EP/AT/Length …), config-space maps with offsets, capability linked
  lists with arrows, BAR bit layout, topology trees with bus numbers, layer-wrapping stacks, register maps, init
  sequence flowcharts. Numbers in figures must match your code / real output.
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why this
  matters in silicon-validation / SSD-FW / bring-up interviews. Tie to firmware reality: SSD controller endpoint FW,
  host driver probe, board bring-up with a link that trains at the wrong width, Don's ex-SSD background.

## Interview flavour
- 2. Problem (English) reads like a real firmware/validation coding prompt (signatures, constraints, example I/O,
  e.g. "decode `lspci -xxx` text for 00:06.0 → print VID:DID, class, enabled decoders").
- 면접 Q&A = real follow-ups: why read-back flushes posted writes, why a BAR must be naturally aligned, why 0xFFFF
  means "no device", what LnkSta x2 vs LnkCap x4 tells you, why MPS/MRRS matter for throughput, what CC.EN=0 does
  to queues, CAP.TO meaning, how Linux finds devices on ARM (ECAM from DT/ACPI MCFG), Type 0 vs Type 1, etc.

## Code rules (all must hold — you MUST actually run these)
**C lanes:**
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as  # clean (practice too)
  (B13 uses pthreads: also `cc -std=c11 -g -O1 -fsanitize=thread solution.c` clean.)
**Python lanes:** Python 3.9 stdlib only. `python3 -I starter.py` → runs, some FAIL, exit 1; `python3 -I solution.py`,
  `python3 -I practice.py` → all PASS, exit 0; `python3 -I -W error -X dev solution.py` clean. CHECK-style harness
  (`PASS name` / `FAIL name: detail`, "N/M passed", sys.exit(1) on failure); stubs return None / raise
  NotImplementedError caught by the harness so starter never crashes.
- Use your own unique temp names (/tmp/<ID>_*) and a PRIVATE scratch dir /tmp/<ID>_work/ (Mac and VM).
- Config-space access style (shared by all config lessons): config space is a little-endian byte array
  (`uint8_t cfg[256]` or `[4096]`); read helpers assemble from bytes (`cfg_rd8/16/32(const uint8_t*, uint16_t off)`),
  never cast to unaligned pointers. Text input = `lspci -xxx/-xxxx` format (`00: 86 80 ...` — hex offset, colon,
  16 bytes). Print BDF as `bb:dd.f`. Use names from pci_regs.h for constants (your own `#define`s with the same
  names/values — the code must build on the Mac without the header).
- No malloc in "firmware/driver" code paths (static/caller buffers, explicit capacities); parsers take lengths and
  reject truncated / malformed input with an error code, never read past the buffer. Capability walks must stop on
  loops (visited set or hop limit) and on pointers < 0x40 (legacy) / < 0x100 (extended).
- Tests: real and deterministic — exhaustive where cheap, else fixed-seed xorshift32 random + named edge cases.
  Every case the task names must be asserted. solution/practice each < 2 s. Long sweeps / tables → demo_*.{c,py}
  in the lesson dir; run them and paste REAL output. Never invent output.
- solution = starter with ONLY the TODO bodies filled (same harness, same line structure where possible, same header
  comment) so a diff highlights just the changed lines. Starter stubs compile warning-free ((void) casts), return a
  wrong-but-safe value, never crash/hang (B13: starter's wait loops must have timeouts).

## Lanes (stay in yours; cross-ref the others)
- **B01 (Python)** Layer wrapping encoder. One 3DW **MRd** (no data) and one **MWr** with 1–4 DW payload: TLP
  (header bytes + payload + optional ECRC) → DLL (12-bit Sequence Number in 2 bytes + LCRC 4 bytes) → PHY framing in
  two flavours: Gen1/2 (STP/END framing symbols around seq..LCRC, 8b/10b = 10 bits on the wire per byte — compute
  wire bits, full 8b/10b code tables NOT required) and Gen3+ (4-byte STP token carrying length; 128b/130b blocks with
  2-bit sync header; no END) — label exact token bit layout "스펙 확인 권장". Print a byte-by-byte table with a
  "layer" column. Also show one DLLP (Ack, 6 bytes = type + seq + 16-bit CRC, + framing) for contrast. ECRC/LCRC are
  CRC-32 (poly 0x04C11DB7) — compute them with your own code but state that exact bit/byte ordering on the wire is
  spec-defined and your model's ordering is your stated definition (tests check round trip + field positions, not
  claimed silicon bit order). Decoder that unwraps PHY→DLL→TLP and verifies CRCs; injected corruption detected.
  Header field details = B10's lane (keep Fmt/Type/Length/ReqID/Tag/BE/Addr encode here minimal, cite B10).
- **B02 (Python)** Bandwidth tool: Gen1–Gen6 × x1/x2/x4/x8/x16. GT/s 2.5/5/8/16/32/64; encoding 8b/10b (Gen1/2),
  128b/130b (Gen3–5), Gen6 PAM4 + FLIT mode (256-byte flit: TLP bytes / DLP / CRC / FEC split — state your model,
  label 스펙 확인 권장; FLIT mode removes per-TLP LCRC/seq). Per-TLP overhead model (state it: framing + seq + header
  12/16B + LCRC 4 (+ ECRC)) × MPS 128/256/512, plus DLLP overhead (Ack/UpdateFC rate — a stated assumption, e.g. one
  8-byte DLLP per N TLPs). Answer "Gen4 x4 NVMe 이론 최대 순차 읽기" (reads come back as CplD — consider MRRS/RCB, read
  completion size) and compare with real drive spec-sheet numbers (~7 GB/s class, hedged). Tables in md from real runs.
  Units: GT/s vs Gb/s vs GB/s vs GiB/s — make the distinction explicit, tests pin known values (Gen3 x1 ≈ 985 MB/s,
  Gen4 x4 ≈ 7.88 GB/s after encoding — verify with your own arithmetic).
- **B03 (C)** Type 0 header parser: input lspci -xxx text (256B; accept 4KB -xxxx too), decode VID/DID, Command
  (IO/Mem/BusMaster/SERR/INTx-disable …), Status (Cap list, error bits incl. Received Master Abort, Signaled/Received
  Target Abort, Detected Parity), RevID, Class code triple + a small class-name table (01:08:02 = NVMe, 02:00 Ethernet,
  06:04 PCI bridge …), Header Type (bit7 multi-function), BAR raw values (no sizing — B05), Subsystem IDs, Cap Pointer,
  Int Line/Pin. Output must include diagnostic sentences: Mem Space off → "BAR 디코드 안 됨, MMIO 접근 시 UR/0xFFFFFFFF";
  Bus Master off → "장치가 DMA/MSI 못 보냄". Test with ≥1 real VM dump (captured) + hand-made dumps; Header Type 1 →
  report "Type 1, PCIE-B07 참고" and stop. Truncated/malformed text rejected.
- **B04 (C)** Capability walker: legacy list from 0x34 (PM 0x01, MSI 0x05, PCIe 0x10, MSI-X 0x11, vendor 0x09 …) and
  extended list from 0x100 (32-bit header: ID 15:0, version 19:16, next 31:20; AER 0x0001, LTR 0x0018, L1SS 0x001E,
  SR-IOV 0x0010 …). Decode the PCIe Capability: device/port type, LnkCap (max speed code, max width, ASPM support,
  port number), LnkCtl, LnkSta (current speed, negotiated width, training, DLL active). Speed codes 1..6 → 2.5..64 GT/s
  (check pci_regs.h). Loop protection (visited bitmap + hop limit), bad pointer (<0x40, unaligned, off end). Test on a
  hand-built 4KB dump of a fictional Gen4 x4 NVMe SSD trained at Gen4 x2 (say "가상 덤프") + a looped list + a QEMU-
  or VM-captured dump if you capture one.
- **B05 (C)** BAR sizing on a mock device: config write goes through a per-BAR **write mask** (hardwired low bits,
  size-aligned address bits writable). Six BARs of mixed kind: IO (e.g. 256B), Mem32 non-prefetch (4KB), Mem64
  prefetchable pair (e.g. 256MB across BAR2/BAR3), Mem32 (64KB), unimplemented BAR (reads 0). Routine: disable Mem/IO
  decode in Command → save → write 0xFFFFFFFF → read → restore → re-enable; 64-bit pair sizes with both halves;
  size = ~(mask)+1 with the type bits masked (IO: ~0x3, Mem: ~0xF). Tests: every BAR's kind/size/64-bit/prefetch,
  original values restored, Command restored, a mock that **logs MMIO accesses that hit a half-written BAR while
  decode is on** (why you disable decode), and the classic bug (forgetting to mask low bits / treating 64-bit upper
  half as its own BAR) reproduced in a demo or Part B test.
- **B06 (VM lane: linux)** lspci/setpci/sysfs practice. Code: a C config-space reader/parser (`/* ENV: linux */`)
  that reads `/sys/bus/pci/devices/<BDF>/config` (and `resource`, `driver` symlink, `class`, `vendor`) and prints
  BDF, VID:DID, class, BARs from the `resource` file, cap list, "Kernel driver in use" — and a compare step against
  real `lspci -vvv -s <BDF>` / `lspci -xxx` text (same values). starter/solution/practice.c + `check.sh
  <starter|solution|practice>` runs INSIDE the VM from any cwd: copies the lesson dir to `/tmp/PCIE-B06_work/<variant>.$$`,
  builds `gcc -std=c11 -Wall -Wextra -Werror -O2`, runs, prints PASS/FAIL + `N/M passed`, last line
  `RESULT: PASS (<variant>)` / `RESULT: FAIL (...)`, exit 0/1; starter exits 1 fast; cleans up (trap EXIT).
  Checker runs: `limactl shell elinux -- bash ROOT/lessons/PCIE/PCIE-B06/check.sh solution`.
  md: real outputs of `lspci -tv`, `lspci -vvv -s 00:06.0`, `sudo lspci -xxxx -s 00:06.0`, `setpci -s 00:06.0
  COMMAND` / `VENDOR_ID.w` / `CAP_EXP+0x12.w` style reads (only show what you ran; explain why PCIe-cap reads fail on
  this conventional virtio device), /sys files. Strongly recommended: one QEMU-guest capture with
  `pcie-root-port` + `nvme` (recipe above; nvme.ko from /var/lab/build/linux if needed, or just look at config bytes —
  no driver needed for that) to show a real Type 1 bridge + PCIe cap + LnkCap/LnkSta (QEMU-emulated values — label).
  Part C: interpretation questions on **constructed** real-HW-style `lspci -vvv` excerpts (label "실HW 형식 예시"):
  Gen4 x4 SSD showing `LnkSta: Speed 16GT/s, Width x2 (downgraded)`, a Gen1-trained link, `LnkSta: ... DLActive-`,
  Mem- in Control, AER `UESta: CmpltTO+` — what each means and what you'd check first.
- **B07 (C)** Type 1 header + routing: decode Primary/Secondary/Subordinate Bus, IO base/limit (16/32-bit), Memory
  base/limit (bits 15:4 → addr 31:20, limit | 0xFFFFF), Prefetchable base/limit + upper 32 bits (64-bit), Bridge
  Control. Router: given a switch with USP + N DSPs (each a Type 1 header), decide for (a) a memory address and (b) a
  config request to bus B which DSP forwards it (or "not mine" / goes upstream), including empty window (base >
  limit), prefetchable 64-bit above 4GB, bus == secondary (Type 0 to device) vs secondary < bus ≤ subordinate
  (Type 1 forwarded). Exhaustive-ish tests over bus numbers + boundary addresses.
- **B08 (C)** Config access: ECAM `off = bus<<20 | dev<<15 | fn<<12 | reg` over a mock ECAM region (e.g. 2 buses ×
  32 dev × 8 fn × 4KB = static, or a sparse map) with cfg_read/write 8/16/32 (alignment rules: 16-bit at even, 32-bit
  at multiple-of-4 → error), absent function reads all-1s (0xFFFF VID), legacy CF8/CFC emulation
  (`0x80000000 | bus<<16 | dev<<11 | fn<<8 | (reg & 0xFC)`, byte lanes via CFC+ (reg&3)), only 256B reachable via
  CF8 (extended regs need ECAM; AMD's ext-reg bits = hedge). Tests: same register via both paths, absent device,
  misaligned access, dev>31/fn>7 rejected, write to read-only VID ignored. Mention how ARM Linux gets the ECAM base
  (DT `pci-host-ecam-generic` reg / ACPI MCFG) — the VM's QEMU virt has one (show from DT/`/proc/iomem` if you capture).
- **B09 (C)** DFS enumeration over a virtual topology (embed a compact ECAM mock, "PCIE-B08 모델 축약판"):
  root port → switch (USP + 3 DSPs) → endpoints incl. one multi-function device (Header Type bit7, fn1..7 probed only
  then) and an empty slot. Algorithm: assign secondary = next bus, temporarily subordinate = 0xFF, recurse,
  then subordinate = max bus found. Write the numbers into the mock bridges' config space; output `lspci -tv`-style
  tree; tests on exact bus numbers, a second topology, and the bug "forgot to set subordinate before recursing → Type 1
  configs not forwarded" reproduced. No resource (BAR window) allocation — that's PCIE-A08.
- **B10 (C)** TLP header codec: 3DW/4DW MRd, MWr, CplD (and Cpl without data) — Fmt/Type (MRd32 0x00, MRd64 0x20,
  MWr32 0x40, MWr64 0x60, Cpl 0x0A, CplD 0x4A — verify), TC, Attr (RO/NS/IDO), TD, EP, Length (DW, 0 = 1024),
  Requester ID, Tag, Last/First DW BE, address (must be 4DW iff ≥ 4GB), Completer ID, Status, BCM, Byte Count,
  Lower Address. Functions: first/last BE for (addr,len), split a request at 4KB boundaries and MPS/MRRS limits,
  compute CplD Byte Count / Lower Address for a read split into multiple completions (RCB 64/128B).
  Encode→decode round trip over random fields; known hand-computed vectors (show the bytes).
- **B11 (C)** Ordering checker: encode the PCIe ordering table (row = later transaction, column = earlier: Posted,
  Non-Posted Read, NP Write (config/IO writes), Completion; entries Yes / No / Y-or-N) including RO and IDO
  modifications — **write down the table you encode, with the source, and mark any cell you're unsure of**; the
  checker validates whether an observed arrival order is legal for a given issue order. Tests: MWr then MRd to the
  same device (read cannot pass write → read-back flushes), posted writes stay ordered, CplD may not pass posted write
  (producer/consumer correctness: data write then flag write then interrupt), RO relaxations. Part C: driver example —
  MMIO doorbell write + read-back (`writel(); readl();`), and why a DMA completion must not be seen before data
  (cross-ref ARCH-M12 / BM-M08). Deadlock-avoidance rationale for "posted must be able to pass NP".
- **B12 (C)** NVMe BAR0 register map as a struct with `_Static_assert(offsetof(...) == ...)` for CAP 0x00 (64),
  VS 0x08, INTMS 0x0C, INTMC 0x10, CC 0x14, CSTS 0x1C, NSSR 0x20, AQA 0x24, ASQ 0x28 (64), ACQ 0x30 (64), CMBLOC/CMBSZ,
  … and doorbells from 0x1000 with stride 4 << CAP.DSTRD (verify all against nvme.h). Decoders: CAP (MQES 0-based,
  CQR, AMS, TO in 500 ms units, DSTRD, NSSRS, CSS, BPS, MPSMIN/MPSMAX → 2^(12+n)), VS (major.minor.tertiary),
  CC (EN, CSS, MPS, AMS, SHN, IOSQES, IOCQES), CSTS (RDY, CFS, SHST, NSSRO, PP). Sample dumps: hand-made "typical
  consumer SSD" values (hedged) + optional real QEMU nvme BAR0 values (B06 recipe, read via `/sys/.../resource0` +
  mmap or BusyBox devmem in the guest — label QEMU-emulated). Interview: doorbell address for SQ y / CQ y, "MQES=1023
  means how many entries", 64-bit register access width. Note on volatile + 32-bit access ordering for 64-bit regs
  (lo/hi read-retry) — init sequence itself is B13.
- **B13 (C, pthreads)** Init sequence against a mock controller running in its own thread (state machine reacting to
  CC.EN 0→1 / 1→0 after a delay ≤ CAP.TO; scale time so tests stay < 2 s and say so). Host: if CSTS.RDY=1 → CC.EN=0,
  wait RDY=0 (timeout from CAP.TO) → program AQA/ASQ/ACQ (alignment: ASQ/ACQ page-aligned, sizes ≤ MQES+1… check) →
  CC (CSS, MPS within MPSMIN..MAX, AMS, IOSQES=6, IOCQES=4) → EN=1 → wait RDY=1 with timeout. Shared registers as
  `_Atomic uint32_t` (or mutex) — must be TSan-clean. Faults injected: controller sets CSTS.CFS → host aborts with
  error and does a reset; RDY never comes (timeout fired correctly, exact error code); MPS outside range rejected
  before touching CC; register writes while EN=1 that the mock flags. Verdicts from states/logs, not timing. Shutdown
  (CC.SHN=01 → CSTS.SHST=10) in 추가 연습. First admin command / queues = PCIE-M01..M03 (cite).

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = edge/fault cases (every injected error), C = firmware/validation-
  realistic use (driver probe step, bring-up triage, SSD-FW endpoint view, validation checklist).
- 풀이 is detailed: rule stated explicitly with offset/bit tables, a step-by-step trace on a small example (bytes →
  fields), why each design choice. Bit layouts drawn.
- Accuracy over volume. Cross-check every offset/bit with the headers above; every formula with your own arithmetic.

## When done
Reply with: files written (paths), md line count, PASS counts for starter/solution/practice (and check.sh results for
B06), ASan/UBSan (+TSan for B13) result, demo/capture files + what they showed, and a list of facts you were unsure
about (from-memory spec facts). Do NOT edit files outside ROOT/lessons/PCIE/<your ID>/ (and your private /tmp/<ID>_* paths).
