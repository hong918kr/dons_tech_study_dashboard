/* DS-A01 Lock-free SPSC 큐: C11 atomics
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (생산자 코어 / 소비자 코어, 또는 ISR / main).
 *   Part A  SPSC 링: 2의 거듭제곱 용량, 자유 증가(free-running) head/tail, 빈 칸 낭비 없음,
 *           생산자 = relaxed(자기 tail) + acquire(head) → 데이터 쓰기 → release(tail), 소비자는 거울.
 *   Part B  상대 인덱스 캐싱(cached opposite index) 버전 + 경계(UINT32_MAX 랩, 용량 1, 가득 참) 검사.
 *   Part C  UART RX: ISR 생산자(가득 차면 overrun 세고 버림) → main 소비자 bulk 읽기(acquire 1번, release 1번).
 *
 * 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 검사한다: 순서(seq 가 정확히 0,1,2,...),
 * 무손실·무중복(개수 == N, seq 연속), 내용 무결(payload == mix(seq)), 체크섬 일치, overrun 정확한 개수.
 * 각 Part 의 2스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행된다 (시작 코드가 멈추지 않게).
 * 스트레스 대기는 시간 제한(5 s)이 있어서 잘못된 구현도 FAIL 로 끝난다.
 * 메시지 수: 기본 1e7, ASan/TSan 빌드는 1e6 (자동), 환경변수 SPSC_N 으로 덮어쓰기 가능.
 * relaxed 버그 재현·처리량(수억 개)은 demo_relaxed.c / demo_bench.c.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "atomic_uint must always be lock-free");

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define SAN_BUILD 1
#  endif
#endif
#ifndef SAN_BUILD
#  define SAN_BUILD 0
#endif

#define CACHELINE 128u    /* Apple M 시리즈: sysctl hw.cachelinesize = 128 (Cortex-M7/A53 등은 32/64) */

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

/* 메시지: seq + payload(= mix(seq)) — 순서·손실·중복·찢어짐(torn)·옛값(stale)을 한 번에 검사 */
typedef struct { uint64_t seq; uint64_t payload; } msg_t;

