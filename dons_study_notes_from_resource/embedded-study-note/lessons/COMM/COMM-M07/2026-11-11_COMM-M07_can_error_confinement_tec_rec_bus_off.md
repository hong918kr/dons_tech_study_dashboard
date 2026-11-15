---
id: COMM-M07
time: 90분
updated: 2026-11-11
---
# COMM-M07 CAN 에러 처리: TEC/REC와 error-passive·bus-off

## 학습 목표

- CAN 의 fault confinement 규칙(송신 에러 TEC +8, 수신 에러 REC +1, 성공 시 −1, 먼저 본 수신자 +8, 연속 dominant 벌점)을 이벤트 함수로 구현하고, **고장 송신 노드가 정확히 16번째 에러에 passive, 32번째에 bus-off** 가 되는 것을 코드로 확인한다.
- error-active / error-passive / bus-off 상태 기계와 두 가지 예외(혼자 남은 passive 노드의 ACK 에러, 중재 중 stuff error)를 설명하고, **"혼자 남은 노드는 bus-off 에 안 간다", "수신만 하는 노드는 bus-off 에 못 간다"** 를 시나리오로 보인다.
- 에러 플래그 중첩(superposition)을 비트 단위로 시뮬레이션해서 rule 2 (+8) 가 **고장 난 수신자를 멀쩡한 송신자보다 먼저 쫓아내는 장치**임을 수치로 보인다 (rule 2 를 끄면 반대가 된다).
- bus-off 복구 "11 연속 recessive 128회" 를 비트 카운터로 구현하고, 빈 버스 1408 비트 / 꽉 찬 버스 128 프레임이라는 차이를 µs 로 계산한다.
- 드라이버 쪽: 상태 변화 로그, Linux `ip -details -statistics` 형식 출력, bus-off 재시작 정책(백오프 + 최대 횟수)을 짜고 babbling idiot 에 대해 정책별 결과를 비교한다.

## 1. 문제

차량·로봇 CAN 버스 브링업에서 "노드 하나가 자꾸 bus-off 돼요", "`candump` 에 아무것도 안 와요, 근데 `ip -details link` 를 보니 ERROR-PASSIVE 에 tx 128 이에요" 는 매주 나오는 보고다. 면접에서도 "TEC/REC 규칙 말해 봐", "bus-off 에서 어떻게 돌아오나", "노드가 혼자 있으면 어떻게 되나" 는 CAN 질문의 단골 꼬리다. 숫자 몇 개(8, 1, 127, 255, 128×11)를 외우는 데서 끝나지 않고, **왜 그 숫자인지와 그 결과로 버스에서 무슨 일이 벌어지는지**를 말할 수 있어야 한다.

CAN 은 멀티마스터 버스라서 한 노드가 고장 나 계속 에러 플래그(dominant 6비트)를 쏘면 **모든** 노드의 통신이 막힌다. 그래서 각 노드는 자기 에러 카운터 두 개를 들고 스스로를 단계적으로 조용히 만든다(fault confinement). dominant = 0, recessive = 1, 버스 = 모든 드라이버의 AND 는 COMM-M04/M05 와 같다.

| # | 규칙 (ISO 11898-1 / Bosch CAN 2.0B "Fault Confinement" 요지 — 코너는 스펙 확인 권장) | 이 레슨 함수 |
|---|---|---|
| 1 | 수신자가 에러를 감지 → REC += 1 | `fc_rx_error` |
| 2 | 수신자가 **자기 에러 플래그를 보낸 뒤 첫 비트가 dominant** → REC += 8 | `fc_penalty8(n, false)` |
| 3 | 송신자가 에러 플래그를 보냄 → TEC += 8. 예외 ① error-passive 송신자의 ACK 에러인데 passive 플래그 동안 dominant 를 못 봄 ② 중재 구간에서 recessive 로 보낸 stuff bit 를 dominant 로 읽은 stuff error — 둘 다 TEC 그대로 | `fc_tx_error` |
| 4·5 | active 에러 플래그(또는 overload 플래그) 송신 중 bit error → 송신자 TEC / 수신자 REC += 8 | `fc_penalty8` |
| 6 | 플래그 뒤 dominant 는 7개까지 봐준다. active 플래그면 **연속 14번째**(= 플래그 6 + 8), passive 플래그면 플래그 뒤 8번째 dominant, 그 뒤 8개마다 → TEC/REC += 8 | 비트 시뮬레이터 |
| 7 | 성공 송신 (ACK 받고 EOF 까지 에러 없음) → TEC −= 1 (0 이면 그대로) | `fc_tx_ok` |
| 8 | 성공 수신 → REC 1..127 이면 −1, **127 초과면 119..127 중 한 값으로** (이 모델 = 127, STM32 bxCAN RM 은 다른 값을 적고 있을 수 있음 — RM 확인 권장) | `fc_rx_ok` |
| 상태 | error-active: TEC ≤ 127 && REC ≤ 127 / error-passive: TEC > 127 \|\| REC > 127 / bus-off: TEC > 255 | `fc_state_of` |
| 복구 | bus-off 노드는 **11 연속 recessive 를 128번** 본 뒤 TEC = REC = 0 으로 error-active (컨트롤러에 따라 host 요청이 먼저 필요) | `fc_recovery_bit` |

모델 정의 (스펙이 아니라 이 레슨의 선택): REC 는 8비트 레지스터처럼 255 에서 포화, WARNING 레벨 = TEC 또는 REC ≥ 96 (Linux `enum can_state` 주석과 STM32 EWGF 가 쓰는 값, Bosch 스펙의 "96 근처면 심하게 교란된 버스" 메모가 출처 — 확인 권장).

```svg
<svg viewBox="0 0 740 336" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">노드 상태 기계 — 카운터가 문턱을 넘으면 노드가 스스로 목소리를 줄인다</text>
  <rect x="20" y="60" width="200" height="120" rx="10" class="f-ok" opacity=".35" stroke="currentColor"/>
  <rect x="270" y="60" width="200" height="120" rx="10" class="f-warn" opacity=".35" stroke="currentColor"/>
  <rect x="520" y="60" width="200" height="120" rx="10" class="f-mut" opacity=".5" stroke="currentColor"/>
  <rect x="32" y="128" width="176" height="42" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="120" y="82" font-weight="bold">ERROR-ACTIVE</text>
    <text x="120" y="102">TEC ≤ 127 &amp;&amp; REC ≤ 127</text>
    <text x="120" y="120" font-size="12">active 플래그 = dominant 6</text>
    <text x="120" y="146" font-size="12">WARNING: TEC 또는 REC ≥ 96</text>
    <text x="120" y="162" font-size="12">(Linux/bxCAN 표시용, 동작 같음)</text>
    <text x="370" y="82" font-weight="bold">ERROR-PASSIVE</text>
    <text x="370" y="102">TEC &gt; 127 || REC &gt; 127</text>
    <text x="370" y="122" font-size="12">passive 플래그 = recessive 6</text>
    <text x="370" y="140" font-size="12">→ 남의 프레임을 못 깨뜨림</text>
    <text x="370" y="158" font-size="12">송신 뒤 suspend 8비트 (양보)</text>
    <text x="620" y="82" font-weight="bold">BUS-OFF</text>
    <text x="620" y="102">TEC &gt; 255</text>
    <text x="620" y="122" font-size="12">송신·ACK·에러 플래그 없음</text>
    <text x="620" y="140" font-size="12">버스 비트만 본다</text>
    <text x="620" y="158" font-size="12">(11 연속 recessive 세기)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.6">
    <path d="M220 90 L268 90"/><path d="M262 85 L268 90 L262 95"/>
    <path d="M270 150 L222 150"/><path d="M228 145 L222 150 L228 155"/>
    <path d="M470 120 L518 120"/><path d="M512 115 L518 120 L512 125"/>
    <path d="M620 180 L620 240 L120 240 L120 182"/><path d="M115 188 L120 182 L125 188"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="245" y="52">TEC 또는 REC</text><text x="245" y="66">≥ 128</text>
    <text x="245" y="170">둘 다</text><text x="245" y="184">≤ 127</text>
    <text x="494" y="108">TEC ≥ 256</text>
    <text x="370" y="232">11 연속 recessive × 128회 (최소 1408 비트) → TEC = REC = 0</text>
    <text x="370" y="258">auto (STM32 ABOM=1 류) 또는 host 요청 뒤 시작 (Linux restart-ms / ip link … restart)</text>
  </g>
  <text x="10" y="286" fill="currentColor" font-size="12">REC 로는 bus-off 에 못 간다 (bus-off 조건은 TEC 만).</text>
  <text x="10" y="304" fill="currentColor" font-size="12">active → bus-off 직행도 없다: 한 번에 최대 +8 이라 반드시 passive 를 지난다.</text>
  <text x="10" y="322" fill="currentColor" font-size="12">bus-off 는 래치: 카운터 이벤트로는 못 나오고 복구 절차로만 나온다 (fc_update 가 bus-off 면 바로 return).</text>
</svg>
```

