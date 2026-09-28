# Verkada 1차 스크리닝 (2026-09-25) — ALS 논블로킹 래퍼 풀이 노트

> 문제: 블로킹하는 조도 센서(ALS) API `read_next_sample()`을 감싸서
> **Part 1** 최신 lux를, **Part 2** 최근 10분 안의 임의 시점 lux를 **논블로킹 + 스레드 안전**하게 읽는 API 설계·구현.
> 기억에서 재구성한 문제와 하네스, 그리고 검증까지 끝낸 모범답안의 해설이다.

**폴더 구성** (`verkada_sep_25_2026_1st_screening_questions/`)

| 파일 | 내용 |
|---|---|
| `question_note` | 문제 원문 재구성 + 인터뷰어가 듣고 싶어 하는 10개 질문 |
| `als.h` | 벤더 헤더 (pad에서 준 것, 오타만 수정) |
| `main.c` | 하네스 + 가짜 센서 (pad 코드 복원 + 자동 채점 추가) |
| `recent_lux.h` | 우리가 구현할 API |
| `recent_lux.c` | **연습용 stub** — TODO를 채운다 |
| `recent_lux_solution.c` | 모범답안 — Part 2 = **① circular queue** (시간순 링버퍼 + 이진 탐색) |
| `recent_lux_solution_list.c` | Part 2 = **② linked list** (이중 연결 리스트, 최신 쪽부터 탐색) |
| `recent_lux_solution_rbtree.c` | Part 2 = **③ red-black tree** (timestamp 키, floor 조회) |
| `main.sh` | `./main.sh` (내 코드) · `./main.sh sol` · `./main.sh list` · `./main.sh rbtree` · `./main.sh window <sol/list/rbtree>` (윈도우 2초 축소 테스트) |

**한 줄 요약** — 블로킹 호출은 **전용 스레드 하나**에 가두고, 그 스레드가 결과를 **공유 상태에 게시(publish)**한다.
리더는 센서를 절대 건드리지 않고 공유 상태만 읽는다. Part 1은 `_Atomic uint32_t`에 float 비트를 넣어 wait-free로 처리한다.
Part 2는 인터뷰어가 말한 순서대로 **circular queue → linked list → red-black tree** 세 단계로 발전시키고, 세 가지 모두 구현·검증했다 (3.3).
가장 놓치기 쉬운 곳은 **"10분 경계 직전의 샘플 하나는 10분보다 오래됐어도 지우면 안 된다"**는 점이다. 세 구현 모두 이 규칙을 지킨다.

## 0. 문제를 요구사항으로 바꾸기

문제를 읽자마자 코드부터 쓰지 말고, 문장 하나하나를 **제약 조건**으로 번역한다. 인터뷰어는 이 과정을 본다.

| 문제 문장 | 뽑아낸 제약 | 설계에 주는 영향 |
|---|---|---|
| 1초 동안 변화가 없으면 NO_CHANGE, 1초 걸림 | 호출 한 번이 최대 1초 블로킹 | 리더 스레드에서 호출하면 안 된다 |
| 변화가 있으면 VALID, 0~1초 사이 | 값이 바뀐 **순간**에 알려 준다 (timestamp = 변화 시각) | 샘플 사이 구간은 **값이 유지**된다 (sample-and-hold) |
| 첫 호출은 블로킹 없이 VALID | 초기값은 즉시 얻을 수 있다 | `init`에서 동기로 첫 샘플을 받아 두면 리더가 "값 없음"을 볼 일이 거의 없다 |
| (하네스에만 있음) 1/10000 확률로 ERROR | ERROR일 때 `lux`, `timestamp`는 쓰레기 | ERROR는 버리고, 세고, 잠깐 쉬고 재시도 |
| (하네스 코드) `static bool first_run`, `rand()` | 벤더 함수는 **스레드 안전하지 않다** | 호출자는 **정확히 한 스레드** |
| non-blocking, thread-safe | 여러 리더가 동시에, 센서를 기다리지 않고 | 공유 상태 + 동기화 |
| Part 2: "any time within the last 10 minutes" | 과거 질의, 범위 제한 | 시간 인덱스가 있는 히스토리, 메모리 상한 필요 |

### 인터뷰 초반에 던질 확인 질문

코딩 전에 1~2분을 써서 가정을 확정한다. 말하면서 칠판(pad) 맨 위 주석으로 적어 두면 좋다.

- "Is `read_next_sample()` safe to call from multiple threads? The fake uses a static and `rand()`, so I'll assume no, and call it from exactly one thread."
- "When nothing is available yet, what should the getter return — NaN, or an error code? I'll return NaN and mention a better signature."
- "For Part 2, the value at time t is the last VALID reading at or before t, right? The sensor would have told us if it changed."
- "What's the maximum sample rate I should size for? The fake averages under 1 Hz, but a real ALS could go 10 Hz."
- "Timestamps are microseconds from `gettimeofday`, and callers pass the same clock?"
- "Non-blocking — is a short mutex OK, or do you want readers to be lock-free?"

마지막 질문이 특히 중요하다. "non-blocking"이 **센서를 기다리지 않는다**는 뜻인지, **락조차 잡지 않는다**는 뜻인지에 따라 답이 달라지고, 둘 다 알고 있다는 걸 보여 주는 질문이다.

## 1. 핵심 아이디어 — 블로킹은 한 스레드에 가두고, 결과만 공유한다

```
                       ┌──────────────────────────────┐
  read_next_sample() ◄─┤  sampler thread (딱 하나)     │   최대 1초씩 블로킹해도 상관없다
      (벤더, 블로킹)    │   while (running) {          │
                       │     r = read_next_sample();  │
                       │     VALID  → publish(r)      │
                       │     NO_CHANGE → (값 유지)     │
                       │     ERROR  → count, backoff  │
                       │   }                          │
                       └──────────────┬───────────────┘
                                      │ publish
                 ┌────────────────────┴───────────────────┐
                 ▼                                        ▼
      g_latest_bits (Part 1)                    g_hist 링버퍼 (Part 2)
      _Atomic uint32_t                          (ts, lux) 시간순 + mutex
                 ▲                                        ▲
                 │ atomic load (wait-free)                │ lock → 이진탐색 → unlock
      ┌──────────┴──────────┐                  ┌──────────┴──────────┐
      │ reader thread A, B… │                  │ reader thread C, D… │
      │ get_most_recent_lux │                  │ get_lux_at(t)       │
      └─────────────────────┘                  └─────────────────────┘
```

### 왜 getter 안에서 `read_next_sample()`을 부르면 안 되나 — 이유 세 가지

1. **블로킹**: 최대 1초. "non-blocking" 요구사항 위반 그 자체다.
2. **벤더 함수가 스레드 안전하지 않다**: `static bool first_run`과 `rand()`가 공유 상태다. 두 스레드가 동시에 부르면 데이터 레이스.
3. **이벤트 스트림이 쪼개진다** — 이게 면접에서 점수 받는 포인트다. `read_next_sample()`은 "지금 값"을 주는 함수가 아니라 **"다음 변화"를 알려 주는 스트림**이다. 리더 A와 B가 각자 부르면, 3.35 → 5.54로 바뀐 이벤트는 A 한 명에게만 가고 B는 영영 모른다. 게다가 B는 NO_CHANGE를 받고 "값 없음"으로 끝날 수도 있다. 스트림은 **소비자가 하나**여야 하고, 그 소비자가 결과를 모두에게 뿌려야 한다.

> 영어로: "`read_next_sample` is a change-notification stream, not a 'get current value' call. If two threads call it, each change event goes to only one of them. So there must be exactly one consumer, and it fans the result out through shared state."

### NO_CHANGE의 의미

NO_CHANGE는 "새 값이 없다"가 아니라 **"직전 값이 지난 1초 동안 여전히 맞았다"**는 확인(heartbeat)이다. 그래서 공유 상태를 건드릴 필요가 없다.
단, 센서가 살아 있는지(health) 보려면 "마지막으로 VALID 또는 NO_CHANGE를 받은 시각"을 따로 기록해 두면 좋다 (4장).

## 2. Part 1 — 가장 최근 값

