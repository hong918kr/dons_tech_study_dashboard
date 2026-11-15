---
id: COMM-B06
time: 60분
updated: 2026-11-11
---
# COMM-B06 I2C 비트 수준: START/STOP, 주소+R/W, ACK/NACK

## 학습 목표

- 오픈드레인(open-drain) + 풀업(pull-up) 버스가 왜 **wired-AND** 인지 설명하고, "놓음 = 1, 당김 = 0" 드라이버들의 AND 로 선 레벨을 계산하는 시뮬레이터를 짠다.
- START(SCL=1 동안 SDA↓) / STOP(SCL=1 동안 SDA↑) 과 **"데이터 SDA 는 SCL=0 일 때만 바뀐다"** 규칙을 마스터(비트뱅)와 슬레이브(엣지 검출 상태 머신) 양쪽에서 구현한다.
- 주소 바이트(7비트 주소 + R/W)와 9번째 클럭의 ACK/NACK 을 파형에서 읽는다 — **9번째 클럭에 SDA 를 당기는 쪽은 받는 쪽**이다.
- 드라이버 API 에서 **주소 NACK / 데이터 NACK / 버스 busy / 인자 오류**를 구분해 돌려주고, 어떤 경우에도 STOP 으로 버스를 놓는다.
- 규칙을 어기는 마스터(SCL 을 먼저 올리고 SDA 를 바꿈)를 주입해 슬레이브가 **가짜 START/STOP** 을 보는 것을 재현하고, 왜 0x00/0xFF 패턴 테스트로는 안 보이는지 말한다.

## 1. 문제

브링업 첫 주의 단골 버그 보고는 "센서가 안 읽혀요(NACK)" 다. 스코프·로직 애널라이저 파형에서 START·주소·ACK 을 바로 읽고, NACK 이 *주소*에서 났는지 *데이터*에서 났는지 가르고, GPIO 로 I2C 를 비트뱅(bit-bang)할 줄 아는지는 펌웨어 면접의 기본 체크 항목이다.

I2C 는 선 두 가닥이다: **SDA**(데이터)와 **SCL**(클럭). 둘 다 **오픈드레인**이라 칩은 선을 LOW 로 *당기기*만 하고, HIGH 는 풀업 저항 Rp 가 만든다 (Rp 값과 rise time 은 HWLAB-B03). 그래서 선 레벨 = 모든 노드 출력의 AND 다. 아무도 안 당기면 1, 한 명이라도 당기면 0. 이 성질 덕분에 여러 디바이스가 한 선을 공유해도 쇼트가 나지 않고, 슬레이브가 ACK 로 SDA 를 당기거나 클럭 스트레칭(COMM-M01)으로 SCL 을 붙잡을 수 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">오픈드레인 버스 = wired-AND: 누구든 당기면 0, 아무도 안 당기면 풀업이 1</text>
  <g stroke="currentColor" fill="none">
    <line x1="40" y1="45" x2="160" y2="45"/>
    <line x1="80" y1="45" x2="80" y2="55"/><line x1="80" y1="85" x2="80" y2="110"/>
    <line x1="130" y1="45" x2="130" y2="55"/><line x1="130" y1="85" x2="130" y2="150"/>
    <line x1="80" y1="110" x2="700" y2="110"/>
    <line x1="130" y1="150" x2="700" y2="150"/>
  </g>
  <rect x="73" y="55" width="14" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="123" y="55" width="14" height="30" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="10" y="49">VDD</text><text x="92" y="74">Rp</text><text x="142" y="74">Rp</text>
    <text x="705" y="114">SDA</text><text x="705" y="154">SCL</text>
    <circle cx="80" cy="110" r="3"/><circle cx="130" cy="150" r="3"/>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="150" y="195" width="160" height="90" rx="6"/>
    <rect x="340" y="195" width="160" height="90" rx="6"/>
    <rect x="530" y="195" width="160" height="90" rx="6"/>
  </g>
  <rect x="380" y="200" width="44" height="45" class="f-ok" opacity=".45"/>
  <g stroke="currentColor">
    <line x1="190" y1="110" x2="190" y2="215"/><line x1="190" y1="235" x2="190" y2="250"/><line x1="190" y1="235" x2="202" y2="219"/>
    <line x1="260" y1="150" x2="260" y2="215"/><line x1="260" y1="235" x2="260" y2="250"/><line x1="260" y1="235" x2="272" y2="219"/>
    <line x1="380" y1="110" x2="380" y2="215"/><line x1="380" y1="235" x2="380" y2="250"/><line x1="380" y1="215" x2="380" y2="235" stroke-width="3"/>
    <line x1="450" y1="150" x2="450" y2="215"/><line x1="450" y1="235" x2="450" y2="250"/><line x1="450" y1="235" x2="462" y2="219"/>
    <line x1="570" y1="110" x2="570" y2="215"/><line x1="570" y1="235" x2="570" y2="250"/><line x1="570" y1="235" x2="582" y2="219"/>
    <line x1="640" y1="150" x2="640" y2="215"/><line x1="640" y1="235" x2="640" y2="250"/><line x1="640" y1="235" x2="652" y2="219"/>
    <line x1="182" y1="250" x2="198" y2="250"/><line x1="252" y1="250" x2="268" y2="250"/>
    <line x1="372" y1="250" x2="388" y2="250"/><line x1="442" y1="250" x2="458" y2="250"/>
    <line x1="562" y1="250" x2="578" y2="250"/><line x1="632" y1="250" x2="648" y2="250"/>
  </g>
  <g fill="currentColor">
    <circle cx="190" cy="110" r="3"/><circle cx="260" cy="150" r="3"/><circle cx="380" cy="110" r="3"/>
    <circle cx="450" cy="150" r="3"/><circle cx="570" cy="110" r="3"/><circle cx="640" cy="150" r="3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="206" y="232">놓음</text><text x="276" y="232">놓음</text>
    <text x="388" y="232">당김</text><text x="466" y="232">놓음</text>
    <text x="586" y="232">놓음</text><text x="656" y="232">놓음</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="230" y="276">마스터 (MCU)</text><text x="420" y="276">슬레이브 0x50</text><text x="610" y="276">슬레이브 0x68</text>
  </g>
  <text x="10" y="305" fill="currentColor">9번째 클럭(SCL=1) 순간: SDA = 1(마스터) · 0(0x50 의 ACK) · 1(0x68) = 0 → ACK,  SCL = 1 · 1 · 1 = 1</text>
  <text x="10" y="324" fill="currentColor">FET 를 켜면 선이 GND 로. "1 을 내는" 동작은 없다 — 놓으면 Rp 가 올린다 (STM32: OTYPER=1)</text>
