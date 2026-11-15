---
id: COMM-M12
time: 90분
updated: 2026-11-11
---
# COMM-M12 드라이버 계층화: 버스 추상화로 I2C/SPI 겸용 센서 드라이버

## 학습 목표

- `bus_ops { read_regs, write_regs, recover }` + `ctx` 로 **버스 추상화**를 정의하고, I2C 어댑터(포인터 쓰기 → repeated START → 읽기)와 SPI 어댑터(R/W·MS 비트가 든 명령 바이트 + burst)를 그 뒤에 숨길 수 있다.
- **하나의 IMU 드라이버 소스**(WHO_AM_I → soft reset → 설정 → 되읽기 → STATUS+XYZ burst → mg)가 두 버스에서 한 줄도 안 바뀌고 같은 결과를 내는 것을 테스트로 증명한다.
- 에러 코드를 계층마다 번역한다: HAL 고유 코드 → 공통 버스 코드(`BUS_ENODEV/EIO/ETIMEDOUT/EAGAIN/EINVAL`) → 드라이버 코드(`IMU_ENOTFOUND/EBADID/EIO/ETIMEDOUT/EBUSY`...) → 앱 행동. "아래 코드는 위로 새지 않는다", "재시도는 한 계층에서만" 두 규칙을 말하고 지킨다.
- 장애(주소 NACK, 데이터 NACK, 중재 패배, 타임아웃, SPI 칩 없음 = MISO 0xFF)를 주입해 계층별 코드를 단언하고, burst 중간에 끊겨도 반쪽 데이터가 "유효"로 남지 않게 만든다.
- Linux regmap, Zephyr 센서 드라이버, ST 의 `stmdev_ctx_t` 가 같은 구조라는 것을 보고, 면접에서 "이 드라이버를 I2C 판에서 SPI 판으로 옮겨라"에 설계로 답한다.

## 1. 문제

센서 회사들은 같은 칩을 I2C 와 SPI 둘 다로 판다. 보드 rev A 는 I2C, rev B 는 핀이 모자라 SPI — 이때 드라이버를 두 벌 만드는 팀과 어댑터 하나만 더 쓰는 팀의 차이가 이 레슨이다. 시니어 펌웨어 면접의 설계 라운드에서 "HAL·버스·디바이스 드라이버·앱의 경계를 어디에 긋고, 에러와 재시도는 누가 책임지나?"로 자주 나온다.

COMM-B05 는 SPI 레지스터 칩, COMM-B07 은 I2C 레지스터 칩을 각각 짰다. 두 드라이버를 나란히 놓으면 **레지스터의 의미**(WHO_AM_I 값, 리셋 순서, mg 변환)는 똑같고 **선 위의 바이트**만 다르다. 그 다른 부분만 함수 포인터 표 뒤에 숨기는 것이 버스 추상화다 (함수 포인터 기본은 C-M06, 호스트 테스트용 레지스터 계층은 C-A01, C++ 정적 다형성 판은 CPP-M05).

```svg
<svg viewBox="0 0 760 440" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">계층과 책임 — 호출은 아래로, 에러 코드는 번역되며 위로</text> <rect x="20" y="36" width="440" height="50" rx="6" class="f-mut" stroke="currentColor"/> <text x="240" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">app: app_step()</text> <text x="240" y="76" fill="currentColor" text-anchor="middle" font-size="12">정책: 재초기화 · 버스 복구 · 부재 처리 (버스 종류 모름)</text> <rect x="20" y="106" width="440" height="56" rx="6" class="f-acc" stroke="currentColor"/> <text x="240" y="126" fill="currentColor" text-anchor="middle" font-weight="bold">IMU driver: imu_init / imu_read_accel_mg</text>
  <text x="240" y="146" fill="currentColor" text-anchor="middle" font-size="12">레지스터 의미만 앎 · 재시도는 imu_xfer() 한 곳 (EAGAIN만)</text> <text x="240" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">bus_t { ops, ctx }</text> <rect x="20" y="186" width="210" height="50" rx="6" class="f-ok" stroke="currentColor"/> <text x="125" y="206" fill="currentColor" text-anchor="middle" font-weight="bold">i2c_read/write_regs</text> <text x="125" y="226" fill="currentColor" text-anchor="middle" font-size="12">reg 쓰기 + Sr + 읽기</text> <rect x="250" y="186" width="210" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="355" y="206" fill="currentColor" text-anchor="middle" font-weight="bold">spi_read/write_regs</text> <text x="355" y="226" fill="currentColor" text-anchor="middle" font-size="12">0x80|0x40|reg + burst, CS</text> <rect x="20" y="256" width="210" height="50" rx="6" class="f-mut" stroke="currentColor"/> <text x="125" y="276" fill="currentColor" text-anchor="middle" font-weight="bold">I2C HAL (컨트롤러)</text> <text x="125" y="296" fill="currentColor" text-anchor="middle" font-size="12">S/Sr/P, ACK, ARLO, timeout</text> <rect x="250" y="256" width="210" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="355" y="276" fill="currentColor" text-anchor="middle" font-weight="bold">SPI HAL (컨트롤러)</text> <text x="355" y="296" fill="currentColor" text-anchor="middle" font-size="12">CS, 전이중 xfer, timeout</text> <rect x="20" y="326" width="440" height="44" rx="6" class="f-warn" stroke="currentColor"/> <text x="240" y="346" fill="currentColor" text-anchor="middle" font-weight="bold">MIMU-6D (mock 칩, I2C 0x6A / SPI 4선)</text> <text x="240" y="362" fill="currentColor" text-anchor="middle" font-size="12">같은 레지스터 맵 하나</text> <path d="M240 86 V104" stroke="currentColor" marker-end="url(#m12a)"/>
  <path d="M125 162 V184" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M355 162 V184" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M125 236 V254" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M355 236 V254" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M125 306 V324" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M355 306 V324" stroke="currentColor" marker-end="url(#m12a)"/> <text x="610" y="46" fill="currentColor" text-anchor="middle" font-weight="bold">예: 주소 NACK / 중재 패배 / 붙은 SDA</text> <rect x="490" y="56" width="240" height="40" rx="4" class="f-mut" stroke="currentColor"/> <text x="610" y="72" fill="currentColor" text-anchor="middle" font-size="12">absent → 4스텝마다 probe</text>
  <text x="610" y="88" fill="currentColor" text-anchor="middle" font-size="12">(안 보임) · 복구 + 3회면 재초기화</text> <rect x="490" y="116" width="240" height="40" rx="4" class="f-acc" stroke="currentColor"/> <text x="610" y="140" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">IMU_ENOTFOUND / OK* / ETIMEDOUT</text> <rect x="490" y="191" width="240" height="40" rx="4" class="f-ok" stroke="currentColor"/> <text x="610" y="215" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">BUS_ENODEV / EAGAIN / ETIMEDOUT</text> <rect x="490" y="261" width="240" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="610" y="285" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">0x10 / 0x12 / 0x13 (I2C HAL)</text> <path d="M610 259 V233" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M610 189 V158" stroke="currentColor" marker-end="url(#m12a)"/> <path d="M610 114 V98" stroke="currentColor" marker-end="url(#m12a)"/> <text x="10" y="392" fill="currentColor" font-size="12">* 중재 패배는 드라이버가 재시도로 삼킨다 (계속 지면 IMU_EBUSY).</text> <text x="10" y="410" fill="currentColor" font-size="12">코드 도메인: HAL 양수 · BUS −1..−5 · IMU −101..−108 — 섞이면 테스트가 잡는다.</text> <text x="10" y="428" fill="currentColor" font-size="12">계층마다 "자기 말"로 번역하고, 원래 코드는 진단용(last_bus_err, 로그)으로만 남긴다.</text>
</svg>
```

이 레슨의 mock 칩 **MIMU-6D** (값과 비트는 이 레슨의 정의, 배치는 ST LSM6/LIS3DH 계열을 본뜸 — COMM-B05 MIMU-6 의 듀얼 인터페이스 판):

| 항목 | 정의 |
|---|---|
| 레지스터 | 6비트 주소. 0x00–0x07 RW 설정 영역, 0x0F WHO_AM_I = 0xB5 (RO), 0x10 CTRL1 `[7:4] ODR(0..10) [3:2] FS`, 0x11 CTRL2, 0x12 CTRL3 bit0 SW_RESET, 0x27 STATUS bit0 XLDA (bit7..1 예약 = 0), 0x28–0x2D OUT X/Y/Z (LE int16) |
| 부수효과 | OUT_Z_H(0x2D) 읽기 → XLDA = 0. SW_RESET 쓰기 → 전 레지스터 기본값, CTRL3 bit0 이 `reset_polls` 번 읽힐 동안 1 |
| I2C | 7비트 주소 0x6A (와이어 0xD4 쓰기 / 0xD5 읽기). 포인터는 **항상 자동 증가** (0x3F 다음 0x00) |
| SPI | 모드 0, 명령 바이트 = bit7 R/W(1 = 읽기), **bit6 MS(1 = 자동 증가)**, bit5..0 주소. 더미 바이트 없음 (연습 4 에서 추가) |
| mg | `raw × (2000 << FS) / 32768`, 0 쪽으로 버림 (COMM-B05 와 같음) |

### Part A — bus_ops 두 개와 버스를 모르는 드라이버

