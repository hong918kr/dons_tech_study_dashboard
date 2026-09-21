# Verkada 2차 인터뷰 — 동시성·실시간·설계 빈출 10문제

> **최종 갱신**: 2026-09-15 · **대상**: Verkada · Senior Embedded Linux Engineer, Connectivity (San Mateo)
> **근거**: 리크루터 Davis의 안내 메일 (2026-09-15 수신) — "두 파트, problem-solving + system design" · 권장 복습 주제 4가지
> **연습 코드**: `verkada_prep/` (8세트 78문제 은행 + 대시보드) · `concurrency_practice/` — 10문제 모두 `cc -std=c11 -Wall -Wextra` 컴파일 + 실행 검증 완료, ThreadSanitizer 클린(04 제외: 의도된 race)
> 사용법: 문제를 먼저 읽고 `make run N=01`로 직접 구현 → 막히면 해답 펼치기 → `make sol N=01`로 대조

---

## 0. 메일 해석 — 무엇을 묻겠다는 신호인가

Davis가 적어준 4개 항목은 그냥 인사말이 아니라 **출제 범위 공지**다. 각 항목이 실제 문제로 어떻게 나오는지 매핑하면 이렇다.

| 메일 문구 | 실제 출제 형태 | 해당 문제 |
|---|---|---|
| Thread safety in multi-threaded environments | "여러 스레드가 공유하는 자료구조를 설계/구현하라" — 거의 확실히 **bounded queue** | 01, 05, 06 |
| Synchronization mechanisms (mutexes, condition variables, **atomic operations**) | condvar 사용법(while 루프, broadcast vs signal)과 atomic/memory order 질문 | 01, 02, 04, 08 |
| Real-time data handling patterns (such as **double buffering**) | 센서·프레임 데이터를 놓치지 않고 찢어지지 않게 전달 | 03, 04, 05 |
| Design principles for modular, testable embedded software | "하드웨어 없이 어떻게 테스트하나" — 의존성 주입, 시간 주입, 순수 함수 분리 | 10 (+ 전 문제의 테스트 설계) |

평가 기준도 메일에 그대로 적혀 있다 — 코드가 돌아가는 것만으로는 부족하다는 뜻이다.

1. **설계 결정과 trade-off 정당화** → "왜 mutex가 아니라 atomic인가", "왜 drop-oldest인가"를 *말로* 설명
2. **깨끗하고 유지보수 가능한 코드** → 짧은 함수, 명확한 이름, 불변식을 주석으로
3. **edge case와 concurrent 시스템의 견고성** → 종료, 가득 참, 빈 큐, spurious wakeup, 중복 close
4. **커뮤니케이션과 사고 과정의 명료함** → 코드 치기 전에 30초 설계 요약, 치면서 내레이션

> 💡 Don의 강점 연결: SSD 펌웨어의 producer/consumer(호스트↔NAND), DMA 완료 인터럽트, 링버퍼, 에러 리포팅은 전부 이 주제다. "임베디드 Linux 경험이 얕다"는 갭을 **동시성·실시간 설계의 깊이**로 덮는 자리다.

---

## 1. 세션 운영 전략

### 예상 구조 `[추정]`
```
파트 A (25~35분) — problem solving / 라이브 코딩
   공유 자료구조 1개 구현 (bounded queue 또는 ring/double buffer)
   → 동작하면 follow-up으로 확장 (다중 소비자, drop 정책, 종료, lock-free)
파트 B (20~30분) — system design
   "게이트웨이에서 센서 데이터를 클라우드까지" 같은 열린 설계
   → 스레드 모델, 버퍼링, 실패/재연결, 테스트 전략을 묻는다
```

### 코딩 파트 45분 배분
| 시간 | 할 일 | 말할 것 |
|---|---|---|
| 0–3분 | 요구사항 확인 | "생산자 몇 개, 소비자 몇 개인가요? 큐가 가득 차면 블록인가요 drop인가요? 종료는 어떻게 알리나요?" |
| 3–6분 | 인터페이스 먼저 | "API를 먼저 정하겠습니다: init/push/pop/close. 이 네 개면 사용자가 오용할 여지가 적습니다" |
| 6–8분 | 동기화 전략 선언 | "mutex 하나로 상태를 보호하고, not_full/not_empty 두 개의 condvar를 쓰겠습니다. 이유는…" |
| 8–30분 | 구현 (내레이션) | "여기서 while을 쓰는 이유는 spurious wakeup과 다중 소비자 때문입니다" |
| 30–38분 | 스스로 edge case 점검 | "확인할 것: 빈 큐 pop, 가득 참 push, close 후 push, close 시 대기 중인 스레드, 이중 close" |
| 38–45분 | 테스트 전략 + 확장 | "단위 테스트는 이렇게, 스트레스 테스트는 이렇게, TSan으로 검증합니다" |

### 반드시 입 밖으로 내야 하는 문장 5개
1. "조건 검사는 `if`가 아니라 `while`입니다 — spurious wakeup과, 깨어난 뒤 다른 스레드가 먼저 가져가는 경우 때문입니다."
2. "종료 시에는 `signal`이 아니라 `broadcast`입니다 — 대기 중인 스레드가 하나가 아닐 수 있습니다."
3. "작업 실행(또는 I/O)은 락을 놓고 합니다 — 임계구역은 상태 변경만."
4. "버린 데이터는 반드시 카운트해서 보고합니다 — 조용한 손실이 가장 디버깅하기 어렵습니다."
5. "이건 `volatile`로 해결되지 않습니다. `volatile`은 최적화 억제일 뿐 원자성도 순서도 보장하지 않습니다."

---

## 2. 빈출 10문제

각 문제: **왜 나오는지 → 문제 → 인터뷰 진행 순서 → 힌트 → 해답(접힘) → 흔한 실수 → follow-up**
연습: `cd concurrency_practice && make run N=NN` (스텁 구현) / `make sol N=NN` (모범답안) / `make tsan N=NN`

---

### 문제 01 · Bounded thread-safe queue (MPSC) — 출제 확률 ★★★★★

**왜 1순위인가**: 메일의 "thread safety + mutex + condition variable"을 한 문제로 전부 확인할 수 있다. 실제로 Don이 예전에 정리해둔 Verkada 연습 문제(`don-c-prac-master/0ESE_prep/Verkada_FWE_prep/concurrency_prac/note`, AC42 출입 이벤트 큐)도 정확히 이 문제다. **이 문제가 안 나올 가능성이 더 낮다.**

**문제**
> 게이트웨이 펌웨어에서 4개의 I/O 스레드(도어/센서/모뎀/링크 모니터)가 이벤트를 만들고, 하나의 로깅 스레드가 이를 직렬화해 업로드 버퍼에 쓴다. 크기가 제한된 스레드 안전 큐를 구현하라. 큐가 가득 차면 생산자는 블록, 비면 소비자는 블록한다. 종료 시 대기 중인 스레드가 영원히 잠들면 안 된다.

