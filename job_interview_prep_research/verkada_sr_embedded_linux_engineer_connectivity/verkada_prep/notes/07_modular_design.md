# 🧩 모듈화 · 테스트 가능한 설계 (Modular & Testable Embedded Design) — Q59~68

> 리크루터 메일의 **네 번째 주제**가 그대로 이 세트다: *"design principles for
> modular, testable embedded software."* 세션 후반이 시스템 설계인 만큼, 거의
> 확실히 나오는 질문은 하나다 — **"하드웨어 없이 이걸 어떻게 테스트하나?"**
> 이 노트는 그 질문에 3분 안에 끝까지 답하는 스크립트를 만든다.

---

## 1. 핵심 아이디어

임베디드 코드가 테스트 불가능해지는 이유는 거의 항상 **숨은 의존성** 때문이다.

```c
// ❌ 테스트 불가능한 드라이버
int sensor_read(int16_t *out) {
    HAL_I2C_Mem_Write(&hi2c1, 0x62, REG_CMD, 1, &cmd, 1, 100);  // 전역 버스
    HAL_Delay(50);                                              // 실제 50ms 소모
    uint32_t t0 = HAL_GetTick();                                // 전역 시계
    while (!ready()) {
        if (HAL_GetTick() - t0 > 1000) { printf("timeout\n"); return -1; }  // 전역 로그
    }
    ...
}
```

이 함수를 테스트하려면 **보드가 필요하고, 타임아웃 경로 하나 검증하는 데 1초씩
실제로 기다려야 하고**, 로그가 맞게 찍혔는지는 눈으로 봐야 한다. CI에 넣을 수 없다.

고칠 방법은 하나다. **숨은 의존성을 인자로 끌어올린다.**

```c
typedef struct { IoStatus (*read)(...); IoStatus (*write)(...); void *ctx; } I2cOps;
typedef struct { uint32_t (*now_ms)(void *ctx); void *ctx; }               ClockOps;
typedef struct { void (*write)(void *ctx, LogLevel, const char *); void *ctx; } LogOps;
```

이제 드라이버는 **인터페이스에만 의존**한다. 제품 빌드에서는 진짜 HAL을,
테스트에서는 fake를 꽂는다. 이 세트의 4대 원칙 — 면접에서 **이름을 붙여서** 말할 것:

| # | 원칙 | 한 줄 |
|---|---|---|
| 1 | **의존성 주입** (dependency injection) | 드라이버는 `I2cOps`/`ClockOps`만 안다. HAL 이름조차 모른다 |
| 2 | **시간도 의존성이다** | `now_ms()`를 주입하면 테스트가 시계를 조종한다 → 타임아웃 검증 0초 |
| 3 | **정책과 I/O 분리** | 재시도·백오프·CRC·전이는 **순수 함수/테이블** → 버스 없이 단위 테스트 |
| 4 | **에러는 값으로 반환** | 드라이버는 죽지도(assert/panic), 삼키지도 않는다. 정책은 호출자가 |

---

## 2. C에서 의존성 주입을 만드는 3가지 방법

C에는 인터페이스 키워드가 없다. 대신 **세 가지 치환 지점**이 있고, 셋의 트레이드오프를
말할 수 있어야 한다.

| 방식 | 치환 시점 | 비용 | 장점 | 단점 |
|---|---|---|---|---|
| **① 함수 포인터 구조체 (ops/vtable)** | **런타임** | 간접 호출 1회 (~1-3 cycle + 인라인 불가) | 한 바이너리에 여러 구현 공존, 테스트가 가장 깔끔, 런타임 교체 가능 | 인라인·상수전파를 잃는다, 포인터 NULL 방어 필요 |
| **② 링크 타임 치환 (weak symbol / 다른 .o)** | **링크** | **0** | 제품 코드가 평범한 직접 호출 그대로, 오버헤드 없음 | 한 바이너리에 한 구현만, 빌드 시스템이 복잡, "어느 구현이 링크됐지?" 추적 어려움 |
| **③ 매크로 / 컴파일 타임 치환** (`#define HAL_READ ...`) | **전처리** | **0** | 헤더 몇 줄로 끝, 핫패스에 적합 | 타입 안전성 없음, 디버깅 지옥, `#ifdef UNIT_TEST` 가 코드에 번진다 |

