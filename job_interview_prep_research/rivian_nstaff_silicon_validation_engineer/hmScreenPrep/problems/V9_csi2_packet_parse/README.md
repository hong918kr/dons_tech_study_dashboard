# V9 · MIPI CSI-2 Receiver Stream Checker

> **ID**: V9 · **Priority**: P1 · **Pattern**: packet parser + Hamming ECC + per-VC state machine · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V9` / `./run.sh V9 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: 카메라 SoC의 CSI-2 receiver가 캡처한 바이트 스트림을 받아서 패킷 헤더 **ECC**와 payload **CRC**를 검사하고, VC별로 frame/line 구조가 맞는지 세는 **스트림 checker**를 짭니다.

**상황**
- 카메라 SoC의 **CSI-2 receiver**(CSI-2 v1.x, D-PHY 위)를 validation 중입니다. 자동 테스트가 캡처할 때마다 이 checker를 호출합니다.
- receiver의 debug capture가 캡처 한 번 분량의 **merged byte stream**을 줍니다.
  - lane merging은 이미 끝났고 LP/SoT/EoT도 제거됨 → 버퍼에 패킷이 **빈틈없이 연달아** 들어 있음
- 용어
  - **VC**(virtual channel, 0–3): 한 링크에 여러 스트림을 섞어 보내는 채널 번호
  - **DT**(data type): 패킷 종류. **WC**(word count): long packet의 payload 바이트 수
  - **short packet**: 헤더 4바이트만 있음. **long packet**: 헤더 + payload + CRC
  - **ECC**: 헤더 24비트에 대한 6-bit Hamming 코드. 1비트 오류는 고칠 수 있고, 그보다 많으면 고칠 수 없음
- 테스트 파일에 **TPG**(test pattern generator: `tpg_short`, `tpg_long`, `tpg_frame`)가 들어 있습니다. ECC/CRC가 올바른 스트림을 만드는 독립적인 CSI-2 TX model이며, **수정하지 말고 사용**합니다.

**패킷 구조**

| Byte | 내용 |
|---|---|
| 0 | DI = `VC[7:6] \| DT[5:0]` |
| 1 | WC[7:0] — 16-bit WC, **little endian**. short packet이면 이 16비트가 data 필드 |
| 2 | WC[15:8] |
| 3 | ECC — `D[23:0] = byte0 \| byte1<<8 \| byte2<<16`에 대한 `P5..P0`, bit 7:6은 0 |

- `DT 0x00–0x0F` = short packet: `0x00` FS(frame start), `0x01` FE(frame end, data = frame 번호), `0x02` LS(line start), `0x03` LE(line end, data = line 번호)
- `DT >= 0x10` = long packet: 헤더 + `WC`바이트 payload + 16-bit CRC(LSB 먼저). 여기서 쓰는 DT: `0x1E` YUV422 8-bit(16 bpp), `0x2A` RAW8, `0x2B` RAW10, `0x2C` RAW12
- ECC 각 패리티 비트 `P0..P5`는 §1 표에 나열된 data 비트들의 XOR
- CRC-16은 **payload에만** 계산: 다항식 `x^16 + x^12 + x^5 + 1`, init `0xFFFF`, LSB 먼저 처리(reflected `0x8408`), final XOR 없음

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `csi2_ecc(d24)` | 24-bit 헤더 데이터 → 6-bit ECC | §1의 패리티 식 그대로 |
| `csi2_header_decode(raw, h)` | 4바이트 헤더 → `Csi2Hdr{vc, dt, wc}` | 반환: `HDR_OK`(0), `HDR_CORRECTED`(1, 고침), `HDR_UNCORRECTABLE`(-1)<br>short packet이면 `wc`에 data 필드가 들어감<br>ECC bit 7:6은 무시 |
| `csi2_crc16(p, n)` | payload CRC-16 | 위 CRC 정의 그대로 |
| `csi2_expected_wc(dt, width)` | 해당 DT로 한 줄(`width` 픽셀)의 바이트 수 | 모르는 DT이거나 바이트로 나누어 떨어지지 않으면 `0` |
| `csi2_check_stream(buf, len, cfg, r)` | 스트림 전체 검사, `Csi2Report` 채우기 | 아래 규칙. 정상 종료 `0`, sync 잃으면 `-1` |