</svg>
```

I2C 한 트랜잭션의 규칙 (NXP UM10204 의 요지):

| 규칙 | 내용 |
|---|---|
| idle | SDA = SCL = 1 (아무도 안 당김) |
| START (S) | SCL = 1 인 동안 SDA 1→0. 이 뒤로 버스는 busy |
| STOP (P) | SCL = 1 인 동안 SDA 0→1. 이 뒤로 버스는 free |
| 데이터 비트 | **SDA 는 SCL = 0 일 때만 바뀐다**, SCL = 1 동안 안정 → 받는 쪽이 이때 읽는다. (그래서 SCL=1 동안의 SDA 변화는 "데이터"가 아니라 "조건" = S 또는 P) |
| 바이트 | 8비트 **MSB first** + 9번째 클럭. 9번째 클럭에 송신 측은 SDA 를 놓고, **수신 측이 당기면 ACK(0), 놓아 두면 NACK(1)** |
| 첫 바이트 | START 직후 = 7비트 주소 &lt;&lt; 1 \| R/W (0 = 마스터가 쓰기, 1 = 마스터가 읽기) |

아래가 이 레슨 코드가 실제로 만든 파형이다: 주소 0x50 + W (= 0xA0) 를 보내고 9번째 클럭을 읽은 뒤 STOP (Linux `i2cdetect` 가 하는 "주소만 찔러 보기"와 같은 모양). 위는 0x50 이 있을 때, 아래는 없을 때.

```svg
<svg viewBox="0 0 740 295" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">START · 0x50+W (1010000 0) · 9번째 클럭 · STOP — 33틱 트레이스</text>
  <rect x="594" y="114" width="57" height="37" class="f-ok" opacity=".5"/>
  <rect x="613" y="174" width="57" height="37" class="f-warn" opacity=".5"/>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".45">
    <line x1="176" y1="88" x2="176" y2="210"/><line x1="233" y1="88" x2="233" y2="210"/>
    <line x1="290" y1="88" x2="290" y2="210"/><line x1="347" y1="88" x2="347" y2="210"/>
    <line x1="404" y1="88" x2="404" y2="210"/><line x1="461" y1="88" x2="461" y2="210"/>
    <line x1="518" y1="88" x2="518" y2="210"/><line x1="575" y1="88" x2="575" y2="210"/>
    <line x1="632" y1="88" x2="632" y2="210"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="100,60 138,60 138,85 176,85 176,60 195,60 195,85 233,85 233,60 252,60 252,85 290,85 290,60 309,60 309,85 347,85 347,60 366,60 366,85 404,85 404,60 423,60 423,85 461,85 461,60 480,60 480,85 518,85 518,60 537,60 537,85 575,85 575,60 594,60 594,85 632,85 632,60 651,60 651,85 689,85 689,60 727,60"/>
    <polyline points="100,120 119,120 119,145 157,145 157,120 214,120 214,145 271,145 271,120 328,120 328,145 651,145 651,120 670,120 670,145 708,145 708,120 727,120"/>
    <polyline points="100,180 119,180 119,205 157,205 157,180 214,180 214,205 271,205 271,180 328,180 328,205 613,205 613,180 670,180 670,205 708,205 708,180 727,180"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="77">SCL</text>
    <text x="10" y="130">SDA</text><text x="10" y="146" font-size="12">0x50 있음</text>
    <text x="10" y="190">SDA</text><text x="10" y="206" font-size="12">장치 없음</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="185" y="46">A6</text><text x="242" y="46">A5</text><text x="299" y="46">A4</text><text x="356" y="46">A3</text>
    <text x="413" y="46">A2</text><text x="470" y="46">A1</text><text x="527" y="46">A0</text><text x="584" y="46">R/W</text>
    <text x="641" y="46">9번째</text>
    <text x="185" y="106">1</text><text x="242" y="106">0</text><text x="299" y="106">1</text><text x="356" y="106">0</text>
    <text x="413" y="106">0</text><text x="470" y="106">0</text><text x="527" y="106">0</text><text x="584" y="106">0=W</text>
    <text x="119" y="228">START</text><text x="708" y="228">STOP</text>
    <text x="622" y="166">ACK: 슬레이브가 당김</text>
    <text x="590" y="228">NACK: 아무도 안 당김</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="252">START = SCL=1 동안 SDA↓, STOP = SCL=1 동안 SDA↑. 데이터 SDA 는 SCL=0 일 때만 바뀐다.</text>
    <text x="10" y="270">점선 = SCL↑ = 받는 쪽 샘플 시점. 한 칸 = 모델 1틱, 비트 = 3틱 (SDA 설정 → SCL↑ → SCL↓).</text>
    <text x="10" y="288">출처: solution.c 의 실제 트레이스 (WAVE_SCL / WAVE_SDA_ACK / WAVE_SDA_NAK 와 테스트로 대조).</text>
  </g>
</svg>
```

**모델 (주어짐).** 시간은 이산 틱(tick)이다. 마스터는 한 번에 선 하나만 바꾸고(`set_sda`/`set_scl`), 바꿀 때마다 `bus_tick()` 이 한 번 돈다: ① 드라이버들의 AND 로 레벨 계산 → ② 모든 슬레이브가 **같은 스냅샷**을 보고 반응(자기 SDA 드라이버만 바꿈) → ③ 다시 AND → ④ 트레이스에 (SCL, SDA) 기록. 실제 타이밍(µs)은 없고 **순서**만 있다 — I2C 의 정확성은 순서 규칙이고, 시간 규격(tSU;DAT 등)은 5. 풀이 표로 따로 본다.

### Part A — wired-AND 와 비트뱅 마스터

- `bus_resolve(b)` — 노드 0..nnodes−1 의 `sda_drv[]`, `scl_drv[]` AND 로 `b->sda`, `b->scl` 계산.
- `m_start`, `m_stop`, `m_write_bit`, `m_read_bit` — 각각 위 규칙대로 `set_sda`/`set_scl` 을 부른다. 비트 하나 = 정확히 3틱 (SDA 정하기 → SCL↑ → SCL↓). 읽을 때는 **SDA 를 놓고**(1) SCL=1 에서 `b->sda` 를 읽는다.
- `m_write_byte(b, byte)` → MSB first 8비트 + 9번째 클럭 읽기, ACK 면 `true`. `m_read_byte(b, ack)` → 8비트 읽기 + 9번째 클럭에 ACK(0)/NACK(1) 쓰기.
- 테스트: 3노드 × 놓음/당김 전수, 슬레이브 없는 버스에서 위 그림의 NACK 파형과 **틱 단위로 일치**, 아무도 안 내는 바이트 읽기 = 0xFF.

### Part B — 슬레이브 상태 머신과 규칙 위반 주입

`slave_on_tick(s, b)` 를 매 틱 부른다. 슬레이브는 자기가 지난 틱에 본 `prev_scl/prev_sda` 와 지금 레벨을 비교해:

- SCL 이 1→1 인데 SDA 가 바뀜 → ↓ 면 **START**(어느 상태에서든 주소 받기 시작), ↑ 면 **STOP**(IDLE).
- SCL ↑ → 받는 중이면 SDA 를 시프트 레지스터에 넣는다(샘플).
- SCL ↓ → **이때만** 자기 SDA 를 바꾼다: 주소 일치 시 ACK, 데이터 저장 시 ACK, 송신 중 다음 비트, 9번째 클럭 뒤 놓기.

주입할 고장·경계:

1. **주소 NACK** — 아무도 그 주소가 아님. 다른 슬레이브(0x51)는 마지막 주소 비트에서 갈려 조용해야 한다.
2. **데이터 NACK** — 슬레이브 수신 버퍼(`rx_cap`)가 차면 그 바이트를 NACK.
3. **N 바이트 쓰기 / 읽기** — 256개 값 전수, 읽기에서 마스터 ACK·ACK·NACK.
4. **규칙 위반** — 하네스의 `m_write_bit_bad()` 는 SCL 을 먼저 올리고 SDA 를 바꾼다. 0xA5 를 이렇게 보내면 슬레이브가 가짜 START 3번·가짜 STOP 3번을 보고 데이터를 잃어야 한다. 0xFF 는 비트가 안 바뀌어 **버그가 숨는다**.

### Part C — 드라이버 API

```c
enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3, I2C_ERR_NACK_DATA = -4 };
int i2c_write(Bus *b, uint8_t addr7, const uint8_t *data, size_t n, size_t *n_acked);
int i2c_read (Bus *b, uint8_t addr7, uint8_t *buf, size_t n);   /* 마지막 바이트만 NACK */
int i2c_scan (Bus *b, uint8_t *found, size_t cap);               /* 0x08..0x77, 반환 = 찾은 총 개수 */
```

- START 전에 **버스가 idle(SDA=SCL=1)인지** 확인 → 아니면 클럭을 하나도 내지 않고 `I2C_ERR_BUS_BUSY` (SDA stuck-low 복구는 COMM-M03).
- 주소 NACK → `NACK_ADDR`, 데이터 NACK → `NACK_DATA` + `*n_acked` = ACK 받은 바이트 수. **어느 에러든 START 를 냈으면 STOP 으로 끝낸다.**
- `addr7 > 0x7F` (데이터시트의 8비트 주소 0xA0 을 그대로 넣는 실수), `n == 0` 읽기 → `I2C_ERR_ARG`.
- 테스트: 슬레이브 0x50 + 0x68 + 고장 노드 하나가 붙은 버스에서 write/read/scan, 고장 노드가 SDA 를 붙잡은 busy, 고정 시드 랜덤 400 트랜잭션(없는 0x3C 포함) 결과 대조 + "SCL=1 중 SDA 변화 횟수 = 트랜잭션 × 2(START+STOP)" 불변식.

## 2. Problem (English)

> **Bit-banged I2C on a simulated open-drain bus.**
>
> You are given a discrete-time bus model: every node has an SDA and an SCL driver (`1` = released, `0` = pulling low); the master changes one line per tick via `set_sda()` / `set_scl()`, and each tick every attached slave's `slave_on_tick()` sees the same resolved line levels.
>
> 1. Implement `bus_resolve()` (wired-AND with pull-ups) and the master primitives `m_start`, `m_stop`, `m_write_bit`, `m_read_bit`, `bool m_write_byte(Bus*, uint8_t)` (returns true on ACK) and `uint8_t m_read_byte(Bus*, bool ack)`. SDA may change only while SCL is low, except to form START/STOP. Bytes are MSB first; the 9th clock carries ACK (receiver pulls SDA low) or NACK.
> 2. Implement a slave state machine that detects START (SDA falls while SCL high) and STOP (SDA rises while SCL high) in any state, samples on SCL rising edges, changes SDA only after SCL falling edges, ACKs its 7-bit address, ACKs received bytes while its buffer has room (NACK otherwise), and transmits bytes on reads until the master NACKs.
> 3. Implement `int i2c_write(Bus*, uint8_t addr7, const uint8_t *data, size_t n, size_t *n_acked)`, `int i2c_read(Bus*, uint8_t addr7, uint8_t *buf, size_t n)` and `int i2c_scan(Bus*, uint8_t *found, size_t cap)`. Distinguish address NACK, data NACK, bus busy (SDA or SCL low before START) and bad arguments. Always terminate a started transaction with STOP. No dynamic allocation.
>
> Example: probing address `0x50` (write) with the device present produces, per tick (first column = idle),
> `SCL = 1 10 010 010 010 010 010 010 010 010 010 011`
> `SDA = 1 00 111 000 111 000 000 000 000 000 001 001`
> and with no device the 9th clock group reads `111` (NACK) instead of `001`.
>
> Follow-ups: What does the slave see if the master raises SCL before setting SDA? Why must the master NACK the last byte it reads? What do you check first when every address NACKs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열한 군데: Part A 7개(`bus_resolve` + 마스터 6개), Part B 1개(`slave_on_tick` — 가장 큰 함수), Part C 3개. 버스 틱, `set_sda/set_scl`, 트레이스 출력, 일부러 틀린 `m_write_bit_bad()`, xorshift32 는 주어진다. 스텁은 아무 선도 안 움직이므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:234  ok == 8
FAIL starter.c:243  strcmp(b.tr_scl, WAVE_SCL) == 0
…
FAIL starter.c:352  i2c_scan(&b, found, 8) == 2 && found[0] == 0x50 && found[1] == 0x68
…
random: 139 writes, 149 reads, 112 to absent 0x3C, mismatches=400, SDA moves while SCL high=0
FAIL starter.c:391  bad == 0
FAIL starter.c:392  b.sda_moves_scl_high == 400 * 2
9/47 passed
```

