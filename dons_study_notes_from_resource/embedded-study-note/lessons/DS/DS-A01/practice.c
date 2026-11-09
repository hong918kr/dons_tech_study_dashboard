/* DS-A01 추가 연습 — 한 번에 검증
 *
 *   연습 1  제3 스레드(모니터)가 읽는 size: 읽는 순서와 clamp, 그리고 그 순서를 지키는 acquire
 *   연습 2  생산자가 둘이면? 강제 스케줄로 (a) 메시지 손실, (b) fetch_add 예약 후 "아직 안 쓴 칸" 읽기 재현
 *   연습 3  단일 코어 ISR↔main: relaxed + atomic_signal_fence, 진짜 시그널 핸들러(raise)로 선점 지점 강제
 *   연습 4  zero-copy reserve/commit API (큰 메시지를 칸 안에서 바로 쓰고 읽기) + 2스레드 스트레스
 *
 * 연습 2 의 CHECK 는 "깨진다" 는 것을 확인한다 (강제 스케줄이라 매번 똑같이 깨진다).
 * 메시지 수: 기본 2e6, ASan/TSan 빌드는 2e5, 환경변수 SPSC_N 으로 덮어쓰기 가능.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define SAN_BUILD 1
#  endif
#endif
#ifndef SAN_BUILD
#  define SAN_BUILD 0
#endif
#define CACHELINE 128u

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#endif
}

/* 본문과 같은 링 (uint32_t 값) */
typedef struct {
    _Alignas(CACHELINE) atomic_uint tail;
    _Alignas(CACHELINE) atomic_uint head;
    _Alignas(CACHELINE) uint32_t mask;
    uint32_t *buf;
} ring_t;

static void ring_init(ring_t *q, uint32_t *buf, uint32_t cap) {
    q->mask = cap - 1u; q->buf = buf;
    atomic_init(&q->tail, 0u); atomic_init(&q->head, 0u);
}
static bool ring_push(ring_t *q, uint32_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (t - h == q->mask + 1u) return false;
    q->buf[t & q->mask] = v;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return true;
}
static bool ring_pop(ring_t *q, uint32_t *v) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (t == h) return false;
    *v = q->buf[h & q->mask];
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
    return true;
}

/* ======================= 연습 1 — 모니터 스레드의 size ======================= */
/* hook 은 두 load 사이에 다른 스레드가 일한 것을 흉내 낸다 (강제 스케줄). */
typedef void (*hook_fn)(ring_t *);

/* 잘못된 순서: tail 먼저, head 나중 → 그 사이 소비가 일어나면 head > tail → 음수 → 거대한 unsigned */
static uint32_t size_tail_first(ring_t *q, hook_fn between) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (between) between(q);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    return t - h;
}
/* 정답: head 먼저(acquire — 뒤의 tail load 가 앞으로 올라오지 못하게), tail 나중, cap 으로 clamp.
 * head 를 먼저 읽으면 t ≥ h 가 항상 성립(tail 은 head 를 앞지르지 않음)하지만, 그 사이 생산이
 * 계속되면 t - h 가 cap 을 넘을 수 있다 → clamp. 결과는 항상 0..cap. */
static uint32_t size_monitor(ring_t *q, hook_fn between) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (between) between(q);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    uint32_t n = t - h;
    return n > q->mask + 1u ? q->mask + 1u : n;
}
static void hook_drain_then_fill(ring_t *q) {     /* 소비자가 전부 빼고, 생산자가 다시 가득 채움 */
    uint32_t v;
    while (ring_pop(q, &v)) { }
    for (uint32_t i = 0; i < q->mask + 1u; i++) (void)ring_push(q, 100u + i);
}
static void hook_drain(ring_t *q) { uint32_t v; while (ring_pop(q, &v)) { } }
static void hook_push2_drain(ring_t *q) {         /* 생산자가 2개 더 넣고, 소비자가 전부 뺌 */
    (void)ring_push(q, 7u); (void)ring_push(q, 8u);
    hook_drain(q);
}