- `cfg[vc]`(`Csi2VcCfg`)는 VC마다 기대하는 `dt`, `width`, `height`. `dt == 0`이면 그 VC는 안 쓰는 VC.
- `Csi2Report`에는 VC별 통계(`Csi2VcStats`: `frames_ok`, `frames_bad`, `lines_ok`, `crc_err`, `wc_err`, `dt_err`, `no_fs_pkts`, `missing_fe`, `line_count_err`, `frame_num_err`)와 전체 통계(`packets`, `ecc_corrected`, `ecc_uncorrectable`, `unexpected_vc`, `stop_off`)가 들어감.

**`csi2_check_stream` 규칙 (VC마다 따로, 다른 VC와 독립)**
1. **frame 구조**: `FS` → long packet `height`개 → `FE`
2. **good line**: DT가 `cfg.dt`, WC가 `csi2_expected_wc(cfg.dt, cfg.width)`, CRC 일치 — 셋 다 맞아야 함
3. **frame ok**: FE가 왔을 때 good line이 정확히 `height`개, FE 번호 = FS 번호, frame 안에서 실패한 게 없음
4. **FS가 또 옴**(frame이 열린 상태): `missing_fe`, 이전 frame은 bad로 닫고 새 frame 시작
5. **frame 없이 long packet이나 FE가 옴**: `no_fs_pkts`
6. **frame 번호**: 새 FS 번호와 이전 FS 번호가 둘 다 0이 아니면 새 번호는 `prev + 1`이어야 함(`0xFFFF` 다음은 1). 아니면 `frame_num_err` (frame이 빠졌다는 뜻이고, 현재 frame 자체는 bad로 안 침)
7. **payload 건너뛰기**: WC가 틀렸어도 **항상 헤더의 WC**로 건너뜀
8. **sync 잃음**: 고칠 수 없는 헤더, 또는 패킷이 `len`을 넘어감 → 멈추고 `-1`, `stop_off` = 그 패킷 시작 위치. 아니면 `0`, `stop_off = len`. 멈춘 시점에 열려 있는 frame은 모두 `missing_fe` + `frames_bad`
9. LS/LE와 기타 short packet은 받아들이되 이번 버전에선 무시
10. `cfg.dt == 0`인 VC로 온 패킷 → `unexpected_vc`

**예시를 말로 풀면**
- VC0 설정: RAW10, width 8, height 3. RAW10은 픽셀당 10비트라 한 줄 WC = 8×10/8 = **10바이트**.
- 스트림: `FS(1) L L L FE(1)  FS(2) L L L FE(2)` — 패킷 10개.
  - frame 하나 = FS 4 + 줄 3개 × (헤더 4 + payload 10 + CRC 2) + FE 4 = 56바이트, 두 frame이면 112바이트
- 바이트로 보면
  - FS(1) 헤더 `00 01 00 1A`: VC0, DT `0x00`(FS), data = 1, ECC `0x1A`
  - line 헤더 `2B 0A 00 2E`: VC0, DT `0x2B`(RAW10), WC = 10, ECC `0x2E`
  - line payload 10바이트의 CRC = `0x23DC` → LSB 먼저라 `DC 23`으로 붙음
- 그대로 검사하면 → 반환 `0`, `stop_off 112`, `frames_ok 2`, `lines_ok 6`, `ecc_corrected 0`.
- 두 번째 line 헤더의 WC bit 3을 뒤집음(`2B 0A` → `2B 02`): byte1의 bit 3은 `D11`. syndrome `0x25`가 D11 열과 같으므로 1비트 오류 → 고쳐짐. `ecc_corrected 1`, `frames_ok`는 여전히 2.
- offset 60의 헤더(두 번째 frame의 첫 line 헤더)에서 `D0`와 `D20`을 같이 뒤집음: syndrome `0x18`(짝수 weight) → 2비트 오류라 고칠 수 없음. 그 자리에서 멈춰 반환 `-1`, `stop_off 60`. 첫 frame만 ok(`frames_ok 1`), 열려 있던 두 번째 frame은 `missing_fe 1`.

