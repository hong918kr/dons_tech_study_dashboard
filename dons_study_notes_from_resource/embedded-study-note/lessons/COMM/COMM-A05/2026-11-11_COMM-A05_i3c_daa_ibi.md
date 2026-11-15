---
id: COMM-A05
time: 110분
updated: 2026-11-11
---
# COMM-A05 I3C 기초: 동적 주소 할당과 In-Band Interrupt

## 학습 목표

- I3C 가 I2C 에서 **무엇을 물려받고(오픈드레인 wired-AND 중재) 무엇을 바꿨는지(동적 주소, 인터럽트 선 없는 IBI, 푸시풀 데이터)** 를 한 문단으로 말한다.
- ENTDAA 한 라운드를 비트 단위로 그린다: `Sr 7E/R` → 48비트 PID + BCR + DCR 64비트 오픈드레인 중재 → 7비트 DA + 홀수 패리티 → ACK. 왜 **가장 작은 ID 가 가장 작은 주소**를 받는지 설명한다.
- 주소 표를 만든다: 예약(0x00–0x07, 0x7E, 0x7E 의 1비트 이웃 7개), 레거시 I2C 정적 주소, 이미 쓴 동적 주소. 레거시가 섞인 버스에서 **충돌 없는 할당**을 테스트로 증명한다.
- IBI 를 구현한다: 타깃이 SDA 를 당겨 START → 주소 헤더 중재(낮은 DA 가 이김) → 컨트롤러 ACK/NACK → MDB. Hot-Join(0x02/W), NACK 뒤 **Sr 로 DISEC**, 컨트롤러 자신의 START 를 IBI 가 이기는 경우까지.
- 고장을 검출한다: 같은 PID 두 개(→ 개수 대조), 진 뒤 놓지 않는 타깃(→ GETPID 대조), DA 바이트 비트 오류(→ 패리티 NACK 재시도), 주소 고갈. Linux `i3c_master_bus_init()` 순서로 버스를 올리고 브링업 로그를 읽는다.

## 1. 문제

I3C(MIPI I3C / I3C Basic)는 최신 IMU·기압·자력계, 폰 카메라 모듈의 센서 버스, DDR5 DIMM 의 SPD 허브(JEDEC 이 I3C Basic 을 채택 — 확인 권장) 같은 곳에서 I2C 를 대체하고 있다 (카메라 *데이터* 경로인 MIPI CSI-2 는 CAM-B05 — 여기서는 제어 버스만 다룬다). 면접에서는 "I2C 와 뭐가 다르냐", "주소는 누가 정하냐", "인터럽트 핀 없이 센서가 어떻게 알리냐", "레거시 I2C 장치를 같은 선에 둬도 되냐" 가 나오고, 브링업에서는 **DAA 가 장치를 하나 덜 찾았다**, **IBI 가 안 온다**, **같은 부품 두 개가 주소 하나를 같이 쓴다** 를 디버그해야 한다.

COMM-M02 에서 본 I2C 중재가 I3C 의 뼈대다. 오픈드레인(open-drain) 구간에서 SDA 는 모든 노드의 **wired-AND** 이고, 1 을 냈는데 0 을 읽은 쪽이 물러난다. I3C 는 이 규칙을 두 군데에 쓴다:

1. **ENTDAA (동적 주소 할당, Dynamic Address Assignment)**: 주소 없는 타깃들이 각자 64비트 ID(48비트 Provisioned ID + BCR + DCR)를 동시에 MSB 부터 낸다 → 가장 작은 ID 하나만 남는다 → 컨트롤러가 그 타깃에게 7비트 주소를 준다 → 진 타깃은 다음 라운드에 다시. 그래서 정적 주소 핀(ADDR 스트랩)이 필요 없다.
2. **IBI (In-Band Interrupt)**: 버스가 쉬고 있을 때 타깃이 SDA 를 당겨 START 를 요청하고, 주소 헤더에 자기 동적 주소 + R 을 낸다 → 여러 타깃이 동시에 내면 **가장 낮은 주소가 이긴다**. 인터럽트 선(INT 핀)이 사라지는 대신 주소가 곧 우선순위가 된다.

아래는 이 레슨 모델의 ENTDAA 한 번(타깃 N 개)의 바이트 배치다.

```svg
<svg viewBox="0 0 740 286" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">ENTDAA 트랜잭션 (이 레슨 모델) — 클럭 수 = OD 9 + PP 9 + 라운드 × OD 82 + OD 9</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="40" height="34" class="f-mut"/><rect x="60" y="40" width="90" height="34" class="f-acc"/>
    <rect x="150" y="40" width="35" height="34" class="f-acc"/><rect x="185" y="40" width="110" height="34" class="f-ok"/>
    <rect x="295" y="40" width="35" height="34" class="f-ok"/><rect x="330" y="40" width="40" height="34" class="f-mut"/>
    <rect x="370" y="40" width="90" height="34" class="f-acc"/><rect x="460" y="40" width="35" height="34" class="f-acc"/>
    <rect x="20" y="118" width="280" height="34" class="f-acc"/><rect x="300" y="118" width="70" height="34" class="f-acc"/>
    <rect x="370" y="118" width="70" height="34" class="f-acc"/><rect x="440" y="118" width="120" height="34" class="f-acc"/>
    <rect x="560" y="118" width="50" height="34" class="f-acc"/><rect x="610" y="118" width="40" height="34" class="f-acc"/>
    <rect x="20" y="206" width="40" height="34" class="f-mut"/><rect x="60" y="206" width="90" height="34" class="f-acc"/>
    <rect x="150" y="206" width="55" height="34" class="f-acc"/><rect x="205" y="206" width="40" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="40" y="62">S</text><text x="105" y="62">7E / W</text><text x="167" y="62">A</text><text x="240" y="62">CCC 0x07</text>
    <text x="312" y="62">T</text><text x="350" y="62">Sr</text><text x="415" y="62">7E / R</text><text x="477" y="62">A</text>
    <text x="160" y="140">PID[47:0]  (48비트)</text><text x="335" y="140">BCR</text><text x="405" y="140">DCR</text>
    <text x="500" y="140">DA[6:0]</text><text x="585" y="140">PAR</text><text x="630" y="140">A</text>
    <text x="40" y="228">Sr</text><text x="105" y="228">7E / R</text><text x="177" y="228">NACK</text><text x="225" y="228">P</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="92">S·7E/W: 컨트롤러, OD (IBI 와 중재)</text><text x="20" y="106">A: 타깃들 (wired)   CCC·T: 컨트롤러, PP</text>
    <text x="330" y="92">Sr 7E/R: 라운드 시작 — 주소 없는 타깃이 ACK</text>
    <text x="20" y="170">주소 없는 타깃 전부가 동시에 냄 — OD wired-AND, 가장 작은 64비트 ID 만 남는다</text>
    <text x="440" y="186">컨트롤러: 홀수 패리티</text><text x="615" y="186">이긴 타깃</text>
    <text x="660" y="140">→ 다음</text><text x="660" y="156">라운드</text>
    <text x="255" y="228">주소 없는 타깃이 하나도 없으면 NACK → STOP</text>
    <text x="10" y="276">진 타깃은 다음 Sr 7E/R 에서 다시 경쟁.</text>
  </g>
  <g stroke="currentColor"><rect x="300" y="266" width="14" height="10" class="f-mut"/><rect x="410" y="266" width="14" height="10" class="f-acc"/><rect x="560" y="266" width="14" height="10" class="f-ok"/></g>
  <g fill="currentColor" font-size="12"><text x="320" y="276">START/STOP</text><text x="430" y="276">오픈드레인 (OD)</text><text x="580" y="276">푸시풀 (PP)</text></g>
</svg>
```

**모델 (주어짐).** 이 레슨은 I3C SDR 버스를 **트랜잭션 수준**으로 돌리고, 중재가 일어나는 곳(START 뒤 주소 헤더, ENTDAA 의 64비트)만 **비트 수준**으로 계산한다.

| 항목 | 이 레슨의 정의 (실제 규격과 다른 점은 5. 풀이 끝 표) |
|---|---|
| 비트 중재 | `od_arbitrate()` — 참가자마다 MSB 부터 비트, 버스 = AND, 1 냈는데 0 → 빠짐. COMM-M02 와 같은 규칙 |
| 주소 헤더 | 7비트 주소를 왼쪽으로 1 시프트하고 bit 0 = RnW. START 뒤 헤더만 중재 가능 (컨트롤러 7E/W, IBI 타깃 DA/R, Hot-Join 0x02/W). Sr 뒤 헤더는 중재 없음 |
| 레거시 I2C | 자기 정적 주소 헤더에만 ACK, 나머지 I3C 트래픽은 무시 |
| 클럭 수 | 헤더·ACK·ENTDAA 64비트·DA 바이트 = OD, CCC·데이터 바이트 = PP 9 (8 + T 비트) |
| 관찰자 | `collisions`(한 헤더에 2개 이상 ACK), `contention`(PP 에서 두 드라이버가 다른 값), `dup_da` — 펌웨어는 못 보고 LA·스코프로만 보이는 것. 테스트용 |
| CCC 번호 | Linux `include/linux/i3c/ccc.h` 값: ENEC 0x00/0x80, DISEC 0x01/0x81, RSTDAA 0x06, ENTDAA 0x07, SETDASA 0x87, SETNEWDA 0x88, GETPID 0x8D, GETBCR 0x8E, GETDCR 0x8F. 이벤트 비트 SIR(IBI) = bit0, HJ = bit3 |

### Part A — 비트 중재, 주소 표, ENTDAA

- `od_arbitrate(n, w, sticky, nbits, in, lost_at)` — n 명의 MSB-first 오픈드레인 중재. 빠진 비트 위치 기록, `sticky` = "져도 안 놓는" 고장.
- `addr_slots_init()` / `get_free_addr()` — 128칸 주소 표 (FREE / RSVD / I2C / I3C), 가장 낮은 빈 주소.
- `da_with_parity(da)` — ENTDAA 의 주소 바이트 `DA[6:0] | PAR` (8비트 전체 홀수 패리티).
- `ctrl_entdaa()` — 라운드 루프: Sr 7E/R → 64비트 중재 → 주소 선택 → DA+PAR → ACK/NACK.
- 테스트: 8비트 헤더 쌍 65,536 개 전수(승자 = 작은 값, 진 쪽 `lost_at` = 처음 다른 비트), 64비트 다자 중재 4,000번, 고장 참가자, 예약 칸 16개·빈 칸 110개, 패리티 128개 전수, 타깃 5 + 레거시 2 (0x0B 스마트 배터리, 0x50 EEPROM) 고정 예제, 랜덤 버스 1,500개(주소 유일·예약/레거시 회피·**주소 순서 = ID 순서**·모든 주소 읽기에서 충돌 0).

### Part B — IBI, Hot-Join, SETDASA, 고장