- `i2c_read_regs(ctx, reg, buf, n)` = `S addr+W reg Sr addr+R d0..dn-1 P`, `i2c_write_regs` = `S addr+W reg d0.. P` (COMM-B07 의 repeated START). `spi_read_regs` = `CS↓ [0x80 | (n>1 ? 0x40 : 0) | reg] [n 바이트] CS↑`, 쓰기는 bit7 = 0 (COMM-B05).
- 계약: 반환은 `0` 또는 `BUS_E*` 뿐. `n` 은 1..32, 잘못된 인자는 `BUS_EINVAL` 이고 **선을 건드리지 않는다**. SPI 는 6비트 주소라 `reg + n > 0x40` 도 거부. 어댑터는 재시도하지 않는다.
- 드라이버 `imu_probe`, `imu_config`, `imu_read_accel_mg` 는 `imu_xfer()` 하나로만 버스에 간다. **같은 대본**(init → 샘플 5개 중 1개는 새 샘플 없음 → FS 변경 → 잘못된 인자 → probe)을 두 버스에서 돌린 드라이버 출력이 글자 하나까지 같아야 한다.
- 테스트: 와이어 바이트 정확히, 쓰기 가능 레지스터 10개 × 값 256 × 버스 2 전수, 랜덤 burst 3000 × 2, mg 변환 500 × FS 4 × 버스 2.

### Part B — 에러 번역, 재시도 정책, 장애 주입

- `i2c_map_err` / `spi_map_err` (HAL → BUS), `imu_map_err` (BUS → IMU). **전사적(total)**: 모르는 HAL 코드(0..255 전부)도 반드시 BUS 코드로, 실패를 성공으로 바꾸지 않는다.
- `imu_xfer`: 재시도는 **여기 한 곳**, `BUS_EAGAIN`(중재 패배 = 우리 트랜잭션은 일어나지 않았음)만, 첫 시도 포함 최대 3번. 타임아웃·EIO 는 재시도하지 않고 위로 (앱이 정책을 고른다).
- 장애 표 18줄: I2C 주소 NACK(일시/칩 없음), 데이터 NACK, ARLO(1번/계속), burst 중간 타임아웃, SDA stuck, 다른 칩, 쓰기 보호, 리셋이 안 끝남 / SPI 칩 없음(MISO 0xFF), MISO 0x00, 동작 중 MISO 끊김, burst 중간 타임아웃, 쓰기 중 타임아웃 … 각 줄마다 HAL·BUS·IMU 코드, 버스 호출 수, CS/STOP 상태, mg 불변을 단언.
- 랜덤 장애 퍼징 4000번: 코드 도메인, 선 상태, 실패 시 출력 불변, 성공 시 값 정확.

### Part C — 초기화 순서와 앱 정책

- `imu_init`: probe → SW_RESET → CTRL3 폴링(최대 5번) → 설정 + 되읽기 → 그때만 `ready`.
- `app_step` (1 ms 메인 루프에서 부른다고 가정): `INIT / RUN / ABSENT` 상태 기계. NOTFOUND·BADID → ABSENT 로 4스텝마다 재probe, ETIMEDOUT → `recover` op 가 있으면 버스 복구(COMM-M03), 연속 3번 실패 → 재초기화, NOTREADY 는 실패가 아님. 시나리오 두 개(I2C: 부팅 때 센서 없음 → 전원 → SDA stuck → ARLO 2번 → 타임아웃 3번 / SPI: 타임아웃 3번 → 커넥터 빠짐 → 다시 꽂힘)의 로그를 bring-up 엔지니어가 읽을 수 있게 남긴다.

## 2. Problem (English)