**인터뷰 진행 순서**
1. 질문: 생산자/소비자 수, 용량, 종료 시맨틱(남은 항목 처리 여부), 항목 소유권(복사 vs 포인터)
2. API 선언: `bq_init / bq_push / bq_pop / bq_close / bq_destroy`
3. 상태 정의: `buf, cap, head, count, closed` + `mutex, not_full, not_empty`
4. 구현 → edge case 점검 → 테스트 설명

**힌트**
- `count`를 두면 `head == tail`의 모호함(빈 것 vs 가득 참)이 사라진다
- `closed` 플래그는 **뮤텍스로 보호되는 상태**다. atomic으로 따로 두면 조건 검사와 대기 사이에 창이 생긴다
- `pthread_cond_signal`은 unlock **후**에 호출하는 편이 좋다(깨어난 스레드가 곧장 락을 잡는다)

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
bool bq_push(BQueue *q, AccessEvent ev)
{
    pthread_mutex_lock(&q->m);
    while (q->count == q->cap && !q->closed)      /* if가 아니라 while */
        pthread_cond_wait(&q->not_full, &q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return false; }

    q->buf[q->tail] = ev;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);
    return true;
}

bool bq_pop(BQueue *q, AccessEvent *out)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);
    if (q->count == 0) { pthread_mutex_unlock(&q->m); return false; } /* closed && empty */

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
    pthread_cond_broadcast(&q->not_empty);        /* 전원 기상 */
    pthread_cond_broadcast(&q->not_full);
}
```
전체 코드 + 테스트: `concurrency_practice/solutions/01_bounded_queue.c`
(4 생산자 × 2000개, 용량 8로 블로킹 경로 강제, 생산자별 순서 보존 검증, TSan 클린)
</details>

**흔한 실수**
- `if (q->count == 0) cond_wait(...)` — spurious wakeup에서 빈 큐를 pop
- close 시 `signal` 하나만 호출 → 나머지 스레드 영구 대기
- `closed`를 검사한 뒤 락을 놓고 다시 잡는 패턴 → TOCTOU
- 소비자에게 포인터를 넘기면서 소유권을 문서화하지 않음 → double free

**Follow-up과 답**
- *"단순 mutex만 쓰고 busy-wait 하면?"* → CPU를 태우고, 우선순위가 낮은 생산자가 굶는다. condvar는 커널이 스레드를 재우고 정확히 필요할 때 깨우므로 전력(배터리 트레일러!)과 지연 모두 유리하다.
- *"소비자를 여러 개로 늘리면?"* → 코드는 그대로 동작하지만 **항목 간 전역 순서가 깨진다**. 순서가 필요하면 (a) 소비자를 하나로 유지, (b) 항목에 시퀀스 번호를 붙여 재정렬, (c) 키별로 큐를 샤딩.
- *"타임아웃이 필요하면?"* → `pthread_cond_timedwait`, 그리고 문제 07의 CLOCK_MONOTONIC 함정.
- *"C++라면?"* → `std::mutex` + `std::condition_variable` + `std::unique_lock`, `wait(lock, pred)` 형태로 predicate를 넘기면 while이 내장된다. RAII로 예외 안전성도 확보.

---

### 문제 02 · Lock-free SPSC ring buffer (ISR ↔ task) — ★★★★☆

**왜 나오는가**: "임베디드에서 mutex를 못 쓰는 곳은 어디인가?"의 정답이 ISR이다. UART/모뎀 RX 경로는 게이트웨이의 실제 코드다. atomic과 memory order를 묻는 자리이기도 하다.

**문제**
> UART RX 인터럽트가 바이트를 넣고, 파서 태스크가 꺼낸다. 인터럽트 컨텍스트에서는 블록할 수 없다. 락 없이 안전한 단일 생산자/단일 소비자 링버퍼를 구현하라.

**힌트**
- 생산자는 `tail`만 쓰고, 소비자는 `head`만 쓴다 → 각자 상대편 인덱스는 읽기만
- 용량을 2의 거듭제곱으로 잡고 `& (CAP-1)`. 나눗셈 없는 MCU에서 중요
- 한 칸을 비워두면 `head == tail`(빈 것)과 가득 참을 구분할 수 있다
- `volatile`이 아니라 `_Atomic` + release/acquire

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
bool ring_push(Ring *r, uint8_t b)            /* ISR 전용 */
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);  /* 내가 쓴 값 */
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                          /* full → 실전에선 drop 카운트++ */

    r->buf[t] = b;                             /* ① 데이터 먼저 */
    atomic_store_explicit(&r->tail, next, memory_order_release); /* ② 그 다음 publish */
    return true;
}

bool ring_pop(Ring *r, uint8_t *out)          /* task 전용 */
{
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    if (h == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                          /* empty */
    *out = r->buf[h];
    atomic_store_explicit(&r->head, (h + 1u) & (RING_CAP - 1u), memory_order_release);
    return true;
}
```
**release/acquire의 의미**: ①이 ②보다 먼저 보이도록 강제한다. 이게 없으면 컴파일러나 CPU가 순서를 바꿔 소비자가 "인덱스는 늘었는데 데이터는 아직 안 쓰인" 상태를 볼 수 있다.

전체 코드: `concurrency_practice/solutions/02_spsc_ring.c` (20만 바이트, 순서·합계 검증, TSan 클린)
</details>

**흔한 실수**
- `volatile unsigned head/tail`로 충분하다고 답하기 → **오답**. volatile은 재배치/가시성을 보장하지 않는다(아래 4장 Q1)
- 인덱스를 하나만 두고 count를 공유 → count는 양쪽이 쓰므로 원자적 갱신이 필요해져 SPSC의 이점이 사라진다
- 가득 찼을 때 ISR에서 재시도/대기 → 인터럽트를 막는다. **drop하고 카운터를 올린다**

**Follow-up과 답**
- *"MPSC로 확장하려면?"* → tail을 CAS 루프로 예약(`compare_exchange_weak`)하고, 데이터 기록 후 publish 순서를 보장하는 시퀀스 배열 필요(Vyukov 큐). 복잡도가 급증하므로 대개 **생산자별 큐 + 소비자가 라운드로빈**이 실전적이다.
- *"ARM Cortex-M에서 atomic이 없으면?"* → 단일 코어면 `__disable_irq()`/`__enable_irq()`로 초단기 임계구역, 또는 LDREX/STREX. 인덱스가 워드 정렬 + 32비트면 로드/스토어 자체는 원자적이지만 **순서는 여전히 배리어(`__DMB()`)가 필요**.
- *"false sharing?"* → head/tail을 다른 캐시라인에 배치(`_Alignas(64)`). 멀티코어 A7/A53급 게이트웨이에서 처리량이 눈에 띄게 바뀐다.

---

### 문제 03 · Double buffering (ping-pong) — ★★★★★ (메일에 이름이 직접 나옴)

**왜 1순위인가**: 메일이 "such as double buffering"이라고 **예시까지 적어줬다**. 나올 확률이 가장 높은 개념 중 하나.

