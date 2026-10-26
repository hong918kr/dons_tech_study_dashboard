# V6 · PRBS Generator/Checker + CRC-32 for Link Validation

> **ID**: V6 · **Priority**: P1 · **Pattern**: LFSR bit-stream + self-sync checker (popcount of XOR) + bitwise CRC · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V6` / `./run.sh V6 sol`

## 1. Problem

You are validating a high-speed link (SerDes lane, PCIe, Ethernet, MIPI) on new silicon. The bring-up plan is: put the link in loopback, transmit a PRBS pattern, check the received pattern bit by bit, and count errors to get a bit-error rate (BER). Frame-level traffic is protected by CRC-32. Write the software side of this BERT.

```c
typedef struct {
    uint32_t state;    /* last `order` bits, bit 0 = newest */
    uint32_t mask;     /* (1 << order) - 1 */
    uint8_t  order;    /* 7 or 31 */
    uint8_t  tap;      /* 6 or 28 */
} Prbs;

int      prbs_init(Prbs *p, unsigned order, uint32_t seed);   /* 0 or -EINVAL */
unsigned prbs_next_bit(Prbs *p);
void     prbs_fill(Prbs *p, uint8_t *buf, size_t n);
uint64_t prbs_check(unsigned order, const uint8_t *rx, size_t n, uint64_t *bits_checked);
uint32_t crc32_ieee(const uint8_t *data, size_t n);
double   ber(uint64_t errors, uint64_t bits);
```

- **PRBS7** = x⁷ + x⁶ + 1 (`order 7, tap 6`), **PRBS31** = x³¹ + x²⁸ + 1 (`order 31, tap 28`). Fibonacci LFSR:
  `new = state[order-1] ^ state[tap-1]`, `state = ((state << 1) | new) & mask`, output `new`.
- `prbs_init`: seed is masked to `order` bits; a zero state or any other order → `-EINVAL`.
- `prbs_fill`: **MSB-first** — the first generated bit is bit 7 of `buf[0]`.
- `prbs_check`: self-synchronizing. Load the first `order` received bits into the state (assumed error-free), then free-run a local generator and count errors as `popcount(rx ^ expected)`. `*bits_checked = 8n - order` (0 if the buffer is not longer than `order` bits). Return the error count.
- `crc32_ieee`: IEEE 802.3 CRC-32 — reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. Table-free is fine.
- `ber`: `errors / bits`; `bits == 0` → `-1.0`.
- No malloc. `data` may be `NULL` when `n == 0`.

### Example

```
prbs_init(&p, 7, 0x7F); prbs_fill(&p, buf, 4)   -> 02 0C 28 F2
PRBS7 period from seed 0x7F                      -> 127 bits (64 ones)

ref = 256 PRBS7 bytes
prbs_check(7, ref, 256, &chk)                    -> 0 errors, chk = 2041
flip bit 100, then all 8 bits of byte 40         -> 9 errors
flip bit 3 (inside the 7 seed bits)              -> 1027 errors  (checker locked to the wrong phase)

