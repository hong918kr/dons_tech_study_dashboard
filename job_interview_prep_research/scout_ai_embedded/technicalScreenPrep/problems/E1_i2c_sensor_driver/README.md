# E1 · I2C Temperature Sensor Driver (mock HAL)

> **ID**: E1 · **Priority**: P0 · **Pattern**: register driver: RMW + poll with timeout + retry policy · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh E1` / `./run.sh E1 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 주어진 I2C HAL 함수만 써서 TMP102 비슷한 온도 센서 드라이버(초기화, 1회 측정 읽기, 경보 임계값 설정)를 재시도·타임아웃 규칙까지 맞춰 만든다.
- **상황**: MCU가 I2C 버스로 온도 센서 칩(주소 `0x48`)과 대화한다. 실제 하드웨어 대신 `stub.c` 아래쪽의 가짜 버스(레지스터 배열 + 오류 주입)가 HAL 뒤에 붙어 있다.
- **배경 개념**
  - **레지스터**: 칩 안의 작은 번호 붙은 저장칸(`0x00` TEMP, `0x01` CONFIG 등). 쓰기의 첫 바이트가 "몇 번 레지스터"인지(register pointer)이고, 읽기는 `write {reg}` 후 repeated START로 이어서 N바이트를 읽는다(`i2c_write_read`). 2바이트 레지스터는 MSB 먼저.
  - **NACK**: 칩이 바이트를 받았다는 응답(ACK)을 안 준 것. HAL이 `-EIO`로 알려주며, 일시적일 수 있어 재시도 대상이다. `-ETIMEDOUT`은 버스가 계속 busy로 묶인 상태.
  - **WHO_AM_I**: 항상 고정값(`0xA1`)을 돌려주는 ID 레지스터. 버스에 붙은 칩이 맞는 칩인지 확인용.
  - **read-modify-write / one-shot**: CONFIG를 읽어서 필요한 비트만 바꿔 다시 쓴다(나머지·reserved 비트 보존). OS 비트에 1을 쓰면 변환 1회 시작, 끝나면 STATUS의 DATA_READY가 켜진다(최대 30 ms).
- **입력**: `TmpDev *d`(`addr`, `cfg` = CONFIG 사본, `ready`), `addr`, `rate`(0..3), 출력 포인터 `out`(0.01 °C), 경보값 `high_c100`, `low_c100`(0.01 °C). 시간은 `millis()`(ms, `uint32_t`, wrap), 대기는 `delay_ms()`.
- **출력**: 세 함수 모두 `0` 또는 음수 errno(`-EIO`, `-ETIMEDOUT`, `-ENODEV`, `-EINVAL`, `-ERANGE`). `tmp_read_c100`은 `*out`에 0.01 °C 단위 온도, `tmp_init`은 `d->cfg`에 실제로 쓴 CONFIG 값을 저장.
- **규칙**
  - `tmp_init`: `rate > 3`이면 `-EINVAL`. WHO_AM_I가 `0xA1`이 아니면 `-ENODEV`(재시도 없음). CONFIG를 RMW해서 RATE = `rate`, SD = 1, 나머지 비트 유지, 쓴 값을 `d->cfg`에 캐시.
  - `tmp_read_c100`: 초기화 안 됐으면 `-ENODEV`. CONFIG에 `cfg` + OS 비트(OR)를 쓰기 → `TMP_POLL_MS`(5 ms)마다 STATUS 확인 → `TMP_TIMEOUT_MS`(100 ms) 지나면 `-ETIMEDOUT`(`millis()` wrap에도 정확해야 함) → TEMP 2바이트를 한 트랜잭션으로 읽고 `tmp_raw_to_c100()`(이미 제공)으로 변환.
  - `tmp_set_alert`: `low < high` 아니면 `-EINVAL`. 각 값을 TEMP 형식(12비트, 0.0625 °C/LSB, 왼쪽 정렬)으로 인코딩, 반올림은 0에서 먼 쪽(half away from zero). 12비트에 안 들어가면 `-ERANGE`. **둘 다 검사한 뒤에** 쓰기 시작.
  - 재시도: 모든 버스 전송은 `-EIO`일 때만 최대 `TMP_MAX_RETRIES`(3)번 재시도(총 4회). 버스의 `-ETIMEDOUT`은 즉시 반환. malloc 없음.