**문제**
> 카메라/센서가 DMA로 한 버퍼를 채우는 동안 처리 스레드가 다른 버퍼를 읽는다. (1) 읽는 쪽은 절반만 갱신된 프레임을 절대 보면 안 되고, (2) 쓰는 쪽은 소비자를 기다리면 안 되며(샘플 손실 금지), (3) 소비자가 느리면 최신 프레임만 보면 된다. 설계하고 구현하라.

**핵심 통찰 (이걸 말하면 점수)**
> 락으로 **데이터**를 보호하지 말고 **버퍼 소유권**을 보호한다. 임계구역은 인덱스 몇 개를 바꾸는 것뿐이라 수십 나노초로 끝나고, 실제 픽셀 복사는 락 밖에서 일어난다.

각 버퍼는 항상 세 상태 중 하나: `WRITER 소유` / `READY(publish됨)` / `READER 소유`

**힌트**
- `write_idx`, `ready_idx`, `reader_idx` 세 인덱스로 소유권을 표현하면 전부 깔끔해진다
- 버퍼가 2개뿐일 때, reader가 하나를 들고 있고 다른 하나가 READY면 writer는 쓸 곳이 없다 → **READY를 버린다**. 이것이 triple buffering이 존재하는 이유
- 절대 reader가 들고 있는 버퍼를 건드리지 않는다 = tearing 방지의 전부

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
Frame *db_write_begin(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    int idx = -1;
    for (int i = 0; i < NBUF; i++)                 /* 아무도 소유하지 않은 버퍼 */
        if (i != db->ready_idx && i != db->reader_idx) { idx = i; break; }
    if (idx < 0) {                                 /* 2-버퍼의 한계 지점 */
        idx = db->ready_idx;                       /* READY를 희생 */
        db->ready_idx = -1;
        db->dropped++;
    }
    db->write_idx = idx;
    pthread_mutex_unlock(&db->m);
    return &db->buf[idx];                          /* 채우기는 락 밖에서 */
}

void db_write_commit(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    if (db->ready_idx >= 0) db->dropped++;         /* 소비 전에 새 프레임이 옴 */
    db->ready_idx = db->write_idx;                 /* publish */
    db->write_idx = -1;
    pthread_mutex_unlock(&db->m);
}

Frame *db_read_acquire(DoubleBuf *db)
{
    Frame *f = NULL;
    pthread_mutex_lock(&db->m);
    if (db->reader_idx < 0 && db->ready_idx >= 0) {
        db->reader_idx = db->ready_idx;            /* 소유권 이전 */
        db->ready_idx = -1;
        f = &db->buf[db->reader_idx];
    }
    pthread_mutex_unlock(&db->m);
    return f;
}
```
`#define NBUF 3` 한 줄만 바꾸면 그대로 **triple buffering**이 된다 — 면접에서 이 확장성을 보여주면 좋다.

전체 코드: `concurrency_practice/solutions/03_double_buffer.c`
(2만 프레임, 프레임 전체를 동일 패턴으로 채워 tearing을 바이트 단위로 검출 → torn=0, 단조 증가 보장, TSan 클린)
</details>

**흔한 실수**
- `if (buffer_ready) swap()` 식으로 플래그 하나만 두기 → writer가 reader의 버퍼를 덮어써 tearing
- 데이터 복사 전체를 락 안에서 → 실시간성 파괴(DMA 완료 콜백이 블록된다)
- drop을 세지 않음 → 나중에 "가끔 프레임이 비어요" 버그를 추적 불가

**Follow-up과 답**
- *"DMA를 쓰면 어디가 달라지나?"* → 완료 인터럽트에서 `commit`만 하고 다음 버퍼 주소를 DMA에 미리 걸어둔다(ping-pong DMA). 버퍼는 캐시 정렬 + `dma_alloc_coherent`(또는 수동 invalidate/clean) 필요. 캐시 유지가 빠지면 stale 데이터를 읽는 전형적 버그.
- *"double vs triple의 trade-off"* → 2개: 메모리 절약, 대신 reader가 느리면 writer가 프레임을 버린다. 3개: writer가 절대 멈추지 않고 reader도 항상 최신을 받는다. 대신 메모리 1.5배. **1080p 프레임이면 메모리가 곧 BOM 비용**이라 제품 제약이 결정한다.
- *"소비자가 '최신'이 아니라 '모든' 프레임을 봐야 한다면?"* → 그건 double buffer가 아니라 큐(문제 01/05) 문제다. 요구사항을 다시 확인해야 한다.

---

### 문제 04 · Latest-value 스냅샷 (seqlock) — ★★★☆☆

**왜 나오는가**: "읽기는 매우 잦고 쓰기는 드문" 상태 공유(신호세기, WAN 상태, 온도)를 mutex 없이 다루는 패턴. atomic과 memory order를 깊게 물을 때 등장.

**문제**
> 게이트웨이 링크 상태(RSRP, RSRQ, uptime, SIM 슬롯)를 1초마다 갱신하는 writer 1개와, 수시로 읽는 reader 여러 개가 있다. reader는 **여러 필드를 같은 시점의 스냅샷으로** 읽어야 하고, reader 때문에 writer가 지연되면 안 된다.

**힌트**
- 짝수/홀수 시퀀스 번호: 홀수 = 쓰는 중
- reader는 `seq` 읽기 → 데이터 복사 → `seq` 다시 읽기, 두 값이 같고 짝수면 유효
- writer는 절대 기다리지 않는다. 대신 reader가 재시도한다(= writer 우선 정책)

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
void seq_write(SeqStatus *s, const LinkStatus *in)
{
    uint32_t v = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, v + 1u, memory_order_relaxed);  /* 홀수 = 쓰는 중 */
    atomic_thread_fence(memory_order_release);
    s->data = *in;
    atomic_store_explicit(&s->seq, v + 2u, memory_order_release);  /* 짝수 = 완료 */
}

