---
id: COMM-M08
time: 80분
updated: 2026-11-11
---
# COMM-M08 CAN FD: BRS, DLC→64바이트 매핑, 데이터 위상 비트레이트

## 학습 목표

- FD 의 DLC ↔ 길이 표(0~8, 12, 16, 20, 24, 32, 48, 64)와 Classic 의 "DLC 9~15 = 8바이트" 규칙을 코드로 옮기고, 표에 없는 길이(9, 13, 33 …)를 가장 작은 DLC 로 패딩한다. 길이 초과, 버퍼 용량 부족은 오류 코드로 돌려준다.
- 헤더 비트(SOF..DLC)를 Classic / FD × 표준 / 확장 네 형식으로 인코딩·디코딩하고, **같은 자리의 비트 하나(Classic 의 r0/r1 = FD 의 FDF)** 로 형식이 갈린다는 것, FDF·BRS·ESI·RRS·res 가 각각 무슨 뜻인지 설명한다. 잘못된 플래그 조합(BRS 만 켜진 Classic, remote FD 등)을 거부한다.
- 필드별 비트 수를 nominal(중재) 위상과 data 위상으로 나눠 세고, 두 비트레이트로 프레임 시간의 하한(스터프 없음)과 상한(최악 스터핑)을 계산한다. BRS·CRC delimiter 에서 샘플 포인트 기준으로 속도가 바뀌어도 총 시간은 "BRS = nominal, CRC delim = data"로 센 값과 정확히 같음을 증명한다.
- 실제 온-와이어 비트열 인코더(동적 스터핑, stuff count, 고정 스터프 비트, CRC-17/21)로 정확한 시간을 재서 하한 ≤ 정확 ≤ 상한을 6000개 랜덤 프레임에서 확인하고, DP 로 구한 최악 스터프 수와 상한식의 차이를 실측으로 보여 준다.
- Classic 8 B 와 FD 8/16/64 B 의 시간·처리량·효율 표를 만들고, 1 kHz 로봇 관절 버스(모터당 16 B 명령 + 16 B 상태)에 어떤 설정이 몇 개 모터까지 들어가는지 버스 부하로 판정한다.

## 1. 문제

"CAN FD 로 바꾸면 몇 배 빨라지나요?", "64바이트 프레임 하나 보내는 데 몇 µs 걸리죠?" — 자동차·로봇 펌웨어 면접에서 CAN FD 질문은 결국 **프레임 시간 계산**으로 끝난다. 답이 "8배"나 "2 Mbps 니까 4배"로 나오면 탈락이다: FD 는 프레임의 앞뒤(중재, ACK, EOF)를 여전히 느린 nominal 속도로 보내고, 가운데(데이터 위상)만 빠르게 보내기 때문이다.

CAN FD(Flexible Data-rate, ISO 11898-1:2015)가 Classic CAN 에서 바꾼 것은 세 가지다.

1. **페이로드 최대 64바이트.** DLC 는 여전히 4비트라서 9~15 를 12, 16, 20, 24, 32, 48, 64 바이트로 다시 정의했다. 그래서 13바이트는 보낼 수 없고 16바이트로 패딩한다.
2. **두 비트레이트.** 여러 노드가 동시에 비트를 내는 중재 구간은 Classic 처럼 느리게(nominal, 예 500 kbit/s), 송신자가 하나뿐인 데이터 구간은 빠르게(data, 예 2 Mbit/s) 보낸다. 전환 여부는 프레임마다 **BRS**(Bit Rate Switch) 비트로 정한다.
3. **더 강한 오류 검출.** 데이터 길이에 따라 CRC-17(≤ 16 B) / CRC-21(> 16 B), 그리고 ISO 판에서는 CRC 필드 앞에 **stuff count**(동적 스터프 비트 수 mod 8)와 **고정 스터프 비트**를 넣었다.

헤더에서 Classic 과 FD 를 가르는 것은 비트 하나다. Classic 의 예약 비트 r0(표준) / r1(확장) 자리가 FD 에서는 recessive 인 **FDF** 다. 그 뒤로 res(dominant), BRS, ESI(Error State Indicator)가 붙고, RTR 자리는 항상 dominant 인 RRS 가 된다 — FD 에는 remote frame 이 없다.

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">헤더 비트 (SOF..DLC, 스터핑 전) — 같은 자리의 비트가 Classic 과 FD 를 가른다</text>
  <text x="10" y="62" fill="currentColor">Classic 표준 (19)</text>
  <rect x="120" y="34" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="131.5" y="74" fill="currentColor" transform="rotate(-90 131.5 74)">SOF</text>
  <text x="127.5" y="91" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="135" y="34" width="165" height="44" fill="none" stroke="currentColor"/>
  <text x="217.5" y="61" fill="currentColor" text-anchor="middle">ID[10:0]</text>
  <rect x="300" y="34" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="311.5" y="74" fill="currentColor" transform="rotate(-90 311.5 74)">RTR</text>
  <rect x="315" y="34" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="326.5" y="74" fill="currentColor" transform="rotate(-90 326.5 74)">IDE</text>
  <text x="322.5" y="91" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="330" y="34" width="15" height="44" class="f-mut" stroke="currentColor"/>
  <text x="341.5" y="74" fill="currentColor" transform="rotate(-90 341.5 74)">r0</text>
  <text x="337.5" y="91" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="345" y="34" width="60" height="44" fill="none" stroke="currentColor"/>
  <text x="375.0" y="61" fill="currentColor" text-anchor="middle">DLC</text>
  <text x="10" y="130" fill="currentColor">FD 표준 (22)</text>
  <rect x="120" y="102" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="131.5" y="142" fill="currentColor" transform="rotate(-90 131.5 142)">SOF</text>
  <text x="127.5" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="135" y="102" width="165" height="44" fill="none" stroke="currentColor"/>
  <text x="217.5" y="129" fill="currentColor" text-anchor="middle">ID[10:0]</text>
  <rect x="300" y="102" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="311.5" y="142" fill="currentColor" transform="rotate(-90 311.5 142)">RRS</text>
  <text x="307.5" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="315" y="102" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="326.5" y="142" fill="currentColor" transform="rotate(-90 326.5 142)">IDE</text>
  <text x="322.5" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="330" y="102" width="15" height="44" class="f-acc" stroke="currentColor"/>
  <text x="341.5" y="142" fill="currentColor" transform="rotate(-90 341.5 142)">FDF</text>
  <text x="337.5" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="345" y="102" width="15" height="44" class="f-warn" stroke="currentColor"/>
  <text x="356.5" y="142" fill="currentColor" transform="rotate(-90 356.5 142)">res</text>
  <text x="352.5" y="159" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="360" y="102" width="15" height="44" class="f-ok" stroke="currentColor"/>
  <text x="371.5" y="142" fill="currentColor" transform="rotate(-90 371.5 142)">BRS</text>
  <rect x="375" y="102" width="15" height="44" class="f-ok" stroke="currentColor"/>
  <text x="386.5" y="142" fill="currentColor" transform="rotate(-90 386.5 142)">ESI</text>
  <rect x="390" y="102" width="60" height="44" fill="none" stroke="currentColor"/>
  <text x="420.0" y="129" fill="currentColor" text-anchor="middle">DLC</text>
  <text x="10" y="198" fill="currentColor">Classic 확장 (39)</text>
  <rect x="120" y="170" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="131.5" y="210" fill="currentColor" transform="rotate(-90 131.5 210)">SOF</text>
  <text x="127.5" y="227" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="135" y="170" width="165" height="44" fill="none" stroke="currentColor"/>
  <text x="217.5" y="197" fill="currentColor" text-anchor="middle">ID-A</text>
  <rect x="300" y="170" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="311.5" y="210" fill="currentColor" transform="rotate(-90 311.5 210)">SRR</text>
  <text x="307.5" y="227" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="315" y="170" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="326.5" y="210" fill="currentColor" transform="rotate(-90 326.5 210)">IDE</text>
  <text x="322.5" y="227" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="330" y="170" width="270" height="44" fill="none" stroke="currentColor"/>
  <text x="465.0" y="197" fill="currentColor" text-anchor="middle">ID-B[17:0]</text>
  <rect x="600" y="170" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="611.5" y="210" fill="currentColor" transform="rotate(-90 611.5 210)">RTR</text>
  <rect x="615" y="170" width="15" height="44" class="f-mut" stroke="currentColor"/>
  <text x="626.5" y="210" fill="currentColor" transform="rotate(-90 626.5 210)">r1</text>
  <text x="622.5" y="227" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="630" y="170" width="15" height="44" class="f-mut" stroke="currentColor"/>
  <text x="641.5" y="210" fill="currentColor" transform="rotate(-90 641.5 210)">r0</text>
  <text x="637.5" y="227" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="645" y="170" width="60" height="44" fill="none" stroke="currentColor"/>
  <text x="675.0" y="197" fill="currentColor" text-anchor="middle">DLC</text>
  <text x="10" y="266" fill="currentColor">FD 확장 (41)</text>
  <rect x="120" y="238" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="131.5" y="278" fill="currentColor" transform="rotate(-90 131.5 278)">SOF</text>
  <text x="127.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="135" y="238" width="165" height="44" fill="none" stroke="currentColor"/>
  <text x="217.5" y="265" fill="currentColor" text-anchor="middle">ID-A</text>
  <rect x="300" y="238" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="311.5" y="278" fill="currentColor" transform="rotate(-90 311.5 278)">SRR</text>
  <text x="307.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="315" y="238" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="326.5" y="278" fill="currentColor" transform="rotate(-90 326.5 278)">IDE</text>
  <text x="322.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="330" y="238" width="270" height="44" fill="none" stroke="currentColor"/>
  <text x="465.0" y="265" fill="currentColor" text-anchor="middle">ID-B[17:0]</text>
  <rect x="600" y="238" width="15" height="44" fill="none" stroke="currentColor"/>
  <text x="611.5" y="278" fill="currentColor" transform="rotate(-90 611.5 278)">RRS</text>
  <text x="607.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="615" y="238" width="15" height="44" class="f-acc" stroke="currentColor"/>
  <text x="626.5" y="278" fill="currentColor" transform="rotate(-90 626.5 278)">FDF</text>
  <text x="622.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <rect x="630" y="238" width="15" height="44" class="f-warn" stroke="currentColor"/>
  <text x="641.5" y="278" fill="currentColor" transform="rotate(-90 641.5 278)">res</text>
  <text x="637.5" y="295" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <rect x="645" y="238" width="15" height="44" class="f-ok" stroke="currentColor"/>
  <text x="656.5" y="278" fill="currentColor" transform="rotate(-90 656.5 278)">BRS</text>
  <rect x="660" y="238" width="15" height="44" class="f-ok" stroke="currentColor"/>
  <text x="671.5" y="278" fill="currentColor" transform="rotate(-90 671.5 278)">ESI</text>
  <rect x="675" y="238" width="60" height="44" fill="none" stroke="currentColor"/>
  <text x="705.0" y="265" fill="currentColor" text-anchor="middle">DLC</text>
  <text x="10" y="312" fill="currentColor">아래 숫자 = 고정값 (0 dominant, 1 recessive). Classic 의 예약 비트 자리가 FD 에선 FDF=1:</text>
  <text x="10" y="328" fill="currentColor">표준은 IDE 다음 r0 자리, 확장은 RTR 다음 r1 자리. FD 에는 RTR 이 없다 (RRS=0, remote frame 없음).</text>
  <text x="10" y="344" fill="currentColor">BRS=1 → 데이터 위상을 빠른 비트레이트로, ESI=1 → 송신 노드가 error-passive.</text>
