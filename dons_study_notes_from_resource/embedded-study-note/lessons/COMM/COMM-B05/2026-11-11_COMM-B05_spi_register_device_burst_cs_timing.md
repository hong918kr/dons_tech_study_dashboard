---
id: COMM-B05
time: 60분
updated: 2026-11-11
---
# COMM-B05 SPI 디바이스 접근: 명령/주소 포맷, burst, CS 타이밍

## 학습 목표

- 레지스터형 SPI 디바이스의 **명령 바이트**(command byte: R/W 비트 + 7비트 주소)를 만들고, 전이중(full-duplex) SPI 에서 왜 명령 바이트 동안의 MISO 가 쓰레기인지, 왜 읽기에 **더미(dummy) MOSI 바이트**를 보내야 하는지 설명할 수 있다.
- 슬레이브를 `CS↓ → 바이트마다 → CS↑` 상태 기계로 짠다: 자동 증가(auto-increment, IF_INC 비트 / LIS3DH 식 MS 비트), 더미 바이트(BMI088 가속도계 류), 읽기 전용 레지스터, 0x7F → 0x00 랩어라운드.
- **"트랜잭션 하나 = CS 프레임 하나"** 드라이버(`imu_read` / `imu_write`)를 짜고, 세 가지 실제 버그 — 명령과 데이터 사이 CS 해제, 더미 바이트 안 버림, CS high 시간(t_CSW) 위반 — 를 재현하고 고친다.
- 초기화 순서(WHO_AM_I → soft reset → 설정 → 다시 읽어 확인)와 STATUS + XYZ 를 7바이트 burst 한 번으로 읽는 이유를 말하고, WHO_AM_I 가 0xFF / 0x00 / 1비트 밀린 값일 때 무엇을 의심할지 진단표로 정리한다.

## 1. 문제

IMU·자력계·압력 센서·ADC 같은 SPI 칩 대부분은 "레지스터 맵 + 명령 바이트" 방식이다. 보드 bring-up 첫날 **WHO_AM_I 읽기**가 안 되면 아무것도 진행이 안 되고, 면접에서는 "이 IMU 의 burst read 를 짜 보라 → 왜 0xFF 가 나오나 → CS 는 언제 올리나"로 이어진다.

SPI 자체에는 주소가 없다 (비트를 주고받는 규칙은 COMM-B04). 칩이 그 위에 작은 프로토콜을 얹는다: **CS 를 내린 뒤 첫 바이트 = 명령**, 그 다음 바이트들 = 데이터, **CS 를 올리면 끝**. 이 레슨은 가상의 IMU "MIMU-6"(이 레슨의 mock 칩)를 바이트 단위 버스 모델 위에 만든다.

| MIMU-6 규칙 (이 레슨의 정의) | 내용 |
|---|---|
| 명령 바이트 | bit7 = R/W (1 = 읽기), bit6..0 = 레지스터 주소 (0x00~0x7F) |
| 읽기 | 명령 바이트 동안 MISO = 쓰레기 → (칩에 따라) 더미 바이트 `n_dummy` 개(0 또는 1) → 레지스터 값 |
| 쓰기 | 명령 바이트 → 데이터 바이트들 (쓰기에는 더미 없음) |
| 자동 증가 | CTRL3(0x12).IF_INC = 1 (리셋 기본값)이면 데이터 바이트마다 주소 + 1, 0x7F 다음은 0x00 |
| CS | active-low. CS↑ 는 칩의 상태 기계를 리셋. CS high 는 최소 t_CSW = 100 ns |
| 레지스터 | 0x00~0x07 설정(RW), 0x0F WHO_AM_I = 0xB5 (RO), 0x10 CTRL1 [7:4] ODR [3:2] FS, 0x11 CTRL2, 0x12 CTRL3 bit2 IF_INC / bit0 SW_RESET, 0x27 STATUS bit0 XLDA, 0x28~0x2D OUT_X_L … OUT_Z_H (little-endian int16, RO) |
| 부수효과 | OUT_Z_H(0x2D) 를 읽으면 XLDA = 0. RO·예약 주소 쓰기는 무시. SW_RESET 을 쓰는 즉시 모든 레지스터 기본값 |

레지스터 배치(0x0F WHO_AM_I, 0x27 STATUS, 0x28~ OUT, CTRL3 의 IF_INC·SW_RESET)는 ST 의 LIS3DH / LSM6 계열을 본떴지만 **값과 비트는 이 mock 의 정의**다. 실제 칩들은 이렇게 다르다 (기억에 의존한 대표값 — 데이터시트 확인 권장):

| 칩 (대표) | 명령 바이트 | 자동 증가 | 그 밖에 |
|---|---|---|---|
| ST LIS3DH | bit7 R/W, **bit6 MS**, bit5..0 주소(6비트) | 명령의 MS = 1 일 때만 | WHO_AM_I 0x0F = 0x33 (연습 1) |
| ST LSM6 계열 | bit7 R/W + 7비트 주소 | CTRL3_C.IF_INC (기본 1) | 같은 레지스터에 BDU·SW_RESET |
| InvenSense MPU-6000 류 | bit7 R/W + 7비트 주소 | burst 는 항상 증가 | 설정 레지스터는 느린 SCLK 제한이 있다는 기억 — 확인 권장 |
| Bosch BMI088 가속도계 | bit7 R/W + 7비트 주소 | 읽기 증가 | **읽기 때 주소 뒤 첫 바이트가 더미** |
| Nordic nRF24L01 | 명령 바이트(주소 포함) | — | 명령 바이트 동안 MISO = **STATUS 레지스터** (쓰레기가 아님) |

아래는 이 레슨 코드가 실제로 만든 WHO_AM_I 읽기 한 번을 파형으로 그린 것이다 (`solution.c` 로그 `[ 8F/FD 00/B5 ]`: 바이트마다 MOSI/MISO).

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">WHO_AM_I 단일 읽기 — 모드 0 (CPOL=0, CPHA=0), MSB 먼저. 실행 로그 [ 8F/FD 00/B5 ]</text> <text x="10" y="68" fill="currentColor">CS</text> <text x="10" y="118" fill="currentColor">SCLK</text> <text x="10" y="180" fill="currentColor">MOSI</text>
  <text x="10" y="242" fill="currentColor">MISO</text> <path d="M70 48 H76 V74 H732 V48 H750" stroke="currentColor" fill="none" stroke-width="2"/> <path d="M70 124 H100 H119 V98 H138 V124 H157 V98 H176 V124 H195 V98 H214 V124 H233 V98 H252 V124 H271 V98 H290 V124 H309 V98 H328 V124 H347 V98 H366 V124 H385 V98 H404 V124 H423 V98 H442 V124 H461 V98 H480 V124 H499 V98 H518 V124 H537 V98 H556 V124 H575 V98 H594 V124 H613 V98 H632 V124 H651 V98 H670 V124 H689 V98 H708 V124 H750" stroke="currentColor" fill="none" stroke-width="1.6"/> <line x1="119" y1="98" x2="119" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/>
  <line x1="157" y1="98" x2="157" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="195" y1="98" x2="195" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="233" y1="98" x2="233" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="271" y1="98" x2="271" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/>
  <line x1="309" y1="98" x2="309" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="347" y1="98" x2="347" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="385" y1="98" x2="385" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="423" y1="98" x2="423" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/>
  <line x1="461" y1="98" x2="461" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="499" y1="98" x2="499" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="537" y1="98" x2="537" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="575" y1="98" x2="575" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/>
  <line x1="613" y1="98" x2="613" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="651" y1="98" x2="651" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <line x1="689" y1="98" x2="689" y2="252" stroke="currentColor" stroke-dasharray="2 3" opacity=".35"/> <rect x="100" y="160" width="38" height="26" class="f-acc"/>
  <rect x="138" y="160" width="266" height="26" class="f-ok"/> <rect x="404" y="160" width="304" height="26" class="f-mut"/> <path d="M100 160 V160 H138 V186 H176 V186 H214 V186 H252 V160 H290 V160 H328 V160 H366 V160 H404 V186 H442 V186 H480 V186 H518 V186 H556 V186 H594 V186 H632 V186 H670 V186 H708" stroke="currentColor" fill="none" stroke-width="2"/> <text x="119" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
  <text x="157" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="195" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="233" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="271" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
  <text x="309" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="347" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="385" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="423" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
  <text x="461" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="499" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="537" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="575" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
  <text x="613" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="651" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="689" y="202" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <rect x="100" y="222" width="304" height="26" class="f-warn"/>
  <rect x="404" y="222" width="304" height="26" class="f-ok"/> <path d="M100 222 V222 H138 V222 H176 V222 H214 V222 H252 V222 H290 V222 H328 V248 H366 V222 H404 V222 H442 V248 H480 V222 H518 V222 H556 V248 H594 V222 H632 V248 H670 V222 H708" stroke="currentColor" fill="none" stroke-width="2"/> <text x="119" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="157" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
  <text x="195" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="233" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="271" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="309" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
  <text x="347" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="385" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="423" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="461" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text>
  <text x="499" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="537" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <text x="575" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="613" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text>
  <text x="651" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0</text> <text x="689" y="264" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1</text> <line x1="70" y1="173" x2="100" y2="173" stroke="currentColor" stroke-dasharray="4 3" opacity=".6"/> <line x1="708" y1="173" x2="750" y2="173" stroke="currentColor" stroke-dasharray="4 3" opacity=".6"/>
  <line x1="70" y1="235" x2="100" y2="235" stroke="currentColor" stroke-dasharray="4 3" opacity=".6"/> <line x1="708" y1="235" x2="750" y2="235" stroke="currentColor" stroke-dasharray="4 3" opacity=".6"/> <line x1="404" y1="40" x2="404" y2="270" stroke="currentColor" opacity=".7"/> <text x="119" y="154" fill="currentColor" text-anchor="middle" font-size="12">R/W=1</text>
  <text x="271" y="154" fill="currentColor" text-anchor="middle" font-size="12">주소 000 1111 = 0x0F</text> <text x="556" y="154" fill="currentColor" text-anchor="middle" font-size="12">더미 0x00 (클럭만 만들기 위한 바이트)</text> <text x="252" y="216" fill="currentColor" text-anchor="middle" font-size="12">쓰레기 0xFD — 버린다</text> <text x="556" y="216" fill="currentColor" text-anchor="middle" font-size="12">WHO_AM_I = 0xB5 (1011 0101)</text>
  <text x="76" y="92" fill="currentColor" font-size="12" text-anchor="middle">t_CSS</text> <text x="732" y="92" fill="currentColor" font-size="12" text-anchor="middle">t_CSH</text> <text x="755" y="40" fill="currentColor" font-size="12" text-anchor="end">CS↑ 뒤 high ≥ t_CSW</text> <text x="10" y="300" fill="currentColor">점선 = SCLK 상승 엣지 = 양쪽이 샘플하는 순간. 데이터는 하강 엣지에서 바뀐다 (모드 0, COMM-B04).</text>
  <text x="10" y="322" fill="currentColor">명령 바이트 8클럭 동안 칩은 아직 주소를 모른다 → 그동안 MISO 는 의미 없는 값. 데이터는 9번째 클럭부터.</text> <text x="10" y="344" fill="currentColor">CS 프레임 = [CS↓ … CS↑]. t_CSS/t_CSH/t_CSW 이름·값은 칩마다 다르다 (이 mock 은 t_CSW = 100 ns 만 검사).</text>