### 2.1 v0: 틀린 답 (이렇게 쓰면 떨어진다)

```c
float get_most_recent_lux(void)
{
    SensorReading r = read_next_sample();   /* 최대 1초 블로킹, 스레드 안전하지 않음 */
    return r.lux;                           /* NO_CHANGE / ERROR면 쓰레기 */
}
```

문제 세 개가 한 줄에 다 들어 있다. 블로킹, 레이스, 그리고 NO_CHANGE일 때 `lux`가 초기화되지 않은 값.

### 2.2 v1: mutex 버전 — 면접에서 **먼저** 쓸 것

5분 안에 동작하는 답을 먼저 보여 주는 게 좋다. 최적화는 그다음에 말로.

```c
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static float g_latest = NAN;
static bool  g_have   = false;

static void *sampler_main(void *arg)
{
    for (;;) {
        SensorReading r = read_next_sample();        /* 블로킹은 여기서만 */
        if (r.status != VALID) continue;
        pthread_mutex_lock(&g_lock);
        g_latest = r.lux;
        g_have = true;
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

float get_most_recent_lux(void)
{
    pthread_mutex_lock(&g_lock);                     /* 락 보유 시간: 대입 한 번 */
    float v = g_have ? g_latest : NAN;
    pthread_mutex_unlock(&g_lock);
    return v;
}
```

이 코드는 **센서를 기다리지 않는다**는 의미에서 이미 non-blocking이다. 락을 잡고 있는 시간은 float 대입 한 번(수 나노초)뿐이라, 리더가 기다려도 그 정도만 기다린다.
1초 걸리는 센서 호출은 **락 밖**에 있다. 이게 핵심이다 — 락 안에서 `read_next_sample()`을 부르면 모든 리더가 최대 1초씩 막힌다.

### 2.3 v2: atomic 버전 — 모범답안이 쓰는 방식

값이 float **하나**뿐이면 락이 필요 없다. 원자적 변수 하나로 충분하다.

```c
/* IEEE-754 비트. 0x7FC00000 = quiet NaN = "아직 값 없음" */
static _Atomic uint32_t g_latest_bits = 0x7FC00000u;

static void publish_latest(float lux)                 /* sampler 스레드 */
{
    uint32_t bits;
    memcpy(&bits, &lux, sizeof bits);                  /* UB 없는 type-punning */
    atomic_store_explicit(&g_latest_bits, bits, memory_order_release);
}

float get_most_recent_lux(void)                       /* 아무 스레드 */
{
    uint32_t bits = atomic_load_explicit(&g_latest_bits, memory_order_acquire);
    float lux;
    memcpy(&lux, &bits, sizeof lux);
    return lux;
}
```

한 줄씩 짚고 넘어갈 것:

- **왜 그냥 `float g_latest`가 아닌가?** C 표준상 일반 변수를 한 스레드가 쓰고 다른 스레드가 읽으면, 동기화가 없는 한 **데이터 레이스 = 정의되지 않은 동작**이다. 32비트 ARM/x86에서 정렬된 4바이트 store가 실제로 찢어지지는(torn) 않더라도, 컴파일러는 "다른 스레드가 안 바꾼다"고 가정하고 값을 레지스터에 캐시하거나 루프 밖으로 끌어낼 수 있다. `_Atomic`은 **하드웨어의 원자성 + 컴파일러의 가정 금지**를 둘 다 준다.
- **왜 `_Atomic float`가 아니라 `uint32_t` 비트인가?** `_Atomic float`도 C11에서 합법이고 x86/ARM에서 lock-free다. 다만 (1) 일부 툴체인/아키텍처에서 lock-free가 보장되지 않고, (2) `atomic_fetch_add` 같은 RMW는 float에 없다. 정수 비트로 두면 `atomic_is_lock_free` 걱정이 없고, 나중에 64비트로 넓혀 "lux + 짧은 timestamp"를 같이 넣기도 쉽다. 인터뷰에서는 "`_Atomic float` works too; I use the bits so it's obviously lock-free" 정도로 말하면 된다.
- **`memcpy`로 비트 변환**: `*(uint32_t *)&lux` 같은 포인터 캐스팅은 strict aliasing 위반(UB)이다. `memcpy`는 합법이고 컴파일러가 명령 0개로 최적화한다. `union` 트릭은 C에서는 허용되지만 C++에서는 UB라, 양쪽을 오가는 사람은 `memcpy`를 습관으로 둔다.
- **acquire/release가 정말 필요한가?** 공유하는 게 이 변수 **하나**뿐이면 `memory_order_relaxed`로도 값 자체는 원자적으로 보인다. acquire/release가 의미를 갖는 건 **두 개 이상을 순서대로 게시할 때**다 (예: 링버퍼에 데이터를 쓰고 → 인덱스를 올릴 때). 여기서는 "나중에 lux와 함께 다른 필드를 게시하게 되면 순서가 필요하다"는 안전 여유로 release/acquire를 썼다. 면접에서 "relaxed면 안 되나요?"라고 물으면 "for a single independent value relaxed is enough; acquire/release matters once I publish lux together with something else" 라고 답한다.
- **NaN 초기값**: `get_most_recent_lux()`가 첫 샘플 전에 불려도 쓰레기가 아니라 "없음"을 돌려준다. 호출자는 `isnan()`으로 확인한다.

### 2.4 "non-blocking"의 세 단계

면접관이 "is this really non-blocking?"이라고 물을 때 이 표로 답한다.

| 단계 | 정의 | 이 문제에서 | 예 |
|---|---|---|---|
| **센서를 기다리지 않음** | 리더가 I/O(1초 블로킹)를 기다리지 않는다 | 요구사항의 최소선 | v1 mutex 버전 |
| **lock-free** | 어떤 스레드가 멈춰도 시스템 전체는 진행한다 | 리더가 락을 전혀 안 잡는다 | seqlock, CAS 기반 |
| **wait-free** | **모든** 스레드가 유한한 단계 안에 끝난다 | 리더가 재시도도 없다 | v2 atomic load 한 번 |

v1도 실무적으로 충분히 non-blocking이지만, 락은 **우선순위 역전**(낮은 우선순위 writer가 락을 잡은 채 선점되면 높은 우선순위 reader가 기다림)의 여지가 있다. 실시간 스레드가 읽는다면 v2(wait-free)나 PI mutex(`PTHREAD_PRIO_INHERIT`)가 답이다.

### 2.5 초기화 — 첫 샘플은 `init` 안에서 동기로

```c
int init_recent_lux(void)
{
    if (g_started) return 0;
    for (int tries = 0; tries < 3; tries++) {        /* 첫 호출은 블로킹 안 함 (문제 조건) */
        SensorReading r = read_next_sample();
        handle_reading(&r);
        if (r.status == VALID) break;                /* ERROR면 몇 번 재시도 */
    }
    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) { ... return -1; }
    g_started = true;
    return 0;
}
```

왜 스레드를 먼저 띄우지 않고 첫 샘플을 여기서 받나?

- 문제에서 "첫 호출은 블로킹 없이 VALID"라고 **일부러** 알려 줬다. 이걸 쓰라는 힌트다.
- `init`이 리턴하는 순간 `get_most_recent_lux()`가 유효한 값을 준다 — 스레드 시작 타이밍에 따라 NaN을 볼 수 있는 레이스가 사라진다.
- 하네스의 `lux[1]` 체크(init 직후)가 정확히 이걸 본다. 스레드만 띄우고 끝내면 `lux[1]`은 운에 따라 NaN이 된다.

벤더 함수를 main 스레드에서 한 번 부르고 이후 sampler 스레드가 부르는 건 괜찮은가? `pthread_create`가 **happens-before**를 만든다 — 생성 전에 main이 한 모든 쓰기(`first_run = false` 포함)는 새 스레드에서 보인다. 동시에 부르는 순간이 없으니 레이스가 아니다.

### 2.6 더 좋은 API 시그니처 (말로 제안할 것)

`float`에 NaN을 섞어 "없음"을 표현하는 건 pad의 시그니처를 따른 것이다. 실제 코드라면:

