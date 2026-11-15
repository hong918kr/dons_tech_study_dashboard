# Lesson writer brief — track COMM (통신 프로토콜 · 버스), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
COMM-B is the **wire-protocol** track: UART/SPI/I2C simulated at bit level on the Mac, then CRC and byte-stream
framing (length+CRC, SLIP, COBS, Modbus RTU, text CLI). C Basic/Medium/Advanced, DS B/M/A, ALGO-B, ARCH-B,
HWLAB-B/M, SIV-B/M/A lessons exist — assume he knows them and cross-reference by id ("(ALGO-B14 참고)")
instead of re-teaching.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/COMM.json — find your item (title/goal/task/tags). The lesson must do the item's
   `task` completely (every sub-function, every injected fault, every measurement it names). Skim the other COMM
   items (B and M) so you stay in your lane: the other 13 B items are being written in parallel right now —
   reference them by id, don't depend on their files. **Every .c file must be self-contained** (if you need the
   previous item's model, embed a compact version of it in your own file and say "(COMM-B02 모델 축약판)").
3. Reference lessons for depth/tone/harness: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed",
   return 1 on failure) and ROOT/lessons/ALGO/ALGO-B14/ (recent Basic lesson with a demo_detect.c experiment).
   Related existing lessons to cross-ref (read only the parts you need): HWLAB-B03 (I2C pull-up / rise time),
   HWLAB-M05 (LA timing compliance SPI/UART), HWLAB-M06 (sigrok-style decoders), CPP-B07 (RAII SPI chip-select),
   C-M10 (mini printf for UART), DS-B14 (trie command parser), ALGO-B14 (checksums), CPP-M13 (constexpr CRC LUT).
   Not yet written (cite by id only): ALGO-M01 (bitwise CRC theory), ALGO-M02 (table/slicing CRC),
   BM-B12 / BM-M04 (real polling / IRQ UART drivers), TEST-M02 (fuzzing parsers).