**지켜야 할 조건**
- 테스트에서 `len <= 64 KiB`, VC 최대 4개. 헤더 ECC bit 7:6은 무시.
- `malloc` 금지. `buf[len-1]` 너머는 절대 읽지 않음.
- 정답 확인용 값: `csi2_crc16("123456789") = 0x6F91`, `csi2_expected_wc(RAW10, 1920) = 2400`.

## 1. Problem

You are validating the CSI-2 receiver (CSI-2 v1.x over D-PHY) of a camera SoC. The receiver's debug capture gives you the **merged byte stream** of one capture window: lane merging is done and LP/SoT/EoT are stripped, so packets sit back to back in a buffer. Write the checker that the automated test calls after every capture.

**Packet format.** Every packet starts with a 4-byte header:

| Byte | Field | Notes |
|---|---|---|
| 0 | DI = `VC[7:6] \| DT[5:0]` | virtual channel 0–3, data type |
| 1 | WC[7:0] | 16-bit word count, **little endian**. For a short packet this is the 16-bit data field |
| 2 | WC[15:8] | |
| 3 | ECC | 6-bit Hamming code `P5..P0` over `D[23:0] = byte0 \| byte1<<8 \| byte2<<16`; bits 7:6 are 0 |

- `DT 0x00–0x0F` = **short packet** (header only): `0x00` FS, `0x01` FE (data = frame number), `0x02` LS, `0x03` LE (data = line number).
- `DT >= 0x10` = **long packet**: header + `WC` payload bytes + 16-bit CRC (LSB first). Data types used here: `0x1E` YUV422 8-bit (16 bpp), `0x2A` RAW8, `0x2B` RAW10, `0x2C` RAW12.
- **ECC** parity equations (XOR of the listed data bits):

```
P0 = D0 D1 D2 D4 D5 D7 D10 D11 D13 D16 D20 D21 D22 D23
P1 = D0 D1 D3 D4 D6 D8 D10 D12 D14 D17 D20 D21 D22 D23
P2 = D0 D2 D3 D5 D6 D9 D11 D12 D15 D18 D20 D21 D22
P3 = D1 D2 D3 D7 D8 D9 D13 D14 D15 D19 D20 D21 D23
P4 = D4 D5 D6 D7 D8 D9 D16 D17 D18 D19 D20 D22 D23
P5 = D10 D11 D12 D13 D14 D15 D16 D17 D18 D19 D21 D22 D23
```

- **CRC-16** over the payload only: polynomial `x^16 + x^12 + x^5 + 1`, init `0xFFFF`, bits processed LSB first (reflected form `0x8408`), no final XOR.

```c
typedef struct { uint8_t vc, dt; uint16_t wc; } Csi2Hdr;           /* wc = data field for short pkts */
enum { HDR_OK = 0, HDR_CORRECTED = 1, HDR_UNCORRECTABLE = -1 };
typedef struct { uint8_t dt; uint16_t width, height; } Csi2VcCfg;   /* dt == 0: VC not used */
typedef struct { uint32_t frames_ok, frames_bad, lines_ok, crc_err, wc_err, dt_err,
                 no_fs_pkts, missing_fe, line_count_err, frame_num_err; /* + state */ } Csi2VcStats;
typedef struct { Csi2VcStats vc[4]; uint32_t packets, ecc_corrected, ecc_uncorrectable,
                 unexpected_vc; size_t stop_off; } Csi2Report;

uint8_t  csi2_ecc(uint32_t d24);
int      csi2_header_decode(const uint8_t raw[4], Csi2Hdr *h);
uint16_t csi2_crc16(const uint8_t *p, size_t n);
uint16_t csi2_expected_wc(uint8_t dt, uint16_t width);      /* 0 = unknown DT or not byte aligned */
int      csi2_check_stream(const uint8_t *buf, size_t len,
                           const Csi2VcCfg cfg[4], Csi2Report *r);
```

Rules for `csi2_check_stream` (per VC, independent of the other VCs):