- **예제 풀어 읽기**: 가짜 버스는 now = 1000 ms, CONFIG = `0x00`, TEMP = `0x1900`, 변환 30 ms. `tmp_init(&d, 0x48, 2)` → 0, CONFIG = `0x05`(RATE=2, SD=1), 전송 3번(WHO_AM_I 읽기, CONFIG 읽기, CONFIG 쓰기). `tmp_read_c100` → 0, t = 2500(25.00 °C), 전송 9번 = 쓰기 1 + STATUS 폴링 7 + TEMP 읽기 1, 끝난 시각 1030. 처음 CONFIG가 `0x7A`였다면 init 후 `0x7D`(POL, TM, reserved 비트 유지). `tmp_set_alert(&d, 8000, 7500)` → T_HIGH = `0x5000`, T_LOW = `0x4B00`.
- **놓치기 쉬운 것**: 음수 온도(`0xE700` → -2500, `0xFFF0` → -6), `rate = 4` → `-EINVAL`, NACK 1번이면 재시도 후 성공(전송 10번), NACK 3번까지는 성공·계속 NACK면 4번 시도 후 `-EIO`, bus busy면 전송 1번만 하고 `-ETIMEDOUT`, WHO_AM_I 틀리면 전송 1번·CONFIG 안 건드림·이후 read도 `-ENODEV`, DATA_READY가 안 오면 정확히 100 ms 후 타임아웃(5000 → 5100, 쓰기 1 + 폴링 21), 폴링 중 `millis()` wrap(`0xFFFFFFF0`에서 시작), 반올림(8010 → `0x5020`, -1005 → `0xF5F0`), `low == high` → `-EINVAL`, 13000(130 °C) → `-ERANGE`, 잘못된 입력이면 전송 0번. 테스트가 전송 횟수(`F.xfers`)를 정확히 센다.
- **구현할 함수**
  - `tmp_init(d, addr, rate)`: 칩 확인 + CONFIG RMW + 사본 저장.
  - `tmp_read_c100(d, out)`: one-shot 시작 → 폴링(타임아웃) → TEMP 읽고 변환.
  - `tmp_set_alert(d, high_c100, low_c100)`: 검증 후 T_HIGH / T_LOW 인코딩해서 쓰기.

## 1. Problem

Write a small driver for a TMP102-like I2C temperature sensor. You only get the HAL below; it is implemented at the bottom of `stub.c` as a **fake bus** (a register array with fault injection), so do not touch hardware details, just call the HAL.

```c
/* HAL (provided). Return 0, -EIO (NACK) or -ETIMEDOUT (bus stuck busy). */
int i2c_write(uint8_t addr7, const uint8_t *buf, size_t len);   /* S addr+W buf P            */
int i2c_write_read(uint8_t addr7, const uint8_t *wbuf, size_t wlen,
                   uint8_t *rbuf, size_t rlen);                  /* S addr+W wbuf Sr addr+R rbuf P */
uint32_t millis(void);          /* free-running ms counter, wraps at 2^32 */
void     delay_ms(uint32_t ms); /* busy wait */

typedef struct { uint8_t addr; uint8_t cfg; bool ready; } TmpDev;   /* cfg = shadow of CONFIG */

int tmp_init(TmpDev *d, uint8_t addr, uint8_t rate);
int tmp_read_c100(TmpDev *d, int16_t *out);                 /* 0.01 degC */
int tmp_set_alert(TmpDev *d, int16_t high_c100, int16_t low_c100);
```

**Datasheet excerpt.** Device address `0x48`. The first byte of every write is the register pointer; a register read is `write {reg}` + repeated START + read N bytes. Multi-byte registers are MSB first.

| Reg | Name | Size | Access | Description |
|---|---|---|---|---|
| 0x00 | TEMP | 2 B | RO | 12-bit two's complement, left-justified (bits 15..4), 0.0625 °C/LSB |
| 0x01 | CONFIG | 1 B | RW | b7 **OS** one-shot (write 1 to start a conversion, self-clearing) · b6 reserved · b5 **POL** alert polarity · b4 **TM** thermostat mode · b3 reserved · b2:1 **RATE** (0=0.25 Hz, 1=1 Hz, 2=4 Hz, 3=8 Hz) · b0 **SD** shutdown |
| 0x02 | T_LOW | 2 B | RW | same format as TEMP |
| 0x03 | T_HIGH | 2 B | RW | same format as TEMP |
| 0x04 | STATUS | 1 B | RO | b0 **DATA_READY**: set when a conversion finishes, cleared by reading TEMP |
| 0x0F | WHO_AM_I | 1 B | RO | always `0xA1` |

