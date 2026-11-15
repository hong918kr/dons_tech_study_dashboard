---
id: COMM-M03
time: 75분
updated: 2026-11-11
---
# COMM-M03 I2C 버스 복구: SDA stuck-low에서 9클럭 + STOP

## 학습 목표

- "읽기 도중 MCU 가 리셋되면 왜 SDA 가 LOW 로 붙는가"를 슬레이브 상태 머신으로 설명하고, 그 hang 을 틱 단위 시뮬레이터에 **주입**해 재현한다.
- UM10204 의 bus clear(SCL 최대 9펄스)를 **펄스마다 STOP 을 시도하는** 형태로 구현하고, SCL 을 되읽어(read-back) 스트레칭·고착을 구분한다.
- "왜 9 인가"를 **닫힌 식**(쓰기 ACK = 1, 읽기 주소 ACK = 1 + 다음 바이트의 앞쪽 0 개수, 데이터 비트 c = 첫 1 까지)으로 말하고, 9만 5천 개 리셋 위치 전수와 상태 공간 전수로 증명한다.
- 흔한 구현("SDA=1 이 보이면 SCL 내리고 STOP")이 **STOP 을 만들려고 내린 SCL↓ 하나 때문에** 실패하는 경우를 재현하고 고친다.
- 드라이버 경로에서 busy·STOP 실패를 감지 → 복구 → 전원 재인가 → 재시도하고, 브링업 엔지니어가 읽을 로그와 에러 코드(SDA_STUCK / SCL_STUCK / BUS_STUCK)를 남긴다.

## 1. 문제

"디버거로 리셋 버튼을 눌렀더니 센서가 영영 안 읽힌다, 전원을 뽑아야 산다" — 브링업에서 가장 자주 듣는 I2C 버그이고, 면접에서는 "SDA 가 low 로 붙었다. 복구 절차는? 왜 9 클럭이죠? SCL 이 붙었으면요?"로 나온다. 답은 짧지만 **STOP 을 어떻게 만드느냐**에서 대부분 틀린다.

원인은 I2C 에 리셋 신호가 없다는 데 있다. 마스터(MCU)는 워치독·브라운아웃·디버거·펌웨어 업데이트로 언제든 리셋될 수 있지만, 슬레이브는 그 사실을 모른다. 리셋된 MCU 의 핀은 입력(Hi-Z)이 되어 두 선을 **놓는다**. SCL 은 풀업으로 1 이 되고, 슬레이브는 "읽기 도중이니 다음 SCL↓ 을 기다리며 지금 비트를 유지" 한다. 그 비트가 0 이면 SDA 는 영원히 LOW — 마스터는 START(SCL=1 중 SDA↓)를 만들 수 없다. 아래는 이 레슨 코드가 실제로 만든 트레이스다.

```svg
<svg viewBox="0 0 752 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">읽기 도중 마스터 리셋 → SDA stuck-low → 복구 펄스 3개 + STOP (solution.c 실제 트레이스 48틱)</text>
  <rect x="434" y="100" width="56" height="50" class="f-ok" opacity=".35"/>
  <rect x="476" y="100" width="210" height="50" class="f-warn" opacity=".35"/>
  <rect x="574" y="38" width="168" height="112" class="f-acc" opacity=".18"/>
  <line x1="490" y1="30" x2="490" y2="200" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="70,50 98,50 98,80 126,80 126,50 140,50 140,80 168,80 168,50 182,50 182,80 210,80 210,50 224,50 224,80 252,80 252,50 266,50 266,80 294,80 294,50 308,50 308,80 336,80 336,50 350,50 350,80 378,80 378,50 392,50 392,80 420,80 420,50 434,50 434,80 462,80 462,50 476,50 476,80 490,80 490,50 574,50 574,80 602,80 602,50 630,50 630,80 658,80 658,50 686,50 686,80 714,80 714,50 742,50"/>
    <polyline points="70,110 84,110 84,140 112,140 112,110 154,110 154,140 196,140 196,110 238,110 238,140 406,140 406,110 434,110 434,140 686,140 686,110 700,110 700,140 728,140 728,110 742,110"/>
  </g>
  <g fill="currentColor"><text x="10" y="70">SCL</text><text x="10" y="130">SDA</text></g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="86" y="170">S</text>
    <text x="266" y="170">주소 1010000 + R (0xA1)</text>
    <text x="462" y="96">ACK</text>
    <text x="490" y="214">리셋! (k=29)</text>
    <text x="525" y="170">부트</text>
    <text x="602" y="170">펄스1</text>
    <text x="658" y="170">펄스2</text>
    <text x="714" y="170">펄스3</text>
    <text x="728" y="96">P</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="236">0x12 = 0001 0010 의 MSB(0)를 슬레이브가 내던 중 마스터 리셋 → 핀이 입력(놓음).</text>
    <text x="10" y="253">SCL 은 풀업으로 1, SDA 는 슬레이브가 계속 0 으로 쥔다 = hang (START 를 만들 수 없음).</text>
    <text x="10" y="270">펄스 = SCL↓ → SDA 당김 → SCL↑ → SDA 놓기. 펄스1·2 의 SCL↓ 에 슬레이브는 비트6·5 (=0).</text>
    <text x="10" y="287">펄스3 의 SCL↓ 에 비트4 (=1) = 놓음 → SCL=1 에서 마스터가 SDA 를 놓자 SDA↑ = STOP.</text>
    <text x="10" y="304">펄스3 앞의 짧은 SDA=1 은 SCL=0 일 때라 조건이 아니다. 한 칸 = 모델 1틱.</text>
    <text x="10" y="321">초록 = 슬레이브 ACK, 주황 = stuck, 파랑 = 복구. 출처: solution.c 트레이스 (tr_scl / tr_sda).</text>
  </g>
</svg>
```

**모델 (주어짐, COMM-B06 모델 축약판).** SDA/SCL = 모든 노드 드라이버의 wired-AND (1 = 놓음). 마스터가 선을 하나 바꿀 때마다 `bus_tick()` 1번, 슬레이브는 같은 스냅샷을 보고 SCL↑ 에서 샘플, SCL↓ 에서만 SDA 를 바꾸며 START/STOP 은 어느 상태에서든 최우선이다 (규칙 상세는 COMM-B06). 이 레슨이 더한 것은 **고장 주입**이다.

| 주입 | 모델에서 | 현실 |
|---|---|---|
| 마스터 리셋 | `arm_reset(b, k)` → 트랜잭션의 k 번째 마스터 동작 대신 두 핀을 놓고 죽는다. `master_reboot()` 까지 마스터는 아무것도 못 한다 | 워치독/디버거 리셋, BOR. 핀은 리셋 중 입력(Hi-Z) — 칩별 리셋 상태는 데이터시트 확인 |
| 고장 노드 SDA | `fault_sda_low = true` → 영원히 SDA LOW | 래치업·전원 이상으로 죽은 디바이스, 9클럭으로 안 풀리는 버그 칩 |
| 고장 노드 SCL | `[fault_scl_from, fault_scl_until)` 틱 동안 SCL LOW | 짧으면 클럭 스트레칭(COMM-M01), 영원하면 SCL 고착 |
| ACK 슬롯 글리치 | `glitch_ack_once` → 슬레이브가 마스터의 NACK 을 ACK 로 한 번 잘못 봄 | SDA 잡음·느린 rise time(HWLAB-B03) |

### Part A — 9클럭 복구 루틴

```c
typedef enum { BUS_IDLE = 0, BUS_SDA_LOW = 1, BUS_SCL_LOW = 2, BUS_BOTH_LOW = 3 } BusState;
enum { REC_OK = 0, REC_ERR_SDA_STUCK = -10, REC_ERR_SCL_STUCK = -11 };
enum { REC_MAX_PULSES = 9, SCL_WAIT_MAX = 16 };
BusState bus_state(const Bus *b);            /* bit0 = SDA LOW, bit1 = SCL LOW */
int wait_scl_high(Bus *b);                   /* SCL 을 되읽으며 최대 16틱 대기. 반환 = 대기 틱, 실패 -1 */
int rec_pulse(Bus *b, int *waited);          /* SCL↓ · SDA↓ · SCL↑(대기) · SDA↑ → 1 = SDA 풀림(STOP 생성) */
int i2c_bus_recover(Bus *b, RecReport *rep); /* REC_OK / REC_ERR_SDA_STUCK / REC_ERR_SCL_STUCK */
```

- 핀은 이미 GPIO 오픈드레인으로 바뀌었다고 본다(모델의 node 0). 먼저 두 선을 놓고 **SCL 이 정말 1 이 되는지 되읽는다**. 16틱 안에 안 되면 클럭을 하나도 내지 않고 `REC_ERR_SCL_STUCK`.
- SDA 가 이미 1 이면 아무것도 하지 않는다 (0 펄스, `REC_OK`).
- 최대 9 펄스. 펄스 하나 = `SCL↓ → SDA↓ → SCL↑(되읽기) → SDA↑`. 슬레이브가 이번 SCL↓ 에서 SDA 를 놓았다면 마지막 동작이 SCL=1 중 SDA↑ = STOP 이다. 놓은 뒤 SDA=1 이면 끝.
- 9 펄스 뒤에도 SDA=0 이면 `REC_ERR_SDA_STUCK`. `RecReport` 에 펄스 수, 펄스별 SDA, 대기 틱을 남긴다.
- 테스트: 위 그림 시나리오(0x12 의 MSB 에서 리셋) → 정확히 3 펄스·클럭 3개·STOP 1개, 펄스 하나 = 4틱, 복구 뒤 두 슬레이브와 정상 트랜잭션.

### Part B — 왜 9 인가, 그리고 고장마다 다른 결과