```c
typedef struct { float lux; uint64_t ts_us; uint64_t age_us; } lux_reading_t;
bool als_get_latest(lux_reading_t *out);   /* false = 아직 값 없음 / 센서 고장 */
```

- 값이 **언제** 측정됐는지(ts)와 **얼마나 오래됐는지**(age)를 같이 준다 — 나이트 모드 판단 로직은 "5분 전 값"과 "방금 값"을 다르게 다뤄야 한다.
- lux와 ts를 **같이** 원자적으로 줘야 하므로, 이때는 64비트 atomic 하나에 패킹하거나(float 32비트 + 기준 시각으로부터의 ms 32비트) seqlock을 쓴다.

## 3. Part 2 — 최근 10분 안의 임의 시점

### 3.1 "시각 t의 값"을 정확히 정의한다

센서는 값이 **바뀔 때만** VALID를 준다. 그러니 lux는 계단 함수(step function)다.

```
lux
 5.54 ┤                              ●━━━━━━━━━━━━━━━━━━━━━━━  (ts=1.95 s)
 3.94 ┤●━━━━━━━━━━━━━━━━━━━┓         ┃
 3.35 ┤                    ┗●━━━━━━━━┛                          (ts=1.40 s)
      └┬────────┬──────────┬────────┬────────┬────────┬──── t (s)
       0       0.5       1.40     1.95     2.6      3.1
       ▲ t0     ▲ t1(0.2)  ▲ t2(1.6)        ▲ t3     ▲ t4
```

**정의**: `get_lux_at(t)` = timestamp ≤ t 인 VALID 샘플 중 **가장 최근 것**의 lux.

- 샘플의 timestamp와 정확히 같은 t → 그 샘플의 값 (`≤`이므로). 경계 조건을 말로 확정해 두면 이진 탐색을 짤 때 헷갈리지 않는다.
- 첫 샘플보다 이른 t → 모른다 → NaN.
- 미래(t > now) → 모른다 → NaN.
- 10분보다 오래된 t → 약속한 범위 밖 → NaN.

위 그림의 값이 glibc에서 하네스가 기대하는 값이다: t0, t1 → 3.94383, t2 → 3.35223, t3, t4 → 5.5397.

### 3.2 메모리 — 10분이면 샘플이 몇 개인가

**이 시뮬레이터 기준 평균**: 한 번 호출할 때 `next_value_change = 2초 × U(0,1)`.

- 50% 확률로 1초 이상 → NO_CHANGE, 1초 소요
- 50% 확률로 1초 미만 → VALID, 평균 0.5초 소요
- 호출당 기대 소요 시간 = 0.5×1 + 0.5×0.5 = **0.75초**, 호출당 기대 VALID = **0.5개**
- → 초당 0.5 / 0.75 ≈ **0.67개**, 10분이면 평균 **약 400개**

**최악의 경우**: VALID 간격이 U(0,1)이라 이론상 0에 한없이 가까워질 수 있다. 즉 **상한이 없다**. 이 사실을 말하는 게 중요하다 — "the fake has no rate limit, so I must cap the buffer and define what happens when it's full."

**실제 센서 기준**: ALS는 적분 시간(integration time)이 보통 100 ms 이상이라 최대 ~10 Hz. 10분 × 600초 × 10 Hz = **6000개**. 그래서 모범답안은 **8192개**(2의 거듭제곱, 마스킹용)로 잡는다.

| 레이아웃 | 샘플 크기 | 8192개 | 메모 |
|---|---|---|---|
| `{uint64_t ts; float lux;}` | 16 B (4 B 패딩) | 128 KiB | 모범답안. 단순함 우선 |
| `{uint32_t ms_since_base; float lux;}` | 8 B | 64 KiB | 기준 시각 + 상대 ms. 49일까지 표현 |
| `{uint16_t ds_since_prev; uint16_t lux_q;}` | 4 B | 32 KiB | 델타 인코딩 + 양자화. MCU용 |

리눅스 카메라 SoC에서 128 KiB는 문제 되지 않는다. 그래도 "패딩 4바이트가 있다, 줄이려면 이렇게 한다"를 말할 수 있으면 임베디드 감각을 보여 준다.

### 3.3 인터뷰어가 제시한 진행 — circular queue → linked list → red-black tree

실제 인터뷰에서 인터뷰어가 Part 2 자료구조를 이렇게 풀어 줬다:

> "보통은 **circular queue**를 먼저 이야기한다. 그다음 생각해 보면 **linked list**를 말하고,
> 세 번째로 가장 optimized된 것은 **red-black tree**다."

즉 인터뷰어가 기대한 건 **정답 하나가 아니라 발전 과정**이다. 각 단계가 앞 단계의 약점을 하나씩 고친다.
세 가지를 모두 같은 하네스에서 돌아가게 구현해 뒀다: `./main.sh sol`, `./main.sh list`, `./main.sh rbtree`.

```
① circular queue        ② linked list               ③ red-black tree
┌──┬──┬──┬──┬──┬──┐      oldest                        (ts 키로 정렬된 균형 이진 트리)
│t1│t2│t3│t4│  │  │      [t1]⇄[t2]⇄[t3]⇄[t4]                   [t4]B
└──┴──┴──┴──┴──┴──┘                      newest           /      \
 tail→        ←head                                    [t2]R     [t6]R
 고정 크기, 연속 메모리    노드마다 malloc, 크기 무제한   /  \      /  \
                                                    [t1] [t3] [t5] [t7]
 약점: 용량을 미리 정해야   약점: 조회 O(n),            높이 ≤ 2·log₂(n+1)
      (센서 속도 상한 없음)      캐시 미스, malloc       → 삽입·삭제·조회 모두 O(log n)
```

#### ① Circular queue — 보통 가장 먼저 나오는 답

- **아이디어**: 고정 크기 배열에 샘플을 시간순으로 넣는다. head에 추가, tail에서 오래된 것 제거. Part 1 연습에서 한 링버퍼 그대로다.
- **장점**: 메모리 고정(malloc 없음), 연속 메모리라 캐시 친화적, 추가/제거 O(1).
- **약점 1 — 용량**: 10분에 샘플이 몇 개 올지 미리 알아야 한다. 가짜 센서는 속도 상한이 없어서 이론상 무한대다 (3.2). 꽉 차면 오래된 걸 버려야 하고, 그러면 10분 약속이 깨진다.
- **약점 2 — 조회**: 처음 떠올리는 버전은 보통 **선형 탐색 O(n)**이다. 인터뷰어가 "더 최적화하면?"이라고 묻게 되는 지점이다.
- **보충**: 샘플이 **시간순으로만** 들어오면 배열이 이미 정렬돼 있으니 **이진 탐색으로 O(log n)**이 된다. 모범답안(`recent_lux_solution.c`)이 이 버전이다. 이 사실은 ③에서 다시 중요해진다.

#### ② Linked list — 용량 문제를 푼다

- **아이디어**: 샘플마다 노드를 `malloc`해서 이중 연결 리스트 끝에 붙인다. 오래된 쪽(head)에서 떼어 낸다.
- **장점**: **크기 제한이 없다** — 용량을 추측할 필요가 없다. 추가/제거는 여전히 O(1).
- **약점**: 조회가 **O(n)** — 리스트는 이진 탐색이 안 된다 (중간으로 점프할 수 없음). 노드가 메모리에 흩어져 있어 캐시 미스가 나고, 샘플마다 `malloc`/`free`를 한다.
- **구현 포인트 (말하면 점수)**:
  - `malloc`/`free`는 **락 밖에서**: 할당자에도 내부 락이 있고 지연 시간이 예측 불가라, 락 안에서 부르면 리더가 그 뒤에서 기다린다. 지울 노드는 락 안에서 떼어 "garbage" 리스트에 모아 두고, unlock 후에 free한다.
  - 조회는 **최신 쪽에서 거꾸로** 걷는다: 질의는 대개 최근 시각이라, 평균적으로 몇 칸만에 끝난다 (벤치마크에서 최근 5초 조회는 33 ns로 링버퍼만큼 빠르다). 그래서 이중 연결(prev 포인터)로 만든다.
  - 동적이어도 **상한은 둔다** (`LUX_MAX_NODES`): 상한 없는 리스트는 센서가 폭주하면 메모리 누수와 같다.