## Output
ROOT/lessons/COMM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(front matter `updated: 2026-11-11`, `time:` realistic for Basic, e.g. 40분/50분/60분).
- md title line: `# <ID> <title exactly as in COMM.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥5, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
- Size: ~400–600 lines of md.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text). This track lives on **timing diagrams**: draw real waveforms
  (idle level, start/stop, sample points, SCLK edges with "drive"/"sample" marks, SDA/SCL with START/STOP,
  byte layout of a frame, state machine of a parser). Waveforms must match what your code produces (e.g. the
  exact bits of 'A' = 0x41 in 8N1, LSB first). If you draw a measurement curve (B03 error sweep, B08 detection
  rate), use your REAL measured numbers and say so in a caption.
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in embedded interviews / bring-up. Tie to firmware reality: USART/SPI/I2C peripherals on
  STM32/nRF, ISR + ring buffer, DMA, logic analyzer captures, Don's RF-module AT-command and SSD-firmware UART
  console background where it fits.

## Interview flavour
- 2. Problem (English) reads like a real firmware coding/design-round prompt (signatures, constraints,
  examples with I/O — e.g. "encode 0x41 in 8N1 → bits 0,1,0,0,0,0,0,1,0,1").
- 면접 Q&A = real follow-ups: why LSB first / why idle high, what the scope shows, what breaks at 3% clock error,
  why CPHA matters, why I2C needs pull-ups, repeated START vs STOP+START, CRC vs checksum choice, how you
  resync a stream, ISR vs main-loop split, DMA, what you'd check first on a dead bus.

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # compiles, 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as   # clean
    (same ASan/UBSan run for practice.c)
- Use your own unique temp names (/tmp/<ID>_*) and, if you need scratch files, a PRIVATE dir /tmp/<ID>_work/ —
  13 other agents run in parallel and have clobbered shared helper files before.
- No malloc in the "firmware" code (static buffers, caller-provided buffers, explicit capacities). Bounds are
  part of the interface: every encoder/decoder takes an output capacity and reports overflow.
- Simulators are deterministic discrete-time models: a `tick`/`step` function, explicit line levels (0/1),
  wired-AND for open-drain buses, an event log you can print and compare. Keep the model small and readable —
  the point is the protocol, not the simulator framework.
- Tests must be real and deterministic: exhaustive where cheap (all 256 bytes × all configs), otherwise
  fixed-seed xorshift32 random tests with enough cases to mean something + named edge/fault cases. Every fault
  the task names must be injected and the expected flag/error asserted. solution.c and practice.c each < 2 s.
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure where possible)
  so a diff highlights just the changed lines. Header comment same in both. starter.c TODO stubs must compile
  warning-free (cast unused params to (void)) and return a wrong-but-safe value (no crashes, no UB, no hangs —
  a stub inside a simulator loop must still let the loop terminate).
- Long experiments (millions of injected errors, clock-deviation sweeps, overhead tables) go in demo_*.c in
  the lesson dir, not in solution.c. Run them, paste the REAL output into the md as tables. Never invent output.
- Standard check values: CRC "123456789" check values from the RevEng catalogue — compute them with your own
  code and only print a value you verified (CRC-8/SMBUS 0xF4, CRC-16/CCITT-FALSE (=IBM-3740) 0x29B1,
  CRC-16/MODBUS 0x4B37, CRC-16/XMODEM 0x31C3, CRC-32/ISO-HDLC 0xCBF43926, CRC-32/MPEG-2 0x0376E6E7 — re-verify).
  Spec numbers you can't check against the document (exact STM32 register bits, sensor datasheet values,
  Modbus timer rules) → label "대표값" / "데이터시트·RM 확인 권장" and keep them out of tests' ground truth unless
  they are your own mock's definition.

## Lanes (stay in yours; cross-ref the others)
- **B01** UART framing only (bit vector ↔ byte), configs data bits 7/8 (5–9 optional), parity N/E/O, stop 1/2.
  LSB first, idle high, start=0, stop=1. Exhaustive round trip all bytes × configs; decoder rejects bad
  start/stop/parity with a distinct error code. Show 'A' 0x41 8N1 waveform. Count frame length / efficiency
  (8N1 = 80%). No oversampling (that's B02).
- **B02** 16x oversampling receiver state machine fed one sample per tick: falling-edge start detect, start-bit
  confirmation at mid-bit (false start rejection for a short glitch), per-bit majority of 3 middle samples
  (say which samples; STM32 uses samples 8/9/10 — label 대표값), noise flag when the 3 disagree, framing error
  (stop=0), break (all-zero frame incl. stop), overrun (RDR not read before next char completes; keep the old
  or new byte — state the policy and what STM32 does, hedged). Embed a compact B01-style TX to generate streams.
- **B03** Baud math: divisor = round(fclk / (OVS·baud)) for OVS=16 and 8 (+ optional fractional BRR like
  STM32 USARTDIV with 4-bit fraction), actual baud, % error; table for common fclk (8/16/48/72/80 MHz,
  1.8432/14.7456 MHz) × bauds (9600…921600). Then the tolerance experiment: embed a compact B02 receiver, sweep
  TX/RX clock mismatch (both signs), random start phase, find the % where 10-bit (8N1) and 11-bit (8E1/8N2)
  frames start failing; derive theory (≈ (0.5 − sampling-window term) / bits-to-last-sample) and compare. Sweep
  in demo_sweep.c; solution.c tests the calculator + a few fixed mismatch points.
- **B04** SPI time-step simulation: master drives SCLK/MOSI/CS, slave drives MISO, both obey CPOL/CPHA
  ("shift on one edge, sample on the other"; CPHA=1 shifts on the first edge). MSB first default. Full-duplex
  exchange all 4 modes × random bytes; multi-byte with CS held; mode mismatch → reproduce the 1-bit shift
  (and show which mismatches are harmless vs. shift). 4-mode timing figure.
- **B05** Register-style SPI device above B04 (byte-level `spi_xfer(tx,rx,n)` with CS control is fine; embed
  minimal model). Mock IMU protocol: first byte = R/W bit (bit7=1 read) + 7-bit address, optional
  auto-increment bit or always-increment (state yours; compare ST LIS3DH bit6 MS / InvenSense style, hedged),
  dummy byte where MISO is junk, WHO_AM_I check, single/burst read/write. Reproduce the "CS released between
  address and data" bug (slave resets its state machine → reads wrong data), then fix. Cross-ref CPP-B07.
- **B06** I2C bus simulator: SDA = AND of all node drivers + pull-up (release=1), SCL likewise; bit-bang master
  and a slave that detects START (SDA↓ while SCL=1) / STOP (SDA↑ while SCL=1), samples data on SCL high, changes
  SDA only while SCL low. Address + R/W, ACK/NACK on 9th clock. Tests: address NACK (no device), data NACK,
  write of N bytes, the rule violation (master changes SDA while SCL high mid-byte → slave sees spurious
  START/STOP). Cross-ref HWLAB-B03 for pull-ups; clock stretching/arbitration are COMM-M (cite ids from json).
- **B07** Mock temperature sensor on a compact I2C model (byte-level transaction model is fine; embed a small
  bit-level core or event log so START / Sr / STOP / ACK are visible): register map (ID/WHO_AM_I, CONFIG,
  TEMP_MSB/LSB, pointer auto-increment), `i2c_write_read()` with repeated START, driver: ID check → config →
  continuous read and conversion to m°C (your own fixed-point scale; real parts like TMP117 / LM75 hedged).
  Print logs for "STOP then START" vs repeated START; show why it matters (another master can take the bus
  between; a mock option where the device resets its pointer on STOP → wrong register read).
- **B08** Detection-capability experiment (ALGO-B14 did sum/inet/Fletcher at a basic level — cite it; your lane
  is CRC vs checksum, Hamming distance, bursts). Compact own implementations of sum8, Fletcher-16, CRC-8 (0x07),
  CRC-16/CCITT-FALSE, CRC-32 (ISO-HDLC). Measure undetected rate for random 1–4 bit errors and bursts of
  length L = 1…40 at a fixed message size; exhaustive 2-bit and (small) 3-bit sweeps over short messages to
  find the empirical HD; compare with theory (CRC of degree r catches all bursts ≤ r; burst r+1 misses
  2^-(r-1); longer 2^-r; HD depends on polynomial and data length — Koopman's tables, hedged). Heavy runs in
  demo_detect.c (millions of trials); solution.c tests CRC check values + small exhaustive properties.
- **B09** Mock STM32 CRC peripheral: 32-bit DR writes (+ 8/16-bit DR writes on newer families), fixed poly
  0x04C11DB7 (older F1/F4 = fixed config, no reversal) vs. newer-family options INIT, REV_IN (none/byte/
  half-word/word), REV_OUT (RM details hedged). Software references: CRC-32/ISO-HDLC and CRC-32/MPEG-2.
  Tests find the configuration + byte order (little-endian word loads from a byte buffer!) + final XOR that
  make HW == SW, including a non-multiple-of-4 tail. Table/slicing = ALGO-M02 (cite).
- **B10** `[SOF][LEN][TYPE][PAYLOAD][CRC16]` — define every field precisely (CRC algorithm and which bytes it
  covers, LEN meaning, max LEN, byte order of CRC). Encoder with capacity check + byte-at-a-time parser state
  machine (ISR-friendly, static buffer). Tests: valid, truncated (followed by next frame), CRC error, LEN >
  MAX, start mid-stream, SOF byte inside payload/CRC. Part C: resync by re-scanning from the byte after a false
  SOF (bounded buffer) and a random-garbage + frames stream test where every valid frame is recovered. Stats
  counters (frames_ok, crc_err, len_err). No byte stuffing here (that's B11/B12).
- **B11** SLIP per RFC 1055 (END 0xC0, ESC 0xDB, ESC_END 0xDC, ESC_ESC 0xDD): encoder (leading END option),
  streaming decoder (byte at a time), consecutive ENDs → empty frames dropped, invalid escape policy (RFC's
  sample code vs. reject — choose, explain), frame too long. Worst-case overhead (2n+2) measured on all-0xC0
  and random data. Cross-ref B12.
- **B12** COBS (Cheshire & Baker): encode/decode, max overhead ⌈n/254⌉ (+1 delimiter) — state precisely,
  boundary cases 253/254/255/508 bytes, all-zero / no-zero inputs, random round trip lengths 0…1000,
  malformed input rejection (zero inside, code byte past end). Overhead table vs. SLIP on random data and
  worst cases (demo or in practice).
- **B13** Modbus RTU slave: timestamped byte stream (µs), frame split by ≥ t3.5 silence (11-bit char time;
  above 19200 baud fixed t3.5 = 1750 µs and t1.5 = 750 µs per Modbus over Serial Line spec — verify/hedge),
  CRC-16/MODBUS (low byte first on wire), slave address + broadcast 0 (no reply), FC 0x03 Read Holding
  Registers, FC 0x06 Write Single Register, exceptions 0x01/0x02/0x03 (FC | 0x80). Known request example
  `01 03 00 00 00 0A` + CRC — compute and verify before printing. Bytes helper = see:C (big-endian register
  values).
- **B14** Line editor (BS 0x08 / DEL 0x7F, CR / LF / CRLF as one end-of-line, max length → discard until EOL +
  error, non-printable filtering) + command table dispatcher (name, min/max args, handler), tokenizer, number
  parsing with range checks (decimal + 0x hex, overflow, sign, trailing junk — no atoi). Part C: AT-command
  form (`AT`, `AT+BAUD=115200`, `AT+BAUD?`, `OK`/`ERROR`) on the same core. Malicious input tests (overlong,
  embedded NUL, too many args, 99999999999, "12abc", only spaces). Cross-ref DS-B14 trie, C-M10.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = fault/edge cases (the injected errors), C = firmware-realistic use
  (driver API, ISR-fed parser, sensor read loop, console, gateway).
- 풀이 is detailed: protocol rule stated explicitly, step-by-step trace table on a small example (bit-by-bit or
  byte-by-byte with state), why each design choice, complexity/RAM where relevant.
- Accuracy over volume. Double-check every timing number, percent formula, CRC parameter, spec claim. Don (math
  major, ex-RF integrator who has scoped these buses) will notice.

## When done
Reply with: files written (paths), md line count, the PASS counts for starter/solution/practice, ASan/UBSan
result, demo files + what they showed (key numbers), and anything you were unsure about (from-memory spec facts).
Do NOT edit files outside ROOT/lessons/COMM/<your ID>/ (and your private /tmp/<ID>_* paths).
