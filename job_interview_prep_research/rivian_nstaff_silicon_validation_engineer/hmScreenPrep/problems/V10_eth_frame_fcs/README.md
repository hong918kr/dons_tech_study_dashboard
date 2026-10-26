# V10 · Ethernet MAC Loopback Test: Frame Build, FCS Check, Counters

> **ID**: V10 · **Priority**: P1 · **Pattern**: byte-level frame packing + CRC-32 + loopback scoreboard · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V10` / `./run.sh V10 sol`

## 1. Problem

You are bringing up the Ethernet MAC on a new SoC. Write the software side of a **MAC loopback test**: build frames, check received frames like the MAC's receive logic does, keep RMON-style counters, and run a sequence-numbered loopback through a provided fake MAC/PHY loopback (`lb_xmit` / `lb_recv`) that can inject faults. Frames here run from DA to FCS; the preamble/SFD are added and stripped by the MAC/PHY.

- **`eth_crc32`**: CRC-32 IEEE 802.3 — reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. Check value: `eth_crc32("123456789", 9) == 0xCBF43926`.
- **`eth_build_frame`**: `DA(6) SA(6) [TPID 0x8100 + TCI(2) if vlan != NULL] EtherType(2) payload`, zero-pad so the frame is **at least 60 bytes before the FCS** (64 with FCS, tagged or not), then append the FCS computed over DA..pad. **The FCS is transmitted least significant byte first**: `out[n] = fcs & 0xFF`, `out[n+1] = fcs >> 8`, … All header fields, including TCI = `PCP(3) | DEI(1) | VID(12)`, are big-endian. Returns the frame length; payload > 1500 → `-EMSGSIZE`, `cap` too small → `-ENOSPC`, NULL / `vid > 4095` / `pcp > 7` → `-EINVAL`.
- **`eth_check_frame`**: `len < 64` → `ETH_RUNT`; `len > 1518` (or `> 1522` when bytes 12..13 are `0x8100`) → `ETH_OVERSIZE`; FCS mismatch → `ETH_FCS_ERR`; else `ETH_OK`. Length checks come first. Update the counter for the result; on `ETH_OK` also count octets, VLAN-tagged, broadcast, and the RMON size bucket `hist[]` (64, 65–127, 128–255, 256–511, 512–1023, 1024–1518, 1519–1522).
- **`eth_loopback_test(n, payload_len, r)`**: send frames `k = 0..n-1` (DA = `LB_DA`, SA = `LB_SA`, type `0x88B5`, no tag). Payload bytes 0..3 = `k` big-endian, byte `i ≥ 4` = `(uint8_t)(k * 7 + i)`. After every `lb_xmit`, drain `lb_recv` until it returns 0; drain once more at the end. For each received frame run `eth_check_frame`; for an OK frame parse the sequence number and compare the whole payload (mismatch, or `seq >= n` → `data_mismatch`); an OK frame whose seq is lower than the highest seq seen so far is `out_of_order`. Report `sent`, `received`, `good` (FCS-OK), `corrupted = received − good`, `lost = sent − received`, `out_of_order`, `data_mismatch`, and the counters. Return `0` only if all `n` frames came back good, in order and intact, else `-EIO`.
- No `malloc`.

```c
typedef enum { ETH_OK = 0, ETH_RUNT, ETH_OVERSIZE, ETH_FCS_ERR } EthStatus;
typedef struct { uint8_t pcp; uint8_t dei; uint16_t vid; } EthVlan;
typedef struct { uint32_t frames_ok, octets_ok, vlan_ok, bcast_ok;
                 uint32_t runt, oversize, fcs_err; uint32_t hist[7]; } EthCounters;
typedef struct { uint32_t sent, received, good, corrupted, lost, out_of_order, data_mismatch;
                 EthCounters c; } LbReport;

int    lb_xmit(const uint8_t *frame, size_t len);   /* provided: 0 or -ENOBUFS          */
size_t lb_recv(uint8_t *buf, size_t cap);           /* provided: frame length, 0 = none  */

uint32_t  eth_crc32(const uint8_t *p, size_t n);
int       eth_build_frame(const uint8_t dst[6], const uint8_t src[6], const EthVlan *vlan,
                          uint16_t ethertype, const uint8_t *payload, size_t len,
                          uint8_t *out, size_t cap);