- `ctrl_arb_header(c, own_hdr)` — START 뒤 헤더 하나: 컨트롤러(7E/W 또는 클럭만) + IBI 타깃 + Hot-Join 타깃 중재 → 컨트롤러가 이기면 `HDR_WON`, 타깃이 이기면 ACK/NACK·MDB·DISEC 처리 후 `HDR_SERVED`.
- `ctrl_verify(c, n_expected)` — DAA 뒤 GETPID/GETBCR/GETDCR 대조 + 보드 표 개수 대조.
- `ctrl_setdasa(c, sa, da)` — 정적 주소가 있는 타깃에 원하는 동적 주소를 직접 준다.
- 주입·검증할 경우 (전부 테스트):
  1. IBI 4개 동시 → DA 0x08, 0x09, 0x0A, 0x0B 순서, 각자 MDB.
  2. 컨트롤러가 GETPID 를 시작하는 그 START 에 IBI 가 겹침 → IBI 가 이기고 먼저 처리(`n_preempt = 1`), 그 다음 GETPID.
  3. 컨트롤러가 원하지 않는 IBI → NACK + **Sr** DISEC(direct, SIR) → 다시 요청 안 함.
  4. 늦게 전원이 들어온 타깃(Hot-Join) + 동시에 IBI → 0x02 가 이김 → ENTDAA 가 다시 돌고 새 주소. HJ 를 허용하지 않으면 NACK + Sr DISEC(bcast, HJ).
  5. SETDASA 로 0x30 → 0x20, ENTDAA 는 나머지만 0x08, 0x09.
  6. **64비트 ID 가 같은 두 타깃** → 둘 다 이겨 같은 DA → GETPID 로는 못 잡고 개수 대조(`I3C_ERR_MISSING`)로 잡는다. 그 주소를 읽으면 `collisions`/`contention` 발생.
  7. DA 바이트 1비트 오류 → 패리티 NACK → 같은 타깃이 다시 이겨 성공. 같은 ID 가 두 번 NACK → `I3C_ERR_DAA_NACK`. 2비트 오류(패리티 통과) → verify 의 GETPID NACK 로 검출.
  8. 진 뒤에도 놓지 않는 타깃 → 버스에 **유령 ID** → 그 주소의 GETPID 가 다름 → `I3C_ERR_PID_MISMATCH`.
  9. 레거시 표를 무시하는 할당기 → 0x0B 에 I3C 타깃과 배터리가 같이 ACK. 올바른 할당기 → 0x0C.
  10. 주소 고갈: 빈 칸 110, 타깃 115 → 110 할당 + `I3C_ERR_NOADDR`, 5개는 주소 없음.

### Part C — 버스 초기화와 운용

```c
typedef struct { uint64_t pid; uint8_t sa, init_da; bool want_ibi; } BoardI3c;   /* DT 의 i3c 노드 한 개 */
typedef struct { const BoardI3c *i3c; int ni3c; const uint8_t *i2c_sa; int ni2c; bool hot_join; } Board;
int i3c_bus_init(Ctrl *c, const Board *bd);
/* 주소 표 → RSTDAA → DISEC(SIR|HJ) → SETDASA → ENTDAA → verify → ENEC(SIR) 개별 → ENEC(HJ) */
```

- 보드: IMU(정적 0x68 → SETDASA 0x08, IBI), 자력계(IBI), 기압계(IBI 안 씀), 늦게 켜지는 ToF, 레거시 배터리 0x0B·EEPROM 0x50. 초기화 로그 → 운용 로그(Hot-Join + IBI 2개 + 레거시 읽기) → 충돌 0, IBI 를 원하지 않는 기압계는 요청을 걸어도 버스에 못 나옴.
- 소크: 랜덤 보드 40개 × 400 스텝 (IBI, 늦은 합류, 레거시·I3C 읽기 섞음) → 매 스텝 주소 불변식, 끝나면 "건 IBI = 처리 + NACK", 충돌 0, 레거시 ACK 수 = 레거시 읽기 수.
- `demo_ibi.c`: 우선순위 굶주림 — 부하별로 DA 0x08 / 0x0B / 0x0F 의 IBI 지연 (실측 표).

## 2. Problem (English)

> **I3C dynamic address assignment and in-band interrupts on a simulated mixed bus.**
>
> The bus is modelled at transaction level, except where arbitration happens: an open-drain wired-AND where each participant shifts its bits MSB-first and drops out when it sends 1 but reads 0. Legacy I2C devices ACK only their own static address.
>
> 1. `od_arbitrate(n, w, sticky, nbits, in, lost_at)`: resolve n participants; return the bus value, leave `in[k]` true only for survivors, record the bit position where each loser dropped. `sticky[k]` models a faulty target that never releases.
> 2. Build the address table: reserved = 0x00–0x07, 0x7E and every address one bit away from 0x7E; mark legacy static addresses; `get_free_addr()` returns the lowest free slot.
> 3. `ctrl_entdaa()`: after `START 7E/W` + CCC 0x07, repeat `Sr 7E/R`; if any target without a dynamic address ACKs, arbitrate the 64-bit {PID[47:0], BCR, DCR}, send `DA[6:0]` plus an odd-parity bit, and record the device if the winner ACKs. A target NACKs a byte with bad parity; NACK twice for the same ID → error. Stop when nobody ACKs.
> 4. `ctrl_arb_header()`: after a START, the controller's `7E/W`, IBI requests `(DA<<1)|1` and Hot-Join `0x02/W` arbitrate. Accept an IBI only from a known device with IBI enabled (read one MDB byte if BCR[2] is set); otherwise NACK and disable it with a DISEC sent **after a repeated START**. Accept Hot-Join only if enabled, then rerun ENTDAA.
> 5. `ctrl_verify()`, `ctrl_setdasa()`, and `i3c_bus_init()` following the Linux `i3c_master_bus_init()` order.
>
> Example: five targets with IDs `02083f120000:06:00 < 02083f120000:26:00 < 04a2100a1000:06:00 < 04a2100a5000:06:00 < 0bee30040000:02:00` and legacy devices at 0x0B and 0x50 receive 0x08, 0x09, 0x0A, 0x0C, 0x0D, in that order, in 5 rounds (OD clocks = 9 + 5·82 + 9 = 428).
>
> Follow-ups: Why does the lowest ID win? What if two targets have the same PID? Why is 0x7F not usable? What happens when an IBI collides with the controller's own START? How do you stop a target that keeps sending unwanted IBIs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: Part A 5개(`od_arbitrate`, `addr_slots_init`, `get_free_addr`, `da_with_parity`, `ctrl_entdaa`), Part B 3개(`ctrl_arb_header`, `ctrl_verify`, `ctrl_setdasa`), Part C 1개(`i3c_bus_init`). 주어진 것: 타깃·레거시·버스 구조체, `bus_pp_read`(PP 바이트 + contention 관찰), `ctrl_start`/`ctrl_ccc_begin`(START 를 이길 때까지, 상한 8번), 이벤트 CCC(`ctrl_events`, Sr 버전 `ctrl_events_sr`), `ctrl_rstdaa`, `ctrl_get`(GET CCC), `ctrl_priv_read`, `ctrl_service`(메인 루프 조각), 로그, xorshift32. 스텁 상태에서는 헤더 중재가 늘 `HDR_IDLE` 이라 `ctrl_start` 가 8번 안에 `I3C_ERR_PREEMPT` 로 끝나고 서비스 루프도 바로 빠진다 — 멈추지 않고 FAIL 한다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
A1 exhaustive 8-bit pairs: bad=65535
FAIL starter.c:429  bad == 0
…
A6 random DAA: 1500 buses, 0 addresses assigned, bad=1500
…
C2 soak: 40 boards x 400 steps, 0 IBIs served, 0 hot-joins, 0 I3C reads, bad=40
FAIL starter.c:784  bad == 0
5/64 passed
```

(exit 1, ASan/UBSan 빌드도 깨끗하게 끝남. 통과한 5개는 스텁이 우연히 맞는 것: "아무도 없으면 0", "요청이 없으면 service 0", 빈 버스의 불변식 등.)

## 4. 힌트

<details><summary>힌트 1 — od_arbitrate 는 비트마다 두 단계</summary>

① 참가 중(`in[k]`)인 드라이버만 AND 해서 그 비트의 버스 레벨을 구한다(빠진 참가자는 1 = 놓음이므로 AND 에서 빼도 같다). ② 레벨이 0 이고 내가 1 을 냈으면 빠진다 — 단 `sticky` 면 안 빠진다. 순서를 바꿔 ①에서 바로 빼면, 같은 비트에서 0 을 낸 다른 참가자보다 먼저 계산된 참가자만 빠지는 순서 의존 버그가 생긴다. 반환은 비트를 시프트해 모은 버스 값 — 컨트롤러가 *읽은* ID 다.

</details>

<details><summary>힌트 2 — 0x7E 의 1비트 이웃</summary>

`0x7E ^ (1 << i)`, i = 0..6 → 0x7F, 0x7C, 0x7A, 0x76, 0x6E, 0x5E, 0x3E. 브로드캐스트 헤더의 비트 하나가 잡음으로 뒤집혀도 "어떤 타깃의 개별 주소" 로 읽히지 않게 하려는 것이다. Linux `i3c_bus_init_addrslots()` 가 정확히 이 7개 + 0x7E + 0x00–0x07 을 예약한다 (2026-11 소스 확인).

</details>

<details><summary>힌트 3 — ENTDAA 라운드에서 컨트롤러가 기록할 ID</summary>

컨트롤러가 아는 ID 는 *버스에 보인 64비트* = `od_arbitrate` 의 반환값이다. 정상이면 이긴 타깃의 ID 와 같지만, 고장(놓지 않는 타깃)이면 아무의 ID 도 아닌 값이 된다 — 그래서 나중에 GETPID 로 대조할 의미가 있다. 주소 표에는 컨트롤러가 *보낸* 주소를 적는다(타깃이 실제로 받은 주소는 비트 오류가 있으면 다를 수 있다 — Part B 7).

</details>

<details><summary>힌트 4 — 헤더 중재 참가자 목록</summary>

컨트롤러가 내면 인덱스 0 에 `own_hdr`, 그 다음 타깃: DA 가 있고 `ibi_req` 이고 자기 `ev_en` 에 SIR 이 켜져 있으면 `(DA<<1)|1`, DA 가 없고 `hj_req` 이고 HJ 가 켜져 있으면 `0x04`(= 0x02/W). 7E/W = 0xFC 는 어떤 타깃 헤더보다 크므로 타깃이 하나라도 있으면 컨트롤러가 진다. 이긴 헤더가 `own_hdr` 이고 `in[0]` 이 남아 있으면 `HDR_WON`.

</details>

<details><summary>힌트 5 — NACK 한 다음 DISEC 는 왜 Sr 로?</summary>

NACK → STOP → 새 START + 7E/W 로 DISEC 를 보내려 하면, 같은 타깃이 *아직 요청을 걸어 둔 채라* 그 START 의 헤더 중재에서 또 이긴다 → 또 NACK → … 끝나지 않는다. NACK 바로 뒤 Sr(repeated START) 은 중재가 없으므로 컨트롤러가 버스를 쥔 채로 `7E/W DISEC` 를 보낼 수 있다. 주어진 `ctrl_events_sr()` 이 이 경로다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 물리 | SDR 은 OD(중재·주소·ACK)와 PP(데이터, 최대 12.5 MHz — 확인 권장) 가 섞인다. OD 에서 wired-AND → I2C 와 같은 비파괴 중재 |
| 2. 주소 | 정적 주소 대신 동적 주소: ENTDAA(PID 중재) 또는 SETDASA(정적 주소 → 동적). 예약·레거시 칸을 피하는 주소 표 |
| 3. IBI | INT 선 대신 타깃이 START. 헤더 중재 → 낮은 DA 가 우선. ACK 후 MDB, NACK 후 Sr DISEC |
| 4. 확장 | Hot-Join(0x02/W) → ENTDAA 재실행. 같은 PID → 충돌, 진 뒤 안 놓는 타깃 → 유령 ID, 비트 오류 → 패리티 |
| 5. 시스템 | Linux bus init 순서, 우선순위 = 주소 → 굶주림 측정, 레거시 섞인 버스의 속도 제약 |

### Part A — 64비트 중재: 가장 작은 ID 가 이긴다

두 타깃 A(PID `0005 0000 0000`)와 B(PID `000A 0000 0000`)가 같은 라운드에 ID 를 낸다. PID 의 앞 12비트는 둘 다 0 이라 아무 일도 없고, 13번째 비트(PID bit 35)에서 처음 갈린다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">ENTDAA ID 단계 앞 16비트 (PID[47:32]) — A = 0x0005…, B = 0x000A…</text>
  <rect x="566" y="44" width="38" height="168" class="f-warn" opacity=".35"/>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".4"><line x1="129" y1="44" x2="129" y2="212"/><line x1="167" y1="44" x2="167" y2="212"/><line x1="205" y1="44" x2="205" y2="212"/><line x1="243" y1="44" x2="243" y2="212"/><line x1="281" y1="44" x2="281" y2="212"/><line x1="319" y1="44" x2="319" y2="212"/><line x1="357" y1="44" x2="357" y2="212"/><line x1="395" y1="44" x2="395" y2="212"/><line x1="433" y1="44" x2="433" y2="212"/><line x1="471" y1="44" x2="471" y2="212"/><line x1="509" y1="44" x2="509" y2="212"/><line x1="547" y1="44" x2="547" y2="212"/><line x1="585" y1="44" x2="585" y2="212"/><line x1="623" y1="44" x2="623" y2="212"/><line x1="661" y1="44" x2="661" y2="212"/><line x1="699" y1="44" x2="699" y2="212"/></g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="110,70 129,70 129,50 148,50 148,70 167,70 167,50 186,50 186,70 205,70 205,50 224,50 224,70 243,70 243,50 262,50 262,70 281,70 281,50 300,50 300,70 319,70 319,50 338,50 338,70 357,70 357,50 376,50 376,70 395,70 395,50 414,50 414,70 433,70 433,50 452,50 452,70 471,70 471,50 490,50 490,70 509,70 509,50 528,50 528,70 547,70 547,50 566,50 566,70 585,70 585,50 604,50 604,70 623,70 623,50 642,50 642,70 661,70 661,50 680,50 680,70 699,70 699,50 718,50 718,70"/>
    <polyline points="110,115 604,115 604,95 642,95 642,115 680,115 680,95 718,95"/>
    <polyline points="110,160 566,160 566,140 718,140"/>
    <polyline points="110,205 604,205 604,185 642,185 642,205 680,205 680,185 718,185"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="64">SCL</text><text x="10" y="110">SDA 드라이버 A</text>
    <text x="10" y="155">SDA 드라이버 B</text><text x="10" y="200">SDA 버스</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="129" y="36">47</text><text x="167" y="36">46</text><text x="205" y="36">45</text><text x="243" y="36">44</text><text x="281" y="36">43</text><text x="319" y="36">42</text><text x="357" y="36">41</text><text x="395" y="36">40</text><text x="433" y="36">39</text><text x="471" y="36">38</text><text x="509" y="36">37</text><text x="547" y="36">36</text><text x="585" y="36">35</text><text x="623" y="36">34</text><text x="661" y="36">33</text><text x="699" y="36">32</text></g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="234">점선 = SCL↑ (샘플·비교 시점), 위 숫자 = PID 비트 번호.</text>
    <text x="10" y="252">강조 칸 = PID bit 35: A 는 0, B 는 1 → 버스 0 → B 가 빠진다.</text>
    <text x="10" y="270">B 는 그 뒤로 SDA 를 놓은 채(1) — 버스엔 A 의 비트만(0101). 컨트롤러가 읽은 ID = A 의 ID.</text>
    <text x="10" y="288">B 는 이번 라운드 DA 를 못 받고 다음 Sr 7E/R 에서 다시 낸다.</text>
    <text x="10" y="306">규칙 = od_arbitrate (8비트 쌍 65,536개 전수 검증).</text>
  </g>
</svg>
```

