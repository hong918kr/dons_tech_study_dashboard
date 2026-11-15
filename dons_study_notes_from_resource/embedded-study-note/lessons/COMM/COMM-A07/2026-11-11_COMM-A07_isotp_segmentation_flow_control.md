---
id: COMM-A07
time: 120분
updated: 2026-11-11
---
# COMM-A07 ISO-TP (ISO 15765-2): CAN 위 분할·재조립과 흐름 제어

## 학습 목표

- SF / FF / CF / FC 네 가지 PCI(Protocol Control Information) 바이트를 **비트 단위로** 쓰고 읽는다. FF_DL 12비트, SN 랩(…15 → 0), FC 의 FlowStatus·BlockSize·STmin 을 실제 캡처 바이트로 설명한다.
- 송신·수신 ISO-TP 상태 기계를 tick 스케줄러 위에서 구현한다. BS 블록마다 FC 를 기다리고, 블록 안 CF 사이에 STmin 을 지키는 것을 **버스 트레이스로 증명**한다.
- 타이머(N_As / N_Bs / N_Cr), FC.WAIT 와 N_WFTmax, FC.OVFLW, 잘못된 SN, 프레임 손실·중복, 예상 밖 PDU 를 하나씩 주입해 결과 코드(N_Result)를 맞힌다. "송신자는 성공, 수신자는 실패" 가 왜 생기는지 말한다.
- Linux 커널 can-isotp 의 실제 동작(Lima VM, vcan0)과 내 구현을 **바이트 단위로** 대조한다.
- 4095 바이트 전송 시간을 COMM-M04 의 프레임 비트 수로 계산하고(공식 = 시뮬레이션), BS·STmin·버스 부하·게이트웨이 설계가 시간에 주는 영향을 숫자로 말한다.

## 1. 문제

CAN 프레임에는 데이터가 8 바이트밖에 안 들어간다. 그런데 차량 진단(UDS), ECU 플래싱, VIN·캘리브레이션 읽기는 수백~수천 바이트를 주고받는다. 그 사이를 메우는 전송 계층이 ISO-TP(ISO 15765-2)다. Tesla·Rivian·자동차 Tier-1 면접에서는 "4 KB 를 CAN 으로 어떻게 보내죠? 수신자가 느리면? 프레임 하나가 빠지면?" 이 단골이다. bring-up 에서는 candump 를 보고 `10 28 …`, `30 02 F5`, `21 …` 을 바로 읽어야 한다.

ISO-TP 의 생각은 단순하다. 첫 프레임에 전체 길이를 알리고, 이어지는 프레임에 번호를 붙인다. 수신자는 흐름 제어(Flow Control) 프레임으로 "몇 개씩, 얼마 간격으로" 보낼지 정한다. 어려운 곳은 가장자리다. 타이머가 언제 시작되는지, WAIT 를 몇 번까지 허용하는지, 중간에 새 메시지가 오면 어떻게 하는지. 이 레슨은 그 가장자리를 모두 시뮬레이터로 재현하고, Linux 커널 구현과 비교한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">ISO-TP PCI (normal addressing, Classic CAN, 패딩 0xCC) — 예시 바이트는 Linux can-isotp 실제 캡처 (K2·K3)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="54" font-weight="bold">SF  Single</text><text x="10" y="68">len ≤ 7</text>
    <text x="10" y="119" font-weight="bold">FF  First</text><text x="10" y="133">len 8..4095</text>
    <text x="10" y="184" font-weight="bold">CF  Consecutive</text><text x="10" y="198">SN 1..F,0,1..</text>
    <text x="10" y="249" font-weight="bold">FC  Flow Control</text><text x="10" y="263">수신자 → 송신자</text>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="38" width="72" height="30" class="f-acc"/><rect x="222" y="38" width="360" height="30" class="f-ok"/><rect x="582" y="38" width="144" height="30" class="f-mut"/>
    <rect x="150" y="103" width="72" height="30" class="f-acc"/><rect x="222" y="103" width="72" height="30" class="f-acc"/><rect x="294" y="103" width="432" height="30" class="f-ok"/>
    <rect x="150" y="168" width="72" height="30" class="f-acc"/><rect x="222" y="168" width="504" height="30" class="f-ok"/>
    <rect x="150" y="233" width="72" height="30" class="f-acc"/><rect x="222" y="233" width="72" height="30" class="f-warn"/><rect x="294" y="233" width="72" height="30" class="f-warn"/><rect x="366" y="233" width="360" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="186" y="58">0 | SF_DL</text><text x="402" y="58">데이터 SF_DL 바이트</text><text x="654" y="58">패딩</text>
    <text x="186" y="123">1 | DL[11:8]</text><text x="258" y="123">DL[7:0]</text><text x="510" y="123">데이터 6 바이트</text>
    <text x="186" y="188">2 | SN</text><text x="474" y="188">데이터 7 바이트 (마지막 CF 는 남은 만큼 + 패딩)</text>
    <text x="186" y="253">3 | FS</text><text x="258" y="253">BS</text><text x="330" y="253">STmin</text><text x="546" y="253">패딩 (패딩 끄면 DLC 3)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="150" y="86">05 DE AD BE EF 01 CC CC      ← 5 바이트 SF (K3)</text>
    <text x="150" y="151">10 28 00 07 0E 15 1C 23      ← FF_DL = 0x028 = 40 (K2)</text>
    <text x="150" y="216">21 2A 31 38 3F 46 4D 54      ← SN 1, 데이터 6..12 번째</text>
    <text x="150" y="281">30 02 F5 CC CC CC CC CC      ← CTS, BS 2, STmin 0xF5 = 500 µs</text>
  </g>
</svg>
```

바이트 0 의 상위 니블이 종류(0 SF, 1 FF, 2 CF, 3 FC)다. 하위 니블은 종류마다 다르다(SF_DL, 길이 상위 4비트, SN, FlowStatus). 이 레슨의 모델은 **Classic CAN, 표준 11비트 ID, normal addressing**(연결 = CAN ID 쌍, 예: 요청 0x7E0 / 응답 0x7E8)이다.

### Part A — PCI, 송신·수신 상태 기계, Linux 와 바이트 대조

- `stmin_to_us`: STmin 바이트 → µs. 0x00–0x7F = 0–127 ms, 0xF1–0xF9 = 100–900 µs, 나머지는 예약값 → 127 ms 로 취급(256 값 전수 시험).
- `isotp_parse`: PCI 해석과 형식 검사. SF_DL 0(CAN FD escape)·8 이상, FF_DL < 8, FF_DL = 0(2016판 32비트 escape — **이 레슨은 미지원, 무시**), 짧은 DLC, 종류 4..F 는 `PCI_BAD`.
- `tx_start` / `tx_poll` / `tx_on_fc`: 송신자. SF 또는 FF 를 보내고, FC(CTS) 를 받으면 BS 개씩 CF 를 보낸다. 블록 안 CF 사이는 **송신 확인(confirm) 뒤 STmin** 을 기다린다. FC 뒤 첫 CF 는 바로 보낸다(모델 정의 — 아래 Linux 캡처와 같은 동작).
- `rx_first` / `rx_cf`: 수신자. FF_DL > 버퍼면 FC OVFLW. 아니면 CTS 를 주고 CF 를 SN 순서대로 붙인다. BS 블록이 끝날 때마다 FC 를 다시 보낸다.
- 시험: Linux 커널 can-isotp 가 vcan0 에 낸 프레임 4 묶음과 **바이트까지 같아야** 한다. 길이 1..4095 **전수** 왕복(BS 0..20, STmin 6종 무작위)에서 버스 트레이스 검사기가 SN 순서·BS·STmin 간격을 독립적으로 확인한다.

### Part B — 타이머, FC.WAIT, 결함 주입

- `tx_timers`: 송신 확인이 N_As 안에 안 오면 N_TIMEOUT_A(메일박스 abort), FC 를 N_Bs 안에 못 받으면 N_TIMEOUT_Bs.
- `rx_timers`: CF 를 N_Cr 안에 못 받으면 N_TIMEOUT_Cr. WAIT 상태에서는 wait_gap 마다 다시 판단한다.
- `rx_fc_status`: 버퍼가 바쁘면 FC.WAIT. 연속 WAIT 가 N_WFTmax 에 닿으면 포기(N_WFT_OVRN).
- 넣을 결함과 기대 결과: CF 손실(가운데 / 블록 안 / 마지막), CF 중복, 버퍼 바쁨(WAIT 3번 뒤 CTS, N_WFTmax 초과), FF_DL > 버퍼(OVFLW), 깨진 FS(0x33), 버스 잼(확인 없음), 예약 STmin, 수신 중 새 FF / SF, 기다리지 않던 CF·FC. 각 결과와 **발생 tick** 까지 단언한다. Linux 에서 손으로 넣은 같은 프레임 순서와도 결과를 비교한다.

### Part C — 전송 시간과 게이트웨이

- `can_frame_bits`: COMM-M04 의 프레임 비트 수(표준 데이터 프레임 47 + 8n, IFS 포함, 최악 스터핑 ⌊(34+8n−1)/4⌋).
- `isotp_transfer_us`: FF 시작 → 마지막 CF 끝 시간의 닫힌 식. tick = 1 µs 시뮬레이션과 **정확히 같아야** 한다. `demo_time.c` 가 4095 B 를 BS·STmin·비트레이트·버스 부하별로 표로 만든다.
- `gw_on_rx` / `gw_on_tx`: store-and-forward 게이트웨이. 테스터 T → G.in 으로 받은 메시지를 **같은 버퍼 그대로**(zero-copy) G.out → ECU E 로 보낸다. 보내는 동안 버퍼를 잠그고, 그 사이 들어온 FF 에는 FC.WAIT 로 버틴다. 같은 버스에 X → Y 연결이 끼어 있다(COMM-M05 의 ID 중재).

## 2. Problem (English)

Implement ISO 15765-2 (ISO-TP) segmentation and reassembly over Classic CAN (11-bit IDs, normal addressing, optional 0xCC padding to DLC 8). Everything runs on a deterministic tick scheduler with a frame-level shared bus (lowest pending ID wins). No heap: the receive buffer and its capacity are supplied by the caller, and the sender transmits straight from the caller's buffer.

```c
typedef struct { uint32_t id; uint8_t dlc; uint8_t d[8]; } can_msg;
typedef enum { N_OK, N_TIMEOUT_A, N_TIMEOUT_BS, N_TIMEOUT_CR, N_WRONG_SN, N_INVALID_FS,
               N_UNEXP_PDU, N_WFT_OVRN, N_BUFFER_OVFLW } n_result;