```c
/* ② 조회: 최신 노드에서 과거로 */
const lux_node_t *p = g_hist.newest;
while (p && p->ts > t)
    p = p->prev;                 /* 최악 O(n), 최근 질의는 O(몇 칸) */
result = p ? p->lux : NAN;
```

#### ③ Red-black tree — 인터뷰어가 말한 "가장 optimized"

- **아이디어**: timestamp를 키로 하는 **균형 이진 탐색 트리**. 동적 크기(②의 장점) + O(log n) 조회(①+이진 탐색의 장점)를 둘 다 가진다.
- 이 문제에 필요한 연산이 전부 O(log n)이다:

| 필요한 연산 | 트리 연산 | 비용 |
|---|---|---|
| 새 샘플 추가 | `rb_insert` | O(log n) |
| `get_lux_at(t)` = ts ≤ t인 마지막 샘플 | `rb_floor(t)` | O(log n) |
| 10분 밖 샘플 제거 | `rb_min` + `rb_delete` (delete-min) | O(log n) |
| 경계 규칙 (다음 샘플 확인) | `rb_successor(min)` | O(log n) |

- **Red-black 규칙 5개** (높이를 2·log₂(n+1) 이하로 유지하는 장치):
  1. 모든 노드는 빨강 또는 검정
  2. 루트는 검정
  3. 모든 리프(NIL)는 검정
  4. 빨강 노드의 자식은 둘 다 검정 (빨강이 연속으로 오지 않음)
  5. 어떤 노드에서 리프까지 가는 모든 경로의 검정 노드 수가 같다
  → 가장 긴 경로(빨강·검정 번갈아)가 가장 짧은 경로(검정만)의 2배를 넘지 못한다 → 높이 O(log n).
- **삽입 후 복구** (`rb_insert_fixup`): 새 노드는 빨강으로 넣고, 규칙 4가 깨지면 삼촌 색에 따라
  - 삼촌이 빨강 → **색만 바꾸고** 할아버지로 올라감 (case 1)
  - 삼촌이 검정 → **회전 1~2번**으로 끝 (case 2, 3)
- **floor 조회** — 이 문제의 핵심 연산. "ts ≤ t면 후보로 기록하고 오른쪽으로, 아니면 왼쪽으로":

```c
/* ③ 조회: ts <= t 인 가장 큰 키 */
rb_node_t *x = root, *best = RB_NIL;
while (x != RB_NIL) {
    if (x->ts <= t) { best = x; x = x->right; }   /* 후보. 더 큰 것 찾으러 오른쪽 */
    else            { x = x->left; }              /* 너무 큼. 왼쪽 */
}
return best;                                      /* RB_NIL → 첫 샘플보다 이른 t */
```

- **경계 규칙도 그대로**: "최솟값의 successor도 윈도우 밖일 때만 최솟값을 지운다".

```c
while (tree.count >= 2) {
    rb_node_t *next = rb_successor(rb_min(tree.root));
    if (next->ts > now || now - next->ts < LUX_WINDOW_US)
        break;                     /* 다음 샘플이 윈도우 안 → 최솟값은 경계값, 남긴다 */
    evict_min_locked(&garbage);    /* rb_delete 후 garbage로 → unlock 뒤 free */
}
```

- **링버퍼에 없는 장점 — 순서가 뒤바뀐 삽입**: 트리는 아무 순서로 넣어도 정렬을 유지한다. 시계가 뒤로 가거나(NTP), 센서가 여러 개라 이벤트가 늦게 도착해도 그냥 넣으면 된다. 링버퍼/리스트는 "마지막 ts로 clamp"하는 우회가 필요했다 (3.6).
- **리눅스 커널 연결 고리** (임베디드 리눅스 포지션이라 말할 가치가 크다): 커널에는 `include/linux/rbtree.h`가 있고, **시간으로 정렬해야 하는 데이터**에 쓴다 — hrtimer(timerqueue: 가장 빨리 만료되는 타이머 = 최솟값), CFS 스케줄러(vruntime 최솟값 = 다음 실행 태스크), epoll(fd 관리). "시각 키로 넣고, 최솟값을 빼고, floor를 찾는다"는 이 문제의 패턴과 같다.

#### 세 구현 실측 비교

같은 조건: 10 Hz × 10분 = 윈도우 안 약 6000개 샘플, `-O2`, Apple Silicon, 단일 스레드, 락/언락 비용 포함, 호출당 평균.

| 구조 | 추가 (append) | 조회: 10분 안 아무 시각 | 조회: 최근 5초 | 노드/칸 크기 | malloc |
|---|---|---|---|---|---|
| ① circular queue + 이진 탐색 | **46 ns** | **40 ns** | 39 ns | 16 B | 없음 |
| ② linked list (최신부터 탐색) | 74 ns | **3,408 ns** | **33 ns** | 32 B + malloc 헤더 | 샘플마다 |
| ③ red-black tree | 252 ns | 42 ns | 49 ns | 48 B + malloc 헤더 | 샘플마다 |

읽는 법:
- ②는 최근 질의에는 빠르지만, 10분 전 질의는 6000칸을 걸어서 **85배 느리다**. 락을 그만큼 오래 잡으니 다른 리더도 막힌다 → 인터뷰어가 ③으로 넘어가는 이유.
- ③은 조회가 어느 시각이든 **일정하게 O(log n)** (6000개 → 깊이 약 13~25).
- ③의 추가가 ①보다 5배 느린 건 `malloc` + 재균형 회전 때문. 그래도 초당 몇 번 오는 센서에 252 ns는 아무 문제 없다.
- **①+이진 탐색이 ③과 조회 속도가 같다** — 샘플이 시간순으로만 들어오기 때문이다 (sampler 스레드 하나가 시간순으로 넣음).

검증: 세 구현 모두 하네스 전체 + `window` 모드 PASS. red-black tree는 추가로 **fuzz 테스트**를 돌렸다 — 6만 번 삽입(10%는 순서 뒤바뀜) + 제거, 97번마다 RB 규칙 5개·부모 포인터·노드 수를 검사하고 floor 결과를 brute force와 12,380번 비교, AddressSanitizer + UBSan에서 위반 0.

#### 인터뷰에서 이렇게 말한다 — 인터뷰어의 흐름을 따라가며 한 단계 더

인터뷰어가 이 순서를 제시했다면 그 흐름에 **맞춰 가는 게** 먼저다. 반박하지 말고, 각 단계의 **이유**를 말하고, 마지막에 트레이드오프를 덧붙인다.

1. **Circular queue**: "My first thought is a circular buffer of (timestamp, lux) — fixed memory, O(1) append and evict. The problem is capacity: the sensor has no rate limit, so I'd have to guess the size, and a linear scan is O(n)."
2. **Linked list**: "A linked list removes the capacity guess — it grows with the data. But lookup is still O(n), every sample is a malloc, and I'd keep malloc and free outside the lock. Walking from the newest end makes recent queries cheap."
3. **Red-black tree**: "A red-black tree keyed by timestamp gives me dynamic size *and* O(log n) for everything I need — insert, floor lookup for 'last sample at or before t', and delete-min for eviction. It also handles out-of-order timestamps for free. The kernel uses the same structure for hrtimers and CFS."
4. **트레이드오프 한 줄 (시니어 포인트)**: "One nuance: here there's a single producer and timestamps arrive in order, so a sorted circular buffer with binary search already gets O(log n) lookup with no allocation — I measured about the same lookup cost. I'd choose the tree when inserts can be out of order, the size is unknown, or I need deletes in the middle."

> 핵심: ①→②→③은 "무엇을 포기하고 무엇을 얻는가"의 이야기다. ① 메모리 고정/빠름 ↔ 용량 추측, ② 용량 자유 ↔ 조회 O(n), ③ 둘 다 해결 ↔ 코드 200줄 + malloc.
> 인터뷰어가 ③을 "가장 optimized"라고 한 건 **점근적 복잡도와 일반성** 기준이다. 상수 비용과 임베디드 제약(힙 금지 등)까지 보면 ①+이진 탐색이 이기는 경우도 있다 — 이걸 말할 수 있으면 한 단계 위다.