/* ======================= 연습 2 — 생산자 둘 (강제 스케줄) ======================= */
/* SPSC push 를 두 단계로 쪼갠다: begin = tail 읽고 칸 쓰기, commit = tail+1 게시 */
static uint32_t push_begin(ring_t *q, uint32_t v) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    q->buf[t & q->mask] = v;
    return t;
}
static void push_commit(ring_t *q, uint32_t t) {
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
}
/* (a) P1.begin(A) → P2.begin(B) → P1.commit → P2.commit : 둘 다 t=0 을 봤다. 받은 개수를 반환. */
static unsigned two_producers_naive(uint32_t *got_first) {
    static uint32_t buf[4]; static ring_t q;
    ring_init(&q, buf, 4u);
    uint32_t t1 = push_begin(&q, 0xA);
    uint32_t t2 = push_begin(&q, 0xB);           /* 같은 칸 0 을 덮어씀 */
    push_commit(&q, t1);
    push_commit(&q, t2);                          /* tail = 1 (2 가 아님) */
    unsigned n = 0; uint32_t v;
    while (ring_pop(&q, &v)) { if (n == 0) *got_first = v; n++; }
    return n;
}
/* (b) "그럼 tail 을 fetch_add 로 예약하면?" — P1 이 칸 0 을, P2 가 칸 1 을 예약. P2 는 칸 1 을 다 썼지만
 * P1 은 아직 칸 0 을 쓰는 중(선점됨). tail 은 이미 2 → 소비자는 칸 0 을 "준비됨" 으로 보고 옛 값을 읽는다. */
static uint32_t two_producers_faa_reads_unwritten(void) {
    static uint32_t buf[4]; static ring_t q;
    ring_init(&q, buf, 4u);
    buf[0] = 0xDEADu;                             /* 지난 바퀴에 남은 옛 내용 */
    uint32_t t1 = atomic_fetch_add_explicit(&q.tail, 1u, memory_order_acq_rel);   /* P1 예약: 0 */
    uint32_t t2 = atomic_fetch_add_explicit(&q.tail, 1u, memory_order_acq_rel);   /* P2 예약: 1 */
    q.buf[t2 & q.mask] = 0xB;                     /* P2 는 다 씀 */
    uint32_t v = 0;
    (void)ring_pop(&q, &v);                       /* 소비자: tail(2) != head(0) → 칸 0 을 읽음 */
    q.buf[t1 & q.mask] = 0xA;                     /* P1 이 이제서야 씀 — 늦었다 */
    return v;
}

/* ======================= 연습 3 — 단일 코어 ISR: signal fence ======================= */
/* 같은 스레드의 시그널 핸들러 = 같은 코어의 ISR 모델. CPU 순서는 한 코어 안에서 지켜지므로
 * 컴파일러만 막으면 된다 → relaxed + atomic_signal_fence (C11 7.17.4.2). 하드웨어 배리어 없음. */
#define ISR_CAP 16u
static struct {
    atomic_uint tail, head;
    uint8_t buf[ISR_CAP];
} g_isr;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "lock-free atomics are signal-safe (C11 7.14.1.1p5)");