- `predict_pulses(s, b)` — 리셋 직후 슬레이브 상태만 보고 필요한 펄스 수를 계산한다 (SDA 를 안 잡고 있으면 0). 식은 5. 풀이.
- **전수**: {쓰기 3B, 읽기 3B, 레지스터 읽기(쓰기 + Sr + 읽기 2B)} × 데이터 256 패턴 {v, ~v, 난수} × **모든 리셋 위치**(마스터 동작 k = 0, 1, 2, … 트랜잭션 끝까지). 각 시나리오에서: 예측 = 실제 펄스 수, ≤ 9, `REC_OK`, 복구가 만든 START/STOP 은 STOP 하나뿐, 무고한 슬레이브(0x68)는 START·주소 일치 0, 쓰기에서 **끝난 바이트만** 저장, 그 뒤 정상 트랜잭션 2개 성공.
- 이름 붙은 경우: 최악 9 펄스(읽기 주소 ACK + 0x00), 쓰기 ACK 1 펄스, 0x40 에서 흔한 방식(`recover_naive`, 주어짐)이 실패하는 것.
- 고장: SDA 영구 LOW → 정확히 9 클럭 후 `SDA_STUCK` / SCL 영구 LOW → 클럭 0 개로 `SCL_STUCK` / SCL 5틱 스트레칭 → 기다렸다가 성공 / 복구 도중 SCL 고착 → `SCL_STUCK`, pulses = 2 / 이미 idle → 클럭 0.

### Part C — 드라이버 경로: 감지 → 복구 → 전원 재인가 → 재시도

```c
typedef struct { uint8_t addr7; bool read; uint8_t *buf; size_t n; } I2cOp;
int dev_ensure_idle(I2cDev *d);                  /* I2C_OK / I2C_ERR_BUS_STUCK, 카운터·로그 */
int i2c_xfer_robust(I2cDev *d, const I2cOp *op); /* 버스 수준 고장만 여기서 재시도 */
```

- 주어진 `i2c_write/i2c_read` 는 START 전에 idle 을 확인하고(`BUS_BUSY`), STOP 뒤 두 선이 1 인지 확인한다(`STOP_FAILED`).
- `dev_ensure_idle`: idle 이 아니면 로그 → 복구 → 실패하고 `power_cycle` 훅(보드의 센서 전원 스위치)이 있으면 전원 재인가 → 복구 다시 → 그래도 안 되면 `I2C_ERR_BUS_STUCK`.
- `i2c_xfer_robust`: 최대 2번 = ensure_idle → 전송. `STOP_FAILED` 만 재시도(NACK 재시도 정책은 상위 계층 — COMM-M12), 끝까지 실패해도 버스는 정리하고 나간다.
- 로그는 호출자 버퍼(`log_cap`)에 `[t=틱] i2c0: …` 줄로, 넘치면 자르고 `log_trunc` 표시.
- 테스트: 워치독 리셋 뒤 첫 읽기, ACK 글리치로 STOP 실패 → 복구 → 재시도, 죽은 디바이스 + 전원 훅 / 훅 없음, SCL 고착, 작은 로그 버퍼, 고정 시드 300 라운드(40% 는 직전 동작이 랜덤 위치에서 리셋).

## 2. Problem (English)

> **I2C bus recovery after a controller reset.**
>
> You are given a discrete-time open-drain I2C bus model (wired-AND, one line change per tick) with bit-level target state machines, bit-banged controller primitives, and fault injection: the controller can be reset in the middle of any transaction (its pins float, targets keep their state), a fault node can hold SDA low forever or SCL low for a window of ticks.
>
> 1. Implement `BusState bus_state(const Bus*)`, `int wait_scl_high(Bus*)` (read SCL back, wait at most `SCL_WAIT_MAX` ticks), `int rec_pulse(Bus*, int *waited)` and `int i2c_bus_recover(Bus*, RecReport*)`. Release both lines; if SCL never goes high, return `REC_ERR_SCL_STUCK` without clocking. If SDA is already high, do nothing. Otherwise clock SCL at most 9 times; each pulse must end with an attempted STOP so that the bus is idle the moment the target lets go. Return `REC_ERR_SDA_STUCK` if SDA is still low after 9 pulses.
> 2. Implement `int predict_pulses(const Slave*, const Bus*)`: from a target's state, how many pulses will your routine need? Prove (by exhaustive simulation over every reset point of write / read / register-read transactions and 256 data patterns) that the prediction is exact and never exceeds 9, and that a normal transaction succeeds afterwards.
> 3. Implement `int dev_ensure_idle(I2cDev*)` and `int i2c_xfer_robust(I2cDev*, const I2cOp*)`: detect a stuck bus before START and a failed STOP after a transfer, recover, escalate to a board power-cycle hook, retry a transfer once after a failed STOP, keep counters, and write a bounded text log. No dynamic allocation.
>
> Example: a read from `0x50` is interrupted while the target drives the MSB of `0x12` (= 0). After reboot `SCL=1, SDA=0`; `i2c_write()` returns `I2C_ERR_BUS_BUSY`; `i2c_bus_recover()` returns `REC_OK` after exactly 3 pulses (bits 6 and 5 are 0, bit 4 is 1) and generates exactly one STOP.
>
> Follow-ups: Why nine? What if SCL is the stuck line? Why does "toggle until SDA is high, then send STOP" sometimes leave the bus stuck? Where in a real driver do you call this?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데: Part A 4개(`bus_state`, `wait_scl_high`, `rec_pulse`, `i2c_bus_recover`), Part B 1개(`predict_pulses`), Part C 2개(`dev_ensure_idle`, `i2c_xfer_robust`). 버스·슬레이브 모델, 리셋/고장 주입, 비트뱅 프리미티브, STOP 을 확인하는 `i2c_write/i2c_read/i2c_write_read`, 로그 함수 `dlog()`, 비교용 `recover_naive()`, 전수 하네스는 주어진다. 스텁은 아무 선도 안 움직이고, 전수 루프는 버스가 막히면 첫 위치에서 끝나므로 멈추지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:484  bus_state(&b) == BUS_SDA_LOW
FAIL starter.c:486  bus_state(&b) == BUS_BOTH_LOW
…
exhaustive: 795 scenarios, 0 hung (SDA low after reboot), max pulses 0
…
random robust: 300 ops, 111 preceded by reset, 0 recoveries, bad=300
FAIL starter.c:729  bad == 0 && d3v.n_recover > 0 && d3v.n_recover_fail == 0
16/69 passed
```

(exit 1, 약 0.1 초. 전수가 795 개에서 끝나는 이유: 첫 hang 이 안 풀려 이후 트랜잭션이 전부 `BUS_BUSY` 로 즉시 끝나 리셋이 걸리지 않는다.)

## 4. 힌트

<details><summary>힌트 1 — 상태 분류와 SCL 되읽기</summary>

`bus_state` 는 한 줄: `(sda ? 0 : 1) | (scl ? 0 : 2)`. `wait_scl_high` 는 `while (!b->scl && w < SCL_WAIT_MAX) { bus_idle_tick(b); w++; }` 후 `b->scl ? w : -1`. 복구 코드가 "SCL 을 놓았으니 1 이겠지" 하고 넘어가면 클럭 스트레칭 중인 슬레이브에게 SCL=0 구간에서 SDA 를 움직이는 것이 아니라 엉뚱한 순서를 보여 준다 (COMM-M01 과 같은 이유).

</details>

<details><summary>힌트 2 — STOP 을 따로 만들지 말고 펄스에 접어 넣어라</summary>

SDA=1 이 보인 뒤에 STOP 을 만들려면 SDA 를 0 → 1 로 올려야 하는데, SCL=1 에서 SDA 를 내리면 START 이고, SCL 을 먼저 내리면 그 SCL↓ 이 슬레이브에게는 **다음 비트를 내라는 클럭**이다. 그러니 처음부터 매 펄스를 STOP 모양으로 만든다: SCL=0 일 때 SDA 를 당기고, SCL=1 이 된 뒤 SDA 를 놓는다. 슬레이브가 아직 0 을 쥐고 있으면 놓아도 아무 일도 없고(조건 아님), 놓았으면 그 순간이 STOP 이다.

</details>

<details><summary>힌트 3 — i2c_bus_recover 의 뼈대</summary>

`memset(rep)` → `before = bus_state` → `set_sda(1); set_scl(1)` → `wait_scl_high` 실패면 `SCL_STUCK` → `if (b->sda) return REC_OK` → `for (p = 1..9) { r = rec_pulse(); pulses = p; r < 0 → SCL_STUCK; sda_after[p-1] = sda; r == 1 → OK }` → `SDA_STUCK`. 반환 전에 `rep->after` 를 채운다.

</details>

<details><summary>힌트 4 — 몇 펄스? 슬레이브가 "지금 무엇을 내고 있는가"만 보면 된다</summary>

SDA 를 쥐는 상태는 셋뿐이다: ① 쓰기 ACK (`S_RX_ACK`, 또는 `S_ADDR_ACK` + W) → 다음 SCL↓ 에 놓음 = 1. ② 읽기 주소 ACK (`S_ADDR_ACK` + R) → 다음 SCL↓ 에 데이터 MSB 를 냄 → 1 + (그 바이트의 앞쪽 0 개수), 0x00 이면 1 + 8. ③ 데이터 송신 중 (`S_TX`, SCL=1 이므로 지금 비트 c = 8 − nbits) → c−1, c−2, … 중 첫 1 비트까지, 없으면 ACK 슬롯에서 c+1.

</details>

<details><summary>힌트 5 — 드라이버는 "고장 종류마다 한 곳"</summary>

`dev_ensure_idle` 은 START 전 관문 하나로 모든 hang 을 받는다 (부트 직후, 남이 남긴 hang, 내 STOP 실패 뒤). `i2c_xfer_robust` 는 `STOP_FAILED` 일 때만 루프를 한 번 더 돈다 — 다음 시도 첫 줄의 `dev_ensure_idle` 이 복구를 맡는다. NACK 는 그대로 위로 올린다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 원인 | 마스터 리셋 + 슬레이브는 리셋 모름 + 슬레이브가 0 비트(데이터 또는 ACK)를 쥔 채 SCL↓ 을 기다림 |
| 2. 진단 | 두 선 레벨: SCL=1·SDA=0 → 9클럭 복구 가능 / SCL=0 → 클럭 불가, HW 리셋·전원 (UM10204 §3.1.16) |
| 3. 절차 | 핀을 GPIO 오픈드레인으로 → SCL 되읽기 → 최대 9 펄스, 펄스마다 STOP 시도 → idle 확인 → 핀을 I2C 로 되돌리고 주변장치 리셋 |
| 4. 왜 9 | 슬레이브가 SDA 를 쥘 수 있는 가장 긴 구간 = 자기 ACK 1 + 데이터 8 비트 |
| 5. 함정 | STOP 을 만들려고 내린 SCL↓ 도 클럭이다 → 펄스에 STOP 을 접어 넣거나 START+STOP |
| 6. 위치 | init, START 전 busy, STOP 실패, 타임아웃 경로 + 단계적 대응(전원 재인가) + 로그 |

### Part A — 펄스 하나 = STOP 시도 하나

그림의 복구 구간을 틱 단위로 풀면 (solution.c 출력, 15틱 = 놓기 2 + 펄스 3 × 4):

```text
recovery: slave was sending 0x12, reset at MSB (15 ticks)
  SCL ---__--__--__--
  SDA ___________-__-
         v   v   v  P
