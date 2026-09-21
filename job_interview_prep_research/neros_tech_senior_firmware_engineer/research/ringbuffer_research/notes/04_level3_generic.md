# 🧩 레벨 3 — 타입에 무관한 링버퍼 (generic)

> **목표**: `int` 전용이던 링을 **아무 타입이나 담는 부품**으로 만든다.
> C 에는 템플릿이 없으므로 방법은 둘뿐이다 — **런타임 방식(`void*` + `elem_size`)** 과
> **컴파일 타임 방식(매크로 코드 생성)**. 각각의 대가가 다르고, 임베디드에서는
> 그 대가가 실제로 문제가 된다. 그리고 **정렬(alignment)** 이라는 새 함정이 등장한다.
>
> 연습: `make prob N=L3_generic`

---

## 1. 왜 필요한가

실제 펌웨어에는 링버퍼가 한 종류만 있지 않다.

```c
uint8_t        uart_rx[256];      /* 바이트 스트림 */
imu_sample_t   imu_q[32];         /* 14바이트 구조체 */
log_record_t   log_q[64];         /* 16바이트 레코드 */
can_frame_t    can_q[16];         /* 16바이트 */
uint16_t       adc_q[512];        /* 샘플 */
```

레벨 2를 다섯 번 복붙하면 **다섯 벌의 버그**가 생긴다. 하나를 고치면 넷을 잊는다.

---

## 2. 방법 A — 런타임 방식 (`void*` + `elem_size`)

```c
typedef struct {
    uint8_t *buf;     /* cap * esz 바이트 */
    size_t   esz;     /* 원소 하나의 크기 */
    size_t   cap;     /* 원소 개수 (2의 거듭제곱 아니어도 됨) */
    size_t   head, tail, count;
} rbg_t;

bool rbg_push(rbg_t *q, const void *elem) {
    if (q->count == q->cap) return false;
    memcpy(q->buf + q->head * q->esz, elem, q->esz);   /* ★ 곱셈 + memcpy */
    if (++q->head == q->cap) q->head = 0;
    q->count++;
    return true;
}
```

### 장점

- 링 구현이 **딱 한 벌**. 버그를 한 번만 고친다.
- 런타임에 크기가 정해져도 된다 (플러그인, 설정 파일 기반)
- `cap` 이 2의 거듭제곱일 필요 없음 (여기서는 `count` 방식 = 레벨 1 구조를 썼다)

### 대가

| 대가 | 설명 |
|---|---|
| **곱셈** | `head * esz` — 원소마다. M0 에서 `MULS` 는 1사이클이지만 `size_t` 가 32비트면 여전히 붙는다 |
| **`memcpy` 호출** | 4바이트 `int` 하나 넣는데 `memcpy` 함수 호출. 인라인 대입보다 **5~10배** 느릴 수 있다 |
| **타입 안전성 상실** | `rbg_push(&imu_q, &some_float)` 가 **컴파일된다.** 💀 |
| **정렬 책임이 사용자에게** | 아래 §4 |

> 작은 원소(1~4바이트)를 초당 수십만 번 넣는 경로라면 이 오버헤드는 실측된다.
> 큰 원소(16바이트 이상)를 초당 수천 번이면 무시할 수 있다. **측정하고 고르라.**

---

## 3. 방법 B — 매크로 코드 생성 (타입 안전 + 빠름)

```c
#define RB_DECLARE(name, type, capacity)                                   \
    static type     name##_buf[capacity];                                  \
    static unsigned name##_head, name##_tail, name##_count;                \
                                                                           \
    static inline bool name##_push(type v) {                               \
        if (name##_count == (capacity)) return false;                      \
        name##_buf[name##_head] = v;              /* ★ 대입 — memcpy 없음 */ \
        if (++name##_head == (capacity)) name##_head = 0;                  \
        name##_count++;                                                    \
        return true;                                                       \
    }                                                                      \
    static inline bool name##_pop(type *out) { /* ... */ }                 \
    static inline unsigned name##_count_get(void) { return name##_count; }

/* 사용 */
RB_DECLARE(imuq, imu_sample_t, 4)
RB_DECLARE(cmdq, uint8_t,      32)

imuq_push(sample);          /* 타입이 틀리면 컴파일 에러 ✅ */
```