MSB 부터 비교하므로 "처음 다른 비트에서 0 을 낸 쪽" = **64비트 값 {PID, BCR, DCR} 이 작은 쪽**이 이긴다 (COMM-M02 의 주소 바이트 중재와 같은 결론). 이 레슨의 검증 (실제 출력):

```text
A1 exhaustive 8-bit pairs: bad=0
A1b random 64-bit multi-party: bad=0
```

A1b 는 2–8명, 공통 접두사가 길고 같은 값이 섞인 경우까지 4,000번: 버스 값 = 참가자 중 최솟값, 남은 참가자 = 최솟값과 같은 ID 를 낸 참가자 전부. **"전부"** 가 중요하다 — 같은 ID 가 둘이면 둘 다 남는다(Part B 6).

**고장 참가자** (A1c): 0x5A(정상) vs 0xA5(져도 안 놓음). 첫 비트에서 정상 쪽 0 이 이기지만 고장 쪽이 계속 0 을 내므로 둘째 비트에서 정상 쪽이 1 을 내고 0 을 읽어 빠진다. 버스 값은 `0x25` — 0x5A 도, 0xA5 도, AND(0x00) 도 아니다. 이것이 Part B 8 의 "유령 ID" 의 원리다.

### Part A — 주소 표

| 구간 | 상태 | 이유 |
|---|---|---|
| 0x00–0x07 | 예약 | I2C 에서도 예약 구간 (general call 0x00 등). Linux 도 0–7 예약 |
| 0x02 | 예약 (헤더로는 Hot-Join) | 타깃이 START 뒤 `0x02/W` 를 내면 Hot-Join 요청 (스펙 확인 권장) |
| 0x7E | 예약 | 브로드캐스트 주소 |
| 0x7F 0x7C 0x7A 0x76 0x6E 0x5E 0x3E | 예약 | 0x7E 에서 1비트 차이 — 1비트 오류가 개별 주소로 둔갑하지 않게 |
| 레거시 I2C 정적 주소 | I2C | 보드 표(Linux DT 의 i2c 노드, LVR)에서 온다 |
| 나머지 | FREE → 할당하면 I3C | 가장 낮은 빈 칸부터 |

예약 16칸 → 레거시가 없으면 빈 칸 **112**, 이 레슨의 예제(레거시 0x0B, 0x50)는 **110** (실제 출력 `A2 slots: reserved=16 free=110`). 레거시 주소가 이미 예약 칸이면(예: 0x7C) 그대로 예약으로 둔다. 아래는 Part C 보드가 초기화를 마치고 ToF 가 Hot-Join 한 뒤의 주소 표다.