</svg>
```

이 레슨은 Classic 프레임 인코더(COMM-M04)와 중재(COMM-M05)를 다시 만들지 않는다. **길이, 플래그, 시간**에 집중한다. 비트 타이밍 레지스터(BRP/TSEG) 계산은 COMM-M06, ESI 를 결정하는 TEC/REC 는 COMM-M07, Linux SocketCAN 의 `struct canfd_frame` 은 COMM-M09 에서 다룬다.

### Part A — DLC ↔ 길이, 패딩, 헤더 비트

- `canfd_dlc2len(dlc)` (하위 4비트, 16칸 표), `can_cc_dlc2len(dlc)` (Classic: 9~15 → 8), `canfd_len2dlc(len)` (길이를 담는 가장 작은 DLC, len > 64 → −1).
- `canfd_pad(data, len, pad, out, out_cap, &dlc)` — 패딩된 길이를 돌려준다. len > 64 → `CAN_ELEN`, 용량 부족 → `CAN_ECAP`(out 은 건드리지 않음).
- `can_hdr_encode(m, bits, cap)` — SOF..DLC 비트(19 / 39 / 22 / 41)를 만든다. `can_hdr_decode(bits, n, m)` — 거꾸로. Classic DLC 12 를 받으면 len = 8 이고 원래 DLC 는 `len8_dlc` 에 보존한다(Linux `struct can_frame` 의 `len8_dlc` 와 같은 뜻).
- 테스트: DLC 표 전수, len 0~64 전수(가장 작은 DLC 인지, 단조인지), 왕복, 손으로 쓴 헤더 비트열 3개, 랜덤 20000개 인코드→디코드 왕복.

### Part B — 잘못된 입력과 프레임 시간 계산기

- 플래그 규칙(`can_msg_check`): Classic 에 BRS/ESI → `CAN_EFLAGS`, FD + RTR → `CAN_ERTR_FD`, FD 길이가 표에 없음(13) 또는 Classic 길이 > 8 → `CAN_ELEN`, ID 범위 초과 → `CAN_EID`. 디코더: SOF ≠ 0 → `CAN_EFORM`, 비트 부족 → `CAN_ESHORT`, FD 의 res = 1 → `CAN_ERES`.
- `can_layout_of(m, &L)` — 필드별 비트 수와 nominal / data 비트 수, 동적 스터핑 구간 길이. 값은 Linux `include/linux/can/length.h` 의 매크로와 같아야 한다(독립 출처 대조).
- `can_frame_time(m, rates, T_NOSTUFF | T_WORST, &t)` — 두 비트레이트로 시간. data 속도 < nominal 이거나 0 → `CAN_ERATE`.
- `can_wire_encode(m, bits, ph, cap, &n_stuff)` — 실제 선 위 비트열과 비트마다 위상(0 = nominal, 1 = data). 동적 스터핑, stuff count(Gray + 패리티), 고정 스터프 비트, CRC-15/17/21.
- 테스트: 손 계산 시간(Classic 8 B @500k = 222 / 270 µs, FD 64 B @500k/2M = 333 / 404 µs …), 비트레이트 오류, 단조성(길이↑ → 시간↑, data 속도↑ → 시간↓), CRC 확인값 3개, 6000개 랜덤 프레임에서 "비트 수 = 고정 + 스터프", 스터핑 제거 → 원래 비트, 6연속 없음, CRC 나머지 0, stuff count 일치, 하한 ≤ 정확 ≤ 상한, 용량 부족.

### Part C — 버스 부하와 로봇 관절 버스

- `can_bus_load(set, n, rates, mode, &pct)` — 주기 메시지 집합의 부하 = Σ 프레임 시간 / 주기.
- `robot_max_motors(rates, fd, brs, payload, rate_hz, limit_pct, max_n, &load)` — 1 kHz 마다 모터 하나당 명령 1개 + 상태 1개(각 16 B). FD 는 한 프레임, Classic 은 8 B 씩 쪼갠다. 최악 스터핑으로 부하 ≤ limit 인 최대 모터 수.
- demo 로 Classic 500k/1M 과 FD 500k/2M, 1M/2M, 1M/4M, 1M/5M, 1M/8M 를 비교하고, "6관절 팔을 버스 하나에" 가 어디서 가능한지 본다.

## 2. Problem (English)

> **Part A.** A CAN FD DLC is 4 bits: 0–8 mean 0–8 bytes, 9–15 mean 12, 16, 20, 24, 32, 48, 64 bytes. Implement `unsigned canfd_dlc2len(uint8_t dlc)`, `unsigned can_cc_dlc2len(uint8_t dlc)` (Classical CAN: 9–15 still mean 8 bytes) and `int canfd_len2dlc(unsigned len)` (smallest DLC that holds `len`, −1 if `len > 64`). Implement `int canfd_pad(const uint8_t *data, unsigned len, uint8_t pad, uint8_t *out, size_t out_cap, uint8_t *dlc)` returning the padded length. Then encode/decode the header bits from SOF to the end of DLC for Classical and FD frames, base (11-bit) and extended (29-bit) IDs:
> `int can_hdr_encode(const can_msg *m, uint8_t *bits, size_t cap)` / `int can_hdr_decode(const uint8_t *bits, size_t n, can_msg *m)`.
> Example: FD, ID 0x123, BRS = 1, ESI = 0, 64 bytes → `0 00100100011 0 0 1 0 1 0 1111` (22 bits). Classical, ID 0x123, 2 bytes → `0 00100100011 0 0 0 0010` (19 bits).
>
> **Part B.** Reject invalid messages with distinct codes (BRS/ESI without FDF, remote FD frame, FD length not in the DLC table, ID out of range, buffer too small, res bit recessive on decode). Compute, field by field, how many bits are sent at the nominal rate and how many at the data rate, and the frame time for given `(nom_bps, data_bps)`, both without stuff bits (lower bound) and with the worst-case number of dynamic stuff bits (upper bound). Also build the exact on-wire bit sequence (dynamic stuffing SOF..data, stuff count, fixed stuff bits, CRC) and show lower ≤ exact ≤ upper.
> Example: Classical 8 bytes @ 500 kbit/s → 111 bits = 222 µs (no stuffing), ≤ 135 bits = 270 µs. FD 64 bytes @ 500k/2M with BRS → 29 nominal + 550 data bits = 333 µs, ≤ 404 µs.
>
> **Part C.** A robot runs a 1 kHz control loop; each motor gets one 16-byte command and returns one 16-byte status per cycle. For each bus configuration, how many motors fit under 70 % worst-case bus load?
>
> Constraints: no malloc, every writer takes a capacity, deterministic tests (xorshift32), dominant = 0.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열두 군데: Part A `canfd_dlc2len`, `can_cc_dlc2len`, `canfd_len2dlc`, `canfd_pad`, `can_hdr_encode`, `can_hdr_decode` / Part B `can_msg_check`, `can_layout_of`, `can_frame_time`, `can_wire_encode` / Part C `can_bus_load`, `robot_max_motors`. 비트 쓰기·읽기 헬퍼(`put_bits`, `get_bits`), CRC(`crc_bit`, `crc_bytes`), Gray 코드, 스터프 상한 `stuff_max`, `wire_time_us`, 하네스(스터핑 제거기 `destuff` 포함)는 주어진다. 스텁은 0 / −1 / `CAN_EARG` 를 돌려주므로 루프가 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:255  ok
FAIL starter.c:258  ok
FAIL starter.c:259  canfd_dlc2len(0x1F) == 64
…
FAIL starter.c:525  robot_max_motors(&bad2, 1, 1, 16, 1000, 70.0, 64, &la) == CAN_ERATE
FAIL starter.c:535  ok
12/73 passed
```

