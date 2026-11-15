# Lesson writer brief — track COMM (통신 프로토콜 · 버스), **Medium** level (COMM-M01–M15)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/COMM_B_LESSON_WRITER_BRIEF.md (COMM track base rules:
output files, headings verbatim and in order, svg rules, timing-diagram style, code rules, strict build + ASan/UBSan,
"never invent output", hedge spec numbers) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium: deterministic PRNG
tests, demo_*.c for benchmarks/experiments, cross-target asm recipe, POSIX define) → **this file** (COMM-M overrides and
per-item lanes). Read all of them; this file wins on conflicts.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics roles (Anduril, Rivian, Tesla, Apple, NVIDIA). COMM Medium = the mid/senior interview
layer on buses: "슬레이브가 SCL 잡고 안 놓으면 어떻게 되나?", "SDA가 low로 붙었다 — 복구 절차는?", "CAN에서 낮은 ID가
왜 이기나? 표준 vs 확장 같은 base ID면?", "87.5% 샘플 포인트로 500k 설정해봐", "bus-off에서 어떻게 돌아오나?",
"DMA circular + IDLE에서 랩어라운드 처리", "한 SPI 버스를 여러 태스크가 쓰면?", "ACK 유실되면 중복은 어떻게?".
Every lesson = one interview-style coding problem built as a deterministic C model/simulator with checked tests
(COMM-M09 is the exception: real Linux SocketCAN in the Lima VM).

## Overrides for Medium
- Output: ROOT/lessons/COMM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Front matter `updated: 2026-11-11`, `time:` 60분–90분. Title line `# <ID> <title exactly as in COMM.json>`.
- md **450–700 lines**, ≥2 svg figures (≥3 encouraged: waveform/bit layout + state machine + measured curve/table),
  **면접 Q&A ≥ 6** (`<details><summary>Q1. ...</summary>`), 추가 연습 3–6 ending with `### 한 번에 검증` +
  `<!-- code: practice.c -->` + build cmd. §3 `<!-- code: starter.c -->`, §6 `<!-- code: solution.c diff=starter.c -->`.
- Parts escalate: A = core protocol mechanic, B = faults / edge cases interviewers probe (every fault the task names is
  injected and its expected outcome asserted), C = firmware-realistic use (driver API, ISR/DMA-fed parser, recovery
  routine called from a timeout path, gateway, log a bring-up engineer would read).
- Tests: exhaustive where cheap, otherwise fixed-seed xorshift32 random tests (≥ a few thousand cases) + named
  edge/fault cases. Each .c prints "N/M passed" and returns 1 on any failure. solution.c / practice.c < 2 s each.
  No timing numbers inside CHECKs. Heavy sweeps (recovery-rate vs corruption rate, throughput vs loss, tolerance
  sweeps, all-BRP enumerations for many clocks) go into demo_*.c; paste the REAL output into the md.
- solution.c = starter.c with ONLY the TODO bodies filled (diff highlights changed lines); starter stubs compile
  warning-free, return wrong-but-safe values, and every simulator loop still terminates with a stub (bounded loops /
  step caps) — a starter must FAIL tests, not hang.
- No malloc in "firmware" code; static or caller-provided buffers with explicit capacity; every function that writes a
  buffer takes its capacity and reports overflow.
- **Self-contained**: no #include across lessons. If you need a COMM-B model (I2C bit-level bus = B06, UART RX = B02,
  frame parser = B10, CRC = B08), embed a compact version and say "(COMM-B06 모델 축약판)".

## Cross-references
- COMM Basic B01–B14 ALL written (ROOT/lessons/COMM/COMM-Bxx/): B01 UART framing, B02 16x oversampling RX, B03 baud
  error/tolerance, B04 SPI CPOL/CPHA sim, B05 SPI register device + CS bug, B06 I2C bit-level bus sim (wired-AND,
  START/STOP, ACK), B07 I2C temp sensor + repeated START, B08 CRC vs checksum detection, B09 STM32 CRC peripheral,
  B10 [SOF][LEN][TYPE][PAYLOAD][CRC16] parser + resync, B11 SLIP, B12 COBS, B13 Modbus RTU, B14 CLI/AT line editor.
  Read the one(s) your item builds on (B06 for M01–M03, B10 for M10, B02/B10 for M11, B05/B07 for M12, B04/B05 for M13,
  B08/B10 for M14) and cite by id instead of re-teaching.
