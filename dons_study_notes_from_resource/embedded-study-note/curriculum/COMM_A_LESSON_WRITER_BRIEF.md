# Lesson writer brief — track COMM (통신 프로토콜 · 버스), **Advanced** level (COMM-A01–A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md → ROOT/curriculum/COMM_B_LESSON_WRITER_BRIEF.md (COMM base rules: output files, headings
verbatim and in order, svg rules, timing-diagram style, code rules, strict build + ASan/UBSan, "never invent output",
hedge spec numbers) → ROOT/curriculum/COMM_M_LESSON_WRITER_BRIEF.md (Medium overrides: Parts escalate, fixed-seed
xorshift32 tests, demo_*.c for sweeps, no malloc in firmware code, self-contained files, accuracy rules) → **this
file** (Advanced overrides + per-item lanes). Read all of them; this file wins on conflicts.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics / defense roles (Anduril, Rivian, Tesla, Apple, NVIDIA). COMM Advanced = senior/staff
interview + real bring-up layer: "USB 컨트롤 전송에서 wLength보다 짧게 답하면 호스트는 어떻게 끝을 아나?",
"SR에서 시퀀스 공간이 왜 2W 이상이어야 하나?", "PHY 링크가 안 올라온다 — MDIO로 뭘 읽나?", "UDP 체크섬 0의 의미는?",
"I3C ENTDAA에서 두 타깃이 같이 응답하면?", "NOR 페이지 경계 넘겨 쓰면?", "ISO-TP FC Wait/Overflow 처리는?",
"DMA 디스크립터 링에서 언제 clean, 언제 invalidate?", "하드웨어 없이 드라이버 회귀를 어떻게 잡나?",
"LA 캡처에서 baud를 자동으로 추정해봐", "센서 허브에서 버스가 hang 되면 전체가 어떻게 복구되나?".
Every lesson = one interview-style coding problem built as a deterministic C model/simulator with checked tests.

## Overrides for Advanced
- Output: ROOT/lessons/COMM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, + optional verify_*.py / data files). Front matter exactly:
  `---\nid: <ID>\ntime: <90분–130분>\nupdated: 2026-11-11\n---`. Title line `# <ID> <title exactly as in COMM.json>`.