(exit 1. 통과한 12개는 주어진 CRC 확인값 3개, 스텁 값이 우연히 맞는 4개(`canfd_len2dlc(65) == -1`, NULL 인자 두 개와 주기 0 → `CAN_EARG`), 그리고 인코더가 실패해 검사할 프레임이 하나도 없어 참으로 남은 "성질 검사" 5개다 — 그 경우는 `enc_ok` 가 FAIL 로 잡는다.)

## 4. 힌트

<details><summary>힌트 1 — DLC 표는 상수 배열, 역방향은 "처음으로 ≥ len"</summary>

`static const uint8_t t[16] = {0,…,8,12,16,20,24,32,48,64}`. 길이 → DLC 는 len ≤ 8 이면 그대로, 아니면 9 부터 올라가며 `t[d] >= len` 인 첫 d. 16칸뿐이라 선형 탐색이면 충분하고, ISR 에서 쓰려면 65칸 LUT(추가 연습 1).
</details>

<details><summary>힌트 2 — 헤더 인코더: 다른 곳은 IDE 뒤(표준) / ID-B 뒤(확장) 세 비트뿐</summary>

표준: Classic `RTR IDE r0` ↔ FD `RRS IDE FDF res`. 확장: Classic `RTR r1 r0` ↔ FD `RRS FDF res`. 그 뒤 FD 만 `BRS ESI`, 마지막은 공통 DLC 4비트. 확장 ID 29비트는 위 11비트(ID-A = id >> 18)와 아래 18비트(ID-B)로 나뉘고 사이에 SRR(1), IDE(1).
</details>

<details><summary>힌트 3 — 디코더는 "FDF 자리" 비트로 갈린다</summary>

13번째 비트(0-based 13)가 IDE. IDE = 0 이면 다음 비트가 Classic r0 / FD FDF, IDE = 1 이면 ID-B 18비트와 RTR/RRS 를 읽은 다음 비트가 Classic r1 / FD FDF. FDF = 1 이면 res 를 검사하고 BRS, ESI, DLC.
</details>

<details><summary>힌트 4 — 두 비트레이트의 경계는 BRS 와 CRC delimiter</summary>

nominal = SOF..BRS + ACK slot..IFS, data = ESI..CRC delimiter 로 세면 된다. 실제로는 BRS 의 샘플 포인트에서 빨라지고 CRC delimiter 의 샘플 포인트에서 느려지지만, 두 반쪽 비트를 더하면 정확히 T_nom + T_data 다(풀이 참고).
</details>

<details><summary>힌트 5 — 최악 스터프 수를 두 위상에 나누기</summary>

길이 k 구간의 동적 스터프는 최대 ⌊(k−1)/4⌋. 시간은 T_nom ≥ T_data 이므로 스터프 비트를 가능한 많이 nominal 쪽에 놓는 것이 최악이다: s_nom = ⌊(SOF..BRS 앞 비트 수 − 1)/4⌋, s_data = ⌊(전체 − 1)/4⌋ − s_nom.
</details>

<details><summary>힌트 6 — 인코더는 "다음 비트를 내기 전에 run == 5 면 보수"</summary>

이렇게 쓰면 마지막 데이터 비트 뒤의 스터프를 자연히 빼먹는다(FD 에서는 그 자리에 고정 스터프 비트가 온다). Classic 은 CRC 마지막 비트 뒤에도 넣어야 하므로 루프 끝에서 한 번 더 확인한다. 스터프 비트의 위상은 "뒤따르는 원래 비트"의 위상.
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "FD 는 중재와 ACK 를 nominal 로, 그 사이(ESI..CRC delimiter)를 data 속도로 보낸다. 그래서 프레임 시간 = nominal 비트 × T_nom + data 비트 × T_data."
2. "필드를 세면 표준 ID FD 프레임은 nominal 29비트(IFS 포함), data 는 33 + 8n(≤ 16 B) 또는 38 + 8n 비트."
3. "500k/2M, 64 B: 29 × 2 µs + 550 × 0.5 µs = 333 µs. 최악 스터핑을 더하면 404 µs. Classic 8 B @500k 는 222~270 µs 에 8바이트니까 바이트당 5배 이상 빠르다."
4. "단, 8 B 만 보내면 FD 500k/2M 은 106.5 µs 로 2배 정도이고, BRS 를 끄면 오히려 Classic 보다 느리다(252 µs)."

### Part A — DLC 표와 헤더

**왜 12, 16, 20, 24, 32, 48, 64 인가.** DLC 는 Classic 과 호환되게 4비트로 남았고, 9~15 일곱 칸에 64까지를 나눠 담았다. 작은 길이는 촘촘하게(4바이트 간격), 큰 길이는 듬성듬성(16바이트 간격). 대가는 패딩이다 — 33바이트를 보내려면 48바이트 프레임에 15바이트를 채운다(추가 연습 2: 최대 낭비 15 B, 0~64 평균 4.49 B). 패딩 값은 규격이 정하지 않으므로 프로젝트가 정한다(이 레슨은 인자로 받는다. AUTOSAR 계열은 설정값, Linux 는 보통 0 — 확인 권장).

**Classic 의 DLC 9~15.** Classic 에서도 DLC 는 0~15 를 선에 실을 수 있고, 9~15 는 "8바이트"로 해석된다(ISO 11898-1). 그래서 디코더는 len = min(dlc, 8) 로 두되 원래 DLC 를 버리지 않는다. 진단 도구나 게이트웨이는 "DLC 12 인 Classic 프레임"을 그대로 재전송해야 하기 때문이다 — Linux 가 `struct can_frame` 에 `len8_dlc` 를 둔 이유(COMM-M09).

**헤더 비트 추적표 — FD 표준, ID 0x123, BRS = 1, ESI = 0, 64 B:**

| 비트 # | 0 | 1~11 | 12 | 13 | 14 | 15 | 16 | 17 | 18~21 |
|---|---|---|---|---|---|---|---|---|---|
| 필드 | SOF | ID[10:0] | RRS | IDE | FDF | res | BRS | ESI | DLC |
| 값 | 0 | 001 0010 0011 | 0 | 0 | 1 | 0 | 1 | 0 | 1111 |

같은 ID 의 Classic 2바이트 프레임은 비트 12~18 이 `RTR=0 IDE=0 r0=0 DLC=0010` 이다. 비트 14 가 0 이냐 1 이냐로 FD 여부가 갈린다(테스트 `cc_bit14 == 0 && hb[14] == 1`).

**플래그의 뜻과 규칙:**

| 비트 | 값 | 뜻 | 누가 정하나 |
|---|---|---|---|
| FDF | 1 (rec) | FD 형식 (옛 이름 EDL) | 송신 메시지 설정 |
| res | 0 (dom) | 예약. FD 노드가 1 을 받으면 protocol exception 대상(CAN XL 이 이 자리를 XLF 로 씀 — ISO 확인 권장) | 고정 |
| BRS | 1 = 전환 | data 위상을 data 비트레이트로 | 메시지마다 (드라이버 플래그) |
| ESI | 0 = active, 1 = passive | 송신 노드의 오류 상태 (COMM-M07 의 TEC/REC) | 컨트롤러가 자동으로 |
| RRS | 0 (dom) | RTR 자리. FD 에는 remote frame 이 없다 | 고정 |

그래서 `can_msg_check` 는 Classic 에 BRS/ESI 가 켜지면 거부하고(`CAN_EFLAGS`), FD 에 RTR 이 켜져도 거부한다(`CAN_ERTR_FD`). 길이는 Classic 이면 0~8, FD 면 DLC 표에 있는 값만 받는다 — 13 을 받으면 조용히 16으로 올리지 않고 `CAN_ELEN` 을 돌려준다. 패딩은 호출자가 `canfd_pad` 로 명시적으로 한다(어떤 바이트를 채울지 결정할 권한이 앱에 있다).

### Part B — 비트 수와 시간

**필드별 비트 수.** demo_fd_table.c 표 1 (실제 출력):
```text
  프레임           | hdr | data | SBC | FSB | CRC | tail | IFS | 동적구간 | nominal | data
  Classic 8 B      |  19 |   64 |   0 |   0 |  15 |   10 |   3 |     98   |   111   |    0
  FD 8 B  BRS=1    |  22 |   64 |   4 |   6 |  17 |   10 |   3 |     86   |    29   |   97
  FD 16 B BRS=1    |  22 |  128 |   4 |   6 |  17 |   10 |   3 |    150   |    29   |  161
  FD 20 B BRS=1    |  22 |  160 |   4 |   7 |  21 |   10 |   3 |    182   |    29   |  198
  FD 64 B BRS=1    |  22 |  512 |   4 |   7 |  21 |   10 |   3 |    534   |    29   |  550
  FD 64 B BRS=0    |  22 |  512 |   4 |   7 |  21 |   10 |   3 |    534   |   579   |    0
```

tail = CRC delimiter 1 + ACK slot 1 + ACK delimiter 1 + EOF 7. 정리하면 (n = 데이터 바이트, IFS 3 포함):

| 형식 | 스터프 없는 비트 수 | nominal (BRS=1) | data (BRS=1) | 동적 스터핑 구간 |
|---|---|---|---|---|
| Classic 표준 | 47 + 8n | 전부 | — | SOF..CRC = 34 + 8n |
| Classic 확장 | 67 + 8n | 전부 | — | 54 + 8n |
| FD 표준, n ≤ 16 | 62 + 8n | 17 + 12 = 29 | 33 + 8n | SOF..data = 22 + 8n |
| FD 표준, n > 16 | 67 + 8n | 29 | 38 + 8n | 22 + 8n |
| FD 확장 | 81 / 86 + 8n | 36 + 12 = 48 | 33 / 38 + 8n | 41 + 8n |