(exit 1. 통과한 9개는 스텁이 우연히 맞는 경우 — 예: "NACK 이어야 한다"(`m_write_byte` 스텁이 false), "끝나고 버스가 idle".)

## 4. 힌트

<details><summary>힌트 1 — wired-AND 는 1 에서 시작하는 AND</summary>

`uint8_t sda = 1;` (풀업) 에서 시작해 모든 노드에 대해 `sda &= sda_drv[i]`. 한 노드라도 0 이면 0. SCL 도 똑같이. "1 을 강하게 내는" 드라이버는 이 모델에 없다 — 실제 칩에도 없어야 한다 (7. 실수 5).

</details>

<details><summary>힌트 2 — 마스터 함수는 "선을 바꾸는 순서" 그 자체</summary>

- START: SDA 0 → SCL 0 (SCL=1 인 동안 SDA 가 떨어지는 게 핵심)
- 비트 쓰기: SDA = b → SCL 1 → SCL 0
- 비트 읽기: SDA 1(놓기) → SCL 1 → `b->sda` 읽기 → SCL 0
- STOP: SDA 0 → SCL 1 → SDA 1

STOP 첫 단계의 "SDA 0" 은 SCL=0 일 때 하니까 데이터 변화일 뿐 조건이 아니다. 비트 하나 = 3틱으로 맞춰야 그림·테스트의 파형과 일치한다.

</details>

<details><summary>힌트 3 — 슬레이브는 엣지 세 종류만 본다</summary>

`(prev_scl, scl)` 이 `(1,1)` 이면서 SDA 가 바뀌었으면 START/STOP, `(0,1)` 이면 샘플, `(1,0)` 이면 SDA 를 바꿀 차례. 마지막에 `prev_* = 지금` 을 잊지 말 것. START/STOP 검사를 **맨 앞**에 두고 어느 상태에서든 받아 준다 (주소 바이트 도중의 START 도).

</details>

<details><summary>힌트 4 — 비트 카운터는 상승 엣지 수</summary>

`nbits` = 이번 바이트에서 본 SCL↑ 수. 8번째 SCL↑ 에서 바이트가 다 들어오고, **바로 다음 SCL↓** 에서 ACK 를 낸다(SDA=0). 9번째 SCL↑ 에서 마스터가 그 0 을 읽고, 9번째 SCL↓ 에서 놓는다. 송신(읽기 응답)은 반대로: SCL↓ 마다 다음 비트를 내고, 8번째 뒤 SCL↓ 에서 놓아 마스터에게 9번째를 넘긴다.

</details>

<details><summary>힌트 5 — 드라이버는 "검사 → START → … → 항상 STOP"</summary>

인자 검사(주소 ≤ 0x7F, 버퍼) → idle 검사(`b->sda && b->scl`) → `m_start` → 주소 바이트 (NACK 이면 `m_stop` 후 `NACK_ADDR`) → 데이터 (NACK 이면 멈춤) → `m_stop`. 읽기는 `m_read_byte(b, i + 1 < n)` — 마지막만 NACK.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 7비트 주소인가(10비트는?), 클럭 스트레칭·멀티마스터 지원 필요한가(이번엔 없음 → COMM-M01/M02), 에러를 어떻게 구분해 올리나, 바이트 순서는 MSB first |
| 2. 물리 계층 한 줄 | 오픈드레인 + 풀업 = wired-AND. "1 을 쓴다" = 놓는다 |
| 3. 프로토콜 규칙 | idle 둘 다 1 / S·P 는 SCL=1 중 SDA 변화 / 데이터는 SCL=0 에서만 변화 / 9번째 클럭 = 수신 측 ACK |
| 4. 코드 | 마스터 프리미티브 4개 → 바이트 2개 → 슬레이브 상태 머신 → 드라이버 |
| 5. 테스트 | 파형 틱 단위 비교, 주소/데이터 NACK, 두 슬레이브 주소 구분, 256 값 전수, 규칙 위반 주입, 랜덤 + 불변식 |
| 6. 실제와 차이 | 타이밍 규격, rise time(HWLAB-B03), 스파이크 필터, 스트레칭, 버스 복구 |

### Part A — wired-AND

```c
uint8_t sda = 1, scl = 1;
for (int i = 0; i < b->nnodes; i++) { sda &= b->sda_drv[i]; scl &= b->scl_drv[i]; }
```

테스트는 3노드의 SDA 드라이버를 `m` 의 비트(0..7)로, SCL 드라이버를 `~m` 의 비트로 놓고 8가지를 다 본다: SDA 는 `m == 7`(전부 놓음)일 때만 1, SCL 은 `m == 0` 일 때만 1.

### Part A — 마스터 프리미티브: 틱 단위로 무엇이 일어나나

| 함수 | 틱 순서 | 틱 수 | 조건/데이터 |
|---|---|---|---|
| `m_start` | SDA 0 → SCL 0 | 2 | 첫 틱이 SCL=1 중 SDA↓ = **S** |
| `m_write_bit(b)` | SDA b → SCL 1 → SCL 0 | 3 | SDA 는 SCL=0 일 때 바뀜, SCL↑ 에서 상대가 샘플 |
| `m_read_bit` | SDA 1 → SCL 1 (읽기) → SCL 0 | 3 | 놓아 두어야 상대가 0 을 낼 수 있다 |
| `m_write_byte` | 비트 8개 + `m_read_bit` | 27 | 반환 = (9번째 SDA == 0) |
| `m_read_byte(ack)` | `m_read_bit` 8개 + `m_write_bit(!ack)` | 27 | 9번째에 마스터가 ACK/NACK |
| `m_stop` | SDA 0 → SCL 1 → SDA 1 | 3 | 마지막 틱이 SCL=1 중 SDA↑ = **P** |