crc32_ieee("123456789", 9)                       -> 0xCBF43926
crc32_ieee(NULL, 0)                              -> 0x00000000
crc32_ieee(msg || crc as 4 bytes LE, 13)         -> 0x2144DF1C   (CRC-32 residue)
ber(9, 2041)                                     -> 4.41e-3
```

### Constraints

- `n` up to a few MB per call; `uint64_t` counters.
- Byte order inside the stream is MSB-first; CRC is the reflected (LSB-first) variant as on the Ethernet wire.
- C11, no FPU requirement except in `ber()`.

### Clarifying questions to ask (first 3 minutes)

1. Which bit goes out first — MSB of each byte or LSB? (It changes every expected byte.)
2. Which PRBS definition — ITU-T O.150 (the one here) or an inverted variant? Which seed?
3. Can errors happen in the first `order` bits I use to synchronize? What should the checker do on loss of lock?
4. Should the error count be exact, or is the "each error counted 3 times" behavior of a feed-through checker acceptable?
5. CRC: which variant (init, reflection, final XOR) and in which byte order is it appended to the frame?

## 2. Your workspace

`./run.sh V6`로 바로 테스트하세요. 테스트 6묶음(24개 CHECK)이 있습니다. `flip()`·`tbit()`은 테스트용 헬퍼입니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — LFSR 한 스텝은 두 줄</summary>

`nb = ((s >> (order-1)) ^ (s >> (tap-1))) & 1; s = ((s << 1) | nb) & mask;` 이 상태 정의(bit 0 = 가장 최근 비트)에서는 **상태가 곧 최근 출력 7비트**입니다. 그래서 체커는 받은 비트 7개를 같은 방식으로 밀어 넣기만 하면 송신기와 같은 상태가 됩니다. order 31에서 `1u << 31`은 괜찮지만 `1u << 32`는 UB이니 mask는 `(1u << order) - 1u`로 만듭니다.

</details>

<details>
<summary>Hint 2 — 체커: "받은 비트로 시작, 내 비트로 계속"</summary>

seed를 채운 다음에는 **받은 비트를 LFSR에 넣지 말고** 로컬 생성기 출력만으로 계속 돌립니다. 그래야 에러 1개가 1개로 세어집니다. 받은 비트를 계속 넣는 방식(feed-through)은 에러 1개가 탭 수만큼(PRBS7은 3번) 다시 나타납니다.

</details>

<details>
<summary>Hint 3 — popcount와 CRC 한 비트</summary>

`x &= x - 1`은 가장 낮은 1비트를 지웁니다 → 반복 횟수가 popcount. CRC 한 비트: `crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)))`. `0u - 1 = 0xFFFFFFFF`라 분기 없이 다항식을 XOR합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — LFSR이 만드는 비트

PRBS7 생성기는 7칸 시프트 레지스터와 XOR 하나입니다. 매 클럭 s6과 s5를 XOR해서 새 비트를 만들고, 그 비트가 출력이 되면서 동시에 오른쪽 끝(s0)으로 들어갑니다. 0이 아닌 상태 127개를 모두 한 번씩 돌고 처음으로 돌아오므로 주기가 2⁷ − 1 = 127입니다(원시 다항식).

<div class="chart" data-src="chart1.svg" data-caption="PRBS7 LFSR, seed 0x7F. 7번째 스텝에서 처음 1이 나오고 상태는 0x01 → 첫 바이트 0x02. PRBS31은 칸 31개, 탭 s30·s27"></div>

자주 하는 혼동: x⁷ + x⁶ + 1의 "6"은 **1부터 센 탭 위치**라서 코드에서는 `state >> 5`(0부터 센 bit 5 = s5)입니다. 같은 이유로 PRBS31의 x²⁸은 `state >> 27`.

### 4.2 체커 방식 비교 — 왜 "locked" 체커인가

<div class="chart" data-src="chart2.svg" data-caption="같은 에러 주입에 대한 두 체커의 카운트(solution.c로 계산). locked는 정확, feed-through는 단일 에러를 3배로 셈. 대신 seed 구간 에러에는 locked가 동기를 잃음(1027 ≈ 2041의 50%)"></div>

- **locked(이 문제)**: seed 이후 로컬 생성기만 돌림 → 에러 개수가 정확. 대신 seed에 에러가 있으면 위상이 틀어져 약 50%가 에러가 됩니다. 실제 BERT는 "에러율이 임계값(예: 1/8) 넘으면 loss of lock → 다시 seed"를 합니다.
- **feed-through**: 받은 비트로 예측(`b[n-7] ^ b[n-6]`) → 7비트 지나면 저절로 다시 맞음(self-healing). 대신 에러 1개가 3번 세어지고(위치 e, e+6, e+7), 버스트에서는 서로 상쇄되어 8-bit 버스트가 8로 보이는 등 **정확한 BER이 아님**.

### 4.3 손으로 따라가기 (trace)

생성기, seed `0x7F`:

| 스텝 | state 전 | s6 ⊕ s5 | 출력 | state 후 |
|---|---|---|---|---|
| 1 | 7F | 1 ⊕ 1 | 0 | 7E |
| 2–6 | 7E … 60 | 1 ⊕ 1 | 0 | 7C, 78, 70, 60, 40 |
| 7 | 40 | 1 ⊕ 0 | **1** | 01 |
| 8 | 01 | 0 ⊕ 0 | 0 | 02 → 첫 바이트 `0000 0010` = **0x02** |
| 9–16 | 02 … 0C | | 0 0 0 0 1 1 0 0 | → 둘째 바이트 **0x0C** |

체커, `rx = ref`, 비트 100 반전:

| 단계 | 내용 | 값 |
|---|---|---|
| seed | rx 비트 0–6 = `0000001` | state = 0x01 (= 생성기 7스텝 후 상태와 같음) |
| byte 0 | 비트 0–6은 rx 그대로, 비트 7만 예측 | exp 0x02, rx 0x02 → 0 |
| byte 1 | 예측 8비트 | exp 0x0C, rx 0x0C → 0 |
| byte 12 | 비트 100 = 12×8 + 4 → 마스크 `0x80 >> 4 = 0x08` | exp 0x97, rx 0x9F, xor 0x08 → **1** |
| 끝 | 비교한 비트 2048 − 7 | `chk = 2041`, errors = 1, BER = 1/2041 = 4.9e-4 |

CRC-32, "123456789" 바이트별 결과(`crc32_ieee(m, k)`):

| k | 1 | 2 | 3 | 5 | 9 |
|---|---|---|---|---|---|
| CRC | 83DCEFB7 | 4F5344CD | 884863D2 | CBF53A1C | **CBF43926** |

첫 바이트 '1'(0x31): 레지스터 `0xFFFFFFFF ^ 0x31 = 0xFFFFFFCE` → 8번 시프트 후 `0x7C231048` → final XOR → `0x83DCEFB7`.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- 체커는 byte 단위로 expected를 만들고 `popcount8(exp ^ rx[b])`로 셉니다. seed 비트 자리에는 rx 비트를 그대로 넣어 XOR이 0이 되게 했습니다. 이 구조면 나중에 64비트 단위 + `__builtin_popcountll`로 바꾸기 쉽습니다.
- seed가 모두 0이면 `prbs_init`이 실패하므로 state를 0으로 두고 계속합니다. 그러면 expected가 전부 0이라 rx의 1이 전부 에러로 잡힙니다 — "링크가 0만 보낸다"를 에러로 보고하는 것이 맞습니다.
- `n * 8u`는 `size_t` 곱셈입니다. 32-bit MCU에서 512 MB 이상 버퍼면 넘치니 실제 코드에서는 비트 수를 `uint64_t`로 둡니다.
- CRC는 table-free 비트 루프(바이트당 8스텝)입니다. 256-entry 테이블이면 바이트당 1 lookup, slice-by-8이면 8바이트당 8 lookup으로 빨라집니다.
- residue 테스트: 메시지 뒤에 CRC를 little-endian으로 붙여 다시 CRC를 계산하면 항상 `0x2144DF1C`(final XOR 전 `0xDEBB20E3`)가 나옵니다. 수신 측 MAC이 FCS를 따로 떼지 않고 검사하는 방법입니다 → [V10](../V10_eth_frame_fcs/index.html).

### 4.5 복잡도

- 생성/체크: 비트당 O(1), 전체 O(8n). 메모리 O(1).
- CRC: 바이트당 8스텝 O(n). 테이블 버전은 1 KB ROM으로 약 8배 빠름.

---

## 5. 임베디드 관점으로 말할 것

- **실제로는 HW가 한다**: SerDes PHY에는 PRBS 생성기/체커와 에러 카운터 레지스터가 들어 있습니다. 검증 SW가 하는 일은 (1) 패턴·loopback 모드 설정, (2) 일정 시간 대기, (3) 카운터 읽기 + lock 상태 확인, (4) BER 계산과 리포트입니다. 이 문제의 SW 구현은 **HW 체커의 golden model**이자 DMA로 받은 버퍼를 검사하는 데 쓰입니다.
- **비트 순서 합의가 첫 번째 버그**: MSB-first/LSB-first, 8b/10b·64b/66b 인코더 앞/뒤 어디서 PRBS를 넣는지가 다르면 에러율이 정확히 ~50%로 나옵니다. "50%면 링크가 아니라 설정 문제"가 실무 감각입니다.
- **BER 신뢰도**: 에러 0개로 BER ≤ 1e-12를 95% 신뢰도로 말하려면 `N = −ln(0.05) / 1e-12 ≈ 3.0e12` 비트가 필요합니다. 10 Gb/s 레인에서 약 300초(5분), 25 Gb/s면 120초. 그래서 PVT 코너마다 전수로 돌리지 않고, 짧게 돌리며 eye margin(V7 shmoo)과 같이 봅니다 → [V7](../V7_shmoo_margin/index.html).
- **CRC는 프레임, PRBS는 비트**: CRC는 "이 프레임이 깨졌다"만 알려 주고 BER은 못 줍니다. 그래서 물리층은 PRBS, 상위 층(Ethernet FCS, PCIe LCRC, MIPI CSI-2 packet CRC)은 CRC 에러 카운터를 따로 봅니다 → [V9](../V9_csi2_packet_parse/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"체커가 맞는지 어떻게 아나?"** — 알려진 에러를 넣었을 때 정확히 그 개수가 나와야 합니다(1, 8, 9, 5). HW 쪽은 PHY의 error-insert 레지스터로 TX에 에러 1개를 넣고 카운터가 1 올라가는지 봅니다. CRC는 표준 check value `0xCBF43926`로 검증합니다.
  *"I prove the checker with known injected errors: one flipped bit must read back as exactly one error, and the CRC must hit the standard check value."*
- **"near-end vs far-end loopback?"** — near-end(PCS/PMA 내부 loopback)는 디지털 경로만, far-end는 패키지·PCB·커넥터·상대 PHY까지 포함합니다. 안쪽부터 바깥쪽으로 loopback을 넓혀 가며 에러가 처음 나타나는 구간을 좁힙니다.
  *"I walk the loopback outward — PCS, PMA, external cable, far end — and the first loopback that fails tells me which segment to debug."*
- **"FPGA 프로토와 실리콘에서 결과가 다르면?"** — FPGA의 transceiver는 다른 IP이므로 PRBS 결과는 실리콘에서만 의미가 있습니다. FPGA에서는 레지스터 설정 순서·카운터 읽기 SW 흐름을 검증하고, 실리콘에서 전압·온도별 BER을 측정합니다.
  *"On HAPS I validate the control flow and register sequence; the BER numbers only mean something on real silicon across PVT."*
- **"런타임과 통계?"** — 목표 BER과 신뢰도에서 필요한 비트 수를 먼저 계산하고(1e-12 @95% ≈ 3e12 비트), 레인 수만큼 병렬로 돌립니다. SLT에서는 짧은 BER 테스트 + CRC 카운터만 봅니다.
  *"Test time is set by statistics, not by habit: 3 times 10 to the 12 bits for 1e-12 at 95 percent confidence, run on all lanes in parallel."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why use PRBS for SerDes / PCIe / Ethernet loopback testing? | 결정적이라 수신 측이 같은 패턴을 혼자 만들어 비교할 수 있고(동기 비트만 있으면 됨), 스펙트럼이 넓어 랜덤 데이터처럼 ISI·지터를 자극합니다. 표준이 패턴을 정해 놔서(IEEE 802.3, PCIe compliance) 장비와 결과를 비교할 수 있습니다. |
| PRBS7 vs PRBS31? | PRBS7은 주기 127, 최장 연속 같은 비트 7개 → 짧고 스펙트럼이 거침, 빨리 lock. PRBS31은 주기 2³¹−1, 최장 run 31 → 저주파 성분이 많아 CDR·AC coupling(baseline wander)에 훨씬 가혹합니다. 링크 마진은 PRBS31로 확인합니다. |
| How many bits do you need to claim BER < 1e-12? | 에러 0개, 신뢰도 CL: N = −ln(1−CL)/BER. 95%면 3.0e12 비트(10 Gb/s에서 5분). 에러 1개를 허용하면 약 4.74e12 비트로 늘어납니다. |
| What does CRC-32 guarantee? | (1) **길이 32 이하의 모든 burst** 에러, (2) Ethernet 최대 프레임 길이에서 Hamming distance 4 → 모든 1·2·3비트 에러, (3) 그 밖의 무작위 에러는 약 2⁻³²의 확률로만 놓칩니다. 다항식에 (x+1) 인자가 없어서 "모든 홀수 개 에러" 보장은 없습니다. |
| Hardware CRC vs software? | MAC/DMA/CRC 엔진이 라인 속도로 계산합니다. SW CRC는 golden model, 부트로더 이미지 검사, HW 엔진 검증용입니다. SW도 테이블(1 KB) 또는 slice-by-8, ARMv8 `CRC32` 명령으로 빠르게 할 수 있습니다. |
| What happens on loss of lock? | 에러율이 임계값을 넘으면(예: 64비트 창에서 > 1/8) 다시 seed를 받습니다. 실제 카운터는 lock 상태와 에러 수를 따로 보고하므로, SW는 "lock 안 됨"을 "에러 많음"과 구별해서 리포트해야 합니다. |
| Near-end vs far-end loopback? | near-end: 내 칩 안(PCS 또는 PMA)에서 TX→RX를 돌림 → 디지털·아날로그 내부 경로. far-end(remote): 상대 장치가 받은 데이터를 다시 돌려줌 → 채널 전체. 바깥으로 넓혀 가며 고장 구간을 찾습니다. |
| Why does a feed-through checker count 3 errors per bit flip? | 받은 비트로 예측하므로 틀린 비트 하나가 자기 자리에서 1번, 그 비트를 탭으로 쓰는 6, 7비트 뒤 예측에서 2번 더 틀립니다. 탭 수 + 1 = 3. 그래서 BER을 3으로 나누거나 locked 방식을 씁니다. |

## 7. 자주 하는 실수

- 탭을 x의 지수 그대로 `>> 6`으로 씀 → 주기가 127이 아님 (테스트 1번)
- LSB-first로 바이트를 채움 → 첫 바이트가 0x40이 되어 0x02와 다름 (테스트 1번)
- 체커가 받은 비트를 계속 LFSR에 넣음 → 에러 1개가 3개로 셈 (테스트 3번)
- `bits_checked`에서 seed 비트를 빼지 않음 → 2048로 나와 BER이 틀림 (테스트 2번)
- 버퍼가 seed보다 짧을 때(3바이트 < 31비트) 범위 밖을 읽음 (테스트 6번)
- CRC final XOR 누락 → "123456789"에 대해 `0x340BC6D9`, init까지 0으로 두면 `0x2DFD2D88` (테스트 4번)
- `mask = (1u << 32) - 1` 같은 32비트 시프트 → UB (order는 최대 31로 제한)