FD data 위상 = ESI 1 + DLC 4 + 데이터 8n + SBC 4 + FSB 6(7) + CRC 17(21) + CRC delim 1. 이 값들은 Linux `include/linux/can/length.h` 의 `CANFD_FRAME_HEADER_SFF_BITS 22`, `CANFD_FRAME_CRC17_FIELD_BITS 28`(= SBC 4 + FSB 6 + CRC 17 + delim 1), `CANFD_FRAME_CRC21_FIELD_BITS 33`, `CAN_FRAME_FOOTER_BITS 9`, `CAN_INTERMISSION_BITS 3` 과 같다 — solution.c 가 이 합을 검사한다. 고정 스터프 비트 수는 Linux 패치 설명대로 1 + ⌊(4 + CRC 길이)/4⌋ = 6 / 7 이다(ISO 원문은 확인 권장).

**BRS 와 CRC delimiter 의 반쪽 비트.** 속도 전환은 비트 경계가 아니라 **샘플 포인트**에서 일어난다. nominal 샘플 포인트를 SP_n(예 80%), data 샘플 포인트를 SP_d 라 하면

- BRS 비트 = SP_n·T_n (샘플까지 nominal) + (1 − SP_d)·T_d (나머지는 data 의 phase segment 2)
- CRC delimiter = SP_d·T_d + (1 − SP_n)·T_n

둘을 더하면 SP 가 소거되어 **T_n + T_d**. 그래서 "BRS 는 nominal 1비트, CRC delim 은 data 1비트"로 세면 총 시간이 샘플 포인트와 무관하게 정확하다. 개별 비트의 시작 시각이 궁금할 때만 SP 가 필요하다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">FD 표준 ID 0x123, BRS=1, ESI=0, DLC=1, data 0x00 — 500k / 2M (solution.c 인코더 출력)</text>
  <rect x="60" y="46" width="510" height="60" class="f-mut" stroke="none" opacity="0.35"/>
  <rect x="570" y="46" width="142.5" height="60" class="f-acc" stroke="none" opacity="0.35"/>
  <path d="M52,60 H60 V92 H90.0 V92 H120.0 V92 H150.0 V60 H180.0 V92 H210.0 V92 H240.0 V60 H270.0 V92 H300.0 V92 H330.0 V92 H360.0 V60 H390.0 V60 H420.0 V92 H450.0 V92 H480.0 V60 H510.0 V92 H540.0 V60 H570.0 V92 H577.5 V92 H585.0 V92 H592.5 V92 H600.0 V60 H607.5 V92 H615.0 V92 H622.5 V92 H630.0 V92 H637.5 V92 H645.0 V60 H652.5 V92 H660.0 V92 H667.5 V92 H675.0 V60 H682.5 V92 H690.0 V92 H697.5 V60 H705.0 V60 H712.5 V60 H742.5" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="718.5" y="56" fill="currentColor">…CRC</text>
  <text x="75.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="75.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">SOF</text>
  <text x="105.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="105.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">10</text>
  <text x="135.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="135.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">9</text>
  <text x="165.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="165.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">8</text>
  <text x="195.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="195.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">7</text>
  <text x="225.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="225.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">6</text>
  <text x="255.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="255.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">5</text>
  <text x="285.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="285.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">4</text>
  <text x="315.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="315.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">3</text>
  <text x="345.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="345.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">2</text>
  <text x="375.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="375.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">1</text>
  <text x="405.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="405.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">0</text>
  <text x="435.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="435.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">RRS</text>
  <text x="465.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="465.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">IDE</text>
  <text x="495.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="495.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">FDF</text>
  <text x="525.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">0</text>
  <text x="525.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">res</text>
  <text x="555.0" y="122" fill="currentColor" font-family="monospace" text-anchor="middle">1</text>
  <text x="555.0" y="138" fill="currentColor" font-size="12" text-anchor="middle">BRS</text>
  <text x="90" y="156" fill="currentColor">ID[10:0] = 001 0010 0011</text>
  <text x="573.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">E</text>
  <text x="581.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">D</text>
  <text x="588.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">D</text>
  <text x="596.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">D</text>
  <text x="603.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">D</text>
  <text x="611.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="618.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="626.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="633.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="641.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="648.8" y="122" font-family="monospace" font-size="12" text-anchor="middle" class="f-warn">S</text>
  <text x="656.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="663.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="671.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">d</text>
  <text x="678.8" y="122" font-family="monospace" font-size="12" text-anchor="middle" class="f-warn">F</text>
  <text x="686.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">C</text>
  <text x="693.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">C</text>
  <text x="701.2" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">C</text>
  <text x="708.8" y="122" fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">C</text>
  <line x1="564.0" y1="36" x2="564.0" y2="108" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="310" y="32" fill="currentColor">BRS 샘플 포인트에서 비트레이트 전환 ▶</text>
  <text x="570" y="156" fill="currentColor">E=ESI D=DLC d=data</text>
  <text x="570" y="172" fill="currentColor">S=동적 스터프 F=고정 스터프</text>
  <text x="570" y="188" fill="currentColor">C=stuff count (0011)</text>
  <text x="10" y="64" fill="currentColor">rec 1</text><text x="10" y="96" fill="currentColor">dom 0</text>
  <text x="10" y="226" fill="currentColor">가로 길이 = 시간. 중재 위상 SOF..BRS 17비트 × 2 us = 34 us (회색), 데이터 위상 비트 0.5 us (진한 칸, 폭 1/4).</text>
  <text x="10" y="242" fill="currentColor">data 0x00 의 0 다섯 개 뒤 S(1). 프레임 71비트, 정확한 시간 79.0 us = 하한 78.5 + 데이터 위상 스터프 1개 0.5 us.</text>
</svg>
```

**스터핑과 위상.** 동적 스터핑은 SOF 부터 마지막 데이터 비트까지(FD) 또는 CRC 끝까지(Classic). 같은 비트 5개 뒤에 보수 1개, 스터프 비트도 다음 런의 첫 비트로 센다. 그래서 첫 스터프는 원래 비트 5개 뒤, 그다음부터는 4개마다 → 길이 k 구간의 최대 스터프 = **⌊(k − 1)/4⌋** (Linux `can_bitstuffing_len` 과 같은 식, COMM-M04 의 Davis 식과 같은 꼴).

스터프 비트가 어느 위상에 속하는지는 "뒤따르는 원래 비트"의 위상으로 정했다. BRS 뒤에 붙는 스터프 비트는 이미 빨라진 뒤라 data 위상이다. 그러므로 nominal 위상 스터프는 원래 비트 5, 9, 13 번 뒤(표준, BRS 앞 16비트 안)뿐이라 **최대 3개**, 확장은 35비트 안에서 **최대 8개**(상한식 — 실제로는 SRR/IDE 고정 비트 때문에 7, 아래 DP).

최악 시간 상한은 T_n ≥ T_d 이므로 스터프를 nominal 쪽에 최대한 몰아 준다:

```c
s_nom = stuff_max(L.dyn_nom);           /* dyn_nom = SOF..BRS 앞 = 16 (표준) / 35 (확장) */
s_dat = stuff_max(L.dyn) - s_nom;       /* dyn = SOF..마지막 데이터 비트 */
t->us = (L.nom + s_nom) * T_n + (L.dat + s_dat) * T_d;
```

FD 64 B @500k/2M: s_nom = 3, s_dat = ⌊533/4⌋ − 3 = 130 → (29 + 3) × 2 + (550 + 130) × 0.5 = 64 + 340 = **404 µs**.

**CRC 필드 — FD 가 프레임 길이를 CRC 값과 무관하게 만든 방법.** Classic 은 CRC 15비트도 동적 스터핑 대상이라 프레임 길이가 CRC 값에도 달려 있고, 최악 길이를 데이터만 보고 정할 수 없다(COMM-M04). FD 의 CRC 필드는 **고정 스터프 비트**를 쓴다: 필드 맨 앞과 매 4비트 뒤에 직전 비트의 보수를 무조건 넣는다. 그래서 CRC 필드 길이는 항상 SBC 4 + CRC 17/21 + FSB 6/7 이다. 이 모델의 CRC 필드 인코딩:

1. SBC = Gray(동적 스터프 수 mod 8) 3비트 + 짝수 패리티 1비트 (Gray 라서 수가 1 틀리면 1비트만 다르고, 패리티로 1비트 오류를 잡는다 — 추가 연습 3).
2. CRC = 스터핑된 SOF..데이터 비트 + SBC 위에서, 초기값 1 << (w − 1).
3. 마지막 데이터 비트 뒤에 와야 할 동적 스터프 비트는 넣지 않는다 — 바로 뒤의 첫 고정 스터프 비트가 보수라서 같은 역할을 한다.

2·3 의 정확한 규칙(초기값, 범위, 경계 처리)은 ISO 11898-1:2015 확인 권장이다. CRC 다항식은 RevEng 카탈로그 값으로 확인값을 검증했다: CRC-15/CAN `0x059E`, CRC-17/CAN-FD `0x04F03`, CRC-21/CAN-FD `0x0ED841` (init 0, "123456789"). 단, RevEng 항목은 Bosch CAN FD 1.0(비 ISO) 기준이라 init 0 이고, ISO 판은 초기값이 다르다고 알려져 있다 — 이 모델은 1 << (w − 1) 을 쓰고 "모델의 정의"로 둔다. **프레임 길이와 시간은 CRC 값과 무관하므로 이 레슨의 숫자는 이 불확실성에 영향받지 않는다.** 실제 컨트롤러 캡처와 대조한 비트열이 아니므로 CRC 값 자체를 "정답 프레임"으로 쓰지 말 것.

**정확한 최악값 — DP 로 확인.** FD 동적 구간에는 CRC 가 없어서 비트가 "고정"(RRS, IDE, FDF, res, DLC) 아니면 "자유"(ID, BRS, ESI, 데이터)다. 상태 (마지막 비트, 런 길이 1..5) 로 DP 를 돌리면 모든 프레임 중 최대 스터프 수가 정확히 나온다. demo_worst_stuff.c 는 DLC 0, 1 에서 실제 인코더로 2^13, 2^21 개 프레임을 전수 조사해 DP 와 일치함을 먼저 확인한다(실제 출력, 일부):

```text
검증: DP vs 전수 조사 (FD 표준 ID)
  DLC 0: 동적 구간 22비트, DP 최대 4, 전수 최대 4 → 일치
  DLC 1: 동적 구간 30비트, DP 최대 5, 전수 최대 5 → 일치

