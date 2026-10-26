# V8 · I2C Bus Scan + Stuck-Bus Recovery (bit-banged)

> **ID**: V8 · **Priority**: P1 · **Pattern**: open-drain bit-bang + protocol state + timeout · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V8` / `./run.sh V8 sol`

## 1. Problem

On a new board, the SoC's I2C pins are muxed as GPIOs and you drive the bus by hand to see what is on it. Both lines are **open-drain with pull-ups**: you can only pull a line low or release it; a released line is high unless some device holds it low.

A bus model in the file simulates slaves that ACK their address, a slave that **stretches the clock** (holds SCL low during the ACK clock), and a slave that was **stuck mid-byte** by a reset: it holds SDA low and lets go only after it sees enough SCL falling edges to finish its byte.

Implement:

1. `i2c_bus_recover()` — if SDA is already high, return 0 (no clocks). Otherwise pulse SCL (low, then release) **at most 9 times**, checking SDA after each pulse. Once SDA is high, generate a STOP and return the number of pulses used. Still low after 9 pulses → `-EIO` (no STOP). SCL that cannot go high → `-ETIMEDOUT`.
2. `i2c_probe(addr)` — `-EINVAL` for reserved addresses (`0x00–0x07`, `0x78–0x7F`, `> 0x7F`) with nothing on the bus; `-EBUSY` if SCL or SDA is low before START. Otherwise START, send `(addr << 1) | 0` MSB first, read the ACK bit, STOP. Return 0 on ACK, `-ENXIO` on NACK.
3. **Clock stretching**: every time you release SCL, wait until it is really high. If it stays low for `I2C_STRETCH_TIMEOUT_US` (200 µs), release both lines and return `-ETIMEDOUT`.
4. `i2c_scan(found, max)` — probe `0x08..0x77` in order, write up to `max` ACKing addresses into `found`, return the total count. Any probe error other than `-ENXIO` aborts the scan and is returned.

Wait `I2C_HALF_US` (5 µs, 100 kHz) between line changes.

```c
void gpio_scl_set(int release);   /* 0 = drive low, 1 = release */
void gpio_sda_set(int release);
int  gpio_scl_get(void);          /* real line level, 1 = high */
int  gpio_sda_get(void);
void delay_us(uint32_t us);

int i2c_bus_recover(void);                    /* pulses used (0..9) or -EIO / -ETIMEDOUT */
int i2c_probe(uint8_t addr);                  /* 0, -ENXIO, -EINVAL, -EBUSY, -ETIMEDOUT */
int i2c_scan(uint8_t *found, size_t max);     /* total found, or first hard error */
```

### Example

```
devices at 0x1D, 0x48, 0x50
i2c_scan(found, 8)  -> 3, found = {0x1D, 0x48, 0x50}
                       112 probes (0x08..0x77), 112 START + 112 STOP, 110 us each = 12320 us

slave stuck holding SDA, releases after 5 more SCL falling edges
i2c_probe(0x48)     -> -EBUSY      (SDA low: no START possible)
i2c_bus_recover()   -> 5           (5 pulses + STOP, 65 us)
i2c_probe(0x48)     -> 0