아래는 이 레슨 코드가 계산한 실제 TEC 궤적이다 (가로 = 송신 시도 번호, `fc_*` 함수에 이벤트를 넣어 얻은 값).

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">TEC 궤적: 혼자 남은 노드 vs 송신부 고장 노드 vs "에러 1번 + 성공 1번" 반복</text>
  <g stroke="currentColor" fill="none" opacity=".35" stroke-dasharray="3 3">
    <line x1="60" y1="50" x2="700" y2="50"/><line x1="60" y1="170" x2="700" y2="170"/><line x1="60" y1="200" x2="700" y2="200"/>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="60" y1="290" x2="700" y2="290"/><line x1="60" y1="40" x2="60" y2="290"/>
  </g>
  <g fill="currentColor" text-anchor="end">
    <text x="55" y="54">256</text><text x="55" y="174">128</text><text x="55" y="204">96</text><text x="55" y="294">0</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="60" y="306">0</text><text x="188" y="306">16</text><text x="316" y="306">32</text><text x="444" y="306">48</text>
    <text x="572" y="306">64</text><text x="700" y="306">80</text><text x="380" y="324">송신 시도 번호</text>
  </g>
  <g fill="currentColor"><text x="610" y="46">bus-off</text><text x="610" y="166">passive</text><text x="66" y="196">warning (96)</text></g>
  <polyline class="f-mut" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" points="60,290 188,170 700,170"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="60,290 188,170 316,50"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.2" points="60,290 68,282.5 76,283.4 84,275.9 92,276.9 100,269.4 108,270.3 116,262.8 124,263.8 132,256.2 140,257.2 148,249.7 156,250.6 164,243.1 172,244.1 180,236.6 188,237.5 196,230.0 204,230.9 212,223.4 220,224.4 228,216.9 236,217.8 244,210.3 252,211.2 260,203.8 268,204.7 276,197.2 284,198.1 292,190.6 300,191.6 308,184.1 316,185.0 324,177.5 332,178.4 340,170.9 348,171.9 356,164.4 364,165.3 372,157.8 380,158.8 388,151.2 396,152.2 404,144.7 412,145.6 420,138.1 428,139.1 436,131.6 444,132.5 452,125.0 460,125.9 468,118.4 476,119.4 484,111.9 492,112.8 500,105.3 508,106.2 516,98.8 524,99.7 532,92.2 540,93.1 548,85.6 556,86.6 564,79.1 572,80.0 580,72.5 588,73.4 596,65.9 604,66.9 612,59.4 620,60.3 628,52.8 636,53.8 644,46.2"/>
  <circle cx="316" cy="50" r="4" class="f-warn" stroke="currentColor"/><circle cx="644" cy="46" r="4" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="200" y="110">굵은 선: 송신부 고장 (매번 bit error)</text>
    <text x="200" y="124">→ 16번째 passive, 32번째 bus-off</text>
    <text x="400" y="188">점선: 혼자 남은 노드 — TEC 128 에서 멈춤</text>
    <text x="420" y="250">가는 선: 에러·성공 교대 (+8, −1)</text>
    <text x="420" y="264">→ 37번째 에러 = 73번째 시도에서 bus-off</text>
  </g>