void seq_read(const SeqStatus *s, LinkStatus *out)
{
    for (;;) {                                     /* ★ do/while + continue 금지 (아래 함정) */
        uint32_t before = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (before & 1u) continue;                 /* 쓰는 중 → 재시도 */
        *out = s->data;
        atomic_thread_fence(memory_order_acquire);
        uint32_t after = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (before == after) return;               /* 스냅샷 유효 */
    }
}
```

> ⚠️ **실제로 밟은 함정**: `do { ... if (before & 1) continue; ... } while (before != after);`
> 로 쓰면 `continue`가 **조건식으로 점프**해서, 첫 바퀴에 홀수를 만나면 초기화되지 않은
> `after`와 비교한다(UB). 쓰레기 값이 우연히 같으면 `*out`을 채우지도 않고 반환한다.
> 면접에서 seqlock을 쓸 때는 `for(;;)` + 명시적 `return` 형태로 쓰는 게 안전하다.
전체 코드: `concurrency_practice/solutions/04_seqlock_latest.c` (writer 30만 회 + reader 4개, 불변식 `rsrq == -rsrp`로 찢어짐 검출 → inconsistent=0)
</details>

**정직하게 말해야 할 한계 (면접 가산점)**
- seqlock은 C 표준상 **엄밀히는 data race**다. 실제로 이 코드를 `-fsanitize=thread`로 돌리면 TSan이 경고한다(확인함). 실무에서는 ① 필드별 relaxed atomic으로 읽고 쓰거나 ② `memcpy` + 컴파일러 배리어 + 플랫폼 보장에 의존한다. Linux 커널의 seqlock도 같은 이유로 `READ_ONCE/WRITE_ONCE`를 쓴다.
- 데이터에 **포인터나 리소스 핸들이 있으면 쓰면 안 된다** — 찢어진 포인터를 잠시라도 역참조하면 죽는다. POD 스냅샷 전용.
- reader가 writer보다 훨씬 느리면 영원히 재시도할 수 있다(starvation) → 재시도 상한을 두고 실패 시 mutex 경로로 fallback.

**Follow-up**: *"그냥 mutex 쓰면 안 되나?"* → 된다. 그리고 대부분의 경우 **그게 정답**이다. seqlock은 "읽기가 초당 수만 번이고 writer 지연이 실시간 요구를 깬다"는 측정된 근거가 있을 때만. 근거 없이 lock-free를 쓰면 유지보수 비용만 오른다 — 이 말을 덧붙이면 "trade-off를 아는 시니어"로 읽힌다.

---

### 문제 05 · Backpressure와 drop 정책 — ★★★★☆

**왜 나오는가**: Connectivity 팀의 본질적 문제. LTE 링크가 느려지거나 끊기면 업로더가 막히는데 센서는 계속 돈다. **메모리는 유한하다.**

**문제**
> 텔레메트리 큐가 가득 찼다. 어떻게 할 것인가? 정책을 파라미터화한 큐를 구현하고, 각 정책이 어떤 데이터에 적합한지 설명하라.

| 정책 | 동작 | 적합한 데이터 |
|---|---|---|
| BLOCK | 생산자를 막는다 | 손실이 허용되지 않는 감사/과금 이벤트. **단, 실시간 경로(ISR, DMA 콜백)에서는 금지** |
| DROP_NEWEST | 새 것을 버린다 | 과거 이력이 중요한 로그 — 앞부분을 보존 |
| DROP_OLDEST | 오래된 것을 버린다 | 최신 상태가 중요한 상태/알람/신호세기 |

**힌트**: 어떤 정책이든 **버린 개수를 반드시 카운트해서 함께 업로드**한다. "조용한 손실"은 필드에서 가장 추적하기 어려운 버그다.

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
if (q->count == q->cap) {
    switch (q->policy) {
    case POLICY_BLOCK:
        while (q->count == q->cap && !q->closed)
            pthread_cond_wait(&q->not_full, &q->m);
        if (q->closed) { pthread_mutex_unlock(&q->m); return false; }
        break;
    case POLICY_DROP_NEWEST:
        q->dropped++;
        pthread_mutex_unlock(&q->m);
        return true;                       /* 실패가 아니라 '정책에 따른 폐기' */
    case POLICY_DROP_OLDEST:
        q->head = (q->head + 1) % q->cap;  /* 가장 오래된 것 폐기 */
        q->count--;
        q->dropped++;
        break;
    }
}
```
소비 측은 **배치로** 꺼낸다(`tq_pop_batch`) — LTE 업로드는 요청당 오버헤드가 크므로 16~64개씩 묶는 게 전력·데이터 요금 모두에 유리하다.

전체 코드: `concurrency_practice/solutions/05_drop_policy_queue.c`
불변식 `produced == consumed + dropped + queued`를 세 정책 모두에서 검증. TSan 클린.
</details>

**Follow-up과 답**
- *"링크가 몇 시간 끊기면?"* → RAM 큐는 상한이 있으므로 **플래시로 spill**한다. 단, eMMC/NAND 수명(write endurance) 때문에 무한정 쓸 수 없다 → 샘플링 주기를 줄이고(적응형 다운샘플링), 중요도 등급별로 우선 폐기. Don의 SSD 펌웨어 경험(wear, write amplification)을 여기서 꺼내면 강하다.
- *"우선순위가 다른 이벤트가 섞이면?"* → 등급별 큐 + 가중 라운드로빈. 알람은 절대 drop 안 함, 주기 텔레메트리는 drop 가능.
- *"재전송 중 중복은?"* → 시퀀스 번호 + 서버 멱등 처리. at-least-once를 택하고 중복 제거를 서버에 두는 게 디바이스에 싸다.

---

### 문제 06 · Worker pool과 graceful shutdown — ★★★☆☆

**왜 나오는가**: "종료"는 동시성 코드에서 가장 버그가 많은 지점이고, 면접관이 edge case 감각을 보기 가장 쉬운 곳이다.

**문제**
> 백그라운드 작업(로그 업로드, 설정 동기화, 헬스체크)을 처리하는 워커 풀을 만들라. 종료 모드 두 가지를 지원한다: **DRAIN**(남은 작업 모두 처리 후 종료) / **NOW**(진행 중 작업만 끝내고 큐는 폐기).

**힌트**: 상태를 `RUNNING / DRAIN / STOP` 3상태로 두면 워커 루프 조건이 단순해진다.

<details>
<summary><b>해답 보기 (핵심 구조)</b></summary>

```c
static bool pool_take(Pool *p, Task *out)
{
    pthread_mutex_lock(&p->m);
    while (p->count == 0 && p->state == POOL_RUNNING)
        pthread_cond_wait(&p->work, &p->m);

    if (p->count == 0 || p->state == POOL_STOP) {
        if (p->state == POOL_STOP) { p->discarded += p->count; p->count = 0; }
        pthread_mutex_unlock(&p->m);
        return false;                      /* 워커 종료 */
    }
    *out = p->q[p->head];
    p->head = (p->head + 1) % p->cap; p->count--; p->executed++;
    pthread_mutex_unlock(&p->m);
    pthread_cond_signal(&p->slot);
    return true;
}

static void *worker_main(void *arg) {
    Pool *p = arg; Task t;
    while (pool_take(p, &t)) t.fn(t.arg);  /* ★ 작업 실행은 락 밖에서 */
    return NULL;
}
```
전체 코드: `concurrency_practice/solutions/06_thread_pool.c`
검증: DRAIN은 5000개 전부 실행(discarded=0), NOW는 `executed + discarded == submitted`. TSan 클린.
</details>

**면접관이 파는 edge case 6개 (미리 말하면 이긴다)**
1. 워커가 `cond_wait`에 잠들었을 때 종료 → **broadcast**로 전부 깨워야 한다
2. 종료 후 `submit` → 조용히 무시하지 말고 **false를 반환**하고 거부 카운트를 센다
3. `join` 없이 자원 해제 → 워커가 free된 큐를 만진다(UAF). **shutdown은 반드시 join까지**
4. 큐가 가득 차서 제출자가 대기 중일 때 종료 → `not_full`도 broadcast
5. 작업 함수 자체가 블로킹(소켓 read) → 종료가 지연된다. **취소 신호를 작업에 전달**하거나 소켓에 타임아웃
6. 이중 shutdown 호출 → 멱등하게 만든다

