/* DS-A03 demo_broken.c — 깨진 MPMC 세 가지를 보여 준다 (관찰용, CHECK 아님)
 *
 *   naive   : head/tail 두 카운터만 있는 MPMC. "예약" 과 "쓰기 완료" 를 구분 못 한다.
 *             강제 스케줄 2 개 (결정적): (1) 생산자가 tail CAS 후 쓰기 전에 멈춤 → 소비자가 빈 슬롯을 읽음
 *                                      (2) 소비자가 head CAS 후 읽기 전에 멈춤 → 생산자가 그 슬롯을 덮어씀
 *   cap1    : Vyukov 인데 cap = 1 을 허용 → "게시됨(pos+1)" 과 "비었음(pos+cap)" 이 같은 숫자 → 덮어쓰기
 *   relaxed : Vyukov 인데 seq 의 load/store 를 전부 relaxed → 데이터 레이스 (TSan) + 스트레스 관찰
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_broken.c -o db && ./db naive && ./db cap1 && ./db relaxed
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_broken.c -o db_tsan && ./db_tsan relaxed
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- 강제 스케줄용 세마포어 흉내 (mutex + cond) ---------- */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int n; } sem_t2;
static void s_init(sem_t2 *s) { pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->n = 0; }
static void s_post(sem_t2 *s) { pthread_mutex_lock(&s->m); s->n++; pthread_cond_broadcast(&s->c); pthread_mutex_unlock(&s->m); }
static void s_wait(sem_t2 *s) { pthread_mutex_lock(&s->m); while (s->n == 0) pthread_cond_wait(&s->c, &s->m); s->n--; pthread_mutex_unlock(&s->m); }

/* ======================= naive: head/tail 만 ======================= */
#define NCAP 4
typedef struct {
    atomic_size_t head, tail;
    _Atomic uint64_t buf[NCAP];   /* 슬롯을 atomic(relaxed)으로 둬서 TSan 잡음 없이 "논리" 버그만 본다 */
} naive_t;
static sem_t2 n_paused, n_resume;
static atomic_int n_pause_producer, n_pause_consumer;

static bool naive_enq(naive_t *q, uint64_t v) {
    size_t t = atomic_load(&q->tail);
    for (;;) {
        size_t h = atomic_load(&q->head);
        if (t - h >= NCAP) return false;                              /* 가득 */
        if (atomic_compare_exchange_weak(&q->tail, &t, t + 1)) break; /* 슬롯 t "예약" */
    }
    if (atomic_exchange(&n_pause_producer, 0)) { s_post(&n_paused); s_wait(&n_resume); }
    atomic_store_explicit(&q->buf[t % NCAP], v, memory_order_relaxed);
    return true;
}
static bool naive_deq(naive_t *q, uint64_t *out) {
    size_t h = atomic_load(&q->head);
    for (;;) {
        size_t t = atomic_load(&q->tail);
        if (h == t) return false;                                     /* 빔 — 그런데 "예약" 도 t 에 포함됨 */
        if (atomic_compare_exchange_weak(&q->head, &h, h + 1)) break;
    }
    if (atomic_exchange(&n_pause_consumer, 0)) { s_post(&n_paused); s_wait(&n_resume); }
    *out = atomic_load_explicit(&q->buf[h % NCAP], memory_order_relaxed);
    return true;
}
typedef struct { naive_t *q; uint64_t v; bool ok; } narg_t;
static void *naive_enq_th(void *p) { narg_t *a = p; a->ok = naive_enq(a->q, a->v); return NULL; }
static void *naive_deq_th(void *p) { narg_t *a = p; a->ok = naive_deq(a->q, &a->v); return NULL; }