#### 그 밖의 후보 (follow-up 대비)

| 설계 | 조회 | 메모 |
|---|---|---|
| 1초 버킷 600개 (`bucket[t/1s % 600]`) | O(1) | 1초 안의 여러 변화가 뭉개짐. ①에 인덱스로 얹으면 정확도 유지 (7장 Q3) |
| skip list | 평균 O(log n) | 트리 대신 확률적 균형. 구현이 RB tree보다 쉽고, lock-free 버전이 있다 |
| B-tree / B+tree | O(log n) | 노드에 키 여러 개 → 캐시 친화적. 샘플이 수십만 개 이상이거나 디스크에 둘 때 |
| AVL tree | O(log n) | RB보다 더 엄격한 균형 → 조회 약간 빠름, 삽입/삭제 회전이 더 많음. 여기처럼 삽입이 많으면 RB가 유리 |
| 정렬 배열 + `memmove` 삽입 | O(log n) 조회, O(n) 삽입 | 순서 뒤바뀐 삽입이 드물 때 ①의 확장 |

### 3.4 링버퍼 구조 (① circular queue 구현 상세 — 3.4~3.7은 모범답안 기준)

```c
typedef struct { uint64_t ts; float lux; } lux_sample_t;

static struct {
    pthread_mutex_t lock;
    lux_sample_t    buf[LUX_HIST_CAP];   /* 8192, 2의 거듭제곱 */
    uint32_t        head;                /* free-running: 다음에 쓸 칸 */
    uint32_t        tail;                /* free-running: 가장 오래된 칸 */
    uint32_t        overflow;            /* 꽉 차서 버린 개수 */
} g_hist = {.lock = PTHREAD_MUTEX_INITIALIZER};
```

- **free-running 인덱스**: head/tail을 `% CAP`로 자르지 않고 계속 증가시킨다. 개수 = `head - tail` (unsigned 뺄셈이라 2³²에서 wrap해도 정확). 배열 접근할 때만 `& (CAP - 1)`. 링버퍼 연습 문제에서 한 그대로다.
- **불변식**: `buf[tail .. head-1]`의 ts는 **비감소(non-decreasing)**. 이게 있어야 이진 탐색이 된다. 샘플은 시간순으로 들어오니 자연히 성립하지만, 시계가 뒤로 가면 깨진다 → 3.8에서 방어.

### 3.5 가장 중요한 함정 — 경계 직전 샘플을 지우지 마라

"10분보다 오래된 샘플은 지운다"고 단순하게 짜면 **틀린다**.

```
시간 →      [ 10분 윈도우 ─────────────────────────────── ]
   A(ts=−12분)         B(ts=−3분)          now
   ●━━━━━━━━━━━━━━━━━━━●━━━━━━━━━━━━━━━━━━━┫
        ▲
        window start = now − 10분
```

- 질의 t = now − 9분 59초. 이 시각에 유효한 값은 **A**의 값이다 (B는 3분 전에야 왔다).
- 그런데 A의 ts는 12분 전, 윈도우 밖이다. 단순 규칙이면 A는 이미 지워졌고 → **NaN**을 돌려준다. 윈도우 안의 시각인데 값을 모른다고 답하는 버그.
- 조도가 안정된 밤에는 몇십 분 동안 VALID가 하나도 안 올 수 있어서, 이 버그가 **가장 흔한 상황**에서 터진다.

**올바른 규칙**: 가장 오래된 샘플은 **그다음 샘플도 윈도우 밖일 때만** 지운다.
즉, 윈도우 시작 시점에 유효한 샘플 하나는 항상 남겨 둔다.

```c
while (n >= 2) {
    uint64_t next_ts = g_hist.buf[(g_hist.tail + 1u) & HIST_MASK].ts;
    if (next_ts > now || now - next_ts < LUX_WINDOW_US)
        break;                  /* 다음 샘플이 윈도우 안 → tail은 경계값, 남긴다 */
    g_hist.tail++;              /* 다음 샘플도 윈도우 밖 → tail은 필요 없다 */
    n--;
}
```

이 규칙을 **실제로 검증했다**: 같은 코드에서 조건만 "tail 자신이 윈도우 밖이면 삭제"로 바꾸고 `./main.sh window` 테스트를 돌리면

```
  window edge                  got nan        want 6.788647   FAIL
    (value at the edge came from a sample OLDER than the window — the keep-one-older rule was exercised)
SOME CHECKS FAILED (4 failed)
```

모범답안은 같은 테스트를 통과한다. 면접에서 이 함정을 **먼저** 말하면 인터뷰어가 기억한다.

> 영어로: "One subtle thing: the value in effect at the start of the window usually comes from a sample that's *older* than ten minutes — at night the light may not change for an hour. So I only evict the oldest sample once the *next* one is also outside the window."

### 3.6 쓰기 — `hist_append` 한 줄씩

```c
static void hist_append(uint64_t ts, float lux)
{
    uint64_t now = get_timestamp();           /* 락 밖에서 시각을 얻는다 (시스템콜) */

    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;

    if (n > 0) {                              /* 정렬 불변식 방어 (시계 역행) */
        uint64_t last = g_hist.buf[(g_hist.head - 1u) & HIST_MASK].ts;
        if (ts < last) ts = last;
    }

    while (n >= 2) { ... 3.5의 eviction ... }

    if (n == LUX_HIST_CAP) {                  /* 윈도우 정리 후에도 꽉 참 = 센서가 너무 수다스러움 */
        g_hist.tail++;                        /* 가장 오래된 것 버림 → 커버 기간이 10분보다 짧아짐 */
        g_hist.overflow++;                    /* 반드시 세서 알 수 있게 */
    }

    g_hist.buf[g_hist.head & HIST_MASK] = (lux_sample_t){.ts = ts, .lux = lux};
    g_hist.head++;
    pthread_mutex_unlock(&g_hist.lock);
}
```

- **eviction은 쓰기 쪽에서만**: 리더는 락 안에서 읽기만 한다. 리더가 정리까지 하면 리더의 락 보유 시간이 들쭉날쭉해진다.
- **eviction 비용**: while 루프가 여러 번 돌 수 있지만, 각 샘플은 딱 한 번 지워지므로 **분할 상환 O(1)**.
- **꽉 찼을 때 정책**:

| 정책 | 결과 | 언제 |
|---|---|---|
| 가장 오래된 것 버림 (선택) | 최근 데이터는 항상 정확, 오래된 쪽 커버리지 감소 | 나이트 모드처럼 "최근"이 더 중요할 때 |
| 새 것 버림 | 과거는 보존, 최신 값이 히스토리에 안 남음 | 감사 로그처럼 과거가 중요할 때 (여기선 부적합) |
| 다운샘플 (값 차이 작은 샘플 병합) | 10분 커버리지 유지, 정밀도 감소 | 메모리 엄격 + 10분 보장 필요 |
| 동적 확장 | 제한 없음 | 비추천: 락 안의 realloc, 최악 메모리 무한 |

Part 1의 최신 값은 히스토리와 **별개**로 게시되므로, 히스토리가 넘쳐도 Part 1은 영향이 없다.

### 3.7 읽기 — `get_lux_at` 한 줄씩

```c
float get_lux_at(uint64_t t)
{
    uint64_t now = get_timestamp();
    if (t > now || now - t > LUX_WINDOW_US)       /* 미래 / 윈도우 밖 → 락도 안 잡는다 */
        return NAN;

    float result = NAN;
    pthread_mutex_lock(&g_hist.lock);
    uint32_t n = g_hist.head - g_hist.tail;

    uint32_t lo = 0, hi = n;                       /* upper_bound: ts > t 인 첫 오프셋 */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (g_hist.buf[(g_hist.tail + mid) & HIST_MASK].ts <= t)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo > 0)                                    /* lo == 0 → t가 첫 샘플보다 이름 */
        result = g_hist.buf[(g_hist.tail + lo - 1u) & HIST_MASK].lux;
    pthread_mutex_unlock(&g_hist.lock);
    return result;
}
```