```svg
<svg viewBox="0 0 740 302" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">주소 표 (Part C 보드, C1 실행 후) — 행 = 상위 3비트, 열 = 하위 4비트</text>
  <g stroke="currentColor">
    <rect x="60" y="40" width="40" height="22" class="f-mut"/><rect x="102" y="40" width="40" height="22" class="f-mut"/><rect x="144" y="40" width="40" height="22" class="f-mut"/><rect x="186" y="40" width="40" height="22" class="f-mut"/><rect x="228" y="40" width="40" height="22" class="f-mut"/><rect x="270" y="40" width="40" height="22" class="f-mut"/><rect x="312" y="40" width="40" height="22" class="f-mut"/><rect x="354" y="40" width="40" height="22" class="f-mut"/><rect x="396" y="40" width="40" height="22" class="f-ok"/><rect x="438" y="40" width="40" height="22" class="f-ok"/><rect x="480" y="40" width="40" height="22" class="f-ok"/><rect x="522" y="40" width="40" height="22" class="f-warn"/><rect x="564" y="40" width="40" height="22" class="f-ok"/><rect x="606" y="40" width="40" height="22" fill="none"/><rect x="648" y="40" width="40" height="22" fill="none"/><rect x="690" y="40" width="40" height="22" fill="none"/>
    <rect x="60" y="64" width="40" height="22" fill="none"/><rect x="102" y="64" width="40" height="22" fill="none"/><rect x="144" y="64" width="40" height="22" fill="none"/><rect x="186" y="64" width="40" height="22" fill="none"/><rect x="228" y="64" width="40" height="22" fill="none"/><rect x="270" y="64" width="40" height="22" fill="none"/><rect x="312" y="64" width="40" height="22" fill="none"/><rect x="354" y="64" width="40" height="22" fill="none"/><rect x="396" y="64" width="40" height="22" fill="none"/><rect x="438" y="64" width="40" height="22" fill="none"/><rect x="480" y="64" width="40" height="22" fill="none"/><rect x="522" y="64" width="40" height="22" fill="none"/><rect x="564" y="64" width="40" height="22" fill="none"/><rect x="606" y="64" width="40" height="22" fill="none"/><rect x="648" y="64" width="40" height="22" fill="none"/><rect x="690" y="64" width="40" height="22" fill="none"/>
    <rect x="60" y="88" width="40" height="22" fill="none"/><rect x="102" y="88" width="40" height="22" fill="none"/><rect x="144" y="88" width="40" height="22" fill="none"/><rect x="186" y="88" width="40" height="22" fill="none"/><rect x="228" y="88" width="40" height="22" fill="none"/><rect x="270" y="88" width="40" height="22" fill="none"/><rect x="312" y="88" width="40" height="22" fill="none"/><rect x="354" y="88" width="40" height="22" fill="none"/><rect x="396" y="88" width="40" height="22" fill="none"/><rect x="438" y="88" width="40" height="22" fill="none"/><rect x="480" y="88" width="40" height="22" fill="none"/><rect x="522" y="88" width="40" height="22" fill="none"/><rect x="564" y="88" width="40" height="22" fill="none"/><rect x="606" y="88" width="40" height="22" fill="none"/><rect x="648" y="88" width="40" height="22" fill="none"/><rect x="690" y="88" width="40" height="22" fill="none"/>
    <rect x="60" y="112" width="40" height="22" fill="none"/><rect x="102" y="112" width="40" height="22" fill="none"/><rect x="144" y="112" width="40" height="22" fill="none"/><rect x="186" y="112" width="40" height="22" fill="none"/><rect x="228" y="112" width="40" height="22" fill="none"/><rect x="270" y="112" width="40" height="22" fill="none"/><rect x="312" y="112" width="40" height="22" fill="none"/><rect x="354" y="112" width="40" height="22" fill="none"/><rect x="396" y="112" width="40" height="22" fill="none"/><rect x="438" y="112" width="40" height="22" fill="none"/><rect x="480" y="112" width="40" height="22" fill="none"/><rect x="522" y="112" width="40" height="22" fill="none"/><rect x="564" y="112" width="40" height="22" fill="none"/><rect x="606" y="112" width="40" height="22" fill="none"/><rect x="648" y="112" width="40" height="22" class="f-mut"/><rect x="690" y="112" width="40" height="22" fill="none"/>
    <rect x="60" y="136" width="40" height="22" fill="none"/><rect x="102" y="136" width="40" height="22" fill="none"/><rect x="144" y="136" width="40" height="22" fill="none"/><rect x="186" y="136" width="40" height="22" fill="none"/><rect x="228" y="136" width="40" height="22" fill="none"/><rect x="270" y="136" width="40" height="22" fill="none"/><rect x="312" y="136" width="40" height="22" fill="none"/><rect x="354" y="136" width="40" height="22" fill="none"/><rect x="396" y="136" width="40" height="22" fill="none"/><rect x="438" y="136" width="40" height="22" fill="none"/><rect x="480" y="136" width="40" height="22" fill="none"/><rect x="522" y="136" width="40" height="22" fill="none"/><rect x="564" y="136" width="40" height="22" fill="none"/><rect x="606" y="136" width="40" height="22" fill="none"/><rect x="648" y="136" width="40" height="22" fill="none"/><rect x="690" y="136" width="40" height="22" fill="none"/>
    <rect x="60" y="160" width="40" height="22" class="f-warn"/><rect x="102" y="160" width="40" height="22" fill="none"/><rect x="144" y="160" width="40" height="22" fill="none"/><rect x="186" y="160" width="40" height="22" fill="none"/><rect x="228" y="160" width="40" height="22" fill="none"/><rect x="270" y="160" width="40" height="22" fill="none"/><rect x="312" y="160" width="40" height="22" fill="none"/><rect x="354" y="160" width="40" height="22" fill="none"/><rect x="396" y="160" width="40" height="22" fill="none"/><rect x="438" y="160" width="40" height="22" fill="none"/><rect x="480" y="160" width="40" height="22" fill="none"/><rect x="522" y="160" width="40" height="22" fill="none"/><rect x="564" y="160" width="40" height="22" fill="none"/><rect x="606" y="160" width="40" height="22" fill="none"/><rect x="648" y="160" width="40" height="22" class="f-mut"/><rect x="690" y="160" width="40" height="22" fill="none"/>
    <rect x="60" y="184" width="40" height="22" fill="none"/><rect x="102" y="184" width="40" height="22" fill="none"/><rect x="144" y="184" width="40" height="22" fill="none"/><rect x="186" y="184" width="40" height="22" fill="none"/><rect x="228" y="184" width="40" height="22" fill="none"/><rect x="270" y="184" width="40" height="22" fill="none"/><rect x="312" y="184" width="40" height="22" fill="none"/><rect x="354" y="184" width="40" height="22" fill="none"/><rect x="396" y="184" width="40" height="22" fill="none"/><rect x="438" y="184" width="40" height="22" fill="none"/><rect x="480" y="184" width="40" height="22" fill="none"/><rect x="522" y="184" width="40" height="22" fill="none"/><rect x="564" y="184" width="40" height="22" fill="none"/><rect x="606" y="184" width="40" height="22" fill="none"/><rect x="648" y="184" width="40" height="22" class="f-mut"/><rect x="690" y="184" width="40" height="22" fill="none"/>
    <rect x="60" y="208" width="40" height="22" fill="none"/><rect x="102" y="208" width="40" height="22" fill="none"/><rect x="144" y="208" width="40" height="22" fill="none"/><rect x="186" y="208" width="40" height="22" fill="none"/><rect x="228" y="208" width="40" height="22" fill="none"/><rect x="270" y="208" width="40" height="22" fill="none"/><rect x="312" y="208" width="40" height="22" class="f-mut"/><rect x="354" y="208" width="40" height="22" fill="none"/><rect x="396" y="208" width="40" height="22" fill="none"/><rect x="438" y="208" width="40" height="22" fill="none"/><rect x="480" y="208" width="40" height="22" class="f-mut"/><rect x="522" y="208" width="40" height="22" fill="none"/><rect x="564" y="208" width="40" height="22" class="f-mut"/><rect x="606" y="208" width="40" height="22" fill="none"/><rect x="648" y="208" width="40" height="22" class="f-mut"/><rect x="690" y="208" width="40" height="22" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="56">00</text><text x="122" y="56">01</text><text x="164" y="56">02</text><text x="206" y="56">03</text><text x="248" y="56">04</text><text x="290" y="56">05</text><text x="332" y="56">06</text><text x="374" y="56">07</text><text x="416" y="56">08</text><text x="458" y="56">09</text><text x="500" y="56">0A</text><text x="542" y="56">0B</text><text x="584" y="56">0C</text><text x="626" y="56">0D</text><text x="668" y="56">0E</text><text x="710" y="56">0F</text>
    <text x="80" y="80">10</text><text x="122" y="80">11</text><text x="164" y="80">12</text><text x="206" y="80">13</text><text x="248" y="80">14</text><text x="290" y="80">15</text><text x="332" y="80">16</text><text x="374" y="80">17</text><text x="416" y="80">18</text><text x="458" y="80">19</text><text x="500" y="80">1A</text><text x="542" y="80">1B</text><text x="584" y="80">1C</text><text x="626" y="80">1D</text><text x="668" y="80">1E</text><text x="710" y="80">1F</text>
    <text x="80" y="104">20</text><text x="122" y="104">21</text><text x="164" y="104">22</text><text x="206" y="104">23</text><text x="248" y="104">24</text><text x="290" y="104">25</text><text x="332" y="104">26</text><text x="374" y="104">27</text><text x="416" y="104">28</text><text x="458" y="104">29</text><text x="500" y="104">2A</text><text x="542" y="104">2B</text><text x="584" y="104">2C</text><text x="626" y="104">2D</text><text x="668" y="104">2E</text><text x="710" y="104">2F</text>
    <text x="80" y="128">30</text><text x="122" y="128">31</text><text x="164" y="128">32</text><text x="206" y="128">33</text><text x="248" y="128">34</text><text x="290" y="128">35</text><text x="332" y="128">36</text><text x="374" y="128">37</text><text x="416" y="128">38</text><text x="458" y="128">39</text><text x="500" y="128">3A</text><text x="542" y="128">3B</text><text x="584" y="128">3C</text><text x="626" y="128">3D</text><text x="668" y="128">3E</text><text x="710" y="128">3F</text>
    <text x="80" y="152">40</text><text x="122" y="152">41</text><text x="164" y="152">42</text><text x="206" y="152">43</text><text x="248" y="152">44</text><text x="290" y="152">45</text><text x="332" y="152">46</text><text x="374" y="152">47</text><text x="416" y="152">48</text><text x="458" y="152">49</text><text x="500" y="152">4A</text><text x="542" y="152">4B</text><text x="584" y="152">4C</text><text x="626" y="152">4D</text><text x="668" y="152">4E</text><text x="710" y="152">4F</text>
    <text x="80" y="176">50</text><text x="122" y="176">51</text><text x="164" y="176">52</text><text x="206" y="176">53</text><text x="248" y="176">54</text><text x="290" y="176">55</text><text x="332" y="176">56</text><text x="374" y="176">57</text><text x="416" y="176">58</text><text x="458" y="176">59</text><text x="500" y="176">5A</text><text x="542" y="176">5B</text><text x="584" y="176">5C</text><text x="626" y="176">5D</text><text x="668" y="176">5E</text><text x="710" y="176">5F</text>
    <text x="80" y="200">60</text><text x="122" y="200">61</text><text x="164" y="200">62</text><text x="206" y="200">63</text><text x="248" y="200">64</text><text x="290" y="200">65</text><text x="332" y="200">66</text><text x="374" y="200">67</text><text x="416" y="200">68</text><text x="458" y="200">69</text><text x="500" y="200">6A</text><text x="542" y="200">6B</text><text x="584" y="200">6C</text><text x="626" y="200">6D</text><text x="668" y="200">6E</text><text x="710" y="200">6F</text>
    <text x="80" y="224">70</text><text x="122" y="224">71</text><text x="164" y="224">72</text><text x="206" y="224">73</text><text x="248" y="224">74</text><text x="290" y="224">75</text><text x="332" y="224">76</text><text x="374" y="224">77</text><text x="416" y="224">78</text><text x="458" y="224">79</text><text x="500" y="224">7A</text><text x="542" y="224">7B</text><text x="584" y="224">7C</text><text x="626" y="224">7D</text><text x="668" y="224">7E</text><text x="710" y="224">7F</text>
  </g>
  <g stroke="currentColor"><rect x="60" y="244" width="16" height="12" class="f-mut"/><rect x="200" y="244" width="16" height="12" class="f-warn"/><rect x="390" y="244" width="16" height="12" class="f-ok"/></g>
  <g fill="currentColor"><text x="82" y="255">예약 16칸</text><text x="222" y="255">레거시 I2C (0B, 50)</text><text x="412" y="255">I3C (08 imu, 09 mag, 0A baro, 0C tof)</text>
  <text x="10" y="276">0x0B 를 건너뛰어 ToF 가 0x0C 를 받았다 (solution.c C1). 빈 칸 = 테두리만.</text>
  <text x="10" y="294">낮은 주소 = 높은 IBI 우선순위 → 08 imu 가 최우선.</text></g>
</svg>
```

### Part A — DA 바이트와 패리티

컨트롤러는 7비트 DA 뒤에 패리티 비트를 붙여 8비트로 보낸다. 패리티는 **8비트 전체의 1 개수가 홀수**가 되게 한다 (Linux `dw-i3c-master.c` 가 `parity8(addr) ? 0 : BIT(7)` 로 같은 규칙을 DAT 에 쓴다 — 2026-11 소스 확인). 실제 출력:

```text
A4 DA parity: 0x08->0x10 0x09->0x13 0x0A->0x15 0x0B->0x16, bad=0
```

0x08 = 000 1000 은 1 이 한 개(홀수)라 PAR = 0 → 0x10. 0x09 = 000 1001 은 두 개라 PAR = 1 → 0x13. 128개 전수로 `(v >> 1) == da` 와 홀수 패리티를 확인했다. 타깃은 받은 바이트의 패리티가 틀리면 NACK 하고 주소를 갖지 않는다. 1비트 오류는 반드시 잡히고, 2비트 오류는 통과한다(Part B 7).

### Part A — ENTDAA 라운드 루프

```text
ctrl_entdaa:
  ctrl_ccc_begin(ENTDAA)            START 7E/W (IBI 가 이기면 먼저 처리) + CCC 0x07
  loop:
    Sr 7E/R                         OD 9   주소 없는 온라인 타깃이 없으면 → NACK, STOP, return 할당 수
    id = od_arbitrate(64비트)        OD 64  버스에 보인 값
    a  = get_free_addr(c, 8)        없으면 STOP, I3C_ERR_NOADDR
    DA[6:0] + PAR → 이긴 타깃       OD 9   패리티 OK 인 타깃만 da 를 받고 ACK
    ACK  → 표에 {id, a}, slot[a] = I3C
    NACK → 같은 id 가 연속 두 번이면 I3C_ERR_DAA_NACK, 아니면 다음 라운드 (같은 타깃이 또 이긴다)
```

**고정 예제** (타깃 5개 + 레거시 0x0B·0x50, 실제 출력):

```text
A5 ENTDAA log (5 targets, legacy 0x0B/0x50):
  [   18] START 7E/W CCC ENTDAA(0x07)
  [  100]   Sr 7E/R ACK ID 02083f120000 BCR 06 DCR 00 -> DA 0x08 (0x10) ACK
  [  182]   Sr 7E/R ACK ID 02083f120000 BCR 26 DCR 00 -> DA 0x09 (0x13) ACK
  [  264]   Sr 7E/R ACK ID 04a2100a1000 BCR 06 DCR 00 -> DA 0x0A (0x15) ACK
  [  346]   Sr 7E/R ACK ID 04a2100a5000 BCR 06 DCR 00 -> DA 0x0C (0x19) ACK
  [  428]   Sr 7E/R ACK ID 0bee30040000 BCR 02 DCR 00 -> DA 0x0D (0x1A) ACK
  [  437]   Sr 7E/R NACK -> STOP (5 assigned)
```

(대괄호 = 누적 클럭 수 OD + PP.) 읽는 법: ① PID 가 같은 두 장치(`02083f120000`)는 **BCR 에서** 갈렸다(0x06 &lt; 0x26) — PID 가 같아도 BCR/DCR 이 다르면 충돌이 아니다. ② 같은 제조사·부품의 두 인스턴스(`04a2100a1000` / `…5000`)는 PID 의 instance 필드(bit 15:12)에서 갈렸다 — 보드에서 인스턴스 핀을 다르게 묶는 이유(연습 1). ③ 0x0B 는 배터리라 건너뛰었다. ④ 라운드당 82 클럭(Sr 헤더 9 + ID 64 + DA 9), 마지막 NACK 라운드 9 — OD = 9 (START 7E/W) + 5·82 + 9 = 428, PP = 9 (CCC 바이트), 로그 끝 스탬프 437 = 428 + 9. ENTDAA 시간은 거의 전부 OD 클럭이라 **OD 속도가 DAA 시간을 정한다** (연습 3: OD 1 MHz 가정에서 타깃 8개 674.72 µs).