FD 표준 ID: 동적 스터프 비트 최대값 (DP, 정확) vs 상한식
  len | 구간 L | DP 최대 | ⌊(L-1)/4⌋ | ⌊(L-2)/4⌋ | nominal 위상 최대 | 시간가중 최악(500k/2M) s_nom+s_dat
    0 |   22   |     4   |      5    |      5    |        3          | 3 + 1
    8 |   86   |    19   |     21    |     21    |        3          | 3 + 16
   16 |  150   |    34   |     37    |     37    |        3          | 3 + 31
   64 |  534   |   131   |    133    |    133    |        3          | 3 + 128
FD 확장 ID:
    0 |   41   |     8   |     10    |      9    |        7          | 7 + 1
   64 |  553   |   135   |    138    |    137    |        7          | 7 + 128
```

상한식은 1~3비트 보수적이다. 고정 비트(FDF=1 다음 res=0, DLC)가 이상적인 "0000 1111" 교대 패턴을 끊기 때문이다. FD 64 B @500k/2M 의 진짜 최악은 (29 + 3) × 2 + (550 + 128) × 0.5 = **403.0 µs** 로, 계산기 상한 404.0 µs 보다 0.5 µs 짧다. 부하 계산에 상한식을 쓰는 것은 안전 쪽이다. Classic 은 CRC 가 데이터에 종속돼 이런 DP 가 상한만 준다 — Classic 최악 탐색은 COMM-M04 의 몫.

**하한 · 실측 · 상한 표.** demo_fd_table.c 표 2 (실제 출력, 표준 ID, 랜덤 ID·데이터 20000개 실측):

```text
  설정 (nom/data)        | len | 하한 us | 평균 us | 최대 us | 상한 us | 처리량 Mbit/s (하한/상한) | 효율
  Classic   500k         |   8 |   222.0 |   228.6 |   246.0 |   270.0 |  0.288 /  0.237          | 57.7%
  Classic   1M           |   8 |   111.0 |   114.3 |   121.0 |   135.0 |  0.577 /  0.474          | 57.7%
  FD 500k/500k (BRS=0)   |   8 |   252.0 |   257.2 |   274.0 |   294.0 |  0.254 /  0.218          | 50.8%
  FD 500k/500k (BRS=0)   |  64 |  1158.0 |  1193.6 |  1226.0 |  1424.0 |  0.442 /  0.360          | 88.4%
  FD 500k/2M             |   8 |   106.5 |   108.4 |   115.0 |   121.5 |  0.601 /  0.527          | 30.0%
  FD 500k/2M             |  16 |   138.5 |   141.4 |   148.0 |   161.5 |  0.924 /  0.793          | 46.2%
  FD 500k/2M             |  64 |   333.0 |   342.5 |   351.0 |   404.0 |  1.538 /  1.267          | 76.9%
  FD 1M/5M               |   8 |    48.4 |    49.2 |    52.6 |    55.0 |  1.322 /  1.164          | 26.4%
  FD 1M/5M               |  16 |    61.2 |    62.4 |    65.4 |    71.0 |  2.092 /  1.803          | 41.8%
  FD 1M/5M               |  64 |   139.0 |   142.9 |   147.2 |   168.0 |  3.683 /  3.048          | 73.7%
  FD 1M/8M               |  64 |    97.8 |   100.3 |   103.8 |   117.0 |  5.238 /  4.376          | 65.5%
```

처리량 = 페이로드 비트 / 프레임 시간 (100% 부하에서 back-to-back). 효율 = 페이로드 비트가 선을 차지하는 시간 / 프레임 시간. 읽는 법:

- **64 B @500k/2M 은 Classic 8 B @500k 의 5.3배 처리량**(1.538 vs 0.288 Mbit/s). "4배 비트레이트"보다 큰 이유는 오버헤드를 8바이트가 아니라 64바이트로 나누기 때문.
- **8 B 만 보내면 2.1배**. 시간 106.5 µs 중 58 µs 가 nominal 오버헤드(29비트 × 2 µs)라 효율은 30%로 오히려 떨어진다.
- **BRS 를 끈 FD 8 B 는 Classic 보다 느리다**(252 vs 222 µs) — SBC·FSB·긴 CRC 가 추가된 만큼. FD 로 이득을 보려면 BRS 를 켜거나 페이로드를 키워야 한다(추가 연습 4: BRS 켜면 3 B, 끄면 12 B 부터 바이트당 Classic 보다 싸다).
- 랜덤 데이터의 평균은 하한보다 2~3% 길고, 20000개 중 최대도 상한에 못 미친다. 부하 설계는 상한(또는 DP 정확 최악)으로 한다 — 평균은 "보통 날"일 뿐 보장이 아니다.

```svg
<svg viewBox="0 0 760 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">프레임 시간 (스터프 없음 하한, 표준 ID) — 가로 1 px = 0.625 us</text>
  <text x="10" y="51" fill="currentColor">Classic 8 B @500k</text>
  <rect x="150.0" y="36" width="60.8" height="22" class="f-mut" stroke="currentColor"/>
  <text x="180.4" y="51" fill="currentColor" text-anchor="middle">hdr</text>
  <rect x="210.8" y="36" width="204.8" height="22" class="f-acc" stroke="currentColor"/>
  <text x="313.2" y="51" fill="currentColor" text-anchor="middle">data 8 B</text>
  <rect x="415.6" y="36" width="51.2" height="22" class="f-warn" stroke="currentColor"/>
  <text x="441.2" y="51" fill="currentColor" text-anchor="middle">CRC</text>
  <rect x="466.8" y="36" width="28.8" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="495.6" y="36" width="9.6" height="22" fill="none" stroke-dasharray="2 2" stroke="currentColor"/>
  <text x="511.2" y="51" fill="currentColor">222 us</text>
  <text x="10" y="91" fill="currentColor">FD 8 B 500k/2M</text>
  <rect x="150.0" y="76" width="54.4" height="22" class="f-mut" stroke="currentColor"/>
  <text x="177.2" y="91" fill="currentColor" text-anchor="middle">중재</text>
  <rect x="204.4" y="76" width="4.0" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="208.4" y="76" width="51.2" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="259.6" y="76" width="22.4" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="282.0" y="76" width="28.8" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="310.8" y="76" width="9.6" height="22" fill="none" stroke-dasharray="2 2" stroke="currentColor"/>
  <text x="326.4" y="91" fill="currentColor">106.5 us</text>
  <text x="10" y="131" fill="currentColor">FD 64 B 500k/2M</text>
  <rect x="150.0" y="116" width="54.4" height="22" class="f-mut" stroke="currentColor"/>
  <text x="177.2" y="131" fill="currentColor" text-anchor="middle">중재</text>
  <rect x="204.4" y="116" width="4.0" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="208.4" y="116" width="409.6" height="22" class="f-acc" stroke="currentColor"/>
  <text x="413.2" y="131" fill="currentColor" text-anchor="middle">data 64 B (0.5 us/bit)</text>
  <rect x="618.0" y="116" width="26.4" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="644.4" y="116" width="28.8" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="673.2" y="116" width="9.6" height="22" fill="none" stroke-dasharray="2 2" stroke="currentColor"/>
  <text x="688.8" y="131" fill="currentColor">333 us</text>
  <text x="10" y="171" fill="currentColor">FD 64 B 1M/5M</text>
  <rect x="150.0" y="156" width="27.2" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="177.2" y="156" width="1.6" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="178.8" y="156" width="163.8" height="22" class="f-acc" stroke="currentColor"/>
  <text x="260.7" y="171" fill="currentColor" text-anchor="middle">data 64 B</text>
  <rect x="342.6" y="156" width="10.6" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="353.2" y="156" width="14.4" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="367.6" y="156" width="4.8" height="22" fill="none" stroke-dasharray="2 2" stroke="currentColor"/>
  <text x="378.4" y="171" fill="currentColor">139 us</text>
  <rect x="150" y="196" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="170" y="207" fill="currentColor">nominal 위상 오버헤드</text>
  <rect x="320" y="196" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="340" y="207" fill="currentColor">ESI+DLC / CRC 필드 (데이터 위상)</text>
  <rect x="560" y="196" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="580" y="207" fill="currentColor">페이로드</text>
  <text x="10" y="230" fill="currentColor">점선 칸 = IFS 3비트. FD 8 B 는 Classic 의 절반 시간이지만, 그중 58 us 가</text>
  <text x="10" y="246" fill="currentColor">nominal 오버헤드다 (SOF..BRS 17 + ACK..IFS 12 = 29비트 × 2 us).</text>
  <text x="10" y="262" fill="currentColor">숫자는 solution.c can_frame_time(T_NOSTUFF) 와 demo_fd_table.c 표 2 의 "하한" 열과 같다.</text>