**Follow-up**: *"작업 실행을 락 안에서 하면?"* → 워커가 사실상 1개가 된다(직렬화). 게다가 작업이 다시 `submit`을 호출하면 즉시 자기 자신과 데드락(재진입). **락 안에서는 절대 콜백/외부 코드를 부르지 않는다**가 원칙.

---

### 문제 07 · 주기 실행 + 즉시 취소 (timed wait) — ★★★☆☆

**문제**
> 센서를 100ms마다 샘플링하되, 종료 신호가 오면 즉시 깨어나야 한다. `sleep(100ms)` 루프의 문제 두 가지를 지적하고 고쳐라.

**두 문제**: (1) 작업 시간이 누적돼 주기가 밀린다(drift) (2) 종료 반응이 최대 한 주기 늦다

<details>
<summary><b>해답 보기 (핵심)</b></summary>

```c
/* drift 방지: '지금 + 주기'가 아니라 '이전 deadline + 주기' */
int64_t next = now_mono_ns() + period_ns;
for (;;) {
    if (periodic_wait_until(&p, next)) break;   /* stop이면 즉시 true */
    do_sample();
    next += period_ns;
    if (next < now_mono_ns()) {                 /* 주기를 놓쳤다 */
        late_ticks++;
        next = now_mono_ns() + period_ns;       /* catch-up 폭주 방지 */
    }
}
```

**이식성 함정 (가산점 포인트)**: `pthread_cond_timedwait`의 기본 시계는 **CLOCK_REALTIME**이다. NTP가 시각을 되돌리면 대기가 늘어진다.
- Linux: `pthread_condattr_setclock(&attr, CLOCK_MONOTONIC)`
- macOS: setclock 미지원 → `pthread_cond_timedwait_relative_np`

전체 코드: `concurrency_practice/solutions/07_periodic_timer.c`
측정 결과: 10ms 주기 20틱, 평균 10.09ms, 취소 지연 2ms 이내. TSan 클린.
</details>

**Follow-up**: *"주기를 놓쳤을 때 밀린 만큼 몰아서 실행할까?"* → 대개 **아니오**. 센서 샘플링에서 catch-up은 버스트를 만들어 상황을 악화시킨다. 놓친 횟수를 카운터로 노출하고(관측 가능성), 다음 정시로 재정렬한다. 반대로 과금·적분 계산이라면 놓친 구간을 보정해야 한다 — **데이터의 의미가 결정한다**.

---

### 문제 08 · 무중단 설정 교체 (atomic pointer + refcount) — ★★★☆☆

**문제**
> 클라우드에서 새 설정(APN, WAN 우선순위, 업로드 주기)이 내려온다. 워커 스레드들이 설정을 읽는 도중에 안전하게 교체하라. 읽기는 매우 잦고 쓰기는 드물다.

**설계**: 설정을 **불변 객체**로 만들고 포인터만 원자적으로 교체. 사용 중인 쪽이 refcount를 올리고, 0이 되면 해제.

<details>
<summary><b>해답 보기 + ★핵심 함정</b></summary>

```c
Config *cfg_acquire(ConfigStore *s)
{
    /* ★ 함정: 락 없이 아래처럼 쓰면 안 된다
     *      Config *c = atomic_load(&s->cur);
     *      atomic_fetch_add(&c->refs, 1);       ← 이 사이에 publisher가 교체 +
     *                                             마지막 참조 해제 → use-after-free
     * 이 창을 닫는 방법: (1) 아주 짧은 락 (2) hazard pointer (3) RCU/epoch */
    pthread_mutex_lock(&s->writer_m);
    Config *c = atomic_load_explicit(&s->cur, memory_order_acquire);
    atomic_fetch_add_explicit(&c->refs, 1, memory_order_acquire);
    pthread_mutex_unlock(&s->writer_m);
    return c;                                   /* 실제 사용은 락 밖에서 */
}

static void cfg_drop(ConfigStore *s, Config *c)
{
    if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {
        atomic_thread_fence(memory_order_acquire);   /* 다른 스레드의 접근을 모두 본 뒤 */
        free(c);
    }
}
```
전체 코드: `concurrency_practice/solutions/08_config_swap_refcount.c`
검증: 2000회 교체 × 4 reader, 불변식 위반 0, 해제 누수 0(freed=1999 + 마지막 1). TSan 클린.
</details>

**이 문제의 진짜 채점 포인트**는 코드가 아니라 **"포인터 읽기와 refcount 증가 사이의 창"을 스스로 발견하느냐**다. 발견하고 "그래서 여기만 짧게 잠그거나 RCU를 씁니다"라고 말하면 시니어로 읽힌다.

**Follow-up**
- *"refcount 감소에 왜 release가 필요한가?"* → 내가 객체를 읽은 모든 접근이 감소보다 먼저 보여야, 마지막에 free하는 스레드가 "아직 읽는 중인 메모리"를 해제하지 않는다.
- *"임베디드에서 free를 쓰기 싫다면?"* → 설정 슬롯을 정적 배열로 2~3개 두고 순환 사용(풀). malloc 없는 시스템의 표준 해법이며 단편화도 없다.

---

### 문제 09 · 데드락 — 락 순서와 try-lock 백오프 — ★★★☆☆

**문제**
> 두 자원을 동시에 잠가야 하는 연산이 있다(포트 A→B 전력 예산 이전). 스레드 하나는 A→B, 다른 하나는 B→A 순서로 잠근다. 무슨 일이 일어나고, 어떻게 고치는가?

**답변 뼈대**
1. **데드락 4조건**(상호배제, 점유와 대기, 비선점, 순환 대기) 중 **순환 대기**를 깨는 게 가장 실용적
2. 해법 A — **락 순서 고정**: 전역 순서(주소나 ID)로만 잠근다. 단순하고 리뷰로 강제 가능 → 기본 선택
3. 해법 B — **trylock + 백오프**: 두 번째를 trylock, 실패하면 **첫 락도 놓고** 재시도. 순서를 정할 수 없을 때(콜백, 외부 API). livelock 방지를 위해 랜덤 지수 백오프

<details>
<summary><b>해답 보기 (핵심 코드)</b></summary>

```c
bool transfer_ordered(Port *a, Port *b, long mw)   /* 해법 A */
{
    Port *first  = (a->id < b->id) ? a : b;        /* 호출 방향과 무관하게 동일 순서 */
    Port *second = (a->id < b->id) ? b : a;
    pthread_mutex_lock(&first->m);
    pthread_mutex_lock(&second->m);
    ...
    pthread_mutex_unlock(&second->m);              /* 해제는 역순 */
    pthread_mutex_unlock(&first->m);
}

bool transfer_trylock(Port *a, Port *b, long mw)   /* 해법 B */
{
    for (;;) {
        pthread_mutex_lock(&a->m);
        if (pthread_mutex_trylock(&b->m) == 0) break;
        pthread_mutex_unlock(&a->m);               /* ★ 반드시 첫 락도 놓는다 */
        sched_yield();                             /* 실전: 랜덤 백오프 */
    }
    ...
}
```
전체 코드: `concurrency_practice/solutions/09_lock_order.c`
검증: 반대 방향 5만 회 × 2스레드에서 데드락 없이 총 예산 보존(60000). TSan 클린.
</details>