그래서 "주소만 찔러 보기" = 2 + 27 + 3 = **32틱**, 데이터 n 바이트 쓰기·읽기 = 2 + 27(n+1) + 3 (연습 3 에서 모델로 확인).

`m_start` 에는 한 줄이 더 있다: SCL=0 에서 불리면(트랜잭션 도중) 먼저 SDA·SCL 을 올린 다음 SDA 를 떨어뜨린다. 이것이 repeated START(Sr) 로, 레지스터 읽기(쓰기로 레지스터 번호 → Sr → 읽기)에 쓰인다 — COMM-B07 의 주제다. 이 레슨의 테스트는 Sr 을 쓰지 않는다.

### Part B — 슬레이브 상태 머신

```svg
<svg viewBox="0 0 740 440" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b06arr" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">슬레이브 상태 머신 (slave_on_tick) — 어느 상태에서든 START → ADDR, STOP → IDLE</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="110" height="34" rx="8" class="f-mut"/>
    <rect x="220" y="40" width="110" height="34" rx="8" class="f-acc"/>
    <rect x="440" y="40" width="110" height="34" rx="8" class="f-ok"/>
    <rect x="440" y="140" width="110" height="34" rx="8" class="f-acc"/>
    <rect x="620" y="140" width="110" height="34" rx="8" class="f-ok"/>
    <rect x="440" y="240" width="110" height="34" rx="8" class="f-warn"/>
    <rect x="620" y="240" width="110" height="34" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="62">IDLE</text><text x="275" y="62">ADDR</text><text x="495" y="62">ADDR_ACK</text>
    <text x="495" y="162">RX</text><text x="675" y="162">RX_ACK</text>
    <text x="495" y="262">TX</text><text x="675" y="262">TX_ACK</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b06arr)">
    <line x1="130" y1="57" x2="218" y2="57"/>
    <line x1="330" y1="57" x2="438" y2="57"/>
    <path d="M275,74 V110 H95 V76"/>
    <line x1="495" y1="74" x2="495" y2="138"/>
    <path d="M440,66 H400 V257 H438"/>
    <line x1="550" y1="150" x2="618" y2="150"/>
    <line x1="620" y1="166" x2="552" y2="166"/>
    <line x1="550" y1="250" x2="618" y2="250"/>
    <line x1="620" y1="266" x2="552" y2="266"/>
    <path d="M675,274 V305 H60 V76"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="174" y="50">①</text><text x="384" y="50">②</text><text x="185" y="104">③</text>
    <text x="508" y="110">④</text><text x="388" y="170">⑤</text>
    <text x="584" y="144">⑥</text><text x="584" y="184">⑦</text>
    <text x="584" y="244">⑧</text><text x="584" y="284">⑨</text><text x="360" y="299">⑩</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="335">① START → nbits = 0, sh = 0</text>
    <text x="20" y="355">② 8번째 SCL↓, 주소 일치 → SDA = 0 (ACK), rw 저장</text>
    <text x="20" y="375">③ 8번째 SCL↓, 주소 불일치 → 다음 START 까지 무시</text>
    <text x="20" y="395">④ 9번째 SCL↓, W → SDA 놓기</text>
    <text x="20" y="415">⑤ 9번째 SCL↓, R → tx 바이트의 MSB 를 바로 낸다</text>
    <text x="380" y="335">⑥ 8번째 SCL↓ → 자리 있으면 저장 + ACK, 꽉 차면 NACK</text>
    <text x="380" y="355">⑦ 9번째 SCL↓ → 놓기, 다음 바이트 받기</text>
    <text x="380" y="375">⑧ SCL↓ 마다 다음 비트, 8번째 뒤 SCL↓ 에 놓기</text>
    <text x="380" y="395">⑨ 9번째 SCL↑ 에 마스터 ACK 를 봤으면 다음 MSB</text>
    <text x="380" y="415">⑩ 마스터 NACK → 놓기 → IDLE (STOP 대기)</text>
  </g>
</svg>
```

핵심은 **두 엣지의 역할 분담**이다. SCL↑ 은 "읽는 순간", SCL↓ 은 "바꾸는 순간". 슬레이브가 SCL↓ 에서만 SDA 를 바꾸면 자기 변화가 START/STOP 으로 오인될 일이 없다 (SCL 이 0 이니까). 반대로 마스터가 SCL=1 일 때 SDA 를 바꾸면 슬레이브는 그것을 **정의상** START 나 STOP 으로 읽을 수밖에 없다 — 프로토콜에 "SCL=1 중 데이터 변화"라는 의미는 없기 때문이다.

**주소 바이트 끝부분 트레이스** (0x50 프로브, 0x50 과 0x51 두 슬레이브, solution.c 실제 동작):

| 틱 | 마스터 동작 | SCL | SDA | 슬레이브 0x50 | 슬레이브 0x51 |
|---|---|---|---|---|---|
| 21–23 | A0 = 0 쓰기 | 0→1→0 | 0 | sh = 101000**0**, nbits = 7 | 같음 (자기 주소는 A0 = 1 이지만 아직 비교 전) |
| 24 | SDA = 0 (R/W = W) | 0 | 0 | — | — |
| 25 | SCL ↑ | 1 | 0 | 샘플: sh = 0xA0, nbits = 8 | 같음 |
| 26 | SCL ↓ | 0 | **0** | 0x50 == 0x50 → **SDA 당김(ACK)**, ADDR_ACK | 0x50 ≠ 0x51 → IDLE |
| 27 | SDA 놓기 (읽을 준비) | 0 | 0 | (계속 당김) | — |
| 28 | SCL ↑, SDA 읽기 = 0 | 1 | 0 | — | — |
| 29 | SCL ↓ | 0 | **1** | W → 놓기, RX | — |
| 30–32 | STOP: SDA 0 → SCL 1 → SDA 1 | 0,1,1 | 0,0,1 | 32틱에 STOP → IDLE | STOP 카운트 |

틱 26 이 "SCL↓ 에서만 바꾼다"의 실제 모습이다: SCL 이 떨어진 바로 그 틱에 슬레이브가 SDA 를 잡는다. 틱 27 에서 마스터가 SDA 를 "놓아도" 레벨은 0 — 이것이 wired-AND 이고, 마스터가 push-pull 로 1 을 *몰았다면* 여기서 쇼트(전류 다툼)가 났을 것이다.

**읽기 방향**도 같은 구조다: 주소 + R 이 ACK 되면 9번째 SCL↓(⑤)에서 슬레이브가 첫 바이트의 MSB 를 낸다. 마스터는 `m_read_bit` 로 SDA 를 놓고 SCL↑ 에서 읽는다. 8비트 뒤 슬레이브가 놓으면 마스터가 9번째에 ACK(더 줘)/NACK(그만)를 낸다. 마스터가 마지막 바이트에 NACK 을 하지 않으면 슬레이브는 다음 바이트 MSB 를 계속 몰고 있어 STOP 을 못 만든다 — 연습 4 에서 재현한다.

**전수·랜덤 결과** (실제 출력): 256개 값 쓰기 + 256개 값 읽기 = 512 트랜잭션 전부 일치, "SCL=1 중 SDA 변화" = 1024 = 512 × 2. 즉 정상 마스터는 START·STOP 외에는 SCL=1 일 때 SDA 를 한 번도 건드리지 않는다.

### Part B — 규칙 위반 주입: SCL 을 먼저 올리는 마스터

```c
static void m_write_bit_bad(Bus *b, int bit) { set_scl(b, 1); set_sda(b, bit); set_scl(b, 0); }
```