static void demo_naive(void) {
    s_init(&n_paused); s_init(&n_resume);
    /* (1) 생산자 A: tail 0→1 예약 후 멈춤. 소비자: head < tail 이니 슬롯 0 을 읽음 — 아직 안 쓴 값 */
    naive_t q;
    memset(&q, 0, sizeof q);
    for (int i = 0; i < NCAP; i++) atomic_store(&q.buf[i], 0xDEADu);
    atomic_store(&n_pause_producer, 1);
    narg_t a = { &q, 100, false };
    pthread_t th;
    pthread_create(&th, NULL, naive_enq_th, &a);
    s_wait(&n_paused);
    uint64_t got = 0;
    bool ok = naive_deq(&q, &got);
    printf("[naive 1] producer paused after tail CAS: consumer dequeue ok=%d value=0x%llX (expected 100 = 0x64)\n",
           ok, (unsigned long long)got);
    s_post(&n_resume);
    pthread_join(th, NULL);
    ok = naive_deq(&q, &got);
    printf("          after producer resumes: dequeue ok=%d -> message 100 is LOST (written into a slot already consumed)\n", ok);

    /* (2) 소비자 C: head 0→1 후 읽기 전에 멈춤. 생산자: t - h < cap 이니 슬롯 0 을 덮어씀 */
    memset(&q, 0, sizeof q);
    for (int i = 0; i < NCAP; i++) naive_enq(&q, 10u + (uint64_t)i);           /* 10 11 12 13, 가득 */
    atomic_store(&n_pause_consumer, 1);
    narg_t c = { &q, 0, false };
    pthread_create(&th, NULL, naive_deq_th, &c);
    s_wait(&n_paused);
    bool p_ok = naive_enq(&q, 99);                                               /* head 가 1 이라 "자리 있음" */
    s_post(&n_resume);
    pthread_join(th, NULL);
    printf("[naive 2] consumer paused after head CAS: producer enqueue(99) ok=%d, consumer then read %llu (expected 10)\n",
           p_ok, (unsigned long long)c.v);
}

/* ======================= Vyukov (설정 가능: cap1 허용 / relaxed) ======================= */
typedef struct { atomic_size_t seq; uint64_t data; } cell_t;
typedef struct { cell_t *buf; size_t mask; _Alignas(128) atomic_size_t enq; _Alignas(128) atomic_size_t deq; } vq_t;
static memory_order MO_ACQ = memory_order_acquire, MO_REL = memory_order_release;

static void vq_init_any(vq_t *q, cell_t *c, size_t cap) {      /* cap 검사 없음 (cap1 데모용) */
    q->buf = c; q->mask = cap - 1;
    for (size_t i = 0; i < cap; i++) { atomic_init(&c[i].seq, i); c[i].data = 0; }
    atomic_init(&q->enq, 0); atomic_init(&q->deq, 0);
}
static bool vq_enq(vq_t *q, uint64_t v) {
    size_t pos = atomic_load_explicit(&q->enq, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, MO_ACQ) - pos);
        if (dif == 0) { if (atomic_compare_exchange_weak_explicit(&q->enq, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break; }
        else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->enq, memory_order_relaxed);
    }
    c->data = v;
    atomic_store_explicit(&c->seq, pos + 1, MO_REL);
    return true;
}
static bool vq_deq(vq_t *q, uint64_t *out) {
    size_t pos = atomic_load_explicit(&q->deq, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, MO_ACQ) - (pos + 1));
        if (dif == 0) { if (atomic_compare_exchange_weak_explicit(&q->deq, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break; }
        else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->deq, memory_order_relaxed);
    }
    *out = c->data;
    atomic_store_explicit(&c->seq, pos + q->mask + 1, MO_REL);
    return true;
}

static void demo_cap1(void) {
    cell_t c[1];
    vq_t q;
    vq_init_any(&q, c, 1);
    bool e1 = vq_enq(&q, 111), e2 = vq_enq(&q, 222);
    size_t seq = atomic_load(&c[0].seq), deq = atomic_load(&q.deq);
    printf("[cap1] enq 111 -> %d, enq 222 -> %d (cap 1 must refuse the 2nd), slot data=%llu seq=%zu enq=%zu deq=%zu\n",
           e1, e2, (unsigned long long)c[0].data, seq, atomic_load(&q.enq), deq);
    printf("       consumer at pos %zu expects seq %zu, sees %zu: dif=%+td -> 'stale', reloads deq=%zu, forever (livelock)\n",
           deq, deq + 1, seq, (ptrdiff_t)(seq - (deq + 1)), deq);
}