**임베디드 확장 — 우선순위 역전 (자주 나오는 꼬리질문)**
- 낮은 우선순위 태스크가 락을 쥔 채 선점되고, 중간 우선순위가 CPU를 먹으면, 높은 우선순위가 무기한 막힌다 (**Mars Pathfinder** 사례를 언급하면 좋다)
- 해법: **우선순위 상속(priority inheritance)** mutex — FreeRTOS `xSemaphoreCreateMutex()`는 상속을 지원하지만 **바이너리 세마포어는 지원하지 않는다**. 이 구분을 정확히 말하는 게 포인트
- 더 근본적 해법: 공유 자원을 **한 태스크가 소유**하고 다른 태스크는 메시지 큐로만 요청 → 락 자체를 없앤다(actor 모델). 게이트웨이의 모뎀 제어처럼 직렬 자원에 특히 잘 맞는다

---

### 문제 10 · 모듈화·테스트 가능한 드라이버 설계 — ★★★★☆ (메일에 명시)

**왜 중요한가**: 메일의 4번째 항목이자, 파트 B(시스템 설계)에서 반드시 나올 질문. "하드웨어 없이 이걸 어떻게 테스트하나?"

**문제**
> 공기질 센서(I2C)의 드라이버를 설계하라. 요구: 측정 시작 → 준비될 때까지 폴링 → 데이터 읽기 → CRC 검증. NACK 재시도와 타임아웃을 지원하고, **하드웨어 없이 모든 경로를 테스트할 수 있어야 한다.**

**설계 원칙 4가지 — 이름을 말하면서 설명할 것**
1. **의존성 주입**: 드라이버는 `I2cOps`, `ClockOps` 인터페이스에만 의존. 제품에선 진짜 HAL, 테스트에선 fake를 꽂는다
2. **시간도 의존성이다**: `HAL_GetTick()`/`sleep()`을 직접 부르면 타임아웃 테스트에 실제로 10초를 기다려야 한다. `now_ms()`를 주입하면 **시계를 조종**해 0초에 검증
3. **정책과 I/O 분리**: 재시도·타임아웃·CRC는 순수한 상태머신으로, 버스 접근은 얇은 어댑터로
4. **에러는 값으로 반환**: 드라이버는 스스로 죽거나(assert/panic) 로그만 찍고 삼키지 않는다. 호출자가 정책을 정한다

<details>
<summary><b>해답 보기 (인터페이스 + 테스트)</b></summary>

```c
typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;
} I2cOps;

typedef struct { uint32_t (*now_ms)(void *ctx); void *ctx; } ClockOps;

/* 논블로킹 폴링 API: EAGAIN이면 다시 호출, OK면 값이 나온다 */
SensorErr sensor_poll(Sensor *s, int16_t *out_ppm);
```
테스트에서 시간을 조종하는 모습:
```c
Sensor s; FakeI2c f = { .not_ready_times = 10000 }; FakeClock c = { 0 };
setup(&s, &f, &c);
assert(sensor_start_measure(&s) == SENS_OK);
assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
c.ms = 1000;                                    /* ★ 시계를 1초 앞으로 */
assert(sensor_poll(&s, &ppm) == SENS_ETIMEDOUT);/* 실제 대기 0초 */
```
전체 코드: `concurrency_practice/solutions/10_testable_driver.c`
5개 테스트 케이스: 정상(2회 EAGAIN 후 성공) / 일시적 NACK 3회 후 복구 / 타임아웃 → ERROR 고착 / CRC 불일치 시 값 미반영 + 재측정 가능 / crc8 순수 함수 단독 테스트
</details>

**"어떻게 테스트하나"에 대한 완전한 답변 (3단 구조로 말할 것)**
| 층 | 무엇을 | 어떻게 |
|---|---|---|
| 단위 | 프로토콜 파싱, CRC, 상태 전이, 재시도/타임아웃 정책 | fake HAL + 가짜 시계. 하드웨어 0개, 밀리초 단위, CI에서 매 커밋 |
| 통합 | 실제 I2C 버스, 실제 센서 | HIL 랙에 보드 몇 장. 야간 실행 |
| 시스템 | 온도(-40~50°C), 전원 사이클, 링크 단절 | 챔버 + 네트워크 결함 주입. Don의 SSD 챔버 자동화 경험과 직결 |

**Follow-up**
- *"동시성을 어떻게 테스트하나?"* → ① 불변식 기반 스트레스 테스트(내 코드처럼 produced == consumed + dropped) ② **ThreadSanitizer**를 CI에 ③ 결정적 재현을 위해 스케줄 주입(테스트 훅으로 특정 지점에 yield 삽입) ④ 장시간 soak 테스트
- *"이 추상화가 성능을 해치지 않나?"* → 함수 포인터 간접 호출 1회. I2C 트랜잭션(수백 마이크로초)에 비하면 무시 가능. **비용이 문제가 되는 핫패스에서만** 컴파일 타임 다형성(매크로/템플릿)으로 바꾼다 — 측정 없이 최적화하지 않는다.

---

## 3. 파트 B · 시스템 설계 예상 3제

### 답변 프레임 (모든 설계 질문에 공통)
```
1) 요구사항·제약 확인 (3분)   : 처리량? 지연? 메모리? 전력? 실패 모드? 규모?
2) 인터페이스와 데이터 흐름     : 박스와 화살표 먼저. 스레드는 나중
3) 동시성 모델                 : 스레드 몇 개, 무엇이 공유되고, 무엇으로 보호하나
4) 실패와 복구                 : 링크 단절, 전원 손실, 센서 무응답, 부분 실패
5) 관측 가능성                 : 무엇을 카운트하고 무엇을 로그로 남기나
6) 테스트 전략                 : 단위 / 통합 / 필드
7) trade-off 요약              : "메모리를 쓰고 손실을 줄였습니다" 식으로 명시
```

### 설계 1 — 센서에서 클라우드까지의 파이프라인 (가장 유력)
> "공기질 센서가 1초마다 샘플을 만든다. 이걸 LTE로 클라우드에 올리는 게이트웨이 소프트웨어를 설계하라."

말할 뼈대: 샘플러 스레드(주기 = 문제 07) → 링/큐(01, 05) → 배치 업로더 스레드 → 백오프 재연결. 링크 단절 시 drop 정책(05)과 플래시 spill, 시간 동기화(부팅 직후 RTC가 틀리면 타임스탬프 보정 필요), 관측 카운터(sent/dropped/retries/queue_depth). 스레드 3개면 충분하고, 공유 상태는 큐 하나로 좁힌다.

### 설계 2 — WAN failover manager
> "Ethernet과 LTE 두 경로 중 살아있는 쪽으로 자동 전환하는 매니저를 설계하라."