```

| 틱 | 마스터 동작 | SCL | SDA | 슬레이브(0x50) |
|---|---|---|---|---|
| 0 | (부트 직후) | 1 | 0 | `S_TX`, nbits = 1, 비트7(=0) 유지 |
| 1–2 | SDA 놓기, SCL 놓기 | 1 | 0 | 변화 없음 → `wait_scl_high` = 0 |
| 3 | 펄스1: SCL↓ | 0 | 0 | SCL↓ → 비트6 (=0) |
| 4 | SDA↓ | 0 | 0 | — (SCL=0 이므로 조건 아님) |
| 5 | SCL 놓기 | 1 | 0 | SCL↑ → nbits = 2 |
| 6 | SDA 놓기 | 1 | 0 | 슬레이브가 0 을 쥠 → SDA 그대로, `rec_pulse` = 0 |
| 7–10 | 펄스2 | | 0 | 비트5 (=0), nbits = 3 |
| 11 | 펄스3: SCL↓ | 0 | **1** | 비트4 (=1) = 놓음. 마스터도 놓은 상태라 잠깐 1 |
| 12 | SDA↓ | 0 | 0 | — |
| 13 | SCL 놓기 | 1 | 0 | nbits = 4 |
| 14 | SDA 놓기 | 1 | **0→1** | SCL=1 중 SDA↑ = **STOP** → `S_IDLE` |

핵심은 틱 14 다. 마스터는 매 펄스 같은 동작을 했을 뿐인데, 슬레이브가 놓은 펄스에서만 그 동작이 STOP 이 된다. 슬레이브가 아직 쥐고 있는 펄스에서는 SDA 가 0 에 머물러 아무 조건도 생기지 않는다. 그리고 SDA 가 내려가는 일(틱 4, 12)은 언제나 SCL=0 일 때라 **START 는 절대 생기지 않는다** — 그래서 같은 버스의 다른 슬레이브는 클럭만 보고(IDLE 이라 무시) 마지막 STOP 하나만 본다. 전수 테스트가 이것을 불변식으로 확인한다: 복구 중 "SCL=1 동안 SDA 변화" 횟수 = 펄스가 있었으면 정확히 1, 무고한 0x68 의 START 증가 0.

틱 11 의 짧은 SDA=1 은 SCL=0 일 때라 데이터 변화일 뿐이다. 실제 버스에서도 같은 모양이 나온다 — 로직 애널라이저 디코더가 이 구간을 바이트로 해석하면 쓰레기가 보이니 복구 구간은 프레임으로 믿지 말 것 (SIV-A07 의 hang 분석과 같은 결론).

```c
static int rec_pulse(Bus *b, int *waited) {
    set_scl(b, 0);
    set_sda(b, 0);
    set_scl(b, 1);
    int w = wait_scl_high(b);
    if (w < 0) { set_sda(b, 1); *waited += SCL_WAIT_MAX; return REC_ERR_SCL_STUCK; }
    *waited += w;
    set_sda(b, 1);
    return b->sda ? 1 : 0;
}
```

이것은 Linux 커널의 `i2c_generic_scl_recovery()` 와 같은 방식이다: "SDA 를 쓸 수 있으면 SDA 가 SCL 을 반 박자 늦게 따라가게 해서 매번 STOP 을 만든다", 최대 `RECOVERY_CLK_CNT` = 9 클럭, 반 주기 5 µs(100 kHz), high 구간마다 bus free(SDA) 확인, high 여야 할 때 SCL 이 low 면 "SCL is stuck low" 로 `-EBUSY` (drivers/i2c/i2c-core-base.c, 2026-11 master 에서 확인).

### Part A — i2c_bus_recover 흐름

```svg
<svg viewBox="0 0 740 470" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m03arr" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">i2c_bus_recover() 흐름과 Part C 의 단계적 대응 (escalation)</text>
  <rect x="90" y="36" width="220" height="30" rx="6" class="f-mut" stroke="currentColor"/><text x="200" y="55" fill="currentColor" text-anchor="middle" font-size="12">두 선 놓기 (GPIO 오픈드레인)</text>
  <line x1="200" y1="66" x2="200" y2="84" stroke="currentColor" marker-end="url(#m03arr)"/>
  <polygon points="200,86.0 300.0,110 200,134.0 100.0,110" class="f-acc" stroke="currentColor"/><text x="200" y="114" fill="currentColor" text-anchor="middle" font-size="12">SCL 이 1 이 되나?</text>
  <line x1="300" y1="110" x2="370" y2="110" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="318" y="102" fill="currentColor" font-size="12">아니오</text>
  <rect x="370" y="90" width="200" height="40" rx="6" class="f-warn" stroke="currentColor"/><text x="470" y="107" fill="currentColor" text-anchor="middle" font-size="12">REC_ERR_SCL_STUCK</text><text x="470" y="123" fill="currentColor" text-anchor="middle" font-size="12">16틱 기다려도 LOW</text>
  <line x1="200" y1="134" x2="200" y2="152" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="206" y="148" fill="currentColor" font-size="12">예</text>
  <polygon points="200,154.0 300.0,178 200,202.0 100.0,178" class="f-acc" stroke="currentColor"/><text x="200" y="182" fill="currentColor" text-anchor="middle" font-size="12">SDA = 1 ?</text>
  <line x1="300" y1="178" x2="370" y2="178" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="330" y="170" fill="currentColor" font-size="12">예</text>
  <rect x="370" y="162" width="200" height="32" rx="6" class="f-ok" stroke="currentColor"/><text x="470" y="182" fill="currentColor" text-anchor="middle" font-size="12">REC_OK, 0 펄스 (할 일 없음)</text>
  <line x1="200" y1="202" x2="200" y2="220" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="206" y="216" fill="currentColor" font-size="12">아니오</text>
  <rect x="70" y="220" width="260" height="46" rx="6" class="f-mut" stroke="currentColor"/><text x="200" y="240" fill="currentColor" text-anchor="middle" font-size="12">펄스 p: SCL↓ · SDA↓ · SCL↑(되읽기) · SDA↑</text><text x="200" y="256" fill="currentColor" text-anchor="middle" font-size="12">= STOP 시도 1 번</text>
  <line x1="200" y1="266" x2="200" y2="284" stroke="currentColor" marker-end="url(#m03arr)"/>
  <polygon points="200,286.0 300.0,310 200,334.0 100.0,310" class="f-acc" stroke="currentColor"/><text x="200" y="314" fill="currentColor" text-anchor="middle" font-size="12">놓은 뒤 SDA = 1 ?</text>
  <line x1="300" y1="310" x2="370" y2="310" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="330" y="302" fill="currentColor" font-size="12">예</text>
  <rect x="370" y="294" width="200" height="32" rx="6" class="f-ok" stroke="currentColor"/><text x="470" y="314" fill="currentColor" text-anchor="middle" font-size="12">STOP 생성됨 → REC_OK (p 펄스)</text>
  <line x1="200" y1="334" x2="200" y2="352" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="206" y="348" fill="currentColor" font-size="12">아니오</text>
  <polygon points="200,354.0 300.0,378 200,402.0 100.0,378" class="f-acc" stroke="currentColor"/><text x="200" y="382" fill="currentColor" text-anchor="middle" font-size="12">p &lt; 9 ?</text>
  <polyline points="100,378 40,378 40,243 70,243" fill="none" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="46" y="372" fill="currentColor" font-size="12">예: p+1</text>
  <line x1="200" y1="402" x2="200" y2="420" stroke="currentColor" marker-end="url(#m03arr)"/>
  <text x="206" y="416" fill="currentColor" font-size="12">아니오</text>
  <rect x="110" y="420" width="180" height="30" rx="6" class="f-warn" stroke="currentColor"/><text x="200" y="439" fill="currentColor" text-anchor="middle" font-size="12">REC_ERR_SDA_STUCK</text><text x="650" y="232" fill="currentColor" font-size="12" text-anchor="middle" font-weight="bold">Part C: dev_ensure_idle</text>
  <rect x="580" y="242" width="140" height="26" rx="6" class="f-mut" stroke="currentColor"/><text x="650" y="259" fill="currentColor" text-anchor="middle" font-size="12">① 9클럭 복구</text>
  <line x1="650" y1="268" x2="650" y2="282" stroke="currentColor" marker-end="url(#m03arr)"/>
  <rect x="580" y="282" width="140" height="26" rx="6" class="f-mut" stroke="currentColor"/><text x="650" y="299" fill="currentColor" text-anchor="middle" font-size="12">② power_cycle 훅</text>
  <line x1="650" y1="308" x2="650" y2="322" stroke="currentColor" marker-end="url(#m03arr)"/>
  <rect x="580" y="322" width="140" height="26" rx="6" class="f-mut" stroke="currentColor"/><text x="650" y="339" fill="currentColor" text-anchor="middle" font-size="12">③ 복구 다시</text>
  <line x1="650" y1="348" x2="650" y2="362" stroke="currentColor" marker-end="url(#m03arr)"/>
  <rect x="580" y="362" width="140" height="26" rx="6" class="f-warn" stroke="currentColor"/><text x="650" y="379" fill="currentColor" text-anchor="middle" font-size="12">④ BUS_STUCK 보고</text><text x="650" y="404" fill="currentColor" font-size="12" text-anchor="middle">성공하면 즉시 OK.</text><text x="650" y="420" fill="currentColor" font-size="12" text-anchor="middle">전송 재시도: STOP 실패 1회</text>