</svg>
```

### Part C — 버스 부하와 로봇 관절 버스

`can_bus_load` 는 Σ t_i / T_i 다. 예: Classic 8 B @500k 를 1 ms 마다(222 µs → 22.2%) + FD 64 B @500k/2M 를 2 ms 마다(333 µs → 16.65%) = 38.85% (테스트). 부하가 100% 미만이어도 낮은 우선순위 메시지의 지연은 우선순위와 큐잉에 달렸다 — 응답 시간 분석은 COMM-M05(중재·대기 시간)에서 다룬다. 실무 설계 목표는 보통 최악 부하 50~70% 이하(경험칙, 프로젝트마다 다름).

**로봇 관절 버스.** 1 kHz 루프, 모터마다 16 B 명령 + 16 B 상태. Classic 은 16 B 를 8 B × 2 프레임으로 나눠서 모터당 4프레임이다. demo_fd_table.c 표 3 (실제 출력, 최악 스터핑):

```text
  설정 (nom/data)        | 모터당 부하 | N@50% | N@70% | N@100% | 6모터 부하 | 12모터 부하
  Classic   500k         |    108.0%   |    0  |    0  |     0  |    648.0%  |   1296.0%
  Classic   1M           |     54.0%   |    0  |    1  |     1  |    324.0%  |    648.0%
  FD 1M/1M (BRS=0)       |     45.4%   |    1  |    1  |     2  |    272.4%  |    544.8%
  FD 500k/2M             |     32.3%   |    1  |    2  |     3  |    193.8%  |    387.6%
  FD 1M/2M               |     25.9%   |    1  |    2  |     3  |    155.4%  |    310.8%
  FD 1M/4M               |     16.1%   |    3  |    4  |     6  |     96.9%  |    193.8%
  FD 1M/5M               |     14.2%   |    3  |    4  |     7  |     85.2%  |    170.4%
  FD 1M/8M               |     11.3%   |    4  |    6  |     8  |     67.7%  |    135.3%
```

손 계산 확인 (테스트에 들어 있다): Classic 1M 은 8 B 프레임 최악 135 µs × 4 = 540 µs/ms = 54% → 70% 한계면 1개. FD 1M/5M 16 B 는 nominal (29 + 3) × 1 µs + data (161 + 34) × 0.2 µs = 32 + 39 = 71 µs, 모터당 2프레임 142 µs = 14.2% → 70% 이하 4개(56.8%), 100% 이하 7개(99.4%).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">로봇 관절 버스: 6모터 × (16 B 명령 + 16 B 상태) @1 kHz 의 최악 부하 — demo_fd_table.c 표 3</text>
  <text x="10" y="50" fill="currentColor">Classic 500k</text>
  <rect x="150" y="38" width="480.0" height="16" class="f-mut" stroke="currentColor"/>
  <text x="636.0" y="50" fill="currentColor">648.0% ▶</text>
  <text x="700" y="50" fill="currentColor" text-anchor="end">0</text>
  <text x="10" y="78" fill="currentColor">Classic 1M</text>
  <rect x="150" y="66" width="480.0" height="16" class="f-mut" stroke="currentColor"/>
  <text x="636.0" y="78" fill="currentColor">324.0% ▶</text>
  <text x="700" y="78" fill="currentColor" text-anchor="end">1</text>
  <text x="10" y="106" fill="currentColor">FD 1M/1M BRS=0</text>
  <rect x="150" y="94" width="480.0" height="16" class="f-mut" stroke="currentColor"/>
  <text x="636.0" y="106" fill="currentColor">272.4% ▶</text>
  <text x="700" y="106" fill="currentColor" text-anchor="end">1</text>
  <text x="10" y="134" fill="currentColor">FD 500k/2M</text>
  <rect x="150" y="122" width="465.1" height="16" class="f-mut" stroke="currentColor"/>
  <text x="621.1" y="134" fill="currentColor">193.8%</text>
  <text x="700" y="134" fill="currentColor" text-anchor="end">2</text>
  <text x="10" y="162" fill="currentColor">FD 1M/2M</text>
  <rect x="150" y="150" width="373.0" height="16" class="f-mut" stroke="currentColor"/>
  <text x="529.0" y="162" fill="currentColor">155.4%</text>
  <text x="700" y="162" fill="currentColor" text-anchor="end">2</text>
  <text x="10" y="190" fill="currentColor">FD 1M/4M</text>
  <rect x="150" y="178" width="232.6" height="16" class="f-warn" stroke="currentColor"/>
  <text x="388.6" y="190" fill="currentColor">96.9%</text>
  <text x="700" y="190" fill="currentColor" text-anchor="end">4</text>
  <text x="10" y="218" fill="currentColor">FD 1M/5M</text>
  <rect x="150" y="206" width="204.5" height="16" class="f-warn" stroke="currentColor"/>
  <text x="360.5" y="218" fill="currentColor">85.2%</text>
  <text x="700" y="218" fill="currentColor" text-anchor="end">4</text>
  <text x="10" y="246" fill="currentColor">FD 1M/8M</text>
  <rect x="150" y="234" width="162.5" height="16" class="f-ok" stroke="currentColor"/>
  <text x="318.5" y="246" fill="currentColor">67.7%</text>
  <text x="700" y="246" fill="currentColor" text-anchor="end">6</text>
  <line x1="318.0" y1="30" x2="318.0" y2="260" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="318.0" y="274" fill="currentColor" text-anchor="middle">70%</text>
  <line x1="390.0" y1="30" x2="390.0" y2="260" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="390.0" y="274" fill="currentColor" text-anchor="middle">100%</text>
  <text x="700" y="30" fill="currentColor" text-anchor="end">N@70%</text>
  <text x="10" y="296" fill="currentColor">막대 = 6모터 부하 (200% 에서 자름). 오른쪽 숫자 = 부하 70% 이하로 넣을 수 있는 최대 모터 수.</text>
  <text x="10" y="312" fill="currentColor">6모터를 한 버스에 넣으려면 FD 1M/8M (67.7%) 이 필요 — 아니면 버스를 나눈다 (다리마다 한 버스 등).</text>
</svg>
```

결론: 1 kHz 에 모터당 32 B 왕복이면 Classic CAN 버스 하나에는 모터 하나도 여유 있게 안 들어간다. FD 로 바꾸고 data 속도를 4~8 Mbit/s 로 올리거나, 버스를 나누거나(다리마다 버스 하나), 페이로드를 줄이거나(위치 명령 8 B + 상태 12 B 등), 주기를 나눈다(상태는 500 Hz). 5 Mbit/s 이상 data 위상은 트랜시버(예: CAN SIC 트랜시버), 배선 길이·스텁, data 샘플 포인트, TDC 가 받쳐 줘야 한다 — data 비트 타이밍은 COMM-M06, 실기 검증은 오실로스코프로 data 위상 눈(eye)을 본다.