> **One sensor driver, two buses.**
>
> The IMU "MIMU-6D" is sold with both I2C (7-bit address 0x6A, register pointer auto-increments) and 4-wire SPI (mode 0; command byte = bit 7 read, bit 6 auto-increment, bits 5..0 register). Write the driver once.
>
> ```c
> typedef struct bus_ops {
>     const char *name;
>     int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
>     int (*write_regs)(void *ctx, uint8_t reg, const uint8_t *buf, size_t n);
>     int (*recover)(void *ctx);                      /* optional, may be NULL */
> } bus_ops;
> typedef struct { const bus_ops *ops; void *ctx; } bus_t;
>
> int imu_init(imu_t *imu, bus_t bus, uint8_t odr, uint8_t fs);   /* probe, soft reset, configure, verify */
> int imu_read_accel_mg(imu_t *imu, int32_t mg[3]);               /* one 7-byte burst: STATUS + XYZ */
> ```
>
> 1. Implement `read_regs`/`write_regs` for an I2C controller HAL (`i2c_hal_xfer(addr, w, wn, r, rn)` → `I2C_HAL_*`) and an SPI controller HAL (`spi_hal_cs`, `spi_hal_xfer` → `SPI_HAL_*`). Bus ops return only `0` or `BUS_E*`, never retry, reject `n == 0 || n > 32` without touching the wire, and always release CS.
> 2. Define and implement the error translation HAL → bus → driver. Arbitration loss must be retried in exactly one place. Explain where and why.
> 3. A failure in the middle of the burst must not change the caller's output. An SPI device that is absent (MISO pulled up → 0xFF) must be reported as "not found", not as a valid zero sample.
> 4. Write an application step function that decides when to re-probe, run bus recovery, and re-initialize.
>
> Constraints: no dynamic allocation, the driver source contains no `if (bus is SPI)`.
>
> Example (wire log, X/Y/Z = 1000/2000/3000 raw, FS = ±2 g):
>
> ```text
> i2c: S D4 A 27 A Sr D5 A 01 A E8 A 03 A D0 A 07 A B8 A 0B N P
> spi: [ E7/C2 00/01 00/E8 00/03 00/D0 00/07 00/B8 00/0B ]
> both -> imu_read_accel_mg = OK, mg = {61, 122, 183}
> ```
>
> Follow-ups: Where does the bus lock live? What would you change for Linux (regmap) or Zephyr? Why not retry timeouts in every layer? How do you detect a missing SPI device?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열세 군데: Part A — `i2c_read_regs`, `i2c_write_regs`, `spi_read_regs`, `spi_write_regs`, `imu_probe`, `imu_config`, `imu_read_accel_mg` / Part B — `i2c_map_err`, `spi_map_err`, `imu_map_err`, `imu_xfer` / Part C — `imu_init`, `app_step`. mock 칩(`ImuCore`), 두 HAL(로그 포함), 장애 주입(`Fault`), `imu_recover_bus`, `app_try_recover`, 하네스는 주어진다. 스텁은 선을 건드리지 않고 `BUS_EIO` / `IMU_EIO` / `IMU_EINVAL` 을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:477  i2c_read_regs(&g_i2c_dev, R_WHO, buf, 1) == BUS_OK && buf[0] == IMU_ID
FAIL starter.c:478  strcmp(g_wire.s, "S D4 A 0F A Sr D5 A B5 N P\n") == 0
i2c read WHO_AM_I : (no bus activity)
…
66/179 passed
```

통과하는 66개는 mock 자체 점검, 스텁이 선을 안 건드려서 우연히 맞는 단언("CS 가 올라가 있다", "STOP 뒤다"), 실패 시 mg 가 안 바뀌었다는 단언 등이다.

## 4. 힌트

<details>
<summary>힌트 1 — I2C 읽기는 HAL 호출 한 번</summary>

`i2c_hal_xfer(hal, addr7, &reg, 1, buf, n)` — 쓰기 1바이트(포인터) 뒤에 HAL 이 알아서 Sr 을 넣는다. 쓰기는 `tmp[0] = reg; memcpy(tmp + 1, buf, n)` 로 **한 트랜잭션**에 보낸다. 포인터 쓰기와 데이터 쓰기를 HAL 호출 두 번으로 나누면 그 사이에 STOP 이 들어가 다른 마스터나 태스크가 끼어들 수 있다 (COMM-B07).

</details>

<details>
<summary>힌트 2 — SPI 는 어댑터가 명령 바이트를 만든다</summary>

`cmd = read_flag | (n > 1 ? inc_flag : 0) | reg`. 이 비트들은 **버스 + 칩 조합의 사실**이라 ctx(`SpiDev.read_flag/inc_flag`)에 둔다 — Linux regmap 의 `read_flag_mask` 와 같은 자리. `tx[0] = cmd`, 나머지 0 으로 1 + n 바이트를 한 번에 xfer 하고 `rx + 1` 부터 복사. `spi_hal_cs(hal, 1)` 은 xfer 결과와 상관없이 **항상** 부른다.

</details>

<details>
<summary>힌트 3 — 번역 함수는 switch + default</summary>

`default:` 가 없으면 HAL 이 새 코드를 추가하는 날 그 값이 그대로 위로 샌다. `default: return BUS_EIO;` 로 "모르는 실패 = I/O 에러". 하네스는 0..255 전부를 넣어 결과가 BUS 도메인인지, 0 이 아닌 입력이 `BUS_OK` 로 바뀌지 않는지 본다.

</details>

<details>
<summary>힌트 4 — imu_xfer 의 루프</summary>

`for (a = 0; a < 3; a++) { if (a) retries++; attempts++; rc = 호출; if (rc != BUS_EAGAIN) break; }` → `last_bus_err = rc; return imu_map_err(rc);`. 쓰기/읽기를 `wbuf != NULL` 로 가른다.

</details>

<details>
<summary>힌트 5 — burst 는 지역 버퍼로, 커밋은 마지막에</summary>

`uint8_t b[7]` 로 읽고 rc 가 OK 일 때만 해석한다. 실패한 버퍼는 일부만 채워졌을 수 있다 (I2C HAL 은 받은 바이트를 바로 `buf` 에 쓴다). 해석 순서: `b[0] == 0xFF` → NOTFOUND (예약 비트까지 1 = MISO 가 떠 있음), XLDA = 0 → NOTREADY, 아니면 세 축을 지역 배열에 계산해 `memcpy` 로 한꺼번에.

</details>

<details>
<summary>힌트 6 — 앱은 상태 세 개</summary>

ABSENT 이면 `t < next_try` 동안 아무것도 안 하고, 시간이 되면 INIT 로 넘어가 **같은 스텝에서** init 을 시도한다. RUN 에서 실패가 나면 `fails++`, ETIMEDOUT 이면 `app_try_recover`, `fails >= 3` 이면 INIT 로. 성공하면 `fails = 0` (연속 실패만 센다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "칩마다 다른 건 **레지스터 의미**, 버스마다 다른 건 **바이트를 어떻게 싣느냐**. 둘을 `read_regs/write_regs` 경계에서 자른다."
2. "버스 어댑터는 HAL 코드를 공통 코드로 번역하고, 재시도는 안 한다. 드라이버는 공통 코드를 자기 코드로 번역하고, 중재 패배만 재시도한다. 복구·재초기화는 앱 정책."
3. "SPI 는 NACK 이 없어서 칩 부재를 버스가 모른다 — 드라이버가 WHO_AM_I 0x00/0xFF 와 STATUS 예약 비트로 판단한다."
4. "burst 실패는 출력을 건드리지 않는다. 축별로 따로 읽지 않는다."
5. 꼬리질문 대비: 간접 호출 비용(아래 asm), 버스 락의 위치, Linux regmap / Zephyr / ST 드라이버 대응.

### Part A — 같은 호출, 다른 바이트

드라이버가 하는 일은 `imu_rd(imu, 0x27, b, 7)` 한 줄이다. 두 어댑터가 그 한 줄을 선 위에서 이렇게 바꾼다 (실행 로그 그대로, 샘플 X/Y/Z = 1000/2000/3000):

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">read_regs(ctx, 0x27, buf, 7) — 같은 드라이버 호출이 두 버스에서 만드는 바이트</text> <text x="20" y="58" fill="currentColor">(a) I2C: 포인터 쓰기 → Sr → 7바이트 읽기 (마지막만 NACK) → STOP</text> <rect x="20" y="70" width="22" height="24" class="f-mut" stroke="currentColor"/><text x="31" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">S</text> <rect x="42" y="70" width="32" height="24" class="f-acc" stroke="currentColor"/><text x="58" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">D4</text> <rect x="74" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="85" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="96" y="70" width="32" height="24" class="f-acc" stroke="currentColor"/><text x="112" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">27</text>
  <rect x="128" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="139" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="150" y="70" width="26" height="24" class="f-mut" stroke="currentColor"/><text x="163" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">Sr</text> <rect x="176" y="70" width="32" height="24" class="f-acc" stroke="currentColor"/><text x="192" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">D5</text> <rect x="208" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="219" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="230" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="246" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="262" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="273" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
  <rect x="284" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="300" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">E8</text> <rect x="316" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="327" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="338" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="354" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">03</text> <rect x="370" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="381" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="392" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="408" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">D0</text> <rect x="424" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="435" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text>
  <rect x="446" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="462" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">07</text> <rect x="478" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="489" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="500" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="516" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">B8</text> <rect x="532" y="70" width="22" height="24" class="f-ok" stroke="currentColor"/><text x="543" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A</text> <rect x="554" y="70" width="32" height="24" class="f-ok" stroke="currentColor"/><text x="570" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0B</text> <rect x="586" y="70" width="22" height="24" class="f-acc" stroke="currentColor"/><text x="597" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">N</text>
  <rect x="608" y="70" width="22" height="24" class="f-mut" stroke="currentColor"/><text x="619" y="86" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">P</text> <text x="69" y="112" fill="currentColor" text-anchor="middle" font-size="12">주소 0x6A+W</text> <text x="112" y="128" fill="currentColor" text-anchor="middle" font-size="12">reg 0x27</text> <text x="203" y="112" fill="currentColor" text-anchor="middle" font-size="12">0x6A+R</text> <text x="430" y="128" fill="currentColor" text-anchor="middle" font-size="12">STATUS, X_L, X_H, Y_L, Y_H, Z_L, Z_H (칩이 SDA 를 구동, 마스터가 ACK)</text> <text x="20" y="168" fill="currentColor">(b) SPI 모드 0: CS 프레임 하나, 바이트마다 MOSI/MISO</text>
  <text x="34" y="207" fill="currentColor" text-anchor="middle" font-size="12">CS↓</text> <line x1="56" y1="184" x2="56" y2="220" stroke="currentColor" stroke-width="2"/> <rect x="60" y="190" width="58" height="24" class="f-acc" stroke="currentColor"/><text x="89" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">E7/C2</text> <rect x="118" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="147" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/01</text> <rect x="176" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="205" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/E8</text> <rect x="234" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="263" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/03</text>
  <rect x="292" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="321" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/D0</text> <rect x="350" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="379" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/07</text> <rect x="408" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="437" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/B8</text> <rect x="466" y="190" width="58" height="24" class="f-ok" stroke="currentColor"/><text x="495" y="206" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00/0B</text> <line x1="528" y1="184" x2="528" y2="220" stroke="currentColor" stroke-width="2"/> <text x="550" y="207" fill="currentColor" text-anchor="middle" font-size="12">CS↑</text>
  <text x="89" y="236" fill="currentColor" text-anchor="middle" font-size="12">0x80|0x40|0x27</text> <text x="89" y="252" fill="currentColor" text-anchor="middle" font-size="12">MISO C2 = 쓰레기</text> <text x="350" y="240" fill="currentColor" text-anchor="middle" font-size="12">MOSI 0x00 은 클럭을 만들기 위한 바이트, 데이터는 MISO</text> <rect x="20" y="268" width="720" height="50" rx="6" class="f-mut" stroke="currentColor"/> <text x="380" y="288" fill="currentColor" text-anchor="middle">드라이버가 받는 7바이트 (두 버스 동일): 01 E8 03 D0 07 B8 0B</text> <text x="380" y="308" fill="currentColor" text-anchor="middle">→ XLDA=1, X=0x03E8=1000, Y=0x07D0=2000, Z=0x0BB8=3000 → mg {61, 122, 183} (FS ±2 g)</text>
</svg>
```

어댑터 코드는 이 그림을 그대로 옮긴 것이다. 실제 로그 (solution.c 출력):

```text
i2c read WHO_AM_I : S D4 A 0F A Sr D5 A B5 N P
spi read WHO_AM_I : [ 8F/FD 00/B5 ]
i2c read 6 @0x28  : S D4 A 28 A Sr D5 A 01 A 00 A FE A FF A 2C A 01 N P
spi read 6 @0x28  : [ E8/2C 00/01 00/00 00/FE 00/FF 00/2C 00/01 ]
i2c write 3 @0x02 : S D4 A 02 A 11 A 22 A 33 A P
spi write 3 @0x02 : [ 42/17 11/AE 22/A1 33/60 ]
```

| | I2C 어댑터 | SPI 어댑터 |
|---|---|---|
| 읽기 1바이트 | `S D4 A reg A Sr D5 A d N P` (주소 2번, 바이트 4개) | `[ (0x80\|reg)/.. 00/d ]` (바이트 2개) |
| burst 의 자동 증가 | 칩이 항상 증가 → 어댑터는 아무것도 안 함 | 명령 bit6(MS)을 **n > 1 일 때만** 켬 → `0x8F` vs `0xE8` |
| 쓰기 | `[reg, data…]` 를 한 트랜잭션으로 | 명령 bit7 = 0 (`0x42` = MS + 0x02) |
| 칩 부재 | 주소 NACK → `BUS_ENODEV` | 알 수 없음 → `BUS_OK`, 데이터 0xFF |
| 에러 때 선 정리 | HAL 이 STOP (ARLO 는 물러남) | 어댑터가 **반드시** CS↑ |

드라이버 쪽은 `imu_xfer` 하나로만 나가므로 버스 분기가 없다. 그 증거가 "같은 대본 → 같은 출력" 테스트다 (실제 출력, I2C 와 SPI 에서 `strcmp == 0`):

```text
driver output (identical on i2c and spi):
init=OK
accel=OK 1000 -1000 0
accel=OK 0 0 1999
accel=IMU_ENOTREADY 0 0 1999
accel=OK -2000 753 0
accel=OK 6 12 18
config(4,3)=OK
accel=OK 15999 -16000 0
config(11,0)=IMU_EINVAL probe=OK
```

세 번째 줄: 새 샘플이 없으면 NOTREADY 이고 출력 배열은 **이전 값 그대로**(0 0 1999). 마지막 줄: 잘못된 인자는 버스에 가기 전에 거부.

드라이버 핵심 (solution.c):