</svg>
```

순서마다 이유가 있다. **SCL 확인이 먼저**: SCL 이 LOW 면 클럭을 낼 수 없고, 이때 SDA 를 움직이는 것도 의미가 없다. 기다리는 것은 클럭 스트레칭과 고착을 가르기 위해서다 — 몇 틱 뒤 풀리면 스트레칭(정상), `SCL_WAIT_MAX` 를 넘기면 고착. **SDA 가 이미 1 이면 0 펄스**: 슬레이브가 바이트 중간에 1 비트를 내고 있거나 수신 중일 수 있지만(전수에서 hang 이 아닌 7만 4천 개), 다음 START 가 모든 슬레이브 상태 머신을 초기화하므로 건드릴 필요가 없다. 테스트 "이미 idle → 클럭 0, START/STOP 0" 이 이것을 고정한다. **9 번에서 멈춤**: 그 이상은 슬레이브 프로토콜로 설명되지 않는 고장이다 — 더 쳐 봐야 소용없고 시간만 쓴다.

### Part B — 왜 9 클럭이면 충분한가

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">왜 9 클럭인가 — 슬레이브가 SDA 를 쥘 수 있는 가장 긴 구간</text>
  <text x="10" y="62" fill="currentColor" font-size="12">① 읽기 주소 ACK</text>
  <text x="10" y="78" fill="currentColor" font-size="12">  다음 바이트 0x00</text>
  <rect x="150" y="50" width="52" height="28" class="f-mut" stroke="currentColor"/><text x="176" y="69" fill="currentColor" text-anchor="middle" font-size="12">R/W</text>
  <rect x="202" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="228" y="69" fill="currentColor" text-anchor="middle" font-size="12">ACK</text>
  <rect x="254" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="280" y="69" fill="currentColor" text-anchor="middle" font-size="12">D7</text>
  <rect x="306" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="332" y="69" fill="currentColor" text-anchor="middle" font-size="12">D6</text>
  <rect x="358" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="384" y="69" fill="currentColor" text-anchor="middle" font-size="12">D5</text>
  <rect x="410" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="436" y="69" fill="currentColor" text-anchor="middle" font-size="12">D4</text>
  <rect x="462" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="488" y="69" fill="currentColor" text-anchor="middle" font-size="12">D3</text>
  <rect x="514" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="540" y="69" fill="currentColor" text-anchor="middle" font-size="12">D2</text>
  <rect x="566" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="592" y="69" fill="currentColor" text-anchor="middle" font-size="12">D1</text>
  <rect x="618" y="50" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="644" y="69" fill="currentColor" text-anchor="middle" font-size="12">D0</text>
  <rect x="670" y="50" width="52" height="28" class="f-ok" stroke="currentColor"/><text x="696" y="69" fill="currentColor" text-anchor="middle" font-size="12">ACK슬롯</text><text x="228" y="44" fill="currentColor" text-anchor="middle" font-size="12">리셋 ▼</text><text x="280" y="96" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="332" y="96" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="384" y="96" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="436" y="96" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="488" y="96" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="540" y="96" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="592" y="96" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="644" y="96" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="696" y="96" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="10" y="96" fill="currentColor" font-size="12">  펄스 번호 →</text><text x="696" y="112" fill="currentColor" text-anchor="middle" font-size="12">놓음</text><text x="10" y="142" fill="currentColor" font-size="12">② 쓰기 ACK</text>
  <rect x="150" y="130" width="52" height="28" class="f-mut" stroke="currentColor"/><text x="176" y="149" fill="currentColor" text-anchor="middle" font-size="12">D0</text>
  <rect x="202" y="130" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="228" y="149" fill="currentColor" text-anchor="middle" font-size="12">ACK</text>
  <rect x="254" y="130" width="52" height="28" class="f-ok" stroke="currentColor"/><text x="280" y="149" fill="currentColor" text-anchor="middle" font-size="12">D7</text><text x="228" y="124" fill="currentColor" text-anchor="middle" font-size="12">리셋 ▼</text><text x="280" y="176" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="320" y="149" fill="currentColor" font-size="12">다음 바이트는 마스터가 낸다 → 펄스 1 에 놓음</text><text x="10" y="212" fill="currentColor" font-size="12">③ 데이터 비트 c</text><text x="10" y="228" fill="currentColor" font-size="12">  (=0) 내는 중</text>
  <rect x="150" y="200" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="176" y="219" fill="currentColor" text-anchor="middle" font-size="12">Dc=0</text>
  <rect x="202" y="200" width="52" height="28" class="f-warn" stroke="currentColor"/><text x="228" y="219" fill="currentColor" text-anchor="middle" font-size="12">…</text>
  <rect x="254" y="200" width="52" height="28" class="f-ok" stroke="currentColor"/><text x="280" y="219" fill="currentColor" text-anchor="middle" font-size="12">첫 1</text>
  <rect x="306" y="200" width="52" height="28" class="f-mut" stroke="currentColor"/><text x="332" y="219" fill="currentColor" text-anchor="middle" font-size="12">…</text>
  <rect x="358" y="200" width="52" height="28" class="f-mut" stroke="currentColor"/><text x="384" y="219" fill="currentColor" text-anchor="middle" font-size="12">D0</text>
  <rect x="410" y="200" width="52" height="28" class="f-mut" stroke="currentColor"/><text x="436" y="219" fill="currentColor" text-anchor="middle" font-size="12">ACK슬롯</text><text x="176" y="194" fill="currentColor" text-anchor="middle" font-size="12">리셋 ▼</text><text x="470" y="219" fill="currentColor" font-size="12">첫 1 비트 또는 ACK 슬롯 → ≤ c+1 ≤ 8</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="264">펄스 k 의 SCL↓ 에서 슬레이브는 다음 칸으로 넘어간다. 1 비트나 ACK 슬롯(마스터 차례)이 오면 SDA 를 놓는다.</text>
    <text x="10" y="282">최악 = ① : 자기 ACK 1 칸 + 데이터 8 칸이 전부 0 → 9 번째 SCL↓ 에서야 놓음 = 1 + 8 = 9.</text>
    <text x="10" y="300">주황 = SDA 를 0 으로 쥠, 초록 = 놓음. 식: ① 1 + 다음 바이트 앞쪽 0 개수, ② 1, ③ 첫 1 까지.</text>
    <text x="10" y="318">solution.c 의 predict_pulses() = 이 식. 9만 5천 시나리오 전수에서 시뮬레이션과 전부 일치 (bad_pred=0).</text>
  </g>
</svg>
```

슬레이브가 SDA 를 0 으로 쥘 수 있는 순간은 세 가지뿐이다 (COMM-B06 슬레이브 상태 기준). 펄스 k 의 SCL↓ 에서 슬레이브는 한 칸 앞으로 가고, 그 칸이 1 비트이거나 ACK 슬롯(마스터 차례, 슬레이브는 놓음)이면 SDA 가 풀린다.

| 리셋 순간 슬레이브 | 상태 | 필요한 펄스 | 최댓값 |
|---|---|---|---|
| 쓰기 주소·데이터에 ACK 중 | `S_ADDR_ACK`(W) / `S_RX_ACK` | 1 | 1 |
| 읽기 주소에 ACK 중 | `S_ADDR_ACK`(R) | 1 + 다음 바이트의 앞쪽 0 개수 (0x00 → 8) | **9** |
| 데이터 비트 c (=0) 송신 중 | `S_TX`, c = 8 − nbits | c−1, c−2, … 중 첫 1 까지, 없으면 c + 1 | 8 (c = 7, 나머지 0) |
| 그 밖 (수신 중, 1 비트 송신 중, idle) | — | 0 (SDA 안 잡음) | 0 |

그래서 9 = **자기 ACK 1 + 데이터 8 비트**, 최악은 "읽기 주소에 ACK 하고, 이어서 내보낼 바이트가 0x00" 하나뿐이다. 데이터 송신 중 리셋은 최대 8 이다 — 이미 MSB 를 내기 시작했으므로 남은 칸이 비트 7개 + ACK 슬롯. 흔히 "8 데이터 + ACK = 9" 라고 외우는데, 정확히는 **슬레이브 자신의 ACK 다음에 데이터 8 비트가 이어지는 경우**가 9 를 만든다. UM10204 Rev.7 §3.1.16 *Bus clear* 는 숫자만 준다: SDA 가 LOW 로 붙으면 "the controller should send nine clock pulses", 그 안에 안 풀리면 HW 리셋 또는 전원 재인가.

**전수 결과** (solution.c 실제 출력):

