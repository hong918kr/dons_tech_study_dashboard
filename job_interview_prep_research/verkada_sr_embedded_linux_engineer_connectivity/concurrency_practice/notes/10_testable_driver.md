# 10. 테스트 가능한 드라이버 — 하드웨어 없이 드라이버를 검증하는 법

> **이 노트를 다 읽으면**:
> - 함수 포인터 ops 구조체로 C에서 dependency injection을 구현하고, 그게 HAL을 어떻게 대체하는지 설명할 수 있다
> - 시계도 의존성이라는 것, 그래서 타임아웃 테스트가 0초에 끝나는 이유를 보일 수 있다
> - "하드웨어 없이 이걸 어떻게 테스트하겠나"에 unit / integration / system 3층으로 완결된 답을 할 수 있다
>
> **선행**: 없음 (이 노트만 스레드가 안 나온다) · 함수 포인터 문법이 낯설면 §2를 천천히
>
> **연습**: `make run N=10` (내 구현) · `make sol N=10` (모범답안) · `make tsan N=10`

---

## 0. 한 문장으로

I2C 센서 드라이버를 **하드웨어를 전혀 모르는 코드**로 짜서, 실물 센서도 실제 대기 시간도 없이 재시도·타임아웃·CRC 오류 경로까지 밀리초 단위로 회귀 테스트할 수 있게 만드는 문제다. Verkada 채용 메일에 명시된 주제이기도 하다 — 게이트웨이에 붙는 온습도·전류·PoE 컨트롤러가 전부 이 모양이고, 현장에 깔린 뒤에는 실물로 디버깅할 수가 없다.

## 1. 왜 필요한가 — 비유로 시작

보드가 랩에 한 장뿐인 상황을 생각해 보자. 그 보드로만 테스트하면 커밋마다 돌릴 수 없고(누가 쓰고 있다), 타임아웃 1초짜리 경로를 열 번 확인하려면 10초를 **진짜로 기다려야** 하고, CRC가 깨지는 상황은 아예 만들 수가 없고(센서가 착해서 잘 동작한다), 실패하면 원인이 드라이버인지 배선인지 센서 개체 불량인지 모른다.

그래서 드라이버를 **벽에서 떼어낸다**. 드라이버가 바라보는 건 진짜 I2C 버스가 아니라 "read/write 두 개짜리 구멍"이고, 제품에서는 그 구멍에 진짜 HAL을 꽂고 테스트에서는 내가 조종하는 가짜를 꽂는다.

```
        [ Sensor 드라이버 ]   ← 하드웨어를 전혀 모른다
          I2cOps   ClockOps   ← 구멍 (함수 포인터 구조체)
    ┌─────────┘      └─────────┐
    v                          v
 제품: 진짜 HAL             테스트: FakeI2c / FakeClock
  (STM32 HAL_I2C_...)        (내가 NACK, CRC 오류, 시간을 조종)
```

SSD 펌웨어에서 NAND 채널 모델을 붙여 호스트 시나리오를 돌려본 것과 같은 구조다. 다만 여기서는 그 모델이 30줄이다.

## 2. 알아야 할 개념

### 함수 포인터와 ops 구조체

C에는 interface 문법이 없다. 대신 **함수 포인터를 담은 구조체**를 쓴다. Linux 커널의 `file_operations`, `i2c_algorithm` 이 전부 이 패턴이다.

```c
typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;
} I2cOps;
```

`IoStatus (*read)(...)` 를 읽는 법: **read는 `(...)` 를 받아 `IoStatus` 를 돌려주는 함수를 가리키는 포인터**다. `*read` 를 괄호로 감싸는 이유는 그게 없으면 "IoStatus 포인터를 반환하는 함수" 선언이 되기 때문이다.

`void *ctx` 는 C에 `this` 가 없어서 두는 주머니다. 그 구현의 상태를 담고 모든 호출의 첫 인자로 다시 넘어간다(`s->i2c.write(s->i2c.ctx, ...)`) — 제품에서는 I2C 핸들이나 버스 번호, 테스트에서는 `FakeI2c` 포인터가 들어간다. 드라이버는 그게 뭔지 전혀 모른다.