```c
static int imu_read_accel_mg(imu_t *imu, int32_t mg[3]) {
    if (!imu || !mg || !imu->ready) return IMU_EINVAL;
    uint8_t b[7];
    int rc = imu_rd(imu, R_STATUS, b, sizeof b);
    if (rc != IMU_OK) return rc;                       /* b 는 일부만 채워졌을 수 있음 → 버린다 */
    if (b[0] == 0xFFu) return IMU_ENOTFOUND;           /* 예약 비트까지 1: SPI MISO 가 떠 있다는 신호 */
    if (!(b[0] & STATUS_XLDA)) return IMU_ENOTREADY;
    ...                                                /* out[3] 에 계산 */
    memcpy(mg, out, sizeof out);                       /* 세 축을 한꺼번에 커밋 */
```

### Part B — 에러 번역표와 재시도 위치

번역 규칙 (이 레슨의 정의; Linux I2C 관례는 참고자료의 fault-codes 문서 — 주소 NACK = `-ENXIO`, 중재 패배 = `-EAGAIN`, 시간 초과 = `-ETIMEDOUT`, I/O 전 인자 오류 = `-EINVAL`):

| HAL (버스 고유) | BUS (공통) | IMU (드라이버) | 누가 무엇을 |
|---|---|---|---|
| I2C NACK on address | `BUS_ENODEV` | `IMU_ENOTFOUND` | 앱: ABSENT, 주기적 재probe |
| I2C NACK on data | `BUS_EIO` | `IMU_EIO` | 앱: 세고, 연속 3번이면 재초기화 |
| I2C arbitration lost | `BUS_EAGAIN` | (재시도) → `IMU_EBUSY` | **드라이버 `imu_xfer` 만** 재시도 (3번) |
| I2C timeout / SDA stuck | `BUS_ETIMEDOUT` | `IMU_ETIMEDOUT` | 앱: `recover` op (9클럭 + STOP, COMM-M03) |
| SPI timeout | `BUS_ETIMEDOUT` | `IMU_ETIMEDOUT` | 앱: recover 없음 → 재초기화 |
| SPI 칩 없음 (MISO 0xFF/0x00) | `BUS_OK` (버스는 모름) | probe: `IMU_ENOTFOUND` / 읽기: STATUS 0xFF → `IMU_ENOTFOUND` | 드라이버가 값의 그럴듯함으로 판단 |
| (없음) WHO_AM_I 불일치 | — | `IMU_EBADID` | 앱: 멈춤 (BOM·스트랩 확인) |
| (없음) 되읽기 불일치 / 리셋 안 끝남 | — | `IMU_EIO` / `IMU_ETIMEDOUT` | 앱: 재초기화 |
| 잘못된 인자 | `BUS_EINVAL` | `IMU_EINVAL` | 코드 버그 — 재시도 금지 |

하네스의 장애 표가 이 표를 한 줄씩 실행한다. 실제 출력 (att = 그 동작 동안의 버스 호출 수):

```text
fault                    HAL             bus            driver         att
i2c addr NACK (1x)       I2C_NACK_ADDR   BUS_ENODEV     IMU_ENOTFOUND  1
i2c no device            I2C_NACK_ADDR   BUS_ENODEV     IMU_ENOTFOUND  1
i2c data NACK @1         I2C_NACK_DATA   BUS_EIO        IMU_EIO        1
i2c ARLO (1x)            HAL_OK          OK             OK             2
i2c ARLO (always)        I2C_ARLO        BUS_EAGAIN     IMU_EBUSY      3
i2c timeout mid-burst    I2C_TIMEOUT     BUS_ETIMEDOUT  IMU_ETIMEDOUT  1
    wire: S D4 A 27 A Sr D5 A 01 A E8 A 03 A T P
i2c SDA stuck            I2C_TIMEOUT     BUS_ETIMEDOUT  IMU_ETIMEDOUT  1
i2c wrong chip 0x33      HAL_OK          OK             IMU_EBADID     1
i2c write-protected      HAL_OK          OK             IMU_EIO        5
i2c reset never ends     HAL_OK          OK             IMU_ETIMEDOUT  7
spi no device (0xFF)     HAL_OK          OK             IMU_ENOTFOUND  1
spi MISO stuck low       HAL_OK          OK             IMU_ENOTFOUND  1
spi MISO open (run)      HAL_OK          OK             IMU_ENOTFOUND  1
spi timeout mid-burst    SPI_TIMEOUT     BUS_ETIMEDOUT  IMU_ETIMEDOUT  1
    wire: [ E7/C2 00/01 00/E8 T! ]
spi timeout on write     SPI_TIMEOUT     BUS_ETIMEDOUT  IMU_ETIMEDOUT  1
spi wrong chip 0x33      HAL_OK          OK             IMU_EBADID     1
spi write-protected      HAL_OK          OK             IMU_EIO        5
spi reset never ends     HAL_OK          OK             IMU_ETIMEDOUT  7
```

읽는 법:

- **mid-burst** 두 줄: STATUS·X_L·X_H 까지 온 뒤 끊겼다. I2C HAL 은 그 3바이트를 이미 `b[]` 에 썼다. 하네스는 mg 를 777 로 채워 두고 실패 뒤에도 777 인지 단언한다 — 반쪽 샘플이 앱에 안 간다. SPI 줄의 `]` 는 어댑터가 에러 경로에서도 CS 를 올렸다는 표시이고, 모든 줄에서 "CS high + I2C STOP 뒤" 가 단언된다.
- **ARLO** 두 줄: 한 번 지면 두 번째 시도에 성공(att 2, 앱은 모름). 계속 지면 정확히 3번 시도 후 `IMU_EBUSY`. 중재 패배는 "다른 마스터의 트랜잭션이 대신 일어났고 우리 것은 칩에 닿지 않았다"(COMM-M02)이므로 다시 보내도 안전하다.
- **SPI 칩 없음** 세 줄: HAL 도 버스도 OK 다. 이 비대칭이 면접 포인트 — I2C 는 프로토콜에 ACK 가 있어 "아무도 없음"을 버스가 알지만, SPI 는 MISO 가 풀업이면 0xFF, 풀다운이거나 붙어 있으면 0x00 이 "정상 데이터"로 들어온다. 그래서 판단은 레지스터 의미를 아는 드라이버 몫이다. 동작 중 MISO 가 끊기면(커넥터 빠짐) STATUS = 0xFF(XLDA 포함 모든 비트 1)이고 X/Y/Z = 0xFFFF = −1 → mg 0 이 "정상 샘플"로 나갈 뻔했다. 예약 비트 검사 한 줄이 그걸 막는다.
- **write-protected**: 버스는 모두 성공했지만 되읽기가 다르다 → `IMU_EIO`. 쓰기 뒤 되읽기가 없으면 이 고장은 "설정이 안 먹은 센서"로 현장에 나간다.

**재시도는 왜 한 곳, 왜 EAGAIN 만?** 각 계층이 "실패하면 3번"을 하면 곱해진다. 연습 1 의 실제 측정: SDA 가 붙은 버스에서 앱 3 × 드라이버 3 × 버스 3 = **HAL 트랜잭션 27번**, 타임아웃이 25 ms 라 가정하면 675 ms 동안 메인 루프가 멈춘다 (우리 구조는 1번, 25 ms). 그리고 타임아웃·데이터 NACK 은 "칩이 일부를 이미 처리했을 수 있는" 실패다. FIFO 나 clear-on-read 레지스터(이 칩의 OUT_Z_H 읽기 = XLDA 클리어)를 맹목적으로 다시 읽으면 샘플을 잃거나 두 번 쓴다. 재시도해도 되는지는 **실패의 의미(버스)** 와 **레지스터의 의미(드라이버)** 를 둘 다 알아야 판단할 수 있고, 그 둘이 만나는 곳이 `imu_xfer` 다. 버스는 "EAGAIN = 안 일어났음"이라는 정보를 코드로 올려 주는 것까지가 책임이다.

번역 함수의 전사성은 하네스가 HAL 코드 0..255 전부로 확인한다. 연습 5 는 X-macro(C-A07)로 IMU 코드 → 이름 → 앱 행동 표를 **코드에서 생성**해, 문서와 코드가 어긋날 수 없게 한다.

랜덤 장애 퍼징 4000번(버스·장애 종류·위치·횟수·동작 무작위)에서 단언하는 불변식: 모든 `bus_ops` 반환은 BUS 도메인, 모든 드라이버 반환은 IMU 도메인, 동작 뒤 CS high + I2C STOP 뒤, 실패한 `imu_read_accel_mg` 는 출력 불변, 성공한 것은 기준값(64비트 계산)과 일치, 동작 하나당 버스 호출 ≤ 6. 위반 0.

### Part C — 초기화 순서와 앱 정책

`imu_init` 의 실제 와이어 로그 (`reset_polls = 2`: 리셋이 두 번 읽힐 동안 진행 중):

```text
i2c imu_init wire log:              spi imu_init wire log:
S D4 A 0F A Sr D5 A B5 N P          [ 8F/FD 00/B5 ]          WHO_AM_I = 0xB5
S D4 A 12 A 01 A P                  [ 12/2C 01/17 ]          CTRL3 = SW_RESET
S D4 A 12 A Sr D5 A 01 N P          [ 92/AE 00/01 ]          폴링 1: 아직 1
S D4 A 12 A Sr D5 A 01 N P          [ 92/A1 00/01 ]          폴링 2: 아직 1
S D4 A 12 A Sr D5 A 00 N P          [ 92/60 00/00 ]          폴링 3: 끝
S D4 A 10 A 44 A P                  [ 10/1B 44/C2 ]          CTRL1 = ODR 4, FS 1
S D4 A 10 A Sr D5 A 44 N P          [ 90/05 00/44 ]          되읽기 = 0x44 → ready
```