uint32_t stmin_to_us(uint8_t raw);                       /* 0x00-0x7F ms, 0xF1-0xF9 100-900 us, else 127 ms */
int  isotp_parse(const can_msg *m, pci_t *p);            /* PCI_SF/FF/CF/FC or PCI_BAD */
int  tx_start(isotp_link *l, const uint8_t *buf, uint16_t len, uint32_t now);  /* 0, -1 busy, -2 bad len */
void tx_poll(isotp_link *l, uint32_t now);                /* confirmations, next CF (BS, STmin) */
void tx_on_fc(isotp_link *l, const pci_t *fc, uint32_t now);
void rx_first(isotp_link *l, const pci_t *sf_or_ff, uint32_t now);
void rx_cf(isotp_link *l, const pci_t *cf, uint32_t now);
void tx_timers(isotp_link *l, uint32_t now);  void rx_timers(isotp_link *l, uint32_t now);  /* N_As, N_Bs, N_Cr */
int  rx_fc_status(isotp_link *l, uint32_t now);          /* CTS, WAIT (busy, < N_WFTmax) or abort */
double isotp_transfer_us(uint16_t len, uint8_t bs, uint8_t stmin, uint32_t bitrate, bool pad, bool worst);
```

Rules: SF carries 1..7 bytes; FF carries a 12-bit length (8..4095) and 6 bytes; CFs carry 7 bytes with SN starting at 1 and wrapping 15 → 0. After the FF and after every BS consecutive frames (BS = 0: never again) the sender waits for an FC: CTS continues, WAIT restarts N_Bs, OVFLW aborts, any other FlowStatus aborts. STmin is the minimum gap from the end of one CF to the start of the next CF of the same block. Timeouts are 1000 ms in this model.

Examples (bytes match a real Linux can-isotp capture):

```text
40 bytes i*7, receiver BS=2 STmin=0xF5, padding 0xCC:
  7E0#102800070E151C23  7E8#3002F5CCCCCCCCCC  7E0#212A31383F464D54  7E0#225B626970777E85
  7E8#3002F5CCCCCCCCCC  7E0#238C939AA1A8AFB6  7E0#24BDC4CBD2D9E0E7  7E8#3002F5CCCCCCCCCC  7E0#25EEF5FC030A11CC
10 bytes, no padding:   7E0#100AA0A1A2A3A4A5  7E8#300000  7E0#21A6A7A8A9
4095 bytes, BS=0, STmin=0, 500 kbit/s, padded, no stuff bits: 587 frames, 130.314 ms
```

Follow-ups: Why can the sender report success while the receiver failed? What happens if a CF is received twice? What does FC.WAIT protect, and what can't it protect? How long does 4095 B take with STmin = 127 ms? Why does a gateway need more than one buffer?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열네 군데다. Part A 7개(`stmin_to_us`, `isotp_parse`, `tx_start`, `tx_poll`, `tx_on_fc`, `rx_first`, `rx_cf`), Part B 3개(`rx_fc_status`, `tx_timers`, `rx_timers`), Part C 4개(`can_frame_bits`, `isotp_transfer_us`, `gw_on_rx`, `gw_on_tx`)다. 주어진 것: 메일박스 FIFO, 프레임 만들기(`frame_put`, 패딩), `rx_flow`(FC 보내기 + 수신 상태 전환), 디스패처 `link_rx`, 버스 `sim_step`(배달 → poll → 중재), 결함 주입 훅, 트레이스 검사기, Linux 캡처 표. Part B 스텁은 "항상 CTS, 타이머 없음" 이라서 Part A 만 먼저 풀어도 정상 경로 시험은 통과한다. 처음 실행 결과(실제 출력, 중간 생략):

```text
FAIL starter.c:544  bad == 0
FAIL starter.c:550  isotp_parse(&m, &p) == PCI_SF && p.dl == 5 && p.data[0] == 0xDE
FAIL starter.c:567  tx_start(&D.s, SRC, 0, 0) == -2
FAIL starter.c:576  match_linux("K1 20B BS=2 STmin=0x0A pad", k1, 20, true, 2, 0x0A, LINUX_K1, 4)
…
C4 SF while gateway busy: T sent 1 (tx ok 0), E got 0, G.in ignored 0
FAIL starter.c:891  T.tx_ind[N_OK] == 2 && s2e.got == 1 && Gin.rx_ignored == 1 && gw.fwd_ok == 1
11/75 passed
```

(exit 1, 0.1 초 안에 끝난다. 시뮬레이션 루프는 모두 tick 상한이 있다. 통과하는 11개는 "형식이 틀린 프레임은 `PCI_BAD`" 같은 부정 시험이다.)

## 4. 힌트

<details><summary>힌트 1 — 상위 니블이 종류, 하위 니블이 인자</summary>

`t = d[0] >> 4`, `lo = d[0] & 0x0F`. SF 는 `lo` 가 길이, FF 는 `lo << 8 | d[1]` 이 12비트 길이, CF 는 `lo` 가 SN, FC 는 `lo` 가 FlowStatus 이고 `d[1]` = BS, `d[2]` = STmin 이다. 길이 검사도 같이 한다. SF 는 `dlc ≥ 1 + SF_DL`, FF 는 `dlc == 8`, FC 는 `dlc ≥ 3` 이어야 한다(패딩을 끈 FC 는 DLC 3 이다).

</details>

<details><summary>힌트 2 — 송신자는 "확인(confirm)" 을 기준으로 움직인다</summary>

프레임을 메일박스에 넣은 시점이 아니라 **버스에서 전송이 끝난 시점**(tx confirm)에 다음 일을 정한다. FF 확인 → FC 대기(N_Bs 시작). CF 확인 → 마지막이면 N_OK, BS 블록의 마지막이면 FC 대기, 아니면 `next_at = now + STmin`. STmin 은 "CF 끝 → 다음 CF 시작" 이므로 확인 시각에서 재야 한다.

</details>

<details><summary>힌트 3 — BS 카운터는 송신자·수신자가 따로, 블록마다 다시 채운다</summary>

송신자는 FC(CTS) 를 받을 때 `bs_left = BS`, 수신자는 FC(CTS) 를 보낼 때 `bs_left = cfg.bs` 다. CF 하나마다 줄이고 0 이 되면 각각 "FC 기다림" / "FC 보냄" 이다. BS = 0 이면 줄이지 않는다. 마지막 CF 는 블록 끝과 겹쳐도 FC 가 필요 없다(먼저 "다 받았나" 를 본다).

</details>

<details><summary>힌트 4 — FF 의 6 바이트는 CTS 를 줄 때까지 따로 둔다</summary>

버퍼가 바빠 WAIT 를 보낼 때 FF 데이터를 버퍼에 써 버리면, 지금 그 버퍼를 쓰는 쪽(게이트웨이 송신)을 망가뜨린다. `rx_ff[6]` 에 보관하고, 주어진 `rx_flow` 가 CTS 를 줄 때 복사한다. 그래서 `rx_first` 는 `rx_off = 0` 으로 두고 `rx_flow` 를 부르면 된다.

</details>

<details><summary>힌트 5 — 전송 시간은 "프레임 수 × 프레임 시간 + 블록 안 간격"</summary>

ncf = ⌈(len−6)/7⌉, 블록 수 = FC 수 = BS ? ⌈ncf/BS⌉ : 1. STmin 간격은 블록 안에서만 생기므로 (ncf − 블록 수) 번이다. FC 뒤 첫 CF 에는 STmin 이 없다(모델 정의). 프레임 시간은 COMM-M04 처럼 비트 수 / 비트레이트이고, 패딩을 끄면 FC 는 DLC 3, 마지막 CF 는 1 + 남은 바이트다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **왜 필요한가**: CAN 데이터는 8 바이트, 진단·플래싱은 수 KB 다. ISO-TP 는 OSI 3~4 계층 역할이다. 위에는 UDS(ISO 14229, AUTO-M02), 아래에는 CAN 드라이버가 있다.
2. **네 프레임**: SF(한 번에 끝), FF(길이 알림), CF(번호 붙은 조각), FC(수신자가 속도 조절).
3. **흐름 제어**: BS = 몇 개마다 FC 를 다시 받나, STmin = CF 사이 최소 간격. WAIT 는 "조금만 기다려", OVFLW 는 "그 길이 못 받아".
4. **실패**: SN 틀림·타임아웃은 그 메시지만 버리고 끝이다. 재전송은 없다. 복구는 위 계층(UDS 의 P2 타임아웃 → 재요청)이 한다.
5. **숫자**: 500 kbit/s, 패딩, BS=0, STmin=0 이면 4095 B 에 130 ms(587 프레임)다. STmin 1 ms 면 714 ms 다.

### Part A — PCI 와 STmin 표

| 종류 | 바이트 0 | 바이트 1 | 바이트 2 | 데이터 | 이 모델의 유효 조건 |
|---|---|---|---|---|---|
| SF | 0x0L (L = SF_DL) | 데이터 | | L 바이트 (1..7) | 1 ≤ L ≤ 7, DLC ≥ 1 + L |
| FF | 0x1H | 길이 하위 8비트 | 데이터 | 6 바이트 | DLC 8, FF_DL ≥ 8 (0 = escape → 무시) |
| CF | 0x2N (N = SN) | 데이터 | | 최대 7 바이트 | DLC ≥ 2, SN = 기대값 |
| FC | 0x3S (S = FS) | BS | STmin | 없음 | DLC ≥ 3, S: 0 CTS, 1 WAIT, 2 OVFLW |

| STmin 바이트 | 의미 | 이 레슨 |
|---|---|---|
| 0x00–0x7F | 0–127 ms | `raw × 1000 µs` |
| 0x80–0xF0 | 예약 | 127 ms 로 취급 (스펙은 "가장 긴 값 사용" 으로 알려짐 — 스펙 확인 권장) |
| 0xF1–0xF9 | 100–900 µs | `(raw − 0xF0) × 100 µs` |
| 0xFA–0xFF | 예약 | 127 ms 로 취급 |

0x00–0x7F(ms)와 0xF1–0xF9(100 µs 단위), 그 밖은 예약이라는 인코딩은 Linux 커널 ISO-TP 문서(참고자료)에서도 확인했다. FF_DL 12비트의 최댓값이 4095 다. 2016판은 FF_DL = 0 뒤에 32비트 길이를 두는 escape 를 넣었다고 알려져 있다(CAN FD 의 큰 SF 를 위한 SF_DL escape 도 있다 — 스펙 확인 권장). 이 레슨은 둘 다 **구현하지 않고 무시**한다. 시험이 그 프레임이 `PCI_BAD` 인지 확인한다.

### Part A — 송신자·수신자 상태 기계

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a7m" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">ISO-TP 상태 기계 (solution.c) — 왼쪽 송신자, 오른쪽 수신자</text>
  <text x="10" y="42" fill="currentColor" font-weight="bold">송신 (tx_poll · tx_on_fc · tx_timers)</text>
  <text x="410" y="42" fill="currentColor" font-weight="bold">수신 (rx_first · rx_cf · rx_timers)</text>
  <path d="M395 30 L395 340" stroke="currentColor" stroke-dasharray="3 4" opacity="0.5"/>
  <g stroke="currentColor">
    <rect x="20" y="60" width="110" height="34" rx="8" class="f-mut"/><rect x="220" y="60" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="20" y="160" width="110" height="34" rx="8" class="f-acc"/><rect x="220" y="160" width="130" height="34" rx="8" class="f-warn"/>
    <rect x="120" y="270" width="130" height="34" rx="8" class="f-ok"/>
    <rect x="420" y="60" width="120" height="34" rx="8" class="f-mut"/><rect x="610" y="60" width="130" height="34" rx="8" class="f-ok"/>
    <rect x="420" y="270" width="120" height="34" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="81">TX_IDLE</text><text x="285" y="81">TX_SF</text><text x="75" y="181">TX_FF</text>
    <text x="285" y="181">TX_WAIT_FC</text><text x="185" y="291">TX_CF</text>
    <text x="480" y="81">RX_IDLE</text><text x="675" y="81">RX_CF</text><text x="480" y="291">RX_WAIT</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a7m)">
    <path d="M130 70 L218 70"/><path d="M220 86 L132 86"/><path d="M75 94 L75 158"/><path d="M130 177 L218 177"/>
    <path d="M270 194 L215 268"/><path d="M240 270 Q300 245 315 196"/><path d="M150 304 C130 340 240 340 220 306"/>
    <path d="M120 290 L8 290 L8 72 L18 72"/><path d="M350 168 C392 150 392 205 352 188"/>
    <path d="M540 70 L608 70"/><path d="M610 86 L542 86"/><path d="M470 94 L470 268"/>
    <path d="M540 287 L725 287 L725 96"/><path d="M440 304 C420 340 520 340 500 306"/><path d="M650 94 C630 140 720 140 700 96"/>
  </g>
  <g fill="currentColor">
    <text x="174" y="64" text-anchor="middle">len ≤ 7: SF</text><text x="176" y="102" text-anchor="middle">확인 → N_OK</text>
    <text x="82" y="130">len ≥ 8: FF</text><text x="174" y="171" text-anchor="middle">확인 → N_Bs</text>
    <text x="252" y="236">FC CTS</text><text x="290" y="258">BS 개 확인</text>
    <text x="185" y="352" text-anchor="middle">확인 + STmin → 다음 CF</text><text x="14" y="324">마지막 CF 확인 → N_OK</text>
    <text x="285" y="150" text-anchor="middle">WAIT: N_Bs 다시</text>
    <text x="476" y="136">IDLE 에서 SF → 복사, N_OK</text><text x="476" y="152">FF_DL &gt; cap → FC OVFLW</text>
    <text x="575" y="56" text-anchor="middle">FF + 빈 버퍼 → CTS</text><text x="476" y="190">FF + 바쁨 → WAIT</text>
    <text x="560" y="280">wait_gap 뒤 빔 → CTS</text><text x="480" y="352" text-anchor="middle">아직 바쁨, wft &lt; max → WAIT</text>
    <text x="665" y="198" text-anchor="middle">CF: SN 확인, 복사</text><text x="665" y="214" text-anchor="middle">블록 끝 → FC</text>
    <text x="560" y="110" text-anchor="middle">N_OK · SN 틀림 · N_Cr</text>
    <text x="10" y="378">송신 중단: FC OVFLW → N_BUFFER_OVFLW · FS ≥ 3 → N_INVALID_FS · N_Bs 만료 → N_TIMEOUT_Bs · 확인 없음 → N_TIMEOUT_A</text>
    <text x="10" y="394">수신 중단: RX_WAIT 에서 wft = N_WFTmax → N_WFT_OVRN · 수신 중 SF/FF → N_UNEXP_PDU 후 새 메시지 시작</text>
  </g>
</svg>
```