Reserved bits must be written back with the value read. One conversion takes up to 30 ms. One-shot mode works while SD = 1.

Requirements:

- **`tmp_init`**: read WHO_AM_I; if it is not `0xA1` return `-ENODEV` (no retry). Then **read-modify-write** CONFIG: set RATE = `rate` and SD = 1, keep every other bit. Cache the written value in `d->cfg`. `rate > 3` → `-EINVAL`.
- **`tmp_read_c100`**: write CONFIG = `cfg | OS`; poll STATUS every `TMP_POLL_MS` (5) until DATA_READY, or return `-ETIMEDOUT` once `TMP_TIMEOUT_MS` (100) has elapsed (must be correct when `millis()` wraps); then read TEMP as **2 bytes in one transaction** and convert with the provided `tmp_raw_to_c100()` (same as [T1-5](../T1-5_raw_register_to_temp/index.html)). Not initialized → `-ENODEV`.
- **`tmp_set_alert`**: require `low < high` (`-EINVAL`); encode each threshold to the TEMP format, rounding half away from zero; a value that does not fit 12 bits → `-ERANGE`. Validate both before writing anything.
- **Retry policy**: every bus transfer is retried up to `TMP_MAX_RETRIES` (3) times on `-EIO` only (4 attempts total). `-ETIMEDOUT` from the bus is returned immediately.
- Return `0` or a negative errno. No malloc, single-threaded.

### Example

```
fake: now = 1000 ms, CONFIG = 0x00, TEMP = 0x1900, conversion = 30 ms

tmp_init(&d, 0x48, 2)   -> 0     CONFIG = 0x05 (RATE=2, SD=1), 3 transfers
tmp_read_c100(&d, &t)   -> 0     t = 2500 (25.00 degC)
                                 9 transfers: 1 write + 7 STATUS polls + 1 TEMP read, now = 1030

CONFIG = 0x7A before init -> after init CONFIG = 0x7D (POL, TM, reserved bits kept)
tmp_set_alert(&d, 8000, 7500) -> 0   T_HIGH = 0x5000, T_LOW = 0x4B00
```

### Constraints

- C11, single thread, no RTOS. `millis()` is a `uint32_t` that wraps.
- The fake counts every transfer attempt (`F.xfers`), so retry counts are tested exactly.
- Error codes come from `<errno.h>`: `EIO`, `ETIMEDOUT`, `ENODEV`, `EINVAL`, `ERANGE`.

### Clarifying questions to ask (first 3 minutes)

1. Is the address 7-bit (`0x48`) and does the HAL add the R/W bit?
2. Does a register read need a repeated START, or is STOP + START acceptable on this part?
3. Which errors are worth retrying? (NACK yes; wrong chip ID no; bus stuck — retry or recover?)
4. How many retries, and should there be a delay between them?
5. What is the max conversion time, and what timeout do you want? Is `millis()` 32-bit and wrapping?
6. Must reserved CONFIG bits be preserved? Can I keep a shadow copy of CONFIG instead of re-reading it?

## 2. Your workspace

`./run.sh E1`로 바로 테스트하세요. 테스트 10묶음(36개 CHECK)이 있고, 파일 아래쪽 fake bus는 건드리지 않습니다. `tmp_raw_to_c100()`는 이미 구현되어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 재시도는 함수 두 개에 가둔다</summary>

`reg_read(d, reg, buf, n)` / `reg_write(d, buf, n)` 헬퍼를 먼저 만듭니다. 안에서 `for (attempt = 0; attempt <= 3; attempt++)` 돌며 `rc != -EIO`면 바로 빠져나옵니다. 그러면 나머지 코드에는 재시도 로직이 한 줄도 없고, `-ETIMEDOUT`/`-ENODEV`는 자동으로 재시도 대상에서 빠집니다.

</details>

<details>
<summary>Hint 2 — RMW는 "내 필드만 지우고, 내 필드만 쓴다"</summary>

`v &= ~(RATE_MASK | SD | OS); v |= (rate << 1) | SD;` 순서입니다. OS도 지우는 이유: 읽은 값에 OS가 1로 보이는 칩도 있고, 그대로 쓰면 원치 않는 변환이 시작됩니다. 쓴 값은 `d->cfg`에 저장해 두면 읽기 때마다 CONFIG를 다시 읽지 않아도 됩니다(shadow register).