### dependency injection (의존성 주입) — 시계도 의존성이다

모듈이 필요한 것을 **스스로 찾아 쓰지 않고 밖에서 받아 쓰는** 설계. `#include "stm32_hal.h"` 하고 직접 부르면 그 드라이버는 그 칩에서만 빌드된다. ops 구조체로 받으면 호스트 PC에서 `cc` 한 줄로 빌드되고 돌아간다.

시계도 똑같은 의존성이다. `HAL_GetTick()`, `clock_gettime()`, `sleep()` 을 직접 부르는 순간 그 코드는 테스트에서 **진짜 시간을 기다려야 한다**. 그래서 시계도 `uint32_t (*now_ms)(void *ctx)` 하나짜리 `ClockOps` 로 주입받는다. 그러면 테스트가 시간을 조종한다.

### 에러는 값으로 반환

드라이버는 `assert` 로 죽거나, 로그만 찍고 삼키거나, 무한 재시도하지 않는다. 무슨 일이 있었는지 **호출자에게 값으로 돌려주고** 정책은 위층이 정한다. 이 드라이버는 `SensorErr` enum(`SENS_OK`, `SENS_EAGAIN`, `SENS_ETIMEDOUT`, `SENS_ECRC`, `SENS_ESTATE`)을 반환한다. `errno` 대신 반환값을 쓰는 이유: 스레드 안전하고, 호출자가 무시하기 어렵고, enum이라 컴파일러가 `switch` 누락을 잡아준다.

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| 하드웨어 없이 테스트 | 드라이버가 HAL 심볼을 직접 참조하면 안 된다 |
| 타임아웃 경로 검증 | 시간을 읽는 곳이 **한 군데**여야 하고 주입 가능해야 한다 |
| 재시도/CRC 경로 검증 | 실패를 만들어낼 수 있어야 한다 → fake가 실패를 연출 |
| 블로킹 금지 | 드라이버 안에 `sleep` 이 없어야 한다 → 논블로킹 폴링 |
| 값이 오염되면 안 됨 | CRC 실패 시 `*out_ppm` 을 **건드리지 않는다** |

네 번째가 중요하다. 드라이버가 `while (!ready) delay(10);` 를 하면 그 코드는 RTOS 태스크 하나를 통째로 잡아먹고, 테스트도 진짜로 기다린다. 그래서 `sensor_poll` 은 **한 번 보고 바로 돌아온다**.

## 4. 단계별로 만들기

### 4-1. 드라이버가 들고 다닐 것

목표: 드라이버 구조체가 "무엇에 의존하는지"를 눈으로 보이게 만든다.

```c
typedef struct {
    I2cOps      i2c;
    ClockOps    clk;
    uint8_t     addr;
    SensorState state;
    uint32_t    op_start_ms;
    uint32_t    timeout_ms;
    unsigned    retries;        /* 통계: 누적 재시도 횟수 */
    unsigned    crc_errors;
} Sensor;
```

첫 두 필드가 의존성 전부다. 이 구조체를 보면 **이 드라이버가 세상에 대고 하는 일이 I2C 읽기/쓰기와 시간 읽기 셋뿐**이라는 게 보인다. 전역 변수가 하나도 없다는 점도 중요하다 — 같은 버스에 센서 세 개를 달면 `Sensor` 인스턴스를 세 개 만들면 끝이다. `retries` 와 `crc_errors` 는 기능이 아니라 **관측 지점**이다. 테스트가 "재시도가 정확히 2번 일어났나"를 확인할 수 있고, 제품에서는 그대로 텔레메트리로 올린다. `sensor_init` 은 ops를 **값으로** 받아 복사하므로 호출자가 스택에 만든 임시 구조체를 넘겨도 안전하다.

### 4-2. 순수 함수부터 떼어낸다 — I/O 없는 계산을 독립 테스트 가능하게

```c
static uint8_t crc8(const uint8_t *d, size_t n)      /* 순수 함수 → 단독 테스트 가능 */
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}
```