- A frame is `FS` → `height` long packets → `FE`. A long packet is a good line only if its DT is `cfg.dt`, its WC is `csi2_expected_wc(cfg.dt, cfg.width)` and its CRC matches. A frame is **ok** only if FE arrives with exactly `height` good lines, the FE number equals the FS number and nothing failed inside it.
- `FS` while a frame is open → `missing_fe`, close the old frame as bad, start the new one. A long packet or FE with no open frame → `no_fs_pkts`.
- Frame numbers: when both the new and the previous FS number are non-zero, the new one must be `prev + 1` (`0xFFFF` wraps to 1), else `frame_num_err` (a dropped frame; the current frame itself is not marked bad).
- Always use the header's WC to skip the payload, even when the WC is wrong.
- An uncorrectable header or a packet that runs past `len` means we lost sync: stop, return `-1`, `stop_off` = start of that packet. Otherwise return `0`, `stop_off = len`. At the stop point every open frame counts as `missing_fe` + `frames_bad`.
- LS/LE and other short packets are accepted and ignored in this version. A packet on a VC with `cfg.dt == 0` → `unexpected_vc`.
- No malloc. Never read past `buf[len-1]`.

The test file includes a **test pattern generator** (`tpg_short`, `tpg_long`, `tpg_frame`): an independent CSI-2 TX model that builds streams with correct ECC and CRC. Use it, do not edit it.

### Example

```
VC0: RAW10, width 8 (WC = 8*10/8 = 10), height 3
stream = FS(1) L L L FE(1)  FS(2) L L L FE(2)     56 + 56 = 112 bytes, 10 packets

FS(1) header  = 00 01 00 1A          (ECC 0x1A)
line header   = 2B 0A 00 2E          (VC0 RAW10 WC=10, ECC 0x2E)
line payload  = 10 17 1E 25 2C 33 3A 41 48 4F   CRC = 0x23DC -> DC 23

check -> return 0, stop_off 112, frames_ok 2, lines_ok 6, ecc_corrected 0

flip WC bit 3 of the 2nd line header (2B 0A -> 2B 02): syndrome 0x25 = column of D11
      -> corrected, ecc_corrected 1, frames_ok still 2
flip D0 and D20 of the header at offset 60: syndrome 0x18 (even weight)
      -> uncorrectable, return -1, stop_off 60, frames_ok 1, missing_fe 1
```

### Constraints

- `len <= 64 KiB` in tests, up to 4 VCs. Header ECC bits 7:6 are ignored.
- Known answers: `csi2_crc16("123456789") = 0x6F91`; `csi2_expected_wc(RAW10, 1920) = 2400`.

### Clarifying questions to ask (first 3 minutes)

1. Is this CSI-2 v1.x (24-bit ECC, 2-bit VC) or v2.0+ (VC extension bits, ECC over 26 bits)?
2. Is the buffer already lane-merged, with LP/SoT/EoT removed? Is the CRC included in WC? (No: WC is payload only.)
3. On an uncorrectable header, do we try to resync (scan for the next valid header) or stop?
4. Are frame numbers used (non-zero) on this sensor? Do I check line numbers in LS/LE?
5. Should a CRC-errored line still count toward the line count? (Here: no, only good lines count.)
6. Which statistics does the test framework want, and per VC or global?

## 2. Your workspace

`./run.sh V9`로 바로 테스트하세요. 테스트 11묶음이 있고, 아래쪽 TPG(패턴 생성기)는 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — ECC는 선형 코드: 비트 하나의 ECC가 곧 H 행렬의 열</summary>

`csi2_ecc()`는 각 Pi에 대해 "마스크 AND 후 parity"입니다(P0 마스크 = `0xF12CB7` …). 선형이므로 **신드롬 = 받은 ECC ^ 다시 계산한 ECC**가 데이터 비트 i 하나만 틀렸을 때 `csi2_ecc(1u << i)`와 같습니다. 24개 열을 돌며 같은 값을 찾으면 그 비트를 뒤집습니다.

</details>

<details>
<summary>Hint 2 — 신드롬의 무게(popcount)로 분류</summary>