(두 로그는 각각 실제 출력이고, 나란히 놓으려고 열만 맞췄다.) 순서의 이유: ① 리셋보다 ID 확인이 먼저 — 다른 칩의 0x12 에 0x01 을 쓰면 무슨 일이 날지 모른다. ② 리셋 직후의 설정 쓰기는 칩이 무시할 수 있다 → 완료 비트를 폴링 (실제 칩은 폴링 사이에 ms 지연, ST 드라이버의 `mdelay` 콜백이 그 자리). 폴링 한계 테스트: `reset_polls` 0..4 → OK, 5·6 → `IMU_ETIMEDOUT` (최대 5번 읽기). ③ 설정은 되읽어 확인. ④ 그 다음에만 `ready = true` — 어느 단계에서 실패해도 반쯤 설정된 드라이버가 "준비됨"으로 남지 않는다 (SW_RESET 쓰기에 data NACK 을 넣은 테스트: `IMU_EIO`, `ready == false`, 칩의 리셋 횟수 0).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m12b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">app_step() 상태 기계 (1 스텝 = 메인 루프 1회)</text> <rect x="250" y="44" width="260" height="54" rx="10" class="f-acc" stroke="currentColor"/> <text x="380" y="66" fill="currentColor" text-anchor="middle" font-weight="bold">INIT — imu_init()</text> <text x="380" y="86" fill="currentColor" text-anchor="middle" font-size="12">그 밖의 실패: 머묾 (ETIMEDOUT→recover)</text> <rect x="570" y="230" width="170" height="56" rx="10" class="f-ok" stroke="currentColor"/> <text x="655" y="254" fill="currentColor" text-anchor="middle" font-weight="bold">RUN</text>
  <text x="655" y="274" fill="currentColor" text-anchor="middle" font-size="12">imu_read_accel_mg()</text> <rect x="20" y="230" width="170" height="56" rx="10" class="f-mut" stroke="currentColor"/> <text x="105" y="254" fill="currentColor" text-anchor="middle" font-weight="bold">ABSENT</text> <text x="105" y="274" fill="currentColor" text-anchor="middle" font-size="12">t ≥ next_try 까지 대기</text> <path d="M510 70 C590 70 650 120 655 228" stroke="currentColor" fill="none" marker-end="url(#m12b)"/> <text x="590" y="60" fill="currentColor" font-size="12">init OK</text>
  <path d="M590 230 C560 180 510 140 470 100" stroke="currentColor" fill="none" marker-end="url(#m12b)"/> <text x="530" y="205" fill="currentColor" font-size="12" text-anchor="end">연속 실패 3번</text> <text x="530" y="221" fill="currentColor" font-size="12" text-anchor="end">→ reinit</text> <path d="M250 70 C170 70 115 120 105 228" stroke="currentColor" fill="none" marker-end="url(#m12b)"/> <text x="170" y="40" fill="currentColor" font-size="12" text-anchor="middle">NOTFOUND / BADID</text> <text x="170" y="56" fill="currentColor" font-size="12" text-anchor="middle">또는 init 실패 3번</text>
  <path d="M170 230 C210 180 250 140 290 100" stroke="currentColor" fill="none" marker-end="url(#m12b)"/> <text x="225" y="185" fill="currentColor" font-size="12">4 스텝 뒤 다시</text> <path d="M570 266 H194" stroke="currentColor" fill="none" marker-end="url(#m12b)"/> <text x="380" y="258" fill="currentColor" text-anchor="middle" font-size="12">읽기 NOTFOUND (SPI: STATUS 0xFF)</text> <text x="740" y="306" fill="currentColor" text-anchor="end" font-size="12">RUN 에 머묾: OK · NOTREADY(실패 아님)</text> <text x="740" y="322" fill="currentColor" text-anchor="end" font-size="12">EIO/EBUSY/ETIMEDOUT → fails++ (+recover)</text>
</svg>
```

시나리오 실행 결과 (실제 출력). I2C: t=0..5 센서 전원 없음, t=6 전원, t=14 SDA stuck, t=17 ARLO 2번, t=20 새 샘플 없음, t=22 부터 타임아웃 3번.

```text
app log (i2c):
t=0   init IMU_ENOTFOUND -> absent, retry at t=4
t=4   init IMU_ENOTFOUND -> absent, retry at t=8
t=8   init OK on i2c
t=14  read IMU_ETIMEDOUT (1/3)
      bus recover -> OK
t=22  read IMU_ETIMEDOUT (1/3)
      bus recover -> OK
t=23  read IMU_ETIMEDOUT (2/3)
      bus recover -> OK
t=24  read IMU_ETIMEDOUT (3/3)
      bus recover -> OK
t=24  -> reinit
t=25  init OK on i2c
```

- t=6 에 전원이 들어왔지만 다음 probe 는 t=8 — 재probe 주기는 앱의 정책이고, 실제로는 수백 ms~초 단위로 둔다 (부재 칩에 매 ms NACK 을 받는 건 버스 낭비).
- t=14 의 stuck 은 복구 한 번으로 풀려 t=15 부터 정상 (`fails` 는 성공 때 0).
- t=17 의 ARLO 2번은 **로그에 없다** — 드라이버 안에서 재시도로 끝났고(`imu.retries == 2` 단언) 앱이 알 필요가 없다.
- t=20 NOTREADY 는 실패가 아니라 카운터만 (`notready == 1`).
- 샘플 수 25 = 40 스텝 − (t=0..8 의 9) − t=14 − t=20 − t=22..24 − t=25 (하네스가 단언).

SPI: t=3 부터 타임아웃 3번, t=12 커넥터 빠짐(MISO 떠 있음), t=20 다시 꽂힘.

```text
app log (spi):
t=0   init OK on spi
t=3   read IMU_ETIMEDOUT (1/3)
      (spi: no bus recovery op)
t=4   read IMU_ETIMEDOUT (2/3)
      (spi: no bus recovery op)
t=5   read IMU_ETIMEDOUT (3/3)
      (spi: no bus recovery op)
