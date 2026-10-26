# J1 · Decode CAN Signals and Monitor CANopen Heartbeats

> **ID**: J1 · **Priority**: P0 · **Pattern**: bit extraction + sign extension + scale/offset + per-node timeout · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh J1` / `./run.sh J1 sol`

## 1. Problem

A robot's motor drives talk **CANopen** over a classic CAN bus (11-bit ids, up to 8 data bytes). Write the receive side on the main controller.

**Part A — generic signal extraction.** Signals use the **Intel / little-endian** layout (DBC `@1`): bit `k` of the frame lives in `data[k / 8]`, bit `k % 8`. A signal of `len` bits starting at `start_bit` occupies bits `start_bit .. start_bit + len - 1`, and `start_bit` is its LSB.

- `can_get_bits()` returns the raw unsigned value. `1 <= len <= 32`, `start_bit + len <= 64`; invalid arguments return `0`.
- `can_get_signed()` returns the same bits as a **two's-complement** signed value. No undefined behaviour — watch out for `len == 32` and shifts by 32.

**Part B — decode a TPDO1 from a motor drive.** COB-ID = `0x180 + node_id`, `node_id` in `1..127`. Reject (return `false`, leave `*out` untouched) a wrong function code, node `0`, an id that does not fit in 11 bits, or `dlc < 8`. Integer math only.

| Bits | Signal | Type | Scaling → stored field |
|---|---|---|---|
| 0..15 | speed | signed 16 | 1 rpm → `speed_rpm` |
| 16..31 | current | signed 16 | 0.1 A → `current_ma` (mA) |
| 32..39 | temperature | unsigned 8 | 1 °C, offset −40 → `temp_c` |
| 40..47 | status | 8 flags | bit 0 = `fault`, bit 1 = `enabled` |
| 48..59 | bus voltage | unsigned 12 | 0.1 V → `bus_mv` (mV) |
| 60..63 | rolling counter | unsigned 4 | → `counter` |

**Part C — heartbeat monitor.** Every node sends a heartbeat with COB-ID `0x700 + node_id` and 1 data byte = NMT state (`0x00` boot-up, `0x04` stopped, `0x05` operational, `0x7F` pre-operational). Track nodes `1..8` (ignore others and frames with `dlc == 0`). `hb_check()` returns a bitmask (bit `n-1` = node `n`) of nodes that were **seen at least once** and are now **timed out** (`now - last > timeout`, must be correct when the `uint32_t` millisecond clock wraps) **or** whose last state is **not operational**. A boot-up message mid-run means the node rebooted → report it.

```c
uint32_t can_get_bits  (const uint8_t data[8], uint8_t start_bit, uint8_t len);
int32_t  can_get_signed(const uint8_t data[8], uint8_t start_bit, uint8_t len);

typedef struct {
    uint8_t  node_id;
    int16_t  speed_rpm;
    int32_t  current_ma;
    int16_t  temp_c;
    bool     fault, enabled;
    uint32_t bus_mv;
    uint8_t  counter;
} MotorStatus;
bool decode_motor_status(uint32_t can_id, uint8_t dlc, const uint8_t data[8], MotorStatus *out);

typedef struct {
    uint32_t timeout_ms;
    uint32_t last_ms[8];
    uint8_t  state[8];
    uint8_t  seen;          /* bit n-1 = node n has sent a heartbeat */
} HbMon;
void    hb_init    (HbMon *m, uint32_t timeout_ms);
void    hb_on_frame(HbMon *m, uint32_t can_id, uint8_t dlc, const uint8_t *data, uint32_t now_ms);
uint8_t hb_check   (HbMon *m, uint32_t now_ms);
```

### Example

```
id = 0x183, dlc = 8, data = DC 05 83 FF 69 02 E0 51

can_get_bits(data, 0, 16)    = 0x05DC = 1500
can_get_signed(data, 16, 16) = 0xFF83 -> -125
decode_motor_status -> true:
  node_id 3, speed_rpm 1500, current_ma -12500, temp_c 65,
  fault 0, enabled 1, bus_mv 48000, counter 5