입력만 보고 출력을 정하는 함수다. 하드웨어도, 시간도, 상태도 안 본다. 이런 함수는 테스트가 공짜다(self test의 case 5가 이것만 따로 검증한다). 드라이버를 짤 때 **먼저 순수 계산을 최대한 뽑아내는 것**이 테스트 가능성의 절반이다.

### 4-3. 측정 시작 — 시간을 여기서 찍는다

```c
SensorErr sensor_start_measure(Sensor *s)
{
    if (s->state == SENS_ERROR) return SENS_ESTATE;   /* 에러 상태는 명시적 복구 필요 */

    IoStatus st = s->i2c.write(s->i2c.ctx, s->addr, REG_STATUS, 0x01);
    if (st != IO_OK) {
        s->state = SENS_ERROR;
        return SENS_ESTATE;
    }
    s->op_start_ms = s->clk.now_ms(s->clk.ctx);       /* 주입된 시계 */
    s->state = SENS_READY;
    return SENS_OK;
}
```

- 첫 줄: 한 번 `SENS_ERROR` 에 빠지면 **스스로 빠져나오지 않는다**. 버스가 망가진 상태에서 드라이버가 혼자 재시도를 계속하면 상위는 아무것도 모른 채 시스템이 느려진다.
- `HAL_GetTick()` 대신 `s->clk.now_ms(s->clk.ctx)` 로 시작 시각을 여기서 한 번만 찍는다. 이 한 줄 덕분에 타임아웃 테스트가 0초에 끝난다. 그리고 타임아웃은 "폴링 횟수"가 아니라 **경과 시간**으로 재야 한다 — 폴링 주기는 상위 스케줄러 사정에 따라 바뀌기 때문이다.

### 4-4. 폴링 — 정책이 여기 모여 있다

```c
SensorErr sensor_poll(Sensor *s, int16_t *out_ppm)
{
    if (s->state != SENS_READY) return SENS_ESTATE;

    uint8_t status = 0;
    IoStatus st = s->i2c.read(s->i2c.ctx, s->addr, REG_STATUS, &status, 1);

    if (st == IO_NACK || (st == IO_OK && !(status & STATUS_RDY))) {
        /* 아직 변환 중이거나 일시적 NACK → 타임아웃 전까지는 재시도 */
        s->retries++;
        if (s->clk.now_ms(s->clk.ctx) - s->op_start_ms >= s->timeout_ms) {
            s->state = SENS_ERROR;
            return SENS_ETIMEDOUT;
        }
        return SENS_EAGAIN;
    }
    if (st != IO_OK) {                                 /* 버스 오류는 즉시 실패 */
        s->state = SENS_ERROR;
        return SENS_ESTATE;
    }
```

여기가 **정책(policy)** 이다. I/O는 `s->i2c.read` 한 줄뿐이고 나머지는 전부 판단이다.

- "아직 준비 안 됨"과 "일시적 NACK"은 같은 취급 — 재시도 대상이다. 센서가 변환 중에 NACK을 내는 건 흔한 동작이다.
- 버스 오류(`IO_BUS_ERR`)는 재시도 대상이 아니다. 배선이나 클럭 문제라 다시 해도 안 된다. **어떤 실패가 재시도 가능하고 어떤 것이 아닌가**를 구분하는 것이 드라이버 설계의 핵심 판단이고, 면접에서 물어보는 지점이다.
- `now_ms() - op_start_ms >= timeout_ms` — 뺄셈으로 비교하는 게 중요하다. `now_ms() >= op_start_ms + timeout_ms` 로 쓰면 32비트 틱이 한 바퀴 돌 때(약 49.7일) 오작동한다. unsigned 뺄셈은 wrap을 자연스럽게 흡수한다.

### 4-5. 데이터 읽기와 CRC