</svg>
```

### Part A — 명령 바이트와 슬레이브 상태 기계

- `imu_cmd(rd, addr)` — 명령 바이트를 만든다. 주소의 bit7 은 버린다.
- `imu_cs_edge(d, level)` — CS↓ 이면 "명령 대기", CS↑ 이면 "선택 안 됨". 프레임 상태는 CS↑ 에서 모두 버린다.
- `imu_byte(d, mosi)` — 선택된 동안 바이트 하나가 오갈 때 호출. **반환값 = 그 바이트 동안 칩이 MISO 로 내보낸 값**이고, 이 값은 MOSI 바이트를 다 받기 *전에* 정해진다. 상태: `CMD → (DUMMY) → READ` 또는 `CMD → WRITE`. READ/WRITE 바이트 뒤에만 IF_INC 에 따라 주소 + 1.
- 테스트(버스를 직접 써서): WHO_AM_I, 6바이트 burst, IF_INC = 0 이면 같은 레지스터 반복, burst 쓰기, RO 쓰기 무시, 0x7F → 0x00 랩, 더미 1개 칩, CS high 동안 오간 바이트 무시, CS↑ 의 상태 리셋, SW_RESET, **전수 검사**(주소 127개 × 값 256개 쓰고 읽기), 랜덤 burst 읽기 3000개·쓰기 1000개.

### Part B — 드라이버와 세 가지 CS 버그

- `imu_frame(d, tx, rx, len)` — `CS↓ → len 바이트 → CS↑ → t_CSW 대기`. 트랜잭션 하나는 반드시 이 함수 한 번.
- `imu_read(d, addr, buf, n)` — `[명령][더미 × n_dummy][데이터 × n]` 을 한 프레임으로, 데이터만 `buf` 로. `imu_write(d, addr, buf, n)` — `[명령][데이터 × n]`.
- 인자 검사: `n == 0`, `n > 32`, `addr > 0x7F`, `addr + n > 0x80`(랩어라운드 금지 — 0x7F 를 넘어 0x00 설정 영역을 건드리는 건 거의 항상 버그), NULL → `IMU_E_ARG`, **버스는 건드리지 않는다**.
- 버그 재현(하네스에 주어짐): ① `bug_read_split_cs` — 명령과 데이터를 서로 다른 CS 프레임으로 (MOSI 채움 0x00 과 0xFF 둘 다), ② `bug_read_no_dummy` — 더미 1개 칩에서 더미를 안 버림, ③ `bug_write2_no_gap` — 쓰기 두 번 사이 CS high 0 ns. 각각 무엇이 틀어지는지 단언(assert)하고, 고친 드라이버로 같은 장면을 다시 돌린다.

### Part C — 초기화와 가속도 읽기

- `imu_init(d, bus, n_dummy, fs, odr)` — WHO_AM_I 읽기(0x00/0xFF → `IMU_E_NODEV`, 그 외 불일치 → `IMU_E_ID`) → CTRL3 = SW_RESET → 리셋 끝날 때까지 폴링(최대 10번) → CTRL3 = IF_INC → CTRL1 = `odr<<4 | fs<<2` → CTRL1..CTRL3 을 burst 로 읽어 확인(불일치 → `IMU_E_VERIFY`).
- `imu_read_accel_mg(d, mg)` — STATUS(0x27) + OUT_X_L..OUT_Z_H 를 **7바이트 burst 한 번**으로. XLDA = 0 이면 `IMU_E_NOTREADY`. mg = raw × (2000 << fs) / 32768 (C 정수 나눗셈, 0 쪽으로 버림).
- 장애 주입: 칩 없음, MISO 끊김(0xFF), MISO 0 고정, 마스터가 1비트 늦게 샘플, 다른 칩(ID 0x33), 쓰기 보호, 더미 설정 불일치(양방향). 각각 기대 오류 코드를 단언.

## 2. Problem (English)

> **Driver for a register-based SPI IMU.**
>
> The device uses SPI mode 0, MSB first, active-low CS. The first byte after CS falls is a command: bit 7 = 1 for read, bits 6..0 = register address. For reads, the device outputs garbage during the command byte, then `n_dummy` (0 or 1) garbage bytes, then register data. Register addresses auto-increment (wrapping 0x7F → 0x00) while `CTRL3.IF_INC` (reset value 1) is set. Deasserting CS resets the device's protocol state; CS must stay high for at least 100 ns between transactions.
>
> ```c
> void    bus_cs(Bus *b, int level);                                  /* given */
> void    spi_xfer(Bus *b, const uint8_t *tx, uint8_t *rx, size_t n); /* given: full-duplex, does not touch CS */
> uint8_t imu_byte(Imu *d, uint8_t mosi);          /* device model: returns the MISO byte for this MOSI byte */
> int imu_read (ImuDrv *d, uint8_t addr, uint8_t *buf, size_t n);        /* 1 <= n <= 32, addr + n <= 0x80 */
> int imu_write(ImuDrv *d, uint8_t addr, const uint8_t *buf, size_t n);
> int imu_init (ImuDrv *d, Bus *b, uint8_t n_dummy, uint8_t fs, uint8_t odr);
> int imu_read_accel_mg(ImuDrv *d, int32_t mg[3]);  /* one burst: STATUS + 6 data bytes */
> ```
>
> Constraints: no dynamic allocation; each read/write is exactly one CS frame; invalid arguments must not touch the bus; return distinct error codes for no device (ID 0x00/0xFF), wrong ID, verify failure and data-not-ready.
>
> Examples (wire bytes as MOSI/MISO, `..` = garbage):
>
> ```text
> read WHO_AM_I (0x0F), n_dummy=0 : [ 8F/.. 00/B5 ]                    -> 0xB5
> read WHO_AM_I,        n_dummy=1 : [ 8F/.. 00/.. 00/B5 ]              -> 0xB5
> read 6 bytes from 0x28          : [ A8/.. 00/01 00/00 00/FE 00/FF 00/2C 00/01 ]  -> X=1, Y=-2, Z=300
> write CTRL1 (0x10) = 0x44       : [ 10/.. 44/.. ]
> ```
>
> Follow-ups: What happens if CS is released between the command byte and the data? Why is the first MISO byte garbage? WHO_AM_I reads 0xFF — what do you check first? Why read STATUS and all six data bytes in one burst?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데 (Part A 3개, Part B 3개, Part C 2개). mock 칩의 레지스터 도우미(`imu_reg_read/write`, `imu_junk`, `imu_set_sample`), 버스 모델(`bus_cs`, `spi_xfer`, 로그), 버그 재현 함수, 하네스는 주어진다. 스텁은 0 / 0xFF / `IMU_E_ARG` 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:282  imu_cmd(true, 0x0F) == 0x8F
…
WHO_AM_I read      : [ 8F/FF 00/FF ]
FAIL starter.c:293  rx[1] == WHO_AM_I_VAL
…
exhaustive write/read 127 addr x 256 values: bad=32497
…
24/74 passed
```

통과하는 24개는 mock 자체 점검 3개, 주어진 버스 모델만으로 맞는 단언(CS high 동안 오간 바이트 무시 등), 스텁의 `IMU_E_ARG` 가 우연히 맞는 인자 오류 단언들이다.

## 4. 힌트

<details>
<summary>힌트 1 — 명령 바이트는 OR 두 개</summary>