**실무 선택**: 기본값은 ①. 초당 수십만 번 불리는 **증명된 핫패스**에서만 ②·③으로
내린다. 이 세트의 드라이버가 ①을 쓰는 이유는 I2C 트랜잭션 하나가 수백 마이크로초라
간접 호출 한 번이 **0.001% 미만**이기 때문이다.

```c
// ① ops 구조체: 값으로 복사해 보관 → 호출자의 지역변수여도 안전
void aq_init(AqDriver *d, I2cOps i2c, ClockOps clk, uint8_t addr, uint32_t timeout_ms) {
    memset(d, 0, sizeof *d);
    d->i2c = i2c;  d->clk = clk;           // vtable 을 '값'으로 들고 있는다
    d->addr = addr;  d->timeout_ms = timeout_ms;
    d->state = AQ_S_IDLE;
}
```

```c
// ② 링크 타임: 제품은 hal_i2c.o, 테스트는 fake_i2c.o 를 링크
__attribute__((weak)) IoStatus i2c_read(uint8_t addr, uint8_t reg, uint8_t *b, size_t n);
```

> **면접 한마디**: *"C에서 DI는 결국 vtable을 손으로 만드는 일이다. 런타임 유연성이
> 필요 없으면 링크 타임 치환이 비용 0이지만, 나는 테스트 가독성과 한 바이너리 안에서의
> 다중 구현 때문에 기본적으로 ops 구조체를 쓴다."*

---

## 3. 무엇을 주입해야 하나 — 시간, I/O, 난수, 로그

테스트를 망치는 것은 **비결정적이거나 느린 전역 상태**다. 전부 같은 병이다.

| 의존성 | 직접 부르면 | 주입하면 |
|---|---|---|
| **시간** `HAL_GetTick()`, `clock_gettime()` | 타임아웃 1개 검증에 실제 1초. 랩어라운드는 49.7일 기다려야 재현 | `c.ms += 1000;` 한 줄로 타임아웃·랩어라운드 즉시 재현 |
| **I/O** I2C/UART/소켓 | 보드·센서·네트워크 필요. NACK·버스 오류를 의도적으로 못 만든다 | fake가 NACK 2회 → 성공, CRC 깨짐, 영구 버스 오류를 **주문형**으로 생성 |
| **난수** `rand()` | 재시도 jitter 때문에 테스트가 플래키 | seed 주입 또는 `rng_ops` → 실패 케이스를 그대로 재현 |
| **로그** `printf()` | "로그가 찍혔나"를 눈으로 확인 | 링 버퍼 싱크에 모아 `log_has(&ring, "timeout")` 로 **단정** |

시계 주입의 핵심 코드 — **랩어라운드 안전한 데드라인**:

```c
bool deadline_expired(uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms) {
    return (uint32_t)(now_ms - start_ms) >= timeout_ms;   // ✅ unsigned 뺄셈
    // ❌ now_ms >= start_ms + timeout_ms  → 32비트 tick 랩 근처에서 영원히 미만료
}
```

```c
FakeClock c = { .ms = 500 };
aq_start_measure(&d);                       // op_start_ms = 500
assert(aq_wait_ready(&d) == AQ_EAGAIN);
c.ms = 1500;                                // ★ 시계를 1초 앞으로 — 실제 대기 0초
assert(aq_wait_ready(&d) == AQ_ETIMEDOUT);
```

이 세트의 테스트 87개가 **전부 합쳐 10ms 미만**에 끝나는 이유다. 그리고 **스레드가
하나도 없다** — 좋은 설계는 테스트에서 동시성을 제거한다. 논블로킹 `aq_service()`
한 틱씩 호출하면 "센서 폴링"이라는 본질적으로 비동기적인 일이 완전히 결정적인
함수 호출 시퀀스가 된다.

---