### 장점

- **컴파일 타임 타입 체크.** 잘못된 타입을 넣으면 빌드가 깨진다.
- `memcpy` 대신 **구조체 대입** → 컴파일러가 최적의 코드를 낸다 (작은 구조체는 레지스터로)
- `capacity` 가 상수라서 `% capacity` 나 `& mask` 를 **컴파일러가 상수 폴딩**한다
- 인라인되므로 함수 호출 오버헤드 0

### 대가

| 대가 | 설명 |
|---|---|
| **코드 중복 = flash** | 큐 종류마다 push/pop 코드가 통째로 복제된다. 5종류면 5벌 |
| **디버깅 지옥** | 매크로가 한 줄로 펼쳐져 브레이크포인트를 못 건다. `gcc -E` 로 펼쳐 봐야 한다 |
| **에러 메시지가 끔찍** | 매크로 안의 오타는 해독 불가능한 에러를 낸다 |
| **매크로 위생(hygiene)** | 인자를 반드시 괄호로 감싸야 한다. `capacity` 를 `(capacity)` 로 쓴 이유 |

> 리눅스 커널의 `kfifo` 가 정확히 이 방식이다 (`DECLARE_KFIFO(name, type, size)`).
> 커널 수준에서 이걸 감수한다는 건, **성능이 중요한 경로에서는 이게 정답**이라는 뜻이다.

---

## 4. ⭐⭐ 정렬(alignment) — 이 레벨의 진짜 함정

런타임 방식은 스토리지를 사용자가 준다. 그런데 이렇게 주면 어떻게 될까?

```c
static uint8_t storage[32 * sizeof(imu_sample_t)];   /* ❌ 위험 */
rbg_init(&q, storage, sizeof(imu_sample_t), 32);
```

`uint8_t` 배열의 정렬 요구는 **1바이트**다. 컴파일러가 이 배열을 홀수 주소에 놓아도 합법이다.
그런데 `imu_sample_t` 안에는 `uint32_t` 가 있으니 **4바이트 정렬**이 필요하다.

```
 imu_sample_t { uint32_t t_us; int16_t gx, gy, gz; }   /* align = 4 */

 storage 가 0x20000001 에 잡혔다면...
   memcpy 로는 무사히 복사된다 (memcpy 는 바이트 단위라 안전)
   하지만 rbg_at() 이 준 포인터를 (imu_sample_t*) 로 캐스팅해 필드를 읽는 순간:
```

| 플랫폼 | 결과 |
|---|---|
| x86 | 동작 (느림) |
| Cortex-M3/M4/M7 (`LDR`) | 보통 동작 — 하드웨어가 비정렬 접근 지원 |
| Cortex-M0/M0+ | 💥 **HardFault** |
| Cortex-M 의 `LDM`/`STM`/`LDRD` | 💥 **HardFault** (정렬 지원이 안 되는 명령) |
| `-mno-unaligned-access` 로 빌드 | 💥 **HardFault** |
| Cortex-A / Linux (일부 설정) | SIGBUS 또는 커널 trap (매우 느림) |

### 해법 셋

```c
/* ① 가장 안전: 타입이 있는 배열로 스토리지를 잡는다 */
static imu_sample_t storage[32];
rbg_init(&q, storage, sizeof(imu_sample_t), 32);

/* ② C11 alignas */
#include <stdalign.h>
static alignas(imu_sample_t) uint8_t storage[32 * sizeof(imu_sample_t)];

/* ③ union 으로 최대 정렬 강제 (C99 호환) */
static union { imu_sample_t _a; uint8_t bytes[32 * sizeof(imu_sample_t)]; } storage;
```

> **면접 포인트**: "generic 링버퍼를 만들 때 주의할 점?" 이라는 질문의 모범 답에 정렬이 들어간다.
> `memcpy` 로만 접근하면 비정렬이어도 안전하지만, **사용자에게 포인터를 돌려주는 API
> (`rbg_at`, zero-copy)** 를 만드는 순간 정렬 계약이 필요하다.

---