**랜덤** (실제 출력): `A6 random DAA: 1500 buses, 15753 addresses assigned, bad=0` — 타깃 1–20개, 제조사 3종에서 PID 를 뽑아 접두사가 자주 겹치게, 레거시 0–4개를 임의 주소에. 매번 ① 모두 할당 ② 주소 유일 ③ 예약·레거시 칸 없음 ④ 표의 PID = 그 주소 타깃의 PID ⑤ **ID 가 작을수록 주소가 작다** ⑥ 모든 I3C 주소와 레거시 주소를 읽어 값이 맞고 `collisions = contention = 0`.

### Part B — IBI: 타깃이 START 를 연다

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">IBI · NACK · Hot-Join (이 레슨 모델, solution.c 로그와 같은 순서)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="90" height="36" class="f-mut"/><rect x="110" y="40" width="90" height="36" class="f-warn"/>
    <rect x="200" y="40" width="170" height="36" class="f-acc"/><rect x="370" y="40" width="60" height="36" class="f-ok"/>
    <rect x="430" y="40" width="150" height="36" class="f-ok"/><rect x="580" y="40" width="40" height="36" class="f-mut"/>
    <rect x="200" y="120" width="170" height="36" class="f-acc"/><rect x="370" y="120" width="60" height="36" class="f-warn"/>
    <rect x="430" y="120" width="250" height="36" class="f-ok"/><rect x="680" y="120" width="40" height="36" class="f-mut"/>
    <rect x="200" y="200" width="170" height="36" class="f-acc"/><rect x="370" y="200" width="60" height="36" class="f-ok"/>
    <rect x="430" y="200" width="40" height="36" class="f-mut"/><rect x="470" y="200" width="250" height="36" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="65" y="56">버스 idle</text><text x="65" y="70">SDA=SCL=1</text>
    <text x="155" y="56">타깃: SDA↓</text><text x="155" y="70">= START</text>
    <text x="285" y="56">헤더 0001001 + R</text><text x="285" y="70">(0x09/R) OD 중재</text>
    <text x="400" y="63">C: ACK</text>
    <text x="505" y="56">MDB 0x42 + T</text><text x="505" y="70">PP (BCR[2]=1 일 때)</text><text x="600" y="63">P</text>
    <text x="285" y="143">헤더 0x08/R (IBI 원치 않음)</text><text x="400" y="143">C: NACK</text>
    <text x="555" y="136">Sr 7E/W DISEC(0x81) Sr 08/W</text><text x="555" y="150">ev=0x01 (SIR 끔) — 중재 없음</text><text x="700" y="143">P</text>
    <text x="285" y="216">헤더 0000010 + W</text><text x="285" y="230">(0x02/W = Hot-Join)</text><text x="400" y="223">C: ACK</text>
    <text x="450" y="223">P</text><text x="595" y="216">START 7E/W ENTDAA … 새 타깃만 응답</text><text x="595" y="230">→ 다음 빈 주소</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="108">① 정상 IBI (B1, C1)</text><text x="20" y="188">② 원치 않는 IBI (B3)</text><text x="20" y="268">③ Hot-Join (B4, C1)</text>
    <text x="20" y="288">컨트롤러가 같은 START 에 7E/W 를 내도 0xFC 는 어떤 타깃 헤더보다 커서 진다</text>
    <text x="20" y="304">→ IBI·HJ 를 먼저 처리하고, 그 다음 다시 START (B2).</text>
  </g>
</svg>
```

IBI 의 중재는 ENTDAA 와 같은 `od_arbitrate` 를 8비트 헤더에 쓰는 것뿐이다. 헤더 = `(DA << 1) | 1` 이므로 **낮은 DA 가 이긴다**. 진 타깃은 요청을 그대로 들고 있다가 다음 START(버스가 다시 쉴 때)에 또 낸다. 실제 출력 (B1: 네 타깃이 동시에 요청):

```text
B1 four simultaneous IBIs:
  [  373] target START 08/R IBI -> ACK MDB 0xA3 STOP
  [  391] target START 09/R IBI -> ACK MDB 0xA2 STOP
  [  409] target START 0A/R IBI -> ACK MDB 0xA1 STOP
  [  427] target START 0B/R IBI -> ACK MDB 0xA0 STOP
```

IBI 한 건 = OD 9 (헤더 + ACK) + PP 9 (MDB + T) = 18 클럭. 컨트롤러의 결정 규칙 (svc-i3c-master.c 의 IBI ISR 이 같은 판단을 한다 — 2026-11 소스 확인): 표에 있는 장치 + IBI 를 켠 장치면 ACK, BCR 의 IBI_PAYLOAD(bit 2)가 있으면 MDB 를 읽는다. 모르는 주소이거나 IBI 를 안 켰으면 NACK.

**컨트롤러의 START 가 IBI 에 진다 (B2).** 컨트롤러가 GETPID 를 보내려고 START + 7E/W 를 낸 바로 그때 타깃 0x08 이 IBI 를 냈다. 7E/W = 1111 1100, 0x08/R = 0001 0001 → 첫 비트에서 컨트롤러가 진다. 로그 `START (ctrl 7E/W lost) 08/R IBI -> ACK (no MDB) STOP`, `n_preempt = 1`, 그 다음 시도에서 GETPID 가 정상 완료 (`p[0] = 0x22`). 펌웨어 관점: 컨트롤러 드라이버의 "명령 시작" 이 **IBI 처리로 바뀔 수 있다** — 실제 컨트롤러 IP 는 이걸 하드웨어로 하고 IBI 를 큐에 넣는다. 그래서 `ctrl_start` 에 상한(8)을 두었다: IBI 폭주 타깃이 있으면 명령이 영원히 못 나간다(→ `I3C_ERR_PREEMPT`).

**원치 않는 IBI (B3).** 실제 출력:

```text
B3 unwanted IBI:
  [  118] target START 08/R IBI -> NACK, Sr
  [  154] Sr 7E/W CCC DISEC(0x81) Sr 08/W ev=0x01 STOP
```

NACK 직후 STOP 없이 Sr 로 이어 DISEC 를 보냈다 — `ctrl_events_sr` 은 중재를 하지 않는다. 이후 `ev_en` 에서 SIR 이 꺼져 타깃은 `ibi_req` 를 들고 있어도 헤더를 내지 않고, `ctrl_service` 는 0 을 돌려준다. Sr 대신 STOP + 새 START 로 DISEC 를 보내면 무슨 일이 생기는지는 7. 자주 하는 실수 3 (변이 실험에서 실제로 재귀가 끝나지 않아 크래시).

**Hot-Join (B4).** 가장 작은 PID 를 가진 타깃이 늦게 켜졌다. 동시에 0x09 가 IBI 를 걸었다. 실제 출력:

```text
B4 Hot-Join + IBI at the same time:
  [  200] target START 02/W Hot-Join -> ACK STOP (ENTDAA pending)
  [  218] START (ctrl 7E/W lost) 09/R IBI -> ACK MDB 0x42 STOP
  [  236] START 7E/W CCC ENTDAA(0x07)
  [  318]   Sr 7E/R ACK ID 000100000000 BCR 06 DCR 00 -> DA 0x0B (0x16) ACK
  [  327]   Sr 7E/R NACK -> STOP (1 assigned)
```

① 헤더 0x04(0x02/W) vs 0x13(0x09/R) → Hot-Join 이 이긴다 (0x02 는 할당 가능한 어떤 DA 보다 작다). ② 컨트롤러는 ACK 하고 ENTDAA 를 예약한다. ③ ENTDAA 를 시작하는 START 를 이번엔 0x09 의 IBI 가 이긴다 — 먼저 처리. ④ ENTDAA 에는 주소 없는 타깃(새 것)만 응답 → 0x0A(레거시)를 건너뛰어 0x0B. **새로 온 타깃은 PID 가 제일 작아도 남은 주소 중 가장 낮은 것을 받는다** — 주소 순서 = PID 순서는 한 번의 ENTDAA 안에서만 성립한다. HJ 를 허용하지 않는 컨트롤러는 NACK + Sr DISEC(bcast, HJ) — 이후 그 타깃은 주소 없이 조용히 있다 (테스트).

**SETDASA (B5).** 정적 주소(I2C 식)를 가진 타깃에 원하는 동적 주소를 *직접* 준다. 데이터 바이트는 `DA << 1` (Linux `i3c_master_setda_locked()` 의 `setda->addr = newaddr << 1` — 2026-11 소스 확인). 실제 출력:

```text
  [   36] START 7E/W CCC SETDASA(0x87) Sr 30/W ACK 0x40(=0x20<<1) STOP
  [  216] SETDASA Sr 31/W NACK -> STOP
  [  234] START 7E/W CCC ENTDAA(0x07)
  [  316]   Sr 7E/R ACK ID 000200000000 BCR 06 DCR 00 -> DA 0x08 (0x10) ACK
  [  398]   Sr 7E/R ACK ID 000300000000 BCR 06 DCR 00 -> DA 0x09 (0x13) ACK