`(rd ? 0x80 : 0x00) | (addr & 0x7F)`. 주소를 마스크하지 않으면 `addr = 0x8F` 같은 실수가 "읽기"로 바뀐다.

</details>

<details>
<summary>힌트 2 — MISO 는 MOSI 를 받기 전에 정해진다</summary>

SPI 는 시프트 레지스터 두 개를 고리로 이은 것이다. 바이트 k 의 8클럭 동안 칩이 내보내는 값은 바이트 k 가 *시작할 때* 칩이 아는 것만으로 정해진다. 그래서 `imu_byte` 는 "현재 상태"로 반환값을 정하고, 받은 `mosi` 는 *다음* 상태를 정하는 데만 쓴다. CMD 상태의 반환값은 `imu_junk(d)`.

</details>

<details>
<summary>힌트 3 — 주소 증가는 데이터 바이트 뒤에만</summary>

CMD 바이트(주소를 막 받음)와 DUMMY 바이트 뒤에는 증가하지 않는다. READ/WRITE 바이트를 처리한 뒤 `regs[CTRL3] & IF_INC` 이면 `addr = (addr + 1) & 0x7F`. 쓰기 바이트가 CTRL3 자체를 바꾸면 *그 다음* 바이트부터 새 규칙이 적용된다.

</details>

<details>
<summary>힌트 4 — imu_read 는 tx 버퍼 하나, 프레임 하나</summary>

`tx[1 + 1 + 32]` 를 0 으로 채우고 `tx[0] = imu_cmd(true, addr)`. `imu_frame(d, tx, rx, 1 + n_dummy + n)` 한 번, `memcpy(buf, rx + 1 + n_dummy, n)`. 인자 검사는 버스를 건드리기 *전에*.

</details>

<details>
<summary>힌트 5 — 초기화와 진단</summary>

WHO_AM_I 가 0xFF 이면 MISO 를 아무도 안 끌어내린 것(칩 없음, CS 안 내려감, MISO 끊김 + 풀업), 0x00 이면 MISO 가 low 에 붙었거나 전원이 없다. 둘 다 "통신이 안 된다"(NODEV), 그 외 값은 "통신은 되는데 다른 칩이거나 비트가 밀렸다"(ID). 설정을 쓴 뒤에는 반드시 다시 읽어 비교한다.

</details>

<details>
<summary>힌트 6 — 부호 있는 16비트로 바꾸기</summary>

`u = lo | hi << 8` (0~65535) 를 만든 뒤 `raw = (int32_t)u - (u & 0x8000 ? 65536 : 0)`. `(int16_t)u` 는 u > 32767 일 때 C17 까지 구현 정의(implementation-defined) 변환이다. 곱셈은 int32 로: 32768 × 16000 = 524,288,000 < 2³¹.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "SPI 는 주소가 없어서 칩이 명령 바이트를 정의한다. 이 칩은 bit7 = 읽기, 하위 7비트 = 주소, 자동 증가는 IF_INC."
2. "전이중이라 n 바이트를 읽으려면 n + 1(+ 더미) 바이트를 클럭해야 하고, 첫 MISO 바이트는 버린다."
3. "트랜잭션 하나 = CS 프레임 하나. CS 를 중간에 올리면 칩은 다음 바이트를 새 명령으로 해석한다."
4. "초기화는 ID 확인 → 리셋 → 설정 → 읽어서 확인. 데이터는 STATUS 포함 burst 한 번."
5. 꼬리질문 대비: CS 타이밍(t_CSS / t_CSH / t_CSW), DMA 로 burst 할 때 CS 를 언제 올리나, 여러 태스크가 버스를 공유하면?

### Part A — 상태 기계

```svg
<svg viewBox="0 0 760 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cb05-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs> <text x="10" y="20" fill="currentColor" font-weight="bold">MIMU-6 슬레이브 상태 기계 (imu_cs_edge + imu_byte)</text>
  <rect x="20" y="110" width="110" height="50" rx="8" class="f-mut" stroke="currentColor"/> <text x="75" y="130" fill="currentColor" text-anchor="middle" font-weight="bold">IDLE</text>
  <text x="75" y="148" fill="currentColor" text-anchor="middle" font-size="12">선택 안 됨</text> <rect x="190" y="110" width="130" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="255" y="130" fill="currentColor" text-anchor="middle" font-weight="bold">CMD</text> <text x="255" y="148" fill="currentColor" text-anchor="middle" font-size="12">MISO = 쓰레기</text>
  <rect x="390" y="40" width="130" height="50" rx="8" class="f-warn" stroke="currentColor"/> <text x="455" y="60" fill="currentColor" text-anchor="middle" font-weight="bold">DUMMY</text>
  <text x="455" y="78" fill="currentColor" text-anchor="middle" font-size="12">MISO = 쓰레기</text> <rect x="590" y="40" width="150" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="665" y="60" fill="currentColor" text-anchor="middle" font-weight="bold">READ</text> <text x="665" y="78" fill="currentColor" text-anchor="middle" font-size="12">MISO = regs[addr]</text>
  <rect x="590" y="170" width="150" height="50" rx="8" class="f-ok" stroke="currentColor"/> <text x="665" y="190" fill="currentColor" text-anchor="middle" font-weight="bold">WRITE</text>
  <text x="665" y="208" fill="currentColor" text-anchor="middle" font-size="12">regs[addr] ← MOSI</text> <path d="M130 135 H188" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="159" y="128" fill="currentColor" font-size="12" text-anchor="middle">CS↓</text> <path d="M320 125 L388 75" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="330" y="88" fill="currentColor" font-size="12" text-anchor="end">bit7=1, 더미>0</text> <path d="M520 65 H588" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="554" y="58" fill="currentColor" font-size="12" text-anchor="middle">더미 소진</text> <path d="M320 120 C420 110 500 100 588 70" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="470" y="128" fill="currentColor" font-size="12" text-anchor="middle">bit7=1, 더미 0</text> <path d="M320 145 L588 190" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="450" y="186" fill="currentColor" font-size="12" text-anchor="middle">bit7=0</text> <text x="665" y="108" fill="currentColor" font-size="12" text-anchor="middle">바이트마다 IF_INC 면 addr+1</text>
  <text x="665" y="124" fill="currentColor" font-size="12" text-anchor="middle">(0x7F 다음 0x00)</text> <path d="M600 220 C420 268 160 262 75 162" stroke="currentColor" fill="none" marker-end="url(#cb05-arr)"/>
  <text x="340" y="282" fill="currentColor" font-size="12" text-anchor="middle">어느 상태든 CS↑ → IDLE (프레임 상태 전부 버림)</text> <text x="10" y="308" fill="currentColor" font-size="12">단, CS high 가 t_CSW(100 ns) 보다 짧으면 칩은 CS↑ 를 못 본 것으로 모델링 — 이전 상태 그대로 이어짐 (버그 3).</text>
</svg>
```

규칙을 그대로 코드로 옮기면 `switch` 하나다. 핵심은 **반환값(MISO)이 현재 상태로 정해진다**는 것:

| 상태 | 이번 바이트의 MISO | 받은 MOSI 로 하는 일 | 다음 상태 | 주소 증가 |
|---|---|---|---|---|
| CMD | 쓰레기 | addr = mosi & 0x7F, bit7 로 읽기/쓰기 결정 | DUMMY / READ / WRITE | 안 함 |
| DUMMY | 쓰레기 | 무시 | 남은 더미 0 이면 READ | 안 함 |
| READ | regs[addr] (읽기 부수효과 포함) | 무시 | READ | IF_INC 면 +1 |
| WRITE | 쓰레기 | regs[addr] ← mosi (RW 일 때만) | WRITE | IF_INC 면 +1 |
| IDLE | 0xFF (선택 안 됨 — 실제론 버스 모델이 호출하지 않음) | 무시 | IDLE | — |

6바이트 burst 의 실제 로그 `[ A8/2C 00/01 00/00 00/FE 00/FF 00/2C 00/01 ]` (샘플 X = 1, Y = −2, Z = 300) 를 한 바이트씩 따라가 보면:

| # | MOSI | 들어갈 때 상태 | addr (전) | MISO | 처리 | addr (후) |
|---|---|---|---|---|---|---|
| 0 | A8 = 1 010 1000 | CMD | — | 2C (쓰레기) | 읽기, addr = 0x28 → READ | 0x28 |
| 1 | 00 | READ | 0x28 | 01 = X_L | | 0x29 |
| 2 | 00 | READ | 0x29 | 00 = X_H | | 0x2A |
| 3 | 00 | READ | 0x2A | FE = Y_L | | 0x2B |
| 4 | 00 | READ | 0x2B | FF = Y_H | | 0x2C |
| 5 | 00 | READ | 0x2C | 2C = Z_L | | 0x2D |
| 6 | 00 | READ | 0x2D | 01 = Z_H | XLDA ← 0 | 0x2E |
| — | CS↑ | → IDLE | | | 프레임 상태 버림 | |

X = 0x0001, Y = 0xFFFE = −2, Z = 0x012C = 300. 같은 프레임을 IF_INC = 0 으로 보내면 MISO 는 `01 01 01 01 01 01` (0x28 만 여섯 번) — 테스트가 이걸 확인한다.

자동 증가를 정하는 방법은 칩마다 셋 중 하나다:

| 방식 | 대표 | 장점 | 함정 |
|---|---|---|---|
| 레지스터 비트 (IF_INC) | ST LSM6 계열, 이 mock | 명령 바이트에 주소 7비트 전부 | 누가 IF_INC 를 끄면 모든 burst 가 같은 값 반복 → init 에서 soft reset + 명시적 설정 |
| 명령 바이트 비트 (MS) | ST LIS3DH | 트랜잭션마다 선택 | 주소가 6비트로 줄어듦, MS 빠뜨리면 반복 (연습 1) |
| 항상 증가 | InvenSense 류 | 단순 | FIFO 레지스터처럼 "같은 주소 반복 읽기"가 필요한 곳은 칩이 따로 처리 |

`imu_cs_edge` 는 두 줄이지만 이 레슨의 핵심 규칙이다: **CS↑ 는 칩의 프로토콜 상태를 지운다**. 그래서 CS 를 어디서 올리느냐가 곧 트랜잭션의 경계다.

전수 검사 결과 (실행 출력): `exhaustive write/read 127 addr x 256 values: bad=0` — RW 주소는 쓴 값이, RO·예약 주소는 원래 값이 읽힌다. CTRL3 은 SW_RESET 때문에 따로 시험한다.

### Part B — 한 트랜잭션 = 한 CS 프레임

```svg
<svg viewBox="0 0 760 470" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 CS 프레임 안의 바이트 배치 (위 = MOSI, 아래 = MISO, 값은 solution.c 실행 로그)</text> <text x="10" y="60" fill="currentColor">(a) 단일 읽기</text> <text x="10" y="84" fill="currentColor" font-size="12" opacity=".75">더미 0</text> <rect x="160" y="44" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="183" y="60" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8F</text> <rect x="210" y="44" width="46" height="22" class="f-mut" stroke="currentColor"/>
  <text x="233" y="60" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="160" y="68" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="183" y="84" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">FD</text> <rect x="210" y="68" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="233" y="84" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">B5</text> <text x="233" y="106" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">0F</text>
  <text x="10" y="136" fill="currentColor">(b) burst 6</text> <text x="10" y="160" fill="currentColor" font-size="12" opacity=".75">IF_INC=1</text> <rect x="160" y="120" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="183" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A8</text> <rect x="210" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="233" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="260" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="283" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="310" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="333" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="360" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="383" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="410" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="433" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="460" y="120" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="483" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="160" y="144" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="183" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2C</text>
  <rect x="210" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="233" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="260" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="283" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="310" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="333" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">FE</text>
  <rect x="360" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="383" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">FF</text> <rect x="410" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="433" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2C</text> <rect x="460" y="144" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="483" y="160" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <text x="233" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="283" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">29</text> <text x="333" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">2A</text> <text x="383" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">2B</text> <text x="433" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">2C</text> <text x="483" y="182" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">2D</text>
  <text x="10" y="212" fill="currentColor">(c) burst 6</text> <text x="10" y="236" fill="currentColor" font-size="12" opacity=".75">IF_INC=0</text> <rect x="160" y="196" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="183" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A8</text> <rect x="210" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="233" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="260" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="283" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="310" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="333" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="360" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="383" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="410" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="433" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="460" y="196" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="483" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="160" y="220" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="183" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text>
  <rect x="210" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="233" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="260" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="283" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="310" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="333" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <rect x="360" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="383" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="410" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="433" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text> <rect x="460" y="220" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="483" y="236" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <text x="233" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="283" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="333" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="383" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="433" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text> <text x="483" y="258" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">28</text>
  <text x="10" y="288" fill="currentColor">(d) 단일 읽기</text> <text x="10" y="312" fill="currentColor" font-size="12" opacity=".75">더미 1 (BMI088 류)</text> <rect x="160" y="272" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="183" y="288" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8F</text> <rect x="210" y="272" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="233" y="288" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="260" y="272" width="46" height="22" class="f-mut" stroke="currentColor"/> <text x="283" y="288" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="160" y="296" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="183" y="312" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">FD</text> <rect x="210" y="296" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="233" y="312" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2C</text>
  <rect x="260" y="296" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="283" y="312" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">B5</text> <text x="233" y="334" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">더미</text> <text x="283" y="334" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">0F</text> <text x="10" y="364" fill="currentColor">(e) burst 쓰기</text> <text x="10" y="388" fill="currentColor" font-size="12" opacity=".75">8바이트</text>
  <rect x="160" y="348" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="183" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="210" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="233" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A0</text> <rect x="260" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="283" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A1</text>
  <rect x="310" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="333" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A2</text> <rect x="360" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="383" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A3</text> <rect x="410" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="433" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A4</text>
  <rect x="460" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="483" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A5</text> <rect x="510" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="533" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A6</text> <rect x="560" y="348" width="46" height="22" class="f-ok" stroke="currentColor"/> <text x="583" y="364" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A7</text>
  <rect x="160" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="183" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="210" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="233" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="260" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="283" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text>
  <rect x="310" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="333" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="360" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="383" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="410" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="433" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text>
  <rect x="460" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="483" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="510" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="533" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text> <rect x="560" y="372" width="46" height="22" class="f-warn" stroke="currentColor"/> <text x="583" y="388" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">··</text>
  <text x="233" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">00</text> <text x="283" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">01</text> <text x="333" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">02</text> <text x="383" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">03</text> <text x="433" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">04</text> <text x="483" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">05</text>
  <text x="533" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">06</text> <text x="583" y="410" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12" opacity=".75">07</text> <rect x="620" y="44" width="16" height="14" class="f-acc" stroke="currentColor"/><text x="642" y="56" fill="currentColor" font-size="12">명령 바이트</text> <rect x="620" y="66" width="16" height="14" class="f-ok" stroke="currentColor"/><text x="642" y="78" fill="currentColor" font-size="12">유효 데이터</text> <rect x="620" y="88" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="642" y="100" fill="currentColor" font-size="12">쓰레기 — 버림</text> <rect x="620" y="110" width="16" height="14" class="f-mut" stroke="currentColor"/><text x="642" y="122" fill="currentColor" font-size="12">더미 MOSI</text>
  <text x="10" y="432" fill="currentColor">작은 숫자 = 그 바이트가 접근한 레지스터 주소. 0xA8 = 1010 1000 = 읽기 + 주소 0x28.</text> <text x="10" y="454" fill="currentColor">(b) 01 00 FE FF 2C 01 = X=1, Y=−2, Z=300 (little-endian). (d) 는 데이터가 한 바이트 늦다.</text>
</svg>
```

드라이버는 짧다. 중요한 건 **무엇을 한 프레임에 넣는가**:

```c
uint8_t tx[1 + IMU_MAX_DUMMY + IMU_MAX_BURST] = { 0 }, rx[sizeof tx];
size_t hdr = 1u + d->n_dummy;
tx[0] = imu_cmd(true, addr);
int rc = imu_frame(d, tx, rx, hdr + n);   /* CS↓ … CS↑ + t_CSW, 딱 한 번 */
memcpy(buf, rx + hdr, n);                 /* 명령·더미 동안 받은 바이트는 버린다 */
```

- 스택 사용: tx/rx 각 34 바이트 = 68 바이트. malloc 없음, 최대 길이가 인터페이스에 박혀 있다 (32 바이트 초과는 `IMU_E_ARG`).
- 랜덤 2000번(더미 0/1, 주소·길이 랜덤): 값이 맞고, `bus.frames` 가 정확히 1 증가하고, 프레임 길이가 `1 + n_dummy + n` 인지까지 확인한다.

#### 버그 1 — 명령과 데이터 사이에 CS 를 올림

```svg
<svg viewBox="0 0 760 352" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">버그 1: 명령과 데이터 사이에 CS 를 올림 — 실행 로그 [ A8/FD ] (+100ns) [ 00/2C 00/17 … ]</text> <text x="10" y="64" fill="currentColor">CS</text> <path d="M70 40 H110 V66 H170 V40 H230 V66 H560 V40 H740" stroke="currentColor" fill="none" stroke-width="2"/> <text x="200" y="36" fill="currentColor" font-size="12" text-anchor="middle">CS↑ 100 ns</text>
  <text x="10" y="98" fill="currentColor">MOSI</text><text x="10" y="124" fill="currentColor">MISO</text> <rect x="116" y="82" width="46" height="22" class="f-acc" stroke="currentColor"/> <text x="139" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A8</text> <rect x="116" y="108" width="46" height="22" class="f-warn" stroke="currentColor"/>
  <text x="139" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">FD</text> <rect x="236" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/> <text x="261" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="290" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/>
  <text x="315" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="344" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/> <text x="369" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="398" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/>
  <text x="423" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="452" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/> <text x="477" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="506" y="82" width="50" height="22" class="f-mut" stroke="currentColor"/>
  <text x="531" y="98" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text> <rect x="236" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/> <text x="261" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">2C</text> <rect x="290" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/>
  <text x="315" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">17</text> <rect x="344" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/> <text x="369" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">AE</text> <rect x="398" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/>
  <text x="423" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A1</text> <rect x="452" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/> <text x="477" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">60</text> <rect x="506" y="108" width="50" height="22" class="f-warn" stroke="currentColor"/>
  <text x="531" y="124" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">1B</text> <text x="10" y="166" fill="currentColor" font-weight="bold">칩이 본 것</text> <rect x="110" y="176" width="110" height="44" class="f-mut" stroke="currentColor"/> <text x="165" y="194" fill="currentColor" text-anchor="middle" font-size="12">명령: 읽기 0x28</text>
  <text x="165" y="212" fill="currentColor" text-anchor="middle" font-size="12">→ CS↑ 리셋</text> <rect x="236" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/> <text x="261" y="194" fill="currentColor" text-anchor="middle" font-size="12">명령:</text> <text x="261" y="212" fill="currentColor" text-anchor="middle" font-size="12">쓰기 00</text>
  <rect x="290" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/> <text x="315" y="194" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">[00]</text> <text x="315" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">←00</text> <rect x="344" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/>
  <text x="369" y="194" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">[01]</text> <text x="369" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">←00</text> <rect x="398" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/> <text x="423" y="194" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">[02]</text>
  <text x="423" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">←00</text> <rect x="452" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/> <text x="477" y="194" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">[03]</text> <text x="477" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">←00</text>
  <rect x="506" y="176" width="50" height="44" class="f-warn" stroke="currentColor"/> <text x="531" y="194" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">[04]</text> <text x="531" y="212" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">←00</text> <text x="10" y="250" fill="currentColor">드라이버가 받은 값: 2C 17 AE A1 60 1B — 전부 쓰레기.</text>
  <text x="10" y="272" fill="currentColor">기대값: E8 03 18 FC 00 40 (X=1000, Y=−1000, Z=16384).</text> <text x="10" y="294" fill="currentColor">더 나쁜 것: 더미 MOSI 0x00 이 "쓰기 0x00" 명령이 되어 설정 레지스터 0x00~0x04 가 0 으로 지워졌다.</text> <text x="10" y="316" fill="currentColor">더미가 0xFF 면 "읽기 0x7F" 가 된다 — 덮어쓰기는 없지만 0x7F, 0x00, 0x01… 을 읽어 값은 여전히 틀림.</text> <text x="10" y="338" fill="currentColor" font-family="monospace" font-size="12">수정(한 프레임): [ A8/54 00/E8 00/03 00/18 00/FC 00/00 00/40 ]</text>
</svg>
```