t=5   -> reinit
t=6   init OK on spi
t=12  read IMU_ENOTFOUND -> absent, retry at t=16
t=16  init IMU_ENOTFOUND -> absent, retry at t=20
t=20  init OK on spi
```

같은 앱 코드가 버스 종류를 묻지 않고 `recover` op 유무만 본다. `recover` 가 `bus_ops` 의 **선택 항목**인 이유: 복구 절차(9클럭 + STOP)는 버스 고유 지식이라 어댑터에 살고, **언제** 부를지는 앱의 정책이라 위에 산다. Linux 도 같은 분리다 — I2C 어댑터 드라이버가 `struct i2c_bus_recovery_info` 를 등록하고 코어/드라이버가 필요할 때 `i2c_recover_bus()` 를 부른다 (이름은 기억 — 커널 소스 확인 권장).

### 실제 프레임워크와 대응

| 이 레슨 | Linux | Zephyr | ST "platform-independent" 드라이버 |
|---|---|---|---|
| `bus_ops` + `ctx` | `struct regmap` (`devm_regmap_init_i2c` / `_spi`) | `struct bme280_bus_io { check, read, write }` + `union bme280_bus { spi_dt_spec, i2c_dt_spec }` (bme280 드라이버) | `stmdev_ctx_t { write_reg, read_reg, mdelay, handle, priv_data }` |
| `read_regs/write_regs` | `regmap_bulk_read(map, reg, val, count)` / `regmap_bulk_write` | `bus_io->read(bus, start, buf, size)` | `int32_t (*stmdev_read_ptr)(void *handle, uint8_t reg, uint8_t *buf, uint16_t len)` |
| `SpiDev.read_flag/inc_flag` | `regmap_config.read_flag_mask` / `write_flag_mask` | 어댑터 파일(`bme280_spi.c`) 안의 비트 | 사용자가 짠 플랫폼 함수가 처리 |
| 드라이버 | 예: `st_lsm6dsx_core.c` + `_i2c.c` + `_spi.c` (기억 — 확인 권장) | `bme280.c` 하나 + `bme280_i2c.c`/`bme280_spi.c` | `lsm6dso_reg.c` 하나 |
| 앱 API | IIO (`/sys/bus/iio/...`) | `sensor_sample_fetch()` + `sensor_channel_get()` | 사용자 코드 |
| 연습 2 캐시 | regcache (`cache_type`, `volatile_reg`) | — | — |

regmap/ST/Zephyr 정의는 참고자료 링크에서 확인한 것이다 (`regmap_config` 필드 설명, `stmdev_ctx_t`, `bme280_bus_io`). ST 헤더는 I2C 주소를 **8비트 형태**로 둔다 (`LSM6DSO_I2C_ADD_L 0xD5U` = 0x6A << 1 | 1) — 7비트/8비트 주소 혼동은 Q8.

### 간접 호출의 비용 (demo_indirect.c)

`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_indirect.c` 의 실제 출력 (지시어 생략, `@` 주석과 두 열 배치는 내가 붙임):

```text
who_direct:                         who_indirect:
  push  {r7, lr}                      push  {r7, lr}
  sub   sp, #8                        sub   sp, #8
  movs  r1, #0                        ldr   r2, [r0]          @ b->ops
  strb.w r1, [sp, #7]                 movs  r1, #0
  add.w r2, sp, #7                    ldr   r0, [r0, #4]      @ b->ctx
  movs  r1, #15                       ldr.w r12, [r2]         @ ops->read_regs
  movs  r3, #1                        strb.w r1, [sp, #7]
  bl    i2c_read_regs                 add.w r2, sp, #7
  ...                                 movs  r1, #15
                                      movs  r3, #1
                                      blx   r12               @ 간접 호출
```

(같은 파일을 맥에서 `cc -std=c11 -Wall -Wextra -O2 demo_indirect.c -o demo_indirect && ./demo_indirect` 로 돌리면 가짜 버스로 세 경로가 같은 값을 내는지 확인한다: `who_direct=0xB5 who_indirect=0xB5 who_known=0xB5`.) 차이는 로드 3개 + `bl` → `blx r12`. 같은 호출이 I2C 400 kHz 에서 수백 µs 걸리는 트랜잭션을 만든다는 걸 생각하면 사이클 몇 개는 잡음이다. 그리고 `who_known`(ops 테이블이 같은 번역 단위의 `static const`)은 컴파일러가 **`bl i2c_read_regs` 로 직접 호출화**했다 — 출력이 `who_direct` 와 명령어 단위로 같다. 진짜 비용은 사이클이 아니라 (1) 인라인이 막혀 상수 전파가 안 되는 것, (2) 코드 크기(쓰지 않는 버스 어댑터도 링크될 수 있음 — `ops` 를 빌드 설정으로 고르면 해결), (3) 함수 포인터가 RAM 에 있으면 깨질 수 있다는 것(`const` 로 flash 에 두기).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 179/179 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 깨끗함
```

## 7. 자주 하는 실수

**1. HAL 코드를 그대로 위로 반환.** `return i2c_hal_xfer(...);` — HAL 이 0x13 을 주면 드라이버는 그 숫자를 모른다. 더 나쁜 건 앱이 `if (rc == HAL_TIMEOUT)` 같은 코드를 쓰기 시작하는 것: 이제 앱이 I2C HAL 에 묶였고 SPI 보드에서 조용히 틀린다. 고친 코드: 어댑터의 마지막 줄은 항상 `return i2c_map_err(st);`, 번역 함수엔 `default:`.

**2. 에러 경로에서 CS 를 안 올림.**

```c
spi_hal_cs(d->hal, 0);
if (spi_hal_xfer(d->hal, tx, rx, 1 + n) != SPI_HAL_OK) return BUS_ETIMEDOUT;   /* CS 가 low 로 남음 */
spi_hal_cs(d->hal, 1);
```

다음 트랜잭션의 명령 바이트가 이전 프레임의 데이터로 해석된다 (COMM-B05 버그 1 과 같은 결과). 고친 코드: xfer 결과를 변수에 받고 **무조건** `spi_hal_cs(d->hal, 1)` 뒤에 판단. C++ 이면 RAII 가드 (CPP-B07).

**3. 실패한 burst 를 해석하거나, 축마다 따로 읽기.**

```c
imu_rd(imu, R_STATUS, imu->raw, 7);            /* rc 무시 */
for (k...) mg[k] = conv(imu->raw + 1 + 2 * k);  /* 반쪽 버퍼를 변환 */
```

또는 X, Y, Z 를 트랜잭션 세 개로 읽다가 Y 에서 실패하면 X 만 새 값인 "섞인 샘플"이 나간다. 고친 코드: 지역 버퍼 하나에 burst 한 번, rc 확인 뒤 지역 배열에 계산, 마지막에 `memcpy`.

**4. 모든 계층에서 재시도.** HAL 의 `HAL_I2C_Mem_Read(..., timeout)` 위에 어댑터가 3번, 드라이버가 3번, 앱이 3번 → 고장 난 버스에서 27배 (연습 1 실측). 고친 코드: 재시도는 `imu_xfer` 한 곳, `BUS_EAGAIN` 만. 타임아웃은 복구라는 **다른 행동**으로 앱이 처리.

**5. 드라이버 안에 버스 분기나 버스 비트.** `if (imu->is_spi) reg |= 0x80;` 또는 드라이버가 `R_STATUS | 0xC0` 을 넘기는 것. I2C 에서는 그 비트가 다른 의미(일부 ST 칩은 I2C 서브주소 bit7 = 자동 증가 — 데이터시트 확인 권장)거나 다른 레지스터 주소가 된다. 고친 코드: 드라이버는 순수 레지스터 번호만, 읽기/증가 비트는 SPI 어댑터의 ctx(`read_flag`, `inc_flag`).

**6. SPI 의 0xFF 를 데이터로 믿기.** probe 에서 0xFF 를 "ID 불일치"(BADID)로 보고하면 bring-up 엔지니어가 칩 교체를 의심한다. 0x00/0xFF 는 "아무도 MISO 를 구동하지 않음" = NOTFOUND 로 구분하고, 동작 중에는 STATUS 예약 비트로 같은 판단을 한다.

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 함수 포인터 표인가요? #ifdef 나 C++ 가상 함수, 템플릿과 비교하면?</summary>

선택지는 언제 버스가 정해지느냐로 갈린다. ① 빌드 때 하나로 고정(`#ifdef USE_SPI` 또는 링크 때 `bus_i2c.c`/`bus_spi.c` 중 하나): 비용 0, 대신 한 펌웨어로 두 보드 rev 를 못 돌리고 `#ifdef` 가 드라이버 안으로 번지기 쉽다. ② 런타임 표(`bus_ops`, C 판 vtable): 보드 ID 를 읽고 고를 수 있고, 테스트에서 mock·데코레이터(연습 2·3)를 끼울 수 있다. 비용은 demo 의 asm 대로 로드 3개 + `blx`, 표가 컴파일 타임에 보이면 직접 호출로 바뀐다. ③ C++ 가상 함수 = ②와 같은 기계어. ④ CRTP/템플릿(CPP-M05) = ①의 타입 안전 판, 인라인 가능, 대신 드라이버가 헤더로 가고 버스마다 코드가 복제된다. 센서 드라이버에선 버스 트랜잭션이 µs~수백 µs 라 ②가 기본값이고, 레지스터 접근이 ns 단위인 MMIO 계층(C-A01)에선 ①/④를 고려한다.

</details>

<details>
<summary>Q2. 재시도는 어느 계층이 하나요? 타임아웃도 재시도하나요?</summary>

한 계층, 이 설계에선 드라이버의 `imu_xfer`. 이유: 재시도 판단에는 "실패가 칩에 부수효과를 남겼나"(버스가 앎)와 "이 레지스터를 다시 읽어도 되나"(드라이버가 앎)가 다 필요하다. 버스는 그 정보를 코드로 올린다: `BUS_EAGAIN` = 중재 패배 = 우리 트랜잭션은 칩에 안 닿음 → 안전. 타임아웃·데이터 NACK 은 일부가 처리됐을 수 있어 clear-on-read / FIFO 레지스터면 재시도가 데이터를 잃게 한다. 그래서 타임아웃은 재시도가 아니라 **복구**(I2C 9클럭, COMM-M03)라는 다른 행동이고 앱이 정한다. 계층마다 재시도하면 곱해진다: 연습 1 실측 27 트랜잭션, 25 ms 타임아웃 가정 시 675 ms 블록 — 워치독이 먼저 터질 수 있다. Linux I2C 는 adapter 의 `retries` 필드가 있지만 코어가 다시 보내는 건 `-EAGAIN`(중재 패배)일 때뿐이라는 기억이다 (i2c-core 소스 확인 권장) — 같은 철학이다.

</details>

<details>
<summary>Q3. SPI 에는 ACK 가 없는데 센서가 없는 걸 어떻게 아나요?</summary>

버스는 모른다 — MISO 가 풀업이면 0xFF, 풀다운/단락이면 0x00 이 "성공한 전송"으로 들어온다. 그래서 ① probe 에서 WHO_AM_I 를 읽고 0x00/0xFF 면 NOTFOUND(통신 불가), 그 외 불일치면 BADID(다른 칩이거나 1비트 밀림, COMM-B05), ② 동작 중엔 값의 그럴듯함: 예약 비트가 0 이어야 하는 STATUS 가 0xFF 면 NOTFOUND. 이걸 안 하면 커넥터가 빠진 순간 0xFFFF → −1 → 0 mg 가 "정상"으로 나간다 (장애 표의 `spi MISO open (run)` 줄). 하드웨어 팁: MISO 에 약한 풀업을 두면 부재가 항상 0xFF 로 결정되고, 떠 있는 MISO 는 랜덤 값이라 진단이 더 어렵다. 더 강하게 가려면 주기적으로 WHO_AM_I 를 다시 읽거나 칩의 데이터 카운터·CRC(있다면)를 쓴다.

</details>

<details>
<summary>Q4. 에러를 번역하면 원래 정보가 사라지지 않나요? 디버깅은?</summary>

번역은 **위층의 결정**을 위한 것이고, 진단 정보는 따로 남긴다. 이 코드에선 `imu.last_bus_err`, HAL 의 `last_status`, 와이어 로그가 그 역할이다. 앱의 결정(재초기화할까, 복구할까)은 IMU 코드만으로 충분해야 하고, 로그·텔레메트리에는 "IMU_ETIMEDOUT (bus ETIMEDOUT, hal I2C_TIMEOUT, at t=22)" 처럼 세 층을 다 찍는다. 번역 안 하고 HAL 코드를 올리면 두 가지가 깨진다: 앱이 버스에 의존하게 되고, 같은 숫자가 다른 층에서 다른 뜻(예: STM32 HAL 의 `HAL_StatusTypeDef` 에서 `HAL_TIMEOUT = 3` — 기억, 확인 권장 — 과 다른 라이브러리의 3)이 되어 충돌한다. 그래서 도메인을 일부러 띄웠다 (HAL 양수, BUS −1..−5, IMU −101..) — 섞이면 `is_bus_code` 단언이 잡는다.

</details>

<details>
<summary>Q5. burst 중간에 실패하면 이미 받은 X 는 쓰나요?</summary>

안 쓴다. 한 샘플의 세 축은 같은 순간의 값이어야 의미가 있다 (자세 추정에서 X 만 새 값이면 가짜 회전이 생긴다). 그래서 ① STATUS + 6바이트를 burst 한 번으로(칩이 출력 레지스터를 래치하는 BDU 와 같이 — COMM-B05 연습 2), ② 지역 버퍼로 받고 rc 확인 뒤에만 해석, ③ 결과는 한꺼번에 커밋. 이전 샘플을 유지할지 "무효"로 표시할지는 앱 계약 — 이 코드는 출력을 안 건드리고 에러를 돌려준다. DMA 로 읽는다면 완료 콜백에서 상태를 확인한 뒤에야 버퍼를 넘기고, 다음 전송이 같은 버퍼를 덮지 않게 더블 버퍼를 쓴다 (COMM-M13 비동기 큐).

</details>

<details>
<summary>Q6. 이걸 Linux 커널 드라이버로 옮긴다면?</summary>

`bus_ops` 를 직접 만들지 않고 regmap 을 쓴다. I2C probe 에서 `devm_regmap_init_i2c(client, &cfg)`, SPI probe 에서 `devm_regmap_init_spi(spi, &cfg)` 로 각각 regmap 을 만들고 공통 core 의 probe 에 넘긴다. SPI 의 읽기 비트는 `regmap_config.read_flag_mask = 0x80`, burst 증가 비트가 필요하면 그것도 마스크로 (ST 계열 드라이버의 실제 설정은 확인 권장). 드라이버 core 는 `regmap_bulk_read(map, 0x27, buf, 7)` 만 쓴다. 덤으로 regcache (연습 2), `regmap_update_bits()`(읽기-수정-쓰기), 락, debugfs 레지스터 덤프가 따라온다. 에러는 errno 로: I2C 어댑터는 주소 NACK 에 `-ENXIO`, 중재 패배에 `-EAGAIN`, 시간 초과에 `-ETIMEDOUT` 을 쓰는 게 커널 문서의 관례다. 위로는 IIO 서브시스템이 앱 API 역할.

</details>

<details>
<summary>Q7. 같은 I2C 버스에 센서 세 개, 태스크 두 개면 락은 어디에 두나요?</summary>

버스 인스턴스(ctx) 안, **트랜잭션 단위**. `read_regs` 하나 = 락 획득 → HAL 전송 → 락 해제 (RTOS 뮤텍스, RTOS-B10). 이것만으로 I2C 의 "포인터 쓰기 + Sr + 읽기"가 원자적이다. 하지만 드라이버의 **읽기-수정-쓰기**(필드 하나만 바꾸기)는 트랜잭션 두 개라 버스 락으론 부족하고, 드라이버 인스턴스 락이 그 둘을 감싸야 한다 (regmap 은 자체 락으로 `update_bits` 를 감싼다). ISR 에서 버스를 직접 쓰지 않는다 — 블로킹 락을 못 잡으니까. 대신 요청을 큐에 넣고 완료 콜백으로 받는 비동기 구조가 COMM-M13 이다. 우선순위 역전(낮은 우선순위 태스크가 400 kHz 로 32바이트 읽는 동안 높은 우선순위 IMU 태스크가 대기)은 뮤텍스의 우선순위 상속 + 트랜잭션 길이 제한으로 다룬다.

</details>

<details>
<summary>Q8. 데이터시트엔 I2C 주소가 0xD4/0xD5, 코드엔 0x6A — 뭐가 맞나요?</summary>

둘 다 같은 칩. I2C 주소는 7비트(0x6A)이고, 와이어 첫 바이트는 `addr << 1 | R/W` = 0xD4(쓰기) / 0xD5(읽기)다. ST 의 드라이버 헤더는 8비트 형태 `LSM6DSO_I2C_ADD_L 0xD5U` 를 쓰고, Linux(`i2c_client->addr`)와 대부분 RTOS 는 7비트를 쓴다. 섞으면 0xD4 를 7비트로 넣어 0x1A8 같은 이상한 값이 되거나 0x35 로 잘못 줄여 NACK 만 받는다. 이 레슨의 와이어 로그 `S D4 A ...` 가 정확히 그 변환이다. 경계는 어댑터 하나로 정한다: `I2cDev.addr7` 은 7비트, 시프트는 HAL 이 한다.

</details>

<details>
<summary>Q9. 테스트는 어떻게 하나요? 하드웨어 없이 드라이버를 검증할 수 있나요?</summary>

이 구조의 가장 큰 이득이 테스트다. ① mock 칩 + mock HAL(이 레슨) → 와이어 바이트까지 단언. ② `bus_ops` 수준 장애 주입 데코레이터(연습 3) → HAL 없이 "k 번째 버스 호출이 EIO 면?"을 전수로 (imu_init 7호출 × 에러 5종 = 35경우). ③ 실제 보드에서 녹화한 트랜잭션을 재생하는 `bus_ops`(COMM-A09) → 회귀 테스트. ④ 파서·디코더 쪽은 퍼징(TEST-M02, 다음 단계). 드라이버가 `bus_t` 만 알기 때문에 ①~③ 이 드라이버 소스를 한 줄도 안 바꾸고 가능하다 — 이게 "계층화했다"의 실제 증명이다.

</details>

## 9. 추가 연습

### 연습 1 — 재시도 증폭 측정

모든 에러를 3번 재시도하는 순진한 버스 데코레이터(`RetryBus`), 드라이버, 앱을 쌓고 SDA 가 붙은 버스와 중재를 항상 지는 버스에서 HAL 트랜잭션 수를 세라. 이 레슨의 구조와 비교하라.

<details>
<summary>답 보기</summary>

```c
static int rb_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    RetryBus *r = (RetryBus *)ctx;
    int rc = BUS_EIO;
    for (unsigned i = 0; i < r->tries; i++) {
        rc = r->inner.ops->read_regs(r->inner.ctx, reg, buf, n);
        if (rc == BUS_OK) break;
    }
    return rc;
}
```

실행 출력:

```text
ex1 retry amplification (stuck bus, 25 ms timeout each):
    naive 3x3x3: 27 transactions, 675 ms blocked | ours: 1 transaction, 25 ms
    arbitration always lost: naive 27, ours 3
```

25 ms 는 가정값(SMBus 의 t_TIMEOUT 25–35 ms 를 대표값으로 — 스펙 확인 권장)이고 트랜잭션 수는 실측이다. 복구(`imu_recover_bus`)는 재시도가 아니라 별개 행동이라는 것도 같이 단언한다: I2C → OK, SPI → `IMU_ENOTSUP`.

</details>

### 연습 2 — 레지스터 캐시 데코레이터와 그 함정

`bus_ops` 를 감싸는 write-through 캐시(`CacheBus`)를 짜라: 휘발성 레지스터(CTRL3, STATUS, OUT)는 항상 선으로, 나머지는 캐시 적중 시 선을 안 탄다. (a) 적중을 확인하고, (b) 쓰기 보호된 칩에서 `imu_init` 의 되읽기 검증이 어떻게 되는지, (c) 칩이 캐시 뒤에서 리셋되면 어떻게 되는지 보여라.

<details>
<summary>답 보기</summary>

```c
bool all = !c->bypass;
for (size_t i = 0; all && i < n; i++) all = !cb_volatile(reg + i) && c->valid[reg + i];
if (all) { memcpy(buf, c->val + reg, n); c->hits++; return BUS_OK; }
int rc = c->inner.ops->read_regs(c->inner.ctx, reg, buf, n);   /* 그 외엔 선으로, 성공하면 캐시 채움 */
```

실행 결과: CTRL1 10번 읽기 → HAL 트랜잭션 0개, accel 읽기 → 1개(휘발성). **함정**:

```text
ex2 cache: write-protected chip -> init without bypass=OK, with bypass=IMU_EIO
```

쓰기는 ACK 됐으니 write-through 캐시에 0x44 가 들어가고, 되읽기가 캐시에서 나와 검증이 **거짓으로 통과**한다 (칩의 CTRL1 은 0 — 테스트로 확인). 검증 읽기는 캐시를 우회해야 한다 (Linux 의 `regcache_cache_bypass()` 같은 장치 — 이름은 기억, 확인 권장). (c) SW_RESET(휘발성 CTRL3 쓰기)은 캐시를 지나가지만 칩의 CTRL1 을 0 으로 만든다 → 캐시는 여전히 0x44 를 돌려줌 → `cb_invalidate()` 뒤에야 0. 리셋·전원 차단 뒤엔 캐시를 무효화하고 필요하면 다시 써 넣는다 (regmap 의 `regcache_mark_dirty()` + `regcache_sync()` 패턴, 확인 권장).

</details>

### 연습 3 — 버스 수준 장애 주입으로 init 의 모든 실패 경로

`n` 번째 `bus_ops` 호출만 지정한 코드로 실패시키는 데코레이터(`FaultBus`, 선은 안 건드림)를 짜고, `imu_init`(버스 호출 7개: probe, 리셋 쓰기, 폴링 3, 설정 쓰기, 되읽기)의 k = 1..7 × 코드 {EINVAL, ENODEV, EIO, ETIMEDOUT, EAGAIN} 전부에서 반환값과 중단 지점을 단언하라.

<details>
<summary>답 보기</summary>

```c
static int fb_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    FaultBus *f = (FaultBus *)ctx;
    if (++f->calls == f->fail_at) return f->code;
    return f->inner.ops->read_regs(f->inner.ctx, reg, buf, n);
}
```

기대: EAGAIN 한 번은 드라이버가 삼켜 OK + 호출 8개, 나머지는 `imu_map_err(code)`, 호출 수 == k (실패 뒤 즉시 중단), 선 위 프레임 == k − 1, `ready == false`. 실행 출력 `ex3 fault-injection seam: 35 cases (7 calls x 5 bus errors), bad=0`. HAL mock 을 고치지 않고 드라이버의 모든 에러 경로를 덮는 "테스트 이음새(seam)" — `bus_ops` 를 둔 덕이다.

</details>

### 연습 4 — 더미 바이트가 있는 SPI 칩

같은 칩의 SPI 판이 읽기 명령 뒤에 더미 바이트 1개를 낸다고 하자 (Bosch BMI088 가속도계 식 — 데이터시트 확인 권장). mock 에 `n_dummy` 를 넣고, 드라이버는 그대로 둔 채 어댑터 설정만으로 지원하라. 설정을 빠뜨리면 무슨 일이 나는가?

<details>
<summary>답 보기</summary>

어댑터: 길이 `1 + n_dummy + n` 으로 xfer, `memcpy(buf, rx + 1 + n_dummy, n)`, `n_dummy > 1` 은 `BUS_EINVAL`. 실행 출력:

```text
ex4 dummy chip, adapter n_dummy=0: probe=IMU_EBADID  wire [ 8F/FD 00/2C ]
ex4 dummy chip, adapter n_dummy=1: probe=OK  wire [ 8F/FD 00/2C 00/B5 ]
```

설정을 빠뜨리면 WHO_AM_I 자리에 더미의 쓰레기(0x2C)가 와서 BADID — "칩이 틀렸다"로 오진하기 쉽다. 설정 후 init + 샘플 5개 대본의 드라이버 출력이 I2C 판과 글자 그대로 같다 (테스트 확인). 칩별 버스 특성(더미, 읽기 비트, 증가 비트)은 어댑터 설정에 산다.

</details>

### 연습 5 — 번역표를 코드에서 생성

X-macro(C-A07)로 IMU 코드 → 이름 → 앱 행동 표를 만들고, HAL 코드 0..255 전부가 BUS → IMU 를 거쳐 표에 있는 항목에 닿는지(전사성) 검사하라. 문서용 표를 출력하라.

<details>
<summary>답 보기</summary>

```c
#define IMU_ERR_TABLE(X) \
    X(IMU_OK,        "use the sample") \
    X(IMU_ENOTFOUND, "absent: re-probe every N steps, check power/wiring/address") \
    ...
#define ROW(c, act) { c, #c, act },
static const ErrRow IMU_ERR_ROWS[] = { IMU_ERR_TABLE(ROW) };
```

실행 출력 (그대로 문서에 붙인다):

| HAL | bus | driver | app action |
|---|---|---|---|
| HAL_OK | OK | IMU_OK | use the sample |
| I2C_NACK_ADDR | BUS_ENODEV | IMU_ENOTFOUND | absent: re-probe every N steps, check power/wiring/address |
| I2C_NACK_DATA | BUS_EIO | IMU_EIO | count; reinit after 3 in a row |
| I2C_ARLO | BUS_EAGAIN | IMU_EBUSY | bus contended: try next cycle |
| I2C_TIMEOUT | BUS_ETIMEDOUT | IMU_ETIMEDOUT | bus recovery (I2C 9 clocks), reinit after 3 |
| SPI_TIMEOUT | BUS_ETIMEDOUT | IMU_ETIMEDOUT | bus recovery (I2C 9 clocks), reinit after 3 |
| (none) | (none) | IMU_EBADID | wrong part: check BOM / strap pins, stop |
| (none) | (none) | IMU_ENOTREADY | not an error: no new sample yet |
| (none) | (none) | IMU_ENOTSUP | feature missing: fall back |

새 에러 코드를 추가하면서 표에 행을 안 넣으면 전사성 검사가 실패한다 — 문서가 코드에서 뒤처질 수 없다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 37/37 passed, exit 0
```

## 10. 핵심 정리

- 칩마다 다른 것 = 레지스터 의미 (드라이버), 버스마다 다른 것 = 바이트를 싣는 법 (어댑터). 경계는 `read_regs(ctx, reg, buf, n)` / `write_regs`.
- `bus_t = { const bus_ops *ops; void *ctx; }`. 칩별 버스 특성(I2C 주소, SPI 읽기·증가 비트, 더미 수)은 ctx 설정. 드라이버엔 `if (spi)` 가 없다.
- 에러는 계층마다 번역(HAL → BUS → IMU → 앱 행동), 번역 함수는 `default` 까지 전사적. 원래 코드는 진단 로그로만.
- 재시도는 한 곳(드라이버의 버스 출입구), 안전한 것(중재 패배)만. 타임아웃은 복구(앱 정책), 연속 실패는 재초기화. 계층마다 재시도하면 곱해진다 (27배).
- SPI 는 칩 부재를 모른다: WHO_AM_I 0x00/0xFF → NOTFOUND, STATUS 예약 비트로 동작 중 감시.
- burst 한 번 + 지역 버퍼 + 성공 때만 한꺼번에 커밋. 에러 경로에서도 CS↑ / STOP.
- 이 구조 = Linux regmap, Zephyr `bus_io`, ST `stmdev_ctx_t`. 덤으로 mock·장애 주입·캐시·녹화 재생을 `bus_ops` 데코레이터로 끼운다.

## 참고자료

- Linux kernel, *I2C/SMBus fault codes*: <https://docs.kernel.org/i2c/fault-codes.html> — ENXIO(주소 NACK), EAGAIN(중재 패배), ETIMEDOUT, EINVAL(I/O 전 인자 오류), EIO (원문 확인함)
- Linux kernel, `include/linux/regmap.h`: <https://github.com/torvalds/linux/blob/master/include/linux/regmap.h> — `struct regmap_config`(`reg_bits`, `val_bits`, `max_register`, `volatile_reg`, `read_flag_mask`, `write_flag_mask`, `cache_type`), `regmap_bulk_read`, `regmap_update_bits`, `devm_regmap_init_i2c/_spi` (원문 확인함). `regcache_cache_bypass`, `regcache_mark_dirty/sync`, `i2c_recover_bus` 는 기억 — 확인 권장
- Linux kernel, *Register map API* 문서와 `drivers/iio/imu/st_lsm6dsx/` (core + i2c + spi 분리 구조 — 기억, 확인 권장)
- STMicroelectronics, *lsm6dso-pid* (platform-independent driver) `lsm6dso_reg.h`: <https://github.com/STMicroelectronics/lsm6dso-pid> — `stmdev_ctx_t { write_reg, read_reg, mdelay, handle, priv_data }`, `LSM6DSO_I2C_ADD_L 0xD5U`, `LSM6DSO_ID 0x6CU` (원문 확인함)
- Zephyr, `drivers/sensor/bosch/bme280/bme280.h`: <https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/sensor/bosch/bme280/bme280.h> — `union bme280_bus`, `struct bme280_bus_io { check, read, write }` (원문 확인함); Zephyr *Sensors* API 문서 — `sensor_sample_fetch`, `sensor_channel_get`
- NXP, *UM10204 I2C-bus specification and user manual* — 주소/ACK, 중재, bus clear (COMM-B06/M02/M03 참고)
- STMicroelectronics, *STM32 HAL I2C* — `HAL_I2C_Mem_Read`, `HAL_I2C_GetError()` 의 AF/ARLO/TIMEOUT 에러 비트 (기억 — UM 확인 권장)
- Bosch Sensortec, *BMI088 datasheet* — SPI 읽기의 더미 바이트 (연습 4, 확인 권장)
- 관련 레슨: COMM-B05 (SPI 레지스터 칩, CS 버그), COMM-B07 (I2C repeated START 드라이버), COMM-B06 (I2C 비트 수준), C-M06 (함수 포인터·콜백 표), C-A01 (호스트 테스트 가능한 레지스터 계층), C-A07 (X-macro 에러 표), CPP-B07 (RAII CS/락 가드), CPP-M05 (CRTP 드라이버 인터페이스), RTOS-B10 (뮤텍스), COMM-M01/M02/M03 (스트레칭·중재·버스 복구), COMM-M13 (비동기 트랜잭션 큐), COMM-A09 (트랜잭션 녹화·재생), COMM-A11 (센서 허브 스택), TEST-M02 (퍼징, 다음 단계)