**복잡도.** DLC 함수 O(1), 헤더 O(41), 레이아웃·시간 계산 O(1), 온-와이어 인코더 O(프레임 비트 ≤ 약 700). 메모리: 메시지 구조체 ~80 B, 인코더 내부 스크래치 568 B (스택), 출력은 호출자 버퍼. DP 는 O(L × 10) 상태.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 73/73 passed (Apple clang 21, 0.1 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san
cc -std=c11 -Wall -Wextra -O2 demo_fd_table.c -o demo_fd_table && ./demo_fd_table
cc -std=c11 -Wall -Wextra -O2 demo_worst_stuff.c -o demo_worst_stuff && ./demo_worst_stuff   # demo 는 계산이라 기계와 무관 (고정 시드)
```

## 7. 자주 하는 실수

**1. "2 Mbps 니까 4배 빠르다".** 프레임 전체에 data 속도를 곱한다. 실제로는 표준 ID 기준 29비트가 nominal 이다. 500k/2M 8 B: 잘못된 계산 (62 + 64) × 0.5 = 63 µs, 정답 29 × 2 + 97 × 0.5 = 106.5 µs. 고친 코드: `bits_nom * T_n + bits_dat * T_d`, nominal = SOF..BRS + ACK..IFS.

**2. FD 길이를 아무 값이나 받는다.** `m.len = 13` 을 그대로 DLC 13 으로 넣으면 수신자는 32바이트를 기대한다(DLC 13 = 32 B). 인코더가 13 → DLC 10(16 B) 으로 몰래 올려도 뒤 3바이트에 무엇이 실렸는지 아무도 모른다. 고친 코드: `canfd_len2dlc` 후 `canfd_dlc2len(d) != len` 이면 `CAN_ELEN`, 패딩은 `canfd_pad` 로 명시적으로.

**3. DLC 를 길이로 쓴다.** `memcpy(dst, f.data, f.dlc)` — FD 의 DLC 15 는 64바이트인데 15바이트만 복사, Classic 의 DLC 12 는 12바이트를 복사해 8바이트 버퍼를 넘친다. 고친 코드: 언제나 `dlc2len()` 을 거친다. Linux 가 `can_frame.len`(옛 이름 `can_dlc`)을 "길이"로 바꾸고 `len8_dlc` 를 따로 둔 이유.

**4. Classic 프레임에 BRS 를 켠다 / FD remote frame 을 만든다.** Classic 헤더에는 BRS 자리가 없고, FD 에는 RTR 이 없다. 드라이버가 플래그를 검사하지 않으면 BRS 가 그냥 무시되어 "왜 안 빨라지지?"가 된다. 고친 코드: `CAN_EFLAGS`, `CAN_ERTR_FD`.

**5. 최악 스터핑을 nominal / data 비율로 나눈다.** "스터프 비트 20% 가산"처럼 총 비트 수에 비례로 붙이면, 가장 비싼 nominal 쪽 스터프(최대 3개 × 2 µs)를 과소평가하거나 data 쪽을 과대평가한다. 고친 코드: 구간 길이로 위상별 상한을 따로 계산(`stuff_max(dyn_nom)`), T_n ≥ T_d 이므로 nominal 쪽을 먼저 채운다.

**6. FD CRC 필드에도 동적 스터핑을 적용한다.** 그러면 프레임 길이가 CRC 값에 따라 흔들리고, 위 표의 CRC 필드 길이(28/33)와 맞지 않는다. FD 의 CRC 필드는 고정 스터프 비트만 — 매 4비트, 직전 비트의 보수.

**7. BRS 를 켜 놓고 data 비트 타이밍을 설정하지 않는다.** STM32 FDCAN 은 NBTP(nominal)와 DBTP(data) 레지스터가 따로 있고(RM 확인 권장), Linux 는 `ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on` 처럼 `dbitrate` 가 따로 있다. data 쪽을 기본값으로 두면 data 위상에서 비트 오류 → 에러 프레임 폭주(COMM-M07). 계산기도 `data_bps < nom_bps` 나 0 을 `CAN_ERATE` 로 막는다.

## 8. 면접 Q&A

<details><summary>Q1. CAN FD 는 왜 중재 구간은 느리게 두고 데이터 구간만 빠르게 하나요?</summary>

중재와 ACK 에서는 여러 노드가 같은 비트 시간에 동시에 구동하고, 각 송신기는 자기가 낸 비트를 버스에서 다시 읽어 비교한다. 이게 성립하려면 버스 양 끝 사이의 왕복 지연(케이블 + 트랜시버 루프 지연 × 2)이 샘플 포인트 전에 끝나야 한다 — 그래서 비트레이트와 버스 길이가 반비례한다(1 Mbit/s Classic 에서 수십 m 정도, 대표값). 데이터 구간은 중재에서 이긴 송신기 하나만 구동하므로 왕복 조건이 필요 없고, 수신기는 단방향 지연만 견디면 된다. 그래서 그 구간만 빠르게 한다. 송신기 자신의 비트 검사는 TDC(Transmitter Delay Compensation)로 지연을 보정한 보조 샘플 포인트(SSP)에서 한다(ISO 확인 권장). ACK 에서 다시 여러 노드가 구동하므로 CRC delimiter 에서 nominal 로 돌아온다.
</details>

<details><summary>Q2. 500k/2M 에서 64바이트 FD 프레임 하나는 몇 µs 인가요? 손으로 계산해 보세요.</summary>

표준 ID, BRS = 1. nominal: SOF 1 + ID 11 + RRS + IDE + FDF + res + BRS = 17, ACK slot + ACK delim + EOF 7 + IFS 3 = 12 → 29비트 × 2 µs = 58 µs. data: ESI 1 + DLC 4 + 512 + SBC 4 + FSB 7 + CRC 21 + CRC delim 1 = 550비트 × 0.5 µs = 275 µs. 합 333 µs (스터프 없음). 최악 스터프는 nominal 쪽 3개, data 쪽 130개(상한식) → 404 µs. DP 로 구한 정확한 최악은 403 µs. 랜덤 데이터 평균은 342.5 µs (demo). 그다음 "BRS 와 CRC delimiter 는 샘플 포인트에서 바뀌지만 둘을 합치면 T_n + T_d 라 이렇게 세도 정확하다"를 덧붙이면 좋다.
</details>

<details><summary>Q3. DLC 가 왜 9 = 12바이트인가요? 9바이트를 보내야 하면?</summary>

DLC 는 Classic 호환 때문에 4비트로 남았고, 16칸으로 64바이트까지 표현하려고 9~15 를 12, 16, 20, 24, 32, 48, 64 로 정의했다. 9바이트는 DLC 9(12 B)로 보내고 3바이트를 패딩한다. 수신자는 12바이트를 받으므로, 실제 길이가 의미 있으면 페이로드 안에 길이 필드를 두거나(상위 프로토콜, 예: ISO-TP 의 FD 확장) 신호 배치(DBC 의 시그널 오프셋)로 해석한다. 패딩 값(0x00 / 0xCC / 0xAA 등)은 프로젝트 규칙 — 패딩 바이트가 의미 없는 값인지 수신 측이 검사하는 경우도 있다.
</details>

<details><summary>Q4. BRS, ESI, FDF 는 각각 누가 세팅하나요? ESI 는 왜 있나요?</summary>

FDF 와 BRS 는 송신 메시지마다 소프트웨어가 정한다(STM32 FDCAN 의 Tx buffer element 에 FDF/BRS 비트, Linux `canfd_frame.flags` 의 `CANFD_BRS` — 레지스터 이름은 RM 확인 권장). ESI 는 컨트롤러가 자기 오류 상태로 자동 세팅한다: error-active 면 dominant(0), error-passive 면 recessive(1). error-passive 노드의 오류 플래그는 recessive 라 다른 노드가 알아채기 어렵기 때문에, 프레임 안에 "나는 지금 아픈 노드다"를 실어 보내 진단·네트워크 관리가 문제 노드를 찾게 한다. TEC/REC 와 상태 전이는 COMM-M07. Linux 에서는 수신 프레임의 `CANFD_ESI` 플래그로 보인다(COMM-M09).
</details>

<details><summary>Q5. Classic CAN 2.0 전용 노드가 FD 프레임이 도는 버스에 있으면 어떻게 되나요?</summary>

그 노드는 FDF 자리를 예약 비트 r0 로 읽는다. 예약 비트는 값과 상관없이 받아들이므로 그 자리에서는 오류가 아니다. 문제는 다음이다: 다음 4비트(res, BRS, ESI, DLC3)를 DLC 로 읽고(추가 연습 5: BRS=1, ESI=0, DLC=15 면 "유령 DLC" 5), 이어지는 data 위상 비트를 nominal 속도로 샘플한다. 빠른 비트를 느리게 샘플하면 곧 6연속 같은 값(스터프 오류)이나 CRC 오류를 만나 **에러 프레임**을 보내고, 이것이 FD 프레임을 파괴한다. 해결: 버스의 모든 노드를 FD 지원 컨트롤러로 하거나, ISO 11898-1:2015 의 protocol exception 을 지원하는 "FD tolerant" 노드(FD 프레임을 보면 버스 idle 까지 무시)로 하거나, 트랜시버 단에서 FD 를 걸러 주는 부품(FD shield 계열 — 제품명 확인 권장)을 쓰거나, 게이트웨이로 버스를 나눈다.
</details>

<details><summary>Q6. FD 로 바꾸면 처리량이 몇 배가 되나요? 항상 이득인가요?</summary>

설정과 페이로드에 따라 다르다. demo 표: 500k/2M 64 B 는 Classic 8 B @500k 대비 처리량 5.3배(1.538 vs 0.288 Mbit/s, 하한 기준). 그러나 8 B 만 보내면 2.1배이고, BRS 를 끈 FD 8 B 는 252 µs 로 Classic 222 µs 보다 느리다(SBC·FSB·CRC-17 이 추가된 만큼). 바이트당 시간으로 보면 BRS 를 켜면 3 B 부터, 끄면 12 B 부터 Classic 8 B 보다 싸다(추가 연습 4). 또 중재는 그대로 nominal 이라 **우선순위 지연(블로킹) 시간**은 긴 FD 프레임 하나가 버스를 오래 점유하는 만큼 늘 수 있다 — 64 B @500k/2M 한 프레임 404 µs 동안 가장 높은 우선순위 메시지도 기다린다.
</details>

<details><summary>Q7. 왜 CRC 가 17/21비트이고 stuff count 가 추가됐나요?</summary>

긴 데이터에 같은 수준의 해밍 거리를 유지하려면 더 긴 CRC 가 필요해서 ≤ 16 B 는 CRC-17, 그 이상은 CRC-21 이다(목표 HD 값은 Bosch/ISO 문서 확인 권장). stuff count 는 Classic 의 알려진 약점 때문이다: 비트 오류가 스터프 비트를 만들거나 지우면 수신자가 그 뒤 비트를 한 칸씩 밀려 읽게 되고, 이런 "스터프 + 비트 오류" 조합 일부는 CRC 가 못 잡는다. ISO 판 FD 는 동적 스터프 수(mod 8, Gray + 패리티)를 CRC 필드에 넣고 CRC 계산에 스터프 비트를 포함시켜 이 구멍을 막았다. 이 변경 전의 Bosch CAN FD 1.0 을 "non-ISO CAN FD"라 부르고 서로 호환되지 않는다 — 컨트롤러 설정에 ISO/non-ISO 선택 비트가 있는 이유(예: STM32 FDCAN, Linux `fd-non-iso on` — 확인 권장).
</details>

<details><summary>Q8. data 위상을 5 Mbit/s 이상으로 올릴 때 무엇을 확인하나요?</summary>

(1) 트랜시버 사양(최대 data 속도, 루프 지연, 대칭성 — 2 / 5 Mbit/s 등급, 8 Mbit/s 는 SIC 트랜시버 권장, 대표값), (2) TDC 활성화와 오프셋 — 루프 지연이 data 비트 시간보다 길어지면 TDC 없이는 송신기가 자기 비트를 잘못 비교해 비트 오류를 낸다, (3) data 샘플 포인트(nominal 보다 앞쪽, 예 70~80% — CiA 권장값 확인), 모든 노드가 같은 tq 와 같은 SP 를 쓰도록(COMM-M06), (4) 토폴로지: 스텁 길이, 종단 저항, 링잉 — 스코프로 data 위상 눈 패턴, (5) 오실레이터 오차 예산이 data 위상에서는 더 빡빡하다. 그리고 계산기로 다시 본다: 1M/5M → 1M/8M 은 64 B 프레임을 139 → 97.8 µs 로 줄일 뿐이고, nominal 29 µs 는 그대로다.
</details>

<details><summary>Q9. 버스 부하를 계산할 때 왜 최악 스터핑을 쓰나요? 평균이면 안 되나요?</summary>

부하는 "이 주기 안에 모든 메시지가 나갈 수 있는가"의 보장 문제라서 데이터가 최악일 때도 성립해야 한다. 데이터는 센서 값에 따라 바뀌고, 0x00 / 0xFF 가 많은 페이로드(정지 상태의 모터, 포화된 센서)는 스터프 비트가 많다. demo 표에서 64 B @500k/2M 의 랜덤 평균은 342.5 µs 지만 상한은 404 µs 로 18% 길다. 평균으로 설계하면 "가만히 있을 때만 버스가 넘친다" 같은 재현 어려운 버그가 된다. 상한식이 1~3비트 보수적인 것(DP 403 µs)은 안전 여유로 받아들인다.
</details>

## 9. 추가 연습

### 연습 1 — 65칸 LUT 로 길이 → DLC

ISR 이나 DMA 완료 콜백에서 반복문 없이 쓰도록 `len2dlc_lut[65]` 를 상수 표로 만들고, 반복문 버전과 0~70 전수 비교하라.

<details><summary>답 보기</summary>

0~8 은 그대로, 9~12 → 9, 13~16 → 10, 17~20 → 11, 21~24 → 12, 25~32 → 13, 33~48 → 14, 49~64 → 15. 65 이상은 표 밖이라 −1. 65바이트 ROM 으로 분기 없는 O(1). Linux 커널의 `can_fd_len2dlc()` 도 같은 방식의 표를 쓴다(구현 세부는 커널 소스 확인 권장).
</details>

### 연습 2 — 패딩 낭비

길이 0~64 각각에 대해 패딩 바이트 수를 구하고, 최대 낭비와 그 길이, 총합, 패딩 없는 길이 개수를 구하라.

<details><summary>답 보기</summary>

실제 출력: `최대 낭비 15 B (len 33), 총 292 B, 패딩 없는 길이 16개, 평균 4.49 B`. 33 → 48 과 49 → 64 가 각각 15바이트를 버린다. 큰 구간(32~48, 48~64)이 16바이트 간격이라 33~48 사이 길이를 자주 쓰는 설계라면 메시지를 둘로 쪼개거나 시그널을 재배치하는 편이 버스 시간을 아낀다.
</details>

### 연습 3 — stuff count 필드

SBC = Gray(s mod 8) 3비트 + 짝수 패리티 1비트를 인코드/디코드하라. 왕복, 1비트 오류 전부 검출, 이웃 값(s, s+1 mod 8)의 Gray 코드가 1비트만 다른지 전수 검사.

<details><summary>답 보기</summary>

Gray: g = s ^ (s >> 1), 역변환은 b = g ^ (g >> 1) ^ (g >> 2). 패리티 p = g2 ^ g1 ^ g0 이면 4비트 안의 1 개수가 짝수. s = 0 → `0000`, 1 → `0011`, 2 → `0110`, 7 → `1001`. 4비트 중 어느 1비트가 뒤집혀도 1 개수가 홀수가 되어 검출된다. 또 SBC 바로 뒤에 오는 고정 스터프 비트(직전 비트의 보수)가 사실상 두 번째 패리티 역할을 한다는 설명도 있다(CiA, 확인 권장). 패리티 방향(짝/홀)은 ISO 확인 권장 — 이 레슨은 짝수로 정의했다.
</details>

### 연습 4 — 바이트당 손익분기

최악 스터핑 기준으로, Classic 8 B @500k 의 바이트당 시간(270/8 = 33.75 µs)보다 바이트당 싼 가장 짧은 FD 길이를 (a) 500k/2M BRS = 1, (b) 500k BRS = 0 에서 구하라. 64 B 를 Classic 8프레임으로 보낼 때와 FD 한 프레임으로 보낼 때도 비교하라.

<details><summary>답 보기</summary>

실제 출력: `FD 500k/2M 은 3 B 부터, BRS 없는 FD 는 12 B 부터 더 싸다`, `64 B — Classic 8 프레임 2160.0 us vs FD 1 프레임 404.0 us (5.35배)`. (a) 3 B: nominal 32 × 2 + data (57 + 8) × 0.5 = 96.5 µs → 32.2 µs/B. 2 B 는 91.5/2 = 45.75 µs/B 로 아직 비싸다. (b) BRS 없는 FD 8 B 는 294 µs(36.75 µs/B)로 Classic 보다 비싸고, 12 B(374 µs, 31.2 µs/B)부터 싸진다. BRS 없이 FD 로 바꾸는 것은 페이로드를 크게 쓸 때만 이득.
</details>

### 연습 5 — Classic 전용 노드가 보는 "유령 DLC"

FD 표준 헤더를 Classic 디코더가 읽으면 FDF 를 r0 로 받아들이고 다음 4비트(res, BRS, ESI, DLC3)를 DLC 로 읽는다. `phantom_dlc(brs, esi, dlc)` 를 쓰고, 노드 종류(Classic / FD-tolerant / FD)와 수신 비트(FDF, res, protocol exception 설정)에 따른 수신 정책 `rx_policy` 를 표로 구현하라.

<details><summary>답 보기</summary>

유령 DLC = (0 << 3) | (BRS << 2) | (ESI << 1) | DLC3. BRS=1, ESI=0, DLC=15 → `0101` = 5. res 가 항상 0 이라 유령 DLC 는 0~7 이다. 정책: Classic 프레임 → 모두 수신. FD 프레임 → Classic 전용 노드는 data 위상을 nominal 로 샘플하다 스터프/CRC/폼 오류로 에러 프레임(FD 프레임 파괴), FD-tolerant 노드는 protocol exception(버스 idle 까지 무시), FD 노드는 수신. FD 노드가 res = 1 을 보면 protocol exception 이 켜져 있으면 무시, 꺼져 있으면 에러 프레임(ISO 11898-1:2015 의 protocol exception 규칙 — 확인 권장).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 21/21 passed
```

## 10. 핵심 정리

- FD DLC: 0~8 그대로, 9~15 = 12, 16, 20, 24, 32, 48, 64. 표에 없는 길이는 가장 작은 DLC 로 **명시적으로** 패딩(최대 낭비 15 B). Classic 의 DLC 9~15 는 8 B, 원래 DLC 는 보존(`len8_dlc`).
- Classic r0(표준) / r1(확장) 자리가 FD 의 FDF=1. 그 뒤 res=0, BRS(1 = 데이터 위상 고속), ESI(1 = 송신자 error-passive). RTR 자리는 RRS=0 — FD 에 remote frame 없음.
- 비트 수 (IFS 포함): Classic 표준 47 + 8n, FD 표준 62 + 8n (≤ 16 B) / 67 + 8n. FD 표준 nominal 29비트(SOF..BRS 17 + ACK..IFS 12), 나머지 data.
- 시간 = nominal 비트 × T_n + data 비트 × T_d. BRS·CRC delim 의 반쪽 비트 합 = T_n + T_d 라 샘플 포인트와 무관.
- 동적 스터프 상한 ⌊(k−1)/4⌋, 위상별로 나눠 nominal 쪽 먼저(표준 최대 3). FD CRC 필드는 고정 스터프(6/7)라 길이가 CRC 와 무관. DP 정확 최악은 상한식보다 1~3비트 짧다.
- 500k/2M: 64 B 333~404 µs, Classic 8 B @500k 222~270 µs. 처리량 5.3배, 8 B 만이면 2.1배, BRS 끈 FD 8 B 는 Classic 보다 느림.
- 1 kHz × 모터당 32 B 왕복: Classic 1M 은 70% 한계에 1모터, FD 1M/5M 4모터, 1M/8M 6모터 → 버스 분할·페이로드 축소가 현실적 설계.
- 부하 설계는 최악 스터핑으로, 비트레이트 오류(data < nominal)와 플래그 오류는 드라이버 API 에서 코드로 막는다.

## 참고자료

- ISO 11898-1:2015 Road vehicles — Controller area network (CAN) — Part 1: Data link layer and physical signalling (FD 프레임 형식, stuff count, 고정 스터프 비트, protocol exception, ESI). 이 레슨의 세부 규칙 중 "확인 권장"으로 표시한 것은 원문 미확인.
- Robert Bosch GmbH, CAN with Flexible Data-Rate Specification Version 1.0 (2012) — non-ISO CAN FD, CRC-17/21 정의.
- Greg Cook, CRC RevEng catalogue — CRC-17/CAN-FD (poly 0x1685B, check 0x04F03), CRC-21/CAN-FD (poly 0x102899, check 0x0ED841): https://reveng.sourceforge.io/crc-catalogue/17plus.htm , CRC-15/CAN (check 0x059E): https://reveng.sourceforge.io/crc-catalogue/
- Linux `include/linux/can/length.h` — 필드별 비트 수 매크로(헤더 19/39/22/41, CRC 필드 16/28/33, footer 9, intermission 3), `can_bitstuffing_len`: https://raw.githubusercontent.com/torvalds/linux/master/include/linux/can/length.h
- Linux netdev 패치 "can: length: fix bitstuffing count" (Vincent Mailhol, 2023) — SBC 4비트, FSB 6/7 의 근거 설명: https://lists.openwall.net/netdev/2023/06/11/15
- CAN in Automation (CiA), "CAN FD — The basic idea", "Cyclic redundancy check (CRC) in CAN frames": https://www.can-cia.org/can-knowledge/
- STMicroelectronics, STM32G4 / STM32H7 reference manual — FDCAN 장(NBTP / DBTP, TDC, Tx buffer element 의 FDF / BRS / ESI 비트 — RM 확인 권장).
- 관련 레슨: COMM-M04 (Classic 프레임 인코더·스터핑·최악 길이), COMM-M05 (중재와 대기 시간), COMM-M06 (nominal / data 비트 타이밍 계산기), COMM-M07 (TEC/REC, error-passive → ESI), COMM-M09 (SocketCAN `canfd_frame`, `CAN_RAW_FD_FRAMES`, MTU 72), COMM-B08 (CRC 검출 능력), ALGO-M01 (CRC 이론).