heartbeat monitor, timeout = 1000 ms:
  t=0     0x701 [05], 0x702 [05]
  t=900   hb_check -> 0x00
  t=1000  0x701 [05]
  t=1500  hb_check -> 0x02          (node 2 silent for 1500 ms)
  t=1600  0x702 [00]                (node 2 rebooted: boot-up)
  t=1700  hb_check -> 0x02          (fresh, but not operational)
```

### Constraints

- Classic CAN only (8-byte payload). Only 11-bit ids are valid for Part B / C.
- No `malloc`, no floating point. `HbMon` is a fixed-size struct the caller owns.
- `hb_check` is called from a periodic task; `hb_on_frame` from the CAN RX path.

### Clarifying questions to ask (first 3 minutes)

1. **Byte order**: Intel (little-endian, `@1`) or Motorola (big-endian, `@0`)? For Motorola, is `start_bit` the MSB in DBC "sawtooth" numbering?
2. **DLC**: reject `dlc < 8`, or decode only the signals that fit? What about `dlc > 8` (classic CAN DLC 9..15 still means 8 bytes)?
3. **Extended 29-bit ids**: can they appear on this bus (e.g. J1939 devices)? Should the driver pass an IDE flag so I can reject them explicitly?
4. Units of the output: keep raw + scale, or convert to mA / mV in integers? Is `int32_t` range enough?
5. Heartbeat: is the timeout strict (`>`) or inclusive? Should a node that was never seen be reported as missing?
6. Is `hb_on_frame` called from an ISR while `hb_check` runs in a task (concurrency)?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh J1`로 바로 테스트하세요. 테스트 12묶음이 들어 있습니다(Part A 1–4, Part B 5–7, Part C 8–12). 정상 프레임이 먼저, 그다음 음수·`len 32`·바이트 경계·잘못된 id·짧은 DLC·타이머 wrap 순서입니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 8바이트를 uint64_t 하나로 합치기</summary>

Intel 순서에서는 `data[0]`이 가장 낮은 바이트입니다. `for (i = 7; i >= 0; i--) v = (v << 8) | data[i];`로 64비트 정수 하나를 만들면, 신호는 그냥 `(v >> start_bit) & mask`입니다. 비트별 루프로 풀어도 맞지만 이쪽이 짧고 실수가 적습니다.

</details>

<details>
<summary>Hint 2 — mask와 부호 확장에서 UB 피하기</summary>

`(1u << len) - 1`은 `len == 32`일 때 **UB**입니다(32비트 값을 32만큼 shift). `((uint64_t)1 << len) - 1`로 64비트에서 계산하세요. 부호 확장은 "MSB가 1이면 `2^len`을 뺀다"가 가장 안전합니다: `(int32_t)((int64_t)raw - ((int64_t)1 << len))`. `uint32_t`를 바로 `int32_t`로 캐스트하는 것은 값이 범위를 넘으면 implementation-defined입니다.

</details>

<details>
<summary>Hint 3 — COB-ID 나누기와 wrap-safe 타임아웃</summary>

11비트 COB-ID = 상위 4비트 function code + 하위 7비트 node id입니다. `(id & 0x780) == 0x180`, `node = id & 0x7F`. 타임아웃은 반드시 `(uint32_t)(now - last) > timeout` — **뺄셈 한 번**으로 비교합니다. `now > last + timeout`은 wrap 근처에서 틀립니다(테스트 12).

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 비트 레이아웃 그림

예제 프레임을 바이트(행) × 비트(열) 격자로 그리면, Intel 순서의 의미가 바로 보입니다. 각 칸 오른쪽 위 숫자가 프레임 전체 기준 비트 번호 `k`이고, `k = byte*8 + bit`입니다. 신호는 이 번호가 **연속**이면 되므로, bus_voltage(48..59)처럼 byte 6 전체 + byte 7의 **아래 nibble**에 걸쳐도 `>>`와 `&` 한 번이면 됩니다.

<div class="chart" data-src="chart1.svg" data-caption="예제 프레임 DC 05 83 FF 69 02 E0 51. 색 = 신호. byte 7은 아래 nibble이 전압 MSB(0x1), 위 nibble이 counter(5) — 바이트 경계를 넘는 필드"></div>

부호 있는 값은 "읽은 다음 부호 확장"입니다. current는 raw `0xFF83` = 65411이고 MSB(bit 15)가 1이므로 `65411 − 65536 = −125`, 0.1 A 단위라 `−125 × 100 = −12500 mA`입니다.