```

SETDASA 를 받은 타깃은 이미 주소가 있으므로 ENTDAA 에 참가하지 않는다. SETDASA 뒤 컨트롤러는 GETPID/GETBCR/GETDCR 로 PID 를 읽어 표에 넣는다(36 → 216 사이의 클럭) — Linux 의 `i3c_master_retrieve_dev_info()` 가 하는 일. 왜 쓰나: **우선순위를 고정**하려고 (IMU 를 0x08 로 → IBI 최우선) 또는 ENTDAA 를 지원하지 않는 타깃.

### Part B — 고장과 검출

| 주입 | 버스에서 일어난 일 (실제 값) | 컨트롤러가 알 수 있는 것 | 검출 |
|---|---|---|---|
| 64비트 ID 가 같은 두 타깃 (B6) | 둘 다 끝까지 남아 둘 다 DA 0x08 을 받고 ACK (`dup_da = 1`) — ACK 는 wired 라 하나로 보인다 | 장치 2개(3개여야 함) | **개수 대조** → `I3C_ERR_MISSING`. GETPID 는 둘이 *같은 값*을 내므로 통과(contention 0). 그 주소를 읽으면 `0x11 & 0x22 = 0x00`, collisions·contention 발생 |
| DA 바이트 1비트 오류 (B7) | 패리티 틀림 → NACK → 같은 타깃이 다시 이겨 0x08 받음. OD = 9 + 3·82 + 9 | NACK 한 번 | 재시도로 자동 복구. 연속 두 번이면 `I3C_ERR_DAA_NACK` (svc-i3c-master 의 DAA 도 같은 PID 의 두 번째 NACK 에서 -EIO — 2026-11 소스 확인) |
| DA 바이트 2비트 오류 (B7) | 패리티 통과 → 타깃이 엉뚱한 주소 0x0B 를 가짐, 컨트롤러 표는 0x08 | 없음 | verify 의 GETPID(0x08) NACK → `I3C_ERR_NACK` |
| 진 뒤 안 놓는 타깃 (B8) | 버스에 유령 ID `000200000000` (둘 다 아님), 고장 타깃이 0x08 을 받음, 정상 타깃은 다음 라운드 0x09 | 표: 0x08 = 유령 PID | **GETPID 대조** → `verify DA 0x08: GETPID 000a00000000 != DAA 000200000000` → `I3C_ERR_PID_MISMATCH` |
| 레거시 표 무시 (B9) | 4번째 타깃이 0x0B → 배터리와 같이 ACK, 읽은 값 0x00 | 없음 (값이 이상할 뿐) | 관찰자 collisions = 1. 올바른 할당기는 0x0C, 배터리 0x5B |
| 주소 고갈 (B10) | 110개 할당 후 빈 칸 없음 | `I3C_ERR_NOADDR` | 남은 5개 주소 없음 (`entdaa=-3 ndev=110 unaddressed=5`) |

같은 PID 는 규격이 막으려는 상황이다: PID 의 bit 32 가 1 이면 하위 32비트를 *랜덤 값*으로 쓰고, 0 이면 part ID + **instance ID(bit 15:12)** 를 쓴다 — 같은 부품 둘을 한 버스에 두려면 인스턴스 핀을 다르게 묶거나 SETDASA 로 미리 주소를 준다 (Linux `I3C_PID_*` 매크로의 비트 배치, 2026-11 소스 확인). 펌웨어가 할 수 있는 방어는 **보드 표와 개수 대조** 하나뿐이라는 점이 면접 포인트다.

### Part C — 버스 초기화: Linux 와 같은 순서

`i3c_bus_init()` 은 Linux `i3c_master_bus_init()` 의 순서를 줄인 것이다 (2026-11 소스에서 확인: RSTDAA broadcast → DISEC(SIR|MR|HJ) broadcast → 정적 주소 + `assigned-address` 가 있는 장치에 SETDASA → (SETAASA) → `i3c_master_do_daa()` (컨트롤러 드라이버의 `do_daa` 훅 — DAA 자체는 대개 컨트롤러 하드웨어가 돌린다) → 새 장치마다 GETPID/GETBCR/GETDCR). 이 레슨의 `ctrl_entdaa()` 는 그 하드웨어가 하는 일을 펼친 것이다. 실제 브링업 로그:

```text
C1 bring-up log (i3c_bus_init, init=0):
  [   18] RSTDAA bcast STOP
  [   45] START 7E/W CCC DISEC(0x01) ev=0x09 STOP
  [   81] START 7E/W CCC SETDASA(0x87) Sr 68/W ACK 0x10(=0x08<<1) STOP
  [  252] START 7E/W CCC ENTDAA(0x07)
  [  334]   Sr 7E/R ACK ID 02083f120000 BCR 06 DCR 00 -> DA 0x09 (0x13) ACK
  [  416]   Sr 7E/R ACK ID 0bee30040000 BCR 02 DCR 00 -> DA 0x0A (0x15) ACK
  [  425]   Sr 7E/R NACK -> STOP (2 assigned)
  [  578] verify DA 0x08: PID 04a2100a5000 BCR 07 DCR c4 OK
  [  731] verify DA 0x09: PID 02083f120000 BCR 06 DCR 00 OK
  [  884] verify DA 0x0A: PID 0bee30040000 BCR 02 DCR 00 OK
  [  920] START 7E/W CCC ENEC(0x80) Sr 08/W ev=0x01 STOP
  [  956] START 7E/W CCC ENEC(0x80) Sr 09/W ev=0x01 STOP
  [  983] START 7E/W CCC ENEC(0x00) ev=0x08 STOP
C1 run: hot-join + IBIs + legacy reads (2 requests):
  [  992] target START 02/W Hot-Join -> ACK STOP (ENTDAA pending)
  [ 1010] START (ctrl 7E/W lost) 08/R IBI -> ACK MDB 0x01 STOP
  [ 1028] START (ctrl 7E/W lost) 09/R IBI -> ACK MDB 0x02 STOP
  [ 1046] START 7E/W CCC ENTDAA(0x07)
  [ 1128]   Sr 7E/R ACK ID 02083f230000 BCR 06 DCR 00 -> DA 0x0C (0x19) ACK
  [ 1137]   Sr 7E/R NACK -> STOP (1 assigned)
  [ 1164] START 7E/W Sr 50/R ACK data 0xEE STOP
  [ 1191] START 7E/W Sr 0B/R ACK data 0x5B STOP
C1 clocks: OD 642, PP 549
```

브링업 엔지니어가 읽을 포인트:

| 줄 | 의미 | 이상하면 의심할 것 |
|---|---|---|
| RSTDAA | 이전 부팅의 동적 주소를 지운다 (MCU 만 리셋되고 센서는 전원 유지된 경우) | 생략하면 타깃이 옛 주소를 들고 ENTDAA 에 안 나옴 → "장치가 사라짐" |
| DISEC ev=0x09 | 초기화 도중 IBI·Hot-Join 금지 (SIR 0x01 + HJ 0x08) | 생략하면 초기화 CCC 가 IBI 에 계속 밀림 (B2 현상) |
| SETDASA 68 → 08 | IMU 를 최우선 주소에 고정 | NACK 이면 정적 주소·전원·풀업 확인 |
| ENTDAA 2개 | 나머지 둘. 0x08 은 이미 썼으므로 0x09 부터 | 보드 표보다 적으면 MISSING (B6) |
| verify ×3 | DAA 때 읽은 ID 와 GETPID/BCR/DCR 대조 | 다르면 PID_MISMATCH (B8) |
| ENEC 08, 09 / ENEC HJ | 원하는 장치만 IBI 허용, 그 다음 Hot-Join 허용 | baro 는 SIR 을 안 켰으므로 요청을 걸어도 헤더를 못 낸다 (테스트) |
| 운용 | HJ → IBI 2개(ENTDAA 의 START 를 두 번 이김) → ToF 0x0C (0x0B 레거시 건너뜀) → 레거시 읽기 정상 | collisions 0, 레거시 ACK 수 = 읽은 수 |

**소크** (실제 출력): `C2 soak: 40 boards x 400 steps, 2137 IBIs served, 78 hot-joins, 3923 I3C reads, bad=0`. 보드마다 타깃 3–14개 중 1/4 은 늦게 켜지고, 레거시 0–3개. 매 스텝 주소 불변식, 끝에 ① 건 IBI = ACK 처리 + NACK ② 남은 요청 0 ③ 온라인인데 주소 없는 타깃 0 ④ collisions = contention = 0 ⑤ 레거시가 ACK 한 횟수 = 레거시를 읽은 횟수 (레거시는 I3C 트래픽에 한 번도 반응하지 않았다).

### Part C — 우선순위는 주소로 고정된다 (demo_ibi.c)

IBI 는 비선점이고, 버스가 비면 대기 중인 요청끼리 중재한다 → 가장 낮은 DA 는 최대 "진행 중인 IBI 한 건" 만 기다리고, 높은 DA 는 부하가 높을수록 굶는다. 타깃 8개(DA 0x08–0x0F)가 같은 도착률로 IBI 를 내고, 한 건 비용 = 헤더 9 OD + MDB 9 PP + START/STOP 몫 1 OD (가정), OD 1 MHz·PP 12.5 MHz (가정) → 10.72 µs. 2초 시뮬레이션, 실제 출력:

| ρ | DA | 처리 | 평균 [µs] | p99 [µs] | 최대 [µs] | 합쳐짐 |
|---|---|---|---|---|---|---|
| 0.20 | 0x08 | 4759 | 1.1 | 10.2 | 10.7 | 9 |
| 0.20 | 0x0B | 4637 | 1.3 | 15.4 | 31.0 | 14 |
| 0.20 | 0x0F | 4545 | 1.6 | 20.7 | 49.9 | 19 |
| 0.50 | 0x08 | 11595 | 2.6 | 10.4 | 10.7 | 172 |
| 0.50 | 0x0B | 11400 | 3.7 | 27.9 | 72.7 | 251 |
| 0.50 | 0x0F | 11228 | 6.8 | 60.7 | 138.0 | 448 |
| 0.80 | 0x08 | 18186 | 4.0 | 10.6 | 10.7 | 667 |
| 0.80 | 0x0B | 17444 | 7.2 | 42.1 | 88.0 | 1169 |
| 0.80 | 0x0F | 15507 | 20.9 | 154.4 | 393.9 | 3050 |
| 0.95 | 0x08 | 21094 | 4.5 | 10.6 | 10.7 | 985 |
| 0.95 | 0x0B | 19980 | 9.1 | 51.3 | 135.2 | 1894 |
| 0.95 | 0x0F | 16169 | 33.4 | 238.3 | 554.8 | 6133 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">IBI p99 지연 [µs] — demo_ibi.c 실측 (막대 = p99 × 2 px)</text>
  <g fill="currentColor"><text x="10" y="58">ρ = 0.50</text><text x="10" y="118">ρ = 0.80</text><text x="10" y="178">ρ = 0.95</text></g>
  <g stroke="currentColor">
    <rect x="130" y="36" width="20.8" height="13" class="f-ok"/><rect x="130" y="51" width="55.8" height="13" class="f-acc"/><rect x="130" y="66" width="121.4" height="13" class="f-warn"/>
    <rect x="130" y="96" width="21.2" height="13" class="f-ok"/><rect x="130" y="111" width="84.2" height="13" class="f-acc"/><rect x="130" y="126" width="308.8" height="13" class="f-warn"/>
    <rect x="130" y="156" width="21.2" height="13" class="f-ok"/><rect x="130" y="171" width="102.6" height="13" class="f-acc"/><rect x="130" y="186" width="476.6" height="13" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="156" y="47">10.4</text><text x="191" y="62">27.9</text><text x="257" y="77">60.7</text>
    <text x="157" y="107">10.6</text><text x="220" y="122">42.1</text><text x="444" y="137">154.4</text>
    <text x="157" y="167">10.6</text><text x="238" y="182">51.3</text><text x="612" y="197">238.3</text>
  </g>
  <g stroke="currentColor"><rect x="130" y="214" width="14" height="10" class="f-ok"/><rect x="250" y="214" width="14" height="10" class="f-acc"/><rect x="370" y="214" width="14" height="10" class="f-warn"/></g>
  <g fill="currentColor"><text x="150" y="224">DA 0x08</text><text x="270" y="224">DA 0x0B</text><text x="390" y="224">DA 0x0F</text>
  <text x="10" y="244">0x08 의 최대 지연은 부하와 무관하게 IBI 한 건(10.72 µs) — 비선점이라 진행 중인 것만 기다린다.</text></g>
</svg>
```

읽는 법: ρ 0.95 에서 0x0F 의 p99 는 0x08 의 22배이고, "합쳐짐"(대기 중에 새 이벤트가 와서 하나로 합쳐진 것)이 6133 — 그 타깃이 이벤트를 FIFO 에 쌓지 않는 센서라면 **데이터를 잃는다**. 대응: ① 지연이 중요한 장치(IMU, 모터 엔코더)를 SETDASA 나 SETNEWDA 로 낮은 주소에(연습 2) ② 덜 중요한 장치는 IBI 대신 폴링 ③ IBI 를 FIFO 워터마크로 묶어 빈도 줄이기 ④ 총 IBI 부하 예산을 계산 (COMM-M02 의 I2C 굶주림, COMM-M05 의 CAN ID 우선순위와 같은 구조).

### 모델이 빼먹은 것