말할 뼈대: 인터페이스별 모니터 스레드 → 상태 이벤트 큐 → **단일 의사결정 스레드(상태머신)**. 이렇게 하면 라우팅 테이블이라는 공유 자원을 한 스레드만 만지므로 락이 사라진다(문제 09의 actor 모델). 히스테리시스(플래핑 방지: 3회 연속 성공해야 승격), health check는 ping이 아니라 **실제 클라우드 엔드포인트 HTTPS**(캐리어가 ICMP를 막거나 walled garden일 수 있음), 듀얼 SIM 전환 조건과 쿨다운.

### 설계 3 — 200만 대 fleet OTA
> "펌웨어 업데이트를 안전하게 배포하라."

말할 뼈대: A/B 파티션 + 서명 검증 + 부팅 후 헬스체크 통과 전까지 rollback 대기(watchdog이 자동 복구), 단계적 롤아웃(1% → 10% → 100%)과 중단 기준, 셀룰러 데이터 요금 때문에 delta 업데이트와 시간대 분산, 전원 손실 중 중단돼도 벽돌이 안 되는 원자적 스위치. Verkada의 2021년 사고 이력을 고려하면 **서명·보안 이야기를 먼저 꺼내는 것이 유리**하다.

---

## 4. 개념 속사포 Q&A 20

동시성 코딩 중간에 튀어나오는 질문들. 각 답은 **한 문장 + 근거 한 문장** 길이로 연습할 것.

1. **volatile로 스레드 동기화가 되나?** → 안 된다. volatile은 컴파일러 최적화(레지스터 캐싱) 억제일 뿐, 원자성도 메모리 순서도 보장하지 않는다. MMIO 레지스터와 `sig_atomic_t` 플래그용이다. 스레드 간에는 `_Atomic`/`std::atomic`.
2. **mutex vs binary semaphore** → mutex는 소유자가 있다(잠근 스레드만 풀 수 있고, 우선순위 상속이 가능). 세마포어는 소유자가 없어 ISR→태스크 시그널링에 쓴다. FreeRTOS에서 상속이 필요하면 반드시 mutex.
3. **spurious wakeup이란?** → `cond_wait`가 시그널 없이 깨어날 수 있다. 그래서 조건은 언제나 `while`로 재검사한다.
4. **signal vs broadcast** → 조건을 만족시킬 스레드가 하나면 signal(thundering herd 방지), 종료처럼 전원이 상태를 재평가해야 하면 broadcast.
5. **acquire/release가 뭔가?** → release store 이전의 쓰기는, 그 값을 acquire load로 본 스레드에게 모두 보인다. 데이터를 먼저 쓰고 플래그를 나중에 세우는 패턴을 실제로 보장해주는 규칙.
6. **relaxed는 언제?** → 순서가 필요 없는 통계 카운터 같은 것. 인덱스 publish에는 절대 안 된다.
7. **memory barrier와 컴파일러 배리어 차이** → 컴파일러 배리어는 재배치만 막고, CPU 배리어(DMB/DSB)는 실행 순서/가시성까지. atomic의 memory_order는 둘 다 적절히 생성한다.
8. **ISR에서 하면 안 되는 것** → 블로킹(mutex, malloc, printf), 긴 작업. 짧게 플래그/링버퍼에 넣고 나온다. RTOS API는 `...FromISR` 변형만.
9. **priority inversion과 해법** → 3장 문제 09 참고. 우선순위 상속 mutex 또는 자원 소유 태스크화.
10. **데드락 4조건** → 상호배제/점유대기/비선점/순환대기. 실무에선 순환대기를 락 순서로 깬다.
11. **livelock** → 서로 양보만 하다 진전이 없는 상태. trylock 백오프에 랜덤성을 넣어 깬다.
12. **starvation** → 특정 스레드가 계속 밀리는 것. 공정 큐(FIFO), 우선순위 상한, 재시도 상한으로 대응.
13. **race condition vs data race** → data race는 동기화 없이 동시 접근(UB). race condition은 타이밍에 따라 결과가 달라지는 논리 버그 — data race가 없어도 발생한다(예: check-then-act).
14. **TOCTOU** → 검사 시점과 사용 시점 사이에 상태가 바뀌는 것. 검사와 행동을 같은 임계구역 안에서.
15. **false sharing** → 다른 변수가 같은 캐시라인에 있어 코어끼리 라인을 뺏는 현상. 핫한 카운터는 `_Alignas(64)`로 분리.
16. **lock-free vs wait-free** → lock-free는 전체 진행 보장, wait-free는 모든 스레드가 유한 단계 내 완료. 임베디드에서 실시간 보장이 필요하면 wait-free(SPSC 링버퍼가 대표적).
17. **ABA 문제** → CAS가 값만 보므로 A→B→A 변화를 놓친다. 태그된 포인터나 hazard pointer로 대응.
18. **RTOS와 Linux의 차이 (이 팀 맥락)** → 센서/MCU는 FreeRTOS로 결정론적 지연, 게이트웨이는 Linux로 네트워킹 스택과 프로세스 격리. Linux는 기본적으로 실시간이 아니다 → 필요하면 `SCHED_FIFO`, CPU 격리, PREEMPT_RT.
19. **임베디드에서 동적 할당을 피하는 이유** → 단편화로 장시간 구동 후 실패, 할당 시간이 비결정적. 정적 풀이나 부팅 시 1회 할당으로.
20. **동시성 버그를 어떻게 잡나** → ThreadSanitizer/Helgrind, 불변식 assert, 스트레스 + soak, 로그에 스레드 ID·시퀀스, 재현 안 되면 상태를 텔레메트리로 뽑아 통계적으로 추적(Don의 필드 디버깅 경험과 연결).

---

## 5. 말하기 스크립트 (영어)

### 시작 — 요구사항 확인
> "Before I write code, let me make sure I understand the constraints. How many producers and consumers? Is the queue bounded — and if it's full, do we block the producer or drop? And how does shutdown work: do we drain pending items or discard them?"

### 설계 선언
> "I'll protect the state with a single mutex and use two condition variables, not_full and not_empty. One mutex keeps the invariant simple, and two condvars avoid waking producers when only consumers can make progress."

### trade-off 표현
> "There's a trade-off here. Blocking gives us zero loss but back-pressures the sensor thread, which we can't do on a DMA callback. So for the real-time path I'd drop the oldest sample and export a dropped counter, and keep blocking only for audit events."

### edge case 나열 (면접관이 묻기 전에)
> "Let me walk the edge cases: empty pop, full push, push after close, close while threads are blocked, double close, and spurious wakeups. The `while` loops and the broadcast in close cover those."

### 테스트 전략
> "For tests: unit tests on a single thread for the ordering invariant, a stress test with four producers and a small capacity to force the blocking path, an invariant check that produced equals consumed plus dropped, and I'd run it under ThreadSanitizer in CI."

### 모를 때
> "I haven't used that specific API in production. Here's how I'd reason about it — [원리] — and I'd verify against the man page before committing. In my last role, the equivalent problem was [SSD/DMA 경험]."

---

## 6. 3일 압축 플랜