```c
    uint8_t raw[3];                                    /* [hi, lo, crc] */
    st = s->i2c.read(s->i2c.ctx, s->addr, REG_DATA, raw, sizeof raw);
    if (st != IO_OK) {
        s->state = SENS_ERROR;
        return SENS_ESTATE;
    }
    if (crc8(raw, 2) != raw[2]) {
        s->crc_errors++;
        s->state = SENS_RESET;                         /* 재측정으로 복구 가능 */
        return SENS_ECRC;
    }
    *out_ppm = (int16_t)((raw[0] << 8) | raw[1]);
    s->state = SENS_RESET;
    return SENS_OK;
}
```

**CRC가 깨졌을 때 `*out_ppm` 을 건드리지 않는다.** 이게 값 오염 방지다. 오염된 값이 한 번이라도 새어나가면 그 위의 필터, 로그, 클라우드 알림까지 전부 오염된다. 테스트가 이걸 직접 확인한다(case 4에서 `ppm == -1` 을 그대로 유지하는지 본다). 그리고 CRC 실패는 `SENS_ERROR` 가 아니라 `SENS_RESET` 으로 간다 — 전송 한 번 깨진 건 다시 측정하면 되는 일이지 버스가 죽은 게 아니다. **복구 가능한 실패와 고착 실패를 다른 상태로 보낸다.**

### 4-6. 가짜 하드웨어 — 30줄이면 된다

```c
typedef struct {
    int      not_ready_times;   /* 이 횟수만큼 "아직 준비 안 됨"을 반환 */
    int      nack_times;        /* 이 횟수만큼 NACK */
    bool     corrupt_crc;
    int16_t  value;
    unsigned reads, writes;
} FakeI2c;
```

필드가 전부 **연출 지시서**다. "NACK 3번 내고 그 다음 400을 줘", "CRC를 망가뜨려". 실물 센서로는 이런 걸 시킬 방법이 없다. `reads`/`writes` 는 반대로 **관측**용이라 "실제로 몇 번 버스를 쳤나"를 테스트가 검사할 수 있다. 가짜 시계는 더 짧다.

```c
typedef struct { uint32_t ms; } FakeClock;           /* 시간을 테스트가 조종한다 */
static uint32_t fake_now(void *ctx) { return ((FakeClock *)ctx)->ms; }
```

테스트가 `c.ms = 1000;` 이라고 쓰면 드라이버에게는 1초가 흐른 것이다. **실제 대기 시간은 0이다.** 그리고 주입이 일어나는 곳은 아래 세 줄뿐이다.

```c
static void setup(Sensor *s, FakeI2c *f, FakeClock *c)
{
    I2cOps io   = { fake_read, fake_write, f };
    ClockOps ck = { fake_now, c };
    sensor_init(s, io, ck, 0x62, 1000);
}
```

제품 코드에서는 같은 자리에 진짜 HAL 함수 이름과 진짜 I2C 핸들이 들어간다. **드라이버 본체는 한 글자도 안 바뀐다.**

## 5. 전체 흐름 따라가기

case 1(정상 경로)에서 테스트와 드라이버와 fake가 주고받는 순서.

```
 테스트                     드라이버                  FakeI2c / FakeClock
 ─────────────────────────────────────────────────────────────────────
 f.not_ready_times=2, f.value=812, c.ms=0
 start_measure() ───────>  i2c.write(STATUS,1) ──>  writes++ , IO_OK
              <───────── SENS_OK          op_start_ms = now() = 0
 poll() ────────────────>  i2c.read(STATUS) ────>  not_ready(2→1), 0x00
              <───────── SENS_EAGAIN      retries=1, 0-0 < 1000
 c.ms += 10   ← 테스트가 시간을 민다 (실제로는 0초)
 poll() ────────────────>  i2c.read(STATUS) ────>  not_ready(1→0), 0x00
              <───────── SENS_EAGAIN      retries=2, 10-0 < 1000
 c.ms += 10
 poll() ────────────────>  i2c.read(STATUS) ────>  STATUS_RDY
                           i2c.read(DATA,3) ────>  [03,2C,crc]
              <───────── SENS_OK          crc8 일치 → *out = 812
 assert(ppm == 812 && s.retries == 2)
```