</svg>
```

### Part A — 카운터 규칙과 상태 전이

```c
typedef enum { ST_ACTIVE, ST_PASSIVE, ST_BUSOFF } CanState;
typedef enum { ERR_BIT, ERR_STUFF, ERR_CRC, ERR_FORM, ERR_ACK } ErrType;
CanState fc_state_of(unsigned tec, unsigned rec);
void fc_update(FcNode *n);                     /* 카운터 → 상태, bus-off 래치, 전이 횟수 */
void fc_tx_error(FcNode *n, ErrType e, bool dom_in_pflag, bool arb_rec_stuff);
void fc_rx_error(FcNode *n);   void fc_penalty8(FcNode *n, bool is_tx);
void fc_tx_ok(FcNode *n);      void fc_rx_ok(FcNode *n);
```

- 문턱: 127/128, 255/256 경계, REC 만으로는 bus-off 불가, bus-off 중 이벤트 무시, REC 128 → 성공 1번 → 127 (active 복귀), 포화 255.
- 고장 송신 노드: passive 가 몇 번째 에러인지, bus-off 가 몇 번째인지 **기계가 세게** 하라.
- "에러 1번 + 성공 m번" 반복이면 몇 번째 에러에서 passive/bus-off? 식을 세우고 m = 0..9 전부 시뮬레이션과 맞춰라.
- 고정 시드 랜덤 이벤트 600000개: 이벤트마다 기대 TEC/REC/상태를 규칙표로 따로 계산해 대조, 수신만 하는 노드는 bus-off 0회.

### Part B — 예외, 에러 플래그 중첩, bus-off 복구

1. **예외 ①②** 를 넣고 테스트: passive + ACK 에러 + 플래그 중 dominant 없음 → TEC 그대로, dominant 봤으면 +8, active 면 +8. 중재 stuff error → 그대로.
2. **에러 플래그 중첩 시뮬레이터** `flag_superpose(nodes, k, stuck_len, out, wave, cap)`: 노드마다 (송신자?, passive?, 플래그 시작 비트 또는 "6 연속 dominant 를 stuff error 로 보고 뒤따름") 를 주면 비트별 버스 레벨과 노드별 TEC/REC 증가량을 낸다. passive 플래그는 "같은 레벨 6 연속을 볼 때까지", 플래그 뒤에는 recessive 를 기다리며 dominant 를 센다 (rule 2, rule 6), 그다음 delimiter 8.
   - 모두 같은 비트에서 감지 → 14비트, 아무도 +8 없음 / 수신자 하나가 먼저 → 20비트, 그 수신자만 +9 / 고장 노드가 dominant 를 13·14·21·22·30 비트 밀기 → 7개까지 봐주고 14번째부터 8개마다 +8 / passive 수신자 혼자 → 버스에 안 보임 / passive 송신자 + 늦게 본 active 수신자 → passive 플래그 9비트.
3. **복구**: `fc_recovery_bit(n, bit)` — 11 연속 recessive 를 **센 뒤 0부터 다시**, dominant 면 0, 128회째에 TEC = REC = 0. auto 가 아니면 `fc_request_recovery()` 전 비트는 안 센다. 빈 버스에서 정확히 1408 비트, "10 rec + 1 dom" 반복이면 영원히 못 나옴.

### Part C — 프레임 수준 버스 시나리오와 드라이버

프레임 단위 슬롯 엔진(주어짐: 중재 = 가장 낮은 ID, passive 송신자의 suspend transmission, 스터핑된 실제 비트열 방출, bus-off 노드는 그 비트로 복구 카운트)에 다음을 채운다.

- `Outcome bus_attempt(Bus *b, int t)` — 시도 하나의 결과 판정과 모든 참여 노드의 카운터 갱신 (5. 풀이의 표).
- `LState lstate_of(const FcNode *f)`, `void drv_poll(Drv *d, Node *x, uint64_t now)` — 레벨 변화 로그 `"ERROR-WARNING -> ERROR-PASSIVE (tx 128 rx 0)"`, Linux `can_device_stats` 와 같은 통계, bus-off 재시작 예약(restart_ms × 2^(n−1) 백오프, max_restarts 넘으면 STOPPED).
- `int drv_format_ip(const Drv *d, const FcNode *f, char *buf, size_t cap)` — `ip -details -statistics link show can0` 의 CAN 부분 형식, 잘리면 −1.
- 시나리오: C1 혼자 남은 노드, C2 송신부 고장 + 정상 2개, C3 고장 수신자, C4 수신 전용 고장 노드 5000 프레임, C5 전역 교란 1회, C6 복구(빈 버스 1408 비트 / 꽉 찬 버스 128 프레임) + 2노드 버스의 함정, C7 babbling idiot (자동 복구 vs 드라이버 정책), C8 **멀쩡한 프레임으로** 떠드는 노드, C9 출력 형식.

## 2. Problem (English)

> **CAN fault confinement.**
>
> Implement the error-counter rules of a classic CAN node. `fc_tx_error(n, err, dom_in_pflag, arb_rec_stuff)` adds 8 to TEC except (a) an error-passive transmitter's ACK error when no dominant bit was seen during its passive error flag and (b) a stuff error during arbitration on a recessive stuff bit that was read dominant. `fc_rx_error` adds 1 to REC (saturating at 255), `fc_penalty8` adds 8 (rule 2/4/5/6), `fc_tx_ok` decrements TEC if non-zero, `fc_rx_ok` decrements REC in 1..127 and sets it to 127 if it was above 127. State: error-active if TEC ≤ 127 and REC ≤ 127, error-passive otherwise, bus-off when TEC > 255; bus-off is latched until `fc_recovery_bit()` has seen 128 occurrences of 11 consecutive recessive bits (counting starts at bus-off entry if `auto_recover`, else after `fc_request_recovery()`), then TEC = REC = 0.
>
> Then: (1) simulate error-flag superposition bit by bit and report each node's counter increments; (2) drive a frame-level bus with faulty nodes and report state changes like a Linux CAN driver, with a restart policy.
>
> Examples: a node whose transmitter corrupts every frame becomes error-passive at its 16th error and bus-off at its 32nd. A node alone on the bus (no ACK) stops at TEC = 128, error-passive, forever. A recovering node on an idle bus needs exactly 1408 recessive bits; on a 100 % loaded bus, exactly 128 frames.
>
> Follow-ups: Why does the receiver that detects an error first get +8? Can a receive-only node go bus-off? Who protects the bus from a node that sends *valid* frames too often?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열네 군데: Part A 7개(`fc_state_of`, `fc_update`, 이벤트 함수 5개), Part B 3개(`fc_request_recovery`, `fc_recovery_bit`, `flag_superpose`), Part C 4개(`bus_attempt`, `lstate_of`, `drv_poll`, `drv_format_ip`). 버스 슬롯 엔진, 비트 방출(스터핑 포함), 큐, 로그 버퍼, 출력 도우미는 주어진다. 스텁은 카운터를 하나도 안 바꾸고 모든 시도를 "성공"으로 돌려주므로 모든 루프가 상한에서 끝난다. 처음 실행 (실제 출력 앞부분):

```text
FAIL starter.c:355  fc_state_of(128, 0) == ST_PASSIVE && fc_state_of(0, 128) == ST_PASSIVE
FAIL starter.c:356  fc_state_of(255, 0) == ST_PASSIVE && fc_state_of(256, 0) == ST_BUSOFF
FAIL starter.c:357  fc_state_of(0, 255) == ST_PASSIVE
broken TX: passive after 40 errors (TEC 0)
FAIL starter.c:363  k - 1 == 16 && f.tec == 128 && f.n_passive == 1
…
8/87 passed
```

(exit 1. 통과한 8개는 스텁이 우연히 맞는 경우 — 예: `fc_state_of(0,0) == ACTIVE`, "m ≥ 8 이면 bus-off 안 감", C8 의 "멀쩡한 babbler 는 TEC 0".)

## 4. 힌트

<details><summary>힌트 1 — 상태는 카운터의 함수, 단 bus-off 만 래치</summary>

`fc_state_of` 는 순수 함수: `tec >= 256` → BUSOFF, `tec >= 128 || rec >= 128` → PASSIVE, 나머지 ACTIVE. `fc_update` 는 "지금 bus-off 면 아무것도 안 함" 을 맨 앞에 둔다. 그래야 bus-off 중 들어온 `fc_tx_ok` 같은 이벤트가 상태를 되돌리지 못한다. bus-off 로 들어가는 순간에 복구 카운터(run, occ)를 0 으로, `recov_armed = auto_recover`.

</details>

<details><summary>힌트 2 — 예외 ①은 "혼자 남은 노드" 를 위한 것</summary>

ACK 에러 = 아무도 ACK slot 을 당기지 않았다 = 받는 노드가 없다. 이때 TEC 가 계속 올라 bus-off 가 되면, 케이블이 빠졌다 다시 꽂힐 때마다 그 노드는 처음부터 복구해야 한다. 그래서 passive 송신자의 ACK 에러는 (passive 플래그 동안 dominant 를 못 봤다면 = 정말 아무도 없다면) 세지 않는다. 결과: TEC 는 128 에서 멈춘다. 조건 세 개를 모두 써라: `e == ERR_ACK && state == PASSIVE && !dom_in_pflag`.

</details>

<details><summary>힌트 3 — 플래그 중첩은 노드마다 작은 상태 기계</summary>

WAIT → FLAG → AFTER → DELIM → DONE. 매 비트: ① 버스 = (stuck 이면 0) AND (FLAG 중인 active 노드면 0) ② 노드마다 진행. WAIT 은 `t + 1 == detect` 또는 "지금까지 연속 dominant 6" 이면 다음 비트부터 FLAG (시작할 때 TX +8 / RX +1). active FLAG 는 6비트 세면 끝, passive FLAG 는 "버스가 같은 레벨인 연속 비트 수" 가 6 이 되면 끝. AFTER 에서 dominant 면 d++ (d == 1 이고 수신자면 +8, d % 8 == 0 이면 역할 카운터 +8), recessive 면 DELIM 시작(그 비트가 delimiter 1번째).

</details>

<details><summary>힌트 4 — 복구 카운터는 "11개 모이면 1회, 그리고 0부터"</summary>

`run` 을 0 으로 되돌리지 않고 `run >= 11` 일 때마다 세면 빈 버스에서 11 + 127 = 138 비트 만에 "복구" 된다 (연습 3). dominant 비트는 run 을 0 으로. 128회째 비트에서 TEC = REC = 0, 상태 ACTIVE, `n_recovered++`.

</details>

<details><summary>힌트 5 — bus_attempt 는 표 하나</summary>

먼저 참여자(present, bus-off 아님)를 훑어 "active 고장 수신자가 있나", "ACK 할 정상 수신자가 있나" 를 본다. 결과 우선순위: 송신자 고장 > active 고장 수신자 > 전역 교란 > ACK 없음 > 성공. 송신자: 성공이면 `fc_tx_ok`, 아니면 `fc_tx_error(ERR_ACK 또는 ERR_BIT)`. 수신자: 프레임이 끝까지 갔으면(성공 또는 ACK 에러) 정상 노드 `fc_rx_ok`, 고장 노드 `fc_rx_error`; 프레임이 깨졌으면 모두 `fc_rx_error`, 그리고 **시도 전에 active 였던** 고장 수신자만 `fc_penalty8(…, false)`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 목적 | 한 노드의 고장이 버스 전체를 막지 않도록, 노드가 **스스로** 단계적으로 물러난다 (active → passive → bus-off) |
| 2. 카운터 | 송신 +8 / 수신 +1 / 성공 −1. 8:1 비대칭 = "송신자가 자기 고장일 확률이 높다" + "에러 1번을 성공 8번으로 갚아야 한다" |
| 3. 문턱 | 128 (passive: 남의 프레임을 못 깨뜨림), 256 (bus-off: 버스에서 빠짐), 96 (warning, 보고용) |
| 4. 예외·보정 | 혼자 남은 노드의 ACK 에러, rule 2 (+8: 먼저 본 수신자), 연속 dominant 벌점 |
| 5. 복구 | 11 연속 recessive × 128, 컨트롤러별 auto vs host 요청, 펌웨어 재시작 정책 |
| 6. 검증 | 16/32 를 기계로 세기, 시나리오 테스트, 드라이버 로그 |

### Part A — 규칙을 그대로 코드로

이벤트 함수는 전부 "bus-off 면 return → 카운터 변경 → `fc_update`" 모양이다. 송신 에러만 예외 조건 두 개가 붙는다:

```c
bool exc1 = e == ERR_ACK && n->state == ST_PASSIVE && !dom_in_pflag;
bool exc2 = e == ERR_STUFF && arb_rec_stuff;
if (!exc1 && !exc2) n->tec = (uint16_t)(n->tec + 8);
fc_update(n);
```

TEC 는 `uint16_t` 다. bus-off 진입 값 256 을 담아야 하고(8비트면 0 으로 돌아간다), +8 이 한 번에 248 → 256 으로 넘어가기 때문이다. 실제 STM32 bxCAN 의 TEC 필드는 8비트라 bus-off 는 별도 BOFF 비트로만 보인다 (연습 2, RM 확인 권장).

**16 과 32 를 기계로 세기.** 매번 bit error 를 내는 송신 노드 (실제 출력):

```text
broken TX: passive after 16 errors (TEC 128)
broken TX: bus-off after 32 errors (TEC 256)
```

| 에러 # | 1 | … | 12 | … | 15 | **16** | … | 31 | **32** |
|---|---|---|---|---|---|---|---|---|---|
| TEC | 8 | … | 96 (warning) | … | 120 | **128 → passive** | … | 248 | **256 → bus-off** |

**성공이 섞이면.** "에러 1번, 성공 m번" 을 반복하면 j 번째 에러 직후 TEC = 8j − m(j − 1) (m < 8 이면 0 바닥에 안 닿는다). 이것이 L 이상이 되는 첫 j = ⌈(L − m)/(8 − m)⌉. m ≥ 8 이면 TEC 는 8 과 0 사이를 오가며 영원히 active. 코드가 m = 0..9 를 시뮬레이션해 식과 대조한 실제 출력:

```text
  1 error + 0 ok: passive at error  16 (formula  16), bus-off at error  32 (formula  32)
  1 error + 1 ok: passive at error  19 (formula  19), bus-off at error  37 (formula  37)
  1 error + 4 ok: passive at error  31 (formula  31), bus-off at error  63 (formula  63)
  1 error + 7 ok: passive at error 121 (formula 121), bus-off at error 249 (formula 249)
  1 error + 8 ok: passive at error   0 (formula   0), bus-off at error   0 (formula   0)