- 0 → 정상
- 비트 1개(무게 1) → ECC 바이트 자체가 틀림. 데이터는 그대로 두고 CORRECTED
- 데이터 열과 일치(D0–D19은 무게 3, D20–D23은 무게 5) → 그 비트 정정
- 그 외(무게 2, 4, 6 등 짝수) → 2비트 이상 오류, UNCORRECTABLE

모든 열의 무게가 홀수라서 2비트 오류는 항상 짝수 무게가 되어 정정 열과 헷갈리지 않습니다. 이것이 SEC-DED입니다.

</details>

<details>
<summary>Hint 3 — 상태는 VC별로, 패킷 길이는 헤더 WC로</summary>

`r->vc[h.vc]`에 `in_frame / lines / frame_num / frame_bad`를 둡니다. 한 VC의 FE가 다른 VC 프레임을 닫으면 안 됩니다. 루프는 `off += 4 + (long ? wc + 2 : 0)`; WC가 틀렸어도(테스트 9번) 그 WC로 건너뛰어야 다음 헤더에 맞습니다. 그래서 **헤더 ECC 정정이 동기 유지의 전부**입니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 패킷 구조

<div class="chart" data-src="chart1.svg" data-caption="short/long 패킷 바이트 배치. 헤더 24비트는 ECC(정정 가능), payload는 CRC(검출만). WC는 little endian이고 CRC를 포함하지 않습니다"></div>

수신기 관점에서 중요한 비대칭이 있습니다.

- **헤더가 깨지면 스트림 전체가 깨집니다.** WC가 틀리면 다음 헤더 위치를 잘못 계산해 이후 모든 패킷이 쓰레기가 됩니다. 그래서 헤더에는 정정 가능한 ECC(SEC-DED)를 둡니다.
- **payload가 깨지면 그 줄 하나만 버리면 됩니다.** 그래서 CRC(검출 전용)로 충분합니다.

### 4.2 VC별 상태 머신과 인터리빙

<div class="chart" data-src="chart2.svg" data-caption="테스트 10번: VC0(RAW10)과 VC1(YUV422) 패킷이 한 스트림에 섞여 들어옵니다. 상태는 VC마다 따로, VC2(미설정) 패킷은 unexpected_vc로만 셉니다"></div>

### 4.3 손으로 따라가기 (trace)

테스트 3·4번 스트림(VC0, RAW10 8×3, 2 프레임):

| off | 헤더 바이트 | decode | 패킷 | 동작 | lines | 결과 |
|---|---|---|---|---|---|---|
| 0 | 00 01 00 1A | OK | FS fn=1 | IDLE → IN_FRAME | 0 | |
| 4 | 2B 0A 00 2E | OK | line, WC 10 | DT·WC·CRC ok | 1 | |
| 20 | 2B **02** 00 2E | syn = 0x0B ^ 0x2E = **0x25** = D11 열 → WC 2 → 10 | line | ecc_corrected = 1 | 2 | (테스트 4번) |
| 36 | 2B 0A 00 2E | OK | line | ok | 3 | |
| 52 | 01 01 00 1D | OK | FE fn=1 | lines 3 == height, fn 일치 | – | frames_ok = 1 |
| 56 | 00 02 00 … | OK | FS fn=2 | prev 1 → 2 ✓ | 0 | |
| 60 | 2B 0A 00 2E | OK | line | ok | 1 | |
| … | | | | | | |
| 108 | FE fn=2 | OK | FE | ok | – | frames_ok = 2, stop_off 112 |

오류 주입별 결과 (solution.c 테스트와 같은 숫자):