- **upper_bound 패턴**: "ts ≤ t인 마지막 원소" = "ts > t인 첫 원소의 바로 앞". 이진 탐색을 `lo < hi`, `hi = mid`, `lo = mid + 1` 형태로 쓰면 무한 루프와 off-by-one이 안 난다. 이 모양을 외워 두자.
- **raw 인덱스가 아니라 tail로부터의 오프셋으로 탐색**: `lo`, `hi`를 `tail`, `head`로 두고 `lo < hi`를 비교하면, head가 2³²를 넘어 0으로 wrap한 순간 비교가 뒤집힌다. 오프셋 `[0, n)`은 wrap과 무관하다.
- **`now - t > WINDOW` 순서**: `t > now`를 먼저 걸렀으므로 `now - t`는 음수(= unsigned 언더플로우)가 될 수 없다. unsigned 뺄셈 전에 대소 비교를 먼저 하는 습관.
- **락 보유 시간**: 8192개면 비교 13번. 수십~수백 나노초. 하네스 스트레스 테스트에서 4개 리더 스레드가 2초 동안 약 2천만 번 호출했고, 최악 지연 ~0.5 ms는 락이 아니라 OS 스케줄링(선점) 때문이다.

### 3.8 경계 조건 표 — 면접에서 먼저 말하기

| 입력 | 반환 | 이유 |
|---|---|---|
| t > now | NaN | 미래는 모른다 |
| now − t > 10분 | NaN | 약속한 범위 밖 (내부에 남아 있어도 안 준다 — 계약을 일관되게) |
| 윈도우 안이지만 첫 샘플보다 이름 (부팅 직후) | NaN | 모른다 |
| t == 어떤 샘플의 ts | 그 샘플의 값 | `≤` 정의 |
| 같은 ts의 샘플 두 개 | 나중에 들어온 것 | upper_bound가 마지막 것을 고름 |
| 시계가 뒤로 감 (새 ts < 마지막 ts) | ts를 마지막 값으로 clamp | 정렬 불변식 유지. 근본 해결은 monotonic clock (4장) |
| 버퍼 overflow 후 오래된 t | NaN일 수 있음 | `overflow` 카운터로 관측 가능 |

### 3.9 락 대신 쓸 수 있는 것들 (follow-up 대비)

| 방식 | 리더 | 라이터 | 비고 |
|---|---|---|---|
| **mutex** (선택) | 짧게 대기 가능 | 짧게 대기 가능 | 가장 단순, 정확. 락 구간이 O(log n) |
| rwlock | 리더끼리 병렬 | 리더가 많으면 굶을 수 있음 | 구간이 이렇게 짧으면 rwlock 자체 오버헤드가 더 큼. 이득 없음 |
| **seqlock** | 락 없음, 충돌 시 재시도 | 절대 안 기다림 | 라이터가 1명일 때 최적. 리더가 복사한 값을 검증 후 사용 |
| double buffer / RCU | 락 없음 | 복사 비용 | 전체 히스토리를 복사하기엔 128 KiB가 큼 |
| `pthread_mutex_trylock` | 절대 안 기다림, 실패 가능 | — | "busy면 NaN" — 진짜 non-blocking이 필요할 때 |

seqlock 스케치 (말로 설명할 수준이면 충분):

```c
static _Atomic uint32_t seq;          /* 홀수 = 쓰는 중 */

/* writer (1명) */
atomic_fetch_add_explicit(&seq, 1, memory_order_relaxed);     /* → 홀수 */
atomic_thread_fence(memory_order_release);
/* ... buf, head, tail 갱신 ... */
atomic_fetch_add_explicit(&seq, 1, memory_order_release);     /* → 짝수 */

/* reader */
for (;;) {
    uint32_t s1 = atomic_load_explicit(&seq, memory_order_acquire);
    if (s1 & 1) continue;                                      /* 쓰는 중 */
    /* ... 이진 탐색, 결과를 지역 변수에 복사 ... */
    atomic_thread_fence(memory_order_acquire);
    if (atomic_load_explicit(&seq, memory_order_relaxed) == s1) break;   /* 도중에 안 바뀜 */
}
```

주의: seqlock 리더는 **쓰는 도중의 데이터를 읽을 수 있고**(나중에 버리긴 하지만), C 표준상 그 읽기 자체가 데이터 레이스라 엄밀히는 버퍼 원소도 atomic으로 읽어야 한다. 그래서 "seqlock은 커널처럼 규칙을 아는 곳에서 쓰고, 여기서는 mutex가 충분하다"가 좋은 답이다.

## 4. 에러, 시간, 수명 — 실무에서 물어볼 것들

### 4.1 ERROR 처리

```c
default:                                        /* ERROR */
    atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
    usleep(ERROR_BACKOFF_US);                   /* 10 ms */
    break;
```

- ERROR일 때 가짜 센서는 `lux`, `timestamp`를 **초기화하지 않는다**. 절대 쓰면 안 된다 — 쓰레기 값을 히스토리에 넣으면 이진 탐색 정렬도 깨진다.
- ERROR는 **즉시** 리턴한다. 센서가 계속 에러를 내면 sampler 스레드가 CPU 100%로 돈다 → backoff. 실제로는 지수 backoff(10 ms → 20 → 40 … 최대 1 s)와 "N번 연속이면 센서 리셋(I2C 버스 복구, 전원 토글)"을 둔다.
- 카운터(`g_errors`)는 관측용. 텔레메트리/로그로 내보낸다.

### 4.2 오래된 값(staleness) — 센서가 죽으면?

지금 구조는 센서가 멈추면(드라이버 hang, I2C stuck) **마지막 값을 영원히** 돌려준다. 나이트 모드가 낮에 켜진 채로 굳을 수 있다.

- sampler 스레드가 **VALID 또는 NO_CHANGE를 받을 때마다** `g_last_alive_us`를 갱신한다. NO_CHANGE가 heartbeat 역할을 한다 (정상이면 최소 1초마다 온다).
- `now - g_last_alive_us > 3초`면 "센서 비정상" — API가 age를 돌려주거나 health 함수를 둔다.
- 면접 한 줄: "NO_CHANGE is effectively a 1 Hz heartbeat, so I can detect a dead sensor within a few seconds and report the value as stale."

### 4.3 시계 — `gettimeofday`는 단조 증가가 아니다

- `gettimeofday` = wall clock. NTP 동기화, 사용자 설정, 윤초로 **뒤로 가거나 점프**한다. 카메라가 부팅 직후 NTP를 받으면 수년 단위로 점프할 수도 있다.
- 히스토리 정렬, 윈도우 계산, 나이 계산은 전부 **`clock_gettime(CLOCK_MONOTONIC)`**으로 해야 한다.
- 문제는 timestamp를 **벤더가 채운다**는 것. 벤더 시계가 wall clock이면: (1) 샘플을 받은 순간 우리 monotonic 시각을 따로 찍어 그걸 키로 쓰거나, (2) 두 시계의 오프셋을 추적해 변환한다.
- 모범답안은 pad의 `get_timestamp()`를 그대로 쓰되, 역행 시 clamp로 정렬만 지킨다. 면접에서는 "in production I'd key the history on CLOCK_MONOTONIC"를 꼭 말한다.

### 4.4 시작과 종료

- `init`은 한 번만: 모범답안은 `g_started` 플래그(단일 스레드에서 init 가정). 여러 스레드가 init을 동시에 부를 수 있으면 `pthread_once`.
- `deinit`: `g_running = false` 후 `pthread_join`. sampler가 `read_next_sample()` 안에서 자고 있으면 **최대 1초** 기다린다. 이게 싫다고 `pthread_cancel`을 쓰면 벤더 코드가 락이나 버스를 잡은 채 죽을 수 있다 → 쓰지 않는다. 실제 드라이버라면 `poll()` + eventfd로 깨울 수 있는 인터페이스를 요구한다.
- `g_running`이 `_Atomic bool`인 이유: sampler 루프가 매번 다시 읽어야 한다. 일반 `bool`이면 컴파일러가 루프 밖으로 끌어내 무한 루프가 될 수 있다 (`volatile`로도 "다시 읽기"는 되지만 C11 메모리 모델상 스레드 간 동기화는 atomic이 맞다).