SDA shorted to GND:            i2c_bus_recover() -> -EIO after exactly 9 pulses
slave stretches SCL 40 us:     i2c_probe(0x50) -> 0 in 150 us
slave never releases SCL:      i2c_probe(0x50) -> -ETIMEDOUT after 290 us
```

### Constraints

- No `malloc`, no interrupts: plain polling with `delay_us`.
- Change SDA **only while SCL is low**, except for START (SDA falls while SCL high) and STOP (SDA rises while SCL high).
- Never put a reserved address on the bus.

### Clarifying questions to ask (first 3 minutes)

1. Is this a 7-bit-only bus? Should the scan use a write probe (`addr+W`) or a read probe (some devices only answer reads)?
2. What bus speed and what clock-stretching timeout do you want?
3. If the bus is stuck before the scan, should `i2c_scan` recover automatically or report and stop?
4. What should recovery return — pulse count, or just success/fail?
5. Are there devices that must not be probed (e.g. ones that treat an empty write as a command)?

## 2. Your workspace

`./run.sh V8`로 바로 테스트하세요. 테스트 7개(스캔, 버퍼 부족, 단일 probe, stuck 복구, 9클록 최악, SDA short, clock stretching)가 들어 있습니다. 파일 아래쪽 bus model은 수정하지 마세요.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — "SCL을 놓는다" = 놓고 기다린다</summary>

open-drain에서 `gpio_scl_set(1)`은 "high로 만든다"가 아니라 "놓는다"입니다. 슬레이브가 잡고 있으면 여전히 low입니다. 그래서 헬퍼 하나를 먼저 만드세요.

`scl_high()`: `gpio_scl_set(1)` → `while (!gpio_scl_get())` 동안 `delay_us(1)`, 누적 200 µs면 `-ETIMEDOUT`. probe와 recover의 **모든** SCL 상승은 이 헬퍼로 합니다.

</details>

<details>
<summary>Hint 2 — 한 비트 = "SCL low에서 SDA 세팅 → SCL high에서 샘플"</summary>

```
START : SDA low, delay, SCL low
bit   : SDA = b, delay, scl_high(), delay, SCL low        (x 8, MSB first)
ACK   : SDA release, delay, scl_high(), ack = !SDA, delay, SCL low
STOP  : SDA low, delay, scl_high(), delay, SDA release, delay
```

ACK는 **SCL이 high일 때** 읽습니다. 슬레이브가 ACK을 SDA low로 표현하므로 `ack = (gpio_sda_get() == 0)`.

</details>

<details>
<summary>Hint 3 — 복구 루프의 종료 조건 두 개</summary>

`while (SDA low) { if (n == 9) return -EIO; pulse; n++; }` 그리고 루프를 빠져나오면 STOP. 반드시 펄스 **후에** SDA를 확인해야 정확히 필요한 만큼만 클록을 줍니다. STOP은 `SCL low → SDA low → SCL high → SDA high` 순서 — SDA를 먼저 low로 만들어야 "SCL high 중 SDA 상승" 엣지를 만들 수 있습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 파형으로 보기

I2C의 규칙은 딱 두 줄입니다: **데이터는 SCL low일 때 바꾸고 SCL high일 때 읽는다**, 그리고 **SCL high 중 SDA가 바뀌는 것은 START/STOP 신호다**. probe 한 번은 START + 주소 바이트 8비트 + ACK 1클록 + STOP입니다.

<div class="chart" data-src="chart1.svg" data-caption="i2c_probe(0x48): 주소 바이트 0x90 = 1001 0000. 9번째 클록(ACK)에 슬레이브가 SDA를 low로 잡으면 장치가 있는 것. 반 주기 22개 = 110 µs"></div>

ACK/NACK의 전기적 의미가 핵심입니다. ACK은 **누군가 능동적으로 SDA를 끌어내린 것**이고, NACK은 **아무도 안 잡아서 pull-up이 high로 만든 것**입니다. 그래서 장치가 없거나, 전원이 꺼졌거나, 주소가 틀리면 모두 NACK입니다 — 스캔이 동작하는 원리입니다.

### 4.2 stuck bus와 9클록 복구

슬레이브가 **읽기 바이트를 보내는 도중** 마스터만 리셋되면, 슬레이브는 아직 바이트의 남은 비트를 내보내는 중이라 0인 비트에서 SDA를 잡고 있습니다. 마스터는 SDA가 low라 START를 못 겁니다(`-EBUSY`). SCL을 더 주면 슬레이브가 남은 비트를 다 내보내고, ACK 슬롯에서 마스터가 SDA를 놓아 둔 상태(=NACK)를 보고 전송을 끝냅니다.

<div class="chart" data-src="chart2.svg" data-caption="테스트 4번: 슬레이브가 5번째 SCL falling edge에서 SDA를 놓음 → 펄스 5번 후 SDA high 확인 → STOP. 남은 비트가 최대 8 + ACK 1 = 9라서 9클록이면 충분합니다"></div>

### 4.3 손으로 따라가기 (trace)

**i2c_probe(0x48)** — 바이트 `0x90`, 시간 단위 5 µs:

| 단계 | SDA (마스터) | SCL | 슬레이브 동작 | 누적 µs |
|---|---|---|---|---|
| START | 1 → 0 (SCL high) | high → low | 모든 슬레이브 ADDR 상태 | 5 |
| bit A6..A0 | 1,0,0,1,0,0,0 | 7 클록 | 상승 엣지마다 shift | 75 |
| bit R/W | 0 | 1 클록 | 8비트 수신 완료 | 85 |
| (8번째 falling) | – | low | 주소 일치 → **SDA low (ACK)** | – |
| ACK | release | `scl_high()` → 샘플 SDA = 0 | – | 95 |
| (falling) | – | low | SDA 놓음 | – |
| STOP | 0 → 1 (SCL high) | high | 모두 IDLE | 110 |

**테스트별 숫자** (solution.c로 측정):

| 시나리오 | 반환 | 시간 | 확인 |
|---|---|---|---|
| probe 0x48 (ACK) | 0 | 110 µs | START 1, STOP 1 |
| probe 0x49 (NACK) | `-ENXIO` | 110 µs | NACK이어도 STOP은 보냄 |
| scan, 장치 3개 | 3 | 12 320 µs | 주소 112개, 최소 0x08, 최대 0x77 |
| scan, `max = 2` | 3 | – | `found[2]` 안 건드림 |
| stuck, 5클록 남음 | 5 | 65 µs (5×10 + STOP 15) | STOP 1, 이후 probe 성공 |
| stuck, 9클록 남음 | 9 | – | 최악의 복구 가능 케이스 |
| SDA short | `-EIO` | 90 µs | SCL falling 정확히 9번, STOP 0 |
| stretch 40 µs | 0 | 150 µs (110 + 40) | ACK 클록에서 40 µs 대기 |
| stretch forever | `-ETIMEDOUT` | 290 µs (5 + 80 + 5 + 200) | 두 라인 모두 release |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `scl_high()` 하나가 clock stretching 전체입니다. 이게 없으면 마스터가 슬레이브보다 먼저 다음 비트로 넘어가서 버스가 깨집니다(테스트 7번: STOP이 안 나감).
- `i2c_probe`는 NACK이어도 STOP을 보냅니다. STOP 없이 다음 START를 걸면 repeated START가 되어 슬레이브 상태가 꼬일 수 있습니다.
- 주소 검사(`-EINVAL`)와 idle 검사(`-EBUSY`)가 START보다 먼저입니다 → 잘못된 호출은 버스에 흔적이 없음(테스트 3, 4, 6번 `starts == 0`).
- `i2c_bus_recover`의 9번 제한은 `n == 9` 체크를 **펄스 전에** 해서 정확히 9번에서 멈춥니다(테스트 6번 `m_falls == 9`).
- 스캔의 반환/버퍼 규칙은 T1-1과 같습니다: 쓸 수 있는 만큼 쓰고 전체 개수를 반환.

### 4.5 복잡도

- probe 1회: 9클록 + START/STOP = 반 주기 22개 = 110 µs(100 kHz). 스캔: 112 × 110 µs ≈ 12.3 ms.
- recover: 최대 9 × 10 µs + STOP 15 µs = 105 µs (stretching 없을 때).
- 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **왜 bit-bang인가**: 실제 SoC에는 I2C 컨트롤러 IP가 있지만, 복구는 컨트롤러가 못 하는 경우가 많습니다(SDA low면 START 자체가 안 걸림). 그래서 드라이버는 pinmux를 **GPIO로 바꿔서** 9클록을 주고 다시 I2C 기능으로 돌립니다. Linux의 `i2c_generic_scl_recovery()` / `recovery-gpios`(scl-gpios, sda-gpios) DT 속성이 바로 이것입니다.
- **open-drain과 pull-up**: 상승 시간은 `R_pullup × C_bus`로 정해집니다. 100 kHz 표준 모드는 rise time ≤ 1000 ns, 400 kHz는 ≤ 300 ns. 풀업이 너무 크면 엣지가 느려 고속에서 비트가 틀리고, 너무 작으면 3 mA sink 한도를 넘습니다.
- **스캔이 SoC validation에서 하는 일**: 보드에 실장된 장치(PMIC, 센서, EEPROM, 카메라 serializer)가 기대한 주소에서 응답하는지 = **보드 enumeration**, 그리고 pinmux·pad 설정·전원 시퀀스·레벨 시프터가 맞는지를 한 번에 확인합니다. 기대 목록과 diff해서 리포트합니다.
- **probe의 위험**: 일부 장치는 빈 write에도 반응합니다(예: 일부 EEPROM이 write pointer를 바꿈). Linux `i2cdetect`가 주소 범위마다 quick-write/read-byte를 골라 쓰는 이유입니다.
- 관련: 레지스터 접근 계층과 sanity test는 [V2](../V2_reg_field_rmw/index.html), 테스트 결과 집계는 [V5](../V5_test_runner_coverage/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"로직 분석기로 어떻게 디버그하나?"** — SCL/SDA 두 채널을 최소 4–10배 오버샘플(100 kHz면 1–2 MS/s 이상)로 잡고 I2C 디코더를 켭니다. NACK인지, 주소가 틀렸는지(7비트 vs 8비트 표기 혼동), stretching이 얼마나 긴지, STOP이 있는지 바로 보입니다. 엣지가 둥글면 아날로그 채널/오실로스코프로 rise time을 봅니다.
  *"First I put a logic analyzer on SCL and SDA: the decoder tells me ACK versus NACK and the address, and a scope tells me whether the rise time is within spec."*
- **"스캔에서 장치 하나가 안 보이면 원인 순서는?"** — 전원/리셋 핀 → pinmux/pad(open-drain 설정, 내부 pull-up) → 주소 strap 핀 → 레벨 시프터/버스 스위치 enable → 장치 고장. 스캔은 이 중 "응답이 없다"는 것만 알려주므로 측정으로 좁힙니다.
  *"A missing device in the scan is a symptom; I walk power, reset, pinmux, address straps, then the level shifter before blaming the part."*
- **"Silicon에서 이 테스트를 어떻게 쓸 건가?"** — bring-up 첫날 보드 enumeration, 그리고 PVT(전압·온도) 코너에서 반복 스캔해서 마진이 없는 버스(엣지가 느린 버스)를 찾습니다. SLT에서는 기대 장치 목록과 diff해서 pass/fail.
  *"The same scan runs at every PVT corner; a device that disappears only at low voltage and hot points to a marginal pull-up or rise time."*
- **"복구가 실패하면?"** — 9클록 후에도 SDA가 low면 소프트웨어로는 못 고칩니다: 슬레이브 전원/리셋 라인 토글, 버스 스위치로 구간 분리, 그래도 안 되면 보드 short. 그래서 `-EIO`를 위로 올려서 보고합니다.
  *"If nine clocks don't free SDA, it's not a protocol hang anymore — I power-cycle or reset that device, or isolate the segment with the bus mux."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why exactly 9 clocks? | 슬레이브가 멈출 수 있는 최악의 위치는 바이트의 첫 비트입니다. 남은 것은 데이터 8비트 + ACK 슬롯 1개 = 9클록. ACK 슬롯에서 마스터가 SDA를 놓아 두면 슬레이브는 NACK으로 보고 전송을 끝내고 SDA를 놓습니다. |
| What do ACK and NACK mean electrically? | ACK = 수신 측이 9번째 클록 동안 SDA를 **능동적으로 low**로 잡음. NACK = 아무도 안 잡아서 pull-up이 high로 둠. 그래서 "장치 없음"과 "장치가 거부함"은 전기적으로 구별이 안 됩니다. |
| How do you size the pull-up resistor? | 하한: `R_min = (VDD − V_OL) / I_OL` (예: 3.3 V, 0.4 V, 3 mA → 약 1 kΩ). 상한: `R_max = t_r / (0.8473 × C_bus)` (100 kHz, t_r 1000 ns, 200 pF → 약 5.9 kΩ). 그 사이에서 속도·소비 전류를 보고 고릅니다. |
| What is a repeated START and when do you need it? | STOP 없이 START를 다시 거는 것(Sr). 레지스터 읽기에서 "레지스터 포인터 쓰기 → Sr → 읽기"를 버스 소유권을 놓지 않고 이어서 하려고 씁니다(멀티 마스터에서 중간에 다른 마스터가 끼어들지 못함). |
| How does 10-bit addressing work? | 첫 바이트가 `11110 A9 A8 R/W`, 두 번째 바이트가 A7..A0. `0x78–0x7B`가 예약된 이유입니다. 스캔에서 이 범위를 건너뛰는 이유이기도 합니다. |
| I2C vs I3C? | I3C는 push-pull(최대 12.5 MHz SDR), 동적 주소 할당(ENTDAA)으로 스캔 대신 **열거**, in-band interrupt(IBI)로 별도 IRQ 선 불필요, I2C 장치와 같은 버스에서 공존 가능. 센서가 많은 카메라/IMU 시스템에서 점점 많이 씁니다. |
| Why does an SoC validation test do a bus scan at all? | 보드 enumeration(실장된 장치가 기대 주소에서 응답) + pinmux/pad/전원 시퀀스 검증을 한 번에. 기대 장치 목록과 diff해서 리포트하고, PVT 코너마다 반복해서 마진을 봅니다. |
| What about a slave holding SCL low forever? | 9클록 복구는 SDA stuck용이고, SCL stuck은 마스터가 클록을 줄 수 없어서 못 고칩니다. 타임아웃(SMBus는 25–35 ms)으로 감지하고 장치 리셋/전원 차단으로 처리합니다. 이 문제의 `-ETIMEDOUT`이 그 감지 부분입니다. |

## 7. 자주 하는 실수

- `gpio_scl_set(1)` 후 바로 다음 단계로 감 → clock stretching 무시, STOP이 안 나감 (테스트 7번)
- stretching 대기에 타임아웃이 없음 → 영원히 매달림 (테스트 7번 forever, 모델이 강제 종료)
- SCL high일 때 SDA를 바꿈 → 의도치 않은 START/STOP이 생겨 슬레이브가 리셋됨
- NACK이면 STOP을 생략함 → `starts == stops` 불일치 (테스트 1번)
- 예약 주소(0x00–0x07, 0x78–0x7F)까지 스캔 → general call(0x00)로 모든 장치가 반응할 수 있음 (테스트 1번 주소 범위)
- 복구에서 SDA 확인 전에 9번을 무조건 다 줌 → 반환 값(필요한 펄스 수)이 틀림 (테스트 4번 = 5)
- 복구 후 STOP을 안 만듦 → 슬레이브 상태 머신이 "전송 중"으로 남을 수 있음 (테스트 4번 `stops == 1`)
- 버스가 막혔는데 그냥 START를 시도 → `-EBUSY` 대신 엉뚱한 NACK (테스트 4, 6번)