| 테스트 | 주입 | 결과 |
|---|---|---|
| 2 | 30비트(데이터 24 + ECC 6) 각각 1비트 / 모든 2비트 쌍 | 30개 전부 CORRECTED / 435쌍 전부 UNCORRECTABLE |
| 5 | off 60 헤더의 D0, D20 뒤집기 | syn 0x07 ^ 0x1F = 0x18(무게 2) → return −1, stop_off 60, frames_ok 1, missing_fe 1 |
| 6 | frame 1 line 1 payload 1바이트 | crc_err 1, lines_ok 5, line_count_err 1, frames_ok 1 / bad 1 |
| 7 | frame 2를 2줄로 (96 B) | line_count_err 1, frames_ok 1 / bad 1, lines_ok 5 |
| 8 | FE(1) 누락 + fn 2 → 4 (164 B) | missing_fe 1, frame_num_err 1, frames_ok 2 / bad 1, lines_ok 9 |
| 9 | 2번째 줄 WC 12 (ECC·CRC는 정상, 58 B) | wc_err 1, crc_err 0, lines_ok 2, stop_off 58 (동기 유지) |
| 10 | VC0 + VC1 인터리브 + VC2 LS (80 B, 9 패킷) | 양쪽 frames_ok 1, lines_ok 2, unexpected_vc 1 |
| 11 | 56 B 프레임을 30 B에서 자름 | return −1, stop_off 20 (16 B 필요, 10 B 남음), lines_ok 1, missing_fe 1 |

CRC 구현 검증: 표준 check 값 `"123456789"` → `0x6F91`(CRC-16/MCRF4XX와 같은 파라미터)과, CSI-2 스펙 부록에 실린 24바이트 payload 예제 두 개(`FF 00 00 02 B9 DC …` → `0x00F0`, `FF 00 00 00 1E F0 …` → `0xE569`)가 모두 맞습니다. ECC는 스펙 수식 그대로이고, 테스트 2번에서 24개 열이 서로 다르고 모두 홀수 무게라 1비트 정정 30/30, 2비트 검출 435/435가 되는 것으로 **자기 일관성**을 확인했습니다(외부 ECC 예제 벡터와의 대조는 하지 않았습니다). TPG는 같은 ECC를 열 테이블 XOR 방식으로, CRC를 bit-serial 방식으로 따로 구현해서 서로 교차 검증합니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `csi2_header_decode`는 **1비트 신드롬(ECC 비트 오류)** 을 먼저 처리합니다. 이걸 빠뜨리면 ECC 바이트만 깨진 정상 헤더를 UNCORRECTABLE로 버립니다(테스트 2번에서 6개 실패).
- 정정 후 `vc/dt/wc`를 **정정된 d에서** 뽑습니다. VC 비트가 깨진 경우 정정하지 않으면 다른 VC의 상태 머신을 망가뜨립니다.
- 길이 검사는 `len - off - 4 < plen` 형태로 뺄셈만 씁니다. `off + 4 + plen > len`은 `size_t` 오버플로 여지가 있고, 이 순서여야 payload를 읽기 전에 멈춥니다(테스트 11번, ASan이 지켜봄).
- `ecc_corrected++`는 패킷이 버퍼 안에 다 들어온 것을 확인한 뒤에 합니다. 잘린 패킷은 "받은 패킷"으로 세지 않습니다.
- 프레임 종료 처리(`close_frame`)를 FE, FS 재진입, 스트림 끝 세 곳이 공유합니다. 열린 프레임을 끝에서 닫지 않으면 테스트 5·11번이 실패합니다.

### 4.5 복잡도

- 시간 **O(len)**: 헤더당 ECC 계산이 상수(정정 시 최대 24열 탐색), payload 바이트마다 CRC 8 step.
- 메모리 **O(1)**: VC 4개분 상태. 실제 HW에서는 CRC를 바이트 테이블(512 B)이나 lane당 병렬 CRC로 계산합니다.

---

## 5. 임베디드 관점으로 말할 것