static bool isr_push(uint8_t b) {                 /* "ISR" (시그널 핸들러) 안에서만 호출 */
    uint32_t t = atomic_load_explicit(&g_isr.tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&g_isr.head, memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);    /* 칸 쓰기가 head 읽기보다 먼저 가지 않게 */
    if (t - h == ISR_CAP) return false;
    g_isr.buf[t % ISR_CAP] = b;
    atomic_signal_fence(memory_order_release);    /* 칸 쓰기가 tail 게시보다 늦게 가지 않게 */
    atomic_store_explicit(&g_isr.tail, t + 1u, memory_order_relaxed);
    return true;
}
static volatile sig_atomic_t g_fire_in_pop;       /* 1 이면 pop 의 가장 위험한 지점에서 "인터럽트" 발생 */
static bool main_pop(uint8_t *out) {
    uint32_t h = atomic_load_explicit(&g_isr.head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&g_isr.tail, memory_order_relaxed);
    atomic_signal_fence(memory_order_acquire);    /* 칸 읽기가 tail 읽기보다 먼저 가지 않게 */
    if (t == h) return false;
    if (g_fire_in_pop) { g_fire_in_pop = 0; raise(SIGUSR1); }   /* 테스트 훅: 읽기 직전 선점 */
    *out = g_isr.buf[h % ISR_CAP];
    atomic_signal_fence(memory_order_release);    /* 칸 읽기가 head 반납보다 늦게 가지 않게 */
    atomic_store_explicit(&g_isr.head, h + 1u, memory_order_relaxed);
    return true;
}
static volatile sig_atomic_t g_isr_next;          /* ISR 이 보낼 다음 바이트 값 */
static volatile sig_atomic_t g_isr_drop;
static volatile sig_atomic_t g_isr_calls;
static void uart_isr_handler(int sig) {           /* 한 번 "인터럽트" 에 3 바이트 수신 시도 */
    (void)sig;
    g_isr_calls = g_isr_calls + 1;
    for (int i = 0; i < 3; i++) {
        if (isr_push((uint8_t)g_isr_next)) g_isr_next = (uint8_t)(g_isr_next + 1);
        else g_isr_drop = g_isr_drop + 1;
    }
}

/* ======================= 연습 4 — zero-copy reserve/commit ======================= */
typedef struct { uint64_t seq; uint8_t body[240]; uint64_t sum; } big_t;   /* 256 B */
typedef struct {
    _Alignas(CACHELINE) atomic_uint tail;
    _Alignas(CACHELINE) atomic_uint head;
    _Alignas(CACHELINE) big_t slot[8];
} zring_t;
static zring_t g_z;

/* 생산자: 빈 칸이 있으면 그 칸의 주소 (아직 게시 안 됨), 없으면 NULL */
static big_t *z_write_slot(zring_t *q) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    return (t - h == 8u) ? NULL : &q->slot[t & 7u];
}
static void z_commit_write(zring_t *q) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
}
/* 소비자: 준비된 칸이 있으면 그 칸의 주소 (반납 전까지 소비자 소유), 없으면 NULL */
static const big_t *z_read_slot(zring_t *q) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    return (t == h) ? NULL : &q->slot[h & 7u];
}
static void z_commit_read(zring_t *q) {
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    atomic_store_explicit(&q->head, h + 1u, memory_order_release);
}

static uint64_t g_zn;
static void fill_big(big_t *m, uint64_t seq) {
    m->seq = seq;
    uint64_t s = 0;
    for (int i = 0; i < 240; i++) { m->body[i] = (uint8_t)(seq * 31u + (uint64_t)i); s += m->body[i]; }
    m->sum = s ^ seq;
}
static bool big_ok(const big_t *m) {
    uint64_t s = 0;
    for (int i = 0; i < 240; i++) s += m->body[i];
    return (s ^ m->seq) == m->sum;
}
static void *z_producer(void *arg) {
    (void)arg;
    for (uint64_t i = 0; i < g_zn; i++) {
        big_t *w;
        while ((w = z_write_slot(&g_z)) == NULL) cpu_relax();
        fill_big(w, i);                            /* 칸 안에서 바로 쓴다 (복사 0번) */
        z_commit_write(&g_z);
    }
    return NULL;
}