```text
exhaustive: 95488 scenarios, 21528 hung (SDA low after reboot), max pulses 9
  write 3B          pulses 0..9: 26112 3072 0 0 0 0 0 0 0 0
  read 3B           pulses 0..9: 19176 5643 2475 1068 477 210 84 36 12 3
  reg read (Sr) 2B  pulses 0..9: 28672 5376 1728 768 336 144 60 24 9 3
  bad_pred=0 bad_rc=0 bad_invariant=0 bad_verify=0
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">hang 21528 건이 풀리는 데 든 펄스 수 (실측, 로그 스케일) — 9 를 넘는 경우 0</text>
  <line x1="60" y1="240" x2="700" y2="240" stroke="currentColor"/>
  <line x1="60" y1="240" x2="60" y2="40" stroke="currentColor"/>
  <line x1="56" y1="240" x2="700" y2="240" stroke="currentColor" opacity=".15"/>
  <text x="52" y="244" fill="currentColor" text-anchor="end" font-size="12">1</text>
  <line x1="56" y1="196" x2="700" y2="196" stroke="currentColor" opacity=".15"/>
  <text x="52" y="200" fill="currentColor" text-anchor="end" font-size="12">10</text>
  <line x1="56" y1="152" x2="700" y2="152" stroke="currentColor" opacity=".15"/>
  <text x="52" y="156" fill="currentColor" text-anchor="end" font-size="12">100</text>
  <line x1="56" y1="107" x2="700" y2="107" stroke="currentColor" opacity=".15"/>
  <text x="52" y="111" fill="currentColor" text-anchor="end" font-size="12">1000</text>
  <line x1="56" y1="63" x2="700" y2="63" stroke="currentColor" opacity=".15"/>
  <text x="52" y="67" fill="currentColor" text-anchor="end" font-size="12">10000</text>
  <rect x="80" y="57" width="44" height="183" class="f-acc" stroke="currentColor"/><text x="102" y="51" fill="currentColor" text-anchor="middle" font-size="12">14091</text><text x="102" y="258" fill="currentColor" text-anchor="middle">1</text>
  <rect x="148" y="80" width="44" height="160" class="f-acc" stroke="currentColor"/><text x="170" y="74" fill="currentColor" text-anchor="middle" font-size="12">4203</text><text x="170" y="258" fill="currentColor" text-anchor="middle">2</text>
  <rect x="216" y="96" width="44" height="144" class="f-acc" stroke="currentColor"/><text x="238" y="90" fill="currentColor" text-anchor="middle" font-size="12">1836</text><text x="238" y="258" fill="currentColor" text-anchor="middle">3</text>
  <rect x="284" y="111" width="44" height="129" class="f-acc" stroke="currentColor"/><text x="306" y="105" fill="currentColor" text-anchor="middle" font-size="12">813</text><text x="306" y="258" fill="currentColor" text-anchor="middle">4</text>
  <rect x="352" y="127" width="44" height="113" class="f-acc" stroke="currentColor"/><text x="374" y="121" fill="currentColor" text-anchor="middle" font-size="12">354</text><text x="374" y="258" fill="currentColor" text-anchor="middle">5</text>
  <rect x="420" y="145" width="44" height="95" class="f-acc" stroke="currentColor"/><text x="442" y="139" fill="currentColor" text-anchor="middle" font-size="12">144</text><text x="442" y="258" fill="currentColor" text-anchor="middle">6</text>
  <rect x="488" y="161" width="44" height="79" class="f-acc" stroke="currentColor"/><text x="510" y="155" fill="currentColor" text-anchor="middle" font-size="12">60</text><text x="510" y="258" fill="currentColor" text-anchor="middle">7</text>
  <rect x="556" y="182" width="44" height="58" class="f-acc" stroke="currentColor"/><text x="578" y="176" fill="currentColor" text-anchor="middle" font-size="12">21</text><text x="578" y="258" fill="currentColor" text-anchor="middle">8</text>
  <rect x="624" y="206" width="44" height="34" class="f-warn" stroke="currentColor"/><text x="646" y="200" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="646" y="258" fill="currentColor" text-anchor="middle">9</text><text x="380" y="278" fill="currentColor" text-anchor="middle" font-size="12">펄스 수 (= 낸 SCL↓ 수). 9 = 읽기 주소 ACK 중 리셋 + 첫 데이터 0x00 (3 위치 × 2종 = 6)</text><text x="380" y="296" fill="currentColor" text-anchor="middle" font-size="12">hang 수: 쓰기 3072 · 읽기 10008 · Sr 읽기 8448. 출처: demo 의 fold 행 = solution.c 히스토그램</text>
</svg>
```

읽는 법: 쓰기에서 hang 은 전부 1 펄스다 (3072 = 256 패턴 × ACK 슬롯 4개 × 리셋 위치 3개 — 슬레이브가 ACK 를 낸 뒤 마스터의 세 동작 SDA 놓기 · SCL↑ · SCL↓ 중 어느 것 대신 리셋돼도 슬레이브는 같은 ACK 상태). 읽기에서 9 는 3 개 — 패턴 v = 0x00 하나 × 주소 ACK 구간의 리셋 위치 3 개. `bad_pred=0` 은 `predict_pulses` 식이 9만 5천 시나리오 모두에서 시뮬레이션과 같았다는 뜻이고, 추가 연습 3 은 리셋 주입 없이 **상태 공간 1282 개**를 직접 만들어 같은 결론(9 는 단 한 상태)을 낸다.

`bad_invariant=0` 에는 쓰기 쪽 성질도 들어 있다: 복구 클럭이 슬레이브 수신 버퍼에 **새 바이트를 만들지 않는다**. 쓰기 hang 은 ACK 슬롯뿐이고 1 펄스 + STOP 으로 끝나므로 슬레이브가 받는 것은 1 비트 + STOP 이다 (이 모델은 미완성 바이트를 버린다; 실제 EEPROM 이 바이트 경계가 아닌 STOP 을 어떻게 다루는지는 데이터시트 확인 권장).

### Part B — 흔한 방식이 실패하는 이유

인터넷 예제와 면접 답안에 가장 많은 형태는 "SDA 를 놓고 SCL 을 토글하다가 SDA=1 이 보이면 STOP" 이다. SDA=1 이 보인 순간 SCL=1 이므로 STOP 을 만들려면 **SCL 을 한 번 내렸다 올려야** 한다. 그 SCL↓ 에서 슬레이브는 다음 비트를 낸다. 다음 비트가 0 이면 STOP 의 SDA↑ 가 wired-AND 에 막힌다. 0x40 (0100 0000) 을 읽기 주소 ACK 에서 리셋하면: 펄스1 → 비트7 = 0, 펄스2 → 비트6 = 1 (SDA=1 보임) → STOP 의 SCL↓ → 비트5 = 0 → STOP 실패, 버스는 다시 SCL=1·SDA=0. solution.c 의 `recover_naive()` 테스트가 정확히 이 결과(`REC_ERR_SDA_STUCK`, 슬레이브 `S_TX`)를 확인한다.

`demo_recovery_sweep.c` 로 네 방식을 같은 전수(21528 hang)에 돌린 실제 결과:

```text
algorithm        transaction         hung     ok     ok% maxclk avgclk  >9clk
fold (solution)  write 3B            3072   3072 100.00%      1   1.00      0
fold (solution)  read 3B            10008  10008 100.00%      9   1.76      0
fold (solution)  reg read (Sr) 2B    8448   8448 100.00%      9   1.64      0
naive            write 3B            3072   3072 100.00%      2   2.00      0
naive            read 3B            10008   6741  67.36%     10   2.76      3
naive            reg read (Sr) 2B    8448   6141  72.69%     10   2.64      3
naive+retry      write 3B            3072   3072 100.00%      2   2.00      0
naive+retry      read 3B            10008  10008 100.00%     10   3.86    102
naive+retry      reg read (Sr) 2B    8448   8448 100.00%     10   3.57    102
START+STOP       write 3B            3072   3072 100.00%      1   1.00      0
START+STOP       read 3B            10008  10008 100.00%      9   1.76      0
START+STOP       reg read (Sr) 2B    8448   8448 100.00%      9   1.64      0
innocent slave 0x68 during recovery, fold (solution) : STARTs 0, STOPs 21528 (hung scenarios 21528)
innocent slave 0x68 during recovery, naive           : STARTs 0, STOPs 15954 (hung scenarios 21528)
innocent slave 0x68 during recovery, naive+retry     : STARTs 0, STOPs 21528 (hung scenarios 21528)
innocent slave 0x68 during recovery, START+STOP      : STARTs 21528, STOPs 21528 (hung scenarios 21528)
clock histogram (all transactions, hung only):
  fold (solution)  1:14091 2:4203 3:1836 4:813 5:354 6:144 7:60 8:21 9:6
  naive            1:0 2:14091 3:4203 4:1836 5:813 6:354 7:144 8:60 9:21 10:6
  naive+retry      1:0 2:10776 3:2823 4:2595 5:1782 6:1317 7:945 8:654 9:432 10:204
  START+STOP       1:14091 2:4203 3:1836 4:813 5:354 6:144 7:60 8:21 9:6
```

| 방식 | 결론 (위 실측) |
|---|---|
| fold (펄스마다 STOP 시도) | 전부 성공, 최대 9 클럭, 복구가 만드는 조건은 STOP 1개 |
| naive (재시도 없음) | 읽기 hang 의 27–33% 실패 (67.36% / 72.69% 성공) — 남은 비트에 "1 다음 0" 이 있으면 |
| naive + 재시도 | 결국 다 풀지만 최대 **10 클럭**, 204 건이 9 를 넘음 — "9 클럭이면 충분"은 STOP 이 클럭을 안 쓴다는 전제 |
| START + STOP | 클럭 수는 fold 와 같다. 대신 SCL=1 에서 SDA 를 내려 START 를 만들고 바로 STOP — 모든 슬레이브가 **void message** 를 본다 (21528 번) |

UM10204 §3.1.10 은 "START 바로 뒤 STOP (void message)" 을 illegal format 이라고 하면서 "많은 디바이스가 이 조건에서 정상 동작하도록 설계되어 있다" 고 덧붙인다. 그래서 START+STOP 은 대부분 동작하지만, 규격상 깨끗한 쪽은 fold 다.

**실제 시간** (demo 출력, 클럭 W 개 × T_SCL + tSU;STO + tBUF, UM10204 Rev.7 Table 10 의 최소값으로 계산한 하한):