- **이 체커가 실제로 어디서 도나**: 수신기 IP는 보통 HW로 ECC/CRC를 검사하고 에러를 **인터럽트 + 카운터 레지스터**(ECC 1-bit corrected, ECC 2-bit, CRC error, frame sync error, WC error)로 냅니다. 검증 SW의 일은 (1) 이 카운터를 읽고 (2) 디버그 캡처 버퍼를 SW 체커로 독립 재검증해서 **HW 카운터와 SW 카운터가 일치하는지** 보는 것입니다. 둘이 다르면 수신기 RTL 버그나 카운터 버그입니다.
- **TPG가 핵심 자극원**: 센서 내장 TPG(color bar, walking 1s, PRBS)나 FPGA 패턴 소스로 **기대값을 아는** 데이터를 넣어야 bit-exact 비교가 됩니다. 실제 카메라 이미지로는 "보기에 괜찮다"만 알 수 있습니다.
- **오류 주입**: FPGA 송신기에서 헤더 1비트/2비트, payload 1비트, WC, FE 누락, VC 인터리브를 주입하는 시나리오 표를 만들고, 각 시나리오의 기대 카운터 값을 이 문제처럼 숫자로 고정합니다 → [V5](../V5_test_runner_coverage/index.html)의 테스트 러너에 그대로 들어갑니다.
- **처리량 계산**: 1920×1080 RAW12 30 fps = 1920·1080·12·30 = **746.5 Mb/s** payload. 패킷 오버헤드는 줄당 6 B/2880 B ≈ 0.2%, 블랭킹·LP 전환까지 약 10–20% 여유를 두면 D-PHY 2 lane × 1 Gb/s면 충분합니다. 4K60 RAW12(≈ 6 Gb/s)는 4 lane × 2.0–2.5 Gb/s가 필요합니다.
- 프레임 타임스탬프와 FS 간격 검증은 [V12](../V12_frame_timestamp_check/index.html)와 이어집니다.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"First silicon에서 카메라 없이 수신기를 어떻게 검증하나?"** — FPGA(Zynq + D-PHY 송신 보드)를 패턴 소스로 쓰거나, SoC에 CSI-2 TX가 있으면 TX→RX 루프백, 또는 PHY 레벨 내부 루프백/PPI 패턴으로 시작합니다. 센서는 TPG 모드부터 켭니다.
  *"I'd bring the receiver up with a known pattern source first — an FPGA CSI-2 transmitter or the sensor's own test pattern — so every byte is predictable and I can compare bit-exactly."*
- **"당신 체커가 맞다는 걸 어떻게 아나?"** — CRC는 공개 check 값과 스펙 예제, ECC는 30개 1비트 / 435개 2비트 전수 검사, 그리고 TPG를 다른 방식(열 테이블, bit-serial)으로 구현해 교차 검증합니다. HW 카운터와 SW 카운터의 일치도 서로를 검증합니다.
  *"I check the checker: published CRC vectors, an exhaustive single- and double-bit sweep of the ECC, and an independently written generator."*
- **"ECC corrected 카운트가 조금씩 올라가면?"** — 링크 마진 문제입니다. D-PHY HS 데이터 레이트·deskew·termination·온도/전압을 바꿔 가며 에러율을 그리는 **shmoo**로 이어집니다 → [V7](../V7_shmoo_margin/index.html). 1-bit corrected는 기능상 통과지만 PVT에서 늘어나는지 봐야 합니다.
  *"Corrected errors are a margin signal, not a pass — I'd shmoo data rate against voltage and temperature and watch the corrected-error rate."*
- **"Baidu에서 FPGA로 카메라 데이터를 처리했는데, 멀티 카메라는?"** — VC(최대 4, v2.0은 16)나 DT로 여러 센서를 한 링크에 묶고(aggregator/SerDes, 예: GMSL/FPD-Link 디시리얼라이저), 수신기는 VC별로 다른 버퍼/ISP 채널로 라우팅합니다. 테스트 10번이 그 최소 형태입니다.
  *"With a deserializer aggregating several cameras, each camera gets its own VC, so the checker must keep state per VC exactly like this."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| D-PHY vs C-PHY? | D-PHY: clock lane + 1–4 data lanes(차동쌍), HS 구간에 클럭 별도 전송, lane당 ~2.5 Gb/s(v2.x 4.5). C-PHY: 3선 trio에 3상 신호, 클럭 내장(embedded clock), 7심볼에 16비트(≈ 2.28 bit/symbol)라 같은 선 수에 더 높은 처리량. 패킷 계층(CSI-2)은 거의 같지만 C-PHY는 헤더 구조가 다르고 ECC 대신 CRC를 씁니다. |