case 3(타임아웃)은 여기서 `c.ms = 1000;` 한 줄만 다르다. **1초짜리 타임아웃을 0초에 검증한다.** 이 한 줄이 "시계도 의존성이다"의 전부다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 드라이버가 HAL을 직접 `#include` | 호스트에서 빌드조차 안 됨 | 컴파일 타임 결합 | ops 구조체로 주입 |
| 드라이버 안에서 `HAL_GetTick()` | 타임아웃 테스트가 실제로 1초 걸려 CI에서 빠짐 | 시간이 주입 불가 | `ClockOps.now_ms` |
| 드라이버 안에 `delay()` / `sleep()` | 태스크 하나를 통째로 점유, 테스트가 느려짐 | 블로킹 폴링 | `SENS_EAGAIN` 반환하는 논블로킹 폴링 |
| `now >= start + timeout` 비교 | 49.7일마다 한 번 오작동 | 32비트 틱 wraparound | `now - start >= timeout` (unsigned 뺄셈) |
| CRC 실패에도 `*out_ppm` 대입 | 오염된 측정값이 상위로 전파 | 실패 경로에서 출력 오염 | 성공 경로에서만 출력 대입 |
| 모든 실패를 일괄 재시도 | 배선 끊김인데 무한 재시도 | 재시도 가능/불가능 미구분 | `IO_NACK` 은 재시도, `IO_BUS_ERR` 은 즉시 실패 |
| 에러를 로그만 찍고 `void` 반환 | 상위가 실패를 모름, 조용한 오동작 | 에러가 값이 아님 | `SensorErr` 로 반환 |

## 7. 직접 확인하기

```sh
make sol N=10
```

```
  case1 ok: ppm=812 retries=2
  case2 ok: NACK 3회 후 복구, retries=3
  case3 ok: 타임아웃 → ERROR 고착
  case4 ok: CRC 오류 시 값 미반영 + 재측정 가능
  case5 ok: crc8 순수 함수
10 PASS
```

**주목할 것은 출력이 아니라 걸린 시간이다.** 타임아웃 1초짜리 경로를 포함해 5가지 시나리오가 전부 즉시 끝난다. 실물 센서로 같은 걸 하면 최소 몇 초가 걸리고, CRC 오류는 아예 만들 수가 없다. 이 문제는 스레드를 안 쓰므로 `make tsan N=10` 은 의미가 없고(돌려도 깨끗하게 통과한다), 대신 `-fsanitize=address,undefined` 로 빌드해 배열 경계와 정수 승격을 확인하는 쪽이 유용하다. 단 `make asan N=10` 은 `starters/10_testable_driver.c` 를 먼저 찾으므로 구현을 채우기 전에는 링크 에러가 나는 게 정상이다.

가장 좋은 연습은 새 경로를 테스트에 추가하는 것이다. `fake_read` 가 `REG_DATA` 에서 `IO_BUS_ERR` 를 내게 만들고, `sensor_poll` 이 `SENS_ESTATE` 를 반환하며 `state == SENS_ERROR` 로 고착하는지 확인해 보면 "재시도 불가 실패" 분기가 실제로 그렇게 동작하는지 눈으로 보인다.

## 8. 면접에서 말하기