## 4. 정책을 순수 함수와 테이블로 빼기

### 4.1 재시도 정책 = 순수 함수

```c
bool should_retry(IoStatus st, unsigned attempt, unsigned max_attempts) {
    if (st == IO_OK)   return false;      // 성공은 재시도 대상이 아니다
    if (st != IO_NACK) return false;      // 버스 오류는 '영구' → 상위로 올린다
    return attempt < max_attempts;        // 예산 안에서만
}
uint32_t next_delay_ms(unsigned attempt, uint32_t base_ms, uint32_t cap_ms);  // 지수 + 상한
```

버스도 시계도 없이 **표를 그대로 테스트로 옮길 수 있다**. 그리고 여기서
"일시적(NACK) vs 영구(BUS_ERR)"를 구분한 것이 설계의 핵심 — 이걸 같은 에러 코드로
뭉개면 상위 레이어가 **재시도할지 포기할지 결정할 수 없다**.

### 4.2 상태머신 = 전이 테이블

```c
static const uint8_t TBL[AQ_S__COUNT][AQ_E__COUNT] = {
    /*              START           DATA_RDY      DECODED       RETRY      FAIL        RECOVER      */
    /* IDLE      */{AQ_S_MEASURING, AQ_S_INVALID, AQ_S_INVALID, AQ_S_IDLE, AQ_S_FAULT, AQ_S_INVALID},
    /* MEASURING */{AQ_S_INVALID,   AQ_S_READY,   AQ_S_INVALID, AQ_S_IDLE, AQ_S_FAULT, AQ_S_INVALID},
    /* READY     */{AQ_S_INVALID,   AQ_S_INVALID, AQ_S_IDLE,    AQ_S_IDLE, AQ_S_FAULT, AQ_S_INVALID},
    /* FAULT     */{AQ_S_INVALID,   AQ_S_INVALID, AQ_S_INVALID, AQ_S_INVALID, AQ_S_FAULT, AQ_S_IDLE},
};
```

`switch` 중첩 대비 장점: ① 전이 **전체**가 한 화면에 — 화이트보드에 그대로 그릴 수
있다 ② 빈 칸 = **명시적으로 금지된 전이** → 잘못된 순서 호출을 조용히 삼키지 않고
`bad_transitions++` 후 거부 ③ 상태·이벤트 추가가 코드 수정이 아니라 **데이터 수정**.

단점도 말할 것: 전이마다 다른 **액션**이 필요하면 테이블에 함수 포인터 열이 붙으면서
읽기 어려워진다. 상태가 5개 이하이고 액션이 단순하면 `switch`가 더 읽기 쉽다.

### 4.3 출력 파라미터 오염 금지

```c
AqErr aq_decode(const uint8_t *raw3, int16_t *out_ppb) {
    if (crc8(raw3, 2) != raw3[2]) return AQ_ECRC;   // ★ 검증 먼저, out 은 손대지 않는다
    *out_ppb = (int16_t)((raw3[0] << 8) | raw3[1]);
    return AQ_OK;
}
```

**"성공했을 때만 출력을 쓴다"**는 규칙 하나가, 호출자가 반환값을 무시했을 때
쓰레기 PPM 값이 클라우드로 올라가는 사고를 막는다. 카메라/게이트웨이 fleet 200만
대에서 이런 값 하나가 오탐 알림을 만든다.

---

## 5. 테스트 3층 — "하드웨어 없이 어떻게 테스트하나"의 뼈대

| 층 | 무엇을 검증 | 어떻게 | 주기 | 하드웨어 |
|---|---|---|---|---|
| **단위 (Unit)** | 프로토콜 파싱, CRC, 재시도/백오프 정책, 전이 테이블, 설정 검증, 타임아웃 경계 | fake HAL + 가짜 시계. 이 세트의 87개 체크 | **매 커밋 / CI**, 수 ms | **0개** |
| **통합 (Integration)** | 실제 I2C 버스 타이밍, 클럭 스트레칭, 진짜 센서의 NACK 패턴, 드라이버+애플리케이션 결합 | HIL 랙(보드 몇 장) + pytest 러너, 전원/버스 결함 주입기 | 야간 / PR 게이트 | 소수 |
| **시스템 (HIL / soak)** | -40~50°C 챔버, 전원 사이클, LTE·Wi-Fi 링크 단절, PoE 부하, 30분 오프라인 자가 재부팅 | 챔버 자동화 + 네트워크 결함 주입 + 장시간 soak | 야간/주간 | 랙 전체 |