```

즉 **송신 에러율 1/9 이 경계**다: 9번 중 1번 이하로 실패하는 노드는 영원히 버티고, 그보다 자주 실패하면 언젠가 bus-off. 연습 1 이 이것을 길이 12 이하 모든 주기 패턴(8190개)으로 증명한다 — "결국 bus-off ⇔ 8E &gt; S".

**랜덤 대조.** 3000 시퀀스 × 200 이벤트를 고정 시드로 만들고, 이벤트마다 테스트 안에서 규칙표로 기대 TEC/REC 와 상태(`> 255`, `> 127` 로 따로 쓴 식)를 계산해 비교한다. 1/4 은 수신 이벤트만 받는 "수신 전용" 노드:

```text
random: 600000 events, mismatches=0, receiver-only bus-off=0
```

### Part B — 예외 두 개는 왜 있나

- **예외 ① (혼자 남은 passive 송신자의 ACK 에러)**: 커넥터가 빠진 노드는 아무도 ACK 를 안 해 주니 매 시도가 ACK 에러다. 이걸 세면 32번 만에 bus-off, 다시 꽂아도 복구 절차부터. 규칙상 passive 가 될 때(TEC 128)까지는 세고, 그 뒤로는 안 센다 → **혼자 남은 노드는 passive 에 머물며 계속 재전송**하고, 상대가 생기는 순간 ACK 를 받아 TEC 가 내려간다. "passive 플래그 동안 dominant 를 봤다" 는 조건은 "사실은 다른 노드가 있고 그들이 에러를 봤다" 는 뜻이라 그때는 센다.
- **예외 ② (중재 중 stuff error)**: 두 노드가 같은 ID 비트를 보내다 한쪽이 recessive stuff bit 자리에서 dominant 를 읽으면, 그건 고장이 아니라 상대가 다른 비트열을 보낸 것(중재 상황)일 수 있다 — 벌점 없음 (이 상황의 정확한 판정은 ISO 11898-1 확인 권장, 중재 자체는 COMM-M05).

### Part B — 에러 플래그 중첩과 rule 2

에러를 감지한 노드는 다음 비트부터 active 에러 플래그(dominant 6)를 보낸다. 6 연속 dominant 는 스터핑 규칙 위반이라, 아직 에러를 못 본 노드들도 6번째 비트에서 stuff error 를 보고 **자기 플래그를 뒤이어** 보낸다. 그래서 버스의 dominant 구간은 6~12 비트 (연습 4 전수: 6..12, 에러 프레임 14..20). 아래는 `flag_superpose` 의 실제 B2 출력을 그린 것이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">에러 플래그 중첩: 수신자 R1 이 먼저 봄 (로컬 고장) → 나머지가 6 dominant 를 보고 뒤따름</text>
  <g fill="currentColor" text-anchor="middle">
    <text x="163" y="44">0</text><text x="293" y="44">5</text><text x="319" y="44">6</text><text x="449" y="44">11</text>
    <text x="475" y="44">12</text><text x="657" y="44">19</text><text x="560" y="44">비트 번호</text>
  </g>
  <rect x="150" y="54" width="156" height="24" class="f-warn" opacity=".7" stroke="currentColor"/>
  <rect x="306" y="94" width="156" height="24" class="f-acc" opacity=".5" stroke="currentColor"/>
  <rect x="462" y="54" width="208" height="24" class="f-ok" opacity=".35" stroke="currentColor"/>
  <rect x="462" y="94" width="208" height="24" class="f-ok" opacity=".35" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="10" y="70">R1 (먼저 봄)</text><text x="10" y="110">T, R2 (뒤따름)</text><text x="10" y="166">버스 (AND)</text>
    <text x="196" y="70">active flag 0..5</text><text x="320" y="70">recessive 내며 대기</text>
    <text x="350" y="110">active flag 6..11</text><text x="500" y="70">delimiter 8</text><text x="500" y="110">delimiter 8</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="110,140 150,140 150,170 462,170 462,140 670,140 700,140"/>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".5">
    <line x1="306" y1="50" x2="306" y2="180"/><line x1="332" y1="50" x2="332" y2="180"/><line x1="462" y1="50" x2="462" y2="180"/>
  </g>
  <rect x="306" y="190" width="26" height="18" class="f-warn" opacity=".8" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="340" y="204">R1 플래그 뒤 첫 비트가 dominant → REC +8 (rule 2)</text>
    <text x="10" y="232">결과 (실제 출력): primary RX REC+9, other RX REC+1, TX TEC+8, 버스 = 0×12 + 1×8 = 20 비트</text>
    <text x="10" y="252">모두 같은 비트에서 봤다면: 0×6 + 1×8 = 14 비트, 수신자 전원 REC+1 뿐 (플래그 뒤 첫 비트가 recessive)</text>
    <text x="10" y="272">플래그 뒤 dominant 는 7개까지 봐준다. 8번째(= active 플래그 시작부터 14번째)부터 8개마다 +8 (rule 6)</text>
    <text x="10" y="292">한 칸 = 1 비트. dominant = 아래, recessive = 위. 출처: solution.c B1·B2 (wave 문자열과 테스트로 대조)</text>
  </g>
</svg>
```

`flag_superpose` 의 다른 경우들 (실제 출력):

```text
aligned   : 00000011111111 (14 bits)
primary RX: 00000000000011111111 (20 bits)  primary REC+9, other RX REC+1, TX TEC+8
stuck dominant 13 bits: TX TEC+8 RX REC+9
stuck dominant 14 bits: TX TEC+16 RX REC+17
stuck dominant 21 bits: TX TEC+16 RX REC+17
stuck dominant 22 bits: TX TEC+24 RX REC+25
stuck dominant 30 bits: TX TEC+32 RX REC+33
passive TX: 11100000011111111  TX flag [0..8]
```

- stuck 13 → 플래그 뒤 dominant 7개 = 봐줌. 14 → 8개째에서 +8. 21 은 15개라 그대로, 22 는 16개라 +8 하나 더. 수신자는 플래그 뒤 첫 비트가 dominant 라 rule 2 의 +8 도 받는다.
- passive 송신자: 비트 0..2 는 recessive(3연속), 비트 3 부터 다른 노드의 active 플래그 → 레벨이 바뀌어 다시 세서 비트 8 에 6연속 → passive 플래그가 9비트. "passive 플래그 = recessive 6비트" 가 아니라 **"같은 레벨 6연속을 볼 때까지"** 라는 정의가 여기서 드러난다.

**rule 2 는 왜 있나 — 실험.** 수신부가 고장 나 모든 프레임에서 혼자 에러를 보는 노드 R 이 있다고 하자. R 이 active 인 동안은 R 의 플래그가 프레임을 깨뜨리므로 **멀쩡한 송신자 T 도 매번 TEC +8** 을 받는다. 누가 먼저 쫓겨나느냐가 rule 2 에 달려 있다. `demo_confinement.c` 실제 출력:

| rule 2 | R passive at | T peak TEC | T passive | T bus-off | R REC at end |
|---|---|---|---|---|---|
| on | frame 15 | 120 | never | never | 255 |
| off | never | 248 | frame 16 | frame 32 | 32 |

rule 2 가 있으면 R 은 프레임당 +9 라 15번째(REC 135)에 passive 가 되고, passive 플래그는 버스에 안 보이니 T 는 TEC 120 에서 멈춰 다시 내려온다. rule 2 를 끄면 R 은 +1 씩이라 T 가 32 프레임 만에 bus-off — **고장 난 수신자가 멀쩡한 송신자를 버스에서 쫓아낸다.** "먼저 본 노드가 고장일 가능성이 높다" 는 가정을 숫자 8 로 구현한 것이다. 15 와 16 의 차이는 일부러 맞춘 것처럼 아슬아슬하다: 9 × 15 = 135 ≥ 128 인데 8 × 15 = 120 &lt; 128.

### Part B — bus-off 복구: 왜 "11 연속 recessive" 인가

정상 프레임의 끝은 ACK slot(dominant) 뒤 **ACK delimiter 1 + EOF 7 + intermission 3 = recessive 11** 이고, 에러 프레임의 끝은 **error delimiter 8 + intermission 3 = 11** 이다. 스터핑 영역 안에서는 같은 비트가 5개를 못 넘는다. 그래서 "11 연속 recessive" 는 *프레임 하나가 끝났다* (또는 버스가 11비트 동안 조용했다) 의 신호이고, 128회 = "버스가 최소 128번 정상적으로 숨을 쉬는 것을 지켜봤다". 고장 노드가 망가진 버스에 바로 다시 뛰어들지 못하게 하는 대기 시간이다.

| 상황 | 필요한 것 | 비트 (실측) | 125 k | 500 k | 1 M |
|---|---|---|---|---|---|
| 빈 버스 | 1408 비트 (= 128 × 11) | 1408 | 11264 µs | 2816 µs | 1408 µs |
| 100 % 부하 (DLC 8 표준 프레임 연속) | 프레임 128개 | 14606 | 116848 µs | 29212 µs | 14606 µs |

(`demo_confinement.c` 실제 출력. 꽉 찬 버스 값은 스터핑 비트를 포함한 실제 비트열 = 프레임당 평균 114.1 비트. 프레임 사이에 빈 비트 g 개가 있으면 프레임 하나가 1 + ⌊g/11⌋ 회 — 연습 3.)

### Part C — 시도 하나의 결과표

| 결과 (우선순위 순) | 송신자 T | 정상 수신자 | 고장 수신자 R |
|---|---|---|---|
| OUT_TX_CORRUPT (T 의 송신부 고장) | TEC +8 | REC +1 | REC +1 |
| OUT_RX_PRIMARY (active R 이 먼저 봄) | TEC +8 | REC +1 | REC +1 +8 (rule 2) |
| OUT_GLOBAL (EMI, 모두 같은 비트) | TEC +8 | REC +1 | REC +1 |
| OUT_ACK_ERR (ACK 할 정상 노드 없음) | TEC +8, passive 면 그대로 (예외 ①) | — | REC +1 |
| OUT_OK | TEC −1 | REC −1 (또는 127) | REC +1 (passive 라 혼자만) |

이 표의 rule 2 줄은 Part B 시뮬레이터의 B2 결과(primary +9, 나머지 +1, TX +8)와 같다 — 같은 규칙의 두 추상화 수준이 일치하는지가 테스트의 일부다. 비트열은 슬롯 엔진이 방출한다: 성공 = SOF + 스터핑된 본문 97비트(ID..CRC, DLC 8) + CRC delim + ACK + 11 recessive, 에러 = 본문 40비트 지점에서 감지 + 플래그(중첩 길이) + delimiter 8 + IFS 3, passive 송신자는 다음 송신 전 suspend 8 (다른 노드가 대기 중이면 양보).

**시나리오 결과 (solution.c 실제 출력).**

```text
C1 lone node: passive after 16 attempts, 1933 bits = 3866.0 us @500k
C1 after 1000 attempts: error-passive TEC 128
  [A]    2894.0 us  ERROR-ACTIVE -> ERROR-WARNING (tx 96 rx 0)
  [A]    3866.0 us  ERROR-WARNING -> ERROR-PASSIVE (tx 128 rx 0)
C2 broken TX: passive at attempt 16, bus-off at attempt 32 (4254.0 us), B REC 32, C REC 32
  [X]    1580.0 us  ERROR-ACTIVE -> ERROR-WARNING (tx 96 rx 0)
  [X]    2104.0 us  ERROR-WARNING -> ERROR-PASSIVE (tx 128 rx 0)
  [X]    4254.0 us  ERROR-PASSIVE -> BUS-OFF (tx 256 rx 0)
C3 faulty RX: R passive at frame 15 (REC after 40 slots 160), T peak TEC 120, T first frame waited 15 slots, C REC 0
C6 recovery, idle bus: 1408 bits after bus-off (2816.0 us @500k)
C6 recovery, busy bus: 128 B frames, 14606 bits (29212.0 us @500k)
C6b two-node bus: X bus-off, B error-passive TEC 128 (B ok 16, last outcome ACK error=1)
C8 valid babbler: X ok 999 TEC 0; B ok 0 arb lost 997 dropped 325
```

- **C1**: 1000번 시도 뒤에도 TEC 128 — 혼자 남은 노드는 passive 에서 재전송을 영원히 반복한다 (버스 점유율 100 %, 하지만 상대가 없으니 피해도 없다). 시도 하나 ≈ 121 비트 (프레임 전체 + 빈 ACK + 플래그 + 11, passive 뒤엔 suspend 8).
- **C2**: 정상 노드 B, C 는 X 의 깨진 프레임마다 REC +1 → 32. X 의 시도는 40비트 지점에서 깨지므로 하나 ≈ 66 비트, 500 k 에서 4.25 ms 만에 bus-off.
- **C3·C4**: R 은 15 프레임 만에 passive, 그 뒤로는 혼자만 에러(REC 포화 255). T 의 첫 프레임은 15번 깨지고 16번째에 성공(대기 15 슬롯). 수신 전용 R 은 5000 프레임 동안 bus-off 0회 — bus-off 는 TEC 로만 간다.
- **C6b (함정)**: 노드가 둘뿐인 버스에서 X 가 bus-off 되면 B 는 **ACK 해 줄 상대가 없어** ACK 에러를 받고 passive(TEC 128)로 간다. 벤치에서 보드 두 개로 테스트하다 "멀쩡한 쪽도 passive 가 됐다" 는 보고의 정체다.
- **C8**: 우선순위 최고 ID 로 **멀쩡한 프레임**을 쉬지 않고 보내는 노드는 에러가 없으니 TEC 0, B 는 999 슬롯 동안 한 번도 못 보낸다(중재 패배 997, 큐 넘침 325). fault confinement 는 에러를 내는 노드만 막는다 — 이 경우는 상위 계층(송신 주기 감시, 버스 가디언, 게이트웨이 필터)이 막아야 한다.

**드라이버 재시작 정책 — babbling idiot.** X 는 송신부가 고장 났고 ID 0x010(최고 우선순위)으로 늘 보낼 게 있다. B, C 는 5 슬롯마다 프레임 하나. 40000 슬롯 @500 k, `demo_confinement.c` 실제 출력:

| policy | X bus-offs | X restarts | bus bits used by X | B ok | B max wait | C ok | C max wait | B peak REC | B went passive |
|---|---|---|---|---|---|---|---|---|---|
| no X (baseline) | 0 | 0 | 0.0 % | 8000 | 0 | 7999 | 1 | 0 | 0 |
| auto-recover (ABOM-like) | 257 | 0 | 19.8 % | 8000 | 16 | 7999 | 28 | 129 | 384 |
| restart-ms 100, unlimited | 6 | 5 | 0.5 % | 8000 | 15 | 7999 | 27 | 26 | 0 |
| 2 ms backoff, max 2 restarts | 3 | 2 | 0.3 % | 8000 | 15 | 7999 | 27 | 26 | 0 |