### 4.2 CANopen COB-ID와 heartbeat 감시

Part B와 C는 같은 COB-ID 분해를 씁니다. `0x183` = function `0x180`(TPDO1) + node 3, `0x702` = function `0x700`(heartbeat) + node 2. 감시는 노드마다 `last_ms`, `state` 두 값과 `seen` 비트마스크만 있으면 됩니다.

<div class="chart" data-src="chart2.svg" data-caption="테스트 8–11 타임라인(timeout 1000). node 2는 1500에 타임아웃, 1600 boot-up(0x00)은 '살아는 있지만 operational 아님'이라 계속 보고, node 4는 처음 보이기 전엔 보고하지 않음"></div>

타이머 wrap은 별도 그림으로 봅니다. `uint32_t` 뺄셈은 mod 2^32라서 `now`가 0을 지나 다시 작아져도 `now − last`는 실제 경과 시간이 됩니다.

<div class="chart" data-src="chart3.svg" data-caption="테스트 12: last = 0xFFFFFF00. 뺄셈 비교는 128 / 356 / 768 ms를 정확히 얻지만, 덧셈 deadline(0xF4로 wrap)은 128 ms에서 이미 '타임아웃'이라고 오판"></div>

### 4.3 손으로 따라가기 (trace)

**Part B — `decode_motor_status(0x183, 8, DC 05 83 FF 69 02 E0 51)`**

| 단계 | 계산 | 결과 |
|---|---|---|
| id 검사 | `0x183 ≤ 0x7FF`, `0x183 & 0x780 = 0x180`, node = `0x183 & 0x7F = 3`, dlc 8 | 통과 |
| speed @0 len 16 | bytes 0–1 → `0x05DC`, MSB 0 | `1500` rpm |
| current @16 len 16 | bytes 2–3 → `0xFF83` = 65411, MSB 1 → 65411 − 65536 | `−125` × 100 = `−12500` mA |
| temp @32 len 8 | `0x69` = 105, − 40 | `65` °C |
| status @40 len 8 | `0x02` → bit0 = 0, bit1 = 1 | fault 0, enabled 1 |
| voltage @48 len 12 | `v >> 48` = `0x51E0`, `& 0xFFF` = `0x1E0` = 480, × 100 | `48000` mV |
| counter @60 len 4 | `0x51E0 >> 12` = `0x5` | `5` |

**Part C — 테스트 8–11 (timeout = 1000)**

| now | 이벤트 | node 1 (last, state) | node 2 (last, state) | node 4 | `hb_check` |
|---|---|---|---|---|---|
| 0 | `0x701 [05]`, `0x702 [05]` | 0, 05 | 0, 05 | 미확인 | |
| 900 | check | 900 ≤ 1000 | 900 ≤ 1000 | | `0x00` |
| 1000 | `0x701 [05]` | 1000, 05 | 0, 05 | | |
| 1500 | check | 500 | **1500 > 1000** | | `0x02` |
| 1600 | `0x702 [00]` boot-up | | 1600, **00** | | |
| 1700 | check | 700 | 100, **not op** | | `0x02` |
| 1800 | `0x702 [05]` | | 1800, 05 | | |
| 1900 | check | 900 | 100 | | `0x00` |
| 2000 | check | **1000 = 1000** (strict `>` → alive) | 200 | | `0x00` |
| 2001 | check | **1001 > 1000** | 201 | | `0x01` |
| 2001 | `0x704 [7F]`, 무시: `0x709`, `0x700`, `0x703` dlc 0, `0x183` | | | 2001, **7F** | |
| 2001 | check | timed out | ok | pre-op | `0x09` |