```text
worst-case recovery time (lower bound) for W clocks:
  Standard 100 kHz   W= 9:   98.70 us  W=10:  108.70 us
  Fast 400 kHz       W= 9:   24.40 us  W=10:   26.90 us
  Fast+ 1 MHz        W= 9:    9.76 us  W=10:   10.76 us
```

100 µs 안쪽이라 부트 시퀀스나 타임아웃 경로에서 부담이 없다. Linux 는 버스 속도와 상관없이 100 kHz 타이밍(반 주기 5 µs)으로 복구한다 — 느린 쪽에 맞춰 두면 어떤 디바이스에도 안전하기 때문이다.

### Part B — 고장별 결과

| 주입 | 반환 | 클럭 | 의미 / 다음 행동 |
|---|---|---|---|
| 0x12 MSB 에서 리셋 | `REC_OK`, pulses 3 | 3 | 정상 복구 |
| 읽기 주소 ACK + 0x00 | `REC_OK`, pulses 9 | 9 | 최악 정상 |
| SDA 영구 LOW | `REC_ERR_SDA_STUCK`, pulses 9 | 9 | 프로토콜로 설명 안 됨 → 리셋 핀/전원 (Part C) |
| SCL 영구 LOW | `REC_ERR_SCL_STUCK`, pulses 0 | 0 | 클럭 불가 → 전원, 범인 찾기 |
| SCL 5틱 붙잡힘 | `REC_OK`, pulses 3, `scl_wait_ticks` > 0 | 3 | 스트레칭 존중 |
| 펄스 2 부터 SCL 고착 | `REC_ERR_SCL_STUCK`, pulses 2 | 2 | 풀린 뒤 다시 부르면 `REC_OK` |
| 이미 idle | `REC_OK`, pulses 0 | 0 | 아무것도 안 함 |

`SDA_STUCK` 과 `SCL_STUCK` 을 나누는 이유는 다음 행동이 다르기 때문이다. SDA 만 붙었다면 9 클럭이 실패한 것 = 슬레이브 내부가 프로토콜 밖에 있다(래치업, 펌웨어가 있는 슬레이브 MCU 의 행, 전원 이상). SCL 이 붙었다면 클럭 스트레칭을 끝내지 못하는 슬레이브거나 쇼트다. SMBus 디바이스는 SCL low 가 t_TIMEOUT(25–35 ms, 대표값 — SMBus 규격 확인 권장)을 넘으면 스스로 인터페이스를 리셋해야 하지만, **SCL=1·SDA=0 hang 은 그 타임아웃 대상이 아니다** — 그래서 마스터 쪽 9클럭 복구가 필요하다.

### Part C — 드라이버 경로

```text
dev_ensure_idle(d):   idle 이면 OK
                      로그 → i2c_bus_recover → OK 면 끝
                      실패 && power_cycle 훅 → 전원 재인가 → i2c_bus_recover → OK 면 끝
                      n_recover_fail++, 로그 → I2C_ERR_BUS_STUCK
i2c_xfer_robust(d,op): attempt 1..2 { ensure_idle (실패면 그대로 반환) → 전송
                                      STOP_FAILED 가 아니면 반환 (OK, NACK 등) → n_retry++, 로그 }
                      끝까지 STOP_FAILED → dev_ensure_idle 로 버스 정리 후 반환
```

부르는 자리는 네 곳이다: ① **init** — 부트 직후 첫 트랜잭션 전 (리셋 hang 은 여기서 발견된다) ② **START 전 busy** — 다른 코드(부트로더, 다른 태스크, 디버거 스크립트)가 남긴 hang ③ **STOP 실패** — 내 트랜잭션이 끝났는데 SDA 가 0 ④ **타임아웃 경로** — 하드웨어 I2C 가 BUSY/타임아웃 인터럽트를 내면 (STM32 의 TIMEOUT/BERR 류 — RM 확인 권장). 이 레슨의 테스트는 ①②③ 을 실행한다. 실제 로그 (solution.c 출력):

```text
--- bring-up log ---
[t=   30] i2c0: boot after watchdog reset
[t=   30] i2c0: bus not idle before START (SCL=1 SDA=0) -> recovery
[t=   68] i2c0: recovered after 9 pulse(s), STOP sent
[t=  267] i2c0: STOP failed on read 0x50 (SDA held low), attempt 1
[t=  267] i2c0: bus not idle before START (SCL=1 SDA=0) -> recovery
[t=  289] i2c0: recovered after 5 pulse(s), STOP sent
[t=  376] i2c0: bus not idle before START (SCL=1 SDA=0) -> recovery
[t=  414] i2c0: recovery failed: SDA still low after 9 clocks (pulses=9)
[t=  415] i2c0: power-cycled sensor rail
[t=  417] i2c0: bus idle after power cycle (pulses=0)
[t=  531] i2c0: bus not idle before START (SCL=1 SDA=0) -> recovery
[t=  569] i2c0: recovery failed: SDA still low after 9 clocks (pulses=9)
[t=  569] i2c0: giving up: bus stuck, report fault
[t=  571] i2c0: bus not idle before START (SCL=0 SDA=1) -> recovery
[t=  589] i2c0: recovery failed: SCL stuck low (pulses=0)
[t=  589] i2c0: giving up: bus stuck, report fault
--- end log (958 bytes) ---
random robust: 300 ops, 111 preceded by reset, 23 recoveries, bad=0
69/69 passed
```

브링업 엔지니어가 이 로그에서 읽는 것: 줄 2–3 = 워치독 리셋이 읽기 주소 ACK 에서 걸렸고(9 펄스) 센서 FIFO 는 이어서 정상 데이터를 줬다. 줄 4–6 = ACK 슬롯 글리치 → 슬레이브가 다음 바이트 0x05 의 MSB(0)를 쥠 → STOP 실패 → 5 펄스(0x05 = 0000 0101: 비트 6·5·4·3 = 0, 비트 2 = 1) → 재시도 성공. 이런 줄이 현장에서 반복되면 SDA 신호 품질(rise time, 잡음)을 보라는 신호다. 줄 7–10 = 9 클럭으로 안 풀리는 디바이스를 전원 재인가로 살림. 줄 11–13 = 같은 고장인데 전원 스위치가 없는 보드 → 상위에 `BUS_STUCK` (센서 고장 플래그, 안전 상태 진입은 앱 몫). 줄 14–16 = SCL 원인. "300 ops, 111 preceded by reset, 23 recoveries" — 리셋 111 번 중 hang 을 남긴 것은 23 번뿐이다. 나머지는 리셋 순간 슬레이브가 SDA 를 쥐고 있지 않았다(전수의 22.5% 와 같은 성격의 비율).

**재시도 정책은 한 곳.** 이 계층은 버스 수준 고장(hang, STOP 실패)만 다루고, NACK 은 그대로 올린다. 센서가 바쁘다는 NACK 을 여기서도 재시도하고 위에서도 재시도하면 최악 지연이 곱해진다 (COMM-M12 의 계층 규칙).

### 실제 칩에서