</details>

<details>
<summary>Hint 3 — 타임아웃은 "경과 시간"으로 비교</summary>

`if ((uint32_t)(millis() - t0) >= TMP_TIMEOUT_MS)`. `millis() > t0 + 100`은 `t0`가 `0xFFFFFFF0`일 때 `t0 + 100 = 0x54`로 넘쳐서 첫 폴링에 바로 타임아웃이 납니다(테스트 9번).

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 버스 위에서 일어나는 일

레지스터 읽기 한 번은 I2C 프레임 하나입니다. 먼저 "쓰기"로 레지스터 포인터를 0x00에 맞추고, STOP 없이 repeated START(Sr)로 방향을 바꿔 2바이트를 읽습니다. 마지막 바이트에 마스터가 NACK을 보내야 슬레이브가 SDA를 놓습니다.

<div class="chart" data-src="chart1.svg" data-caption="TEMP 레지스터 읽기 프레임. 주소 바이트는 (0x48 << 1) | R/W = 0x90 / 0x91. 주소 단계에서 ACK이 안 오면 HAL은 -EIO를 돌려줍니다"></div>

MSB와 LSB를 **한 트랜잭션**으로 읽어야 하는 이유: 따로 읽으면 그 사이에 변환이 끝나 MSB는 옛 값, LSB는 새 값인 "찢어진(torn)" 값이 나올 수 있습니다. 대부분 센서는 burst read 동안 출력 레지스터를 잠급니다.

### 4.2 tmp_read_c100 흐름

<div class="chart" data-src="chart2.svg" data-caption="one-shot 읽기 흐름. ⟳ 표시된 박스마다 -EIO 재시도(최대 3회)가 들어가고, 버스 -ETIMEDOUT은 즉시 반환"></div>

### 4.3 CONFIG read-modify-write

<div class="chart" data-src="chart3.svg" data-caption="CONFIG = 0x7A에서 rate = 2로 init: 0x7A & ~0x87 = 0x78, | 0x05 = 0x7D. 그냥 0x05를 쓰면 POL·TM·reserved 비트가 날아감 (테스트 3번)"></div>

### 4.4 손으로 따라가기 (trace)

정상 읽기 (테스트 1번: init 후 `now = 1000`, `d->cfg = 0x05`, 변환 30 ms):

| xfer | 시각 now | 동작 | 결과 | elapsed | 다음 |
|---|---|---|---|---|---|
| 1 | 1000 | write CONFIG = 0x05 + OS(0x80) = **0x85** | 변환 시작, OS 자동 클리어 | – | t0 = 1000 |
| 2 | 1000 | read STATUS | DRDY = 0 | 0 | delay 5 |
| 3 | 1005 | read STATUS | DRDY = 0 | 5 | delay 5 |
| 4–7 | 1010 … 1025 | read STATUS | DRDY = 0 | 10 … 25 | delay 5 |
| 8 | 1030 | read STATUS | **DRDY = 1** | 30 | break |
| 9 | 1030 | read TEMP (2 B) | 0x19 0x00 → **2500** | – | return 0 |

실패 경로 (테스트에서 확인하는 숫자):

| 시나리오 | 결과 | 전송 횟수 | 시각 |
|---|---|---|---|
| CONFIG write가 1번 NACK | `0`, t = 2500 | 10 (1 + 9) | – |
| init 중 NACK 3번 | `0` | 6 (WHO 4번 시도 + 2) | – |
| 항상 NACK | `-EIO` | 4 (1 + 재시도 3) | – |
| 버스 stuck busy | `-ETIMEDOUT` | 1 (재시도 안 함) | – |
| WHO_AM_I = 0x75 | `-ENODEV` | 1 (재시도 안 함) | – |
| DRDY 안 뜸, t0 = 5000 | `-ETIMEDOUT` | 22 (1 + STATUS 21번: 5000…5100) | 5100 |
| t0 = 0xFFFFFFF0, 정상 | `0`, t = 8000 (TEMP 0x5000) | 9 | 0x0000000E |
| t0 = 0xFFFFFFC0, DRDY 안 뜸 | `-ETIMEDOUT` | 22 | 0x00000024 (= t0 + 100) |

랩어라운드 경우 경과 시간: `0xFFFFFFF0, F5, FA, FF, 0x04, 0x09, 0x0E` → `millis() - t0` = 0, 5, 10, 15, 20, 25, 30. 덧셈 비교였다면 `t0 + 100 = 0x54`이고 `0xFFFFFFF0 > 0x54`라서 첫 폴링에서 끝납니다.