실제 로그:

```text
BUG split CS, 0x00 : [ A8/FD ] (+100ns) [ 00/2C 00/17 00/AE 00/A1 00/60 00/1B ]
FIX one CS frame   : [ A8/54 00/E8 00/03 00/18 00/FC 00/00 00/40 ]
```

첫 프레임에서 칩은 "0x28 읽기"를 받았지만, CS↑ 에서 그 상태를 버렸다. 두 번째 프레임의 첫 바이트 0x00 은 **새 명령 = "0x00 에 쓰기"**이고, 이어지는 0x00 다섯 개가 0x00~0x04 에 쓰였다. 테스트는 세 가지를 단언한다: 읽은 값이 틀림, `regs[0..4] == 0`(설정 파괴), `regs[5] == 0xA5`(딱 n − 1 개만 덮임). 더미를 0xFF 로 보내면 "0x7F 부터 읽기"가 되어 덮어쓰기는 없지만 `buf = {쓰레기, regs[0x7F], regs[0x00], …}` 로 여전히 틀린다.

실제로 이 버그가 생기는 경로:

- CS 를 자동으로 올리는 "전송 함수"를 명령용 / 데이터용으로 두 번 부르는 래퍼. 예: Linux `spidev` 에서 `write()` 후 `read()` — 각 호출이 별도 메시지라 그 사이 CS 가 해제된다. 해결은 `SPI_IOC_MESSAGE(2)` 로 전송 두 개를 한 메시지에 넣는 것 (연습 3), 커널 안에서는 `spi_write_then_read()`.
- MCU 의 하드웨어 NSS 를 "데이터 프레임마다 펄스" 모드로 쓰는 경우 (STM32 의 NSSP 같은 기능 — 동작 조건은 RM 확인 권장). 레지스터형 칩에는 대개 GPIO 로 CS 를 직접 잡는 쪽이 안전하다.
- 에러 경로에서 CS 를 올리고 재시도할 때 명령 바이트부터 다시 보내지 않는 코드. C++ 라면 RAII 가드로 "프레임 = 스코프"를 강제한다 (CPP-B07).

#### 버그 2 — 더미 바이트를 안 버림

BMI088 가속도계 같은 칩은 읽기 때 명령 뒤에 더미 바이트를 하나 더 내보낸다. 드라이버가 이걸 모르면 `buf[0]` 에 쓰레기, `buf[1..]` 에 한 칸씩 밀린 데이터가 들어간다 — 테스트가 `memcmp(buf + 1, exp, 5) == 0` 으로 "정확히 한 바이트 밀림"을 확인한다. 더미 수는 칩의 속성이므로 드라이버 핸들(`ImuDrv.n_dummy`)에 두고 읽기 경로 한 곳에서만 처리한다.

#### 버그 3 — CS high 가 너무 짧음 (t_CSW 위반)

```text
BUG CS high 0 ns   : [ 01/FD 03/2C ] (+0ns) [ 10/17 5C/AE ]
FIX t_CSW kept     : [ 01/FD 03/2C ] (+100ns) [ 10/17 5C/AE ]
```

두 로그의 **선 위 바이트는 똑같다** — 다른 건 CS high 시간뿐이다. 0 ns 짜리 CS↑ 를 칩이 못 보면 두 쓰기가 한 프레임이 된다: `0x01 ← 03` 다음 바이트 0x10(두 번째 명령)이 **데이터**로 0x02 에, 0x5C 가 0x03 에 쓰이고, 원래 목표인 CTRL1 은 그대로 0 이다. 이 mock 은 "t_CSW 미만이면 CS↑ 를 못 본다"로 정했다 (`dev.cs_glitches == 1`). 실제 칩에서 위반 시 동작은 정의되지 않는다 — 못 보거나, 반쯤 보거나, 다음 명령의 첫 비트를 놓친다. 고속 MCU 에서 GPIO 로 CS 를 올렸다 바로 내리면 수 ns 만에 끝날 수 있어서, 드라이버의 `imu_frame` 이 CS↑ 뒤에 `bus_delay_ns(IMU_T_CSW_NS)` 를 넣는다.

CS 타이밍 이름 (칩마다 이름·값이 다르다 — 데이터시트의 SPI timing 표 확인):

| 기호 (흔한 이름) | 뜻 | 위반하면 |
|---|---|---|
| t_CSS (CS setup) | CS↓ 에서 첫 SCLK 엣지까지 | 첫 비트(R/W)를 놓쳐 명령이 1비트 밀림 |
| t_CSH (CS hold) | 마지막 SCLK 엣지에서 CS↑ 까지 | 마지막 비트 / 마지막 쓰기가 반영 안 됨 |
| t_CSW / t_CSD (CS high, deselect) | 두 프레임 사이 CS high 최소 시간 | 프레임 경계를 못 봄 (버그 3) |

이 모델은 t_CSW 만 검사한다. t_CSS·t_CSH 는 비트 수준 모델(COMM-B04)이나 로직 애널라이저 측정(HWLAB-M05)의 영역이다.

### Part C — 초기화 시퀀스

`imu_init(&drv, &bus, 0, 1, 4)` 의 실제 버스 로그를 프레임별로 풀면:

| 프레임 | 로그 | 의미 |
|---|---|---|
| 1 | `[ 8F/FD 00/B5 ]` | WHO_AM_I 읽기 → 0xB5 확인 |
| 2 | `[ 12/2C 01/17 ]` | CTRL3 ← SW_RESET (쓰는 즉시 전체 리셋) |
| 3 | `[ 92/AE 00/04 ]` | CTRL3 읽기 → 0x04: SW_RESET 비트 0 = 리셋 끝 |
| 4 | `[ 12/A1 04/60 ]` | CTRL3 ← IF_INC (기본값이어도 명시) |
| 5 | `[ 10/1B 44/C2 ]` | CTRL1 ← 0x44 (ODR 4, FS ±4g) |
| 6 | `[ 90/05 00/44 00/00 00/04 ]` | CTRL1..CTRL3 burst 읽기 → 44 00 04 확인 |

프레임 사이는 모두 `(+100ns)` 였다. 왜 이 순서인가:

- **ID 먼저**: 통신이 되는지, 맞는 칩인지 확인하기 전에 쓰기를 하면 엉뚱한 칩의 레지스터를 바꿀 수 있다.
- **soft reset**: MCU 만 리셋되고 센서는 전원이 유지된 경우(디버거 리셋, 워치독) 이전 펌웨어가 남긴 설정 — 예컨대 IF_INC = 0 — 이 그대로다. 테스트 "누가 IF_INC 를 끄고 설정을 어질러 놓은 칩"이 이 경우다.
- **폴링에 상한**: 실제 칩의 리셋은 시간이 걸린다. 무한 루프 대신 횟수(또는 시간) 상한 + 오류 코드.
- **다시 읽어 확인**: 쓰기는 MISO 를 안 보므로, 배선·모드·쓰기 보호 문제는 읽어 봐야 드러난다. `write_protect` 장애 주입이 `IMU_E_VERIFY` 로 잡힌다.

WHO_AM_I 진단표 (실행에서 나온 값):