자동 복구는 X 를 128 occurrence 마다 버스에 돌려보내고, X 는 매번 32번을 더 깨뜨린다: 버스 비트의 **19.8 %** 를 X 가 먹고, 정상 노드 B 의 REC 는 에피소드마다 32 오르고 C 의 프레임으로 조금씩만 내려가 **결국 128 을 넘어 passive 와 active 를 384번 오간다** (B 자신은 멀쩡한데!). 드라이버 정책이 재시작 횟수를 제한하면 X 의 점유는 0.3 % 로 끝난다. B 의 처리량은 어느 정책에서나 8000 으로 같다 — 이 부하(약 40 %)에서는 대역폭보다 **정상 노드의 에러 상태 오염**과 지연(최대 대기 15~16 슬롯)이 피해다.

정책 2 의 드라이버 로그 (solution.c C7, 실제 출력, 앞부분):

```text
  [X]       0.0 us  policy: restart-ms 2 (x2 backoff), max restarts 2
  [X]    7664.0 us  ERROR-PASSIVE -> BUS-OFF (tx 256 rx 0)
  [X]    7664.0 us  bus-off #1: restart in 2 ms
  [X]    9664.0 us  restart requested (wait 128x11 recessive)
  [X]   21340.0 us  BUS-OFF -> ERROR-ACTIVE (tx 0 rx 0)
  …
  [X]   52380.0 us  bus-off #3: giving up -> STOPPED
```

재시작 요청(9.66 ms)과 실제 복귀(21.34 ms) 사이 11.7 ms 가 128×11 대기다 — 이 부하에서는 빈 버스 최소값 2.8 ms 의 4배. 로그 한 줄로 "정책 타이머" 와 "프로토콜 대기" 를 구분해 볼 수 있게 남기는 것이 브링업 엔지니어에게 필요한 정보다.

**Linux 와 STM32 에서 보이는 모습 (확인 권장).** `ip -details -statistics link show can0` 의 CAN 부분은 이 모양이다 (C9 가 만든 문자열):

```text
can state ERROR-PASSIVE (berr-counter tx 128 rx 3) restart-ms 100
  re-started bus-errors arbit-lost error-warn error-pass bus-off
  0          16         2          1          1          0
```

Linux `enum can_state` 의 주석은 ACTIVE "&lt; 96", WARNING "&lt; 128", PASSIVE "&lt; 256", BUS-OFF "&gt;= 256" 이고 `struct can_device_stats` 는 bus_error, error_warning, error_passive, bus_off, arbitration_lost, restarts 를 센다 (include/uapi/linux/can/netlink.h). `restart-ms 0` 이면 bus-off 에서 `ip link set can0 type can restart` 로 손으로, `restart-ms 100` 이면 드라이버가 100 ms 뒤 컨트롤러를 재시작한다. bus-errors 는 드라이버가 버스 에러 보고(berr-reporting)를 켤 때만 세는 경우가 많다. STM32 bxCAN 은 CAN_MCR.ABOM = 1 이면 128×11 뒤 하드웨어가 스스로 복귀, 0 이면 소프트웨어가 초기화 모드 진입/해제로 복구를 시작한다 (RM 확인 권장). 상태는 CAN_ESR 의 EWGF/EPVF/BOFF 비트와 TEC/REC 필드 — 연습 2.

### 모델이 빼먹은 것