임계값 인코딩 (`counts = round(c100 × 16 / 100)`, 반올림은 0에서 먼 쪽, `reg = counts << 4`):

| c100 | c100 × 16 | counts | 레지스터 | 다시 읽으면 (raw_to_c100) |
|---|---|---|---|---|
| 8000 | 128000 | 1280 | 0x5000 | 8000 |
| 7500 | 120000 | 1200 | 0x4B00 | 7500 |
| 8010 | 128160 | 1282 (1281.6) | 0x5020 | 8013 |
| −1005 | −16080 | −161 (−160.8) | 0xF5F0 | −1006 |
| 13000 | 208000 | 2080 > 2047 | – | `-ERANGE` |

8010 → 8013처럼 왕복하면 값이 조금 바뀌는 것은 정상입니다. 분해능이 6.25 c100(0.0625 °C)이라 그 격자의 가장 가까운 점으로 갑니다.

### 4.5 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- 재시도는 `reg_read` / `reg_write` 두 곳에만 있습니다. 조건은 `rc != -EIO`면 탈출 — 그래서 `-ETIMEDOUT`, 성공(0)은 바로 나갑니다. `-ENODEV`는 버스 에러가 아니라 **데이터 검사 결과**라 헬퍼 밖에서 판단하고, 재시도하지 않습니다.
- 쓰기 재시도가 안전한 이유: CONFIG/T_HIGH/T_LOW 쓰기는 **멱등(idempotent)** 입니다. FIFO push나 "읽으면 클리어" 레지스터처럼 부작용이 있는 접근은 무조건 재시도하면 안 됩니다(§6).
- 폴링 루프는 "읽기 → 확인 → 타임아웃 검사 → delay" 순서입니다. 그래서 정확히 100 ms 지점에서 마지막으로 한 번 더 읽어 보고 포기합니다(STATUS 21번).
- `encode_c100`에서 `counts << 4`를 `(uint32_t)counts << 4`로 합니다. 음수의 왼쪽 시프트는 C에서 UB라서 UBSan이 잡습니다.
- `tmp_set_alert`은 두 값을 **먼저 모두 검증**한 뒤 씁니다. T_HIGH만 써지고 T_LOW에서 실패하면 하드웨어가 반쯤 바뀐 상태가 됩니다.

### 4.6 복잡도

- 시간: 읽기 1회당 전송 `2 + ⌈변환시간 / 5 ms⌉ + 1`번(정상 9번), 최악 `1 + 21`번 + 재시도. CPU는 `delay_ms` 동안 바쁜 대기.
- 메모리: O(1). 스택 버퍼 2–3바이트, 장치 상태는 `TmpDev` 3바이트.

---

## 5. 임베디드 관점으로 말할 것