| 실제 | 이 모델 | 어디서 |
|---|---|---|
| OD/PP 전환 타이밍, t<sub>CAS</sub>·t<sub>CBP</sub>, Bus Available/Idle 조건 시간 | 트랜잭션 단위, START 는 "버스가 비면" | 스펙 타이밍 표 |
| T 비트: 쓰기 = 패리티, 읽기 = end-of-data (타깃이 끝냄 / 컨트롤러가 중단) | PP 바이트당 9 클럭으로만 셈 | Q5 |
| 컨트롤러 역할 요청(DA/W 헤더), 2차 컨트롤러, ENTAS·GETSTATUS·SETMWL 등 CCC | 없음 (DA/W 헤더는 NACK) | — |
| HDR 모드(DDR 등), I3C v1.1 의 그룹 주소·타깃 리셋 | 없음 | — |
| 레거시 장치의 50 ns 스파이크 필터, 혼합 버스의 OD 속도 제한 | "자기 주소에만 반응" 으로 추상화 | Q4 |
| 실제 IBI 이후 private read 로 데이터 읽기 | MDB 1바이트만 | Q6 |
| 푸시풀 충돌 = 전기적 단락 (중간 전압, 과전류) | AND + contention 카운터 | B6 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 64/64 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -O2 -Wall -Wextra demo_ibi.c -o demo_ibi -lm && ./demo_ibi             # 5. 풀이의 IBI 지연 표 (약 0.1 초)
```

## 7. 자주 하는 실수

**1. 중재 비트를 계산하면서 바로 참가자를 뺀다.** 루프 안에서 "AND 해 가다가 0 이 나오면 1 을 낸 참가자를 뺀다" 를 한 번에 하면, 0 을 낸 참가자가 배열 뒤쪽에 있을 때 앞쪽 참가자는 아직 레벨 1 을 보고 남는다. 고친 코드: 비트마다 ① 레벨을 끝까지 구하고 ② 그 다음 1 을 낸 참가자를 뺀다 (`od_arbitrate` 의 두 루프). 변이 실험: 비참가자도 AND 에 넣는 실수(`if (in[k])` 빠뜨림)는 64개 중 29개 테스트를 깨뜨린다.

**2. 패리티를 짝수로 붙인다.** "패리티 = XOR" 로 외우면 짝수 패리티가 된다. ENTDAA 의 PAR 는 8비트 전체를 **홀수**로 만든다 — 짝수로 보내면 모든 타깃이 NACK 하고 DAA 가 0개를 찾는다 (변이 실험: 64개 중 38개 FAIL). 고친 코드: `(da << 1) | (pop8(da) % 2 == 0)`.

**3. IBI 를 NACK 한 뒤 STOP 하고 새 START 로 DISEC 를 보낸다.** 그 타깃은 아직 요청을 들고 있으므로 새 START 의 헤더 중재에서 또 이기고, 또 NACK, 또 DISEC 시도… 끝나지 않는다. 변이 실험에서 `ctrl_events_sr` 을 `ctrl_events`(START 경로)로 바꾸자 재귀가 끝나지 않아 **스택 오버플로로 크래시**했다. 실제 버스에서는 IBI 폭풍(livelock)이다. 고친 코드: NACK 바로 뒤 Sr → 7E/W → DISEC (Sr 뒤는 중재가 없다).

**4. 예약 주소를 0x00–0x07 과 0x7E 만 막는다.** 0x7E 의 1비트 이웃(0x3E 0x5E 0x6E 0x76 0x7A 0x7C 0x7F)에 타깃을 두면, 브로드캐스트 헤더의 1비트 오류가 그 타깃 하나에게만 가는 명령이 된다. 타깃이 100개 넘는 버스가 아니어도 랜덤 테스트(레거시가 위쪽을 막는 경우 등)에서 걸린다 — 변이 실험 7개 FAIL. 고친 코드: `slot[0x7E ^ (1 << i)] = SLOT_RSVD` (i = 0..6).

**5. 레거시 I2C 주소를 주소 표에 넣지 않는다.** 할당기는 "빈 칸" 만 보므로 0x0B 배터리와 같은 주소를 I3C 타깃에게 준다 → 두 장치가 같이 ACK, 데이터가 섞인다(B9: 배터리 읽기 0x5B → 0x00). 펌웨어에서는 오류가 아니라 **이상한 값**으로만 보인다. 고친 코드: `addr_slots_init(c, i2c_sa, ni2c)` 로 레거시 칸을 I2C 로 표시 (Linux 는 DT 의 i2c 노드에서 읽는다).

**6. GETPID 대조만 하고 개수 대조를 안 한다.** 같은 PID 두 개는 GETPID 도 같은 값을 내서 통과한다(B6, contention 0). "보드에 장치 몇 개가 있어야 하나" 를 아는 것은 보드 표뿐이다. 변이 실험: 개수 검사를 지우면 B6 이 FAIL. 고친 코드: `if (rc == I3C_OK && c->ndev < n_expected) rc = I3C_ERR_MISSING;`

**7. 초기화 중에 IBI·Hot-Join 을 열어 둔다.** 이전 부팅에서 IBI 를 켜 둔 타깃이 초기화 CCC 의 START 를 계속 이겨, RSTDAA·SETDASA 가 밀린다(B2 를 반복하는 상황). 고친 코드: RSTDAA 직후 DISEC(SIR|HJ) broadcast, 마지막에 원하는 장치에만 ENEC — Linux 와 같은 순서.

## 8. 면접 Q&A

<details><summary>Q1. I3C 는 I2C 와 무엇이 같고 무엇이 다른가요?</summary>

같은 것: 2선(SCL/SDA), 7비트 주소 + R/W 헤더, 오픈드레인 wired-AND 중재, ACK 비트, START/Sr/STOP. 다른 것: ① 데이터는 **푸시풀**(SDR 최대 12.5 MHz, 확인 권장) — 풀업 RC 로 올라가는 I2C 보다 빠르고 전력이 적다 ② 주소를 런타임에 **동적 할당**(ENTDAA, SETDASA) ③ 인터럽트 선 대신 **IBI** ④ 9번째 비트가 데이터에서는 ACK 가 아니라 T 비트(쓰기 패리티 / 읽기 end-of-data) ⑤ CCC(공통 명령)로 이벤트 허용·주소 리셋·정보 조회 ⑥ Hot-Join. 꼬리: "클럭 스트레칭은?" — I3C 에는 클럭 스트레칭이 없다고 알고 있다(타깃이 SCL 을 잡지 않음; 확인 권장) — COMM-M01 의 문제 자체가 사라진다.

</details>

<details><summary>Q2. ENTDAA 에서 왜 가장 작은 PID 가 가장 작은 주소를 받나요? 그게 좋은 건가요?</summary>

64비트 {PID, BCR, DCR} 를 MSB 부터 오픈드레인으로 내면 처음 다른 비트에서 0 을 낸 쪽이 남는다 = 수가 작은 쪽. 컨트롤러가 가장 낮은 빈 주소부터 주면 ID 순서 = 주소 순서 (이 레슨 랜덤 1,500 버스에서 확인). 좋은 점은 결정적이라는 것(같은 보드면 늘 같은 주소). 나쁜 점은 **IBI 우선순위가 제조사 ID 로 정해진다**는 것 — 중요한 IMU 가 제조사 ID 가 커서 낮은 우선순위를 받을 수 있다. 그래서 SETDASA(초기화 전 고정)나 SETNEWDA(나중에 변경), Linux DT 의 `assigned-address` 를 쓴다. Linux 는 DAA 후 원하는 주소가 비어 있으면 SETNEWDA 로 옮긴다 (`__i3c_master_add_i3c_dev_locked`, 2026-11 소스 확인).

</details>

<details><summary>Q3. 같은 PID 를 가진 장치 두 개가 있으면 어떻게 되나요? 어떻게 막고, 어떻게 찾나요?</summary>

BCR/DCR 까지 같으면 둘 다 끝까지 남아 같은 DA 를 받고 둘 다 ACK — ACK 는 wired 라 컨트롤러는 하나로 본다. 이후 그 주소로 읽으면 두 장치가 푸시풀로 동시에 구동 → 다른 비트에서 단락(이 모델에서 contention). GETPID 는 둘이 같은 값을 내므로 **못 잡는다**. 찾는 법: 보드 표(DT)의 장치 수와 DAA 결과 수 대조 (B6). 막는 법: PID bit 32 = 1(하위 32비트 랜덤) 부품을 쓰거나, instance ID 핀을 다르게 스트랩, 또는 정적 주소가 있으면 SETDASA 로 먼저 주소를 준다(그러면 ENTDAA 에 안 나온다).

</details>

<details><summary>Q4. 레거시 I2C 장치와 I3C 를 한 버스에 두려면 무엇을 지켜야 하나요?</summary>

① 컨트롤러가 레거시 정적 주소를 알아야 한다(Linux DT 의 i2c 노드 + LVR) — 동적 주소가 그 칸을 피한다 (B9 는 안 피했을 때). ② 레거시 장치는 Fm/Fm+ 의 **50 ns 스파이크 필터**가 있어야 한다 — I3C 푸시풀 SCL 의 짧은 high 펄스를 무시하게 (UM10204 의 t<sub>SP</sub>, I3C 스펙의 레거시 요구 — 확인 권장). 필터 없는 장치는 I3C 트래픽을 주소로 오해할 수 있다. ③ 레거시 장치는 클럭 스트레칭을 하면 안 된다(I3C 컨트롤러는 SCL 을 읽어 기다리지 않는다 — 확인 권장). ④ 레거시가 섞이면 OD 구간 속도를 I2C 속도에 맞춰야 할 수 있다 → ENTDAA·IBI 헤더가 느려진다 (연습 3 의 시간 표가 OD 속도에 비례).

</details>

<details><summary>Q5. T 비트는 무엇인가요? ACK 와 무엇이 다른가요?</summary>

SDR 데이터 바이트의 9번째 비트. 컨트롤러가 쓰는 바이트에서는 **패리티**(이 레슨의 CCC 바이트 PP 9 클럭) — 타깃은 ACK 하지 않고 패리티로 오류를 검출한다. 타깃이 읽어 주는 바이트에서는 **end-of-data**: 타깃이 "더 있음 / 끝" 을 표시하고, 컨트롤러는 그 비트 자리에서 읽기를 중단시킬 수 있다(I2C 의 마스터 NACK 역할). 주소 헤더의 9번째는 여전히 ACK/NACK(OD) 이다. (세부 전기적 핸드오프는 스펙 확인 권장.) 꼬리: "그럼 데이터 바이트 단위 흐름 제어는?" — 바이트마다 ACK 가 없으므로 SETMWL/SETMRL(최대 길이) 같은 CCC 와 프로토콜 수준에서 한다.

</details>

<details><summary>Q6. IBI 를 받은 뒤 펌웨어는 무엇을 하나요? MDB 는 무엇인가요?</summary>

BCR[2](IBI payload)가 1 인 타깃은 IBI 가 ACK 되면 최소 1바이트 MDB(Mandatory Data Byte)를 이어서 보낸다 — "무슨 이벤트인지" 를 담는다(예: data-ready, FIFO 워터마크). 컨트롤러 IP 는 보통 IBI 를 {주소, MDB, 페이로드} 로 하드웨어 큐에 넣고 인터럽트를 올린다. 드라이버는 ISR 에서 큐를 비우고, 실제 데이터는 태스크에서 private read 로 읽는다 (Linux: 장치 드라이버가 `i3c_device_request_ibi()` 로 핸들러를 등록, IBI 슬롯 풀 — 확인 권장). 원하지 않는 IBI 는 NACK + DISEC. IBI 큐가 넘치면(이 모델 `q_drop`) 이벤트를 잃으므로 큐 깊이를 최악 버스트로 잡는다.

</details>

<details><summary>Q7. 컨트롤러가 명령을 보내려는데 IBI 가 계속 이기면 어떻게 되나요?</summary>

7E/W(0xFC)는 어떤 타깃 헤더보다 크므로 START 마다 대기 중인 IBI 가 이긴다(B2, C1 에서 ENTDAA 의 START 가 두 번 짐). 정상이라면 IBI 를 처리할 때마다 요청이 줄어 결국 컨트롤러가 이긴다. 문제는 IBI 를 쉬지 않고 내는 타깃 — 컨트롤러 명령이 영원히 밀린다. 대책: ① 이 모델처럼 시도 상한 → `I3C_ERR_PREEMPT` 로 올리고 ② 원인 타깃을 DISEC(NACK + Sr 경로로) ③ 초기화·중요 명령 구간 전에 DISEC broadcast 로 IBI 를 막는다. 꼬리: "Hot-Join 이 계속 오면?" — HJ 를 DISEC(bcast, HJ) 로 끈다 (B4 의 비허용 경로).

</details>

<details><summary>Q8. 버스 초기화 순서는? 왜 RSTDAA 를 먼저 하나요?</summary>

주소 표(예약 + 레거시) → RSTDAA → DISEC(SIR|HJ) → SETDASA(정적 주소 장치) → ENTDAA → 각 장치 GETPID/BCR/DCR → 필요한 장치에 ENEC(SIR) → Hot-Join 허용. RSTDAA 이유: MCU 만 리셋(워치독, 디버거)되고 센서는 전원이 유지되면 타깃은 **옛 동적 주소를 갖고 있다** — 주소가 있는 타깃은 ENTDAA 에 참가하지 않으므로 컨트롤러는 "장치가 없다" 고 결론 낸다. 주소를 지우고 처음부터 할당해야 상태가 결정적이다. Linux `i3c_master_bus_init()` 도 RSTDAA broadcast 로 시작한다 (2026-11 소스 확인).

</details>

## 9. 추가 연습

### 연습 1 — PID 필드 해부와 인스턴스 핀

`pid_decode()` / `pid_make_fixed()` 를 짜라: [47:33] 제조사 ID(15비트), [32] 랜덤 PID 여부, [31:16] part ID, [15:12] instance ID, [11:0] 추가 정보 (Linux `I3C_PID_*` 매크로와 같은 배치). 같은 부품 4개를 instance 3, 2, 1, 0 으로 만들어 ENTDAA 결과 주소 순서를 예측하고 확인하라.

<details><summary>답 보기</summary>

실제 출력 `ex1: PID 04a2100a5000 -> manuf 0x0251 rnd 0 part 0x100A inst 5 extra 0x000`. 0x04A2100A5000 >> 33 = 0x04A2 >> 1 = 0x0251 이고 bit 32 = 0x04A2 의 bit 0 = 0 → 고정 PID (이 레슨의 PID 값들은 가상의 예). 무작위 5,000개 왕복 OK. 같은 부품 4개는 instance 필드에서만 다르므로 instance 0 → 0x08, 1 → 0x09, 2 → 0x0A, 3 → 0x0B. 즉 **보드의 인스턴스 스트랩이 IBI 우선순위를 정한다** — 회로도 리뷰에서 볼 것. 랜덤 PID(bit 32 = 1)면 하위 32비트가 부품마다 다르므로 스트랩이 없어도 충돌 확률은 낮지만, 그 대신 부팅마다/부품마다 주소 순서가 예측 불가다.

</details>

### 연습 2 — SETNEWDA 로 IMU 를 최우선 주소로

`ctrl_setnewda(c, old, new)` (direct CCC 0x88, 데이터 `new << 1`)를 짜고, DAA 결과 0x0C 에 있는 IMU 와 0x08 의 장치를 **맞바꿔라**. 그 뒤 다섯 장치가 동시에 IBI 를 내면 IMU 가 먼저 처리되는지 확인하라.

<details><summary>답 보기</summary>

맞바꾸기는 빈 칸 하나를 임시로 쓰는 3단계다 (실제 출력):

```text
ex2: swap imu 0x0C <-> 0x08:
  [  473] START 7E/W CCC SETNEWDA(0x88) Sr 0C/W 0x1A(=0x0D<<1) STOP
  [  509] START 7E/W CCC SETNEWDA(0x88) Sr 08/W 0x18(=0x0C<<1) STOP
  [  545] START 7E/W CCC SETNEWDA(0x88) Sr 0D/W 0x10(=0x08<<1) STOP