> **Don 연결고리**: SSD 펌웨어 시절 온도 챔버 회귀 자동화와 RF/무선 통합 테스트
> 경험이 그대로 3층에 대응한다. "챔버 자동화 해봤다"는 Verkada의 -40~50°C IP66
> 게이트웨이 맥락에서 강력한 신호다.

**핵심 메시지**: 단위 층이 커버 범위의 대부분을 가져가야 한다. HIL은 비싸고 느리고
플래키하다 → **HIL은 "현실과 fake가 여전히 같은가"를 확인하는 층**이지, 로직 버그를
잡는 층이 아니다.

---

## 6. 흔한 함정 (인터뷰 감점 포인트)

1. **시간을 주입하지 않음** — 타임아웃 테스트가 실제로 10초 걸린다. CI에서 제일 먼저 꺼진다.
2. **`now >= start + timeout`** — 32비트 tick 랩어라운드에서 영원히 만료되지 않는다.
3. **에러를 로그만 찍고 삼킴** — `printf("i2c fail"); return 0;`. 상위가 모른다.
4. **드라이버 안에서 `assert()`/panic** — 센서 하나 때문에 게이트웨이가 리부팅한다.
5. **검증 전에 출력 파라미터를 씀** — CRC 실패해도 쓰레기 값이 새어나간다.
6. **fake를 "mock 프레임워크"라고 부르며 동작 검증을 안 함** — 반환값만 보고 **호출 횟수/레지스터/인자**를 단정하지 않으면 레지스터를 잘못 써도 테스트가 통과한다.
7. **로그가 malloc을 쓰거나 무한 증가** — 고정 슬롯 링 + `dropped` 카운터로 상한을 둔다.
8. **설정 검증 없음** — 클라우드에서 `period_ms = 0` 이 내려오면 장치가 조용히 미친다.
9. **모든 것을 인터페이스로 추상화** — 반대 방향 과잉. 인터페이스는 **치환할 이유가 있는 경계**(HW, 시간, 네트워크, 파일시스템)에만.
10. **테스트에 스레드를 씀** — 논블로킹 스텝 API면 테스트는 단일 스레드 결정적 시퀀스로 끝난다.

---

## 7. 추상화 비용 — 균형 잡힌 답변

*"함수 포인터로 감싸면 느려지지 않나?"* 는 거의 반드시 나오는 follow-up이다.
**비용을 인정하고 → 숫자로 맥락화하고 → 언제 바꿀지 말한다.**

**비용은 실재한다**:
- 간접 호출 = 로드 1회 + 간접 분기. Cortex-M에서 대략 **2~5 cycle**, 분기 예측 실패 시 더.
- 더 큰 비용은 **인라인 불가 + 상수 전파 손실** — 컴파일러가 호출 너머를 못 본다.
- ops 구조체가 RAM/ROM을 몇 워드 먹고, NULL 방어 코드가 붙는다.
- LTO/devirtualization도 vtable이 런타임에 바뀔 수 있으면 손을 못 댄다.

**맥락**:
- I2C 트랜잭션 1회 = **100~900 µs**. 400 MHz Cortex-A에서 간접 호출 3 cycle ≈ **7.5 ns**.
  → 오버헤드 **10⁻⁵ 수준**. 측정 불가능하다.
- 얻는 것: CI에서 매 커밋 도는 회귀 테스트, 하드웨어 없이 재현되는 버그 리포트,
  새 센서 SKU를 어댑터 한 장으로 붙이는 능력.