- Other written lessons you may cite: HWLAB-B03 (I2C pull-up/rise time), HWLAB-M05/M06 (LA timing, decoders),
  CPP-B07 (RAII chip-select), DS-B ring buffer lessons, ALGO-M01/M02 (CRC theory / table CRC), ARCH-M12 (DMA cache
  coherency clean/invalidate — relevant to M11), RTOS-B01–B13 (tasks, mutex, queues — relevant to M13), OS-B lessons,
  SIV-B/M/A (validation). Check the directory exists before citing (`ls ROOT/lessons/<TRACK>`).
- Not yet written (cite by id as "다음 단계" only): BM-B12/BM-M04 (real UART drivers), BM-M08 (DMA driver), TEST-M02 (fuzzing).
- The other 14 COMM-M items are being written IN PARALLEL right now. Stay in your lane (below); cross-ref by id.

## Accuracy rules for spec facts (Don will check)
- Anything you can verify by your own code (CRC check values, bit counts, stuffing counts, DLC tables, formula results)
  must be computed and printed by your code before it appears in the md.
- Facts from specs you cannot open (ISO 11898-1 corner rules, NXP UM10204 timing numbers, STM32 RM register field
  ranges, USB 2.0 ch.9 field semantics, CiA recommendations) → state them as you know them, mark
  "(ISO 11898-1 / UM10204 / RM 확인 권장)" where you are not certain, and in tests treat them as YOUR model's
  definition. You may use WebFetch/WebSearch to confirm a spec fact; cite the URL in 참고자료 if you do.
- Never invent measured output. Never claim a "known frame bitstream" from memory unless you verified it two independent
  ways (e.g. your encoder + an independently written python check, or a published example you fetched).

## Lanes (stay in yours)

### I2C (M01–M03): each embeds a compact COMM-B06-style bit-level model
Shared model rules so the three lessons agree: SDA and SCL are wired-AND of every node's driver (1 = release, pull-up
→ 1). Discrete time steps; master generates SCL as half-periods; data changes only while SCL is low, sampled while SCL
is high; START = SDA↓ while SCL=1, STOP = SDA↑ while SCL=1; 7-bit address + R/W, ACK on the 9th clock (0 = ACK).
- **M01 clock stretching**: slave holds SCL low for N steps after ACK (and optionally before data bit 0 of a read —
  state which). Master releases SCL, then READS SCL back and waits until it is really high before starting its high
  half-period (that is the whole point: a master that "assumes" high is shown to violate timing / mis-sample — demo
  it with a naive master). Timeout: max stretch (configurable, e.g. in steps ↔ µs; SMBus 25–35 ms t_TIMEOUT as
  대표값 hedged) → error code, bus left in a defined state (release lines; say what a real driver does next =
  COMM-M03). Tests: N = 0, small, just below/at/above timeout, stretch on every byte, stretch in a read vs write.
  Part C: driver-level `i2c_write_read()` returning distinct codes (NACK addr / NACK data / TIMEOUT) and a timing log.
  Mention STM32 I2C NOSTRETCH bit and that some masters (e.g. old Raspberry Pi BCM2835 I2C) mishandle stretching
  — hedge.
- **M02 multi-master arbitration**: two masters start (same step) to different addresses (and a case: same address,
  different data; and fully identical messages → no one loses, both "succeed"). Each master compares what it drove
  with what it reads on SDA at each SCL high; the one that sent 1 but read 0 loses, releases SDA immediately (keeps
  clocking to end of byte or stops — state your policy, per UM10204 the loser may continue generating clock until end
  of byte, hedge), reports ARB_LOST, and retries after the bus is free (STOP detected + bus free time) with bounded
  retries. Also SCL synchronization (wired-AND of two clocks: low period = longest low, high = shortest high) — model
  it at least in a figure + one test. Tests: which master wins for address pairs (exhaustive over pairs of 7-bit
  addresses or a big random set: winner = the one with the smaller first-differing bit → numerically smaller address
  byte), loser sees its target never got addressed, the retry completes, loss in data phase. Note: a slave addressed
  by the winner must not be confused by the loser (show log).