테스트 12(timeout 500, node 5 last = `0xFFFFFF00`): `now = 0xFFFFFF80` → 128 → `0x00`, `now = 0x00000064` → 356 → `0x00`, `now = 0x00000200` → 768 → `0x10`.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `can_get_bits`는 인자 검사 → 64비트 조립 → shift + mask 세 줄입니다. 인자 검사에서 `start_bit + len > 64`는 `int`로 승격된 덧셈이라 `uint8_t` overflow 걱정이 없습니다.
- 부호 확장 `(int64_t)raw - ((int64_t)1 << len)`은 `len == 32`에서도 UB가 없습니다. `raw | ~mask` 후 캐스트하는 방법도 흔하지만, `uint32_t → int32_t` 변환이 범위 밖이면 implementation-defined라는 점을 말할 수 있으면 좋습니다.
- `decode_motor_status`는 지역 변수 `m`에 다 채운 뒤 `*out = m;` 한 번으로 내보냅니다. 실패 경로에서 `*out`이 반쯤 바뀐 상태로 남지 않습니다(테스트 7).
- heartbeat 상태는 `data[0] & 0x7F` — bit 7은 node guarding의 toggle bit라서 heartbeat에서는 0이지만 방어적으로 지웁니다.
- `seen` 비트마스크 덕분에 "아직 부팅 중이라 한 번도 안 보인 노드"를 알람으로 오판하지 않습니다. 필수 노드 목록이 있다면 `expected` 마스크를 따로 받아 `expected & ~seen`도 보고하면 됩니다.

### 4.5 복잡도

- `can_get_bits` / `can_get_signed` / `decode_motor_status`: **O(1)** (8바이트 고정).
- `hb_on_frame` **O(1)**, `hb_check` **O(N)** (N = 8 노드). 메모리는 `HbMon` 하나 = 8 × (4 + 1) + 5 바이트 정도.

---

## 5. 임베디드 관점으로 말할 것