한 tick 의 순서가 중요하다. `sim_step` 은 ① 전송이 끝난 프레임을 받는 노드에 배달하고 보낸 노드에 확인 플래그를 세운다. ② 모든 링크를 poll 한다. ③ 버스가 비었으면 기다리는 프레임 중 **가장 낮은 ID** 를 보낸다(COMM-M05 의 중재를 프레임 단위로 줄인 것). 그래서 "FC 수신 → 같은 tick 에 첫 CF 출발", "CF 확인 → STmin 0 이면 같은 tick 에 다음 CF 출발" 이 된다. 이 순서 덕분에 Part C 의 공식과 시뮬레이션이 tick 하나까지 맞는다.

### Part A — K2 를 한 프레임씩 따라가기

40 바이트(i × 7), 수신자 BS = 2, STmin = 0xF5(500 µs). ncf = ⌈34/7⌉ = 5, 블록 = ⌈5/2⌉ = 3, 프레임 = 1 + 5 + 3 = 9.

| # | 프레임 | 송신자 | 수신자 |
|---|---|---|---|
| 1 | `7E0#10 28 00 07 0E 15 1C 23` | TX_FF → 확인 → TX_WAIT_FC (N_Bs 시작) | FF_DL 40 ≤ cap, 6 B 보관 → CTS 결정 |
| 2 | `7E8#30 02 F5 CC…` | CTS: bs_left 2, STmin 5 tick → TX_CF, 첫 CF 바로 | RX_CF, bs_left 2, N_Cr 시작 |
| 3 | `7E0#21 2A … 54` | 확인, bs_left 1 → next_at = 확인 + STmin | SN 1 OK, off 13, bs_left 1 |
| 4 | `7E0#22 5B … 85` | 확인, bs_left 0 → TX_WAIT_FC | SN 2 OK, off 20, bs_left 0 → FC |
| 5 | `7E8#30 02 F5 CC…` | CTS → TX_CF, 첫 CF 바로 | |
| 6–7 | `23 …`, `24 …` | 같은 패턴 | off 27, 34 → FC |
| 8 | `7E8#30 02 F5 CC…` | CTS | |
| 9 | `7E0#25 EE F5 FC 03 0A 11 CC` | 마지막(6 B + 패딩 1) → 확인 → N_OK | off 40 = len → N_OK |

마지막 CF 는 블록 끝(BS 2 의 첫 번째)과 상관없이 "다 받았다" 가 먼저다. 그래서 FC 가 3 개이고 4 개가 아니다.

### Part A — Linux 커널 can-isotp 와 바이트 대조

`demo_linux.sh` 는 Lima VM(Ubuntu, kernel 7.0, can-utils 2023.03)의 vcan0 에서 `isotprecv`(수신, `-b` BS, `-m` STmin, `-p CC:CC` 패딩)와 `isotpsend` 를 돌리고 `candump -L` 로 찍는다. 실제 출력(K2, K4):

```text
-- K2 40 B, BS=2 STmin=0xF5, pad CC
    (1794456424.791984) vcan0 7E0#102800070E151C23
    (1794456424.792009) vcan0 7E8#3002F5CCCCCCCCCC
    (1794456424.792011) vcan0 7E0#212A31383F464D54
    (1794456424.792568) vcan0 7E0#225B626970777E85
    (1794456424.792581) vcan0 7E8#3002F5CCCCCCCCCC
    (1794456424.792582) vcan0 7E0#238C939AA1A8AFB6
    (1794456424.793212) vcan0 7E0#24BDC4CBD2D9E0E7
    (1794456424.793222) vcan0 7E8#3002F5CCCCCCCCCC
    (1794456424.793224) vcan0 7E0#25EEF5FC030A11CC
-- K4 10 B, no padding
    (1794456426.251508) vcan0 7E0#100AA0A1A2A3A4A5
    (1794456426.251528) vcan0 7E8#300000
    (1794456426.251529) vcan0 7E0#21A6A7A8A9
```