- **M03 bus recovery**: scenario = master resets mid-read while the slave is driving a 0 data bit (slave's state
  machine still thinks it is in the middle of a byte). After reset: SDA stuck low, SCL high. Recovery routine: check
  SDA; toggle SCL up to 9 pulses (as GPIO), after each pulse check whether SDA released; once SDA=1, generate STOP
  (SDA low while SCL high? → careful: drive SDA low while SCL low, raise SCL, then raise SDA = STOP); verify bus idle;
  [post-write note 11-11: the written lesson measured that this literal "toggle until SDA=1, then SCL low + STOP" fails
  in ~30% of read cases (the extra SCL fall clocks out the slave's next 0 bit); the lesson's solution folds a STOP
  attempt into every pulse like Linux i2c_generic_scl_recovery() and keeps the literal version as a counter-example]
  report failure if SDA still low after 9 (hardware fault / slave holding forever / SCL stuck low — distinct code).
  Explain why 9 is enough (worst case: slave is at bit 7 of a byte + ACK slot; also slave waiting to ACK) and why the
  slave may also be holding SDA for ACK. Exhaustive test: reset at EVERY bit position of a multi-byte read and write,
  with every data byte value pattern that matters (0x00, 0xFF, random) → recovery succeeds within ≤ 9 pulses, then a
  normal transaction succeeds. NXP UM10204 §3.1.16 "bus clear" (hedge section number), STM32 I2C peripheral needs
  pins switched to GPIO for this (hedge), Linux `i2c_generic_scl_recovery` mention (hedge).

### CAN (M04–M08): dominant = 0, recessive = 1 everywhere. Classic CAN 2.0A/B per ISO 11898-1 (hedge corner rules).
- Frame fields (classic data frame, standard ID): SOF(1, dominant) ID[10:0] RTR IDE r0 DLC[3:0] DATA(8·n) CRC[14:0]
  CRC-delim(1 rec) ACK-slot(1; transmitter sends rec, receivers overwrite dominant) ACK-delim(1 rec) EOF(7 rec)
  (+ IFS 3 rec). Extended: SOF ID-A[10:0] SRR(rec) IDE(rec) ID-B[17:0] RTR r1 r0 DLC DATA CRC ...
- CRC-15/CAN: poly 0x4599 (x^15+x^14+x^10+x^8+x^7+x^4+x^3+1), init 0, no reflection, no xorout, computed over the
  UNSTUFFED bits SOF..end of DATA. RevEng check value for "123456789" (bytes, MSB-first) = 0x059E — compute and verify.
- Bit stuffing: from SOF through the end of the CRC sequence; after 5 consecutive identical bits (stuff bits count
  toward the next run) insert the complement. CRC delim/ACK/EOF are fixed-form (not stuffed). 6 identical bits in the
  stuffed region = stuff error.
- **M04 frame encoder/decoder**: build unstuffed + stuffed bit vectors for standard and extended data (and remote)
  frames; decoder destuffs, parses, checks CRC, form errors (fixed-form fields), stuff errors. Verification without
  trusting memory: (1) CRC-15 check value 0x059E, (2) CRC over the frame bits followed by its own CRC → remainder 0,
  (3) exhaustive/random round trip, (4) an independently written python3 encoder (put it in the lesson dir as
  verify_frame.py, run it, show it agrees on a few frames — print the bitstrings), (5) worst-case stuffing count vs
  the Davis/Burns formula: classic standard frame length ≤ 8n + 47 + ⌊(34 + 8n − 1)/4⌋ bits (extended: 8n + 67 +
  ⌊(54 + 8n − 1)/4⌋) — search for the max stuff count by your own experiment (random + crafted 0x78/0x3C patterns)
  and compare honestly. Show a stuffed-frame waveform for e.g. ID 0x123 DLC 2 data 11 22 with stuff bits marked.
- **M05 arbitration sim**: K nodes start SOF in the same bit time; bus = AND of drivers; each transmitter during the
  arbitration field (ID [+SRR/IDE/ID-B] + RTR) compares sent vs read; sent rec & read dom → lost → becomes receiver
  and still receives the winner's frame (assert it received it correctly), retries at next bus idle (after EOF+IFS).
  Same-ID-both-data-but-different-data → no arbitration loss, bit error in data/later field (show detection, error
  handling itself = M07). Required comparisons: lower ID wins; data vs remote same ID → data wins (RTR dom); standard vs
  extended with same 11-bit base → standard wins (RTR(std, dom for data) vs SRR(rec) — and for a std REMOTE frame vs
  extended: RTR rec = SRR rec tie, then IDE dom (std) wins). Use M04-style stuffing in the sim (embed compact) — note
  stuff bits are identical for all nodes during arbitration as long as they sent identical bits so far (why it works).
  Part C: priority inversion / latency: a queue of messages from several nodes, record per-message wait; show the
  highest-ID message starving under load (link to M08's bus load / response-time analysis — Tindell, hedge).
- **M06 bit timing calculator**: NBT = (1 + TSEG1 + TSEG2) tq, tq = BRP / f_can, SP = (1 + TSEG1) / NBT.
  Constraints as your model's definition (bxCAN-like, hedge: BRP 1–1024, TSEG1 1–16, TSEG2 1–8, SJW 1–4, SJW ≤ TSEG2;
  NBT 8–25 tq "권장"). Enumerate all BRP/TSEG combos, compute bitrate error and SP error, pick best by a stated rule
  (exact bitrate first, then SP closest, then prefer more tq, then smallest BRP...). Tables for f_can = 8/16/36/42/
  45/48/80 MHz × 125k/250k/500k/1M at 87.5% (CANopen / CiA recommendation — hedge) and 80% (automotive/SAE J2284-ish
  — hedge). Oscillator tolerance (Bosch/ISO formula: df ≤ min(PS1,PS2) / (2·(13·NBT − PS2)) and df ≤ SJW/(20·NBT),
  with PS1 = TSEG1 − PROP — explain PROP/PS1 split and propagation delay; hedge) printed for chosen settings. Part C:
  emit an STM32 bxCAN BTR value (BRP−1, TS1−1, TS2−1, SJW−1 fields — RM 확인 권장) and Linux `ip link set can0 type can
  bitrate ... sample-point ...` equivalent. Compare against "standard" settings you can justify; hedge vendor tables.
- **M07 error confinement**: TEC/REC rules (ISO 11898-1, hedge exceptions): transmitter detects error → TEC += 8;
  receiver detects error → REC += 1; receiver detects dominant bit as first bit after sending its error flag → REC += 8;
  successful TX (ACK + EOF) → TEC −= 1 if > 0; successful RX → REC −= 1 if 1..127, if > 127 set to 119..127 (pick
  one value e.g. 127 and say so). States: error-active (TEC ≤ 127 && REC ≤ 127), error-passive (TEC > 127 || REC >
  127), bus-off (TEC > 255). Bus-off recovery: 128 occurrences of 11 consecutive recessive bits (+ host request in some
  controllers: STM32 ABOM, Linux restart-ms — hedge). Exceptions to implement (and test): error-passive transmitter
  ACK error with no dominant detected during passive error flag → TEC unchanged; stuff error during arbitration on a
  recessive stuff bit read dominant → not counted (hedge). Active vs passive error flag (6 dom vs 6 rec), suspend
  transmission (8 rec) for passive transmitter. Scenario tests: one broken node (always TX error) → goes passive at 16
  errors and bus-off at 32 (verify the numbers from your machine), interleaved successes delay it (compute), recovery
  timing in bit times at 500k (128 × 11 bits min = 1408 bits → µs), receiver-only node can never reach bus-off.
  Part C: a driver-level event log + "babbling idiot" scenario, and what Linux reports (`ip -details -statistics link
  show can0`: berr-counter tx/rx, state ERROR-ACTIVE/WARNING(≥96)/PASSIVE/BUS-OFF — add the 96 warning level as your
  model's definition, hedge).
- **M08 CAN FD**: DLC → length (0–8, 9:12, 10:16, 11:20, 12:24, 13:32, 14:48, 15:64) and length → smallest DLC (with
  padding). FD control field: FDF (rec, formerly EDL) res(dom) BRS ESI; no RTR in FD (RRS). CRC-17 for ≤16 data
  bytes, CRC-21 for > 16 (ISO FD adds stuff-count field + fixed stuff bits in CRC field — hedge exact counts).
  Transmission-time calculator: arbitration phase bits at nominal rate (SOF..BRS sample point, plus CRC delim..EOF
  back at nominal), data phase (after BRS ... CRC delimiter) at data rate; worst-case vs no-stuffing bounds; produce a
  table: Classic 8 B @500k vs FD 8/16/64 B @500k/2M and 1M/5M, efficiency = payload bits / total time, and payload
  throughput at 100% bus load. State every bit count you used (field by field) and where you are approximating (the
  exact BRS switch point / stuff count field). Tests: DLC table exhaustive, round trips, monotonicity, Classic numbers
  match M04-style exact counts for no-stuff case. Part C: choosing FD settings for a robot joint bus (e.g. 1 kHz × N
  motors × 16 B) — compute bus load, show which configs fit.
- **M09 SocketCAN (Linux, real)**: runs in the Lima VM `elinux` (Ubuntu, kernel 7.0, vcan + can-raw modules load, can-
  utils 2023.03 installed, passwordless sudo). The Mac home is mounted READ-ONLY in the VM at the same path, so: write
  files on the Mac in your lesson dir, and provide `check.sh` (+ optional `demo.sh`) that, run as
  `limactl shell elinux -- bash ROOT/lessons/COMM/COMM-M09/check.sh`, copies sources to /tmp/COMM-M09_work, sets up
  vcan0 idempotently (`sudo modprobe vcan; sudo ip link add dev vcan0 type vcan 2>/dev/null; sudo ip link set vcan0
  mtu 72; sudo ip link set up vcan0`), builds starter/solution/practice with `gcc -std=c11 -Wall -Wextra -Werror -O2`
  (+ an ASan/UBSan build of solution/practice if libasan works in the VM; if it doesn't, say so) and runs them:
  starter must FAIL (exit 1), solution/practice PASS. Put `#define _DEFAULT_SOURCE` (or _GNU_SOURCE) before includes.
  Programs: raw socket bind to vcan0, send/recv `struct can_frame` (classic) — self-test inside one process with two
  sockets (loopback is on by default on vcan; RECV_OWN_MSGS off → sender doesn't see its own), ID filters
  (`CAN_RAW_FILTER` with can_id/can_mask, EFF vs SFF via CAN_EFF_FLAG in the mask, `CAN_INV_FILTER`, multiple filters =
  OR, empty filter = receive nothing), error frames mask (`CAN_RAW_ERR_FILTER`, vcan won't generate errors — say so),
  CAN FD: `CAN_RAW_FD_FRAMES`, `struct canfd_frame` 64 B write with MTU 72 → ok; with mtu 16 → write fails (EINVAL) —
  test both (switch MTU in check.sh or a second vcan1). Use poll() with timeouts so nothing blocks forever. Then in
  demo.sh: candump vcan0 in background + your sender, cansend from shell → your receiver, `candump -l` / `cangen` maybe;
  paste REAL outputs. Explain the stack: socket → af_can → vcan driver; and how a real can0 differs (bitrate via ip link
  = M06, bus-off = M07, restart-ms). Mac-side: nothing to build — checker runs your check.sh. In the md, §9 "한 번에
  검증" shows the limactl command instead of a mac cc line (still include `<!-- code: practice.c -->`).

### Streams / drivers / reliability
- **M10 robust parser**: UBX framing exactly (sync 0xB5 0x62, class, id, len LE16, payload, CK_A CK_B = 8-bit Fletcher
  over class..payload; known: UBX-MON-VER poll `B5 62 0A 04 00 00 0E 34` — verify with your code). Optionally a
  MAVLink v1-like second format; if you add it, state its CRC (MCRF4XX/X.25 + CRC_EXTRA) precisely and verify a known
  frame two ways or drop it. Byte-at-a-time state machine (ISR-friendly) with: max length guard, inter-byte timeout
  (timestamped bytes; a gap > T mid-frame resets → counter), and **in-buffer rescan**: on checksum/len failure, resume
  searching for sync from byte after the false sync INSIDE the bytes already consumed (bounded buffer, no re-read from
  the link), so a real frame that started inside a garbage "frame" is not lost. Error counters per kind (sync_skip,
  bad_len, ck_fail, timeout, overflow, frames_ok). Measure: random stream of valid frames + injected corruption (bit
  flips, byte drops, byte insertions, burst garbage, fake sync bytes inside payload) → recovery rate (frames recovered /
  frames intact) for naive parser (discard-and-restart-after-frame) vs rescan parser, and false-accept rate (should be
  ~0 but Fletcher-16 isn't CRC — measure and explain). Table from demo_recovery.c. Cross-ref B10 (single-byte SOF
  resync) — your lane is 2-byte sync + timeout + rescan + measurement.
- **M11 DMA circular UART RX + IDLE**: model: DMA buffer of size N, NDTR counts down from N, wraps to N after TC
  (circular), write position = N − NDTR (NDTR==N means position 0). Events: HT (half transfer), TC (transfer complete),
  IDLE (line idle ≥ 1 frame after last byte). Handler logic (one function called from all three IRQs): read NDTR once,
  pos = N − NDTR, if pos != last → if pos > last process [last,pos) else process [last,N) then [0,pos); last = pos
  (pos == N → 0). Feed bytes to a compact frame parser (embed B10/M10-lite) or line assembler. Tests: random byte
  arrival bursts with random event interleavings (IDLE late, HT and TC both pending, event for 0 new bytes, exactly N
  bytes between events = ambiguous!), assert the parser receives exactly the byte stream. Show the overrun case: more
  than N bytes between two handler runs → silent data loss that NDTR can't reveal; detection strategies (sequence
  numbers / frame CRC / ORE flag hedge / counting HT+TC events) — implement one detector and test it. Part C: double
  processing hazard if handler runs from both IRQ and task without a critical section; cache maintenance on Cortex-M7
  (invalidate before reading — ARCH-M12 cite), STM32 HAL `HAL_UARTEx_ReceiveToIdle_DMA` / ReceptionType (hedge).
  Timing math: at 115200/1 Mbaud how long until the buffer wraps for N=256 → required worst-case handler latency.
- **M12 driver layering**: `struct bus_ops { int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
  int (*write_regs)(void *ctx, uint8_t reg, const uint8_t *buf, size_t n); }` (+ ctx) — exact shape yours, but keep
  read_regs/write_regs. Two mock buses: I2C (addr, register pointer, repeated START; faults: addr NACK, data NACK,
  arbitration lost, timeout) and SPI (bit7 = read, bit6 = auto-increment ST-style or always-increment — state it, CS,
  dummy byte; faults: no device (MISO floating 0xFF), timeout). ONE IMU driver source (WHO_AM_I check, reset, config
  ODR/range, read accel xyz 16-bit LE → mg) runs unchanged on both. Error-code translation table, layer by layer:
  HAL/bus-specific codes → common bus errors (BUS_ENODEV, BUS_EIO, BUS_ETIMEDOUT, BUS_EAGAIN(arb lost)...) → driver
  errors (IMU_ENOTFOUND, IMU_EBADID, IMU_EIO, IMU_ETIMEDOUT) → app; rule: lower layers never leak bus-specific codes
  upward, retry policy lives in ONE layer (state which). Tests: same scripted session produces identical driver output
  on both buses; each injected fault maps to the documented code; mid-burst failure doesn't leave partial data
  "valid". Part C: compare to Linux regmap / Zephyr sensor API / ST "platform-independent drivers" (stmdev_ctx_t
  write_reg/read_reg — hedge). Cross-ref B05, B07, CPP-B07.
- **M13 async SPI transaction queue**: descriptor { cs, tx, rx, len, cb(ctx, status), ctx, deadline/timeout, state }.
  Fixed-capacity queue (ring), `spi_submit()` returns QUEUE_FULL when saturated (no malloc), mock SPI peripheral that
  completes a transfer after k ticks and raises a "completion IRQ"; the ISR finishes the current descriptor (deassert
  CS, call cb) and starts the next one automatically. Cancel: queued → removed, cb(CANCELLED) exactly once; in-flight
  → state your policy (abort + CS release, or "too late") and test it. Timeout: tick-driven watchdog aborts a stuck
  transfer (mock injects "never completes"), cb(TIMEOUT), queue continues. Invariants checked every tick: at most one
  CS asserted, asserted CS == current descriptor's CS, every submitted descriptor gets exactly one callback, callbacks
  in submit order per priority (if you add 2 priority levels — e.g. high-prio queue for an IMU at 1 kHz vs a slow
  flash read — show the latency difference). Random stress: random submits/cancels/timeouts from "task" and from
  callbacks (resubmit inside cb!) with a fixed seed. Part C: reentrancy / critical sections (what you'd mask on real
  hardware), compare with Linux spi_async / Zephyr spi_transceive_cb (hedge). Cross-ref RTOS-B mutex/queue lessons.
- **M14 stop-and-wait ARQ**: link simulator (tick-based, deterministic PRNG) with loss, duplication, delay (fixed +
  random), optional corruption (CRC fail → drop) applied to both DATA and ACK. Protocol: sequence number in DATA, ACK
  carries the seq it acknowledges, sender timeout → retransmit with backoff (fixed / exponential with cap, + jitter),
  max retries → report FAILED to the app (error classification: TIMEOUT_EXHAUSTED, LINK_DOWN, CORRUPT_DROPPED counts).
  Receiver: deliver in order exactly once, re-ACK duplicates. Required tests: lost DATA, lost ACK (→ duplicate at
  receiver, must not be delivered twice), delayed ACK arriving after retransmit (spurious retransmission), duplication,
  all-loss → failure reported after exactly max_retries+1 transmissions. Show the classic bug: 1-bit alternating
  sequence + a link that can delay/reorder beyond one timeout → old ACK accepted for new DATA → silent loss; fix with a
  wider seq (and explain the bound: seq space must exceed packets in flight + max link lifetime/timeout). Measure
  (demo_arq.c): goodput and mean/99th-percentile delivery latency vs loss rate (0–50%) for fixed vs exponential backoff;
  compare goodput with theory (1−p)^2 ·... — derive yours and compare honestly. Part C: timeout choice from RTT
  (Jacobson/Karels SRTT+4·RTTVAR, RFC 6298 — hedge), why sliding window is next (cite but don't build).
- **M15 USB descriptors + enumeration**: parse device (18 B), configuration (9 B, wTotalLength), interface (9 B),
  endpoint (7 B: bEndpointAddress dir bit7, bmAttributes transfer type bits1:0 + iso sync/usage bits, wMaxPacketSize
  bits 10:0 + high-bandwidth bits 12:11, bInterval meaning per type/speed), IAD (8 B, type 0x0B), string descriptors
  (UTF-16LE → print ASCII/UTF-8), and skip unknown class-specific descriptors by bLength (HID 0x21, CS_INTERFACE 0x24).
  Example dumps: build your own spec-consistent byte arrays for (1) a CDC-ACM serial device with IAD (control interface
  + interrupt IN EP, data interface + bulk IN/OUT), (2) a HID mouse (interrupt IN), (3) a UAC-like iso endpoint; state
  they are constructed examples. Try `ioreg -p IOUSB -l -w0` / `system_profiler SPUSBDataType` on this Mac to show a
  real device's VID/PID/bcdUSB/speed fields (raw descriptors are generally not shown by these tools — say so; on Linux
  `lsusb -v` shows them). Robust parser (Part B): bLength 0 (infinite-loop trap!), bLength > remaining, wTotalLength >
  buffer or < 9, descriptor type mismatch, bNumInterfaces/bNumEndpoints disagree with what's present, strings with odd
  length — each → distinct error, never read out of bounds (ASan proves it), fuzz with fixed-seed mutations of the
  example dumps. Part C: enumeration flow as a decoded log of SETUP packets (bmRequestType/bRequest/wValue/wIndex/
  wLength): reset → GET_DESCRIPTOR(device, 64 or 8) → reset (Windows-style, hedge) → SET_ADDRESS → GET_DESCRIPTOR
  (device, 18) → GET_DESCRIPTOR(config, 9) → GET_DESCRIPTOR(config, wTotalLength) → strings (LANGID 0x0409) →
  SET_CONFIGURATION; implement a decoder for 8-byte setup packets and a mock device that answers them from the
  descriptor arrays (respecting wLength truncation). Table of endpoint types: control / bulk / interrupt / iso — use,
  guaranteed bandwidth/latency, retries, max packet size per speed (FS/HS values — hedge).

## Verify (you MUST run; own temp names /tmp/<ID>_*, private scratch dir /tmp/<ID>_work/)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # builds, some FAIL, exit 1, no hang
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0, < 2 s
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0, < 2 s
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as
    (same sanitizer run for practice.c)                                                  # clean
    demo_*.c: build with -Wall -Wextra (no -Werror needed) and RUN; paste real output.
(COMM-M09: run check.sh in the VM as described in its lane instead.)
Self-check the md: headings verbatim & in order (no extra `## ` headings outside code fences), Part A/B/C under §1,
≥2 ```svg with viewBox width ≤ 760 and no hard-coded colors (`fill="#..."` forbidden; use currentColor + f-acc/f-ok/
f-warn/f-mut classes), Q&A ≥ 6, markers present, every `<!-- code: X -->` file exists. An independent checker will
re-run all of this.

## When done
Reply (short) with: files written + md line count, PASS counts starter/solution/practice, sanitizer result, demo files
+ the key numbers they showed, and anything you were unsure about (from-memory spec facts, hedges). Do NOT edit files
outside ROOT/lessons/COMM/<your ID>/ (and your private /tmp/<ID>_* paths).