EthStatus eth_check_frame(const uint8_t *f, size_t len, EthCounters *c);
int       eth_loopback_test(uint16_t n, size_t payload_len, LbReport *r);
```

### Example

```
eth_build_frame(DA=ff:ff:ff:ff:ff:ff, SA=02:00:00:00:00:01, no tag, 0x0800, "hello", 5)
  -> 64        14 B header + 5 B payload + 41 B zero pad = 60 B, + 4 B FCS
               CRC32(first 60 B) = 0x32DD79F1 -> bytes 60..63 = F1 79 DD 32
               CRC32(all 64 B)   = 0x2144DF1C   (the fixed "magic residue")

same with VLAN {pcp 5, vid 100}  -> 64, bytes 12..15 = 81 00 A0 64
payload 1500, untagged -> 1518 OK;  tagged -> 1522 OK;  payload 1501 -> -EMSGSIZE

loopback n=10, payload 50, faults: drop #2, bit flip #4, truncate #6 to 30 B, swap #7/#8
  -> -EIO  sent 10  received 9  good 7  corrupted 2 (1 FCS_ERR, 1 RUNT)  lost 1  out_of_order 1
```

### Constraints

- Frames up to 1522 bytes, `n ≤ 256`, `4 ≤ payload_len ≤ 1500`, single thread.
- The fake has its own CRC routine, so a wrong `eth_crc32` cannot "agree with itself" in the loopback tests.

### Clarifying questions to ask (first 3 minutes)

1. Does my frame include the FCS, or does the MAC append it in hardware? Preamble/SFD too?
2. Which byte order does the FCS go out in, and what reflection/init/XOR does the CRC use?
3. Is the 64-byte minimum measured with or without the VLAN tag? (Here: 64 either way, so tagged frames need less padding.)
4. When a frame is both too short and has a bad FCS, which counter wins?
5. In the loopback, what counts as lost vs corrupted vs out of order?
6. Is the loopback inside the MAC, or does it go through the PHY? (It changes what a pass proves.)

## 2. Your workspace

`./run.sh V10`으로 바로 테스트하세요. 테스트 10묶음이 있고, 파일 아래쪽 fake loopback(fault injection 포함)은 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — CRC-32는 비트 단위 8줄이면 된다</summary>

`crc = 0xFFFFFFFF; for each byte { crc ^= b; 8번 { crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0); } } return ~crc;` 오른쪽 시프트 + 반사(reflected) 다항식이라 바이트의 **bit 0이 먼저** 들어갑니다. 이더넷이 각 바이트를 LSB부터 직렬 전송하기 때문입니다. 먼저 `"123456789" → 0xCBF43926`으로 검증하세요(테스트 1번).

</details>

<details>
<summary>Hint 2 — 패딩 기준은 "FCS 앞까지 60바이트"</summary>

`body = hdr + len; if (body < 60) body = 60; total = body + 4;`. 패딩은 반드시 0으로 채우고, FCS는 **패딩까지 포함해서** 계산합니다. VLAN 태그가 있으면 헤더가 18바이트라 페이로드 42바이트부터 패딩이 필요 없습니다.

</details>

<details>
<summary>Hint 3 — 루프백 판정은 "받은 것" 기준으로 센다</summary>

받은 프레임마다 `received++` → `eth_check_frame`이 OK면 `good++` → seq 파싱 → 페이로드 비교 → `seq < highest`면 out-of-order. 마지막에 `corrupted = received − good`, `lost = sent − received`. 잃어버린 프레임은 받은 쪽에서는 "안 온 것"이라 셀 수가 없으므로 뺄셈으로 구합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 프레임 구조

<div class="chart" data-src="chart1.svg" data-caption="이더넷 프레임. eth_build_frame은 DA부터 FCS까지 만들고, preamble/SFD와 IFG는 MAC/PHY가 붙임. FCS는 DA..pad 전체에 대한 CRC-32이고 LSB부터 전송. 64 B 프레임은 선로에서 84 B를 차지"></div>

핵심은 세 가지 숫자입니다.
- **60 / 64**: FCS 제외 최소 60바이트, 포함 64바이트. CSMA/CD 시절 "가장 먼 끝에서 충돌이 일어나도 송신 중에 알아챌 수 있는 최소 길이"(10 Mb/s에서 512 bit time)에서 나온 값이고, 지금도 규격으로 남아 있습니다.
- **1518 / 1522**: 페이로드 1500(MTU) + 헤더 14 + FCS 4 = 1518, VLAN 태그 4바이트가 붙으면 1522.
- **FCS 바이트 순서**: CRC 값 `0x32DD79F1`은 선로에 `F1 79 DD 32` 순서로 나갑니다. 반사 CRC는 LSB부터 계산하므로, 계산 순서와 전송 순서가 같아지도록 낮은 바이트부터 보내는 것입니다.

**FCS 검사 두 가지 방법**: ① 받은 FCS를 떼어 내고 DA..pad의 CRC를 다시 계산해 비교(이 풀이), ② FCS까지 포함해 전체 CRC를 계산하면 프레임이 정상일 때 항상 상수 `0x2144DF1C`(final XOR 전 값은 `0xDEBB20E3`)가 나옴 — 하드웨어 MAC은 바이트를 흘려보내면서 끝에서 상수와 비교하는 ②를 씁니다. FCS 위치를 미리 알 필요가 없기 때문입니다.

### 4.2 루프백 지점 — 무엇을 증명하나

<div class="chart" data-src="chart2.svg" data-caption="루프백 지점이 바깥으로 갈수록 검증하는 경로가 길어짐. 1 MAC 내부 → 2 PCS → 3 PHY near-end(PMA/SerDes) → 4 외부 루프백 플러그·케이블 → 5 상대편 PHY의 far-end remote loopback"></div>

브링업 순서는 안쪽부터입니다. 1번이 통과하면 DMA·디스크립터·MAC FCS 로직이 맞고, 2번에서 실패하면 xMII 타이밍(RGMII clock delay 등)을 의심합니다. 각 단계가 **고장 위치를 반으로 줄이는 이분 탐색**입니다.

### 4.3 손으로 따라가기 (trace)

**테스트 2번 "hello" 프레임 바이트 배치:**

| 오프셋 | 내용 | 바이트 |
|---|---|---|
| 0–5 | DA (broadcast) | `FF FF FF FF FF FF` |
| 6–11 | SA | `02 00 00 00 00 01` |
| 12–13 | EtherType IPv4 | `08 00` |
| 14–18 | payload "hello" | `68 65 6C 6C 6F` |
| 19–59 | pad 41 B | `00 …` |
| 60–63 | FCS = `0x32DD79F1`, LSB first | `F1 79 DD 32` |

`eth_check_frame(f, 64)` → 길이 64 OK, CRC(0..59) = `0x32DD79F1` = 받은 FCS → `ETH_OK`, `frames_ok 1`, `octets_ok 64`, `bcast_ok 1`, `hist[0]` (64 B) 1.

**분류 표 (테스트 3–7번):**

| 입력 | 길이 | 결과 | 카운터 |
|---|---|---|---|
| payload 46 | 14 + 46 + 4 = 64 | 패딩 없음 | – |
| payload 100 | 118 | `ETH_OK` | `hist[1]` (65–127) |
| 잘린 프레임 | 40 | `ETH_RUNT` | `runt` |
| payload 1500, untagged | 1518 | `ETH_OK` | `hist[5]` |
| payload 1500, tagged | 1522 | `ETH_OK` | `vlan_ok`, `hist[6]` |
| 같은 1522 B, TPID를 `08 00`으로 바꾸고 FCS 재계산 | 1522 | `ETH_OVERSIZE` | `oversize` |
| byte 30 bit 2 반전 | 64 | `ETH_FCS_ERR` | `fcs_err` |
| VLAN PCP 5 VID 100 | 18 + 5 + 37 pad + 4 = 64 | TCI = 5 × 2¹³ + 100 = `0xA064` | `vlan_ok` |

**테스트 9번 루프백 (payload 50 → 프레임 68 B):**

| k | fault | 받은 것 | check | highest | 판정 |
|---|---|---|---|---|---|
| 0, 1 | – | seq 0, 1 | OK | 0 → 1 | good |
| 2 | drop | (없음) | – | 1 | 나중에 lost로 계산 |
| 3 | – | seq 3 | OK | 3 | good |
| 4 | byte 30 bit 3 flip | 68 B | **FCS_ERR** | 3 | corrupted |
| 5 | – | seq 5 | OK | 5 | good |
| 6 | truncate → 30 B | 30 B | **RUNT** | 5 | corrupted |
| 7 | swap (보류) | (없음) | – | 5 | – |
| 8 | – | seq 8, 이어서 seq 7 | OK, OK | 8 | 7 < 8 → **out_of_order** |
| 9 | – | seq 9 | OK | 9 | good |

합계: sent 10, received 9, good 7, corrupted 9 − 7 = 2, lost 10 − 9 = 1, out_of_order 1 → `-EIO`.

**테스트 10번**: fake가 MAC **내부에서** payload byte 5를 깨뜨린 **다음** FCS를 새로 만듭니다. FCS는 통과(`good 10`, `fcs_err 0`)하지만 payload 비교가 `data_mismatch 1`을 잡습니다. 테스트 8번 깨끗한 루프백: 10 × 68 = `octets_ok 680`.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `0xEDB88320u & (0u - (crc & 1u))`는 분기 없는 "LSB가 1이면 XOR" 트릭입니다. `(crc & 1) ? 0xEDB88320 : 0`로 써도 같습니다. 바이트당 8번 도는 비트 방식이라 느리지만(§6) 틀릴 곳이 없습니다.
- 헤더 필드는 `put_be16`으로 **명시적으로** 빅엔디언, FCS는 명시적으로 리틀엔디언으로 씁니다. 구조체를 `memcpy`하거나 `htons` 없이 `uint16_t`를 그대로 쓰면 호스트 엔디언에 따라 결과가 달라집니다.
- `eth_check_frame`에서 길이 검사가 FCS 검사보다 먼저입니다. 길이 4 미만에서 `len - 4`가 `size_t` 언더플로로 터지는 것도 이 순서가 막아 줍니다.
- 루프백의 `good`은 "FCS가 맞은 프레임"이고, 내용 검증(`data_mismatch`)은 따로 셉니다. FCS 통과 ≠ 데이터 정상이라는 점을 카운터 구조로 드러냅니다.
- 루프 범위가 `k <= n`인 것은 마지막에 한 번 더 drain하기 위해서입니다(보류된 프레임이 남아 있을 수 있음).

### 4.5 복잡도

- CRC: 바이트당 8 스텝, 프레임당 O(len). 루프백 전체 O(n × payload_len).
- 메모리: 정적 버퍼 3개(약 4.5 KB) — 스택에 1.5 KB 배열을 올리지 않으려고 `static`. 동적 할당 없음.

---

## 5. 임베디드 관점으로 말할 것

- **실제 MAC은 FCS를 하드웨어로 처리**: TX 디스크립터의 "append CRC" 비트, RX의 "strip CRC" 설정이 있습니다. SW가 직접 FCS를 붙이는 경우는 이 테스트처럼 **raw/FCS-passthrough 모드로 MAC의 CRC 로직 자체를 검증**할 때입니다(일부러 틀린 FCS를 보내서 RX가 `fcs_err`를 세는지 확인).
- **카운터는 HW 레지스터와 대조**: SW 스코어보드 숫자와 MAC의 RMON MIB 카운터(`rx_crc_errors`, `rx_undersize`, `rx_oversize`, `rx_64_octets` …)를 비교합니다. 둘이 다르면 테스트가 아니라 카운터 로직(clear-on-read, 32-bit wrap)의 버그일 수 있습니다.
- **Sequence number + 패턴 페이로드**: 손실·중복·순서 바뀜·데이터 손상을 하나의 스트림으로 구분해 냅니다. PRBS 패턴을 쓰면 더 강력합니다 → [V6](../V6_prbs_crc_checker/index.html).
- **DMA 링과 연결**: 실제 테스트는 `lb_xmit`/`lb_recv` 대신 TX/RX 디스크립터 링에 프레임을 넣고 꺼냅니다 → [V3](../V3_dma_desc_ring/index.html). 처리량 측정은 [V4](../V4_perf_bench_stats/index.html)의 통계 방식으로.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"FCS passed but data is wrong. How is that possible?"** — FCS는 MAC TX에서 생성되므로, 그보다 **앞**(DMA, 내부 FIFO, AXI 버스)에서 깨진 데이터는 깨진 채로 올바른 FCS를 받습니다(테스트 10번). 그래서 end-to-end 페이로드 비교가 필요합니다. 내부 SRAM의 ECC/parity가 이 구간을 보호합니다.
  *"FCS only protects the wire; corruption before the MAC computes it gets a valid FCS, so I always compare the payload end to end."*
- **"Which loopback do you run first on first silicon?"** — MAC 내부 루프백 → PCS → PHY near-end → 외부 플러그 순서. 각 단계가 실패 위치를 반으로 줄입니다. FPGA/HAPS에서는 PHY가 보통 외부 칩이라 MAC 루프백까지만 SoC 로직 검증이 되고, 그 이후는 FMC PHY 카드 의존입니다.
  *"Inside out: MAC, PCS, PHY near-end, then an external plug, so each pass halves the search space."*
- **"How do you know your checker is correct?"** — 알려진 벡터(`123456789 → 0xCBF43926`), 손으로 계산한 프레임 FCS, fault injection(드롭·비트 플립·절단·순서 바꿈)을 하나씩 넣어서 각 카운터가 **정확히 1**씩 오르는지 확인합니다. 이 fake가 그 역할입니다.
  *"I inject each fault type alone and check that exactly one counter moves by exactly one."*
- **"Line rate: can your test saturate 10G?"** — 64 B 프레임은 선로에서 84 B(preamble 8 + IFG 12 포함) = 672 bit → 10 Gb/s에서 14.88 Mpps, 프레임당 67.2 ns. CPU가 프레임마다 CRC를 SW로 계산하면 절대 못 따라갑니다. 처리량 테스트는 HW FCS + DMA + 미리 만든 프레임 재사용으로 하고, SW는 카운터만 읽습니다.
  *"At 64 bytes, 10G is 14.88 million packets per second, 67 ns per frame, so the data path must be hardware; software only checks counters."*
- **"Your PHY link doesn't come up. Where do you look?"** — MDIO로 PHY ID 레지스터(2, 3) 읽기 → BMSR(reg 1)의 link status/autoneg complete → 상대편 광고 능력(reg 5) → RGMII TX/RX clock delay(2 ns) 설정 → PHY 리셋 시퀀스/strap 핀.
  *"First MDIO: read the PHY ID, then BMSR for link and autoneg-complete, then the partner's abilities, then RGMII delays."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| What are MAC, PCS, PMA and PMD? | MAC: 프레임 단위(주소, FCS, 흐름 제어, 카운터). PCS(physical coding sublayer): 라인 코딩(1G는 8b/10b, 10G는 64b/66b), 정렬, 스크램블. PMA: 직렬화/역직렬화(SerDes), 클럭 복원(CDR). PMD/AFE: 실제 매체 신호(구리선 드라이버, 광 모듈). 위로 갈수록 디지털, 아래로 갈수록 아날로그. |
| MII vs RGMII vs SGMII vs USXGMII? | MAC–PHY 사이 인터페이스. MII: 4 bit, 25 MHz(100M). RGMII: 4 bit DDR, 125 MHz(1G), 핀 수가 적지만 클럭–데이터 2 ns skew를 PHY나 PCB에서 맞춰야 함(가장 흔한 브링업 버그). SGMII: 1.25 Gbaud 직렬 SerDes 한 쌍, 8b/10b. USXGMII: 10.3125 Gbaud 하나로 10M~10G 멀티 레이트. |
| How do you access PHY registers? | MDIO/MDC 2선 직렬 관리 버스. Clause 22: 5 bit PHY 주소 + 5 bit 레지스터(32개, BMCR 0, BMSR 1, PHYID 2–3, ANAR 4, ANLPAR 5). Clause 45: MMD(device) 주소 + 16 bit 레지스터 주소를 먼저 쓰고 읽는 2단계 접근, 10G 이상과 T1 PHY에서 사용. 일부 칩은 Clause 22 reg 13/14로 Clause 45를 간접 접근. |
| Link does not come up. How do you debug? | ① MDIO로 PHY ID 확인(0xFFFF면 MDIO/리셋/주소 문제), ② BMSR link 비트는 **latch-low**라 두 번 읽음, ③ autoneg 완료·상대 광고 비교, ④ 강제 속도로 autoneg 배제, ⑤ PHY 루프백(BMCR bit 14)으로 MAC–PHY 구간 분리, ⑥ RGMII delay, ⑦ 오실로스코프로 클럭. |
| What is automotive Ethernet (100BASE-T1 / 1000BASE-T1)? | 꼬인 **한 쌍**(single twisted pair)으로 전이중 통신, 무게·비용 절감. 100BASE-T1은 PAM3, 1000BASE-T1은 PAM3 + 강한 FEC. autoneg 대신 master/slave를 미리 설정하는 경우가 많고, 링크 품질은 SQI(signal quality index) 레지스터로 봅니다. Rivian 차량의 카메라·ECU 백본이 이것입니다. |
| Why does a CRC catch burst errors so well? | 차수 32인 CRC는 **길이 32 이하의 모든 burst 오류**를 반드시 잡고, 홀수 개 비트 오류도 잡으며(다항식 성질), 그 밖의 무작위 오류를 놓칠 확률은 약 2⁻³². 구리선 노이즈는 연속 비트를 깨뜨리는 burst 형태가 많아서 잘 맞습니다. 단, 프레임 길이에 따라 보장되는 Hamming distance가 줄어듭니다(점보 프레임에서 약해짐). |
| What does the IFG and preamble do to throughput? | 프레임마다 preamble/SFD 8 B + IFG 12 B = 20 B가 추가로 선로를 씀. 64 B 프레임: 64 / 84 = 76 % 효율, 10G에서 14.88 Mpps. 1518 B 프레임: 1518 / 1538 = 98.7 %, 812,744 pps. 작은 패킷 성능(pps)이 패킷 프로세서 설계의 진짜 한계입니다. |
| How would you speed up CRC in software? | 바이트 단위 256-entry 테이블(바이트당 1 lookup), slicing-by-8(8바이트 동시), ARMv8 `CRC32` 명령(`__crc32b/w/d`, 같은 0x04C11DB7 다항식 반사형), x86 PCLMULQDQ carry-less 곱셈. 하지만 실제 데이터 경로는 MAC 하드웨어가 처리합니다. |
| What is PTP timestamping in the MAC? | IEEE 1588 PTP 메시지가 MAC을 지나는 순간(SFD 감지 시점)을 하드웨어 클럭으로 찍어 디스크립터에 넣습니다. SW 타임스탬프는 인터럽트·스케줄링 지연 때문에 µs 오차가 나지만 HW 타임스탬프는 ns 수준. 차량에서는 gPTP(802.1AS)로 카메라·라이다·레이더 시간을 맞춥니다 → [V12](../V12_frame_timestamp_check/index.html). |
| Cut-through vs store-and-forward? | Store-and-forward는 프레임 전체를 받고 FCS를 확인한 뒤 전달(나쁜 프레임 차단, 지연 = 프레임 길이). Cut-through는 DA만 보고 바로 전달(낮은 지연, 대신 깨진 프레임이 퍼지고 FCS를 끝에서 "stomp"해서 표시). Cisco/Marvell 스위치 ASIC 단골 주제입니다. |

## 7. 자주 하는 실수

- FCS를 빅엔디언으로 붙임(`32 DD 79 F1`) → 자기끼리는 맞지만 실제 MAC/Wireshark와 안 맞음 (테스트 2번이 바이트를 직접 확인)
- 패딩을 FCS 계산 **뒤에** 붙이거나, 패딩 전 길이로 CRC 계산 → FCS 불일치
- 최소 길이를 "64 B 페이로드" 또는 "VLAN이면 68 B"로 착각 → 테스트 2, 7번
- 태그 프레임의 최대 길이를 1518로 검사 → 정상 1522 B VLAN 프레임을 oversize로 버림 (테스트 5번)
- 길이 검사 전에 `len - 4`를 계산 → `size_t` 언더플로, 짧은 프레임에서 버퍼 밖 읽기
- `uint16_t` 필드를 호스트 순서로 `memcpy` → 리틀엔디언 CPU에서 EtherType이 `00 08`
- 루프백에서 FCS만 보고 데이터 비교를 생략 → 테스트 10번(내부 손상) 놓침
- 마지막 drain을 빼먹음 → 보류된 프레임이 lost로 잡힘