**언제 바꾸나** (이게 진짜 답이다):
- 초당 수십만 회 불리는 **증명된** 핫패스(ISR 내부 바이트 처리, DMA 완료 콜백)라면
  컴파일 타임 치환(②/③)으로 내린다.
- 단, **프로파일 먼저**. "추상화가 느릴 것 같아서" 지우는 건 최적화가 아니라 추측이다.
- 실용적 절충: **경계는 유지하되 핫패스만 직접 호출** — 링 버퍼 push 같은 안쪽 루프는
  인라인 함수로, 버스 트랜잭션 같은 바깥 경계만 ops로.

---

## 8. 면접에서 말할 것 (한국어 + 영어)

### 8.1 "하드웨어 없이 어떻게 테스트하나" — 완성 스크립트 (60~90초)

**KO**
> "드라이버가 HAL을 직접 부르지 않게 만드는 게 출발점입니다. I2C와 **시계**, 로그를
> 함수 포인터 구조체로 주입받게 하면, 제품에서는 진짜 HAL을, 테스트에서는 fake를 꽂을
> 수 있습니다. 시간을 주입하면 타임아웃 경로를 **실제로 기다리지 않고** 검증할 수 있고요 —
> 가짜 시계를 1초 앞으로 돌리면 끝입니다.
> 그다음 재시도·백오프·CRC·상태 전이 같은 **정책은 순수 함수와 전이 테이블로 분리**해서
> 버스 없이 단위 테스트합니다. 에러는 값으로 반환하고 절대 삼키지 않습니다.
> 그러면 '시작 → NACK 두 번 재시도 → 준비 → CRC 실패 → 재측정 → 성공 → 타임아웃 후
> FAULT 고착' 같은 전체 시나리오를 fake만으로 한 테스트에 담아 **밀리초 단위로 매 커밋**
> 돌릴 수 있습니다.
> 물론 이게 전부는 아니고 3층으로 봅니다. 단위는 fake로 CI에서, 통합은 HIL 랙에서 실제
> 버스와 실제 센서로 야간에, 시스템은 온도 챔버와 전원 사이클·링크 단절까지. HIL은
> **'현실이 여전히 내 fake와 같은가'를 확인하는 층**이고, 로직 버그는 단위 층에서 잡습니다."

**EN**
> "The starting point is that the driver never calls the HAL directly. I inject the I2C
> bus, **the clock**, and the logger as structs of function pointers — production plugs in
> the real HAL, tests plug in fakes. Injecting the clock is what lets me test the timeout
> path **without actually waiting**: I just move the fake clock forward a second.
> Then I pull the policy out of the I/O — retry/backoff, CRC, and the state transitions
> become pure functions and a transition table, so they're unit-testable with no bus at
> all. Errors are returned as values; the driver never panics and never swallows them —
> the caller owns the policy.
> With that, a whole scenario — start, two NACK retries, ready, CRC failure, re-measure,
> success, then a timeout that latches into FAULT — is one deterministic test that runs
> in milliseconds on every commit.
> That's the unit layer. Above it I'd run integration on a HIL rack with real boards and
> real sensors nightly, and system-level tests in a temperature chamber with power cycling
> and link drops. The HIL layer exists to confirm **my fakes still match reality** — the
> logic bugs should already be caught by the unit layer."

### 8.2 짧은 한 방 문장들