- overload 프레임, 에러 delimiter 중의 dominant(새 form error), CRC 에러가 ACK delimiter 뒤에 플래그를 시작하는 타이밍 같은 코너는 다루지 않았다.
- 고장 수신자가 여럿이면 이 모델은 모두를 "먼저 본 노드" 로 친다. 실제로는 감지 비트가 조금씩 달라 일부만 +8 을 받을 수 있다 (Part B 시뮬레이터로 따로 계산 가능).
- 프레임 수준 엔진의 비트열은 길이와 런 길이만 현실적이다 (본문 비트는 의사난수). 정확한 프레임 비트·CRC 는 COMM-M04, 중재 비트는 COMM-M05, 비트레이트·샘플 포인트는 COMM-M06, CAN FD 의 ESI 비트(송신자가 passive 인지 알리는 비트)는 COMM-M08.
- 트랜시버의 TXD dominant time-out (TXD 가 오래 dominant 면 트랜시버가 스스로 끊음, 수 ms 급 — 데이터시트 확인 권장)은 "dominant 를 붙잡은 고장" 을 하드웨어에서 막는 별도 장치다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 87/87 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_confinement.c -o demo_confinement && ./demo_confinement   # 5. 풀이의 표 3개
```

## 7. 자주 하는 실수

**1. bus-off 를 카운터 함수로만 계산한다.** `state = fc_state_of(tec, rec)` 를 매번 다시 계산하면, bus-off 중 들어온 `fc_tx_ok` 가 TEC 를 255 로 내리는 순간 "passive 로 복귀" 해 버린다. bus-off 는 **래치**이고 나가는 길은 복구 절차 하나다. 고친 코드: `fc_update` 첫 줄 `if (n->state == ST_BUSOFF) return;` + 모든 이벤트 함수의 bus-off 가드.

**2. 경계를 `>` 와 `>=` 로 섞는다.** passive = "TEC &gt; 127" = "TEC ≥ 128", bus-off = "TEC &gt; 255" = "TEC ≥ 256". `tec > 128` 로 쓰면 고장 노드가 17번째 에러에서야 passive 가 된다. 테스트 `fc_state_of(127,·)`, `(128,·)`, `(255,·)`, `(256,·)` 네 점을 꼭 넣는다.

**3. REC 를 127 초과에서 −1 한다.** REC 200 인 노드가 성공 수신 한 번에 199 가 되면 passive 에서 빠져나오는 데 73번이 필요하다. 규칙은 "127 초과면 119..127 로" — 성공 한 번이면 active 로 돌아온다 (이 모델은 127). 반대로 TEC 에는 이런 보정이 없다.

**4. 복구 카운터를 슬라이딩 윈도로 센다.** `if (++run >= 11) occ++;` (run 을 안 비움) → 빈 버스에서 138 비트 만에 복구 (연습 3 재현). 고친 코드: 11 이 되면 `run = 0` 후 `occ++`.

**5. ACK 에러를 늘 +8 한다 (예외 ① 누락).** 벤치에서 보드 하나만 켜 두면 32번 만에 bus-off — 그러면 상대 보드를 켜도 128×11 을 기다려야 하고, auto 복구가 꺼져 있으면 영원히 조용하다. 규칙대로면 TEC 128 에서 멈추고, 상대가 켜지는 즉시 ACK 를 받아 통신이 이어진다 (C1).

**6. bus-off 에서 바로 재초기화를 반복한다.** "bus-off 인터럽트 → 즉시 init" 을 무한 반복하면 babbling idiot 이 된다 (demo: 버스 19.8 %, 정상 노드 REC 오염, passive 384번). 고친 정책: 재시작 지연(백오프) + 최대 횟수 + 상위에 보고 (C7).

**7. 2노드 벤치 결과를 일반화한다.** 상대가 bus-off 되면 남은 쪽은 ACK 를 못 받아 passive 가 된다 (C6b). "멀쩡한 노드도 passive 가 됐다 = 그 노드도 고장" 이라고 결론 내리기 전에 버스에 ACK 해 줄 노드가 몇 개 남았는지 본다.

## 8. 면접 Q&A

<details><summary>Q1. TEC 는 +8 인데 REC 는 왜 +1 인가요?</summary>

송신자는 버스에서 자기 비트를 직접 되읽으므로(bit monitoring) 에러를 가장 직접적으로 본다. 프레임이 깨졌을 때 고장 원인이 송신자 쪽일 가능성이 상대적으로 높고, 또 송신자는 자동 재전송으로 같은 에러를 반복 생산한다. 그래서 송신자에게 무거운 벌점을 준다. 수신자 +1 은 "에러를 봤다" 정도. 꼬리: 그럼 수신자 고장은 어떻게 잡나요? → rule 2: 혼자 먼저 본 수신자는 플래그 뒤 첫 비트에서 다른 노드의 플래그(dominant)를 보게 되어 +8 을 더 받는다 (+9). 이 레슨 실험에서 rule 2 를 끄면 고장 수신자 대신 멀쩡한 송신자가 32 프레임 만에 bus-off 된다.

</details>

<details><summary>Q2. 고장 송신 노드는 몇 번 만에 passive, bus-off 인가요? 성공이 섞이면요?</summary>

매번 에러면 16번째에 TEC 128 (passive), 32번째에 256 (bus-off). 에러 1번에 성공 m번이 섞이면 j번째 에러 직후 TEC = 8j − m(j−1), passive 는 ⌈(128−m)/(8−m)⌉ 번째 에러, bus-off 는 ⌈(256−m)/(8−m)⌉ 번째 (m=1: 19/37, m=4: 31/63, m=7: 121/249). m ≥ 8 이면 영원히 active — 에러율 1/9 이 경계다. 꼬리: 시간으로는? 이 모델의 깨진 프레임은 약 66 비트라 500 kbit/s 에서 32번 = 4.25 ms. 즉 고장 노드는 수 ms 안에 스스로 빠진다.

</details>

<details><summary>Q3. 노드 하나만 버스에 있으면 어떻게 되나요?</summary>

아무도 ACK 를 안 해 주니 매 시도가 ACK 에러다. active 동안은 +8 씩 올라 16번째에 TEC 128, passive. 그 뒤로는 "passive 송신자의 ACK 에러 + passive 플래그 동안 dominant 없음" 예외라 TEC 가 안 오른다 → passive 에 머물며 계속 재전송 (suspend 8비트씩 쉬면서). 그래서 bus-off 는 안 간다. 실무 증상: 다른 노드 없이 켠 보드의 `ip -details link` 가 "ERROR-PASSIVE (berr-counter tx 128 rx 0)" 이고 candump 에는 아무것도 없다. 꼬리: 송신 큐는? 재전송이 끝나지 않으니 큐가 꽉 차서 `write()` 가 ENOBUFS 를 낼 수 있다 (드라이버별, 확인 권장).

</details>

<details><summary>Q4. bus-off 에서 어떻게 돌아오나요? 얼마나 걸리나요?</summary>

규격은 "11 연속 recessive 를 128번 본 뒤 error-active, TEC = REC = 0". 빈 버스면 최소 1408 비트 = 500 k 에서 2.816 ms, 125 k 에서 11.264 ms. 버스가 바쁘면 프레임 하나의 꼬리(ACK delim 1 + EOF 7 + IFS 3 = 11)가 1회라 프레임 128개 — DLC 8 연속 트래픽이면 500 k 에서 약 29 ms (이 레슨 측정). 컨트롤러별로 시작 조건이 다르다: STM32 bxCAN ABOM=1 이면 자동, 0 이면 소프트웨어가 init 모드를 들어갔다 나와야 시작 (RM 확인 권장). Linux 는 `restart-ms` 가 0 이면 수동 `ip link set can0 type can restart`. 꼬리: 왜 바로 안 돌려보내나요? → 고장이 그대로면 다시 32번을 깨뜨리기 때문. 펌웨어는 여기에 백오프·최대 횟수를 더한다.

</details>

<details><summary>Q5. 수신만 하는 노드도 bus-off 될 수 있나요?</summary>

아니다. bus-off 조건은 TEC &gt; 255 뿐이고 수신 에러는 REC 만 올린다. REC 가 아무리 높아도 error-passive 까지다 (C4: 고장 수신자 5000 프레임, bus-off 0회, REC 255 포화). 그래서 수신 전용 노드의 고장은 "passive 가 되어 남의 프레임을 못 깨뜨리는" 것으로 격리된다. 꼬리: 수신 전용 노드가 ACK 는 하나요? → 예, passive 여도 ACK slot 은 당긴다. listen-only(silent) 모드라야 ACK 도 안 한다 — 버스 모니터링 툴이 쓰는 모드.

</details>

<details><summary>Q6. error-passive 노드는 active 노드와 무엇이 다르게 동작하나요?</summary>

① 에러 플래그가 recessive 6 (passive flag) 라서 다른 노드의 프레임을 깨뜨리지 못한다 — 자기만 그 프레임을 버린다. passive 플래그는 "같은 레벨 6연속을 볼 때까지" 라서 다른 노드의 active 플래그와 겹치면 더 길어진다(이 레슨 B6: 9비트). ② 송신 뒤 intermission 다음에 8비트 suspend transmission — 그동안 다른 노드가 시작하면 양보한다. ③ 예외 ①이 적용된다. CAN FD 에서는 송신자가 ESI 비트로 자기가 passive 임을 알린다 (COMM-M08). 꼬리: passive 노드가 많은 버스는 왜 위험하나요? → 에러를 봐도 아무도 프레임을 깨뜨리지 않으면, 일부 노드만 받은 프레임(일관성 깨짐)이 생길 수 있다.

</details>

<details><summary>Q7. babbling idiot 은 fault confinement 로 막히나요?</summary>

두 종류로 나눠 답한다. (a) 깨진 프레임을 쏘는 노드: 32번 만에 bus-off 로 막힌다. 하지만 자동 복구를 켜 두면 128 occurrence 마다 돌아와 또 32번 깨뜨린다 — 이 레슨 측정에서 버스의 19.8 % 를 먹고 정상 노드의 REC 를 passive 문턱까지 밀어 올렸다. 그래서 드라이버가 재시작 횟수를 제한한다. (b) **멀쩡한** 프레임을 너무 자주 보내는 노드: 에러가 없으니 TEC 0, 아무것도 안 막힌다 (C8: 낮은 ID 가 버스를 독점, 다른 노드는 0개 송신). 이건 상위 설계로 막는다: 송신 주기 감시(소프트웨어 watchdog), 게이트웨이 필터, 버스 가디언, TXD dominant time-out 이 있는 트랜시버(붙잡힌 dominant 만 해당).

</details>

<details><summary>Q8. 필드에서 "가끔 bus-off" 가 나면 무엇부터 보나요?</summary>

① `ip -details -statistics` / ESR 로 **어느 카운터**가 오르는지: TEC 면 송신 쪽(트랜시버, TX 핀, 비트 타이밍), REC 만이면 수신 쪽 또는 다른 노드. ② 버스 전체가 오르는지(모든 노드 REC 증가 = 배선·종단·EMI) 한 노드만인지. ③ 종단 저항 60 Ω(양 끝 120 Ω 두 개)을 전원 끈 상태로 측정. ④ 비트레이트·샘플 포인트가 노드끼리 맞는지 (COMM-M06 — 노드 하나만 87.5 % 대신 75 % 면 긴 버스에서 그 노드만 에러). ⑤ 오실로스코프로 CAN_H/CAN_L 차동 파형, 에러 프레임(6~12 dominant + 8 recessive)이 어디서 시작되는지. ⑥ 노드 수가 2개뿐인 벤치라면 ACK 상대가 사라졌는지 (C6b). 로그에 상태 전이 시각과 TEC/REC 를 남기는 이유가 이것이다.

</details>

## 9. 추가 연습

### 연습 1 — 에러율 1/9 경계를 전수로 증명

송신 결과를 길이 L(1..12) 의 비트 패턴(1 = 에러, 0 = 성공)으로 무한 반복할 때 "결국 bus-off" 인 패턴을 시뮬레이션으로 찾고, 판정식 8E &gt; S (E = 에러 수, S = 성공 수) 와 8190 패턴 전부 일치하는지 확인하라. 수신자 버전(먼저 본 로컬 에러 = REC +9, 성공 −1)은 어떤 식인가?

<details><summary>답 보기</summary>

한 주기 동안 TEC 는 0 바닥에 안 닿으면 정확히 8E − S 변한다. 바닥 클램프는 감소를 줄일 뿐이라 TEC 를 더 크게 만든다. 따라서 8E &gt; S 면 주기마다 최소 +1 → 언젠가 256. 8E ≤ S 이면 TEC 가 S 이상일 때 주기 변화 ≤ 0 이라 위로 유계이고, 길이 12 이하에서는 그 상계가 256 에 못 미친다(E = 1, S ≥ 8 뿐). 실행 결과: bus-off 8136 패턴, 유계 42 패턴(= 길이 9..12 의 "에러 1개" 패턴 9 + 10 + 11 + 12), 불일치 0, 가장 느린 bus-off 는 "S 7번 + E" 패턴의 1992번째 이벤트(= 249번째 에러, 식 ⌈(256 − 7)/1⌉ 과 같음). 수신자 버전은 9E &gt; S 와 전부 일치 — 로컬 고장 수신자는 10프레임 중 1번만 혼자 에러를 봐도 버티지만 그보다 자주면 passive 로 격리된다.

</details>

### 연습 2 — STM32 bxCAN CAN_ESR 디코더

32비트 ESR 값을 `{TEC, REC, LEC, BOFF, EPVF, EWGF}` 로 풀고 `"ERROR-PASSIVE TEC 128 REC 3 LEC=ack"` 처럼 출력하라. 대표 레이아웃 (RM0090 등, RM 확인 권장): REC[31:24], TEC[23:16], LEC[6:4] (0 없음, 1 stuff, 2 form, 3 ack, 4 bit recessive, 5 bit dominant, 6 CRC, 7 소프트웨어 설정), BOFF[2], EPVF[1], EWGF[0]. bus-off 가 아닌데 플래그가 카운터와 안 맞으면 "(INCONSISTENT)".

<details><summary>답 보기</summary>

시프트·마스크로 풀고, 상태는 BOFF &gt; EPVF &gt; EWGF 순으로 고른다. 일관성: bus-off 가 아니면 EWGF == (max ≥ 96), EPVF == (max ≥ 128). 테스트: {TEC 128, REC 3, LEC 3, EPVF, EWGF} → 0x03800033 → "ERROR-PASSIVE TEC 128 REC 3 LEC=ack", 0x00F80065 → "BUS-OFF TEC 248 REC 0 LEC=crc" (TEC 필드가 8비트라 256 을 못 담는다 — bus-off 는 BOFF 로만 안다), 0x00800002 (EPVF 인데 EWGF 없음) → INCONSISTENT, TEC·REC 65536 조합 전수 round trip + 상태 일치, 버퍼 8바이트면 −1. 실제 펌웨어에서는 LEC 를 읽은 뒤 7(소프트웨어 값)로 써 두면 "그 뒤 새 에러가 있었나" 를 구분할 수 있다는 것이 LEC=7 의 용도다 (RM 확인 권장).

</details>

### 연습 3 — 버스 부하와 복구 시간, 그리고 슬라이딩 윈도 버그

프레임(꼬리 11 recessive) 사이에 빈 비트 g 개가 있는 트래픽을 비트 단위로 `fc_recovery_bit` 에 먹여, 복구에 필요한 프레임 수를 g = 0..40 과 랜덤 간격 2000 시퀀스에서 식과 맞춰라. 그리고 run 을 비우지 않는 버그 버전이 빈 버스에서 몇 비트 만에 "복구" 하는지 보여라.

<details><summary>답 보기</summary>

프레임 꼬리 + 간격 = 11 + g 연속 recessive → ⌊(11 + g)/11⌋ = 1 + ⌊g/11⌋ 회, 남은 run 은 다음 SOF(dominant)가 지운다. 필요 프레임 = ⌈128 / (1 + ⌊g/11⌋)⌉: g = 0..10 → 128, 11..21 → 64, 22..32 → 43, 33..43 → 32 (실행 결과와 일치). 랜덤 간격은 누적 합이 128 에 닿는 프레임 번호로 예측 — 2000 시퀀스 불일치 0. 즉 **간격이 10비트까지는 꽉 찬 버스와 똑같이 128 프레임**이 걸린다. 버그 버전은 11번째 비트부터 매 비트를 1회로 세서 11 + 127 = 138 비트 만에 복구 — 규격보다 10배 빨리 고장 노드를 돌려보낸다.

</details>

### 연습 4 — 에러 플래그 중첩 길이 전수

active 노드 셋(송신자 1, 수신자 2)이 각자 비트 0..5 중 하나에서 감지하거나 "6 dominant 를 보고 뒤따름" (단, 누군가는 비트 0 에서 처음 본다) 인 127 조합을 `flag_superpose` 로 돌려라. dominant 구간 길이, 에러 프레임 길이(플래그 + delimiter)의 범위와, rule 2 를 받는 노드의 규칙을 확인하라.

<details><summary>답 보기</summary>

결과: dominant 6..12 비트, 에러 프레임 14..20 비트 (TI 의 CAN 입문 문서가 말하는 "에러 프레임 6~12 비트 + delimiter 8" 과 같다). 모든 조합에서 송신자 +8, 수신자는 **자기 플래그가 마지막 플래그보다 먼저 끝났으면 +9, 아니면 +1** — rule 2 는 "늦게 끝난 플래그가 있다 = 내가 먼저 봤다" 를 감지하는 장치라는 것이 전수로 보인다. 그리고 버스 비트 수 = dominant 길이 + 8, dominant 길이 = 마지막 플래그 끝 + 1.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 송신 에러 TEC +8, 수신 에러 REC +1, 성공 송신 TEC −1, 성공 수신 REC −1 (127 초과면 119..127 로 — 이 모델 127).
- error-active: 둘 다 ≤ 127 / error-passive: 하나라도 ≥ 128 (recessive 플래그, suspend 8) / bus-off: TEC ≥ 256. REC 로는 bus-off 불가, bus-off 는 래치.
- 고장 송신 노드: 16번째 에러 passive, 32번째 bus-off. 성공 m번이 섞이면 ⌈(256 − m)/(8 − m)⌉, 에러율 1/9 이하면 영원히 버팀.
- 예외: 혼자 남은 passive 노드의 ACK 에러는 안 셈 → TEC 128 에서 멈춤. 중재 중 stuff error 도 안 셈.
- rule 2: 플래그 뒤 첫 비트가 dominant = 내가 먼저 봤다 → REC +8. 이게 없으면 고장 수신자가 멀쩡한 송신자를 쫓아낸다 (실측: 15 vs 32 프레임).
- 플래그 중첩 6..12 dominant + delimiter 8. 플래그 뒤 dominant 는 7개까지 봐주고 14번째(active)부터 8개마다 +8.
- 복구 = 11 연속 recessive × 128 (센 뒤 0부터): 빈 버스 1408 비트(500 k 2.8 ms), 꽉 찬 버스 128 프레임(측정 29 ms). auto(ABOM) vs host 요청(Linux restart-ms).
- 드라이버: 상태 전이 로그 + TEC/REC, 재시작 백오프·최대 횟수. 멀쩡한 프레임으로 떠드는 노드는 fault confinement 가 못 막는다.

## 참고자료

- Bosch, *CAN Specification Version 2.0* (1991), Part B §7 Error Handling, §8 Fault Confinement — 규칙 1~12, 문턱 128/256, 128×11 복구, 에러 플래그·delimiter·suspend transmission. 이 레슨의 규칙 문장은 기억에 의존한 요약 — 원문 대조 권장 (예: 119..127, 14번째 dominant).
- ISO 11898-1:2015 *Road vehicles — CAN — Part 1: Data link layer and physical signalling* — 현행 규격 (FD 포함). 코너 규칙 확인용.
- Linux kernel, `include/uapi/linux/can/netlink.h` — `enum can_state` (주석: active &lt; 96, warning &lt; 128, passive &lt; 256), `struct can_berr_counter`, `struct can_device_stats`: <https://github.com/torvalds/linux/blob/master/include/uapi/linux/can/netlink.h>
- Linux kernel docs, "SocketCAN — Controller Area Network", 특히 restart-ms 와 bus-off 처리: <https://docs.kernel.org/networking/can.html>
- iproute2 `ip-link(8)` — `type can ... restart-ms`, `restart`, `berr-reporting` 옵션 (출력 형식은 버전별 확인 권장): <https://man7.org/linux/man-pages/man8/ip-link.8.html>
- ST, RM0090 (STM32F4) bxCAN 장 — CAN_MCR.ABOM, CAN_ESR(REC/TEC/LEC/BOFF/EPVF/EWGF), bus-off 복구 (RM 확인 권장). FDCAN 계열은 별도 장.
- Texas Instruments, SLOA101B *Introduction to the Controller Area Network (CAN)* — 에러 프레임 6~12 비트 + delimiter 8: <https://www.ti.com/document-viewer/lit/html/SLOA101B/error-checking-and-fault-confinement-sloa1013619>
- CSS Electronics, "CAN Bus Errors Explained" (입문용 그림): <https://www.csselectronics.com/pages/can-bus-errors-intro-tutorial>
- 관련 레슨: COMM-M04 (프레임·스터핑·CRC-15), COMM-M05 (중재 — 같은 wired-AND), COMM-M06 (비트 타이밍·샘플 포인트), COMM-M08 (CAN FD, ESI 비트), COMM-M09 (SocketCAN 실습, vcan 은 에러를 안 만든다), COMM-B06 (오픈드레인 wired-AND), COMM-M14 (재시도·백오프 정책), SIV-B/M (validation 로그)