`solution.c` 의 `LINUX_K1..K4` 표가 이 바이트들이다. 내 송신자·수신자가 만든 프레임과 문자열로 비교해 네 묶음 모두 `match` 다. 두 구현이 독립적이므로, PCI 배치·패딩·FC 바이트·DLC(패딩 끈 FC 3, 마지막 CF 5)를 기억에 의존하지 않고 확인한 셈이다. 타임스탬프에서 모델 정의 두 가지도 확인된다.

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a7t" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">K2 실제 타임라인 (Linux can-isotp, vcan0, demo_linux.sh) — 시간은 FF 기준 µs</text>
  <text x="200" y="44" fill="currentColor" text-anchor="middle">송신 isotpsend (0x7E0)</text>
  <text x="560" y="44" fill="currentColor" text-anchor="middle">수신 isotprecv (0x7E8)</text>
  <path d="M200 52 L200 330 M560 52 L560 330" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor" fill="none" marker-end="url(#a7t)">
    <path d="M200 70 L558 70"/><path d="M560 100 L202 100"/><path d="M200 128 L558 128"/><path d="M200 178 L558 178"/>
    <path d="M560 206 L202 206"/><path d="M200 232 L558 232"/><path d="M200 282 L558 282"/><path d="M560 304 L202 304"/>
    <path d="M200 326 L558 326"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="380" y="65">FF 10 28 … (FF_DL 40)</text><text x="380" y="95">FC 30 02 F5 (CTS, BS 2, STmin 500 µs)</text>
    <text x="380" y="123">CF 21</text><text x="380" y="173">CF 22</text><text x="380" y="201">FC (블록 끝)</text>
    <text x="380" y="227">CF 23</text><text x="380" y="277">CF 24</text><text x="380" y="299">FC</text>
    <text x="380" y="321">CF 25 (6 B + 0xCC)</text>
  </g>
  <g fill="currentColor">
    <text x="570" y="74">0</text><text x="570" y="104">+25</text><text x="570" y="132">+27</text><text x="570" y="182">+584</text>
    <text x="570" y="210">+597</text><text x="570" y="236">+598</text><text x="570" y="286">+1228</text>
    <text x="570" y="308">+1238</text><text x="570" y="330">+1240</text>
    <text x="625" y="132">← FC 뒤 2 µs</text><text x="625" y="236">← FC 뒤 1 µs</text>
  </g>
  <path d="M185 128 L178 128 L178 178 L185 178 M185 232 L178 232 L178 282 L185 282" stroke="currentColor" fill="none"/>
  <g fill="currentColor" text-anchor="end">
    <text x="172" y="150">557 µs</text><text x="172" y="166">≥ STmin</text>
    <text x="172" y="254">630 µs</text><text x="172" y="270">≥ STmin</text>
  </g>
</svg>
```

- 블록 안 CF → CF 간격은 557 µs, 630 µs 로 STmin 500 µs 이상이다(연습 4 가 타임스탬프를 파싱해 확인).
- FC → 그 블록 첫 CF 는 1~2 µs 다. Linux 는 FC 뒤 첫 CF 에 STmin 을 기다리지 않는다. 이 레슨의 송신자도 같게 정의했다(스펙 원문의 정확한 표현은 확인 권장). 이 정의는 Part C 공식에 그대로 들어간다.

### Part A — 1..4095 전수 왕복

길이마다 BS(0..20)와 STmin(0x00, 0x01, 0x02, 0xF1, 0xF3, 0xF9)을 xorshift32 로 고른다. 데이터도 무작위다. tick = 100 µs, 프레임 1개 = 1 tick 인 빠른 버스를 쓴다. 트레이스 검사기 `chk_hook` 은 상태 기계를 전혀 보지 않고 **버스에 나간 프레임만** 본다. FC(CTS)를 보면 BS·STmin 을 읽고(STmin 변환을 따로 다시 씀), CF 마다 SN 이 1, 2, …, F, 0, … 순서인지, 블록 안 CF 수 ≤ BS 인지, 간격 ≥ STmin 인지 센다. 실제 출력:

```text
round trip len 1..4095: fails=0 frame-count mismatches=0, 1399065 frames, 8749949 ticks; trace: 1196324 CF, 198646 FC, 997678 STmin gaps checked, min slack 0 ticks, violations 0
SN wrap 120 B: CF SN sequence 123456789ABCDEF01
```

프레임 수는 길이마다 닫힌 식 1 + ncf + ⌈ncf/BS⌉ 와 정확히 같았다. 간격 99만 7천여 개 중 최소 여유(slack)가 0 tick 이다. 즉 송신자가 STmin 을 **딱 맞게** 지킨다. 너무 일찍 보내면 위반, 너무 늦게 보내면 시간 낭비다.

### Part B — 결함 주입 결과

결함은 버스 훅 `fault_hook` 으로 넣는다. 버스에서 프레임이 끝날 때 수신자에게 배달하지 않거나(DROP), 두 번 배달하거나(DUP), 첫 바이트를 바꿔 배달한다. 실제 CAN 에서는 깨진 프레임을 컨트롤러가 자동 재전송한다. 그래서 ISO-TP 에서 "프레임 손실" 은 수신 FIFO 오버런, 게이트웨이 필터·버퍼 부족, 수신 노드 bus-off(COMM-M07) 같은 곳에서 생긴다. 중복은 EOF 마지막 비트 근처 오류로 송신자가 재전송했는데 수신자는 이미 받은 경우다(COMM-M04 의 EOF 7번째 비트 규칙). 모든 결과는 solution.c 의 실제 출력이다(tick = 100 µs, N_As = N_Bs = N_Cr = 10000 tick = 1 s, wait_gap = 3000 tick).

| # | 주입 | 수신자 | 송신자 | 확인한 것 |
|---|---|---|---|---|
| B1 | 50 B, BS 0, CF#3 손실 | N_WRONG_SN (CF#4 에서) | **N_OK** | 남은 CF 3개는 무시됨 |
| B2 | 50 B, BS 4, CF#2 손실 | N_WRONG_SN | N_TIMEOUT_Bs @10006 | = 4번째 CF 끝 6 + N_Bs |
| B3 | 20 B, 마지막 CF 손실 | N_TIMEOUT_Cr @10003 | **N_OK** | = CF1 도착 3 + N_Cr |
| B4 | CF#1 중복 | N_WRONG_SN | N_OK | 이중 수신도 SN 으로 잡힘 |
| B5 | 버퍼 바쁨 ~7000 tick | WAIT 3번 → CTS → N_OK | WAIT 3번 봄 → N_OK | 데이터 일치 |
| B6 | 계속 바쁨, N_WFTmax 2 | N_WFT_OVRN @6001 | N_TIMEOUT_Bs @13002 | = 마지막 WAIT 끝 + N_Bs |
| B7 | 200 B → cap 100 | FC `320000CC…`, IDLE 유지 | N_BUFFER_OVFLW | CF 0개 |
| B8 | FC 의 첫 바이트 → 0x33 | CTS 보냈으니 CF 기다림 → N_TIMEOUT_Cr | N_INVALID_FS | CF 0개 |
| B9 | 버스 잼 (확인 안 옴) | — | N_TIMEOUT_A @10000 | 메일박스 비움 |
| B10 | STmin 0x80 / 0xF0 / 0xFA / 0xF9 | N_OK | N_OK | CF 간격 1270 / 1270 / 1270 / 9 tick |
| B13 | CF 보내는 중 엉뚱한 FC | N_OK | N_OK, FC 무시 1 | 전송 계속 |

가장 중요한 줄은 B1 과 B3 이다. **ISO-TP 에는 끝-끝 확인(ACK)이 없다.** BS = 0 이면 송신자는 FF 뒤 첫 FC 이후로 수신자 소식을 듣지 않는다. 그래서 마지막 CF 를 보냈으면 N_OK 다. 수신자는 같은 메시지를 실패로 버린다. 이 차이를 메우는 것은 위 계층이다. UDS 라면 테스터가 응답을 P2 시간 안에 못 받아 재요청한다(AUTO-M02). BS 를 작게 하면 실패를 더 빨리 안다(B2: 블록 끝에서 N_Bs). 대신 FC 가 늘어 느려진다(Part C).

### Part B — FC.WAIT 와 N_WFTmax

B5 의 타임라인은 이렇다. FF 가 tick 1 에 도착한다. 버퍼가 바쁘니 WAIT#1 을 보내고 3000 tick 뒤 다시 본다. tick 3001 에도 바빠 WAIT#2, 6001 에 WAIT#3 을 보낸다. 9001 에는 비었으니 CTS 를 보낸다. 송신자는 WAIT 마다 N_Bs 를 다시 시작하므로, WAIT 간격(300 ms)이 N_Bs(1 s)보다 짧은 한 기다린다. B6 은 N_WFTmax = 2 에서 세 번째 판단 때 포기한다(N_WFT_OVRN). 그 뒤 수신자는 아무것도 안 보낸다. 송신자는 마지막 WAIT 에서 N_Bs 가 지나 N_TIMEOUT_Bs 로 끝난다. N_WFTmax 는 수신자 쪽 파라미터로 알려져 있다(스펙 확인 권장). 이 모델의 송신자는 WAIT 횟수를 세지 않고 N_Bs 만 본다.

설계 규칙은 두 가지다. **wait_gap < N_Bs** 이어야 하고(아니면 송신자가 먼저 포기), **wait_gap × N_WFTmax ≥ 바쁜 시간의 최댓값** 이어야 한다(아니면 수신자가 먼저 포기). 부트로더가 블록마다 플래시를 쓰는 동안 WAIT 를 보내는 경우가 전형이다. 섹터 지우기가 수백 ms 라면 이 곱을 먼저 계산한다.

### Part B — 예상 밖 PDU 표, Linux 와 비교

| 수신자 상태 | 받은 것 | 이 모델 | Linux can-isotp (demo_linux.sh 실측) |
|---|---|---|---|
| RX_CF | SN 이 기대값과 다름 | N_WRONG_SN, IDLE (뒤 CF 무시) | 메시지 버림, isotprecv 출력 없음 |
| RX_CF | 마지막 CF 안 옴 | N_Cr 뒤 N_TIMEOUT_Cr | 약 1 s 뒤 출력 없이 끝 (1.08 s, sleep 0.05 s 포함) |
| RX_CF | 새 FF | 이전 것 N_UNEXP_PDU, 새 메시지 시작 | 새 메시지(A0..A9) 받음 |
| RX_CF | SF | 이전 것 N_UNEXP_PDU, SF 전달 | SF(11 22 33) 받음 |
| RX_CF | 같은 CF 두 번 | 두 번째에서 N_WRONG_SN | 메시지 버림 |
| RX_IDLE | CF / FC / 형식 틀린 프레임 | 무시 (rx_ignored, tx_fc_ignored) | — |

송신자 쪽도 같은 방식으로 비교했다. Linux `isotpsend` 는 FC OVFLW 에 `write: Message too long`(EMSGSIZE), FC 가 없으면 1.01 s 뒤 `write: Communication error on send`(ECOMM)로 끝난다. WAIT 3번(0.4 s 간격) 뒤 CTS 에는 정상 완료했다. FS = 3 에는 CF 를 하나도 보내지 않았다(중단). 다만 `isotpsend -b` 는 exit 0 을 냈다. 오류가 write 반환값으로 올라오지 않은 것이다(관찰 사실만 적는다). B11·B12 는 이 손 프레임 순서를 raw 노드로 똑같이 넣어 같은 결론을 확인한다. ISO 15765-2 의 "예상 밖 N_PDU 처리" 표가 이 동작을 정한다고 알려져 있다(스펙 확인 권장).

### Part C — 전송 시간 공식

프레임 비트 수는 COMM-M04 와 같다. 표준 ID 데이터 프레임은 SOF..EOF 44 + 8n, IFS 3 을 더해 **47 + 8n** 이다. DLC 8 은 111 비트, 500 kbit/s 에서 222 µs 다. 최악 스터핑은 ⌊(34 + 8n − 1)/4⌋ 를 더한다(DLC 8: 135 비트, 270 µs). 이 레슨 표는 **스터프 비트 0 개(하한)** 와 **Davis 상한(최악)** 두 가지로 낸다. 실제 데이터는 그 사이다.

T(len) = [ f(FF) + nfc · f(FC) + (ncf − 1) · f(CF) + f(마지막 CF) ] + (ncf − nfc) · STmin

여기서 f 는 프레임 시간, nfc = BS ? ⌈ncf/BS⌉ : 1 이다. 수신자 처리 지연(N_Br)과 송신자 지연(N_Cs)은 0 으로 둔다. solution.c 의 C2 는 tick = 1 µs 시뮬레이션과 이 식을 7 가지 설정에서 비교하고, 모두 µs 단위까지 같다. `demo_time.c`(실제 출력, 1.5 s):

```text
4095 B @ 500 kbit/s, std ID, DLC 8 + 0xCC padding (CF 585, frame 111 bits = 222 us no-stuff / 135 bits = 270 us worst)
 BS  STmin   STmin_us  frames | no-stuff ms  sim ms  | worst ms | payload kB/s (no-stuff)
  0   0x00         0     587 |     130.31   130.31 |   158.49 |    31.42
  0   0xF5       500     587 |     422.31   422.31 |   450.49 |     9.70
  0   0x01      1000     587 |     714.31   714.31 |   742.49 |     5.73
  0   0x05      5000     587 |    3050.31  3050.31 |  3078.49 |     1.34
  0   0x14     20000     587 |   11810.31  11810.31 | 11838.49 |     0.35
  0   0x7F    127000     587 |   74298.31  74298.31 | 74326.49 |     0.06
  8   0x00         0     660 |     146.52   146.52 |   178.20 |    27.95
  8   0xF5       500     660 |     402.02   402.02 |   433.70 |    10.19
  8   0x01      1000     660 |     657.52   657.52 |   689.20 |     6.23
  8   0x05      5000     660 |    2701.52  2701.52 |  2733.20 |     1.52
  8   0x14     20000     660 |   10366.52  10366.52 | 10398.20 |     0.40
  8   0x7F    127000     660 |   65043.52  65043.52 | 65075.20 |     0.06
 32   0x00         0     605 |     134.31   134.31 |   163.35 |    30.49
 32   0xF5       500     605 |     417.31   417.31 |   446.35 |     9.81
 32   0x01      1000     605 |     700.31   700.31 |   729.35 |     5.85
 32   0x05      5000     605 |    2964.31  2964.31 |  2993.35 |     1.38
 32   0x14     20000     605 |   11454.31  11454.31 | 11483.35 |     0.36
 32   0x7F    127000     605 |   72016.31  72016.31 | 72045.35 |     0.06

BS=0 STmin=0, by bitrate (padding; unpadded in brackets)
   125 kbit/s:   521.26 ms [  520.55]  worst   633.96 ms  ->   7.86 kB/s payload, 50.3% of raw bitrate
   250 kbit/s:   260.63 ms [  260.28]  worst   316.98 ms  ->  15.71 kB/s payload, 50.3% of raw bitrate
   500 kbit/s:   130.31 ms [  130.14]  worst   158.49 ms  ->  31.42 kB/s payload, 50.3% of raw bitrate
  1000 kbit/s:    65.16 ms [   65.07]  worst    79.25 ms  ->  62.85 kB/s payload, 50.3% of raw bitrate
```

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">4095 B 전송 시간 (500 kbit/s, 패딩, 스터프 0) — demo_time.c 실측, 세로축 로그</text>
  <path d="M90 260 L745 260 M90 260 L90 50" stroke="currentColor" fill="none"/>
  <path d="M90 193.3 L745 193.3 M90 126.7 L745 126.7 M90 60 L745 60" stroke="currentColor" stroke-dasharray="2 4" opacity="0.4" fill="none"/>
  <g fill="currentColor" text-anchor="end"><text x="84" y="264">100 ms</text><text x="84" y="197">1 s</text><text x="84" y="131">10 s</text><text x="84" y="64">100 s</text></g>
  <g stroke="currentColor">
    <rect x="100" y="252.3" width="24" height="7.7" class="f-acc"/><rect x="126" y="248.9" width="24" height="11.1" class="f-ok"/><rect x="152" y="251.5" width="24" height="8.5" class="f-warn"/>
    <rect x="208" y="218.3" width="24" height="41.7" class="f-acc"/><rect x="234" y="219.7" width="24" height="40.3" class="f-ok"/><rect x="260" y="218.6" width="24" height="41.4" class="f-warn"/>
    <rect x="316" y="203.1" width="24" height="56.9" class="f-acc"/><rect x="342" y="205.5" width="24" height="54.5" class="f-ok"/><rect x="368" y="203.6" width="24" height="56.4" class="f-warn"/>
    <rect x="424" y="161.0" width="24" height="99.0" class="f-acc"/><rect x="450" y="164.6" width="24" height="95.4" class="f-ok"/><rect x="476" y="161.9" width="24" height="98.1" class="f-warn"/>
    <rect x="532" y="121.8" width="24" height="138.2" class="f-acc"/><rect x="558" y="125.6" width="24" height="134.4" class="f-ok"/><rect x="584" y="122.7" width="24" height="137.3" class="f-warn"/>
    <rect x="640" y="68.6" width="24" height="191.4" class="f-acc"/><rect x="666" y="72.5" width="24" height="187.5" class="f-ok"/><rect x="692" y="69.5" width="24" height="190.5" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="138" y="278">STmin 0</text><text x="246" y="278">0.5 ms</text><text x="354" y="278">1 ms</text>
    <text x="462" y="278">5 ms</text><text x="570" y="278">20 ms</text><text x="678" y="278">127 ms</text>
    <text x="138" y="243">130 ms</text><text x="246" y="212">422 ms</text><text x="354" y="188">714 ms</text>
    <text x="462" y="155">3.05 s</text><text x="570" y="116">11.8 s</text><text x="678" y="55">74.3 s</text>
  </g>
  <g stroke="currentColor"><rect x="420" y="30" width="12" height="12" class="f-acc"/><rect x="510" y="30" width="12" height="12" class="f-ok"/><rect x="600" y="30" width="12" height="12" class="f-warn"/></g>
  <g fill="currentColor"><text x="436" y="41">BS 0</text><text x="526" y="41">BS 8</text><text x="616" y="41">BS 32</text><text x="10" y="296">막대 위 숫자 = BS 0 값. STmin 0 에서는 FC 가 많은 BS 8 이 가장 느리고, STmin ≥ 0.5 ms 에서는 BS 8 이 가장 빠르다.</text></g>
</svg>
```

읽는 법은 다음과 같다.

- **STmin 이 시간을 지배한다.** STmin 0 이면 버스가 한계(130 ms)다. 1 ms 만 되어도 5.5 배(714 ms), 예약값 폴백 127 ms 면 **74 초** 다. 수신 ECU 가 STmin 을 크게 주면 플래싱이 몇 분이 된다. "STmin 이 예약값이면 127 ms" 규칙이 왜 위험한지 보여 준다.
- **BS 의 효과는 STmin 에 따라 뒤집힌다.** STmin 0 에서는 BS 8 이 BS 0 보다 16 ms 느리다(FC 73 개 × 222 µs). STmin 0.5 ms 이상에서는 BS 8 이 **더 빠르다**(402 vs 422 ms). FC 뒤 첫 CF 는 STmin 을 기다리지 않는다. 그래서 FC 왕복(222 µs × 2)이 STmin 간격 하나를 대신한다. 이 모델(과 Linux 관찰)의 정의에서 나오는 결과다.
- **페이로드 효율은 50.3 %** 다. CF 하나가 111 비트 중 데이터 56 비트(7 B)를 나른다. 패딩을 꺼도 거의 같다(마지막 CF 와 FC 만 짧아진다). 효율을 올리려면 CAN FD(COMM-M08, 64 B 프레임)로 간다.
- 최악 스터핑은 STmin 0 에서 +22 %(158 ms)다. 응답 시간 예산은 이 값으로 잡는다.

### Part C — 버스 부하와 우선순위

같은 4095 B 전송(BS 0, STmin 0)에, 더 높은 우선순위 ID 0x100 이 1 ms 마다 DLC 8 프레임 k 개를 넣는다(모터 제어 루프 흉내). demo_time.c 실제 출력:

```text
same transfer (BS=0 STmin=0, 500k) while ID 0x100 sends k DLC-8 frames every 1 ms (higher priority)
  k=0 (bus load from 0x100 =   0%):   130.31 ms  (x1.00)
  k=1 (bus load from 0x100 =  22%):   167.61 ms  (x1.29)
  k=2 (bus load from 0x100 =  44%):   234.21 ms  (x1.80)
  k=3 (bus load from 0x100 =  67%):   390.05 ms  (x2.99)
  k=4 (bus load from 0x100 =  89%):  1162.17 ms  (x8.92)
```

간단한 이론은 "남는 용량으로만 진행한다" 다. T ≈ 130.3 / (1 − ρ) 이고, ρ = 0.22 → 167, 0.44 → 233, 0.67 → 395, 0.89 → 1180 ms 다. 실측과 몇 % 안에서 맞는다. 진단 트래픽에 높은 ID(0x7E0 대)를 주는 이유가 이것이다. 제어 프레임을 방해하지 않고, 남는 시간에만 흐른다. 반대로 진단 쪽 시간 예산은 버스 부하로 나눠서 잡아야 한다.

### Part C — 게이트웨이

```text
C3 bring-up log (first 16 of 2297 frames, 500 kbit/s, 1 tick = 100 us):
   t=    0.0 ms  700#12BC8BEB387E35D0
   t=    0.3 ms  708#301001CCCCCCCCCC
   t=    0.6 ms  700#21C712E85E5ACC59
   t=    0.9 ms  7E0#1FFFC4DE6D873843
   t=    1.2 ms  7E8#3000F2CCCCCCCCCC
   t=    1.5 ms  7E0#214FF9675C6C34AA
   t=    1.9 ms  700#22277B30C023D09C
   t=    2.2 ms  7E0#2278186FF14EFD32
   …
C3 gateway: E got 3/3 (bad 0), Y got 6/6 (bad 0), fwd ok 3 fail 0, G.in WAIT sent 4, errors 0, connection switches 1130, done at 1862.1 ms (T->G 1689.7 ms, G->E 1862.0 ms, X->Y 765.0 ms)
C4 SF while gateway busy: T sent 2 (tx ok 2), E got 1, G.in ignored 1
```

구성은 테스터 T(0x7E0 → / ← 0x7E8), 게이트웨이 G 의 두 링크(G.in 이 T 를 받고, G.out 이 0x6F0 → / ← 0x6F8 로 ECU E 에 보냄), 끼어드는 연결 X → Y(0x700 / 0x708) 다. 여섯 링크가 한 버스를 쓴다. 500 kbit/s 의 222 µs 프레임은 tick 100 µs 에서 3 tick(300 µs)으로 올림된다. 그래서 이 시나리오의 ms 값은 근사다. 정확한 시간은 위의 tick 1 µs 표를 쓴다. T 는 4095 · 300 · 1000 B 를 연달아 보낸다. 로그 첫 줄들에서 X 의 FF 와 T 의 FF 가 섞여 나가는 것이 보인다. T 의 FF `1FFF…` 는 FF_DL 0xFFF = 4095 다. 연결이 1130 번 바뀌며 섞였지만 E 와 Y 는 모든 메시지를 바이트까지 맞게 받았다. 설계 포인트는 셋이다.

1. **zero-copy 와 버퍼 소유권.** G.in 의 수신 버퍼를 G.out 이 그대로 보낸다(4 KB RAM 한 벌). 보내는 동안 G.in 은 `rx_lock` 이다. 그 사이 T 의 다음 FF 에는 FC.WAIT 를 보낸다(총 4 번). CTS 를 주는 순간 FF 의 6 바이트가 버퍼로 들어간다. 그래서 힌트 4 의 "FF 데이터는 CTS 때 복사" 가 필요하다. 스텁처럼 무조건 CTS 를 주면 보내는 중인 버퍼를 덮어써서 E 가 깨진 데이터를 받는다.
2. **SF 는 WAIT 할 방법이 없다(C4).** 게이트웨이가 바쁠 때 온 5 바이트 SF 는 버려지고 `rx_ignored` 만 1 오른다. T 는 N_OK 다. 진짜 게이트웨이는 버퍼를 둘 이상 두거나(핑퐁), SF 용 작은 큐를 따로 둔다. 아니면 위 계층 재시도에 맡긴다. 이 레슨은 "조용한 손실" 을 카운터로 드러내는 데서 멈춘다.
3. **우선순위가 WAIT 를 숨길 수 있다.** 처음에 E 의 STmin 을 0 으로 두었더니 WAIT 가 0 번이었다. G.out(0x6F0)이 CF 를 쉬지 않고 보내 T 의 FF(0x7E0)가 중재에서 계속 졌다. 그래서 FF 는 전달이 끝난 뒤에야 도착했다. 낮은 ID 의 연속 전송은 높은 ID 를 굶긴다(COMM-M05). STmin 1 ms 로 바꾸자 T 의 FF 가 틈에 들어왔고 WAIT 가 생겼다.

**functional vs physical addressing.** 지금까지는 physical(1 : 1, 0x7E0 ↔ 0x7E8) 이다. 테스터가 "모든 ECU 에게" 묻는 functional 요청(OBD 의 0x7DF 가 대표 예)은 여러 수신자가 동시에 받는다. 이때 FF 를 보내면 여러 ECU 가 각자 FC 를 보낸다. 송신자는 누구 FC 를 따를지 알 수 없다. 그래서 functional 요청은 SF 로만 보낸다고 알려져 있다(스펙 확인 권장. Linux 의 `isotpsend -S` 가 이 모드다). 응답은 각 ECU 가 자기 physical ID 로 보낸다. 큰 응답이면 테스터가 ECU 마다 FC 를 보낸다.

### 복잡도와 RAM

| 항목 | 값 |
|---|---|
| 링크 하나 (`isotp_link`) | 336 B (64비트 Mac 의 sizeof, 메일박스 4 프레임 포함) + 수신 버퍼(최대 4095 B, 호출자가 줌) |
| 송신 | 호출자 버퍼를 직접 읽음, 복사 0 |
| 프레임 처리 | 프레임당 O(1), 수신 복사 ≤ 7 B |
| 메일박스 | 링크당 4 프레임 FIFO (가득 차면 `q_full` 카운트) |

펌웨어에서는 `link_rx` 를 CAN RX 인터럽트(또는 RX FIFO 를 비우는 태스크)에서, `link_poll` 을 1 ms 타이머 tick 에서 부른다. STmin 0xF1–0xF9(100 µs 단위)를 지키려면 tick 이 더 촘촘하거나, 하드웨어 타이머로 다음 CF 를 예약해야 한다. 1 ms tick 이면 0xF5 가 사실상 1 ms 가 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 75/75 passed, exit 0, 약 0.2 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음, 약 0.5 s
cc -std=c11 -Wall -Wextra -O2 demo_time.c -o demo_time && ./demo_time          # solution.c 를 include, 약 1.5 s
limactl shell elinux -- bash "$PWD/demo_linux.sh"                              # Linux can-isotp 기준 실험 (VM)
```

실제 출력(위에서 보인 줄은 생략):

```text
K1 20B BS=2 STmin=0x0A pad (4 frames, Linux can-isotp capture): 7E0#1014000102030405 7E8#30020ACCCCCCCCCC 7E0#21060708090A0B0C 7E0#220D0E0F10111213 -> match
K3 5B SF pad (1 frames, Linux can-isotp capture): 7E0#05DEADBEEF01CCCC -> match
K4 10B no pad (3 frames, Linux can-isotp capture): 7E0#100AA0A1A2A3A4A5 7E8#300000 7E0#21A6A7A8A9 -> match
B11 new FF mid: rx ok 1, errors 1 (N_UNEXP_PDU), ignored 0
B12 sender vs raw FC #2: tx N_OK, CF sent 2, WAIT seen 3
B12 sender vs raw FC #3: tx N_TIMEOUT_Bs, CF sent 0, WAIT seen 1
C2 4095 B BS=8  STmin=0xF5 pad @ 500 k: formula 402020 us, sim 402020 us
C2  100 B BS=3  STmin=0x02 nopad worst-stuff @ 250 k: formula 27640 us, sim 27640 us
75/75 passed
```

하네스가 버그를 실제로 잡는지 보려고, solution.c 사본에 그럴듯한 버그 8 개를 하나씩 넣어 돌렸다. 모두 FAIL 로 잡혔다. (1) FC 뒤 첫 CF 도 STmin 대기 → C2 공식 불일치. (2) 수신자 SN 검사 생략 → B1. (3) SN 이 15 → 1 로 돎 → SN 랩 시험. (4) WAIT 에 N_Bs 재시작 안 함 → B6. (5) 예약 STmin → 0 → STmin 표. (6) 새 FF 에 N_UNEXP_PDU 보고 안 함 → B11. (7) STmin 을 확인이 아니라 송신 요청 시각부터 잼 → 전수 왕복의 트레이스 검사기. (8) 수신자가 블록마다 BS 카운터를 다시 안 채움 → Linux K2 대조. (1)은 바이트가 같아 Linux 대조로는 안 잡히고, 시간 공식만 잡는다.

## 7. 자주 하는 실수

**1. STmin 을 "송신 요청" 시각부터 잰다.** STmin 은 이전 CF 의 **전송 끝** 부터다. 메일박스에 넣은 시각부터 재면, 버스가 바쁠 때 CF 가 늦게 나가도 다음 CF 는 일찍 나간다. 그러면 간격이 STmin 보다 짧아진다. 트레이스 검사기가 바로 위반으로 잡는다(버그 7).

```c
if (l->tx_conf) { ... l->tx_next_at = now + l->tx_stmin_t; }   /* now = 확인(전송 끝) 시각 */
```

**2. SN 을 1..15 로 돌린다.** 첫 CF 만 1 로 시작하고, 15 다음은 **0** 이다. `(sn + 1) & 0x0F` 가 맞다. 120 B 메시지(CF 17 개)의 SN 이 `123456789ABCDEF01` 이어야 한다.

**3. 마지막 CF 뒤에 FC 를 기다린다.** BS 블록의 마지막과 메시지의 마지막이 겹치면, 송신자는 FC 를 기다리다 N_TIMEOUT_Bs 가 난다. 수신자도 쓸데없는 FC 를 보낸다. 두 쪽 모두 "다 보냈나/받았나" 를 먼저 본다(K2 에서 FC 가 3 개인 이유).

**4. 예약 STmin 을 0 으로 읽는다.** 0x80–0xF0, 0xFA–0xFF 를 0 으로 다루면 느린 수신자에게 CF 를 몰아 보낸다. 규칙은 반대로 "가장 긴 값(127 ms)" 이다. 안전한 쪽으로 틀리는 셈이다. 0xF1–0xF9 를 ms 로 읽는 실수(0xF5 → 245 ms)도 흔하다.

**5. "송신 N_OK = 전달 성공" 으로 믿는다.** B1·B3 처럼 수신자가 실패해도 송신자는 N_OK 일 수 있다. 끝-끝 확인은 위 계층(UDS 응답)의 일이다. 송신 성공 카운터만 보고 "링크 정상" 이라고 보고하면 안 된다.

**6. WAIT 중에 FF 데이터를 버퍼에 쓴다.** 수신 버퍼가 바쁜 이유는 누가 그 버퍼를 쓰고 있기 때문이다(게이트웨이 송신, 앱 파싱). FF 6 바이트는 따로 두고 CTS 때 복사한다. C3 게이트웨이 시험이 이것을 잡는다.

**7. 새 FF 를 무시한다.** 수신 중 새 FF 가 오면 이전 수신을 끝내고 새 것을 받는다(Linux 도 같다). 무시하면 테스터가 타임아웃 뒤 다시 보낸 요청을 놓친다. 이전 수신이 영영 끝나지 않을 수도 있다.

## 8. 면접 Q&A

<details><summary>Q1. 4 KB 를 CAN 으로 보내는 과정을 프레임 단위로 설명해 보세요.</summary>

송신자가 FF 를 보낸다. `1F FF` 면 길이 4095 이고 데이터 6 B 가 따라온다. 수신자는 버퍼가 있으면 FC CTS(BS, STmin)로 답한다. 송신자는 CF 를 SN 1, 2, …, 15, 0, … 으로 7 B 씩 보낸다. BS 개마다 다시 FC 를 기다린다(BS 0 이면 끝까지). CF 사이에는 STmin 이상을 둔다. 4095 B 는 CF 585 개다. 500 kbit/s, BS 0, STmin 0, 패딩이면 587 프레임, 130 ms 이고 최악 스터핑이면 158 ms 다.

</details>

<details><summary>Q2. 수신자가 느리면 어떻게 하나요? BS 와 STmin 은 어떻게 고르나요?</summary>

STmin 은 CF 하나를 처리하는 데 걸리는 시간(ISR + 복사 + 큐)을 덮어야 한다. 350 µs 면 0xF4(400 µs)다(연습 3). BS 는 수신 FIFO·버퍼 여유를 기준으로 고르고, 블록마다 "아직 살아 있다" 를 확인하는 효과도 있다. 0.5 ms 이상의 STmin 에서는 BS 를 작게 해도 손해가 없다. FC 왕복이 STmin 간격 하나를 대신하기 때문이다(4095 B: BS 8 이 402 ms, BS 0 이 422 ms). STmin 0 이면 BS 가 작을수록 FC 비용(BS 1 이면 1171 프레임)이 커진다. 순간적으로 못 받으면 FC.WAIT 다.

</details>

<details><summary>Q3. CF 하나가 사라지면 어떻게 되나요? 송신자는 압니까?</summary>

수신자는 다음 CF 의 SN 이 기대값과 달라 N_WRONG_SN 으로 메시지를 버린다. 마지막 CF 가 사라졌으면 N_Cr(모델 1 s) 뒤 N_TIMEOUT_Cr 이다. 송신자는 BS = 0 이면 **모르고** N_OK 로 끝낸다. BS > 0 이고 손실이 블록 중간이면, 블록 끝에서 FC 가 안 와 N_TIMEOUT_Bs 로 안다. ISO-TP 에는 재전송이 없다. 복구는 UDS 같은 위 계층의 응답 타임아웃과 재요청이 한다. CAN 자체는 깨진 프레임을 재전송하므로, 이런 손실은 수신 FIFO 오버런·게이트웨이·bus-off 에서 생긴다.

</details>

<details><summary>Q4. 같은 CF 가 두 번 들어오면요? 왜 그런 일이 생기죠?</summary>

두 번째 CF 의 SN 이 기대값보다 1 작으므로 N_WRONG_SN 으로 메시지 전체를 버린다(Linux 도 같다). CAN 에서 이중 수신은 실제로 생긴다. 송신자가 EOF 마지막 비트 근처에서 오류를 보고 재전송했는데, 일부 수신자는 이미 그 프레임을 유효로 받은 경우다(COMM-M04/M07). ISO-TP 는 이것을 고치지 않고 "실패" 로 바꾼다. 그래서 위 계층 재시도가 필요하다.

</details>

<details><summary>Q5. FC.WAIT 와 FC.OVFLW 의 차이, N_WFTmax 의 역할은?</summary>

OVFLW 는 "이 길이는 내 버퍼에 절대 안 들어간다" 다. FF 바로 뒤에만 쓰고, 송신자는 N_BUFFER_OVFLW 로 즉시 포기한다(Linux 는 EMSGSIZE). WAIT 는 "지금은 못 받으니 기다려" 다. 송신자는 WAIT 마다 N_Bs 를 다시 시작한다. 수신자가 WAIT 를 무한히 보내지 않도록 N_WFTmax 가 연속 WAIT 수를 제한한다. 넘으면 수신자는 N_WFT_OVRN 으로 포기하고, 송신자는 N_Bs 만료로 끝난다(B6: 수신 6001, 송신 13002 tick). 설계 조건은 WAIT 간격 < N_Bs, 간격 × N_WFTmax ≥ 최대 바쁜 시간이다.

</details>

<details><summary>Q6. 타이머 N_As, N_Bs, N_Cr 은 각각 언제 시작하고 무엇을 지키나요?</summary>

N_As 는 송신자가 프레임을 CAN 에 넣고 **전송 확인** 을 받기까지다. 버스가 막히거나 bus-off 면 만료된다(B9, 메일박스 abort). N_Bs 는 송신자가 FF 나 블록 마지막 CF 의 확인 뒤 **FC 를 받기까지** 이고, WAIT 를 받으면 다시 시작한다. N_Cr 은 수신자가 FC 를 보낸 뒤 또는 CF 를 받은 뒤 **다음 CF 까지** 다. 수신 측 N_Ar(FC 확인), 성능 요구 N_Br·N_Cs 도 있다고 알려져 있다(타임아웃 기본 1000 ms — 스펙 확인 권장). Linux 실측: FC 가 없으면 1.01 s, 마지막 CF 가 없으면 약 1 s 에 끝났다.

</details>

<details><summary>Q7. 수신 중에 새 FF 나 SF 가 오면? 기다리지 않던 CF 나 FC 는?</summary>

새 FF·SF 는 진행 중이던 수신을 끝내고(N_UNEXP_PDU) 새 메시지로 처리한다. 테스터가 타임아웃 뒤 새 요청을 보낸 상황을 살리려는 규칙이다. Linux 도 새 FF(A0..A9)와 SF(11 22 33)를 받아 냈다. 기다리지 않던 CF(IDLE 상태)와, FC 를 기다리지 않을 때 온 FC 는 무시한다. 형식이 틀린 PCI(SF_DL 0, FF_DL < 8, 종류 4..F)도 무시한다. 근거는 ISO 15765-2 의 예상 밖 N_PDU 처리 표다(스펙 확인 권장).

</details>

<details><summary>Q8. 게이트웨이를 설계한다면 버퍼는 몇 개, 무엇을 조심하나요?</summary>

store-and-forward 면 최소 한 메시지 분(4095 B)이 필요하다. zero-copy 로 받은 버퍼를 그대로 다른 쪽에 보내면 RAM 은 아끼지만, 보내는 동안 그 연결은 막힌다. FF 는 WAIT 로 버틸 수 있다. SF 는 WAIT 할 수 없어 버려진다(C4). 그래서 버퍼를 2 개 이상 두거나 SF 큐를 따로 둔다. N_WFTmax × WAIT 간격은 반대편 최장 전송 시간(상대 STmin·버스 부하 포함)보다 커야 한다. 또 한쪽 ID 가 낮으면 다른 연결의 FF 가 굶을 수 있다. 연결별 메일박스·우선순위를 정하고, 큐 하나만 쓸 때의 우선순위 역전(COMM-M05)도 점검한다.

</details>

<details><summary>Q9. ISO-TP 를 CAN FD 로 옮기면 무엇이 바뀌나요?</summary>

프레임 데이터가 최대 64 B 라 SF 가 커진다. 8 B 를 넘는 SF 는 SF_DL escape(첫 바이트 0x00 + 길이 바이트)를 쓴다고 알려져 있다. CF 는 63 B 씩 실린다. 2016판은 4095 B 를 넘는 메시지용 FF_DL escape(32비트)도 정의한다(스펙 확인 권장). 같은 4095 B 라도 프레임 수가 약 1/9 이 되고, 데이터 구간을 BRS 로 빠르게 보내면 시간이 크게 준다(COMM-M08). DLC 가 이산값(12, 16, 20, 24, 32, 48, 64)이라 패딩 규칙이 더 중요해진다. Linux 에서는 `isotpsend -L 72:64:0` 같은 링크 옵션으로 FD 를 켠다.

</details>

## 9. 추가 연습

### 연습 1 — 프레임 수 닫힌 식

길이 L, BS 가 주어질 때 FF·CF·FC 를 모두 센 프레임 수의 닫힌 식을 세우고, 프레임을 하나씩 세는 분할기와 L = 1..4095 × BS = 0..32 전체에서 비교하라. 4095 B 의 BS = 0, 8, 32, 1 값은?

<details><summary>답 보기</summary>

L ≤ 7 이면 1, 아니면 1 + ncf + (BS ? ⌈ncf/BS⌉ : 1), ncf = ⌈(L−6)/7⌉ 이다. 135,135 경우 모두 일치한다. 4095 B 는 BS 0 → 587, BS 8 → 660, BS 32 → 605, BS 1 → 1171 프레임이다. BS 1 은 CF 마다 FC 가 붙어 프레임이 두 배가 된다.

</details>

### 연습 2 — extended addressing 의 비용

extended addressing 은 모든 프레임의 첫 바이트를 대상 주소(N_TA)로 쓴다. 그래서 SF 최대 6 B, FF 데이터 5 B, CF 데이터 6 B 가 된다. 4095 B(BS 0, STmin 0, 500 kbit/s, 패딩)의 프레임 수와 시간을 normal addressing 과 비교하라.

<details><summary>답 보기</summary>

CF = ⌈4090/6⌉ = 682 이므로 1 + 682 + 1 = **684 프레임**, 151.85 ms 다. normal 은 587 프레임, 130.31 ms 이니 **+16.5 %** 다. 게이트웨이 뒤 서브넷 주소가 필요할 때(BMW 계열 진단이 대표 예로 알려짐) 쓰는 방식이고, 대가는 프레임당 1 바이트다.

</details>

### 연습 3 — 처리 시간에 맞는 STmin 고르기

수신자가 CF 하나에 t µs 가 필요하다. t 이상을 보장하는 **가장 짧은** STmin 바이트를 고르는 함수를 쓰고, 0..127000 µs 전체에서 모든 유효 STmin 값을 훑는 brute force 와 비교하라.

<details><summary>답 보기</summary>

t = 0 → 0x00, t ≤ 900 → 0xF0 + ⌈t/100⌉, t ≤ 127000 → ⌈t/1000⌉, 그 이상은 표현할 수 없으니 0x7F 를 주고 BS·WAIT 로 보완한다. 350 µs → 0xF4, 901 µs → 0x01(1 ms), 1001 µs → 0x02 다. 127,001 경우에서 brute force 와 불일치 0 이다. 901 µs 가 1 ms 로 뛰는 것처럼, µs 범위는 900 에서 끝나고 그 위는 ms 단위라 해상도가 거칠다.

</details>

### 연습 4 — Linux 실제 캡처 디코드

demo_linux.sh 가 찍은 K2 의 `candump -L` 9 줄(타임스탬프 포함)을, 본문과 다른 작은 재조립기로 디코드하라. 40 바이트가 i × 7 인지, 블록 안 CF → CF 간격이 STmin(500 µs) 이상인지, FC → 첫 CF 간격이 얼마인지 확인하라.

<details><summary>답 보기</summary>

40/40 바이트가 맞다. 블록 안 최소 CF 간격은 **557 µs**(≥ 500)이고, FC → 첫 CF 는 최대 **2 µs** 다. Linux 송신자는 FC 뒤 첫 CF 에 STmin 을 적용하지 않는다. 이 레슨 모델의 정의와 Part C 공식이 이 관찰에 근거한다. 타임스탬프는 초·마이크로초를 정수로 따로 읽는다. double 하나로 읽으면 1.79e9 초 근처에서 해상도가 약 0.24 µs 로 떨어진다.

</details>

### 연습 5 — UDS 플래시 다운로드 시간

256 KiB 이미지를 UDS TransferData(SID 0x36 + 블록 순번 1 B + 데이터 4093 B = ISO-TP 4095 B)로 보낸다. 응답 `76 xx` 는 SF 하나다. 500 kbit/s, 패딩에서 (a) BS 0 / STmin 0, (b) BS 8 / STmin 1 ms, (c) BS 0 / 최악 스터핑의 ISO-TP 시간 합계를 구하라(ECU 의 플래시 쓰기 시간은 제외).

<details><summary>답 보기</summary>

요청은 65 개다(64 × 4093 + 마지막 192 B). (a) **8.36 s**, (b) **42.13 s**, (c) **10.17 s** 다. ECU 가 STmin 1 ms 를 요구하면 플래싱이 5 배 느려진다. 실제 부트로더는 블록마다 플래시를 쓰느라 응답이 늦고(NRC 0x78 responsePending), 이 시간이 더해진다(AUTO-M06). 그래서 FD 나 이더넷 진단(DoIP, AUTO-M01)으로 옮겨 간다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 8/8 passed
```

실제 출력:

```text
ex1 frames(4095): BS=0 587, BS=8 660, BS=32 605, BS=1 1171; formula vs counter mismatches 0 (135135 cases)
ex2 4095 B @500k BS=0: normal 130.31 ms (587 frames), extended 151.85 ms (684 frames), +16.5%
ex3 stmin_for_us: 350us->0xF4 901us->0x01 950us->0x01 1001us->0x02 200ms->0x7F; brute-force mismatches 0
ex4 Linux capture K2: reassembled 40/40 B ok=1, min CF->CF gap in block 557 us (STmin 500), max FC->first CF 2 us
ex5 256 KiB in 65 TransferData (last 192 B data) @500k: BS=0/STmin=0 8.36 s, BS=8/STmin=1ms 42.13 s, BS=0 worst-stuff 10.17 s (ECU flash time not included)
8/8 passed
```

## 10. 핵심 정리

- PCI 상위 니블: 0 SF(len ≤ 7), 1 FF(12비트 길이 ≤ 4095 + 6 B), 2 CF(SN 1..F, 0, … + 7 B), 3 FC(FS·BS·STmin). FS 는 0 CTS, 1 WAIT, 2 OVFLW 이고 나머지는 중단이다.
- STmin: 0x00–0x7F ms, 0xF1–0xF9 = 100–900 µs, 예약값은 127 ms. 블록 안 "CF 끝 → 다음 CF 시작" 간격이다. FC 뒤 첫 CF 는 바로 보낸다(Linux 관찰 = 이 모델).
- 타이머: N_As(송신 확인), N_Bs(FC 대기, WAIT 마다 재시작), N_Cr(CF 대기). N_WFTmax 가 연속 WAIT 를 제한한다.
- 끝-끝 ACK 가 없다. 수신 실패(SN 틀림, N_Cr)에도 송신은 N_OK 일 수 있다. 복구는 위 계층(UDS) 몫이다.
- 수신 중 새 FF/SF 면 이전 것은 N_UNEXP_PDU 로 끝내고 새로 시작한다. 기다리지 않던 CF·FC 는 무시한다.
- 4095 B @500k: 587 프레임, 130 ms(스터프 0) ~ 158 ms(최악), 효율 50 %. STmin 1 ms 면 714 ms, 127 ms 면 74 s. 버스 부하 ρ 에서는 약 ÷(1−ρ) 다.
- 게이트웨이: zero-copy 버퍼는 송신 중 잠그고 FF 는 WAIT 로 버틴다. SF 는 버려지므로 버퍼 2 개 이상이 필요하다.

## 참고자료

- ISO 15765-2:2016 Road vehicles — Diagnostic communication over Controller Area Network (DoCAN) — Part 2: Transport protocol and network layer services (PCI 형식, FC, 타이머, N_Result, 예상 밖 N_PDU 처리 표. 이 레슨에서 "스펙 확인 권장" 으로 표시한 부분의 원문).
- Linux kernel documentation — ISO 15765-2 (CAN_ISOTP) protocol: <https://docs.kernel.org/networking/iso15765-2.html> 와 소스 `net/can/isotp.c` (이 레슨의 기준 구현. kernel 7.0 의 can_isotp 모듈로 실측).
- can-utils (isotpsend, isotprecv, isotpdump, candump): <https://github.com/linux-can/can-utils>
- ISO 14229-1 Unified Diagnostic Services (UDS) — ISO-TP 위의 진단 서비스(다음 단계: AUTO-M02, AUTO-M06).
- R. I. Davis, A. Burns, R. J. Bril, J. J. Lukkien, "Controller Area Network (CAN) schedulability analysis: Refuted, revisited and revised", Real-Time Systems 35(3), 2007 — 최악 스터프 비트 수(Part C 의 worst 열).
- 관련 레슨: COMM-M04 (프레임 비트 수·스터핑), COMM-M05 (ID 중재·굶주림), COMM-M07 (오류 카운터·bus-off), COMM-M08 (CAN FD), COMM-M09 (SocketCAN·vcan), COMM-M14 (stop-and-wait ARQ — ISO-TP 에 없는 재전송), COMM-B10 (프레임 파서), AUTO-M01/M02/M06 (DoIP·UDS·부트로더, 다음 단계)
