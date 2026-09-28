# Verkada 1차 기술 인터뷰 — 예상 문제 (2026-09-25)

> **포지션**: Senior Embedded Linux Engineer, Connectivity (San Mateo) · [공고](https://job-boards.greenhouse.io/verkada/jobs/5209588007)
> **세션**: 2파트 — problem solving + system design · **근거**: 리크루터 Davis 안내 메일
> **읽는 법**: 오늘 밤은 **1~2장**(Tier 1 세 문제)만 확실히. 내일 아침은 **6·7·8장**(속사포·말하기·치트시트). 3~5장은 시간 남을 때.
> **원칙**: 코드를 맞히는 시험이 아니다. **말하면서 짜는** 시험이다.

---

## 1장. 세션 예상 구조와 첫 3분

### 1.1 구조 `[추정]`

```
0–5분    인사 · 간단한 배경 (2~3문장으로 끝낸다)
5–35분   파트 A: problem solving — 공유 자료구조 하나를 라이브로 구현
             → 돌아가면 follow-up으로 확장 (다중 소비자 / 종료 / drop / lock-free)
35–55분  파트 B: system design — 게이트웨이·센서 맥락의 열린 설계
55–60분  내 질문
```

메일이 "two parts"라고 했으니 **코딩만 하다 끝나지 않는다.** 파트 A를 40분까지 끌면 파트 B가 사라진다 → **30분쯤에 "이 정도면 동작합니다, 설계 쪽으로 넘어갈까요?"** 라고 먼저 제안할 수 있어야 한다.

### 1.2 시작하자마자 할 일 — 요구사항 질문 3개

문제를 듣고 **바로 코딩하지 않는다.** 이 세 개를 먼저 묻는다(영어 그대로 써도 됨).

1. "How many producers and how many consumers?"
2. "Is the queue bounded? If it's full, do we block the producer or drop — and if we drop, oldest or newest?"
3. "How does shutdown work — do we drain what's left, or discard it?"

여기에 하나 더 붙이면 좋다: "Who owns the items — do I copy the value, or take ownership of a pointer?"

면접관이 "알아서 정하세요"라고 하면 **가정을 소리 내어 선언**하고 진행한다.
> "Then I'll assume multiple producers, one consumer, a bounded queue that blocks the producer, and a close() that drains remaining items. I'll note where those assumptions change the design."

### 1.3 코딩 전 30초 설계 선언

```
"I'll define the API first: init, push, pop, close, destroy.
 State: a ring buffer with head and count, a closed flag.
 Synchronization: one mutex for the state, and two condition variables —
 not_full and not_empty — so producers and consumers don't wake each other needlessly.
 Then I'll walk the edge cases and talk about testing."
```

이 30초가 메일의 채점 기준 4개 중 2개(trade-off 정당화, 커뮤니케이션)를 시작부터 가져간다.

### 1.4 시간 배분 (파트 A 30분 기준)

| 분 | 할 일 |
|---|---|
| 0–3 | 요구사항 질문 · 가정 선언 |
| 3–5 | API와 상태 정의를 말로 + 주석으로 |
| 5–20 | 구현하며 내레이션 (한 줄씩 왜 그런지) |
| 20–25 | **스스로** edge case 점검 (8장 체크리스트 낭독) |
| 25–30 | 테스트 전략 + follow-up 대응 |

---

## 2장. Tier 1 — 거의 확실히 나오는 3문제

> 이 셋 중 하나가 파트 A로 나온다고 보면 된다. **P1이 가장 유력**하고, P2는 메일이 이름을 직접 언급했으며, P3는 파트 B에서 "어떻게 테스트하나"로 반드시 등장한다.

---

### P1. Bounded thread-safe queue (mutex + 2 condvar) — 확률 ★★★★★

**예상 지문 (면접관 말투)**
> "게이트웨이 펌웨어에서 여러 I/O 스레드가 이벤트를 만들고, 업로더 스레드 하나가 그걸 꺼내서 클라우드로 보냅니다. 크기가 제한된 스레드 안전 큐를 구현해 보세요. 큐가 가득 차면 생산자는 기다리고, 비면 소비자가 기다립니다."

**진행 순서**

1. 요구사항 질문 3개 (1.2)
2. API 선언: `bq_init / bq_push / bq_pop / bq_close / bq_destroy`
3. 상태: `buf, cap, head, tail, count, closed` + `mutex m`, `cond not_full, not_empty`
4. push → pop → close 순으로 구현
5. edge case 낭독 → 테스트 전략

**모범 구현 (검증된 코드)**

```c
typedef struct {
    Event          *buf;
    size_t          cap, head, tail, count;
    bool            closed;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} BQueue;

bool bq_push(BQueue *q, Event ev)
{
    pthread_mutex_lock(&q->m);
    /* while: spurious wakeup + 여러 생산자가 동시에 깨는 경우 방어 */
    while (q->count == q->cap && !q->closed)
        pthread_cond_wait(&q->not_full, &q->m);

    if (q->closed) {                      /* close 이후 push는 거부 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    q->buf[q->tail] = ev;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_mutex_unlock(&q->m);          /* unlock 먼저 → 깨어난 소비자가 곧장 lock */
    pthread_cond_signal(&q->not_empty);
    return true;
}

bool bq_pop(BQueue *q, Event *out)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    if (q->count == 0) {                  /* closed && empty → 종료 신호 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

void bq_close(BQueue *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    /* 대기 중인 모든 스레드를 깨워야 한다 → signal이 아니라 broadcast */
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}
```

**짜면서 반드시 말할 문장 4개**

- "I use `count` so that `head == tail` isn't ambiguous between empty and full."
- "The wait is in a `while` loop — spurious wakeups are allowed, and another consumer may take the item before I re-acquire the lock."
- "The closed flag is part of the wait predicate; otherwise a closed queue would put threads back to sleep forever."
- "On close I broadcast both condition variables — signal would wake only one and the rest would sleep forever."

**예상 follow-up과 답**

| 질문 | 답의 핵심 |
|---|---|
| "소비자를 여러 개로 늘리면?" | 코드는 그대로 동작한다. 다만 **항목 간 전역 순서가 깨진다.** 순서가 필요하면 소비자를 하나로 두거나, 시퀀스 번호를 붙여 재정렬하거나, 키별로 큐를 샤딩한다. |
| "busy-wait로 하면 안 되나?" | CPU를 태우고(태양광 트레일러의 배터리), 락 경합을 늘려 생산자까지 느려진다. condvar는 커널이 재우고 필요할 때만 깨운다. |
| "타임아웃이 필요하면?" | `pthread_cond_timedwait`. 함정은 기본 시계가 CLOCK_REALTIME이라는 것 — Linux는 `pthread_condattr_setclock(CLOCK_MONOTONIC)`, macOS는 `pthread_cond_timedwait_relative_np`. 깨어날 때마다 남은 시간을 다시 계산한다. |
| "생산자가 ISR이면?" | 블록할 수 없으므로 이 설계를 못 쓴다. → SPSC lock-free 링버퍼(P4)로 바꾸고, 가득 차면 **버리고 카운트**한다. |
| "가득 찼을 때 블록 말고 버려야 한다면?" | 정책을 파라미터로: BLOCK / DROP_NEWEST / DROP_OLDEST. **무엇을 버릴지는 데이터의 의미가 정한다.** 텔레메트리는 drop-oldest(최신이 중요), 감사 로그는 drop-newest 또는 block. 어느 쪽이든 `dropped` 카운터를 노출한다. |
| "lock-free로 만들 수 있나?" | MPSC는 CAS로 슬롯을 예약하고 완료 표시를 따로 두는 방식(Vyukov)으로 가능하지만 복잡도가 급증한다. **측정된 병목이 있을 때만.** 실무에선 생산자별 큐 + 소비자 라운드로빈이 더 낫다. |
| "어떻게 테스트하나?" | 단일 스레드 단위 테스트(순서·경계) + 스트레스 테스트(생산자 4, 용량 8로 블로킹 경로 강제) + **불변식 검사**(생산 = 소비 + 버림 + 남음, 생산자별 순서 보존) + **ThreadSanitizer를 CI에**. |

**흔한 감점**

- 조건 검사를 `if`로 (가장 흔한 탈락 사유)
- close에서 `signal`만 호출 → 나머지 스레드 영구 대기
- `closed`를 wait 조건에 안 넣음
- join 전에 `free`/`destroy` → use-after-free
- 락을 쥔 채 업로드/로그 같은 I/O 수행

---

### P2. Double buffering — 확률 ★★★★☆ (메일이 이름을 직접 언급)

**예상 지문**
> "센서(또는 카메라)가 DMA로 프레임을 채우고, 처리 스레드가 그걸 읽습니다. 읽는 쪽이 절반만 갱신된 프레임을 보면 안 되고, 쓰는 쪽은 읽는 쪽을 기다리면 안 됩니다. 어떻게 설계하시겠어요?"

**먼저 말할 한 문장 (이게 이 문제의 핵심)**
> "I protect **ownership of the buffers**, not the data itself. The critical section only swaps a couple of indices — filling the frame happens outside the lock."

**소유권 3상태**

```
            write_begin              write_commit            read_acquire
  [FREE] ──────────────▶ [WRITER] ──────────────▶ [READY] ──────────────▶ [READER]
    ▲                   생산자가 채움             publish됨                 소비자가 읽음
    └──────────────────────────────── read_release ◀──────────────────────────┘

  버퍼 2개: reader가 하나 + READY가 하나면 writer는 쓸 곳이 없다
          → READY(아직 안 읽힌 프레임)를 버리고 그 자리에 쓴다   = drop (카운트!)
          → reader가 쥔 버퍼는 절대 건드리지 않는다              = tearing 방지
```

**모범 구현 (검증된 코드)**

```c
Frame *db_write_begin(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    int idx = -1;
    for (int i = 0; i < NBUF; i++) {              /* 아무도 소유하지 않은 버퍼 */
        if (i != db->ready_idx && i != db->reader_idx) { idx = i; break; }
    }
    if (idx < 0) {
        idx = db->ready_idx;                      /* READY를 희생 */
        db->ready_idx = -1;
        db->dropped++;
    }
    db->write_idx = idx;
    pthread_mutex_unlock(&db->m);
    return &db->buf[idx];                          /* 데이터 write는 락 밖에서 */
}

void db_write_commit(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    if (db->ready_idx >= 0)
        db->dropped++;                             /* 소비 전에 더 새 프레임이 나옴 */
    db->ready_idx  = db->write_idx;                /* publish */
    db->write_idx  = -1;
    pthread_mutex_unlock(&db->m);
}

Frame *db_read_acquire(DoubleBuf *db)
{
    Frame *f = NULL;
    pthread_mutex_lock(&db->m);
    if (db->reader_idx < 0 && db->ready_idx >= 0) {
        db->reader_idx = db->ready_idx;            /* 소유권 이전 */
        db->ready_idx  = -1;
        f = &db->buf[db->reader_idx];
    }
    pthread_mutex_unlock(&db->m);
    return f;
}
```

**예상 follow-up과 답**

| 질문 | 답의 핵심 |
|---|---|
| "트리플 버퍼로 바꾸면?" | `NBUF`를 3으로 바꾸면 코드 변경 없이 된다. writer가 빈 버퍼를 못 찾는 경우가 사라진다. **비용은 프레임 하나만큼의 메모리** — 1080p면 BOM에 실제로 잡히므로 제품 제약이 결정한다. |
| "tearing이 없다는 걸 어떻게 테스트하나?" | 프레임 전체를 시퀀스에서 유도한 같은 패턴으로 채우고, 소비자가 모든 바이트가 동일한지 검사한다. 불변식: 읽은 수 + 버린 수 + 남은 수 = 만든 수. |
| "DMA면 뭐가 달라지나?" | 완료 ISR에서는 **인덱스만 교체**하고 다음 버퍼 주소를 DMA에 미리 건다(ping-pong). 캐시가 있으면 DMA가 쓴 버퍼는 읽기 전 invalidate, CPU가 쓴 버퍼는 보내기 전 clean. 버퍼는 캐시 라인 정렬. |
| "소비자가 모든 프레임을 봐야 한다면?" | 그건 double buffer 문제가 아니라 **큐** 문제다. 요구사항을 다시 확인해야 한다. |
| "작은 상태값(신호 세기 등)이면?" | 버퍼 교체 대신 mutex로 보호한 스냅샷, 또는 읽기가 매우 잦고 측정 근거가 있으면 seqlock. seqlock은 C 표준상 data race라 TSan이 경고한다는 점도 같이 말한다. |

---

### P3. 모듈화·테스트 가능한 드라이버 — 확률 ★★★★☆ (파트 B에서 거의 확실)

**예상 지문**
> "I2C 공기질 센서 드라이버를 설계한다고 합시다. 측정 시작 → 준비될 때까지 폴링 → 데이터 읽기 → CRC 검증. NACK 재시도와 타임아웃이 필요합니다. **하드웨어 없이 이걸 어떻게 테스트하죠?**"

**답변의 뼈대 — 설계 원칙 4개를 이름으로 말한다**

1. **의존성 주입**: 드라이버는 `I2cOps`·`ClockOps` 인터페이스만 안다. 제품에선 진짜 HAL, 테스트에선 fake.
2. **시간도 의존성이다**: `now_ms()`를 주입하면 "1초 타임아웃" 테스트가 **실제로는 0초**에 끝난다.
3. **정책과 I/O 분리**: 재시도·타임아웃·CRC는 순수 함수와 상태머신으로, 버스 접근은 얇은 어댑터로.
4. **에러는 값으로 반환**: 드라이버는 스스로 죽거나 로그만 찍고 삼키지 않는다. 정책은 호출자가.

```c
typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;
} I2cOps;

typedef struct {
    uint32_t (*now_ms)(void *ctx);
    void     *ctx;
} ClockOps;
```

**시간을 조종하는 테스트 — 이 코드를 보여주면 설명이 끝난다**

```c
Sensor s; FakeI2c f = { .not_ready_times = 10000 }; FakeClock c = { 0 };
setup(&s, &f, &c);
assert(sensor_start_measure(&s) == SENS_OK);
int16_t ppm = 0;
assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
c.ms = 1000;                                   /* 타임아웃 경계 */
assert(sensor_poll(&s, &ppm) == SENS_ETIMEDOUT);
assert(s.state == SENS_ERROR);
assert(sensor_poll(&s, &ppm) == SENS_ESTATE);  /* 에러 상태 고착 확인 */
```

**"하드웨어 없이 어떻게 테스트하나"의 완성된 답 — 3층으로**

| 층 | 무엇을 | 어떻게 | 주기 |
|---|---|---|---|
| 단위 | 파싱, CRC, 상태 전이, 재시도·타임아웃 정책 | fake HAL + fake 시계, 하드웨어 0개 | 매 커밋 (CI) |
| 통합 | 실제 I2C 버스, 실제 센서 | HIL 랙에 보드 몇 장 | 매일 밤 |
| 시스템 | -40~50°C, 전원 사이클, 링크 단절 | 챔버 + 결함 주입 | 릴리스 전 |

> **Don의 경험 연결**: 3층은 SK hynix에서 만든 **챔버 테스트 자동화**(온도 제어 API, UART 시퀀스, 다수 클라이언트 스케줄링)와 같은 일이다. 이 말을 꼭 붙일 것 — 추상적인 답이 경험담으로 바뀐다.

**예상 follow-up**

| 질문 | 답 |
|---|---|
| "함수 포인터가 성능에 영향 없나?" | 간접 호출은 몇 사이클, I2C 트랜잭션은 수백 마이크로초 — 측정 가능한 차이가 없다. 핫 루프에서만 컴파일 타임 치환으로 바꾼다. **측정 없이 최적화하지 않는다.** |
| "CRC가 틀리면?" | 출력 파라미터를 **건드리지 않고** 에러 코드를 반환한다. 오염된 값이 새어 나가면 안 된다. 상태는 재측정 가능하게 되돌린다. |
| "동시성은 어떻게 테스트하나?" | 불변식 기반 스트레스 테스트 + TSan을 CI에 + 결정적 스케줄 주입(테스트 훅으로 특정 지점에 yield) + 장시간 soak. |
| "이 드라이버를 여러 센서가 쓰면?" | 상태를 전역이 아니라 구조체에 담아 인스턴스를 여러 개 만든다(테스트에도 유리). 버스를 공유하면 버스 접근을 한 스레드가 소유하거나 뮤텍스로 직렬화한다. |
---

## 3장. Tier 2 — 충분히 나올 수 있는 4문제

> Tier 1의 follow-up으로 흘러 들어오는 경우가 많다. 코드를 외우기보다 **핵심 한 줄**과 **언제 쓰는지**를 말할 수 있으면 된다.

### P4. SPSC lock-free 링버퍼 (ISR ↔ 태스크) — ★★★☆☆

**지문**: "UART RX 인터럽트가 바이트를 넣고 파서 태스크가 꺼냅니다. ISR에서는 뮤텍스를 쓸 수 없죠. 어떻게 하시겠어요?"

**핵심 한 줄**: 생산자는 `tail`만 쓰고 소비자는 `head`만 쓴다 → 같은 변수를 두 스레드가 쓰는 경쟁이 없다. 상대 인덱스는 **acquire로 읽고**, 자기 인덱스는 **release로 publish**한다.

```c
bool ring_push(Ring *r, uint8_t b)         /* ISR 전용 */
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                      /* full → 버리고 drop 카운트++ */
    r->buf[t] = b;                         /* ① 데이터 먼저 */
    atomic_store_explicit(&r->tail, next, memory_order_release);  /* ② 그 다음 publish */
    return true;
}
```

**말할 것**: "release/acquire 짝이 ①이 ②보다 먼저 보이도록 보장합니다. 이게 없으면 소비자가 '인덱스는 늘었는데 데이터는 아직 안 쓰인' 상태를 봅니다." / "`volatile`로는 안 됩니다 — 원자성도 순서도 주지 않습니다." / "ISR은 기다릴 수 없으니 가득 차면 버리고 카운터를 올립니다."

**follow-up**: 용량을 2의 거듭제곱으로 잡아 `& (CAP-1)` (MCU에 나눗셈이 없다) · 한 칸을 비워 full/empty 구분 · MPSC로 확장하면 CAS 예약이 필요해 복잡 · head/tail을 다른 캐시 라인에 두면 false sharing 방지.

---

### P5. 워커 풀 + graceful shutdown — ★★★☆☆

**지문**: "백그라운드 작업(업로드, 설정 동기화)을 처리하는 워커 풀을 만들어 보세요. 종료할 때 어떻게 하죠?"

**핵심**: 종료 모드가 두 개다 — **DRAIN**(남은 작업 처리 후 종료) / **STOP**(진행 중인 것만 끝내고 큐는 폐기). 상태를 `RUNNING / DRAIN / STOP` 3상태로 두면 워커 루프 조건이 단순해진다.

```c
static void *worker_main(void *arg) {
    Pool *p = arg; Task t;
    while (pool_take(p, &t)) t.fn(t.arg);  /* ★ 작업 실행은 반드시 락 밖에서 */
    return NULL;
}
```

**면접관이 파는 edge case 6개 — 먼저 말해버리면 이긴다**

1. 잠든 워커를 깨우려면 **broadcast** (signal은 하나만)
2. 종료 후 submit은 **거부하고 카운트** (조용히 무시하면 추적 불가)
3. **join 전에 free 금지** — UAF
4. 큐가 가득 차 제출자가 대기 중일 때도 깨워야 함
5. 작업 자체가 블로킹이면 종료가 지연됨 → 작업에 취소 신호 전달 또는 소켓 타임아웃
6. 이중 shutdown은 **멱등**하게. shutdown과 destroy를 분리하면 깔끔하다

**말할 것**: "작업을 락 안에서 실행하면 워커가 사실상 1개가 되고, 그 작업이 다시 submit하면 자기 자신과 데드락입니다."

---

### P6. 백프레셔 / drop 정책 큐 — ★★★☆☆

**지문**: "LTE가 끊겨 업로더가 막혔습니다. 그동안에도 센서는 계속 샘플을 만듭니다. 큐가 가득 차면 어떻게 하죠?"

| 정책 | 맞는 데이터 |
|---|---|
| BLOCK | 잃으면 안 되는 감사·과금 이벤트. **실시간 경로(ISR/DMA/주기 샘플러)에서는 금지** |
| DROP_NEWEST | 앞부분 이력이 중요한 로그 |
| DROP_OLDEST | 최신 상태가 중요한 텔레메트리·알람 |

**말할 것**: "정책은 **데이터의 의미**가 정합니다. 그리고 어떤 정책이든 버린 개수를 카운터로 내보냅니다 — a dropped sample is acceptable, a silently dropped sample is a bug."

**follow-up**: 링크가 몇 시간 끊기면 → RAM 큐에 상한을 두고 **플래시로 spill**, 단 eMMC 수명 때문에 배치 append + 상한(Don의 SSD write amplification 경험을 여기서 꺼낸다) · 등급별 큐(알람은 절대 drop 안 함) · 재전송 중복은 시퀀스 번호 + 서버 멱등 처리 · 업로드는 **배치로**(라디오 깨우기 비용) 대신 지연이 늘어나는 trade-off.

---

### P7. 최신 상태 공유 (신호 세기 스냅샷) — ★★☆☆☆

**지문**: "링크 상태(RSRP, RSRQ, SIM 슬롯, uptime)를 1초마다 갱신하는 스레드 하나와, 아무 때나 읽는 스레드 여럿이 있습니다. 읽는 쪽이 필드가 섞인 값을 보면 안 됩니다."

**답의 순서**

1. **먼저 mutex 스냅샷**을 제시한다: 락 안에서 구조체를 지역 변수로 복사하고 락 밖에서 쓴다. 간단하고 틀릴 수 없다.
2. "읽기가 초당 수만 번이고 writer 지연이 실제 문제라면" **seqlock**을 꺼낸다: writer가 시퀀스를 홀수→데이터→짝수로. reader는 앞뒤 시퀀스가 같아야 유효, 다르면 재시도. **writer는 절대 기다리지 않는다.**
3. 한계도 같이 말한다: C 표준상 data race라 TSan이 경고한다 · 포인터/리소스가 든 구조체에는 쓰면 안 된다 · reader 기아 가능.

**감점 포인트**: 근거 없이 처음부터 lock-free로 가는 것. "측정 전에는 mutex"가 시니어의 답이다.

---

## 4장. Tier 3 — 나오면 반가운 4문제 (실무형 코딩)

> Verkada의 보고된 코딩 문제는 퍼즐보다 **로그·이벤트·구간** 모양이다. 파트 A가 자료구조 대신 이쪽으로 갈 수도 있다.

### P8. LRU 캐시 — ★★☆☆☆
해시맵 + 이중 연결 리스트로 `get`/`put` O(1). 임베디드답게 **고정 용량 노드 풀**에서 할당(반복 malloc 금지). 말할 것: "노드를 미리 배열로 잡고 free list로 관리하면 단편화도 없고 최악 지연이 결정적입니다."

### P9. 이벤트 스트림 집계 — ★★☆☆☆
"카메라 상태 로그를 받아 디바이스별로 online/offline 횟수를 집계하라" 류. 오픈 어드레싱 해시맵을 직접 짠다(고정 크기). 경계: 같은 타임스탬프, 알 수 없는 상태 문자열, 용량 초과.

### P10. 인터벌 병합 (오프라인 구간) — ★★☆☆☆
정렬 후 병합, O(n log n). 경계 조건을 **먼저 물어본다**: `[100,200]`과 `[200,260]`은 이어진 것인가(끊김이 없었나)? 답이 "이어진 것"이면 `<=`로, "별개"면 `<`로. 이 질문을 하면 점수가 올라간다.

### P11. AT 커맨드 응답 파서 (FSM) — ★★☆☆☆
스트림으로 들어오는 `+CSQ: 23,99\r\n`, `+CREG: 0,5\r\n`, `OK\r\n`, `ERROR\r\n`를 **부분 수신 상황에서** 조립한다. 핵심: 누적 버퍼 + 줄 단위 프레이밍 + 오버플로 방어(긴 줄이 와도 버퍼를 넘지 않게, 넘으면 그 줄을 버리고 플래그). Connectivity 팀 도메인이라 나오면 가산점을 크게 받는다.

---

## 5장. 파트 B — 시스템 설계 예상 3제

### 5.0 어떤 문제가 나와도 이 순서로

| 단계 | 할 일 | 첫 문장 |
|---|---|---|
| 1 | 요구사항·제약 | "Before I design, a few questions: rate, size, loss tolerance, memory budget, how long can the link be down?" |
| 2 | 데이터 흐름 | "Let me draw the boxes first: sensor → sampler → buffer → uploader → cloud." |
| 3 | 인터페이스 | "The boundary between them is `push(sample)` and `pop_batch(out, max)`." |
| 4 | 동시성 모델 | "Two threads, one shared queue — I want exactly one place where they meet." |
| 5 | 실패와 복구 | "Now the failure modes: link down, power loss, sensor not responding, clock not synced." |
| 6 | 관측 가능성 | "I'd export sent / dropped / retries / queue_depth / last_error." |
| 7 | 테스트 + trade-off | "Unit tests with fakes, fault injection for the link, and here's what I traded." |

### 5.1 설계 A — 센서에서 클라우드까지 (가장 유력)

```
 [I2C 센서] ──▶ (샘플러 스레드, 1Hz, 절대 deadline)
                     │ push (drop-oldest + 카운터)
                     ▼
               [bounded queue, RAM]
                     │ pop_batch (최대 32개 또는 30초)
                     ▼
              (업로더 스레드) ──▶ HTTPS ──▶ 클라우드
                     │ 실패 시 지수 백오프 + 지터
                     ▼
               [플래시 spill]  ← 링크 복구 시 먼저 재전송
```

- **스레드 2개, 공유는 큐 하나.** 샘플러는 절대 블록되지 않는다(고정 주기).
- **실패**: 링크 단절 → 백오프 재연결, RAM 큐 차면 플래시 spill(수명 고려해 배치 append) · 전원 손실 → 임시 파일 + fsync + rename으로 원자적 저장 · 시계 미동기 → monotonic 타임스탬프 + 부팅 ID로 보내고 서버가 보정 · 중복 → 시퀀스 번호 + 서버 멱등.
- **trade-off 한 문장**: "배치를 키우면 전력과 요금이 줄지만 지연이 늘고 전원 손실 시 잃는 양이 커집니다. 30초 배치에 알람은 즉시 전송하는 별도 경로로 절충하겠습니다."

### 5.2 설계 B — WAN failover (Ethernet ↔ LTE, 듀얼 SIM)

- **구조**: 인터페이스별 모니터 → 이벤트 큐 → **단일 의사결정 스레드(상태머신)** → 라우팅 변경. 라우팅 테이블을 한 스레드만 만지니 **락이 필요 없다**(actor 모델).
- **health check는 ping이 아니라 실제 HTTPS**: 캐리어가 ICMP를 막거나 walled garden일 수 있다.
- **히스테리시스**: 3회 연속 실패 → 강등, 5회 연속 성공 → 복귀. 플래핑 방지.
- **듀얼 SIM**: 등록 실패 / 데이터 불통 / 품질 저하를 구분, 전환에 쿨다운(재등록 수십 초).
- **테스트**: 상태머신을 순수 함수로 만들어 "이벤트 시퀀스 → 기대 상태" 표 테스트 + 가짜 시계로 히스테리시스 검증.

### 5.3 설계 C — fleet OTA (200만 대)

- **A/B 파티션** + 서명 검증 + 부팅 후 **헬스 체크 통과 전까지 커밋 보류**(실패하면 watchdog 리셋 → 이전 슬롯).
- **단계적 롤아웃** 1% → 10% → 100%, 지표 악화 시 자동 중단.
- **셀룰러 제약**: delta 업데이트, 시간대 분산.
- **전원 손실 안전**: 쓰기 중 꺼져도 현재 슬롯은 멀쩡.
- Verkada의 2021년 보안 사고 이력을 감안해 **서명·secure boot를 먼저** 언급하면 좋다.

### 5.4 설계 답변에서 쓸 trade-off 문장 틀

> "I'm choosing **X** over **Y** because **[요구사항]**. The cost is **[포기한 것]**, which I'd mitigate with **[보완책]**. If **[조건이 달라지면]**, I'd switch to **Y**."
---

## 6장. 개념 속사포 — 나올 확률 높은 25문항

> 먼저 **소리 내어** 답하고 펼친다. 각 답은 "한 문장 + 근거 한 문장" 길이로 연습한다.

```check
Q: thread-safe를 한 문장으로 정의하라.
A: 여러 스레드가 어떤 순서로 끼어들어도 그 코드가 약속한 불변식이 깨지지 않고, 단일 스레드 실행과 같은 의미의 결과를 내는 것.
Q: data race와 race condition의 차이는?
A: data race는 동기화 없는 동시 접근(하나 이상 쓰기)으로 C 표준상 UB이며 TSan이 잡는다. race condition은 타이밍에 따라 결과가 달라지는 논리 버그로, 모든 접근이 락으로 보호돼도 생길 수 있다(check-then-act).
Q: 락은 무엇을 지키는가?
A: 변수가 아니라 불변식을 지킨다. 불변식이 깨져 있는 구간 전체가 하나의 임계구역이어야 한다. 그래서 여러 필드에 걸친 관계는 필드별 atomic으로는 지킬 수 없다.
Q: 임계구역 안에서 하면 안 되는 것 세 가지는?
A: I/O·로그, 남의 콜백 호출, 블로킹 대기(그리고 큰 할당). 느려서 다른 스레드를 굶기고, 콜백이 같은 락을 다시 잡으면 데드락이 난다.
Q: cond_wait가 뮤텍스를 인자로 받는 이유는?
A: "뮤텍스 놓기"와 "잠들기"를 원자적으로 하기 위해서다. 틈이 있으면 그 사이에 상태가 바뀌고 신호가 와도 못 듣는다(lost wakeup). 깨어나면 반환 전에 뮤텍스를 다시 잡는다.
Q: 왜 if가 아니라 while인가?
A: 가짜 기상, 깨어나 락을 다시 잡는 사이 다른 스레드가 조건을 소비하는 경우, broadcast로 여럿이 깨었지만 조건을 만족하는 건 하나인 경우 — 셋 다 "깨어났는데 조건이 거짓"이다.
Q: signal과 broadcast는 언제 무엇을?
A: 조건을 만족시킬 수 있는 대상이 하나면 signal. 종료나 설정 변경처럼 모두가 상태를 다시 봐야 하면 broadcast. 종료에 signal만 쓰면 나머지가 영원히 잠든다.
Q: 종료 프로토콜 5단계는?
A: 락 안에서 종료 플래그 → 모든 condvar broadcast → 대기자가 while 조건에서 종료를 보고 탈출 → 모든 스레드 join → join 후에만 자원 해제.
Q: volatile로 스레드 동기화가 되나?
A: 안 된다. 컴파일러가 매번 메모리에 접근하게 할 뿐 원자성도, CPU 재배치 방지도, 다른 변수와의 순서 보장도 없다. MMIO 레지스터와 시그널 플래그용이다.
Q: release store / acquire load가 각각 보장하는 것은?
A: release store는 그 이전의 모든 읽기·쓰기가 뒤로 밀리지 않음을, acquire load는 그 이후의 접근이 앞으로 당겨지지 않음을 보장한다. 같은 변수에서 짝을 이루면 release 이전의 쓰기가 acquire 이후에 보인다.
Q: relaxed는 언제 쓰나?
A: 다른 데이터와 순서 관계가 없는 통계 카운터. 링버퍼 인덱스 publish처럼 데이터와 짝을 이루는 곳에는 절대 쓰면 안 된다.
Q: SPSC 링버퍼가 락 없이 안전한 이유는?
A: tail은 생산자만, head는 소비자만 쓴다. 같은 변수에 대한 쓰기 경쟁이 없고, 상대 인덱스는 acquire로 읽고 자기 인덱스는 release로 publish하기 때문에 데이터가 인덱스보다 먼저 보인다.
Q: 데드락의 네 조건과 실무 해법은?
A: 상호 배제, 점유 대기, 비선점, 순환 대기. 실무에서는 순환 대기를 깬다 — 모든 코드가 전역으로 정해진 같은 순서(ID/주소)로 락을 잡는다.
Q: trylock + 백오프에서 빠뜨리면 안 되는 두 가지는?
A: 두 번째 락을 못 잡으면 첫 번째 락도 놓는다. 재시도 대기에 무작위성을 넣는다(아니면 livelock).
Q: 우선순위 역전이란? 해법은?
A: 낮은 우선순위가 락을 쥔 채 중간 우선순위에 선점되면, 락을 기다리는 높은 우선순위가 무관한 태스크 때문에 막힌다. 우선순위 상속 뮤텍스를 쓰거나, 자원을 한 태스크가 소유하고 메시지로 요청하게 한다.
Q: FreeRTOS에서 상호 배제에 바이너리 세마포어를 쓰면 안 되는 이유는?
A: 주인 개념이 없어 우선순위 상속을 지원하지 않는다. xSemaphoreCreateMutex로 만든 뮤텍스를 써야 한다. 바이너리 세마포어는 ISR→태스크 신호용이다.
Q: ISR 안에서 하면 안 되는 것은?
A: 블로킹(뮤텍스, malloc, printf)과 긴 작업. 플래그를 세우거나 lock-free 링버퍼에 넣고 바로 나온다. RTOS API는 FromISR 변형만.
Q: 더블 버퍼링에서 락이 보호하는 것은?
A: 데이터가 아니라 버퍼의 소유권(인덱스)이다. 임계구역은 인덱스 몇 개 교체뿐이고 프레임 쓰기·읽기는 락 밖에서 한다.
Q: 더블 vs 트리플 버퍼의 trade-off는?
A: 더블은 메모리가 적은 대신 소비자가 느리면 생산자가 READY 프레임을 버려야 한다. 트리플은 프레임 하나만큼 메모리를 더 쓰고 생산자가 기다리거나 버리지 않는다.
Q: 큐가 가득 찼을 때 정책은 무엇이 정하나?
A: 데이터의 의미. 최신이 중요하면 drop-oldest, 과거 이력이 중요하면 drop-newest, 잃으면 안 되면 block(단 실시간 경로 제외). 무엇을 하든 버린 수를 카운트해 노출한다.
Q: 주기 실행에서 sleep 루프의 두 문제는?
A: 작업 시간이 누적돼 주기가 밀리는 drift, 그리고 종료 반응이 한 주기만큼 늦는 것. 이전 deadline에 주기를 더하는 절대 시각 스케줄 + 취소 가능한 timed wait로 해결한다.
Q: pthread_cond_timedwait의 시계 함정은?
A: 기본이 CLOCK_REALTIME이라 NTP 조정에 영향을 받는다. Linux는 condattr로 CLOCK_MONOTONIC을 지정하고, macOS는 상대 시간 API를 쓴다.
Q: 하드웨어 없이 드라이버를 어떻게 테스트하나?
A: HAL과 시계를 인터페이스로 주입하고 테스트에서 fake를 꽂는다. 시계를 조종하니 타임아웃 테스트가 즉시 끝난다. 그 위에 통합(HIL), 시스템(챔버·전원 사이클) 층을 쌓는다.
Q: 동시성 코드는 어떻게 검증하나?
A: 불변식 기반 스트레스 테스트(생산 = 소비 + 버림 + 남음), ThreadSanitizer를 CI에, 결정적 스케줄 주입, 장시간 soak.
Q: 게이트웨이가 CGNAT 뒤에 있으면 무엇이 달라지나?
A: 클라우드가 기기로 먼저 접속할 수 없다. 기기가 아웃바운드 연결을 열어 keepalive로 유지하고 그 연결로 명령을 받는다.
```

---

## 7장. 말하기 — 그대로 쓸 문장들

### 7.1 오프닝 (배경 요청을 받으면 2~3문장)

> "I'm an embedded systems engineer — currently at Apple on wireless chipset integration, where I own root-cause analysis when new silicon meets the full system. Before that, about seven years of production SSD firmware in C, bringing up ARM controllers and shipping features end to end. The producer-consumer and DMA buffering patterns in this role are what I did every day there."

### 7.2 각 채점 기준에 대응하는 행동

| 메일의 기준 | 내가 할 행동 |
|---|---|
| design decisions and trade-offs | 선택할 때마다 "X를 골랐고, 이유는 Y, 포기한 건 Z" |
| clean and maintainable code | 짧은 함수, 불변식 주석 한 줄, 에러 경로 출구 하나 |
| edge cases and robustness | 면접관이 묻기 **전에** 8.5 체크리스트 낭독 |
| clarity of communication | 코딩 전 30초 설계 요약 + 치면서 내레이션 |

### 7.3 상황별 문장

- 시작: "Before I code, let me restate the requirements and my assumptions."
- 단순함 옹호: "I'll start with the simplest correct design — one mutex — and optimize only with a measured bottleneck."
- 더블 버퍼: "I protect ownership of the buffers, not the data itself."
- drop: "A dropped sample is acceptable; a silently dropped sample is a bug — so I export a counter."
- volatile: "volatile won't help here — no atomicity, no ordering. I'll use a release store and an acquire load."
- 테스트: "I'd verify with invariant-based stress tests and run them under ThreadSanitizer in CI."
- 경험 연결: "In my SSD firmware work, the host command queue feeding NAND operations was exactly this pattern."
- 전환 제안: "This works for the base case — would you like me to extend it, or move on to the design part?"

### 7.4 막혔을 때

1. 침묵하지 않는다 → "I'm deciding whether the closed flag belongs in the wait predicate…"
2. 더 단순한 버전부터 → "Let me get it correct with one lock first."
3. 모르는 API는 추측하지 않는다 → "I don't recall the exact signature; the idea is X and I'd confirm with the man page."
4. 힌트는 고맙게 받고 **즉시 반영**한다. 받는 태도도 평가된다.

### 7.5 하지 말 것

- 근거 없이 lock-free 제안 · `if`로 조건 검사 · 종료에 signal만 · 락 안에서 I/O
- "그냥 되니까요"로 설명 끝내기 · 요구사항 질문 없이 바로 코딩
- 모르는 걸 아는 척 (특히 Linux 네트워킹·모뎀 스택 — 솔직하게 "배우는 중"이라고 말하고 아는 것을 정확히)

---

## 8장. 인터뷰 중 치트시트 (한 화면)

### 8.1 Bounded queue 골격

```c
lock; while (full && !closed) wait(not_full);      if (closed) {unlock; return false;}
      store; count++; unlock; signal(not_empty);
lock; while (empty && !closed) wait(not_empty);    if (empty) {unlock; return false;}
      load; count--; unlock; signal(not_full);
close: lock; closed=true; unlock; broadcast(both);
```

### 8.2 SPSC 링버퍼 골격

```c
push: t=load(tail,relaxed); n=(t+1)&MASK;
      if (n==load(head,acquire)) return false;     /* full → drop++ */
      buf[t]=x; store(tail,n,release);
pop:  h=load(head,relaxed);
      if (h==load(tail,acquire)) return false;     /* empty */
      x=buf[h]; store(head,(h+1)&MASK,release);
```

### 8.3 더블 버퍼 소유권

```
FREE → (write_begin) → WRITER → (commit) → READY → (acquire) → READER → (release) → FREE
빈 버퍼 없으면 READY를 희생 + dropped++   |   READER 버퍼는 절대 건드리지 않는다
```

### 8.4 의존성 주입 골격

```c
typedef struct { IoStatus (*read)(void*,...); IoStatus (*write)(void*,...); void *ctx; } I2cOps;
typedef struct { uint32_t (*now_ms)(void*); void *ctx; } ClockOps;
/* 제품: 진짜 HAL · 테스트: fake + FakeClock(ms를 테스트가 증가시킴) */
```

### 8.5 edge case 낭독 리스트

- 빈 상태 pop / 가득 찬 상태 push
- 모든 wait가 while 안에 있나
- 종료: broadcast, 종료 후 push 거부, 남은 항목 drain, 이중 close 멱등
- join 후에만 해제 · 모든 에러 경로에서 unlock
- 락 안에서 I/O·콜백 없음 · drop 카운트 · NULL/용량 0 검증

### 8.6 memory order 한 줄 표

| order | 언제 |
|---|---|
| relaxed | 독립 통계 카운터 |
| release (store) | 데이터를 publish하는 쪽 |
| acquire (load) | publish된 데이터를 받는 쪽 |
| seq_cst | 기본값, 헷갈리면 이것 |

---

## 9장. 오늘 밤 · 내일 아침 · 직전 5분

### 9.1 오늘 밤 (2~3시간)

- [ ] **P1 bounded queue를 빈 파일에서 45분 타이머로 1회** — `cd concurrency_practice && make run N=01`로 채점
- [ ] 같은 문제를 다시 한 번, 이번엔 **말하면서** (혼잣말로 내레이션 연습)
- [ ] 2장 P2(더블 버퍼) 소유권 3상태를 **종이에 그려보기** — 그림이 손에 익어야 한다
- [ ] 2장 P3의 "하드웨어 없이 테스트" 3층 답변을 소리 내어 1회
- [ ] 6장 속사포 25문항 — 답을 먼저 말하고 확인
- [ ] 일찍 잘 것. 새 주제를 지금 시작하지 않는다.

### 9.2 내일 아침 (30~40분)

- [ ] 8장 치트시트 전체 훑기 (5분)
- [ ] P1 골격을 **손으로 한 번 더** 타이핑 (10분)
- [ ] 7장 문장들 소리 내어 읽기 (5분)
- [ ] 5장 설계 7단계와 설계 A 흐름도 1회 (10분)
- [ ] 9.4 역질문 확인 (2분)

### 9.3 세션 직전 5분 — 환경 점검

- [ ] 조용한 공간, 안정적인 네트워크, 헤드셋 확인
- [ ] 에디터 준비: 폰트 크게, 자동완성이 방해되면 끄기. 컴파일 가능한 환경이면 더 좋다
- [ ] 코딩 언어 확인 — **C로 하겠다고 먼저 말한다** (C++도 가능하다고 덧붙이기)
- [ ] 종이와 펜 (설계 그림용), 물
- [ ] 이 문서의 8장을 **따로 띄워두기** (다른 창/다른 모니터)

### 9.4 마지막에 할 역질문 5개

1. Connectivity 팀에서 게이트웨이 펌웨어와 센서 펌웨어, 백엔드의 비중은 어떻게 나뉘나요? 제가 들어가면 처음 6개월은 어디에 있게 될까요?
2. 필드에서 가장 자주 보는 연결성 이슈는 어떤 유형인가요? 200만 대 규모에서 원격 진단과 텔레메트리는 어떻게 하시나요?
3. 새 LTE 모듈 도입이나 캐리어 인증은 누가 주도하나요? 앞으로 1~2년 로드맵(5G RedCap, eSIM, 해외 확장)은 어떤가요?
4. 이 팀의 코드 리뷰와 테스트 문화는 어떤가요? CI에서 하드웨어 없이 돌아가는 테스트 비중은 어느 정도인가요?
5. 이 자리에서 첫 6개월에 "잘하고 있다"는 기준은 무엇인가요?

### 9.5 인터뷰 직후 (잊기 전에)

- [ ] 받은 문제와 follow-up을 **그대로** 적어두기 → 컨텍스트 파일 6절 "인터뷰 노트"에 추가
- [ ] 잘한 점 / 막힌 점 / 다음 라운드에 고칠 것 3줄

---

## 10장. 함께 보는 자료

| 자료 | 언제 |
|---|---|
| [학습 가이드 (개념 12장)](2026-09-19_verkada_concurrency_study_guide.html) | 개념이 흔들릴 때 해당 장만 |
| [빈출 10문제 (해설·follow-up)](verkada_concurrency_top10.html) | 각 문제의 깊은 해설 |
| [노트 허브 (기초 11 + 복습 8)](notes_site/index.html) | 주제별 정리 |
| [문제 은행 대시보드 (78문제)](verkada_prep/index.html) | 손으로 드릴 |
| [회사·포지션 컨텍스트](verkada_sr_embedded_linux_engineer_connectivity_context.html) | 역질문·회사 맥락 |

> 내일 끝나고 받은 질문을 알려주면 컨텍스트 파일에 기록하고, 다음 라운드(온사이트) 대비로 바로 이어가겠다.