| 언제 | 할 일 | 산출물 |
|---|---|---|
| **D-3 오전** | 문제 01, 05, 06 — 큐 계열. `make run N=01`을 **보지 않고** 처음부터 타이핑 | 45분 내 01을 무오류로 작성 |
| **D-3 오후** | 문제 02, 04 — atomic/memory order. 4장 Q1~Q7을 소리 내어 답하기 | volatile vs atomic을 30초로 설명 |
| **D-2 오전** | 문제 03 (double buffering) — 메일 명시 주제. NBUF=3 확장까지 | tearing 없는 설계를 그림으로 설명 |
| **D-2 오후** | 문제 07, 09 — 타이머와 데드락. 우선순위 역전 스토리 준비 | Mars Pathfinder + FreeRTOS mutex 구분 |
| **D-1 오전** | 문제 08, 10 — refcount 함정, 테스트 가능한 설계 | "하드웨어 없이 어떻게 테스트하나" 3층 답변 |
| **D-1 오후** | 3장 시스템 설계 3제를 화이트보드에 각 15분씩 + 5장 영어 스크립트 낭독 | 설계 프레임 7단계 암기 |
| **당일 아침** | 01을 한 번 더 타이핑, 4장 Q&A 훑기, 역질문 확인 | 손이 기억하는 상태 |

**체크리스트**
- [ ] 01을 45분 안에 백지에서 작성 (2회 이상)
- [ ] 03의 소유권 3상태를 그림으로 설명할 수 있다
- [ ] volatile/atomic/memory order를 각 30초로 답한다
- [ ] 종료 edge case 6개를 막힘 없이 나열한다
- [ ] "하드웨어 없이 테스트" 3층 답변을 말한다
- [ ] 시스템 설계 7단계 프레임으로 3제를 각 15분에 끝낸다
- [ ] 코딩 언어 확인 (C/C++ 중 선택 가능한지 리크루터에게 질문)
- [ ] 화면 공유 환경 점검 (에디터, 폰트 크기, 컴파일 가능한 환경)
- [ ] 역질문 3개 준비 — 컨텍스트 파일 4.7절

---

## 7. 연습 환경 사용법

### 7.1 문제 은행 (verkada_prep/) — 세분화 드릴 ★ 여기서 시작

앤두릴 키트와 같은 형식의 **8세트 · 78문제 · 592체크** 문제 은행. 이 문서의 10문제를
더 잘게 쪼개고, Linux I/O·네트워크 파싱·실전 자료구조까지 넓혔다.

```bash
cd verkada_prep
open index.html                      # 스터디 대시보드 (문제·힌트·해답·노트, 진행률 저장)

make list                            # 세트 목록
make prob N=02_condvar_queues        # 연습 stub 구현 → FAIL을 PASS로
make sol  N=02_condvar_queues        # 모범답안 확인
make tsan N=02_condvar_queues        # ThreadSanitizer
make test                            # 전 세트 회귀
make check                           # 스키마+컴파일+실행 전체 검증
```

| # | 세트 | 문항 | 우선순위 |
|---|---|---|---|
| 01 | 🧵 스레드 & 뮤텍스 기초 | 10 | ④ |
| 02 | 🔐 조건 변수 & 블로킹 큐 | 10 | **① 최우선** |
| 03 | ⚛️ Atomic & Lock-free | 10 | ④ |
| 04 | 🎞️ 실시간 버퍼링 (더블/트리플) | 8 | **②** |
| 05 | 🐧 임베디드 Linux I/O | 10 | ⑥ |
| 06 | 🌐 네트워크 · 프로토콜 파싱 | 10 | ⑤ |
| 07 | 🧩 모듈화 · 테스트 가능한 설계 | 10 | **③** |
| 08 | 📊 실전 자료구조 (이벤트·로그) | 10 | ⑦ |

### 7.2 노트 사이트 (notes_site/) — 읽기용

```bash
open notes_site/index.html      # 기초 노트 11 + 면접 복습 노트 8 = 19편, 약 354분
```
논문 읽듯이 한 편씩 읽는 HTML. 사이드 목차, 읽기 진행바, 읽음 표시, 체크리스트 저장,
검색, 다크모드, `j`/`k` 로 다음·이전 노트. 소스는 마크다운(`concurrency_practice/notes/`,
`verkada_prep/notes/`)이고 `python3 build_notes_site.py` 로 다시 만든다.

### 7.3 통짜 연습 (concurrency_practice/) — 면접 시뮬레이션


```bash
cd verkada_sr_embedded_linux_engineer_connectivity/concurrency_practice

make list           # 10문제 목록
make starters       # solutions에서 연습용 스텁 재생성 (이미 생성돼 있음)
make run  N=01      # starters/01_*.c 빌드+실행 — 내가 구현한 것 채점
make sol  N=01      # 모범답안 실행
make tsan N=01      # ThreadSanitizer로 레이스 검출
make asan N=01      # AddressSanitizer + UBSan
make all-sol        # 10문제 모범답안 회귀 (전부 PASS 확인용)
make all-tsan       # 전체 TSan (04는 의도된 race라 SKIP)
```

- `starters/NN_*.c`: 문제 설명 + API 선언 + 채점용 self-test는 그대로, **구현부만 비어 있다**. 미구현 함수는 링커가 이름을 알려준다.
- `solutions/NN_*.c`: 검증된 모범답안. 전부 `-Wall -Wextra` 경고 0, 실행 PASS, TSan 클린(04 제외).
- 실행 결과 예시: `01 PASS`, `frames=20000 read=13 dropped=19986 torn=0`

---

## 8. 근거와 출처

1. 리크루터 Davis의 인터뷰 안내 메일 (2026-09-15 수신) — 세션 구성 2파트, 복습 주제 4가지, 평가 기준 4가지. **본 문서의 1차 근거** `[확인됨]`
2. JD: Senior Embedded Linux Engineer - Connectivity (Greenhouse 5209588007) — C/C++/Go, FreeRTOS, Embedded Linux, SPI/I2C/UART, 모뎀·SIM, "test driven and data driven" `[확인됨]`
3. Verkada 제품 스펙(GC31-E 듀얼 SIM·PoE++ 60W·-40°C, GW31-E 오프라인 30분 자가 재부팅·BLE 설정, MT81 태양광 트레일러) — 문제의 시나리오를 실제 제품에 맞춤 `[확인됨]` → 컨텍스트 파일 2.2절
4. `don-c-prac-master/0ESE_prep/Verkada_FWE_prep/concurrency_prac/note` — 이전에 준비해둔 Verkada AC42 이벤트 큐 문제(다중 생산자 → 단일 소비자 bounded blocking queue). 문제 01과 동일 유형 `[확인됨]`
5. 일반 임베디드/동시성 면접 빈출 패턴 — 문제 선정과 확률(★)은 1·2·4를 근거로 한 판단 `[추정]`
6. 모든 코드는 이 맥에서 `cc -std=c11 -Wall -Wextra -O1 -pthread`로 컴파일·실행 검증, `-fsanitize=thread`로 레이스 검증 (2026-09-15) `[확인됨]`