/* relaxed 스트레스: 생산자 P 개가 (p<<32|i) 를 보내고, 소비자가 받은 값의 중복/누락/쓰레기를 센다 */
#define RP 4
#define RC 4
static uint32_t r_per;
static vq_t r_q;
static _Atomic unsigned char *r_hits;
static atomic_size_t r_consumed;
static atomic_size_t r_garbage;
static atomic_bool r_abort;
static double r_deadline;
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + ts.tv_nsec * 1e-9; }
static bool r_timeout(unsigned *spins) {           /* 깨진 큐는 메시지를 잃어 영원히 기다릴 수 있다 → 10 초 상한 */
    if ((++*spins & 0xFFFFu) == 0 && now_s() > r_deadline) atomic_store(&r_abort, true);
    return atomic_load_explicit(&r_abort, memory_order_relaxed);
}
static void *r_prod(void *p) {
    unsigned id = (unsigned)(uintptr_t)p;
    unsigned spins = 0;
    for (uint32_t i = 0; i < r_per; i++)
        while (!vq_enq(&r_q, ((uint64_t)id << 32) | (i + 1u))) if (r_timeout(&spins)) return NULL;
    return NULL;
}
static void *r_cons(void *p) {
    (void)p;
    size_t total = (size_t)RP * r_per;
    unsigned spins = 0;
    while (atomic_load_explicit(&r_consumed, memory_order_relaxed) < total) {
        uint64_t v;
        if (!vq_deq(&r_q, &v)) { if (r_timeout(&spins)) break; continue; }
        atomic_fetch_add_explicit(&r_consumed, 1, memory_order_relaxed);
        unsigned pr = (unsigned)(v >> 32);
        uint32_t i = (uint32_t)v;               /* 1..per (0 = 한 번도 안 쓴 슬롯의 쓰레기) */
        if (pr >= RP || i == 0 || i > r_per) { atomic_fetch_add(&r_garbage, 1); continue; }
        atomic_fetch_add_explicit(&r_hits[(size_t)pr * r_per + (i - 1)], 1, memory_order_relaxed);
    }
    return NULL;
}
static void demo_relaxed(int runs, uint32_t per, size_t cap, bool relaxed) {
    MO_ACQ = relaxed ? memory_order_relaxed : memory_order_acquire;
    MO_REL = relaxed ? memory_order_relaxed : memory_order_release;
    r_per = per;
    for (int run = 0; run < runs; run++) {
        cell_t *c = calloc(cap, sizeof *c);
        vq_init_any(&r_q, c, cap);
        size_t n = (size_t)RP * per;
        r_hits = calloc(n, 1);
        atomic_store(&r_consumed, 0); atomic_store(&r_garbage, 0); atomic_store(&r_abort, false);
        r_deadline = now_s() + 10.0;
        double t0 = now_s();
        pthread_t th[RP + RC];
        for (unsigned k = 0; k < RP; k++) pthread_create(&th[k], NULL, r_prod, (void *)(uintptr_t)k);
        for (unsigned k = 0; k < RC; k++) pthread_create(&th[RP + k], NULL, r_cons, NULL);
        for (unsigned k = 0; k < RP + RC; k++) pthread_join(th[k], NULL);
        size_t lost = 0, dup = 0;
        for (size_t k = 0; k < n; k++) { unsigned h = atomic_load(&r_hits[k]); lost += h == 0; dup += h > 1; }
        printf("[%s] run %d: %zu msgs, cap %zu, %.2f s%s: lost %zu, dup %zu, garbage %zu\n", relaxed ? "relaxed" : "acq/rel",
               run, n, cap, now_s() - t0, atomic_load(&r_abort) ? " (TIMEOUT)" : "", lost, dup, atomic_load(&r_garbage));
        free((void *)r_hits); free(c);
    }
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "naive";
    if (!strcmp(m, "naive")) demo_naive();
    else if (!strcmp(m, "cap1")) demo_cap1();
    else if (!strcmp(m, "relaxed")) {
        uint32_t per = argc > 2 ? (uint32_t)atoi(argv[2]) : 2000000u;
        size_t cap = argc > 3 ? (size_t)atoi(argv[3]) : 4;
        int runs = argc > 4 ? atoi(argv[4]) : 3;
        demo_relaxed(runs, per, cap, true);
    } else if (!strcmp(m, "acqrel")) {
        uint32_t per = argc > 2 ? (uint32_t)atoi(argv[2]) : 2000000u;
        size_t cap = argc > 3 ? (size_t)atoi(argv[3]) : 4;
        demo_relaxed(argc > 4 ? atoi(argv[4]) : 3, per, cap, false);
    }
    return 0;
}