- md **500–750 lines**, ≥3 svg figures (packet/bit layout or waveform + state machine + measured curve/table or
  data-structure diagram), **면접 Q&A ≥ 7** (`<details><summary>Q1. ...</summary>`), 추가 연습 3–6 ending with
  `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd. §3 `<!-- code: starter.c -->`,
  §6 `<!-- code: solution.c diff=starter.c -->`.
- Parts: A = core protocol/driver mechanic, B = the faults / corner cases / proof interviewers probe (every fault the
  task names is injected and its expected outcome asserted), C = firmware-realistic integration (driver API, ISR/DMA
  path, recovery from a timeout path, bring-up log a real engineer would read). Each Part has checked tests.
- Follow-ups probe *judgement*: why this design, true worst case, what breaks with integer widths / fixed RAM / another
  core / a different controller. Assume COMM-B01–B14 and COMM-M01–M15 are known — cross-reference by id instead of
  re-teaching. Read the 1–3 neighbour lessons named in your lane before writing so you build on them, not duplicate.
- Tests: exhaustive where cheap, otherwise fixed-seed xorshift32 random tests (≥ a few thousand cases) + named edge/
  fault cases. Each .c prints "N/M passed" and returns 1 on any failure. solution.c / practice.c < 2 s each at -O2,
  < 90 s under ASan/UBSan. Timing numbers never go into a CHECK. Sweeps/curves → demo_*.c, paste REAL output.
- solution.c = starter.c with ONLY the TODO bodies filled (diff highlights changed lines). Starter stubs compile
  warning-free under the strict flags, return wrong-but-safe values, and every simulator loop terminates with a stub
  (bounded loops / step caps) — a starter must FAIL (exit 1), never hang or crash.
- No malloc in "firmware" code; static or caller-provided buffers with explicit capacity; every function that writes
  a buffer takes its capacity and reports overflow. Self-contained: no #include across lessons; embed a compact model
  and say "(COMM-Mxx 모델 축약판)".
- **Bug-detection honesty**: before finishing, inject ≥ 2 plausible bugs into a PRIVATE copy of solution.c (in your
  scratch dir) and confirm the harness FAILs for each; report which. Don't leave mutants in the lesson dir.
- Accuracy: anything you can compute (checksums, CRC check values, bit counts, timings from formulas) must be printed by
  your code before it appears in the md. Spec facts you can't open (USB 2.0 ch.9 / CDC PSTN, IEEE 802.3 clause 22,
  RFC 791/768/1071, MIPI I3C Basic, JEDEC JESD216 / vendor NOR datasheets, ISO 15765-2) → state as you know them, mark
  "(스펙 확인 권장)" where unsure, and treat them in tests as YOUR model's definition. WebFetch/WebSearch are available
  via ToolSearch — if you confirm a fact online, cite the URL in 참고자료. Never invent measured output or a "known"
  byte stream from memory unless verified two independent ways.
- Host facts: Apple M6 Mac, macOS 27.2, Apple clang. Build needs
  `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` (27.2 SDK link broken). No tshark, no scapy,
  no libFuzzer runtime. `/usr/sbin/tcpdump -r file.pcap -nn -vv -X` works on Mac WITHOUT sudo (reading files). Lima VM
  `elinux` (Ubuntu aarch64, kernel 7.0, passwordless sudo, tcpdump at /usr/bin/tcpdump; Mac home mounted READ-ONLY in
  the VM at the same path → write into the VM's /tmp and copy outputs back via `limactl shell elinux -- cat ...`).
  Other writers run concurrently → host load is high; report ratios, not absolute speeds, if you time anything.

## Workspace
- Private scratch dir: `<SCRATCH>/<ID>/` (SCRATCH is given in your prompt) for binaries, mutants, temp data. Never write
  to /tmp root or a shared name (VM side: /tmp/<ID>_work/ only). Don't edit anything outside ROOT/lessons/COMM/<ID>/.
  Don't run build_site.py or git.

## Cross-references (written lessons you may cite — check the dir exists first)
- COMM-B01–B14, COMM-M01–M15 (M01–M03 I2C stretch/arb/recovery, M04–M09 CAN incl. M09 real SocketCAN, M10 UBX robust
  parser, M11 DMA circular RX + IDLE, M12 bus_ops driver layering, M13 async SPI queue, M14 stop-and-wait ARQ, M15 USB
  descriptors + enumeration). ARCH-M12 (DMA cache clean/invalidate), ARCH-M08/A04/A05 (coherency), HWLAB-B07 (LA capture
  formats CSV/VCD), HWLAB-M05/M06 (LA timing compliance, sigrok decoder), BM-B lessons, RTOS-B/M, CAM-M11 (capture
  buffer queue ownership), TEST-A05. Not yet written (cite as "다음 단계" only): TEST-M02 (fuzzing), AUTO-M02 (UDS),
  CAM-B05 (CSI-2), NET-B01 (pcap link harness), BM-M08 (DMA driver).
- The other 10 COMM-A items are being written IN PARALLEL right now. Stay in your lane.

## Lanes (stay in yours)

- **A01 USB control transfer + CDC-ACM**: read M15 (descriptors/enumeration — it owns descriptor PARSING and the host-
  side SETUP log; your lane is the DEVICE-side EP0 state machine). Model: EP0 max packet size (8 or 64 — parametrize),
  host simulator injects transactions: SETUP(8 B) → DATA stage IN or OUT packets → STATUS (opposite direction ZLP).
  States: IDLE → SETUP → DATA_IN / DATA_OUT → STATUS_IN / STATUS_OUT → IDLE; STALL for unsupported requests (protocol
  STALL clears on next SETUP); a new SETUP aborts any in-progress transfer. Handlers: GET_DESCRIPTOR (device, config
  with wTotalLength, string), SET_ADDRESS (address takes effect only AFTER the status stage completes — test that the
  status IN is still answered at address 0), SET_CONFIGURATION (0 = unconfigured), GET_CONFIGURATION, CDC
  SET_LINE_CODING (7 B OUT data: dwDTERate LE32, bCharFormat, bParityType, bDataBits), GET_LINE_CODING,
  SET_CONTROL_LINE_STATE (wValue bit0 DTR, bit1 RTS; no data stage). Data-stage length = min(wLength, actual); the
  ZLP rule: if the device returns fewer bytes than wLength AND the length is an exact multiple of EP0 MPS, it must end
  with a ZLP — test exhaustively over actual lengths 0..3·MPS × wLength choices; assert the host sees the transfer end
  correctly and no extra/missing ZLP. Part B faults: wLength=0 GET_DESCRIPTOR, host sends SETUP mid-data, unknown
  request → STALL then recovery, OUT data longer than buffer, host aborts early (IN token count < expected). Part C:
  a USB-serial "bring-up log" of a full CDC-ACM enumeration + `stty`-like line coding change; mention how a real stack
  (TinyUSB dcd/usbd, STM32 USB device library) splits EP0 handling (hedge). Mac: optionally show
  `ioreg -p IOUSB -l -w0` excerpts if a CDC device is present — otherwise say none was attached.

- **A02 Sliding window: GBN vs SR**: read M14 (stop-and-wait ARQ, its link sim). Tick-based link simulator: per-
  direction propagation delay (RTT parameter), loss rate p, optional reordering/jitter, bounded in-flight capacity;
  DATA and ACK both lossy. GBN: cumulative ACKs, single timer, resend whole window on timeout. SR: per-packet timers,
  receiver buffers out-of-order within window, individual ACKs. Sequence numbers k bits (modulo 2^k). Prove + test the
  sequence-space requirement: GBN needs W ≤ 2^k − 1, SR needs W ≤ 2^(k−1) (i.e. 2^k ≥ 2W) — construct the exact failure
  for SR with W = 2^(k−1)+1 (all ACKs lost → receiver accepts a retransmitted old packet as new → corrupted delivery,
  assert detected via payload check) and show it disappears at W = 2^(k−1). Tests: exactly-once in-order delivery to
  the app under random loss/reorder (fixed seeds), retransmission counts sane, all-loss → bounded failure. demo:
  goodput vs window size (1..32) for several loss rates (0, 1, 5, 10, 20%) and RTTs, GBN vs SR table + svg curve;
  compare with simple theory (W/RTT bound until bandwidth-delay product; GBN collapse factor at high loss) honestly.
  Part C: telemetry radio scenario (e.g. 57600 baud equivalent, 200 ms RTT, 5% loss) — choose W and k, RAM cost of SR
  receive buffers on an MCU. Mention TCP SACK / HDLC / LoRa-ish links only in 1–2 lines (hedge).

- **A03 Ethernet PHY + MDIO Clause 22**: MDIO frame bit-bang: preamble 32×1, ST=01, OP (10 read, 01 write), PHYAD 5,
  REGAD 5, TA (read: Z then 0 driven by PHY; write: 10), 16 data bits MSB first; MDC clock edges — PHY samples on
  MDC rising edge, read data valid after rising edge (state your model's timing; hedge exact setup/hold, 2.5 MHz max
  MDC). Mock PHY with registers: BMCR(0) (reset bit15 self-clearing after N ticks, AN enable bit12, restart AN bit9,
  speed/duplex bits13/8), BMSR(1) (link status bit2 LATCHING LOW — read twice to get current state!, AN complete bit5,
  ability bits), PHYID1/2 (2/3 → OUI/model/rev decode), ANAR(4), ANLPAR(5). Driver: probe all 32 addresses (absent
  PHY reads 0xFFFF on a pulled-up bus), soft reset with timeout, start AN, poll link-up with timeout, resolve
  speed/duplex = highest common ability of ANAR & ANLPAR (priority 100FD > 100HD > 10FD > 10HD — hedge 802.3 Annex
  28B), and report. Part B faults: PHY never finishes reset, AN never completes, link flaps (latch-low behaviour must
  be caught: link went down between polls → one read shows 0), wrong PHYAD, TA bit not driven (bus fault), partner
  advertises only 10HD, parallel detection fallback (hedge). Part C: Linux `phylib`/`mii-tool`/`ethtool` style status
  line; how Clause 45 differs (one paragraph, hedge). Exhaustive tests over ability combinations.

- **A04 Ethernet/IPv4/UDP parser + checksums**: C tool `pcapparse` reading classic pcap (24 B global header, magic
  0xa1b2c3d4 / swapped / nanosecond 0xa1b23c4d, linktype 1 = Ethernet; 16 B record headers; caplen vs len). Parse
  Ethernet II (dst/src/EtherType), 802.1Q VLAN tag (TPID 0x8100, PCP/DEI/VID; also QinQ 0x88a8 → parse or report
  — say which), IPv4 (IHL ≥ 5 + options, total length vs captured, header checksum RFC 1071 one's-complement verify,
  fragments: MF/offset → don't parse UDP of non-first fragments), UDP (length, pseudo-header checksum, checksum 0 =
  not computed for IPv4, computed 0 sent as 0xFFFF). Robust: every length checked against caplen, never OOB (ASan).
  Generate test pcaps WITHOUT trusting your own encoder alone: (1) your C writer, (2) an independent python3
  `verify_pcap.py` (stdlib struct only) that builds/validates the same packets, (3) a REAL capture: in the Lima VM,
  `sudo tcpdump -i lo -w /tmp/COMM-A04_work/lo.pcap udp port <p>` while sending a few UDP datagrams (e.g. python3 or
  `nc -u`), copy the pcap into the lesson dir (small!), and (4) compare your tool's decoded fields against
  `tcpdump -r file -nn -vv -e` output (Mac, no sudo) — paste both. Note: loopback captures may carry checksum-offload
  partial checksums (tcpdump says "bad udp cksum" / incorrect) — explain if it happens. Part B: corrupt checksums,
  truncated caplen, IHL<5, total length > caplen, VLAN-tagged, IP options, fragment, odd-length UDP payload (checksum
  padding), endianness of pcap. Part C: incremental checksum update RFC 1624 when rewriting a field (e.g. TTL decrement
  or NAT port) — implement and test against full recompute exhaustively for random packets. Mention hardware checksum
  offload in MACs (hedge).

- **A05 I3C basics: DAA + IBI**: model your own simplified I3C SDR bus at the transaction level PLUS bit-level where it
  matters (arbitration by open-drain wired-AND, like M02). ENTDAA (CCC 0x07 broadcast after 0x7E): each unaddressed
  target transmits 48-bit Provisioned ID + BCR + DCR (64 bits) MSB-first in open-drain; wired-AND → the target with
  the lowest PID (first 0 where others send 1) wins; controller assigns a 7-bit dynamic address + parity bit; losers
  retry in the next round; repeat until no ACK (NACK on 0x7E). Address selection: avoid addresses used by legacy I2C
  devices on the bus (from a static table), reserved addresses (0x7E, and the "single-bit-error" neighbours of 0x7E —
  hedge which set; make it your model's definition), and lower dynamic address = higher IBI priority. IBI: target pulls
  SDA low while bus idle; controller starts clocking, targets send their address + R/W=1 in open-drain, arbitration →
  lowest address wins, controller ACKs if IBI enabled (DISEC/ENEC hedge), optional MDB byte; loser retries. Legacy
  I2C devices: they must not be confused (I3C uses 50 ns spike filter avoidance — hedge; in your model: legacy devices
  only respond to their static address). Tests: exhaustive small cases (2–3 targets with crafted PIDs), random PIDs
  (thousands) → assigned addresses unique, avoid legacy table, order == PID order; IBI from multiple targets at once →
  priority by dynamic address; bit-level arbitration equivalence with M02 rules. Part B: two targets with identical PID
  (spec violation → both think they won → address collision, show detection: GETPID/ GETBCR read-back mismatch or
  duplicate-ACK; hedge), target joins late (Hot-Join 0x02 — model as request + SETDASA/ENTDAA rerun, hedge), SETDASA
  from static address. Part C: Linux i3c master driver flow (i3c_master_do_daa, hedge) and a bring-up log. CSI-2 is
  CAM-B05 — one-line mention only.

- **A06 SPI NOR flash driver**: mock NOR (sizes parametric, e.g. 1 MB, 256 B page, 4 KB sector): commands 0x9F JEDEC
  ID (manufacturer, type, capacity code — e.g. a W25Q80-like 0xEF 0x40 0x14; say values are your model's/"typical",
  hedge), 0x06 WREN (sets WEL), 0x04 WRDI, 0x05 RDSR (bit0 WIP, bit1 WEL, BP bits block-protect), 0x01 WRSR, 0x02 page
  program (only clears bits 1→0; address wraps within the page when crossing the page boundary — the classic bug), 0x20
  4 KB sector erase (→ 0xFF), 0xD8 64 KB block erase optional, 0x03 read (no wrap, continuous), busy time in ticks
  (program ~t_PP, erase ~t_SE: hedge datasheet typical/max; model as ticks), commands ignored while WIP=1 except RDSR,
  WEL auto-clears after program/erase completes, program without WREN ignored. CS semantics: command executes on CS
  rising edge (page program with wrong byte count still executes — model it). Driver: jedec probe, wait_ready with
  timeout, write(addr, buf, len) splitting at page boundaries, erase range (sector-aligned, reject unaligned), read,
  unprotect (clear BP bits via WRSR). Tests: random write/erase/read against a reference RAM model with NOR semantics
  (AND on program) — thousands of ops; the page-wrap bug reproduced by a naive driver and caught; erase-before-write
  requirement; protection blocks writes; busy-timeout fault; power-loss mid-program (partial bits) observed by read.
  Part C: a tiny log-append store on NOR (append records with header+CRC, scan on boot to find end, sector rotate) —
  keep it small; mention SFDP (JESD216) and QSPI/4-byte addressing as one paragraph (hedge).

- **A07 ISO-TP (ISO 15765-2) over CAN**: read M04/M05 only for context (frames are abstract 8-byte CAN payloads here;
  use classic CAN, optional padding 0xCC/0xAA — say which). PCI: SF (0x0N, len ≤ 7), FF (0x1L LL, 12-bit length up to
  4095; mention escape FF 0x10 0x00 + 32-bit length in the 2016 edition — hedge, implement or explicitly not), CF
  (0x2N, SN wraps 0..15, first CF SN=1), FC (0x3S: CTS=0, WAIT=1, OVFLW=2; BS; STmin: 0x00–0x7F ms, 0xF1–0xF9 = 100–
  900 µs, other values reserved → treat as 0x7F ms — hedge). Sender and receiver state machines driven by a tick
  scheduler; timers N_As/N_Bs/N_Cr (e.g. 1000 ms values as your model's — hedge). Tests: round trip of every length
  1..4095 (exhaustive) with random BS/STmin; SN wrap; STmin respected (gap between CFs ≥ STmin in ticks — assert from
  the trace); lost CF → receiver N_Cr timeout + error; wrong SN → abort; FC WAIT repeated (N_WFTmax limit) then CTS;
  FC OVFLW → sender aborts; unexpected PCI types ignored or abort per your table; new FF while receiving → restart
  (state the spec behaviour, hedge). Part C: two-node gateway over the M05-style shared bus with a second ID pair
  interleaved (functional vs physical addressing one paragraph); compute transfer time for 4095 B at 500 kbit/s for
  BS/STmin choices (use classic CAN frame bit counts from M04 exactly, state stuffing assumption) and show the table.
  UDS is AUTO — one-line mention only.

- **A08 Zero-copy DMA descriptor rings + cache coherency**: read ARCH-M12 (clean/invalidate rules) and M11. Model a DMA
  engine (Ethernet-MAC-like, e.g. STM32 ETH / Synopsys-ish but generic — say so) with TX and RX descriptor rings in
  "DMA-visible memory": descriptor {OWN bit, flags FS/LS, buffer ptr, length, status}; engine walks the ring while OWN=1
  (DMA-owned), writes status + clears OWN, raises IRQ, stops on OWN=0 (RX: "buffer unavailable"; TX: suspended until
  poll-demand). Simulate a **non-coherent cache**: CPU accesses go through a write-back cache model with 32-byte lines
  (Cortex-M7-like — hedge), DMA accesses go straight to memory; provide clean(addr,len)/invalidate(addr,len) and show
  every failure mode by injecting a missing maintenance step: (a) TX buffer not cleaned → DMA sends stale data;
  (b) TX descriptor not cleaned before setting OWN → engine sees old descriptor; (c) RX buffer not invalidated →
  CPU reads stale data; (d) invalidating a non-line-aligned buffer destroys a neighbour's dirty data (alignment +
  padding rule); (e) ordering: OWN must be written last (memory barrier — model as "descriptor write order" and note
  DMB/DSB on real HW, hedge). Driver code paths: TX submit, TX reclaim (free buffers whose OWN cleared), RX refill
  (zero-copy: hand the filled buffer up, put a fresh one in), ring full / empty conditions, head/tail indices, buffer
  recycling pool without malloc. Every clean/invalidate call in solution.c has a comment justifying it. Tests: random
  traffic for thousands of frames → byte-exact delivery and zero leaks with correct maintenance; each injected omission
  detected (assert corrupted output). Part C: compare with MPU non-cacheable region for descriptors (trade-off) and
  Linux dma_map_single/dma_sync_* direction semantics (hedge), CAM-M11 ownership analogy (cite).

- **A09 Bus transaction record/replay for driver regression**: read M12 (bus_ops shape). Embed a compact bus_ops +
  IMU-like sensor driver (or two: I2C temp sensor + SPI IMU — keep small). Recorder: a bus_ops wrapper that forwards
  to the real (mock) bus and appends records {seq, bus, op, addr/reg, len, data, result, timestamp} to a fixed-size log
  with a compact binary + human-readable text format (round-trip both, versioned header, CRC on the log). Replay: a
  bus_ops implementation that serves responses from the log and asserts the driver issues the SAME op sequence
  (mismatch → precise diff report: "seq 17: expected READ reg 0x28 len 6, got READ reg 0x28 len 2"). Matching modes:
  strict order vs relaxed (by key, tolerating reordered independent reads — define precisely). Mutation replay:
  inject NACK at seq k, delay (timeout), short read, bit-flip in data → driver must handle each (assert documented
  error codes / retry behaviour); sweep k over every record. Regression demo: introduce a "driver refactor" variant
  (e.g. burst read changed to per-register reads, or a missing reset delay) and show the replay test catches it with
  the diff report; show a benign refactor that relaxed mode accepts. Part C: how you'd capture real logs (LA decode
  from A10 / HWLAB-M06, Linux i2c tracepoints `i2c:i2c_write`/`i2c_read` via trace events — hedge), golden-file
  workflow in CI. Fuzzing is TEST-M02 — one-line mention.

- **A10 Logic analyzer decoder**: read HWLAB-B07 (capture formats) and HWLAB-M06 (sigrok decoder) — your lane is a
  standalone C decoder library + CLI over edge/sample data, not sigrok. Input: CSV (time_ns,ch0,ch1,...) and a minimal
  VCD subset ($timescale, $var wire 1 <id> <name>, #t, 0<id>/1<id>) — parse both into the same per-channel edge list.
  Decoders: UART (auto-baud: estimate bit time from minimum pulse width / GCD-style histogram of pulse widths; then
  standard 8N1/8E1 decode sampled at bit centres; framing error detection), SPI (mode 0–3 given, CS active low, MSB
  first, bits per word), I2C (START/STOP/repeated START, address+R/W, ACK/NACK, data). Generate test captures from
  your OWN simulator code embedded in the .c (compact B01/B04/B06-style encoders) with jitter, glitches and sample-rate
  quantization, and assert decoded == generated. Part B: auto-baud accuracy vs sample rate / oversampling ratio and
  clock error (±3%) — demo table; glitch shorter than a threshold filtered; I2C SDA change while SCL high mid-byte
  (= spurious START/STOP — report); truncated capture; channel swapped (detect?). Part C: decode a capture produced by
  sigrok-cli or a captured CSV IF available (sigrok-cli is not installed — say so; you may produce a VCD by hand-written
  generator and show the file excerpt); output format like a LA "protocol table" (time, type, data). Optionally an
  independent python3 verify script that decodes the same VCD (stdlib only) and agrees.

- **A11 Capstone: sensor hub comm stack + fault injection**: integrates, in ONE self-contained deterministic simulator
  (tick-based, cooperative scheduler, no threads): 2 I2C sensors (compact M01/M03 behaviours: stretching timeout + 9-
  clock bus recovery), SPI NOR log storage (compact A06: page program + sector erase + WIP polling, page-boundary split),
  UART telemetry with COBS framing (compact B12) + CRC-16 + seq/ACK/retransmit (compact M14). Architecture: per-bus
  driver layer → sensor service (periodic sampling, staleness) → logger → telemetry; one error-code taxonomy across
  layers (like M12); per-component error statistics struct + health state machine (OK / DEGRADED / FAULT) with
  recovery actions (bus recovery, sensor re-init with backoff, mark data stale, retransmit, drop-oldest when the log
  queue is full). Fault injector: scripted + random (fixed seed) — I2C SDA stuck low for N ticks, sensor NACK forever,
  clock-stretch beyond timeout, NOR busy-timeout, telemetry byte corruption / loss on the UART link, link down. Tests:
  per-fault scenario assertions (which counters increment, which state reached, recovery time bound in ticks), end-to-
  end invariant (every sample is either logged+sent exactly once, or accounted in a "dropped" counter — no silent
  loss), long random soak (thousands of ticks, several seeds) with invariants checked each tick. Part C: the "bring-up
  report" printed by the hub (error stats table) and a design-review discussion (watchdog, priorities, what to log in
  the field). Keep each embedded model compact (say "(COMM-Mxx 축약판)"); total solution.c may be large but must stay
  readable — split the TODOs so starter still makes sense (e.g. the integration/recovery logic is the TODO, the
  device models are given).

## Verify (you MUST run; private scratch dir)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o $S/st && $S/st   # builds, some FAIL, exit 1, no hang
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o $S/so && $S/so   # all PASS, exit 0, < 2 s
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o $S/pr && $S/pr   # all PASS, exit 0, < 2 s
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o $S/as && $S/as
    (same sanitizer run for practice.c)                                    # clean
    demo_*.c: build with -Wall -Wextra and RUN; paste real output. verify_*.py: run with python3 -I.
(build from inside the lesson dir if a .c reads data files by relative path; say so in the md.)
Self-check the md: front matter, title line, headings verbatim & in order (no extra `## ` headings outside code
fences), Part A/B/C under §1, ≥3 ```svg with viewBox width ≤ 760, no hard-coded colors (`fill="#..."`/`stroke="#..."`
forbidden; use currentColor + f-acc/f-ok/f-warn/f-mut classes), only monospace font-family if any, Q&A ≥ 7, line count
500–750, markers present, every `<!-- code: X -->` file exists. An independent checker re-runs all of this.

## When done
Reply (short) with: files written + md line count, PASS counts starter/solution/practice, sanitizer result, mutants
injected + caught, demo files + key numbers, and anything unsure (from-memory spec facts, hedges).