int main(void) {
    uint64_t n = SAN_BUILD ? 200000u : 2000000u;
    const char *env = getenv("SPSC_N");
    if (env && *env) n = strtoull(env, NULL, 10);

    /* 연습 1 */
    static uint32_t b4[4]; static ring_t q;
    ring_init(&q, b4, 4u);
    for (uint32_t i = 0; i < 2; i++) (void)ring_push(&q, i);
    CHECK(size_tail_first(&q, NULL) == 2u && size_monitor(&q, NULL) == 2u);   /* 조용할 때는 같다 */
    CHECK(size_tail_first(&q, hook_push2_drain) == 0xFFFFFFFEu);              /* 2 - 4 = -2 → 거대값 */
    ring_init(&q, b4, 4u);
    for (uint32_t i = 0; i < 3; i++) (void)ring_push(&q, i);
    uint32_t raw_h = atomic_load(&q.head);
    hook_drain_then_fill(&q);
    CHECK(atomic_load(&q.tail) - raw_h == 7u);                                /* 날것 차이는 cap(4)을 넘음 */
    ring_init(&q, b4, 4u);
    for (uint32_t i = 0; i < 3; i++) (void)ring_push(&q, i);
    CHECK(size_monitor(&q, hook_drain_then_fill) == 4u);                      /* clamp → 0..cap 유지 */
    ring_init(&q, b4, 4u);
    for (uint32_t i = 0; i < 3; i++) (void)ring_push(&q, i);
    CHECK(size_monitor(&q, hook_drain) == 3u);   /* head 를 먼저 읽었으니 "읽은 순간 이후의 소비" 는 안 보인다 */

    /* 연습 2 */
    uint32_t first = 0;
    CHECK(two_producers_naive(&first) == 1u && first == 0xBu);   /* 2개 보냈는데 1개, A 는 사라짐 */
    CHECK(two_producers_faa_reads_unwritten() == 0xDEADu);       /* 아직 안 쓴 칸의 옛 값을 읽음 */

    /* 연습 3 */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = uart_isr_handler;
    sigemptyset(&sa.sa_mask);
    CHECK(sigaction(SIGUSR1, &sa, NULL) == 0);
    unsigned got = 0, bad = 0; uint8_t b;
    for (int round = 0; round < 2000; round++) {
        raise(SIGUSR1);                                           /* main 루프 중간의 인터럽트 */
        if (round % 7 == 3) g_fire_in_pop = 1;                    /* 가끔은 pop 의 한가운데서 */
        int k = (round % 5 == 0) ? 0 : 2;                         /* 가끔 main 이 늦어서 쌓임 */
        for (int i = 0; i < k && main_pop(&b); i++) { bad += (b != (uint8_t)got); got++; }
        while (round % 13 == 0 && main_pop(&b)) { bad += (b != (uint8_t)got); got++; }
    }
    g_fire_in_pop = 0;
    while (main_pop(&b)) { bad += (b != (uint8_t)got); got++; }
    printf("  연습 3: %d interrupts, received %u bytes, ISR drops %d\n", (int)g_isr_calls, got, (int)g_isr_drop);
    CHECK(bad == 0u && (uint8_t)got == (uint8_t)g_isr_next);    /* 넣은 그대로, 끊김 없는 순서 */
    CHECK(got + (unsigned)g_isr_drop == 3u * (unsigned)g_isr_calls && g_isr_calls > 2000);  /* pop 중간 선점도 있었음 */

    /* 연습 4 */
    atomic_init(&g_z.tail, 0u); atomic_init(&g_z.head, 0u);
    big_t *w = z_write_slot(&g_z);
    CHECK(w != NULL && z_read_slot(&g_z) == NULL);                /* 쓰는 중인 칸은 아직 안 보임 */
    fill_big(w, 42);
    z_commit_write(&g_z);
    const big_t *r = z_read_slot(&g_z);
    CHECK(r == w && r->seq == 42u && big_ok(r));                  /* 같은 주소 — 복사 없음 */
    z_commit_read(&g_z);
    CHECK(z_read_slot(&g_z) == NULL);
    g_zn = n;
    pthread_t p;
    pthread_create(&p, NULL, z_producer, NULL);
    uint64_t zbad = 0;
    for (uint64_t e = 0; e < g_zn; e++) {
        const big_t *m;
        while ((m = z_read_slot(&g_z)) == NULL) cpu_relax();
        zbad += (m->seq != e) || !big_ok(m);                      /* 칸 안에서 바로 읽는다 */
        z_commit_read(&g_z);
    }
    pthread_join(p, NULL);
    printf("  연습 4: %llu big messages\n", (unsigned long long)g_zn);
    CHECK(zbad == 0u && atomic_load(&g_z.tail) == (uint32_t)(g_zn + 1u));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