- **왜 이 문제인가**: 면접관 Jason MacDonald는 Charles Machine Works에서 CANopen 모터 컨트롤러 펌웨어와 TM4C129x 공통 라이브러리(CAN/CANopen 포함)를 작성했고, Anduril Altius customer integration을 했습니다. CAN은 거의 확실히 물어볼 주제이고 Don의 약점이므로, "비트 추출 → 부호 확장 → scale/offset → node별 타임아웃"을 30분 안에 막힘없이 쓰는 것이 목표입니다.
- **DBC 사고방식**: 실무에서는 신호 정의를 DBC 파일(`start|len@1+ (scale,offset)`)로 받고 코드 생성기를 씁니다. "Intel이면 `@1`, Motorola면 `@0`, 부호는 `+/-`"라고 DBC 문법으로 말하면 경험자로 들립니다.
- **정수 스케일링**: FPU 없는 MCU나 ISR 경로에서는 0.1 A → mA처럼 정수 단위로 바꿉니다. `int16 × 100`은 최대 ±3,276,800이라 `int32_t`에 안전합니다.
- **RX 경로 분리**: CAN RX ISR(또는 FIFO 콜백)에서는 프레임을 링버퍼에 복사만 하고, 디코딩과 `hb_check`는 태스크에서 합니다. `hb_on_frame`을 ISR에서 부른다면 `last_ms`와 `state` 갱신이 원자적이지 않으므로, check 쪽에서 짧게 인터럽트를 막거나 노드별로 한 번에 읽는 구조를 말합니다 → [E3](../E3_isr_to_task_pipeline/index.html).
- **하드웨어 필터**: bxCAN/FDCAN/TM4C CAN 모두 acceptance filter(id + mask)가 있습니다. `0x180/0x780`과 `0x700/0x780` 두 필터만 열면 CPU는 관심 없는 프레임을 아예 보지 않습니다.
- **SocketCAN (embedded Linux)**: 리눅스에서는 `socket(PF_CAN, SOCK_RAW, CAN_RAW)` → `bind` → `read()`로 `struct can_frame { canid_t can_id; __u8 len /* can_dlc */; __u8 data[8]; }`를 받습니다. `can_id`의 상위 비트가 `CAN_EFF_FLAG`(29비트), `CAN_RTR_FLAG`, `CAN_ERR_FLAG`이므로 `can_id & CAN_EFF_FLAG`를 먼저 보고 `& CAN_SFF_MASK`(0x7FF)로 id를 꺼냅니다. 필터는 `setsockopt(CAN_RAW_FILTER, struct can_filter{can_id, can_mask})`로 커널에 맡깁니다. 우리 함수의 `can_id > 0x7FF` 거절이 바로 이 플래그 처리에 해당합니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How would you support Motorola (big-endian) signals? | Motorola는 `data[0]`이 MSB 쪽입니다. 8바이트를 **반대 순서**(i = 0부터 7까지, `data[0]`을 가장 먼저 shift)로 조립하면 big-endian 64비트 값이 되고, DBC의 start bit(신호의 MSB, sawtooth 번호)를 선형 위치로 바꾼 뒤 같은 shift/mask를 씁니다. 가장 흔한 버그가 sawtooth 번호 변환이라 테스트 벡터로 꼭 확인한다고 말합니다. |
| What are CAN error states? What is bus-off? | 각 노드는 TEC/REC 카운터를 가집니다. 둘 다 < 128이면 **error active**(active error flag 전송), 하나라도 ≥ 128이면 **error passive**(passive flag, 송신 전 대기), TEC > 255면 **bus-off**(송신 중단). bus-off 복구는 128 × 11 recessive bit를 본 뒤 가능하고, 펌웨어는 자동 복구 여부·재시도 간격·로그를 정책으로 정합니다. 모터 드라이브가 bus-off면 heartbeat가 끊기므로 Part C가 잡아냅니다. |
| How does arbitration work? Which id wins? | CSMA/CR, bit-wise arbitration입니다. dominant(0)가 recessive(1)를 이기므로 **id 숫자가 작을수록 우선순위가 높습니다**. 그래서 CANopen은 NMT(0x000) > SYNC(0x080) > EMCY(0x081+) > PDO > SDO > heartbeat(0x700+) 순서로 id를 배치했습니다. 같은 id를 두 노드가 동시에 보내면 안 됩니다. |
| SDO vs PDO? | **PDO**는 확인 응답 없는 실시간 프로세스 데이터(최대 8바이트, 미리 매핑된 레이아웃, 이 문제의 TPDO1). **SDO**는 object dictionary를 index/subindex로 읽고 쓰는 요청-응답 채널(0x600+/0x580+), 설정·파라미터용이고 segmented transfer로 긴 데이터도 됩니다. 주기 제어 루프에는 PDO, 설정에는 SDO. |
| How do you get consistent timestamps / sample alignment across nodes? | CANopen **SYNC**(0x080)를 마스터가 주기적으로 보내고, synchronous TPDO는 SYNC 시점에 샘플해 전송합니다. 수신 쪽은 컨트롤러 하드웨어 타임스탬프(RX 시점 캡처)를 같이 기록하고, 필요하면 TIME 객체(0x100)나 상위 시간 동기로 맞춥니다 → [J4](../J4_time_sync_offset/index.html). rolling counter(bit 60..63)로 빠진 프레임·중복을 감지합니다. |
| How would you test this on a bench? | Linux 노트북 + USB-CAN(SocketCAN)에서 `ip link set can0 type can bitrate 500000 && ip link set can0 up`, `cansend can0 183#DC0583FF6902E051`로 프레임 주입, `candump -td can0`로 관찰, `cangen`으로 부하·랜덤 프레임. 단위 테스트는 이 파일처럼 알려진 벡터 + 경계값, HIL에서는 드라이브 전원을 끊어 heartbeat 타임아웃·boot-up 재연결을 확인합니다. 종단 저항(120 Ω × 2)도 먼저 확인합니다. |

## 7. 자주 하는 실수

- `(1u << len) - 1`을 `len == 32`에서 사용 → UB (UBSan이 잡음, 테스트 3)
- 부호 확장을 빼먹고 `(int16_t)`만 하거나, 12비트 같은 비표준 길이에서 `(int16_t)` 캐스트에 의존 → `len 8`/`len 4` 음수 테스트 실패
- Intel 레이아웃에서 바이트를 `data[0]`부터 왼쪽 shift로 조립(=Motorola 순서) → 테스트 1 전부 실패
- `can_id & 0x180`처럼 function code를 잘못 마스킹 → `0x283`(TPDO2) 같은 id를 통과시킴
- 실패 시에도 `*out`을 부분적으로 씀 → 호출자가 쓰레기 값을 사용
- heartbeat 타임아웃을 `now > last + timeout`으로 비교 → wrap 직전에 오판(테스트 12)
- 한 번도 안 보인 노드를 "타임아웃"으로 보고, 또는 boot-up(0x00)을 "살아 있음"으로 처리 → 테스트 8/9 실패