```

새 주소가 비어 있지 않으면(`0x08 → 0x09`) 거부 — 이걸 안 하면 Part B 9 의 충돌을 컨트롤러가 직접 만든다. 표의 `Dev` 항목은 주소와 함께 움직이므로 `ibi_on` 도 따라간다. 결과: IBI 큐 첫 항목 = 0x08 의 MDB 4(IMU). Linux 는 DT `assigned-address` 가 비어 있으면 DAA 후 SETNEWDA 로 옮긴다(Q2).

</details>

### 연습 3 — ENTDAA 클럭 수 닫힌 식

N 개 타깃의 ENTDAA OD/PP 클럭 수를 식으로 쓰고, N = 0..32 시뮬레이션과 대조하라. NACK 재시도 한 번은 몇 클럭을 더하나? OD 1 MHz, PP 12.5 MHz 라고 가정하면 시간은?

<details><summary>답 보기</summary>

OD = 9 (START 7E/W) + 82·N (라운드: Sr 7E/R 9 + ID 64 + DA 9) + 9 (마지막 NACK 라운드), PP = 9 (CCC). N = 0 이면 7E/W 가 NACK 이라 OD 9, PP 0. NACK 재시도는 라운드 하나 = 82. 33개 N 모두 일치, 재시도 1번 = 9 + 82·4 + 9 (CHECK). 실제 출력:

```text
ex3: N  OD clk  PP clk  time @OD 1 MHz/PP 12.5 MHz (가정)
      1     100       9   100.72 us
      4     346       9   346.72 us
      8     674       9   674.72 us
     16    1330       9  1330.72 us
     32    2642       9  2642.72 us
```

거의 전부 OD 시간이다 — 레거시 때문에 OD 를 400 kHz 로 낮추면 2.5배. 부팅 시간 예산(예: 로봇의 센서 버스 수십 개)에서 DAA 는 ms 단위로 잡는다. (실제 OD·PP 속도와 START/STOP·버스 free 시간은 컨트롤러 설정과 스펙 타이밍을 볼 것.)

</details>

### 연습 4 — 주소 용량

레거시 주소 목록이 주어질 때 할당 가능한 동적 주소 수의 식을 쓰고, 무작위 레거시 집합 300개로 `addr_slots_init` 과 대조하라. 용량 + 3 개의 타깃으로 ENTDAA 를 돌리면?

<details><summary>답 보기</summary>

빈 칸 = 128 − 16(예약) − |예약이 아닌 서로 다른 레거시 주소|. 중복 레거시, 예약 칸에 있는 레거시(예: 0x7C)는 빼지 않는다. 300개 모두 일치, 고갈 실험 20번 모두 "정확히 용량만큼 할당 + `I3C_ERR_NOADDR` + 3개 주소 없음 + 불변식 유지" (실제 출력 `ex4: ... bad=0/0`). 실제로 100개 넘는 타깃을 한 버스에 두는 일은 드물지만(정전 용량·속도), 이 경계가 할당기의 off-by-one 과 예약 칸 처리를 잡는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- I3C = I2C 의 오픈드레인 중재 + 푸시풀 데이터 + **동적 주소** + **IBI**. 중재 규칙은 하나: 1 을 냈는데 0 을 읽으면 빠진다 → MSB-first 로 **작은 값이 이긴다**.
- ENTDAA 라운드 = Sr 7E/R → 64비트 {PID, BCR, DCR} 중재 → DA[6:0] + **홀수 패리티** → ACK. 아무도 ACK 안 하면 STOP. 라운드당 OD 82 클럭 → DAA 시간은 OD 속도가 정한다.
- 주소 표: 예약 0x00–0x07, 0x7E, 0x7E 의 1비트 이웃 7개(0x3E 0x5E 0x6E 0x76 0x7A 0x7C 0x7F) + 레거시 I2C 정적 주소. 레거시를 빠뜨리면 두 장치가 같이 ACK.
- IBI 헤더 = (DA&lt;&lt;1)|1 → **낮은 DA = 높은 우선순위**, Hot-Join 0x02/W 가 그보다 앞. 7E/W 는 늘 진다 → 컨트롤러 명령이 IBI 처리로 바뀐다. 원치 않는 IBI 는 NACK 후 **Sr** DISEC.
- 같은 PID 는 GETPID 로 못 잡는다 → 보드 표 개수 대조. 진 뒤 안 놓는 타깃은 유령 ID → GETPID 대조. 1비트 DA 오류 → 패리티 NACK 재시도.
- 초기화: RSTDAA → DISEC(SIR|HJ) → SETDASA → ENTDAA → GETPID/BCR/DCR → ENEC(SIR) 개별 → HJ 허용 (Linux 순서).
- 우선순위가 주소로 고정 → 부하가 높으면 높은 주소가 굶는다 (ρ 0.95: p99 10.6 vs 238.3 µs, 실측). 중요한 장치는 SETDASA/SETNEWDA 로 낮은 주소에.

## 참고자료

- MIPI Alliance, *MIPI I3C Basic Specification* v1.1.1 (공개 다운로드 가능 — 확인 권장): ENTDAA·SETDASA·SETNEWDA, IBI·Hot-Join, BCR/DCR, 예약 주소, T 비트, 레거시 I2C 요구(스파이크 필터): <https://www.mipi.org/specifications/i3c-sensor-specification>
- Linux kernel, `drivers/i3c/master.c` — `i3c_bus_init_addrslots()` (0–7, 0x7E, `0x7E ^ BIT(i)` 예약), `i3c_master_bus_init()` (RSTDAA → DISEC → SETDASA → SETAASA → do_daa), `__i3c_master_add_i3c_dev_locked()` (GETPID/BCR/DCR, SETNEWDA), `i3c_master_setda_locked()` (`newaddr << 1`) — 2026-11 확인: <https://github.com/torvalds/linux/blob/master/drivers/i3c/master.c>
- Linux kernel, `include/linux/i3c/ccc.h` (CCC 번호, `I3C_CCC_EVENT_SIR/MR/HJ`) — 2026-11 확인: <https://github.com/torvalds/linux/blob/master/include/linux/i3c/ccc.h>
- Linux kernel, `include/linux/i3c/device.h` (`I3C_PID_*`, `I3C_BCR_*` 비트 배치) — 2026-11 확인: <https://github.com/torvalds/linux/blob/master/include/linux/i3c/device.h>
- Linux kernel, `drivers/i3c/master/svc-i3c-master.c` (DAA 의 같은 PID 두 번째 NACK → -EIO, IBI ACK/NACK 판단) 와 `dw-i3c-master.c` (`parity8(addr) ? 0 : BIT(7)`) — 2026-11 확인: <https://github.com/torvalds/linux/tree/master/drivers/i3c/master>
- Linux kernel 문서, *I3C subsystem* (프로토콜 개요, 장치 드라이버 API, IBI): <https://docs.kernel.org/driver-api/i3c/index.html>
- NXP, *UM10204 I2C-bus specification* — Fm/Fm+ 50 ns 스파이크 억제 t<sub>SP</sub> (확인 권장): <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>
- 관련 레슨: COMM-M02 (I2C 멀티마스터 중재 — 같은 wired-AND 규칙, 주소 고정 우선순위의 굶주림), COMM-M01 (클럭 스트레칭 — I3C 에는 없음), COMM-M03 (I2C 버스 복구), COMM-M05 (CAN ID 중재), COMM-M12 (bus_ops 드라이버 계층), COMM-B06 (I2C 비트 모델), HWLAB-B03 (풀업·rise time). 다음 단계: CAM-B05 (MIPI CSI-2 — 카메라 데이터 경로, 제어는 I2C/I3C)