- 드라이버가 HAL을 직접 부르지 않게 한다. `I2cOps`, `ClockOps` 같은 함수 포인터 구조체로 의존성을 주입받으면 제품에서는 진짜 HAL을, 테스트에서는 fake를 꽂는다. 드라이버 본체는 한 줄도 안 바뀐다. Linux 커널의 `file_operations` 와 같은 패턴이다.
- 시계도 의존성이다. `HAL_GetTick()` 을 직접 부르면 1초 타임아웃을 검증하는 데 진짜 1초가 걸리고, 그런 테스트는 결국 CI에서 빠진다. 시간을 주입하면 테스트가 시간을 조종해서 타임아웃 경로가 0초에 끝난다.
- 정책은 I/O에서 분리한다. 재시도할지, 타임아웃인지, CRC가 맞는지는 상태와 입력만 보는 판단이다. 이걸 버스 접근과 섞지 않으면 fake 하나로 전부 검증되고, CRC 계산 같은 순수 함수는 아예 따로 뽑아 단독 테스트한다.
- 에러는 값으로 반환한다. 그리고 재시도 가능한 실패(NACK)와 고착 실패(버스 오류)를 다른 상태로 보낸다. CRC가 깨지면 출력 변수를 아예 건드리지 않아서 오염된 값이 위로 새어나가지 않는다.
- 하드웨어 없이 어떻게 테스트하느냐 — 3층으로 나눈다. **unit**: fake HAL과 가짜 시계로 모든 분기를 밀리초 안에, 커밋마다. **integration**: 진짜 버스에 붙여 HIL 랙에서 타이밍과 실제 센서 동작을, 야간에. **system**: thermal chamber, power cycling, 링크 절체 같은 환경 스트레스를 릴리스 전에. SSD 펌웨어에서 온도 프로파일과 전원 차단 시퀀스를 돌리던 챔버 자동화 리그가 딱 그 system 층이었다. 각 층이 잡는 버그가 다르고, 아래층에서 잡을 수 있는 걸 위층으로 올리면 비용이 몇 백 배가 된다.

English:

- The driver never calls the HAL directly. It takes an `I2cOps` and a `ClockOps` — plain function-pointer structs — so production plugs in the real HAL and tests plug in fakes, with zero changes to the driver. Same pattern as `file_operations` in the kernel.
- The clock is a dependency too. If the driver calls `HAL_GetTick()`, verifying a one-second timeout takes a real second, and tests like that get dropped from CI. With an injected clock the test just advances the fake time, so the timeout path runs in zero seconds.
- I separate policy from I/O. Retry, timeout and CRC decisions are pure logic over state and inputs; keeping them out of the bus access is what makes a single fake enough to cover every branch. Pure helpers like the CRC go in their own testable function.
- Errors come back as values, never as asserts or swallowed logs, and I distinguish retryable failures like a NACK from sticky ones like a bus error. On a CRC mismatch the output variable is left untouched, so a corrupted reading never escapes upward.
- For "how do you test this without hardware": three layers. Unit tests with a fake HAL and fake clock cover every branch in milliseconds on every commit. Integration tests run against a real bus on a HIL rack nightly. System tests cover the environment — thermal chamber, power cycling, link loss — before release. That's the same structure I ran on SSD firmware, where the chamber automation rig was the system layer; each layer finds a different class of bug, and anything you push up a layer costs orders of magnitude more to catch.

## 9. 요약 & 체크리스트

테스트 가능성은 테스트를 잘 쓰는 문제가 아니라 **설계 문제**다. 드라이버가 의존하는 것을 함수 포인터 구조체로 밖에서 받게 만들면 하드웨어가 사라진다. 시계도 똑같은 의존성이라서 주입해야 타임아웃 경로를 0초에 검증할 수 있다. 정책을 I/O에서 떼고 순수 함수를 먼저 뽑아내면 fake 하나로 모든 분기가 덮인다. 에러는 값으로 돌려주고, 재시도 가능한 실패와 고착 실패를 구분하고, 실패 경로에서는 출력을 오염시키지 않는다. 그 위에 unit / integration / system 3층을 올리면 "하드웨어 없이 어떻게 테스트하나"에 대한 답이 완성된다.

- [ ] 함수 포인터 구조체로 인터페이스를 만들고 `ctx` 로 상태를 넘기는 이유를 설명할 수 있다
- [ ] 같은 드라이버에 진짜 HAL과 fake를 꽂는 코드가 어디 세 줄인지 짚을 수 있다
- [ ] 시계를 주입해야 타임아웃 테스트가 0초에 끝나는 이유를 말할 수 있다
- [ ] `now - start >= timeout` 이 wraparound에 안전한 이유를 안다
- [ ] 재시도 가능한 실패(NACK)와 고착 실패(버스 오류)를 구분해 설계할 수 있다
- [ ] CRC 실패 시 출력 변수를 건드리지 않는 이유와, 순수 함수를 먼저 뽑는 것이 왜 중요한지 말할 수 있다
- [ ] unit / integration / system 3층을 각각의 도구·주기와 함께 말할 수 있다