| 주입한 장애 | 읽힌 ID | 반환 | 실제로 의심할 것 |
|---|---|---|---|
| 칩 없음 (CS 에 아무도 없음) | 0xFF | NODEV | CS 핀 번호, 칩 전원, MISO 배선 — 풀업 때문에 1 만 읽힘 |
| MISO 끊김 (풀업) | 0xFF | NODEV | 위와 같음. 풀업이 없으면 떠 있는(floating) 값 |
| MISO 0 고정 | 0x00 | NODEV | 단락, 칩 전원 없음, 리셋 상태 |
| 마스터가 1비트 늦게 샘플 | 0xDA (MSB 1, 나머지 0x5A) | ID | CPOL/CPHA 불일치 또는 SCLK 너무 빠름 — 0xB5 >> 1 = 0x5A (COMM-B04) |
| 다른 칩 (ID 0x33) | 0x33 | ID | 부품 변형, 같은 버스의 다른 CS |
| 드라이버 더미 0, 칩 더미 1 | 0x2C (쓰레기) | ID | 칩 종류별 더미 규칙 |
| 드라이버 더미 1, 칩 더미 0 | 0x00 (= CTRL1) | **NODEV** | 오진 주의: 0x0F 다음 주소 0x10 을 읽은 것 |

"1비트 늦은 샘플" 장애는 이 레슨의 단순 모델이다: 받은 바이트 = (직전 MISO 비트 << 7) | (원래 바이트 >> 1). 실제 모드 불일치에서 비트가 어느 쪽으로 밀리는지는 어느 엣지에서 누가 바꾸고 샘플하는지에 달렸다 (COMM-B04 에서 비트 수준으로 재현).

### Part C — 가속도 읽기

- **STATUS 를 같은 burst 에**: 0x27 부터 7바이트를 읽으면 "새 데이터 있음" 플래그와 그 데이터가 같은 프레임에서 온다. STATUS 를 따로 읽고 데이터를 따로 읽으면 그 사이에 새 샘플이 들어올 수 있다.
- **6바이트를 한 번에**: X_L 과 X_H 를 따로 읽으면 그 사이 갱신으로 "찢어진(torn)" 값이 나온다 — 0x00FF → 0x0100 사이에 끼면 0x01FF. 칩의 BDU(block data update) 같은 기능과 같이 써야 완전하다 (연습 2, 실측).
- **XLDA 해제**: 이 mock 은 OUT_Z_H 를 읽으면 XLDA 를 지운다. 그래서 같은 샘플을 두 번 받으면 `IMU_E_NOTREADY` — 테스트가 확인한다.
- **변환**: ±2g 에서 16384 → 16384 × 2000 / 32768 = 1000 mg. −1 → −2000 / 32768 = −0.06 → 0 (0 쪽으로 버림). ±16g 에서 −32768 → −16000, 32767 → 15999 (15999.5 를 버림). 랜덤 2000개(FS·더미 랜덤)를 64비트 기준 구현과 비교.

### 복잡도와 자원

- 시간: 바이트당 O(1), 트랜잭션 O(n). 버스 시간은 (1 + n_dummy + n) × 8 / f_SCLK + t_CSW — 8 MHz 에서 6바이트 burst 7.1 µs, 단일 읽기 6번 12.6 µs (연습 5, 모델 측정).
- RAM: 칩 모델 128 바이트 레지스터, 드라이버 스택 68 바이트. 동적 할당 없음.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 74/74 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

실행 출력 (전체):

```text
WHO_AM_I read      : [ 8F/FD 00/B5 ]
burst 6 from 0x28  : [ A8/2C 00/01 00/00 00/FE 00/FF 00/2C 00/01 ]
dummy=1 WHO_AM_I   : [ 8F/FD 00/2C 00/B5 ]
exhaustive write/read 127 addr x 256 values: bad=0
BUG split CS, 0x00 : [ A8/FD ] (+100ns) [ 00/2C 00/17 00/AE 00/A1 00/60 00/1B ]
FIX one CS frame   : [ A8/54 00/E8 00/03 00/18 00/FC 00/00 00/40 ]
BUG CS high 0 ns   : [ 01/FD 03/2C ] (+0ns) [ 10/17 5C/AE ]
FIX t_CSW kept     : [ 01/FD 03/2C ] (+100ns) [ 10/17 5C/AE ]
imu_init log       : [ 8F/FD 00/B5 ] (+100ns) [ 12/2C 01/17 ] (+100ns) [ 92/AE 00/04 ] (+100ns) [ 12/A1 04/60 ] (+100ns) [ 10/1B 44/C2 ] (+100ns) [ 90/05 00/44 00/00 00/04 ]
late-1bit WHO_AM_I : 0xDA (expected 0xB5)
dummy cfg 0 on dummy-1 chip: id read 0x2C
74/74 passed
```

쓰레기 바이트(FD, 2C, 17 …)는 mock 의 LCG(`x·5 + 0x3B mod 256`)가 만든 값이라 매 실행 같다.

## 7. 자주 하는 실수

**1. 명령과 데이터를 서로 다른 CS 프레임으로 보낸다.** "주소 쓰기 → 데이터 읽기"를 함수 두 개로 나누고 각 함수가 CS 를 잡는 구조. 이 레슨에서는 결과가 단순히 틀린 값이 아니라 **설정 레지스터 0x00~0x04 파괴**였다 (더미 0x00 이 쓰기 명령이 됨). 고친 코드: 명령 + 더미 + 데이터를 버퍼 하나에 넣고 `imu_frame` 한 번.

**2. `rx[0]` 을 데이터로 쓴다.** 명령 바이트 동안의 MISO 는 쓰레기다(nRF24L01 처럼 STATUS 를 주는 칩도 있지만 그건 데이터시트가 그렇게 정의할 때만). 증상은 "값이 한 칸씩 밀린다". 고친 코드: `memcpy(buf, rx + 1 + n_dummy, n)`.

**3. 읽기 비트를 빠뜨린다.** WHO_AM_I 를 읽으려고 0x0F 를 보내면 "0x0F 에 쓰기"다. RO 라 무시되고 MISO 는 쓰레기 — 칩이 없다고 오진하기 쉽다. RW 레지스터였다면 다음 더미 바이트 0x00 이 그 레지스터를 지운다. 고친 코드: `imu_cmd(true, addr)` 하나로만 명령을 만든다.

**4. 더미 바이트 수를 칩마다 확인하지 않는다.** 칩은 더미 1, 드라이버는 0 → 쓰레기(0x2C)를 ID 로 읽음. 반대(칩 0, 드라이버 1) → 0x10 의 값 0x00 을 읽어 **"칩 없음"으로 오진**. 둘 다 실행에서 나온 값이다.

**5. 자동 증가가 켜져 있다고 가정한다.** 이전 펌웨어가 IF_INC 를 껐거나, MS 비트 방식 칩에서 MS 를 안 세우면 burst 가 같은 레지스터를 n 번 읽는다 (`01 01 01 01 01 01`). 고친 코드: init 에서 soft reset + IF_INC 명시 + 다시 읽어 확인.

**6. CS 를 올리자마자 내린다.** 빠른 MCU 의 GPIO 토글은 수 ns 다. t_CSW 를 못 지키면 두 트랜잭션이 하나로 합쳐져 다음 명령 바이트가 **데이터로 쓰인다** (버그 3: 0x02 ← 0x10). 고친 코드: CS↑ 뒤 최소 시간 대기를 `imu_frame` 한 곳에 둔다.

**7. `(int16_t)(hi << 8 | lo)`.** C17 까지 32767 초과 값을 int16_t 로 바꾸는 건 구현 정의다(실제 컴파일러는 대부분 기대대로 동작하지만 MISRA·이식성 검토에서 걸린다). 그리고 `lo`, `hi` 가 `uint8_t` 라도 시프트 전에 int 로 승격된다 (C-B02). 고친 코드: `u = lo | hi << 8; raw = (int32_t)u - (u & 0x8000 ? 65536 : 0)`.

**8. burst 가 0x7F 를 넘는 걸 허용한다.** 칩은 0x00 으로 랩해서 설정 영역을 읽거나 **쓴다**. 드라이버가 `addr + n > 0x80` 을 거부하면 이런 실수가 버스에 나가기 전에 잡힌다.

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터 하나를 읽는데 왜 2바이트를 클럭하나요? 첫 바이트의 MISO 는 무엇인가요?</summary>

SPI 는 마스터·슬레이브 시프트 레지스터가 고리로 연결된 전이중 링크라서, 바이트 하나를 받으려면 바이트 하나를 보내야 한다. 칩은 명령 바이트의 8번째 비트를 받아야 주소를 알기 때문에, 그 8클럭 동안 MISO 로는 아직 레지스터 값을 낼 수 없다 — 그래서 쓰레기(또는 nRF24L01 처럼 STATUS 같은 고정 레지스터)다. 레지스터 값은 9번째 클럭부터 나오고, 그 클럭을 만들려고 마스터는 더미 바이트(보통 0x00 또는 0xFF)를 보낸다. 꼬리질문 "더미 값은 아무거나?" → 대부분 칩은 무시하지만, 이 레슨에서 본 것처럼 프레이밍이 깨지면 0x00 은 "0x00 에 쓰기", 0xFF 는 "0x7F 읽기"로 해석된다. 0xFF 쪽이 사고가 덜 위험하다는 판단으로 0xFF 를 쓰는 팀도 있다. 칩 데이터시트가 정한 값이 있으면 그걸 따른다.

</details>

<details>
<summary>Q2. 센서 값이 가끔 이상하고 설정이 지워집니다. CS 문제를 어떻게 확인하나요?</summary>