주소 0x50 이 ACK 된 직후(SDA = 1, 슬레이브 RX 상태)에 0xA5 = 1010 0101 을 이 함수로 보내면:

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">잘못된 비트뱅: SCL↑ 다음에 SDA 를 바꿈 — 0xA5 를 보낼 때 (틱 29–59)</text>
  <g stroke="currentColor" stroke-dasharray="3 3" opacity=".6">
    <line x1="190" y1="55" x2="190" y2="160"/><line x1="250" y1="55" x2="250" y2="160"/>
    <line x1="310" y1="55" x2="310" y2="160"/><line x1="430" y1="55" x2="430" y2="160"/>
    <line x1="490" y1="55" x2="490" y2="160"/><line x1="550" y1="55" x2="550" y2="160"/>
    <line x1="690" y1="55" x2="690" y2="160"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="90,85 110,85 110,60 150,60 150,85 170,85 170,60 210,60 210,85 230,85 230,60 270,60 270,85 290,85 290,60 330,60 330,85 350,85 350,60 390,60 390,85 410,85 410,60 450,60 450,85 470,85 470,60 510,60 510,85 530,85 530,60 570,60 570,85 610,85 610,60 630,60 630,85 670,85 670,60 710,60"/>
    <polyline points="90,125 190,125 190,150 250,150 250,125 310,125 310,150 430,150 430,125 490,125 490,150 550,150 550,125 650,125 650,150 690,150 690,125 710,125"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="48" font-size="12">보낼 비트</text>
    <text x="10" y="77">SCL</text><text x="10" y="142">SDA</text>
    <text x="10" y="108" font-size="12">샘플된 값</text>
    <text x="10" y="180" font-size="12">슬레이브가 본 것</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="140" y="48">1</text><text x="200" y="48">0</text><text x="260" y="48">1</text><text x="320" y="48">0</text>
    <text x="380" y="48">0</text><text x="440" y="48">1</text><text x="500" y="48">0</text><text x="560" y="48">1</text>
    <text x="620" y="48" font-size="12">9번째</text><text x="680" y="48" font-size="12">STOP</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="110" y="108">1</text><text x="170" y="108">1</text><text x="230" y="108">0</text><text x="290" y="108">1</text>
    <text x="350" y="108">0</text><text x="410" y="108">0</text><text x="470" y="108">1</text><text x="530" y="108">0</text>
    <text x="610" y="108">1=NACK</text>
  </g>
  <rect x="180" y="166" width="380" height="20" class="f-warn" opacity=".3"/>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="190" y="180">S</text><text x="250" y="180">P</text><text x="310" y="180">S</text><text x="430" y="180">P</text>
    <text x="490" y="180">S</text><text x="550" y="180">P</text><text x="690" y="180">P</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="210">비트가 1→0 으로 바뀔 때마다 가짜 START, 0→1 일 때마다 가짜 STOP (표시 구간 6개). 맨 오른쪽 P 만 진짜 STOP.</text>
    <text x="10" y="228">샘플은 한 비트씩 늦다 (1 1 0 1 0 0 1 0 = 이전 비트) — S/P 검출이 없어도 데이터는 이미 틀렸다.</text>
    <text x="10" y="246">출처: solution.c 실제 트레이스. 슬레이브 카운터 starts +4, stops +4, rx 0 바이트, 9번째 클럭 NACK.</text>
  </g>