- **Shadow register**: `d->cfg`에 CONFIG를 들고 있으면 one-shot마다 RMW(전송 2번)가 아니라 쓰기 1번이면 됩니다. 단, 다른 코드가 같은 레지스터를 만지면 shadow가 틀어지므로 "이 드라이버만 CONFIG를 소유한다"를 전제로 말합니다.
- **바쁜 대기 vs 인터럽트**: 실제 보드에서는 `delay_ms` 폴링 대신 ALERT/DRDY 핀 인터럽트나 RTOS `vTaskDelay`로 CPU를 양보합니다. 30분 면접에서는 폴링으로 짜고 "실제라면 DRDY 인터럽트 + 세마포어"라고 한 줄 말하면 충분합니다 → [E3](../E3_isr_to_task_pipeline/index.html).
- **Bus recovery (9 clocks)**: 슬레이브가 바이트 중간에 리셋 없이 멈추면 SDA를 0으로 잡고 있어 마스터가 START를 못 겁니다(HAL의 `-ETIMEDOUT`). 표준 복구는 SCL을 GPIO로 바꿔 **최대 9번 토글**(슬레이브가 남은 비트를 내보내고 NACK을 보게 함) → STOP 생성 → I2C 주변장치 재초기화입니다. 이것이 버스 timeout을 재시도하지 않고 위로 올리는 이유입니다: 재시도해도 안 풀리고, 복구는 버스 소유자(HAL 계층)의 일입니다.
- **Linux로 옮기면**: 같은 칩은 `struct i2c_driver`(`.probe`, `of_match_table`의 `compatible = "vendor,tmp-e1"`)로 등록하고, 레지스터 접근은 직접 `i2c_smbus_*` 대신 **regmap**(`devm_regmap_init_i2c`)을 씁니다. regmap은 `regmap_update_bits()`(= 이 문제의 RMW), 레지스터 캐시(= shadow), `regmap_read_poll_timeout()`(= DRDY 폴링)을 이미 제공합니다. 사용자 인터페이스는 단순 온도면 **hwmon**(`temp1_input`은 밀리도 단위, `temp1_max`/`temp1_min`이 T_HIGH/T_LOW), 샘플링·버퍼·트리거가 필요하면 **IIO**(`in_temp_raw` + `in_temp_scale` = 62.5)를 고릅니다. 실제 TMP102는 커널 `drivers/hwmon/tmp102.c`에 regmap + hwmon으로 들어 있습니다.
- **에러 코드 관례**: Linux I2C 컨트롤러는 주소 NACK을 보통 `-ENXIO`/`-EREMOTEIO`, arbitration lost를 `-EAGAIN`으로 돌려줍니다. 이 문제는 단순화해서 `-EIO` 하나로 둡니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why not retry on -ETIMEDOUT too? | 버스가 SDA low로 붙어 있으면 같은 전송을 다시 해도 똑같이 실패합니다. 위 계층에서 bus recovery(9 clocks + STOP)를 한 다음에 재시도해야 의미가 있습니다. |
| Should there be a delay or backoff between retries? | 네, 실제로는 짧은 delay(예: 1 ms)를 둡니다. 슬레이브가 내부 처리(EEPROM 쓰기 등) 중이라 NACK하는 경우가 흔하기 때문입니다. 테스트 숫자를 단순하게 두려고 생략했습니다. |
| Is it always safe to retry a transfer? | 아닙니다. 읽으면 클리어되는 STATUS, FIFO pop/push처럼 부작용이 있는 레지스터는 "슬레이브는 받았지만 마지막 ACK만 깨진" 경우 두 번 실행됩니다. 멱등 접근만 자동 재시도합니다. |
| What if the timeout must be shorter than the poll interval? | 폴링 간격을 `min(TMP_POLL_MS, timeout - elapsed)`로 줄입니다. 또는 DRDY 핀 인터럽트를 쓰고 세마포어 대기에 타임아웃을 줍니다. |
| How would you make this non-blocking? | 상태 머신으로 바꿉니다: `IDLE → START_SENT → POLLING → DONE/ERROR`. `tmp_poll()`을 주기 태스크가 부르고, 끝나면 콜백이나 플래그로 알립니다. |
| Two tasks call tmp_read_c100 at once (RTOS)? | 버스와 `TmpDev`를 mutex로 보호합니다. 같은 I2C 버스의 다른 장치 드라이버와도 버스 락을 공유해야 하고, ISR에서는 이 함수를 부르면 안 됩니다(블로킹). |
| How would this look in Linux? | `i2c_driver` + regmap + hwmon(또는 IIO). §5 참고. |
| How do you test this without hardware? | 지금처럼 HAL 뒤에 fake를 둡니다(link seam). NACK/stuck/WHO_AM_I/DRDY를 주입하고 전송 횟수와 시각을 검증합니다. 하드웨어에서는 로직 분석기로 프레임을 확인합니다. |

## 7. 자주 하는 실수

- CONFIG에 그냥 `(rate << 1) | SD`를 씀 → POL·TM·reserved 비트 소실 (테스트 3번)
- 타임아웃을 `millis() > t0 + TIMEOUT`으로 비교 → 랩어라운드에서 즉시 타임아웃 (테스트 9번)
- 재시도 횟수 off-by-one: "3번 재시도"는 **총 4번 시도**입니다 (테스트 5번: NACK 3번은 성공, 계속 NACK이면 전송 4번)
- WHO_AM_I가 틀렸는데도 재시도하거나, `-ETIMEDOUT`까지 재시도함 (테스트 6, 7번)
- TEMP를 1바이트씩 두 번 읽음 → torn read 위험 + 전송 횟수가 맞지 않음
- 임계값 두 개 중 하나를 쓴 다음에 검증 실패를 발견함 → 반쯤 바뀐 하드웨어 상태 (테스트 10번: 잘못된 입력이면 전송 0번)
- 음수 `counts << 4` → UB. `(uint32_t)`로 바꾼 뒤 시프트하거나 `* 16`