### 4.5 벤더 코드의 스레드 안전성 다시 보기

- `static bool first_run`: 두 스레드가 동시에 첫 호출을 하면 둘 다 true를 볼 수 있다.
- `rand()`: 내부 상태가 전역. POSIX는 `rand()`가 스레드 안전할 필요가 없다고 명시한다 (`rand_r`이 있는 이유).
- 결론: "호출자는 정확히 한 스레드"는 **선택이 아니라 필수**다. API 문서(헤더 주석)에 적어 둔다.

## 5. 테스트 — 하네스가 확인하는 것

### 5.1 pad의 기대값은 어디서 나왔나 (glibc `rand()` 추적)

glibc `rand()`는 시드를 안 주면 seed=1이고 수열이 고정이다. 처음 14개를 RAND_MAX로 나눈 값과, 가짜 센서가 그걸 어떻게 소비하는지:

| 호출 | rand 소비 | 결과 | 시각 |
|---|---|---|---|
| 1 (init) | #1 0.840 (에러 판정: 통과), #2 0.394 → lux | **VALID 3.94383** | ≈ 0 s |
| 2 | #3 에러 판정, #4 0.798 → 1.597 s ≥ 1 s | NO_CHANGE | ≈ 1.00 s |
| 3 | #5, #6 0.198 → 395 ms, #7 0.335 → lux | **VALID 3.35223** | ≈ 1.40 s |
| 4 | #8, #9 0.278 → 556 ms, #10 0.554 → lux | **VALID 5.53970** | ≈ 1.95 s |
| 5 | #11, #12 0.629 → 1.258 s | NO_CHANGE | ≈ 2.95 s |
| 6 | #13, #14 0.513 → 1.027 s | NO_CHANGE | ≈ 3.95 s |

그래서 `lux[3]`(≈2.5 s) = 5.5397, 테스트 포인트 0 / 200 / 1600 / 2600 / 3100 ms → 3.94383 / 3.94383 / 3.35223 / 5.5397 / 5.5397.
이 표는 **sampler 스레드 하나만 `rand()`를 부른다**는 전제에서만 성립한다 — 여러 스레드가 센서를 부르면 수열이 섞여 기대값이 달라진다. 설계가 맞다는 또 하나의 증거다.

macOS의 `rand()`는 다른 알고리즘이라 값이 다르다. 그래서 복원한 하네스는 가짜 센서가 만든 **모든 VALID 값을 ground truth로 기록**하고, 그것과 비교해 PASS/FAIL을 낸다. 어느 OS에서든 채점된다.

### 5.2 하네스 구성

| 체크 | 무엇을 보나 | 잡아내는 버그 |
|---|---|---|
| `lux[1]` init 직후 | 첫 샘플을 init에서 동기로 받았나 | 스레드만 띄우고 끝낸 구현 (NaN) |
| `lux[2]`, `lux[3]` | 최신 값 갱신 | 게시 누락, NO_CHANGE를 값으로 덮어씀 |
| test points t0~t4 | sample-and-hold 조회 | off-by-one, `<` vs `≤`, 항상 최신값 반환 |
| future / before first | 경계 NaN | 범위 검사 누락 |
| window edge (`./main.sh window`) | 경계 직전 샘플 보존 | **3.5의 단순 eviction 버그** |
| older than window | 계약 준수 | 범위 밖인데 값 반환 |
| stress: 리더 4개 × 2초 | 찢어진/지어낸 값 없음, 최악 지연 < 100 ms | 동기화 누락, getter에서 센서 호출 (지연 1초) |
| 센서가 한 번도 안 불림 | stub 그대로 제출 방지 | — |

### 5.3 실제 실행 결과 (macOS, 모범답안)

```
$ ./main.sh sol
== Part 1: get_most_recent_lux ==
lux [1] = 7.556053, should be 3.94383 (glibc)
  lux[1] right after init      got 7.556053   want 7.556053   PASS
  ...
== Thread safety: 4 readers for 2 s while the sampler writes ==
  calls 19134891, invented/torn values 0, worst getter latency 644 us PASS

ALL CHECKS PASSED (0 failed)
```

- `./main.sh window` — 윈도우 2초 빌드, 경계/범위 밖 체크 포함 전부 PASS.
- `./main.sh list`, `./main.sh rbtree`, `./main.sh window list`, `./main.sh window rbtree` — 전부 PASS.
- red-black tree fuzz (6만 삽입, 10% 역순, ASan + UBSan) — RB 규칙 위반 0, floor 12,380회 brute force 일치.
- ThreadSanitizer (`clang -fsanitize=thread`) — 경고 0개.
- 빈 stub(`./main.sh`) — "read_next_sample() was never called" FAIL. 이제 네가 채울 차례.

## 6. 인터뷰에서 이렇게 진행하고 이렇게 말한다

### 6.1 45분 시간 배분

| 분 | 할 일 | 산출물 |
|---|---|---|
| 0–4 | 문제 읽고 확인 질문 (0장) | pad 맨 위에 가정 5줄 주석 |
| 4–8 | 그림 설명: 스레드 1개 + 공유 상태 (1장) | 말로 + ASCII 그림 |
| 8–16 | Part 1 코드: sampler 스레드 + mutex 버전 → 돌려서 lux[1..3] 확인 | 동작하는 코드 |
| 16–18 | "atomic으로 바꾸면 wait-free" 말하고 바꿈 | v2 |
| 18–24 | Part 2 설계: 의미 정의, 용량 계산, 자료구조 **① circular queue → ② linked list → ③ red-black tree** 순서로 말하기 (3.3), **경계 샘플 함정** | 말로 + 주석 |
| 24–36 | Part 2 코드: ①(링버퍼 + 이진 탐색)을 짜서 통과시키고, ②·③은 말로 + 핵심 함수(floor)만 | 테스트 포인트 통과 |
| 36–45 | 에러, staleness, monotonic clock, 대안(seqlock/버킷), 질문 받기 | 대화 |

### 6.2 핵심 문장 (영어)

- **Framing**: "The sensor call blocks for up to a second and isn't thread-safe, so I'll confine it to one dedicated sampler thread. Readers never touch the sensor; they only read state the sampler publishes."
- **Why one thread**: "It's a change-notification stream. Two callers would split the events between them, and the vendor code uses a static and `rand()`."
- **Part 1**: "The latest value is a single float, so I can publish it with one atomic store and readers do one atomic load — wait-free. A mutex version works too; I'd start there and then tighten it."
- **Init**: "The first call is guaranteed not to block, so I take it synchronously in init. That way a getter called right after init already has a value, no race with thread startup."
- **Part 2 semantics**: "The sensor only reports changes, so the lux at time t is the last valid reading at or before t — a step function."
- **Sizing**: "The fake averages about 0.67 changes per second, roughly 400 in ten minutes, but it has no rate limit, so I size for a real sensor at 10 Hz — 6000 samples — and use 8192 entries, about 128 KB."
- **Data structure (progression)**: "Circular buffer first — fixed memory, but I have to guess the capacity. A linked list removes that limit but lookup is O(n). A red-black tree keyed by timestamp gives dynamic size and O(log n) insert, floor lookup and delete-min."
- **Trade-off**: "With one in-order producer, a sorted ring plus binary search matches the tree's lookup cost with no mallocs; the tree wins for out-of-order inserts or unknown size."
- **The subtle point**: "I never evict the sample that defines the value at the start of the window — at night nothing may change for an hour, and that old sample is still the answer."
- **Overflow**: "If the sensor is chattier than I sized for, I drop the oldest and count it, so recent data stays exact and the drop is observable."
- **Clock**: "In production I'd key everything on CLOCK_MONOTONIC; gettimeofday can jump with NTP."
- **Health**: "NO_CHANGE arrives at least once a second, so it's a free heartbeat; I'd expose the age of the value so night-mode logic can distrust a stale reading."

## 7. 예상 follow-up 질문과 답