static inline uint64_t mix(uint64_t x) {          /* splitmix64 finalizer */
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* ======================= Part A — SPSC 링 ======================= */
/* tail = 다음에 쓸 위치(생산자만 store), head = 다음에 읽을 위치(소비자만 store).
 * 둘 다 0 부터 무한히 증가하고(unsigned 랩은 정의된 동작), 칸 번호는 idx & mask.
 *   empty: tail - head == 0      full: tail - head == cap      (빈 칸 낭비 없음)
 * 각 줄(line)은 한 스레드만 쓴다 → 다른 쪽의 쓰기 때문에 내 줄이 무효화되지 않는다(false sharing 없음). */
typedef struct {
    _Alignas(CACHELINE) atomic_uint tail;   /* 생산자 줄 */
    uint32_t head_cache;                    /*   생산자 전용: 마지막으로 읽은 head (Part B) */
    _Alignas(CACHELINE) atomic_uint head;   /* 소비자 줄 */
    uint32_t tail_cache;                    /*   소비자 전용: 마지막으로 읽은 tail (Part B) */
    _Alignas(CACHELINE) uint32_t mask;      /* 읽기 전용 줄: init 후 안 바뀜 */
    msg_t *buf;
} spsc_t;
_Static_assert(offsetof(spsc_t, head) - offsetof(spsc_t, tail) >= CACHELINE, "head/tail must not share a line");
_Static_assert(_Alignof(spsc_t) == CACHELINE, "queue must start on a line");

/* cap 이 2의 거듭제곱이고 1 ≤ cap ≤ 2^31 이면 head = tail = start 로 초기화하고 true. 아니면 false.
 * (start 는 테스트용: UINT32_MAX 근처에서 시작해 랩어라운드를 바로 밟는다. 보통은 0) */
static bool spsc_init_at(spsc_t *q, msg_t *buf, uint32_t cap, uint32_t start) {
    (void)q; (void)buf; (void)cap; (void)start;
    return false; /* TODO */
}
static bool spsc_init(spsc_t *q, msg_t *buf, uint32_t cap) { return spsc_init_at(q, buf, cap, 0u); }

/* 생산자: 가득 차 있으면 false. 아니면 *m 을 칸에 복사하고 게시(publish), true. */
static bool spsc_push(spsc_t *q, const msg_t *m) {
    (void)q; (void)m;
    return false; /* TODO: relaxed(tail) → acquire(head) → full? → 칸 쓰기 → release(tail) */
}

/* 소비자: 비어 있으면 false. 아니면 칸을 *out 으로 복사하고 칸을 돌려줌, true. */
static bool spsc_pop(spsc_t *q, msg_t *out) {
    (void)q; (void)out;
    return false; /* TODO: relaxed(head) → acquire(tail) → empty? → 칸 읽기 → release(head) */
}

/* 소비자 쪽에서 본 현재 개수 (0..cap). */
static uint32_t spsc_size(spsc_t *q) {
    (void)q;
    return 0; /* TODO */
}

/* ======================= Part B — 상대 인덱스 캐싱 ======================= */
/* 생산자는 head 를 매번 읽지 않고 head_cache 를 믿는다. head_cache ≤ 실제 head 이므로 캐시로 "여유 있음"
 * 이면 진짜로 여유가 있다. 캐시로 "가득" 일 때만 acquire 로 head 를 다시 읽는다. 소비자는 거울. */
static bool spsc_push_cached(spsc_t *q, const msg_t *m) {
    (void)q; (void)m;
    return false; /* TODO: head_cache 로 먼저 판단, "가득" 일 때만 head 를 acquire 로 다시 읽기 */
}

static bool spsc_pop_cached(spsc_t *q, msg_t *out) {
    (void)q; (void)out;
    return false; /* TODO: tail_cache 로 먼저 판단, "비었음" 일 때만 tail 을 acquire 로 다시 읽기 */
}

/* ======================= Part C — UART RX: ISR → main ======================= */
#define UART_RB_SIZE 256u   /* 2의 거듭제곱 */
typedef struct {
    _Alignas(CACHELINE) atomic_uint wr;     /* ISR 만 store */
    atomic_uint overrun;                    /* ISR 만 증가, main 은 통계로 읽기 (relaxed) */
    _Alignas(CACHELINE) atomic_uint rd;     /* main 만 store */
    _Alignas(CACHELINE) uint8_t buf[UART_RB_SIZE];
} uart_rb_t;

static void uart_rb_init_at(uart_rb_t *r, uint32_t start) {
    atomic_init(&r->wr, start);
    atomic_init(&r->rd, start);
    atomic_init(&r->overrun, 0u);
}

/* RX ISR: 한 바이트 넣기. 가득 차면 버리고(새 바이트를 버린다 — 오래된 것은 main 소유) overrun++ 후 false.
 * ISR 은 절대 기다리지 않는다. */
static bool uart_rx_isr(uart_rb_t *r, uint8_t byte) {
    (void)r; (void)byte;
    return false; /* TODO */
}

/* main: 최대 max 바이트를 dst 로 복사, 읽은 개수 반환. acquire 1번 → memcpy 최대 2조각 → release 1번. */
static size_t uart_read(uart_rb_t *r, uint8_t *dst, size_t max) {
    (void)r; (void)dst; (void)max;
    return 0; /* TODO */
}

/* ======================= 테스트 하네스 (수정하지 않음) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define FAILS() (g_total - g_pass)

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#define WAIT_LIMIT_S 5.0   /* 한 메시지를 5 s 동안 못 넣거나/못 빼면 포기 → FAIL (멈추지 않음) */

static uint64_t g_n;                         /* 스트레스 메시지 수 */
static atomic_int g_abort;                   /* 시간 초과 시 양쪽 모두 중단 */

typedef bool (*push_fn)(spsc_t *, const msg_t *);
typedef bool (*pop_fn)(spsc_t *, msg_t *);
typedef struct {
    spsc_t *q; push_fn push; pop_fn pop;
    uint64_t sent_sum, recv_sum, received, order_err, torn_err;
    bool timeout;
} stress_t;

static bool keep_waiting(uint32_t *spins, double *t0) {
    /* 한 번 쉬고, 4096 번마다 시간 확인. 계속 기다려도 되면 true, 포기하면 false */
    if ((++*spins & 4095u) == 0u) {
        if (*t0 == 0.0) *t0 = now_s();
        else if (now_s() - *t0 > WAIT_LIMIT_S) return false;
    }
    if (atomic_load_explicit(&g_abort, memory_order_relaxed)) return false;
    cpu_relax();
    return true;
}

static void *producer_thread(void *arg) {
    stress_t *s = arg;
    for (uint64_t i = 0; i < g_n; i++) {
        msg_t m = { i, mix(i) };
        uint32_t spins = 0; double t0 = 0.0; bool ok;
        while (!(ok = s->push(s->q, &m)) && keep_waiting(&spins, &t0)) { }
        if (!ok) { s->timeout = true; atomic_store(&g_abort, 1); return NULL; }
        s->sent_sum += m.payload;
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    stress_t *s = arg;
    for (uint64_t expect = 0; expect < g_n; expect++) {
        msg_t m;
        uint32_t spins = 0; double t0 = 0.0; bool ok;
        while (!(ok = s->pop(s->q, &m)) && keep_waiting(&spins, &t0)) { }
        if (!ok) { s->timeout = true; atomic_store(&g_abort, 1); return NULL; }
        if (m.seq != expect) s->order_err++;          /* 손실·중복·재배치·옛값 */
        if (m.payload != mix(m.seq)) s->torn_err++;  /* 찢어진 메시지 */
        s->recv_sum += m.payload;
        s->received++;
    }
    return NULL;
}

static void run_stress(stress_t *s) {
    pthread_t p, c;
    atomic_store(&g_abort, 0);
    pthread_create(&c, NULL, consumer_thread, s);
    pthread_create(&p, NULL, producer_thread, s);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
}

static void check_stress(const char *name, spsc_t *q, push_fn pu, pop_fn po) {
    stress_t s = { .q = q, .push = pu, .pop = po };
    double t0 = now_s();
    run_stress(&s);
    double dt = now_s() - t0;
    printf("  %-28s %llu msgs  %.2f s  (%.1f Mops/s, 정보용)\n", name,
           (unsigned long long)s.received, dt, (double)s.received / dt / 1e6);
    CHECK(!s.timeout);
    CHECK(s.received == g_n);                       /* 무손실 */
    CHECK(s.order_err == 0 && s.torn_err == 0);     /* 순서·무중복·무결 */
    CHECK(s.recv_sum == s.sent_sum);                /* 체크섬 */
}

static msg_t g_buf_big[1024], g_buf_small[2];
static spsc_t g_q, g_q2;

/* UART 스트레스: "ISR" 스레드가 패턴 바이트를 보내고, 가득 차서 버려지면(false) 같은 바이트를 다시 시도한다
 * (테스트용 — 실제 ISR 은 버리고 끝). 그래서 main 이 받은 스트림은 정확히 0..N-1 의 패턴이어야 하고,
 * overrun 카운터 == ISR 쪽이 본 false 횟수 여야 한다. */
static uart_rb_t g_uart;
static uint64_t g_uart_n;
static uint64_t g_isr_drops;
static bool g_isr_timeout;
static inline uint8_t pat(uint64_t i) { return (uint8_t)(i * 7u + (i >> 8) + 3u); }

static void *uart_isr_thread(void *arg) {
    (void)arg;
    for (uint64_t i = 0; i < g_uart_n; i++) {
        uint32_t spins = 0; double t0 = 0.0; bool ok;
        while (!(ok = uart_rx_isr(&g_uart, pat(i)))) {
            g_isr_drops++;
            if (!keep_waiting(&spins, &t0)) { g_isr_timeout = true; atomic_store(&g_abort, 1); return NULL; }
        }
    }
    return NULL;
}

int main(void) {
    g_n = SAN_BUILD ? 1000000u : 10000000u;
    const char *env = getenv("SPSC_N");
    if (env && *env) g_n = strtoull(env, NULL, 10);
    printf("DS-A01 SPSC ring  (stress N = %llu%s)\n", (unsigned long long)g_n, SAN_BUILD ? ", sanitizer build" : "");

    /* ---------- Part A: 단위 테스트 ---------- */
    printf("Part A — SPSC ring\n");
    int fa = FAILS();
    msg_t b4[4], b3[3], out = {0, 0};
    static spsc_t q;                                  /* 0 으로 시작 (static) */
    CHECK(!spsc_init(&q, b3, 3u));                  /* 2의 거듭제곱 아님 */
    CHECK(!spsc_init(&q, b4, 0u));
    CHECK(spsc_init(&q, b4, 4u));
    CHECK(!spsc_pop(&q, &out) && spsc_size(&q) == 0u);              /* 빈 큐 */
    bool all4 = true;
    for (uint64_t i = 0; i < 4; i++) { msg_t m = { i, mix(i) }; all4 = all4 && spsc_push(&q, &m); }
    CHECK(all4 && spsc_size(&q) == 4u);                             /* 4칸 전부 사용 (낭비 없음) */
    { msg_t m = { 99, mix(99) }; CHECK(!spsc_push(&q, &m)); }       /* full */
    CHECK(spsc_pop(&q, &out) && out.seq == 0u && out.payload == mix(0));
    { msg_t m = { 4, mix(4) }; CHECK(spsc_push(&q, &m)); }          /* 한 칸 비면 다시 들어감 */
    bool ord = true;
    for (uint64_t i = 1; i <= 4; i++) ord = ord && spsc_pop(&q, &out) && out.seq == i;
    CHECK(ord && !spsc_pop(&q, &out));                              /* FIFO, 끝나면 empty */
    CHECK(atomic_load(&q.tail) == 5u && atomic_load(&q.head) == 5u);  /* 인덱스는 자유 증가 (mask 안 함) */
    if (FAILS() == fa) {
        CHECK(spsc_init(&g_q, g_buf_big, 1024u));
        check_stress("A: push/pop cap=1024", &g_q, spsc_push, spsc_pop);
    } else {
        printf("SKIP 5 stress checks (Part A): unit checks failed\n");
        g_total += 5;
    }

    /* ---------- Part B: 캐싱 + 경계 ---------- */
    printf("Part B — cached index + edges\n");
    int fb = FAILS();
    CHECK(spsc_init_at(&q, b4, 4u, 0xFFFFFFFEu));   /* 2개 넣으면 tail 이 0 으로 넘어감 */
    bool wr = true;
    for (uint64_t i = 0; i < 4; i++) { msg_t m = { i, mix(i) }; wr = wr && spsc_push_cached(&q, &m); }
    CHECK(wr && atomic_load(&q.tail) == 2u);        /* tail 랩 */
    CHECK(spsc_size(&q) == 4u);                     /* tail(2) - head(0xFFFFFFFE) == 4 */
    { msg_t m = { 9, mix(9) }; CHECK(!spsc_push_cached(&q, &m)); }   /* 진짜 가득: 새로 읽어도 가득 */
    CHECK(spsc_pop_cached(&q, &out) && out.seq == 0u);
    { msg_t m = { 4, mix(4) }; CHECK(spsc_push_cached(&q, &m)); }    /* 캐시는 가득이라 하지만 다시 읽으면 여유 */
    CHECK(q.head_cache == 0xFFFFFFFFu);             /* 새로 읽은 값으로 캐시 갱신됨 */
    bool ordb = true;
    for (uint64_t i = 1; i <= 4; i++) ordb = ordb && spsc_pop_cached(&q, &out) && out.seq == i && out.payload == mix(i);
    CHECK(ordb && !spsc_pop_cached(&q, &out));
    CHECK(atomic_load(&q.head) == 3u && q.tail_cache == 3u);          /* head 도 랩 */
    msg_t b1[1];
    CHECK(spsc_init_at(&q, b1, 1u, 0xFFFFFFFFu));   /* 용량 1 (mask = 0) */
    { msg_t m = { 7, mix(7) }; CHECK(spsc_push(&q, &m)); CHECK(!spsc_push_cached(&q, &m)); }
    CHECK(spsc_pop_cached(&q, &out) && out.seq == 7u && !spsc_pop(&q, &out));
    if (FAILS() == fb) {
        CHECK(spsc_init(&g_q, g_buf_big, 1024u));
        check_stress("B: cached/cached cap=1024", &g_q, spsc_push_cached, spsc_pop_cached);
        CHECK(spsc_init_at(&g_q2, g_buf_small, 2u, 0xFFFFFF00u));
        uint64_t keep = g_n;
        g_n = keep / 10u;                           /* 용량 2: 매번 full/empty 경계 + 랩 */
        check_stress("B: cached/plain cap=2 wrap", &g_q2, spsc_push_cached, spsc_pop);
        g_n = keep;
    } else {
        printf("SKIP 10 stress checks (Part B): unit checks failed\n");
        g_total += 10;
    }

    /* ---------- Part C: UART RX ISR → main ---------- */
    printf("Part C — UART RX ring\n");
    int fc = FAILS();
    uint8_t rx[512];
    uart_rb_init_at(&g_uart, 0u);
    CHECK(uart_read(&g_uart, rx, sizeof rx) == 0u);
    unsigned stored = 0;
    for (unsigned i = 0; i < 300u; i++) stored += uart_rx_isr(&g_uart, (uint8_t)i) ? 1u : 0u;
    CHECK(stored == 256u && atomic_load(&g_uart.overrun) == 44u);   /* main 이 안 읽으면 정확히 44개 버림 */
    size_t n1 = uart_read(&g_uart, rx, 100);
    size_t n2 = uart_read(&g_uart, rx + 100, sizeof rx - 100);
    bool okc = (n1 == 100u && n2 == 156u);
    for (unsigned i = 0; okc && i < 256u; i++) okc = (rx[i] == (uint8_t)i);   /* 오래된 256 개가 순서대로 */
    CHECK(okc);
    uart_rb_init_at(&g_uart, 0xFFFFFFF0u);          /* 인덱스 랩 + 버퍼 끝에서 두 조각 */
    for (unsigned i = 0; i < 200u; i++) (void)uart_rx_isr(&g_uart, (uint8_t)(i + 1u));
    size_t k = uart_read(&g_uart, rx, 150);
    for (unsigned i = 0; i < 100u; i++) (void)uart_rx_isr(&g_uart, (uint8_t)(i + 201u));
    k += uart_read(&g_uart, rx + k, sizeof rx - k);
    bool okw = (k == 300u) && atomic_load(&g_uart.overrun) == 0u && atomic_load(&g_uart.rd) == 0xFFFFFFF0u + 300u;
    for (unsigned i = 0; okw && i < 300u; i++) okw = (rx[i] == (uint8_t)(i + 1u));
    CHECK(okw);
    if (FAILS() == fc) {
        uart_rb_init_at(&g_uart, 0u);
        g_uart_n = g_n / 4u;
        atomic_store(&g_abort, 0);
        pthread_t isr;
        pthread_create(&isr, NULL, uart_isr_thread, NULL);
        uint64_t got = 0, bad = 0;
        uint32_t spins = 0; double t0 = 0.0;
        while (got < g_uart_n) {
            size_t n = uart_read(&g_uart, rx, 64);   /* main 루프: 한 번에 최대 64 바이트 */
            for (size_t i = 0; i < n; i++, got++) bad += (rx[i] != pat(got));
            if (n == 0 && !keep_waiting(&spins, &t0)) break;
            if (n) { spins = 0; t0 = 0.0; }
        }
        pthread_join(isr, NULL);
        printf("  C: uart stream %llu bytes, ISR drops %llu (timing-dependent, 정보용)\n",
               (unsigned long long)got, (unsigned long long)g_isr_drops);
        CHECK(!g_isr_timeout && got == g_uart_n && bad == 0u);       /* 재전송 포함 바이트 스트림 정확 */
        CHECK(atomic_load(&g_uart.overrun) == (unsigned)g_isr_drops);  /* overrun == ISR 이 본 false 횟수 */
    } else {
        printf("SKIP 2 stress checks (Part C): unit checks failed\n");
        g_total += 2;
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