| Explain lanes and HS / LP modes. | LP(저속, single-ended, ~10 Mb/s)는 제어·line/frame 사이 휴지용, HS(고속 차동)는 데이터 burst. burst는 LP-11 → LP-01 → LP-00 → HS-0 → SoT 동기 바이트(0xB8)로 시작해 EoT 후 LP-11로 돌아옵니다. 이 문제의 버퍼는 SoT/EoT를 뺀 HS payload입니다. |
| How does lane merging work? | 송신기가 바이트를 lane에 round-robin으로 분배합니다(byte 0 → lane 0, byte 1 → lane 1 …). 수신기는 lane별 SoT로 정렬(deskew)한 뒤 다시 섞습니다. 패킷 길이가 lane 수의 배수가 아니면 lane마다 끝나는 시점이 다르고, lane 간 skew가 크면 merge가 틀어져 헤더 ECC 오류로 나타납니다. |
| How is RAW10 packed? | 4픽셀 → 5바이트: 바이트 0–3은 각 픽셀의 상위 8비트[9:2], 바이트 4는 네 픽셀의 하위 2비트(P0이 bit 1:0). 그래서 RAW10 폭은 4의 배수여야 WC가 정수입니다(1918 → 0). RAW12는 2픽셀 → 3바이트. |
| How does an ISP consume this? | 일반적 순서: BLC(black level) → 렌즈 셰이딩/결함 화소 보정 → demosaic(Bayer → RGB) → AWB 게인 → CCM(색 보정 행렬) → gamma/tone map → NR·sharpen → YUV 변환 → 인코더/NN. 각 블록은 레지스터로 bypass가 가능해서 검증 때 한 블록씩 켜며 golden 모델과 비교합니다. |
| What test pattern generators would you use? | 센서 TPG(color bar, 고정 값, walking 1s), 수신기/ISP 내부 TPG(수신기 이후 경로만 분리 검증), FPGA 패턴 소스(오류 주입 가능). 패턴은 PRBS나 카운터처럼 **수신 측에서 재생성 가능한** 것이 좋습니다 → [V6](../V6_prbs_crc_checker/index.html). |
| How do you validate a receiver without a camera? | FPGA CSI-2 TX(예: Zynq + D-PHY 보드)로 정상/오류 스트림 재생, SoC TX→RX 루프백, PHY 내부 루프백. 시나리오별 HW 에러 카운터와 SW 재검증 결과를 비교합니다. |
| Throughput: 1080p30 RAW12 vs lane rate? | 1920×1080×12×30 = 746.5 Mb/s. 블랭킹 포함 약 0.85–0.9 Gb/s → 1 lane 1.5 Gb/s 또는 2 lane 1 Gb/s. 8b/10b 같은 라인 코딩이 D-PHY에는 없어 lane rate가 그대로 비트율입니다. |
| What would you add next? | LS/LE 줄 번호 연속성, FS 간 시간 간격(프레임 레이트·jitter), 에러 후 **resync**(다음 유효 헤더 탐색 대신 다음 SoT까지 버림), v2.0 VC 확장(VCX)과 ECC 26비트. |

## 7. 자주 하는 실수

- 신드롬 무게 1(ECC 바이트 오류)을 UNCORRECTABLE로 처리 → 테스트 2번 `fixed == 30` 실패
- 정정 전 바이트에서 VC/DT/WC를 읽음 → 테스트 4번에서 WC 2로 잘못 건너뛰어 이후 전부 깨짐
- WC 불일치 때 "기대 WC"로 건너뜀 → 테스트 9번에서 동기 상실. 건너뛰기는 항상 헤더 WC
- 상태를 VC 구분 없이 하나로 둠 → 테스트 10번에서 VC1 FS가 VC0 프레임을 missing FE로 닫음
- 스트림 끝에서 열린 프레임을 닫지 않음 → 테스트 5·11번 `missing_fe` 실패
- 길이 검사 전에 payload/CRC를 읽음 → 테스트 11번에서 ASan heap/stack overflow
- CRC를 MSB-first(0x1021, CCITT-FALSE)로 구현 → `"123456789"`가 0x29B1이 되어 테스트 1번 실패