| 상황 | KO | EN |
|---|---|---|
| DI 정의 | "C에서 의존성 주입은 결국 vtable을 손으로 만드는 일입니다." | "Dependency injection in C is just hand-rolling a vtable." |
| 시간 | "시간은 의존성입니다. 주입하지 않으면 타임아웃은 테스트할 수 없고, 테스트가 실제로 느려집니다." | "Time is a dependency. If you don't inject it, you can't test timeouts — and your tests get as slow as your timeouts." |
| 에러 | "드라이버는 정책을 정하지 않습니다. 에러를 값으로 올리고, 재시도할지 센서를 끌지는 호출자가 정합니다." | "The driver doesn't set policy. It returns errors as values; the caller decides whether to retry or shut the sensor down." |
| 오염 방지 | "검증이 끝나기 전에는 출력 파라미터를 절대 쓰지 않습니다." | "Never touch the output parameter until validation has passed." |
| fake 품질 | "반환값만 보는 테스트는 약합니다. 어떤 레지스터에 몇 번 썼는지까지 단정합니다." | "Asserting only the return value is weak — I assert which registers were touched and how many times." |
| 비용 | "간접 호출 3 cycle 대 I2C 트랜잭션 수백 마이크로초. 측정하기 전엔 최적화하지 않습니다." | "Three cycles of indirect call against a few hundred microseconds of I2C. I don't optimize what I haven't measured." |
| 과잉 추상화 방어 | "치환할 이유가 있는 경계에만 인터페이스를 둡니다 — 하드웨어, 시간, 네트워크, 파일시스템." | "I put interfaces only where there's a reason to substitute: hardware, time, network, filesystem." |
| 동시성 | "논블로킹 스텝 API면 테스트에 스레드가 필요 없습니다. 결정적인 함수 호출 시퀀스가 됩니다." | "With a non-blocking step API the tests need no threads at all — it becomes a deterministic call sequence." |

### 8.3 예상 follow-up

- *"fake가 진짜 센서와 다르면?"* → 그래서 3층이다. fake는 **데이터시트의 계약**을
  구현하고, HIL 층이 그 계약이 현실과 맞는지 주기적으로 검증한다. 실제 버그를 하나
  잡으면 **먼저 fake에 그 동작을 추가해 회귀 테스트를 만들고** 고친다.
- *"동시성은 어떻게 테스트하나?"* → ① 불변식 기반 스트레스(produced == consumed + dropped)
  ② CI에 **ThreadSanitizer** ③ 결정적 재현을 위한 스케줄 훅(특정 지점에 yield 주입)
  ④ 장시간 soak. 그리고 애초에 **논블로킹 스텝 API로 만들어 동시성을 줄인다**.
- *"코드 커버리지는?"* → 라인 커버리지보다 **경로 커버리지**. 이 드라이버는 에러 경로가
  본체이므로 NACK/타임아웃/CRC/버스오류 각각에 테스트가 하나씩 있어야 한다.
- *"메모리는?"* → malloc 0회. 로그는 고정 슬롯 링 + `dropped` 카운터, 버퍼는 호출자 제공.

---

## 9. 체크리스트

- [ ] 드라이버 코드에 `HAL_`, `printf`, `sleep`, `time()` 문자열이 **하나도 없다**
- [ ] ops 구조체를 **값으로** 복사해 보관한다 (호출자 지역변수 수명 문제 회피)
- [ ] 주입 안 된 ops(NULL 함수 포인터)를 방어한다
- [ ] `deadline_expired` 를 unsigned 뺄셈으로 써서 tick 랩어라운드에 안전하다
- [ ] 일시적 오류(NACK)와 영구 오류(BUS_ERR)가 **다른 에러 코드**로 구분된다
- [ ] 재시도에 **예산(max_attempts)과 상한 있는 백오프**가 있다
- [ ] 모든 에러 코드에 문자열이 있고, 범위 밖 값은 "UNKNOWN"
- [ ] 상태 전이가 테이블 한 곳에 선언되고, 금지된 전이는 **거부 + 카운트**된다
- [ ] CRC 검증이 **출력 파라미터를 쓰기 전에** 일어난다
- [ ] 로그는 malloc 없이 고정 슬롯, 넘치면 `dropped` 를 센다
- [ ] 설정 검증이 순수 함수이고 **이유 코드**를 돌려준다 (주기 0, 타임아웃 > 주기, 예약 주소)
- [ ] 테스트가 **반환값 + 호출 횟수 + 레지스터/인자**까지 단정한다
- [ ] 전체 스위트가 **스레드 0개, 1초 미만**에 끝나고 결정적이다
- [ ] "하드웨어 없이 어떻게 테스트하나" 90초 스크립트를 영어로 말할 수 있다
- [ ] 추상화 비용(간접 호출)을 숫자로 맥락화하고, **언제 벗겨낼지**를 말할 수 있다