**Q1. 리더가 다른 프로세스라면? (예: 나이트 모드는 ISP 데몬, 조회는 웹 API)**
공유 메모리(`shm_open` + `mmap`)에 같은 구조를 올린다. pthread mutex를 쓰려면 `PTHREAD_PROCESS_SHARED` + robust mutex(`PTHREAD_MUTEX_ROBUST`, 소유 프로세스가 죽으면 `EOWNERDEAD`). 아니면 seqlock — 라이터가 한 명이라 잘 맞는다. 더 느슨하게는 sampler 데몬이 Unix 소켓/D-Bus로 질의를 받는다.

**Q2. MCU라서 스레드가 없고, 센서가 인터럽트로 알려 준다면?**
ISR이 "변화 있음" 플래그만 세우거나 샘플을 SPSC 링버퍼에 넣는다 (ISR = producer, main loop = consumer). 링버퍼는 free-running head/tail + acquire/release, 크기는 2의 거듭제곱. ISR 안에서는 mutex 금지. Part 1은 atomic 32비트 store 하나. 앞에서 공부한 SPSC ring 그대로다.

**Q3. `get_lux_at`을 O(1)로?**
1초 버킷 인덱스를 추가: `idx[sec % 600]` = 그 초가 시작될 때 유효했던 샘플의 링버퍼 위치. 조회 = 버킷으로 점프 후 그 초 안의 몇 개만 선형 탐색. 버킷을 갱신할 때 빈 초(변화 없음)도 채워야 해서 sampler가 NO_CHANGE마다 채우면 된다.

**Q4. "값이 바뀔 때까지 기다리는" API도 달라고 하면?**
`bool wait_for_change(float *out, uint64_t timeout_us)` — 이건 의도적으로 블로킹이다. mutex + condition variable, sampler가 VALID마다 `pthread_cond_broadcast`. 리더는 `while (seq == my_seq) pthread_cond_timedwait(...)` (spurious wakeup 때문에 while). 콜백 구독 방식도 가능하지만 콜백은 sampler 스레드에서 돌아서 느린 콜백이 샘플링을 막는다 → 큐로 넘긴다.

**Q5. 나이트 모드 판단은 어떻게?** (제품 감각 보너스)
임계값 하나로 켜고 끄면 해 질 녘에 깜빡거린다(chattering). **히스테리시스**: 5 lux 아래로 내려가면 켜고, 15 lux 위로 올라가야 끈다. **시간 조건**: 10초 연속 유지될 때만 전환 (헤드라이트 한 번에 끄지 않도록). 이게 Part 2 히스토리가 필요한 실제 이유다 — "지난 10초 동안 전부 5 lux 미만이었나?"는 `get_lux_at` 여러 번이나 구간 질의로 답한다.

**Q6. 구간 질의(지난 N초의 min/max/평균)를 달라고 하면?**
lower_bound(start) ~ upper_bound(end)로 구간을 찾고, **시간 가중 평균**을 낸다 — 계단 함수라 각 값에 "유지된 시간"을 곱해야 한다. 샘플 개수로 나눈 단순 평균은 틀린다 (1초 동안 10번 바뀐 구간이 과대 반영됨). 자주 부르면 prefix sum을 유지한다.

**Q7. 메모리가 4 KiB밖에 없다면?**
샘플을 8 B → 4 B로 패킹(상대 시간 16비트 ds + lux 양자화 16비트), 그리고 "값 차이가 X lux 미만인 연속 샘플은 병합"하는 dead-band 압축. 나이트 모드 용도라면 0.1 lux 정밀도면 충분하다.

**Q8. 테스트는 어떻게 결정적으로?**
센서와 시계를 주입 가능하게 만든다: `read_next_sample`과 `get_timestamp`를 함수 포인터(또는 링크 타임 대체)로 바꾸고, 가짜 시계를 수동으로 전진시킨다. 그러면 10분 윈도우도 실제 10분 안 기다리고 테스트된다. 이 하네스의 `-DLUX_WINDOW_US=2000000`은 그 간이 버전이다. 동시성은 TSan + 스트레스.

**Q9. sampler 스레드 우선순위는?**
센서 이벤트를 놓치지 않게 일반보다 약간 높게 두되, 실시간(`SCHED_FIFO`)까지는 불필요 — 블로킹 호출 안에서 대부분 잔다. 리더가 실시간 스레드라면 mutex에 우선순위 상속(`PTHREAD_PRIO_INHERIT`)을 켠다.

**Q10. 전원 관점에서 이 설계는?**
sampler는 거의 항상 커널에서 블로킹 중이라 CPU를 안 쓴다. 폴링 루프(`while(1) { if (sensor_ready()) ... }`)였다면 코어 하나를 계속 깨운다. 리더 쪽도 센서를 건드리지 않으니 I2C 트랜잭션이 늘지 않는다.

## 8. 다음에 같은 문제가 나오면 — 체크리스트

- [ ] getter 안에서 `read_next_sample()`을 부르지 않았다
- [ ] 센서 호출은 락 **밖**에서 한다
- [ ] 센서를 부르는 스레드는 정확히 하나다 (그리고 그 이유 3개를 말했다)
- [ ] init에서 첫 샘플을 동기로 받았다
- [ ] ERROR일 때 lux/timestamp를 쓰지 않았다, backoff가 있다
- [ ] NO_CHANGE는 값 유지로 처리했다 (덮어쓰지 않았다)
- [ ] 공유 float에 동기화가 있다 (mutex 또는 atomic)
- [ ] "non-blocking"의 정의를 확인했다
- [ ] 10분 용량을 계산했고, 최악 비율은 상한이 없다는 걸 말했다
- [ ] Part 2 자료구조를 ① circular queue → ② linked list → ③ red-black tree 순서로, 각 단계가 고치는 약점과 함께 말했다
- [ ] 코드로는 ①(링버퍼 + 이진 탐색)을 먼저 완성했다, ③의 floor 조회를 설명할 수 있다
- [ ] malloc/free는 락 밖에서 (②, ③)
- [ ] **경계 직전 샘플을 남겨 뒀다**
- [ ] 꽉 찼을 때 정책을 정하고 세었다
- [ ] 미래 / 윈도우 밖 / 첫 샘플 이전을 NaN으로 처리했다
- [ ] monotonic clock, staleness, deinit(join) 을 말했다
- [ ] `#include <cstddef>`(C++ 헤더) 대신 `<stddef.h>`

## 9. 부록 — pad 코드의 오타와 복원 내용

pad에 보이던 코드 중 일부는 오타였거나 IDE가 그려 준 힌트였다. 복원하면서 고친 것:

| pad에 보인 것 | 실제 | 설명 |
|---|---|---|
| `printf(format:"...")`, `usleep(useconds:max_wait)`, `gettimeofday(tv:&tv, tz:NULL)` | `printf("...")` 등 | IDE의 **inlay hint**(파라미터 이름 표시). 코드가 아니다 |
| `RAND_MAC` | `RAND_MAX` | 오타 |
| `return_vale` | `return_value` | 오타 (선언도 `return_vale`, 사용은 섞여 있었음) |
| `[3]=2600*1000 [4]=...` | 사이에 `,` | 누락 |
| `uint64_t get_timestemp();` (als.h) | `get_timestamp` | 선언과 정의 이름 불일치 → 링크 에러 |
| `#include <cstddef>` (recent_lux.c) | `<stddef.h>` | C++ 헤더. C 컴파일러는 못 찾는다 |
| `gcc -02` (main.sh) | `-O2` | 숫자 0이 아니라 대문자 O |
| `void init_recent_lux(void)` | `int init_recent_lux(void)` | 복원판은 `pthread_create` 실패를 알리려고 int로 바꿈 |

복원판에서 **추가**한 것 (pad에는 없음): ground truth 기록, PASS/FAIL 채점, 리더 스트레스 테스트, 윈도우 축소 테스트, `deinit_recent_lux()`.
`lux[1]`/`lux[2]`와 앞부분 흐름(init → 대기 → 조회)은 기억과 기대 출력("lux[1] = 3.94383", "lux[3] … 5.5397")에서 역산했다. pad 원본과 대기 시간은 다를 수 있지만 검사하는 내용은 같다.