| 항목 | 내용 (확인 권장 표시는 문서 미확인) |
|---|---|
| 핀 전환 | 하드웨어 I2C 블록은 임의 SCL 펄스를 못 낸다. STM32 라면 SCL/SDA 핀을 GPIO 출력 오픈드레인으로 바꿔 비트뱅 → idle 확인 → AF 로 되돌림 (GPIOx_MODER/OTYPER — RM 확인 권장) |
| 주변장치 리셋 | 핀이 돌아와도 I2C 블록의 BUSY 플래그가 남을 수 있다. 구형 블록(F1/F4) 은 `I2C_CR1.SWRST`, 신형 블록(F0/F3/F7/L4/G4/H7 등) 은 `PE` 를 0 으로 해 소프트 리셋 (RM 확인 권장). STM32F1 errata 의 BUSY 고착 항목도 GPIO 토글로 우회 (errata sheet 확인 권장) |
| Linux | `struct i2c_bus_recovery_info` (get/set_scl, get/set_sda, get_bus_free, prepare/unprepare_recovery, pinctrl "gpio" 상태) + `i2c_recover_bus()` → `i2c_generic_scl_recovery()` (9 클럭, 펄스마다 STOP, SCL 고착 감지). DT 의 `scl-gpios`/`sda-gpios` 로 연결 (바인딩 문서 확인 권장) |
| Zephyr | `i2c_recover_bus()` API 가 있고 드라이버마다 구현 여부가 다르다 (확인 권장) |
| 소프트웨어 리셋 | UM10204 §3.1.14: general call(0x00) 뒤 0x06 = software reset (선택 기능, 모든 디바이스가 지원하진 않음). SDA 가 붙어 있으면 보낼 수 없으므로 9클럭 복구 **뒤**에 쓰는 보조 수단 |
| 하드웨어 대비 | 슬레이브 리셋 핀을 MCU GPIO 에 연결, 센서 전원 로드 스위치, I2C 멀티플렉서/버스 스위치(PCA954x 류)로 세그먼트 분리 — 범인 격리와 전원 재인가 수단 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 69/69 passed, exit 0 (약 0.3 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음 (약 0.9 초)
cc -std=c11 -Wall -Wextra -O2 demo_recovery_sweep.c -o demo && ./demo           # 4 방식 비교 표 (약 0.9 초)
```

(Apple clang 21.0.0, Apple Silicon 에서 측정한 실행 시간 — 참고용.)

## 7. 자주 하는 실수

**1. SDA=1 을 본 뒤 "SCL 내리고 STOP".** 위 실측대로 읽기 hang 의 27–33% 가 다시 붙는다. 그 SCL↓ 이 슬레이브에게 다음 비트를 내게 하기 때문이다. 고친 코드: 펄스마다 `SCL↓ → SDA↓ → SCL↑ → SDA↑` (fold), 또는 SCL=1 에서 바로 START+STOP.

**2. SCL=1 일 때 SDA 를 내려 놓고 STOP 이라고 생각한다.** SCL=1 중 SDA↓ 는 START 다. 의도치 않은 START 는 모든 슬레이브를 주소 수신 상태로 만든다 — 그 직후 STOP 이 오면 괜찮지만, 그 사이 클럭을 내면 주소 바이트로 해석된다. 고친 코드: SDA 를 내리는 것은 SCL=0 일 때만.

**3. SCL 을 되읽지 않는다.** `set_scl(1)` 뒤 바로 SDA 를 놓으면, SCL 을 붙잡은 슬레이브(스트레칭)에게는 SCL=0 중 SDA 변화로 보여 STOP 이 안 생기고, SCL 고착은 "SDA_STUCK" 으로 잘못 분류된다. 고친 코드: `wait_scl_high()` + 상한 → `SCL_STUCK`.

**4. 펄스 수 제한이 없다.** `while (!sda) toggle();` 는 SDA 를 영구히 쥔 디바이스 앞에서 영원히 돈다 — 부트 시퀀스가 멈추고 워치독이 다시 리셋한다. 고친 코드: 9 번 + 구분된 에러 코드 + 상위의 단계적 대응.

**5. 복구 뒤 I2C 주변장치를 리셋하지 않는다.** 핀은 GPIO 로 버스를 풀었지만 하드웨어 블록은 "BUSY" 상태를 기억하고 START 를 거부한다. 고친 순서: 복구 → 핀을 AF 로 → 주변장치 소프트 리셋 → 재설정(타이밍 레지스터) → 첫 트랜잭션.

**6. init 에서 버스 상태를 안 본다.** 리셋 hang 은 리셋 직후에만 생긴다. 첫 트랜잭션이 `BUSY` 로 실패하고 그 에러를 "센서 없음" 으로 해석하면 현장에서 "가끔 센서가 안 잡힌다" 버그가 된다. 고친 코드: init 첫 줄에 `dev_ensure_idle()`.

**7. 복구 클럭 중 데이터를 믿는다.** 복구 직전 읽은 바이트, 그리고 STOP 실패 뒤의 버퍼는 트랜잭션이 깨끗이 끝나지 않은 결과다. 이 레슨의 `i2c_xfer_robust` 는 STOP 실패면 전송 전체를 다시 한다.

## 8. 면접 Q&A

<details><summary>Q1. SDA 가 low 로 붙었습니다 (SCL 은 high). 원인과 복구 절차를 말해 보세요.</summary>

원인: 슬레이브가 0 비트를 내는 도중(읽기 데이터 또는 ACK) 마스터가 리셋되거나 트랜잭션을 버렸다. 슬레이브는 다음 SCL↓ 을 기다리며 SDA 를 계속 쥔다. 절차: ① SCL/SDA 핀을 GPIO 오픈드레인으로 ② 두 선을 놓고 SCL 이 1 인지 되읽기 (아니면 SCL 고착 — 다른 처리) ③ SDA 가 1 이 될 때까지 SCL 을 최대 9 번 토글, 매 펄스를 SCL=0 에서 SDA 당김 → SCL=1 → SDA 놓기로 만들어 풀리는 순간 STOP 이 생기게 ④ idle 확인 ⑤ 핀을 I2C 로 되돌리고 주변장치 리셋 ⑥ 9 번에 안 풀리면 리셋 핀/전원 재인가, 그래도 안 되면 고장 보고. 꼬리: "어디서 부르나요?" — init, START 전 busy, STOP 실패, 타임아웃 경로.

</details>

<details><summary>Q2. 왜 하필 9 클럭인가요? 8 이면 안 되나요?</summary>

슬레이브가 SDA 를 0 으로 쥘 수 있는 가장 긴 연속 구간이 "자기 ACK 1 + 데이터 8 비트" 이기 때문이다. 읽기 주소에 ACK 하던 중 리셋되고 내보낼 바이트가 0x00 이면, 1 번째 SCL↓ 에 MSB(0), … 8 번째에 비트 0(0), 9 번째에 ACK 슬롯 — 마스터 차례라 그제야 놓는다. 데이터 송신 중 리셋이면 이미 비트 하나를 냈으므로 최대 8. 쓰기 ACK 면 1. 이 레슨에서 9만 5천 리셋 위치 전수와 상태 공간 1282 개 전수로 확인했고, 9 가 필요한 상태는 정확히 하나다. 꼬리: "STOP 을 만드는 데 클럭을 하나 더 쓰면?" — 10 이 필요할 수 있다 (흔한 방식 + 재시도의 실측 최대 10).

</details>

<details><summary>Q3. 이번엔 SCL 이 low 로 붙어 있습니다.</summary>

마스터가 클럭을 낼 수 없으므로 9 클럭 복구는 무효다. 먼저 정말 고착인지 스트레칭인지 가린다 — 놓고 되읽으며 기다렸다가 상한을 넘기면 고착. SMBus 디바이스는 SCL low 가 25–35 ms(대표값)를 넘으면 스스로 인터페이스를 리셋해야 하므로 그만큼 기다려 볼 가치가 있다. 그다음은 UM10204 §3.1.16 대로 HW 리셋 신호(있다면) 또는 전원 재인가로 POR 을 건다. 범인 찾기: 디바이스를 하나씩 분리(버스 스위치/멀티플렉서, 0 Ω 저항, 전원 차단)하고, SCL 전압을 재서 0 V 에 붙었는지 중간 전압인지(드라이버 다툼·누설·레벨 시프터 의심) 본다.

</details>

<details><summary>Q4. 복구 클럭이 같은 버스의 다른 디바이스에 해를 끼치지 않나요?</summary>

fold 방식은 SDA 를 SCL=0 일 때만 내리므로 START 가 생기지 않는다. 다른 슬레이브는 IDLE 이라 클럭을 무시하고, 마지막 STOP 하나만 본다 — 전수에서 무고한 0x68 의 START 증가 0 을 확인했다. START+STOP 방식은 모든 디바이스에 void message 를 보인다(규격상 illegal, 대부분 견딤). 더 조심할 것은 **hang 을 만든 그 슬레이브**다: 쓰기 중이었다면 복구가 1 비트 + STOP 을 보내므로, 그 디바이스가 미완성 바이트·바이트 경계 밖 STOP 을 어떻게 다루는지(EEPROM 이면 쓰기 사이클을 시작하는지) 데이터시트로 확인해야 한다.

</details>

<details><summary>Q5. "SDA=1 이 될 때까지 토글하고 STOP" 이라고 답했는데 면접관이 "그 STOP 어떻게 만들죠?" 라고 묻습니다.</summary>

SDA=1 이 보이는 순간은 SCL=1 이다. STOP 은 SCL=1 중 SDA 0→1 이므로 SDA 를 먼저 0 으로 만들어야 하는데, 지금 내리면 START 이고, SCL 을 먼저 내리면 그 SCL↓ 이 슬레이브에게 다음 비트를 내게 한다. 다음 비트가 0 이면 STOP 이 막혀 다시 붙는다 — 이 레슨 실측으로 읽기 hang 의 27–33%. 해결은 둘: (a) 매 펄스를 STOP 모양으로(SDA 가 SCL 을 반 박자 늦게 따라감, Linux 방식), (b) SCL=1 인 채로 START 후 STOP. (a) 는 조건을 STOP 하나만 만들고, (b) 는 void message 를 만든다.

</details>

<details><summary>Q6. STM32 HAL 로 구현한다면 무엇을 조심하나요?</summary>

하드웨어 I2C 블록은 임의 클럭을 못 내므로 `HAL_I2C_DeInit` → 핀을 GPIO 출력 오픈드레인(풀업은 외부)으로 → 비트뱅 복구 → 핀을 AF 로 → I2C 블록 소프트 리셋(구형 블록 SWRST, 신형 블록 PE 토글 — RM 확인 권장) → `HAL_I2C_Init`. 함정: GPIO 로 바꾸는 순간 출력 래치 값이 0 이면 SCL/SDA 에 글리치가 생기므로 ODR 을 먼저 1 로 두고 모드를 바꾼다. 일부 STM32 세대는 BUSY 플래그 고착 errata 가 있어 같은 GPIO 토글 절차가 우회법으로 적혀 있다 (칩별 errata sheet 확인). 꼬리: "RTOS 에서 다른 태스크가 같은 버스를 쓰고 있으면?" — 복구도 버스 뮤텍스 안에서 (COMM-M13, RTOS-B 뮤텍스 레슨).

</details>

<details><summary>Q7. Linux 에서는 누가 복구하나요?</summary>

어댑터 드라이버가 `struct i2c_bus_recovery_info` 를 채워 두면 코어가 `i2c_recover_bus()` 로 부른다 — 보통 전송 타임아웃이나 "bus busy" 를 만난 드라이버가 호출한다. 기본 구현 `i2c_generic_scl_recovery()` 는 최대 9 클럭, 100 kHz 타이밍(반 주기 5 µs), SDA 가 SCL 을 반 박자 늦게 따라가 매 펄스 STOP 을 만들고, high 구간마다 bus free 를 확인하며, SCL 이 high 여야 할 때 low 면 "SCL is stuck low" 로 -EBUSY 를 낸다 (i2c-core-base.c). GPIO 로 핀을 바꾸는 것은 pinctrl 의 "gpio" 상태와 `scl-gpios`/`sda-gpios` 로 연결한다 (바인딩 확인 권장). 꼬리: "SDA 를 GPIO 로 못 쓰는 보드면?" — set_sda 없이 SCL 만 토글하고 SDA 를 읽기만 한다; 그러면 STOP 을 못 만들어 실수 1 과 같은 위험이 남는다.

</details>

<details><summary>Q8. 9 클럭으로도 안 풀립니다. 다음은?</summary>

슬레이브가 프로토콜 밖에 있다는 뜻이다(래치업, 슬레이브 MCU 펌웨어 행, 브라운아웃 중간 상태, 두 디바이스가 같은 주소로 싸움). 단계: 리셋 핀 → 센서 전원 로드 스위치로 재인가(이 레슨의 `power_cycle` 훅) → 그래도 안 되면 고장 플래그, 해당 센서 기능을 끄고 안전 상태로. 하드웨어 설계 리뷰 때 이 경로를 미리 요구하는 것이 펌웨어 엔지니어의 일이다: 센서 전원 스위치, 리셋 핀, 버스 스위치, 테스트 포인트 (HWLAB-B01 의 넷리스트 검토). 원인 분석은 hang 직전 캡처 — LA 프리트리거로 마지막 트랜잭션과 리셋 시각을 잡는다 (SIV-A07).

</details>

## 9. 추가 연습

### 연습 1 — START+STOP 변형과 void message

SDA 를 놓은 채 SCL 만 토글하고, SDA=1 이 보이면 SCL=1 인 채로 SDA↓(START) → SDA↑(STOP) 하는 `recover_sp(b, &clocks)` 를 짜라. 읽기·Sr 읽기 전수에서 클럭 수가 `predict_pulses` 와 같은지, 무고한 슬레이브가 몇 번의 START 를 보는지 세어라.

<details><summary>답 보기</summary>

```c
set_sda(b, 1); set_scl(b, 1);
if (b->sda) r = REC_OK;
for (p = 1; p <= 9 && r != REC_OK; p++) {
    set_scl(b, 0); set_scl(b, 1);
    if (b->sda) { set_sda(b, 0); set_sda(b, 1); r = idle ? REC_OK : REC_ERR_SDA_STUCK; break; }
}
```

실행 결과 `ex1 START+STOP: hung=18357 bad=0 max clocks=9, STARTs seen by innocent slave=18357` — 클럭 수는 fold 와 같고 최대 9, 그 대신 hang 하나마다 주소 없는 START 하나(void message). START 가 hang 슬레이브의 상태 머신을 주소 수신으로 초기화하므로, 1 비트를 내던 슬레이브도 바로 STOP 으로 IDLE 이 된다. (hang 수가 본문 18456 과 다른 것은 세 번째 데이터 바이트의 난수열이 다르기 때문 — 이 연습은 쓰기를 건너뛰고 시드를 다시 건다.)

</details>

### 연습 2 — 흔한 방식에 재시도를 붙이면 몇 클럭?

`recover_naive_retry(b, budget, &clocks)`: SDA=0 이면 클럭, SDA=1 이면 SCL↓ 후 STOP, STOP 뒤에도 SDA=0 이면 계속. 전수에서 최대 클럭을 재고, 읽기 주소 ACK + 0x40 이 몇 클럭인지, 예산 9 로는 어떻게 되는지 보여라.

<details><summary>답 보기</summary>

0x40 = 0100 0000: 클럭1 → 비트7(0), 클럭2 → 비트6(1) 보임 → STOP 의 클럭3 → 비트5(0) → 실패, 클럭4–8 → 비트4..0(0), 클럭9 → ACK 슬롯(놓음) → STOP 의 클럭10 → 슬레이브는 NACK 을 보고 IDLE → 성공. 실행 결과 `ex2 naive+retry: fail=0 max clocks=10, cases over 9 clocks=204`, 이름 붙은 경우 `clk == 10`, 예산 9 면 `REC_ERR_SDA_STUCK`. 교훈: 9 라는 숫자는 "STOP 이 클럭을 쓰지 않는" 구현을 전제로 한다.

</details>

### 연습 3 — 상태 공간으로 증명하기

리셋 주입 대신, SDA 를 쥘 수 있는 슬레이브 상태를 **직접** 만들어(`place_state`) 복구를 돌려라: 쓰기 ACK 2종, 읽기 주소 ACK × 다음 바이트 256, 송신 중 nbits 1..8 × 현재 비트가 0 인 sh 128 개. 닫힌 식 `pulses_formula` 와 비교하고 최댓값과 그 개수를 구하라.

<details><summary>답 보기</summary>

상태 수 = 1 + 1 + 256 + 8 × 128 = 1282. 실행 결과 `ex3 state space: 1282 states, mismatches=0, max pulses=9 (count 1)`. 9 는 "읽기 주소 ACK + 다음 바이트 0x00" 단 하나. 리셋 주입 전수(본문)는 "실제로 도달하는 상태"를, 이 연습은 "도달 가능 여부와 상관없이 SDA 를 쥘 수 있는 모든 상태"를 덮는다 — 둘이 같은 결론을 내면 모델 안에서 9 는 증명된 상한이다.

</details>

### 연습 4 — 1 ms 버스 감시 (hang monitor)

드라이버가 쉬고 있을 때 1 ms 타이머에서 `hang_mon_sample(m, sda, scl, drv_busy)` 를 부른다. SDA LOW·SCL HIGH 가 `thresh` 샘플 연속이면 `HANG_SDA`, SCL LOW 가 연속이면 `HANG_SCL` 을 **한 번만** 보고하라 (드라이버 전송 중이면 카운터 리셋). 모델 버스에 붙여 리셋 hang 을 감지·복구하라.

<details><summary>답 보기</summary>

```c
if (drv_busy || (sda && scl)) { m->n_sda = m->n_scl = 0; return HANG_NONE; }
if (!scl) { m->n_sda = 0; return ++m->n_scl == m->thresh ? HANG_SCL : HANG_NONE; }
m->n_scl = 0;
return ++m->n_sda == m->thresh ? HANG_SDA : HANG_NONE;
```

`==` 비교라 임계에 닿는 샘플에서 한 번만 보고하고, 둘 다 LOW 면 SCL 원인으로 분류한다(클럭을 못 내므로). 모델에서 10틱마다 샘플 → 3 번째 샘플에서 `HANG_SDA` → `i2c_bus_recover` 9 펄스 → 정상 트랜잭션. 왜 필요한가: SMBus 타임아웃은 SCL low 만 다루므로, 아무도 I2C 를 쓰지 않는 시간에 생긴 SDA hang 은 다음 사용자가 BUSY 를 맞을 때까지 숨어 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed (약 0.3 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 원인: I2C 에는 리셋 신호가 없다. 마스터가 리셋되면 핀이 놓여 SCL=1, 0 비트(데이터·ACK)를 내던 슬레이브는 SDA 를 쥔 채 다음 SCL↓ 을 기다린다.
- 진단: SCL=1·SDA=0 → 9클럭 복구. SCL=0 → 클럭 불가, HW 리셋/전원 (UM10204 §3.1.16).
- 9 = 슬레이브 자신의 ACK 1 + 데이터 8 비트. 최악은 "읽기 주소 ACK + 다음 바이트 0x00" 하나 (전수 95488 + 상태 공간 1282 로 확인).
- STOP 을 만드는 SCL↓ 도 클럭이다. 펄스마다 `SCL↓ · SDA↓ · SCL↑ · SDA↑` 로 STOP 을 접어 넣으면 풀리는 순간이 곧 STOP (Linux 방식). "보이면 SCL 내리고 STOP" 은 읽기 hang 의 27–33% 에서 다시 붙는다.
- SCL 은 항상 되읽고 상한을 둔다: 스트레칭은 기다리고, 고착은 `SCL_STUCK` 으로 따로 보고.
- 반환 코드 세 개(OK / SDA_STUCK / SCL_STUCK)는 다음 행동이 달라서 나눈다 → 상위는 전원 재인가 → BUS_STUCK.
- 부르는 자리: init, START 전 busy, STOP 실패, 타임아웃. 복구 뒤 핀을 되돌리고 주변장치를 리셋. 재시도 정책은 한 계층에만.
- 시간: 9 클럭 ≈ 99 µs @100 kHz (tSU;STO + tBUF 포함 하한) — 부트 경로에 넣어도 싸다.

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual*, Rev. 7.0 (2021-10-01): §3.1.16 Bus clear (SDA stuck → nine clock pulses, 안 되면 HW reset/power cycle, SCL stuck → HW reset/power cycle), §3.1.10 void message, §3.1.14 Software reset (general call + 0x06), Table 10 (tSU;STO, tBUF) — 이 레슨 작성 시 PDF 텍스트로 확인: <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>
- Linux kernel, `drivers/i2c/i2c-core-base.c` — `i2c_generic_scl_recovery()`, `i2c_generic_bus_free()`, `RECOVERY_CLK_CNT` = 9, `RECOVERY_NDELAY` = 5000 ns (2026-11 master 에서 확인): <https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-base.c>
- Linux kernel docs, "I2C/SMBus Subsystem" — `struct i2c_bus_recovery_info`, `i2c_recover_bus()`: <https://docs.kernel.org/i2c/index.html>
- SMBus Specification 3.x — t_TIMEOUT(SCL low 25–35 ms)와 디바이스 쪽 타임아웃 리셋 (수치 확인 권장): <http://smbus.org/specs/>
- ST, STM32 Reference Manual 의 I2C 장(SWRST / PE 소프트 리셋, BUSY 플래그)과 GPIO 장, 각 칩의 errata sheet I2C 항목 (확인 권장)
- Zephyr, I2C API `i2c_recover_bus()` (드라이버별 지원 여부 확인 권장): <https://docs.zephyrproject.org/latest/hardware/peripherals/i2c.html>
- 관련 레슨: COMM-B06 (이 모델의 원판 — wired-AND, START/STOP, 슬레이브 상태 머신), COMM-B07 (repeated START·레지스터 읽기, 마지막 바이트 ACK 버그 → 이 레슨의 hang), COMM-M01 (클럭 스트레칭 — SCL 되읽기), COMM-M02 (멀티마스터 중재), COMM-M12 (계층별 에러 변환·재시도 정책), COMM-M13 (공유 버스 트랜잭션 큐), HWLAB-B03 (풀업·rise time), HWLAB-B01 (넷리스트 검토 — 리셋 핀·전원 스위치), SIV-A07 (LA 캡처로 hang 진단·9클럭 복구 판정), RTOS-B 뮤텍스 레슨 (버스 공유), BM-M04 (실제 드라이버 — 다음 단계)