</svg>
```

| 비트 | 보낼 값 | SCL↑ 에서 샘플된 값 | SCL=1 중 SDA 변화 | 슬레이브 상태 |
|---|---|---|---|---|
| b7 | 1 | 1 (이전 SDA = 1) | 없음 | RX, nbits 1 |
| b6 | 0 | 1 | 1→0 = **START** | ADDR, nbits 0 |
| b5 | 1 | 0 | 0→1 = **STOP** | IDLE |
| b4 | 0 | 1 | 1→0 = **START** | ADDR |
| b3 | 0 | 0 | 없음 | ADDR, nbits 1 |
| b2 | 1 | 0 | 0→1 = **STOP** | IDLE |
| b1 | 0 | 1 | 1→0 = **START** | ADDR |
| b0 | 1 | 0 | 0→1 = **STOP** | IDLE |
| 9번째 | (읽기) | 1 = NACK | 없음 | IDLE — 아무도 안 당김 |

테스트가 확인하는 숫자: 슬레이브 `starts` +4 (진짜 1 + 가짜 3), `stops` +4 (가짜 3 + 진짜 1), 트레이스 전체의 "SCL=1 중 SDA 변화" = 8, `rx_len` = 0, 마스터는 데이터 NACK. 마스터 입장에서는 그냥 "디바이스가 NACK 했다" 로만 보인다 — 원인이 자기 비트뱅 순서라는 걸 알려면 **로직 애널라이저로 SCL 과 SDA 의 엣지 순서**를 봐야 한다 (연습 1 디코더가 `S A0 A S P S P S P P` 를 찍는다).

그리고 0xFF 를 보내면 SDA 가 한 번도 안 바뀌니 가짜 조건도, 샘플 밀림도 안 생겨 **정상 동작**한다 (테스트로 확인). 0x00 은? 주소 ACK 뒤 SDA = 1 이었으므로 첫 비트에서 1→0 = 가짜 START 가 난다. 그래서 비트뱅 검증 패턴은 0x00/0xFF 가 아니라 **0xA5/0x5A 처럼 비트가 자주 바뀌는 값**과 전수 256개여야 한다.

### Part C — 드라이버: 에러를 구분하고, 언제나 STOP

| 상황 | 반환 | 버스에 나간 것 | 펌웨어에서의 의미 |
|---|---|---|---|
| 정상 쓰기 3바이트 | `I2C_OK`, `n_acked = 3` | S A0 A DE A AD A 01 A P | — |
| 없는 0x3C | `I2C_ERR_NACK_ADDR` | S 78 N P | 배선·전원·주소(7/8비트)·리셋 상태 확인 |
| 버퍼 2바이트 슬레이브에 4바이트 | `I2C_ERR_NACK_DATA`, `n_acked = 2` | S A0 A 01 A 02 A 03 N P | 디바이스가 거부(버퍼 끝, 잘못된 레지스터·명령) |
| `addr7 = 0xA0` | `I2C_ERR_ARG` | 없음 | 데이터시트의 8비트 표기를 그대로 넣은 실수 |
| SDA 가 0 인 채로 시작 | `I2C_ERR_BUS_BUSY`, 틱 증가 0 | 없음 | 다른 마스터가 쓰는 중이거나 stuck — COMM-M03 복구 |

NACK 뒤에도 STOP 을 내는 이유: STOP 이 없으면 다른 디바이스(그리고 멀티마스터 버스의 다른 마스터)는 버스가 아직 busy 라고 본다. 슬레이브 입장에서도 상태 머신이 IDLE 로 돌아가는 공식 신호는 STOP 이다. Linux 의 `i2c-algo-bit` 도 NACK 이 나면 STOP 을 내고 `-ENXIO`(주소)·`-EIO` 계열로 올린다 (정확한 errno 매핑은 커널 버전·어댑터 드라이버마다 다름 — 소스 확인 권장).

`i2c_scan` 은 0x08..0x77 마다 `i2c_write(b, a, NULL, 0, NULL)` — 주소만 보내고 STOP. 예약 주소(0x00–0x07: general call·START byte·CBUS·Hs 마스터 코드 등, 0x78–0x7F: 10비트 주소·device ID)는 건너뛴다 (UM10204 의 예약 주소 표, 확인 권장). `found[]` 에는 `cap` 개까지만 쓰고 반환값은 총 개수라, 호출자가 `ret > cap` 으로 잘림을 안다 — 테스트 `i2c_scan(&b, found, 1) == 2 && found[1] == 0`.

**랜덤 결과** (실제 출력):

```text
random: 139 writes, 149 reads, 112 to absent 0x3C, mismatches=0, SDA moves while SCL high=800
47/47 passed
```

400 트랜잭션 × (START 1 + STOP 1) = 800 — 주소 NACK 으로 끝난 112개도 정확히 START 1·STOP 1 을 냈다는 뜻이다.

### 모델이 빼먹은 것 — 실제 버스에서 더 볼 것

| 실제 | 이 모델 | 어디서 |
|---|---|---|
| 타이밍 규격: Standard-mode 100 kHz 에서 tLOW ≥ 4.7 µs, tHIGH ≥ 4.0 µs, tSU;DAT ≥ 250 ns / Fast-mode 400 kHz 에서 tLOW ≥ 1.3 µs, tHIGH ≥ 0.6 µs, tSU;DAT ≥ 100 ns (대표값 — UM10204 표 확인 권장) | 순서만, 시간 없음 | HWLAB-M05 (LA 타이밍 검사) |
| rise time (Rp × Cb), 레벨 0.3/0.7 VDD | 0/1 즉시 | HWLAB-B03 |
| 스파이크 필터(Fast-mode 에서 50 ns 이하 펄스 무시, 대표값) | 없음 — 1틱 글리치도 S/P 로 보인다 | — |
| 클럭 스트레칭 (슬레이브가 SCL 을 붙잡음) | 마스터가 SCL 을 다시 읽지 않음 | COMM-M01 |
| 중재(arbitration): 1 을 냈는데 0 을 읽으면 패배 | 단일 마스터 | COMM-M02 |
| SDA stuck-low 복구 (9클럭 + STOP) | busy 검출까지만 | COMM-M03 |

**복잡도·RAM.** 슬레이브 상태는 상태 1개 + 시프트 레지스터 1바이트 + 카운터 몇 바이트, 틱당 O(1). 비트뱅 마스터는 바이트당 27번 핀 조작 + 반 주기 대기 — 100 kHz 에서 바이트 하나에 약 90 µs 를 CPU 가 바쁘게 기다린다(인터럽트가 끼면 클럭이 늘어진다; I2C 는 클럭이 마스터 것이라 느려지는 건 허용되지만 SCL=1 시간이 늘어나는 도중 SDA 를 건드리면 안 된다). 그래서 양산 펌웨어는 하드웨어 I2C 주변장치 + 인터럽트/DMA 를 쓰고, 비트뱅은 핀이 모자라거나 하드웨어 블록에 errata 가 있을 때의 대안이다 (Linux `i2c-gpio`).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 47/47 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

## 7. 자주 하는 실수

**1. 8비트 주소를 7비트 자리에 넣는다.** 데이터시트가 "write address 0xA0, read address 0xA1" 처럼 R/W 를 붙인 8비트로 적는 경우가 많다. 그걸 `addr7` 로 넣으면 `0xA0 << 1` = 0x140 → `uint8_t` 로 잘려 0x40 이 나가고 영원히 NACK. 이 레슨의 드라이버는 `addr7 > 0x7F` 를 `I2C_ERR_ARG` 로 잡는다(테스트). 반대로 Linux `i2cdetect` 표와 `i2c_new_client_device()` 는 7비트를 쓴다.

**2. SCL 을 먼저 올리고 SDA 를 바꾼다.** 위 0xA5 실험 그대로 — 슬레이브는 가짜 START/STOP 을 본다. 고친 순서: `set_sda(b, bit); set_scl(b, 1); set_scl(b, 0);` (데이터는 SCL=0 일 때). 0xFF 로만 테스트하면 안 보인다.

**3. 마지막 읽기 바이트에 ACK 한다.** 슬레이브는 "더 달라"로 알아듣고 다음 바이트의 MSB 를 낸다. 그 비트가 0 이면 마스터의 STOP(SDA 0→1)이 wired-AND 에 막혀 버스가 SCL=1·SDA=0 으로 멈춘다 (연습 4: 다음 `i2c_write` 가 `BUS_BUSY`). MSB 가 1 이면 우연히 동작하지만 FIFO 형 디바이스라면 한 바이트를 더 꺼내 버려 데이터를 잃는다. 고친 코드: `m_read_byte(b, i + 1 < n)`.

**4. NACK 에서 그냥 return 한다.** `if (!m_write_byte(...)) return I2C_ERR_NACK_ADDR;` — STOP 이 없으니 버스는 SCL=0 인 채로 남는다. 이 레슨의 드라이버라면 다음 호출이 idle 검사에서 `I2C_ERR_BUS_BUSY` 로 막히고, 실제 버스에서는 슬레이브가 IDLE 로 못 돌아가며 하드웨어 I2C 주변장치나 다른 마스터는 버스가 계속 busy 라고 본다. 고친 코드: 에러 경로마다 `m_stop(b);`.

**5. GPIO 를 push-pull 로 둔 채 비트뱅한다.** "1 을 쓴다"를 HIGH 를 *모는* 것으로 구현하면 슬레이브가 ACK 로 당기는 순간 VDD–GND 가 FET 두 개로 맞붙는다(전류 다툼). 파형은 중간 레벨로 찌그러지고 칩이 상할 수 있다. 고친 방법: STM32 라면 `GPIOx_OTYPER` 비트 = 1(open-drain)로 두고 ODR 1/0 을 쓰거나, 핀을 "입력(놓음)" ↔ "출력 LOW(당김)" 로 전환한다 (Linux `i2c-gpio` 의 open-drain 에뮬레이션과 같은 방식).

**6. SCL 을 올리기 전에 SDA 를 읽는다.** `v = b->sda; set_scl(b, 1);` 순서면 슬레이브가 아직 다음 비트를 확정하지 않았거나 이전 비트를 읽게 된다 — 한 비트 밀린 값. 받는 쪽은 **SCL=1 구간**에서 읽는다.

**7. 슬레이브가 SCL↑ 에서 SDA 를 바꾼다.** 슬레이브가 "샘플하는 엣지"와 "바꾸는 엣지"를 섞으면, SCL=1 인 동안 자기 SDA 가 바뀌어 **다른 슬레이브들**에게 START/STOP 으로 보인다. 규칙은 양쪽 모두에게 같다: 바꾸는 건 SCL↓ 뒤.

## 8. 면접 Q&A

<details><summary>Q1. I2C 는 왜 오픈드레인 + 풀업인가요? push-pull 이면 안 되나요?</summary>

여러 노드가 같은 선을 *동시에* 건드리는 순간이 프로토콜에 들어 있기 때문이다: 9번째 클럭의 ACK(마스터는 놓고 슬레이브가 당김), 클럭 스트레칭(슬레이브가 SCL 을 붙잡음, COMM-M01), 멀티마스터 중재(1 을 낸 마스터가 0 을 읽으면 진다, COMM-M02). 오픈드레인이면 이 겹침이 그냥 AND 가 되고, push-pull 이면 한쪽이 HIGH 를 몰고 다른 쪽이 LOW 를 당길 때 쇼트가 된다. 대가는 속도: HIGH 는 Rp·Cb 로 천천히 올라가므로 Rp 를 작게 하면 sink 전류 한계(Standard/Fast 에서 3 mA 기준, 대표값)를 넘고 크게 하면 rise time 규격을 넘는다 — HWLAB-B03 의 풀업 창. 꼬리: 3.3 V 와 1.8 V 디바이스를 섞으려면? 각자 풀업 + MOSFET 양방향 레벨 시프터(또는 전용 IC) — 오픈드레인이라 가능한 구성이다.

</details>

<details><summary>Q2. ACK 는 물리적으로 누가 무엇을 하는 건가요? NACK 은 언제 나오나요?</summary>

9번째 클럭에서 송신 측은 SDA 를 놓고, 수신 측이 SCL=1 구간 동안 SDA 를 LOW 로 당기면 ACK 다. 쓰기에서는 슬레이브가, 읽기에서는 마스터가 수신 측이다. NACK(놓아 둠, 1)이 나오는 경우는 UM10204 의 요지로 다섯 가지: ① 그 주소의 수신자가 없다 ② 수신자가 다른 일 중이라 받을 수 없다(예: EEPROM 내부 쓰기 사이클 — 연습 2 ACK polling) ③ 이해 못 하는 데이터·명령 ④ 더 받을 수 없다(버퍼 끝 — Part B 데이터 NACK) ⑤ 마스터-수신자가 "마지막 바이트"를 알림. 그래서 드라이버는 **주소 NACK 과 데이터 NACK 을 다른 에러로** 올려야 원인을 좁힐 수 있다.

</details>

<details><summary>Q3. 마스터가 SCL=1 일 때 SDA 를 바꾸면 슬레이브는 무엇을 보나요? 스코프에선요?</summary>

1→0 이면 START, 0→1 이면 STOP 이다 — 프로토콜 정의상 다른 해석이 없다. 0xA5 를 그렇게 보내면 슬레이브는 가짜 START 3번·가짜 STOP 3번을 보고 IDLE 로 빠져 9번째 클럭에 NACK 한다(이 레슨 실측). 스코프나 LA 디코더(연습 1)에는 바이트 중간에 S/P 가 찍힌다. 비트뱅 코드의 순서 버그 말고도 원인이 될 수 있는 것: SCL 과 SDA 사이 크로스토크, 느린 rise time 에서 SDA 변화가 SCL↓ 보다 늦게 보이는 경우(데이터 hold 위반), 스파이크 필터 없는 슬레이브에서의 글리치. 꼬리: "데이터 hold time tHD;DAT 최소가 0 인데 왜 문제가 되나요?" — SCL 하강 엣지에는 레벨이 불확정한 구간이 있어서, UM10204 타이밍 표 주석은 디바이스가 SCL 의 VIH(min) 기준으로 SDA 를 내부적으로 최소 300 ns hold 해 그 구간을 넘기라고 요구한다 (주석 문구는 확인 권장). 느린 SCL 하강 + hold 없는 송신기 = 받는 쪽이 SCL=1 중 SDA 변화로 볼 수 있다.

</details>

<details><summary>Q4. Repeated START 와 STOP 후 START 는 무엇이 다르죠?</summary>

Sr 은 STOP 없이 SCL=0 에서 SDA·SCL 을 올리고 다시 START 조건을 만든다. 버스가 한 번도 free 가 되지 않으므로 ① 멀티마스터 버스에서 다른 마스터가 끼어들 수 없고 ② 일부 디바이스는 STOP 에서 내부 상태(레지스터 포인터 등)를 리셋하므로 "레지스터 번호 쓰기 → 읽기"를 한 묶음으로 만들 수 있다. 전형적 사용 = 레지스터 읽기: `S addr+W reg Sr addr+R data… NACK P`. 이 레슨의 `m_start` 는 SCL=0 에서 불리면 Sr 순서를 따르지만, 로그 비교와 센서 드라이버는 COMM-B07 에서 한다.

</details>

<details><summary>Q5. 버스의 모든 주소가 NACK 입니다. 무엇부터 보나요?</summary>

순서: ① idle 레벨 — 스코프로 SDA·SCL 이 VDD 인가 (풀업 없음/미실장이면 0 근처 또는 떠 있음 → HWLAB-B03 처럼 rise time 이 비정상) ② 하나라도 LOW 에 붙어 있나 (stuck slave → busy, COMM-M03 복구) ③ 파형에서 START 와 주소 비트를 읽어 7비트/8비트 혼동 확인 (0x50 을 원했는데 0xA0 이 나가면 0x50<<1 이 아니라 0x140) ④ 디바이스 전원·리셋·주소 핀(A0..A2) 스트랩 ⑤ 레벨 시프터 방향/전압 ⑥ 클럭 속도 — 400 kHz 를 지원 안 하는 디바이스, 긴 케이블 ⑦ 칩 자체가 다른 마스터나 디버거에 붙잡혀 있나. Linux 라면 `i2cdetect -y <bus>` 로 스캔부터 하되, 주소만 찌르는 quick write 가 특정 디바이스(일부 EEPROM 등)에 부작용이 있을 수 있어 도구가 주소 범위별로 다른 명령을 쓴다는 점을 말해 두면 좋다 (man page 확인 권장).

</details>

<details><summary>Q6. 비트뱅과 하드웨어 I2C 주변장치 중 무엇을 쓰나요? 인터럽트가 끼면 비트뱅은 깨지나요?</summary>

I2C 는 클럭이 마스터 것이라 비트뱅 중 인터럽트로 SCL=0 이나 SCL=1 구간이 *늘어나는* 것은 허용된다 (최소 시간만 있고 최대 시간은 SMBus 가 아니면 없다 — SMBus 는 클럭 low 타임아웃 25–35 ms 가 있다, 대표값). 깨지는 경우는 순서가 바뀔 때뿐이다. 그래서 비트뱅은 핀이 모자랄 때, 하드웨어 블록 errata 가 있을 때(STM32F1 세대 I2C 의 BUSY 플래그 고착 errata 가 유명 — 칩별 errata sheet 확인), 버스 복구용으로 쓴다. 양산 경로는 하드웨어 주변장치 + 인터럽트/DMA: CPU 를 기다리게 하지 않고, 클럭 스트레칭·중재·타임아웃을 하드웨어가 처리한다. 꼬리: 드라이버 API 를 비트뱅과 HW 양쪽에 같게 두려면? `bus_ops`(read/write 함수 포인터) 추상화 — COMM-M12.

</details>

<details><summary>Q7. 왜 바이트는 MSB first 인가요? UART 는 LSB first 인데요.</summary>

I2C 의 첫 바이트는 주소이고, MSB first 라 주소의 상위 비트부터 나간다. 덕분에 멀티마스터 중재에서 *먼저 다른 비트*가 나오는 순간 낮은 주소(0 을 낸 쪽)가 이긴다 — 주소 크기 비교가 비트 순서와 같아진다 (CAN 의 ID 중재와 같은 원리, COMM-M05). 슬레이브 입장에서도 상위 비트부터 주소를 비교하다 다른 비트가 나오면 바로 관심을 끊을 수 있다 (이 레슨의 0x51 슬레이브는 단순화해 8번째에서 한 번에 비교). UART 의 LSB first 는 역사적 관례(시프트 레지스터 구현)이고 프로토콜상 필연은 아니다 (COMM-B01).

</details>

## 9. 추가 연습

### 연습 1 — 로직 애널라이저 디코더

트레이스 문자열(`tr_scl`, `tr_sda`)만 보고 `"S A0 A DE A AD A 01 A P"` 같은 텍스트를 만드는 `decode_trace(scl, sda, n, out, cap)` 를 짜라. 바이트 중간에 S/P 가 오면 잘린 비트는 버린다. 출력 용량이 모자라면 -1. 정상 트랜잭션, 주소 NACK, 데이터 NACK, 읽기, 본문의 규칙 위반 트레이스, 랜덤 200개로 검증.

<details><summary>답 보기</summary>

상태는 `nb` 하나: −1 = START 전/STOP 뒤(무시), 0..8 = 이번 바이트에서 받은 비트 수. 인접 두 칸 비교로 슬레이브와 똑같이 판정한다: SCL 1→1 + SDA 변화 → `S`(nb = 0) 또는 `P`(nb = −1), SCL 0→1 → nb &lt; 8 이면 시프트, nb == 8 이면 바이트(16진 2자리)와 `A`/`N` 출력 후 nb = 0. 규칙 위반 트레이스는 `S A0 A S P S P S P P` 로 디코드된다 — 슬레이브 카운터(+4/+4)와 같은 그림이다. sigrok 같은 디코더도 같은 원리이고, 실제 캡처(샘플링 간격·글리치)를 다루는 건 HWLAB-M06 과 COMM-A10.

</details>

### 연습 2 — EEPROM ACK polling

24xx 계열 EEPROM 은 쓰기 STOP 뒤 내부 쓰기 사이클(tWR, 수 ms — 예: 5 ms max, 대표값·데이터시트 확인 권장) 동안 **자기 주소에 NACK** 한다. 슬레이브에 "쓰기 뒤 B 틱 busy" 를 넣고(`busy_after_write`), 주소만 보내 ACK 가 올 때까지 기다리는 `i2c_wait_ready(b, addr, max_polls)` 를 짜라. 반환 = ACK 받은 시도 번호, 다 써도 NACK 이면 `I2C_ERR_TIMEOUT`. 모델에서 필요한 시도 횟수를 식으로 구해 B = 0..400 전수와 맞춰 보라.

<details><summary>답 보기</summary>

```c
for (int k = 1; k <= max_polls; k++) {
    int r = i2c_write(b, addr7, NULL, 0, NULL);
    if (r == I2C_OK) return k;
    if (r != I2C_ERR_NACK_ADDR) return r;   /* busy 버스 등은 그대로 */
}
return I2C_ERR_TIMEOUT;
```

프로브 하나 = 32틱이고, 슬레이브의 주소 판정은 프로브의 26번째 틱(8번째 SCL↓)이다. busy 가 STOP 틱에서 B 로 시작해 틱마다 1 씩 줄면, k 번째 프로브의 판정 시점에 남은 busy = B − (32(k−1) + 26). 이것이 0 이하인 최소 k = `B ≤ 26 ? 1 : 1 + ⌈(B − 26)/32⌉`. B = 200 → 7, 3번 시도로 96틱을 쓴 뒤 남은 104 → 4. B = 0..400 전수에서 시뮬레이션 = 식. 실제 펌웨어에서는 시도 사이에 짧은 지연을 넣고, 최대 시도 횟수를 tWR(max) ÷ 프로브 시간으로 잡는다. "없는 디바이스"도 영원히 NACK 이라 같은 TIMEOUT 이 나오므로, 쓰기 직후가 아닌데 TIMEOUT 이면 주소/배선 문제로 분류한다.

</details>

### 연습 3 — 트랜잭션은 얼마나 걸리나

① 모델에서 데이터 n 바이트 쓰기·읽기의 틱 수 식을 세우고 실제 `b.ticks` 증가량과 n = 0..8 에서 맞춰라. ② 실제 버스에서 "레지스터 2바이트 읽기"(addr+W, reg, Sr, addr+R, d0, d1)의 SCL 클럭 시간을 100 kHz / 400 kHz / 1 MHz 에서 정수 ns 로 계산하라 (START/STOP 의 setup·hold 와 버스 free 시간은 뺀 하한).

<details><summary>답 보기</summary>

① START 2 + 바이트당 27 (9비트 × 3틱) × (n + 1) + STOP 3 → n = 0 이면 32. 읽기도 바이트당 27 (읽기 8비트 + ACK 쓰기 1비트) 이라 같은 식. ② 바이트 5개 × 9클럭 = 45 클럭 → 100 kHz 450000 ns, 400 kHz 112500 ns, 1 MHz(Fast-mode Plus) 45000 ns. 400 kHz 에서 이런 읽기는 초당 최대 약 8888 번 — 실제로는 START/Sr/STOP 의 setup·hold, 드라이버·인터럽트 오버헤드로 더 적다. 고속 IMU(수 kHz ODR)를 I2C 로 읽기 빠듯한 이유이고, 그래서 IMU 는 SPI 나 FIFO burst 읽기(COMM-B05)를 쓴다.

</details>

### 연습 4 — 마지막 바이트까지 ACK 하는 버그

`i2c_read_bad()` 는 모든 바이트에 ACK 한다. 슬레이브가 다음에 낼 바이트의 MSB 가 0 일 때와 1 일 때 각각 무슨 일이 생기는지 테스트로 보여라. 정답 `i2c_read()` 는 어떻게 다른가?

<details><summary>답 보기</summary>

다음 바이트 0x05 (MSB 0): 마지막 ACK 뒤 SCL↓ 에서 슬레이브가 0 을 몬다. 마스터의 STOP 은 SDA 0 → SCL 1 → SDA 놓기 인데, 놓아도 슬레이브가 당기고 있어 SDA 가 0 에 머문다 → STOP 이 안 생긴다. 끝 상태: SCL = 1, SDA = 0, 슬레이브는 TX 상태, `stops` 그대로, 다음 `i2c_write` 는 `I2C_ERR_BUS_BUSY`. 실행 출력의 트레이스가 SDA `______` 로 끝나는 모습이다. 풀려면 SCL 을 더 쳐서 슬레이브가 바이트를 다 내보내게 한 뒤 STOP — 이것이 COMM-M03 의 "9클럭 복구"다. 다음 바이트 0x85 (MSB 1): STOP 이 우연히 성공하지만 슬레이브는 `tx_pos == 3` — 한 바이트를 더 꺼냈다. 데이터 레지스터가 FIFO 인 센서라면 샘플 하나가 사라진다. 정답 `i2c_read` 는 마지막 바이트에 NACK → 슬레이브가 놓고 IDLE → STOP 정상, `tx_pos == 2`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 35/35 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 오픈드레인 + 풀업 = wired-AND. "1 을 쓴다" = 놓는다. 그래서 ACK·스트레칭·중재가 쇼트 없이 겹칠 수 있다 (push-pull 비트뱅 금지).
- idle = SDA·SCL 모두 1. START = SCL=1 중 SDA↓, STOP = SCL=1 중 SDA↑. 데이터 SDA 는 **SCL=0 에서만** 바뀌고 SCL=1 에서 읽힌다.
- 바이트 = 8비트 MSB first + 9번째 클럭. 9번째엔 송신 측이 놓고 **수신 측이 당기면 ACK**. 첫 바이트 = addr7 &lt;&lt; 1 | R/W.
- 슬레이브: SCL↑ 에서 샘플, SCL↓ 에서만 SDA 변경, START/STOP 은 어느 상태에서든 최우선.
- SCL 을 먼저 올리고 SDA 를 바꾸면 비트 전환마다 가짜 S/P (0xA5 → 가짜 START 3·STOP 3, 실측). 0x00/0xFF 테스트로는 일부가 숨는다 — 0xA5/0x5A + 256 전수.
- 드라이버 에러는 ARG / BUS_BUSY / NACK_ADDR / NACK_DATA(+ n_acked) 로 나누고, START 를 냈으면 무조건 STOP.
- 읽기는 마지막 바이트만 NACK — ACK 하면 슬레이브가 SDA 를 쥐어 STOP 실패·버스 hang 또는 FIFO 바이트 손실.
- 모델 비용: 주소 프로브 32틱, n 바이트 = 2 + 27(n+1) + 3. 실제 400 kHz 레지스터 2바이트 읽기 ≥ 112.5 µs.

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual* (Rev. 7, 2021) — START/STOP 정의, 데이터 유효성(SCL HIGH 동안 안정), ACK/NACK 다섯 경우, 예약 주소 표, Standard/Fast/Fast-mode Plus 타이밍 표: <https://www.nxp.com/docs/en/user-guide/UM10204.pdf> (이 레슨의 타이밍 숫자는 기억에 의존한 대표값 — 표로 확인 권장)
- Linux kernel, `drivers/i2c/algos/i2c-algo-bit.c` (비트뱅 알고리즘: setsda → sclhi → scllo 순서, NACK 처리)와 `drivers/i2c/busses/i2c-gpio.c` — 오픈드레인 에뮬레이션: <https://github.com/torvalds/linux/tree/master/drivers/i2c>
- Linux kernel docs, "I2C/SMBus Subsystem" — 7비트 주소 표기, `i2c_transfer`, I2C_M_* 플래그: <https://docs.kernel.org/i2c/index.html>
- i2c-tools, `i2cdetect(8)` man page — 스캔 범위와 quick write / receive byte 프로빙 선택: <https://manpages.debian.org/i2c-tools/i2cdetect.8.en.html>
- SMBus Specification 3.x — 35 ms 클럭 low 타임아웃 등 I2C 와의 차이 (확인 권장): <http://smbus.org/specs/>
- Microchip 24LC256 데이터시트 — "Acknowledge Polling" 절, tWR (연습 2 의 근거, 수치는 데이터시트 확인)
- ST, STM32 Reference Manual 의 GPIO 장(OTYPER = open-drain)과 I2C 장, 그리고 STM32F1 errata sheet 의 I2C 항목
- 관련 레슨: HWLAB-B03 (풀업 창·rise time), HWLAB-M05 (LA 타이밍 검사), HWLAB-M06 (프로토콜 디코더), COMM-B01 (UART LSB first), COMM-B04/B05 (SPI — 오픈드레인이 아닌 push-pull 버스), COMM-B07 (repeated START·센서 드라이버), COMM-M01 (클럭 스트레칭), COMM-M02 (중재), COMM-M03 (버스 복구 9클럭), COMM-M05 (CAN 중재 — 같은 wired-AND 원리), COMM-M12 (bus_ops 추상화), COMM-A05 (I3C), COMM-A10 (LA 디코더), CPP-B07 (RAII 로 트랜잭션 경계 지키기)