## 5. 구조체를 담을 때 — 패딩도 같이 복사된다

```c
typedef struct { uint32_t t_us; int16_t gx, gy, gz; } imu_sample_t;
/*   offset: 0        4   6   8                     sizeof = 12 (2바이트 패딩) */
```

`memcpy(dst, src, sizeof(imu_sample_t))` 는 **패딩 바이트까지 복사**한다. 이건 링버퍼 안에서는 문제가 없다.
문제가 되는 곳은 따로 있다.

| 상황 | 패딩이 문제인가 |
|---|---|
| 링버퍼에 넣고 빼기 | ❌ 문제없음 (같은 레이아웃끼리 복사) |
| `memcmp` 로 두 구조체 비교 | ⚠️ **패딩의 쓰레기 값 때문에 틀린다** |
| 구조체를 그대로 UART/플래시에 기록 | ⚠️ **컴파일러/버전마다 패딩이 달라진다** → 반드시 직렬화 |
| 구조체를 그대로 메모리 덤프 | ⚠️ 패딩에 이전 스택 내용이 남아 **정보 유출** |

> 규칙: **링버퍼 안 = 구조체 그대로 OK. 링버퍼 밖(와이어/플래시) = 직렬화 필수.**

---

## 6. 어느 방식을 고를까

```
원소가 바이트 스트림인가? ──── 예 ──► 레벨 2 (uint8_t 전용) — generic 불필요
        │ 아니오
        ▼
큐 종류가 2~3개 이하인가? ──── 예 ──► 매크로 방식 (RB_DECLARE)
        │ 아니오
        ▼
성능이 빡빡한 경로인가? ─────── 예 ──► 매크로 방식 (flash 를 감수)
        │ 아니오
        ▼
                              런타임 방식 (rbg_t) — 코드 한 벌, 유지보수 승리
```

실무에서 가장 흔한 조합은 **혼합**이다.

- UART/로그 같은 **바이트 스트림** → 레벨 2 바이트 링 한 벌
- IMU/CAN 같은 **고정 크기 메시지** → 매크로로 타입별 생성
- 설정·진단처럼 **드물게 쓰는 것** → 런타임 `rbg_t`

---

## 7. 한 걸음 더 — 가변 길이 레코드

고정 크기 원소로 안 되는 경우가 있다. 로그 메시지는 길이가 제각각이다.
그때는 **원소 = 바이트**로 두고 그 위에 **레코드 프레이밍**을 얹는다.

```
  [len][payload ...][len][payload ...][len][payload ...]
   1B    len B
```

- 길이 헤더 1바이트 = 최대 255바이트 레코드
- push 는 **all-or-nothing** 이어야 한다 (반쪽 레코드 = 파서 붕괴)
- 공간이 모자라면? → 새 것을 버리거나(레벨 2 방식), **오래된 레코드를 통째로** 버리거나(레벨 5a)

이 구조를 [레벨 5a](06_level5_overwrite_zerocopy.html)에서 `rlog_*` 로 직접 만든다.

---

## 8. 체크

- [ ] `void*` 방식에서 `memcpy` + 곱셈이 왜 오버헤드인지, 언제 무시해도 되는지 판단 기준을 말할 수 있다
- [ ] `RB_DECLARE` 매크로를 `##` 토큰 붙이기와 함께 안 보고 쓸 수 있다
- [ ] 매크로 방식의 대가 3가지(flash, 디버깅, 에러 메시지)를 말할 수 있다
- [ ] `uint8_t storage[]` 로 구조체 링을 만들면 왜 위험한지, Cortex-M0 에서 뭐가 터지는지 안다
- [ ] `alignas` 또는 타입 있는 배열로 정렬을 보장하는 코드를 쓸 수 있다
- [ ] 구조체 패딩이 링버퍼 안에서는 문제없고 와이어 포맷에서는 문제인 이유를 설명할 수 있다
- [ ] 바이트 스트림 / 고정 메시지 / 가변 레코드 각각에 맞는 방식을 고를 수 있다

다음: **[레벨 4 — SPSC lock-free (인터럽트 ↔ 메인 루프)](05_level4_spsc.html)**