로직 애널라이저로 CS·SCLK·MOSI·MISO 4채널을 잡고 SPI 디코더를 CS 기준으로 프레임 단위로 본다 (HWLAB-M06). 확인할 것: ① 읽기 트랜잭션 하나가 CS 프레임 하나인가 — 명령 바이트만 있는 1바이트 프레임이 보이면 버그 1. ② 프레임 사이 CS high 가 데이터시트의 최소값 이상인가 (버그 3). ③ CS↓ 에서 첫 SCLK 까지, 마지막 SCLK 에서 CS↑ 까지 시간(t_CSS / t_CSH). ④ 프레임 길이가 1 + 더미 + n 인가. 소프트웨어 쪽에서는 이 레슨의 `bus.frames` / `last_len` 처럼 트랜잭션 레이어에 카운터를 두면 테스트로도 잡힌다.

</details>

<details>
<summary>Q3. WHO_AM_I 가 0xFF 로 읽힙니다. 무엇부터 보나요? 0x00 이면? 0x5A 면?</summary>

0xFF = 아무도 MISO 를 low 로 끌어내리지 않았다: 칩 전원·리셋 핀, CS 가 실제로 그 칩 핀에서 내려가는지(핀 매핑, 다른 장치 CS 와 혼동), MISO 배선, 칩이 SPI 모드인지(일부 ST 센서는 CS 핀 레벨로 I2C/SPI 를 고르고, BMI088 가속도계는 전원 후 CS 상승 엣지가 있어야 SPI 로 바뀐다는 기억 — 데이터시트 확인). 0x00 = MISO 가 low 에 붙음(단락, 전원 없음) 또는 이 레슨의 "더미 설정 불일치"처럼 다른 레지스터를 읽음. 0x5A·0xDA 처럼 기대값(0xB5)의 1비트 시프트 = CPOL/CPHA 불일치 또는 SCLK 가 너무 빨라 샘플이 늦음 → SCLK 를 낮추고 모드를 데이터시트와 대조 (COMM-B04). 0x33 처럼 다른 유효한 ID = 다른 부품이거나 다른 CS.

</details>

<details>
<summary>Q4. XYZ 를 레지스터 하나씩 6번 읽지 않고 burst 로 읽는 이유는?</summary>

① 일관성: 레지스터를 따로 읽으면 그 사이에 새 샘플이 들어와 X_L 은 옛 값, X_H 는 새 값인 "찢어진" 값이 나온다 (연습 2 실측: 0x00FF → 0x0100 사이에서 0x01FF). burst + 칩의 BDU 를 같이 쓰면 한 샘플의 6바이트가 보장된다. ② 시간: 명령 바이트와 CS 오버헤드가 한 번이다 — 8 MHz 에서 burst 7.1 µs vs 단일 6번 12.6 µs (연습 5). ③ STATUS 를 앞에 붙여 7바이트로 읽으면 data-ready 확인까지 한 트랜잭션이다. 고속 ODR(수 kHz)에서는 이 차이가 CPU 부하와 지터로 바로 보인다.

</details>

<details>
<summary>Q5. burst 읽기를 DMA 로 하면 CS 는 언제 올리나요? ISR 과 메인 루프는 어떻게 나누나요?</summary>

CS 를 GPIO 로 내리고 → TX·RX DMA 를 같은 길이로 시작 → **RX DMA 완료** 인터럽트에서 CS 를 올린다. TX DMA 완료는 "마지막 바이트를 데이터 레지스터에 넣었다"일 뿐 시프트 레지스터에서 아직 나가는 중일 수 있어서, 거기서 CS 를 올리면 마지막 바이트가 잘린다(t_CSH 위반). 전이중이면 RX 가 다 왔다 = 마지막 클럭까지 끝났다. ISR 은 CS↑ + 완료 플래그/세마포어만 하고, 바이트 → int16 → mg 변환과 필터링은 태스크나 메인 루프에서 한다. data-ready 인터럽트 핀(INT1) → ISR 에서 DMA 시작 → 완료 ISR → 태스크 알림이 흔한 구조다. DMA 버퍼는 정적이고, 캐시가 있는 코어(Cortex-M7)에서는 RX 버퍼 invalidate 가 필요하다 (ARCH-M12). 실제 드라이버는 BM-B12 / BM-M04, 비동기 큐는 COMM-M13.

</details>

<details>
<summary>Q6. 같은 SPI 버스에 IMU 와 플래시가 있고 태스크 두 개가 씁니다. 무엇을 보장해야 하나요?</summary>

① 트랜잭션 단위 상호 배제: "버스 락 획득 → 모드/속도 설정 → CS↓ → 전송 → CS↑ → 락 해제". 락 없이 다른 태스크가 중간에 끼면 두 칩의 CS 가 동시에 내려가 MISO 충돌, 또는 버그 1 과 같은 프레임 분할이 생긴다. ② 칩마다 SPI 모드·최대 클럭이 다를 수 있으니 트랜잭션마다 설정을 다시 적용. ③ `update_bits` 같은 읽기-수정-쓰기는 트랜잭션 두 개라서 락을 그 둘을 감싸게 잡아야 원자적이다 (연습 4). C++ 에서는 락 → CS 순서를 RAII 가드 선언 순서로 강제한다 (CPP-B07). 드라이버를 버스에 독립적으로 쌓는 구조는 COMM-M12.

</details>

<details>
<summary>Q7. 쓰기에도 자동 증가가 되나요? 모든 칩이 같나요?</summary>

칩마다 다르다. 이 mock 과 ST 계열은 쓰기에도 IF_INC/MS 가 적용된다. Bosch BME280 은 SPI 쓰기에서 자동 증가를 하지 않고 "주소, 데이터, 주소, 데이터" 쌍을 한 프레임에 보내게 되어 있다는 기억이다 (데이터시트 확인 권장). 그래서 드라이버의 burst 쓰기는 칩 속성(자동 증가 여부, 더미 수, 읽기 비트 위치)을 디스크립터로 받아 일반화하는 게 좋다 — 그게 COMM-M12 의 버스 추상화로 이어진다.

</details>

<details>
<summary>Q8. CS 를 하드웨어 NSS 로 쓸까요, GPIO 로 쓸까요?</summary>

하드웨어 NSS 는 CPU 개입이 없고 타이밍이 정확하지만, MCU 마다 "SPI 가 켜져 있는 동안 계속 low" 또는 "데이터 프레임마다 펄스" 같은 고정된 동작이라 레지스터형 칩의 "여러 바이트 = 한 프레임" 과 안 맞을 수 있다(STM32 의 SSOE/NSSP 동작은 RM 확인 권장). 그리고 하드웨어 NSS 는 핀 하나라 칩이 여럿이면 어차피 GPIO 가 필요하다. 그래서 펌웨어는 대개 CS 를 GPIO 로 잡고, t_CSS/t_CSH/t_CSW 는 코드(또는 DMA 완료 시점)로 지킨다. 클럭이 매우 빠르거나 CS 엣지가 성능에 중요하면 하드웨어 NSS + 트랜잭션 길이 설정이 가능한 SPI(최근 MCU 의 TSIZE 같은 기능)를 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — LIS3DH 식 MS 비트

명령 바이트가 `bit7 R/W, bit6 MS(1 = 자동 증가), bit5..0 주소` 인 칩(ST LIS3DH 방식, 비트 위치는 데이터시트 확인 권장)을 지원하라. `lis_cmd(rd, ms, addr)` 와 슬레이브 쪽 변경(`Imu.ms_style`)을 짜고, MS = 0 burst 는 같은 주소 반복, MS = 1 은 증가, 6비트 주소는 0x3F 다음 0x00 임을 테스트하라.

<details>
<summary>답 보기</summary>

```c
static uint8_t lis_cmd(bool rd, bool ms, uint8_t addr) {
    return (uint8_t)((rd ? 0x80u : 0x00u) | (ms ? 0x40u : 0x00u) | (addr & 0x3Fu));
}
/* imu_byte 의 CMD 상태 */
f->addr = d->ms_style ? (uint8_t)(mosi & 0x3Fu) : (uint8_t)(mosi & 0x7Fu);
f->inc  = d->ms_style ? (mosi & 0x40u) != 0 : true;
/* 데이터 바이트 뒤 */
if (d->ms_style) { if (f->inc) f->addr = (uint8_t)((f->addr + 1u) & 0x3Fu); }
else if (d->regs[REG_CTRL3] & CTRL3_IF_INC) f->addr = (uint8_t)((f->addr + 1u) & 0x7Fu);
```

`lis_cmd(true, true, 0x28) = 0xE8` — LIS3DH 예제 코드에서 자주 보는 "0x28 | 0xC0" 이 이것이다. MS 를 빠뜨린 `0xA8` 로 6바이트를 읽으면 X_L 만 여섯 번 (테스트 확인). MS 방식에서는 자동 증가가 **트랜잭션마다** 정해지므로 IF_INC 같은 전역 상태가 없다는 장점, 주소가 6비트로 줄어드는 단점.

</details>

### 연습 2 — 찢어진 샘플과 BDU

X = 0x00FF 인 상태에서 X_L 을 읽은 직후 새 샘플 X = 0x0100 이 들어오고 X_H 를 읽는다. (a) 단일 읽기 두 번, (b) 한 프레임 burst 인데 X_L 과 X_H 바이트 사이에 갱신이 끼는 경우 각각 무엇을 읽나? mock 에 `bdu` 옵션(선택된 동안 온 샘플은 CS↑ 까지 보류)을 넣고 다시 재라.

<details>
<summary>답 보기</summary>

실행 출력:

```text
bdu=0: split singles=0x01FF  burst(mid-frame update)=0x01FF  next read=0x0100
bdu=1: split singles=0x01FF  burst(mid-frame update)=0x00FF  next read=0x0100
```

BDU 가 없으면 burst 도 찢어진다 — burst 는 "버스 위에서 연속"일 뿐, 칩 내부 갱신을 막지 못한다. 이 mock 의 BDU 는 "CS 프레임 동안 보류"라서 burst 는 지키지만 단일 읽기 두 번은 여전히 0x01FF 다. ST 의 BDU 는 "MSB 와 LSB 를 둘 다 읽을 때까지 출력 레지스터 갱신 보류"로 설명된다는 기억이다 (정확한 조건은 데이터시트 확인). 결론: **burst + BDU** 를 같이 쓰고, 드라이버는 한 축의 L/H 를 절대 다른 프레임으로 읽지 않는다.

</details>

### 연습 3 — 세그먼트 메시지와 cs_change

Linux `spi_message` 처럼 전송 세그먼트 배열(`tx, rx, len, cs_change`)을 받아 CS 를 한 번 내리고 차례로 실행하는 `spi_message_run` 과, 그 위의 `spi_write_then_read`(명령 세그먼트 + 수신 세그먼트)를 짜라. 첫 세그먼트에 `cs_change = true` 를 주면 버그 1 이 재현됨을 보여라.

<details>
<summary>답 보기</summary>

```c
bus_cs(b, 0);
for (size_t i = 0; i < nseg; i++) {
    spi_xfer(b, seg[i].tx, seg[i].rx, seg[i].len);
    if (seg[i].cs_change && i + 1 < nseg) { bus_cs(b, 1); bus_delay_ns(b, IMU_T_CSW_NS); bus_cs(b, 0); }
}
bus_cs(b, 1);
bus_delay_ns(b, IMU_T_CSW_NS);
```

정상: 세그먼트 두 개가 한 프레임(`last_len == 7`)이 되어 값이 맞다. `cs_change` 버그 로그: `[ A8/17 ] (+100ns) [ 00/AE 00/A1 00/60 00/1B 00/C2 00/05 ]` — 설정 0x00~0x04 가 0 이 되고 0x05 는 그대로 (테스트 확인). Linux 에서 마지막 전송의 `cs_change` 는 의미가 다르다(메시지 뒤에도 CS 유지 힌트) — 이 연습은 "세그먼트 사이" 의미만 구현했다. 사용자 공간 spidev 에서는 `write()` + `read()` 대신 `SPI_IOC_MESSAGE(2)` 로 두 전송을 한 메시지에 넣는다.

</details>

### 연습 4 — 필드만 바꾸기 (update_bits)

`imu_update_bits(d, reg, mask, val, &changed)` — 레지스터를 읽어 `mask` 부분만 `val` 로 바꾸고, 값이 같으면 쓰기를 생략하라. CTRL1 의 FS 필드(비트 3:2)만 ±8g 로 바꿔 ODR 이 보존되는지, 같은 값이면 버스 프레임이 1개뿐인지 확인하라. 이 함수가 원자적이지 않은 이유는?

<details>
<summary>답 보기</summary>

```c
uint8_t old = 0;
int rc = imu_read_reg(d, reg, &old);
if (rc != IMU_OK) return rc;
uint8_t nv = (uint8_t)((old & ~mask) | (val & mask));
if (changed) *changed = (nv != old);
if (nv == old) return IMU_OK;
return imu_write_reg(d, reg, nv);
```

CTRL1 0x40 → 0x48 (프레임 2개), 다시 같은 값 → 프레임 1개, `mask = 0xF0, val = 0xF3` → 0xF8 (마스크 밖 비트 무시). 읽기와 쓰기가 **다른 트랜잭션**이라 그 사이 다른 태스크가 같은 레지스터를 바꾸면 그 변경을 덮어쓴다 (C-B01 의 ODR RMW 경쟁과 같은 구조). 버스 락을 읽기 앞에서 잡고 쓰기 뒤에서 풀어야 한다 (Q6). Linux regmap 의 `regmap_update_bits()` 가 같은 일을 한다(캐시가 있으면 읽기도 생략).

</details>

### 연습 5 — 트랜잭션 시간

`xfer_time_ns(nbytes, sclk_hz, gap_ns)` 로 "6바이트 burst 1번"과 "단일 읽기 6번"과 "더미 1개 칩의 burst" 시간을 계산하고, 버스 모델의 `t_ns` 로 잰 값과 같은지 확인하라 (SCLK 1 / 8 / 10 MHz).

<details>
<summary>답 보기</summary>

`nbytes × 8 / f_SCLK + gap`. burst = 7바이트, 단일 = 2바이트 × 6, 더미 burst = 8바이트, gap = t_CSW 100 ns. 실행 출력 (계산 = 모델 측정, 9개 단언 통과):

```text
SCLK      | burst 6 (1 frame) | 6 x single | dummy=1 burst 6
 1000 kHz |      56100 ns     |   96600 ns |    64100 ns
 8000 kHz |       7100 ns     |   12600 ns |     8100 ns
10000 kHz |       5700 ns     |   10200 ns |     6500 ns
```

burst 가 약 1.7~1.8배 빠르다. 실제로는 여기에 t_CSS·t_CSH, CS GPIO 토글, 드라이버 호출 오버헤드가 트랜잭션마다 붙으므로 단일 읽기 쪽이 더 손해다. 1 kHz ODR 에서 6바이트 burst 는 8 MHz 기준 버스 점유 0.7% 수준.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 35/35 passed, exit 0
```

## 10. 핵심 정리

- 레지스터형 SPI 칩: **CS↓ 뒤 첫 바이트 = 명령**(bit7 R/W + 주소), 나머지 = 데이터, **CS↑ = 끝 + 상태 리셋**.
- 전이중: n 바이트 읽기 = (1 + 더미 + n) 바이트 클럭. 명령·더미 동안의 MISO 는 버린다 → `rx + 1 + n_dummy`.
- 자동 증가는 칩마다: 레지스터 비트(IF_INC), 명령 비트(MS), 항상. 쓰기에는 안 되는 칩도 있다.
- **트랜잭션 하나 = CS 프레임 하나.** CS 를 중간에 올리면 다음 바이트가 새 명령이 된다 — 더미 0x00 이 "0x00 에 쓰기"가 되어 설정이 지워진다.
- CS 타이밍: t_CSS(CS↓→첫 엣지), t_CSH(마지막 엣지→CS↑), t_CSW(프레임 사이 high). t_CSW 를 못 지키면 두 프레임이 합쳐진다.
- init = WHO_AM_I(0xFF/0x00 = 통신 불가, 다른 값 = 다른 칩/비트 밀림) → soft reset → 설정 → 다시 읽어 확인.
- 데이터는 STATUS + XYZ 7바이트 burst 한 번 + BDU. 부호 확장은 구현 정의 캐스트 없이.

## 참고자료

- STMicroelectronics, *LIS3DH datasheet* — SPI 읽기/쓰기 프로토콜(RW, MS 비트, 6비트 주소), WHO_AM_I 0x0F = 0x33, STATUS_REG 0x27, OUT_X_L 0x28, BDU: <https://www.st.com/resource/en/datasheet/lis3dh.pdf> (비트 정의는 기억에 의존 — 원문 확인 권장)
- STMicroelectronics, *LSM6DSO datasheet* — CTRL3_C 의 IF_INC·BDU·SW_RESET, SPI 타이밍 표: <https://www.st.com/resource/en/datasheet/lsm6dso.pdf>
- Bosch Sensortec, *BMI088 datasheet* — 가속도계 SPI 읽기의 더미 바이트, 전원 후 SPI 모드 전환 (기억에 의존 — 확인 권장)
- TDK InvenSense, *MPU-6000/MPU-6050 Product Specification* 및 *Register Map* — SPI R/W 비트, burst 읽기, SPI 클럭 제한
- Bosch Sensortec, *BME280 datasheet* §SPI — 쓰기의 (주소, 데이터) 쌍 (Q7, 확인 권장)
- Nordic Semiconductor, *nRF24L01+ Product Specification* §8.3 SPI — 명령 바이트 동안 MISO 로 STATUS 출력 (절 번호는 기억 — 확인 권장)
- Linux kernel, *SPI userspace API (spidev)* — `SPI_IOC_MESSAGE`, `cs_change`: <https://docs.kernel.org/spi/spidev.html> / `include/linux/spi/spi.h` 의 `struct spi_transfer` 주석, `spi_write_then_read()`
- STMicroelectronics, STM32 레퍼런스 매뉴얼 SPI 장 — NSS 하드웨어 관리(SSOE, NSSP) (계열마다 다름, RM 확인)
- 관련 레슨: COMM-B04 (SPI 4모드 비트 수준), COMM-B07 (I2C 레지스터 읽기 — 같은 문제의 I2C 판), CPP-B07 (RAII CS 가드), HWLAB-M05 (LA 타이밍 검사 SPI/UART), HWLAB-M06 (프로토콜 디코더), C-B01 (비트 연산·RMW 경쟁), C-B02 (정수 승격), ARCH-M12 (DMA 캐시 일관성), COMM-M12 (I2C/SPI 겸용 드라이버 계층), COMM-M13 (버스 트랜잭션 큐), COMM-A06 (SPI NOR 플래시 드라이버), COMM-A09 (트랜잭션 녹화·재생 테스트), BM-B12 / BM-M04 (폴링·IRQ 드라이버)
