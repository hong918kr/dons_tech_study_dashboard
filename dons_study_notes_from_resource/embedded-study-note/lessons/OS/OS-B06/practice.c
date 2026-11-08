/* OS-B06 추가 연습 — 한 번에 검증
 *   연습 1  bb_get_timeout(): pthread_cond_timedwait + 절대 마감 시각 (가짜 깨움에도 마감은 그대로)
 *   연습 2  bb_try_put(): ISR 처럼 절대 기다리지 않는 생산자 (꽉 차면 drop 을 센다)
 *   연습 3  mutex+cond 로 만든 카운팅 세마포어 csem_t, 그리고 세마포어 2개로 만든 bounded buffer
 *   연습 4  재사용 가능한 배리어 (macOS 에는 pthread_barrier_t 가 없다) — 세대(generation) 번호
 *   연습 5  covering condition (OSTEP 30.3): 크기가 다른 할당 요청 → free 는 broadcast
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define TMO_MS 2000
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#define WAIT_FOR(mp, ms, cond) do {                                          \
        for (int t_ = 0; ; t_++) {                                           \
            pthread_mutex_lock(mp); bool ok_ = (cond);                       \
            pthread_mutex_unlock(mp);                                        \
            if (ok_ || t_ >= (ms)) break;                                    \
            sleep_ms(1);                                                     \
        }                                                                    \
    } while (0)

/* ---------- 본문과 같은 bounded buffer (축약) ---------- */
#define BB_MAX 64
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t empty, fill;
    int buf[BB_MAX], cap, head, count;
    bool closed;
    int waiting; long waits, drops;
} bb_t;
static void bb_init(bb_t *b, int cap) {
    memset(b, 0, sizeof *b);
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->empty, NULL); pthread_cond_init(&b->fill, NULL);
    b->cap = cap;
}
static void bb_destroy(bb_t *b) {
    pthread_cond_destroy(&b->fill); pthread_cond_destroy(&b->empty); pthread_mutex_destroy(&b->m);
}
static void ring_put(bb_t *b, int v) { b->buf[(b->head + b->count) % b->cap] = v; b->count++; }
static int ring_get(bb_t *b) { int v = b->buf[b->head]; b->head = (b->head + 1) % b->cap; b->count--; return v; }
static void bb_put(bb_t *b, int v) {
    pthread_mutex_lock(&b->m);
    while (b->count == b->cap) pthread_cond_wait(&b->empty, &b->m);
    ring_put(b, v);
    pthread_cond_signal(&b->fill);
    pthread_mutex_unlock(&b->m);
}
static bool bb_get(bb_t *b, int *out) {
    pthread_mutex_lock(&b->m);
    while (b->count == 0 && !b->closed) pthread_cond_wait(&b->fill, &b->m);
    if (b->count == 0) { pthread_mutex_unlock(&b->m); return false; }
    *out = ring_get(b);
    pthread_cond_signal(&b->empty);
    pthread_mutex_unlock(&b->m);
    return true;
}
static void bb_close(bb_t *b) {
    pthread_mutex_lock(&b->m);
    b->closed = true;
    pthread_cond_broadcast(&b->fill); pthread_cond_broadcast(&b->empty);
    pthread_mutex_unlock(&b->m);
}

/* ======================= 연습 1 — 타임아웃 get ======================= */
/* 마감 시각은 "처음 한 번" 계산한다. 가짜 깨움 뒤에 다시 계산하면 대기 시간이 계속 늘어난다.
 * macOS 에는 pthread_condattr_setclock 이 없어서 CLOCK_REALTIME 기준 절대 시각을 쓴다. */
static bool bb_get_timeout(bb_t *b, int *out, int ms) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += ms / 1000;
    dl.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&b->m);
    int rc = 0;
    while (b->count == 0 && !b->closed && rc != ETIMEDOUT) {
        b->waiting++; b->waits++;
        rc = pthread_cond_timedwait(&b->fill, &b->m, &dl);
        b->waiting--;
    }
    if (b->count == 0) { pthread_mutex_unlock(&b->m); return false; }   /* 시간 초과 직후 들어온 항목은 받는다 */
    *out = ring_get(b);
    pthread_cond_signal(&b->empty);
    pthread_mutex_unlock(&b->m);
    return true;
}
typedef struct { bb_t *b; int ms, v; bool r, done; } tget_t;
static void *tget_thread(void *arg) {
    tget_t *t = arg; int v = 0;
    bool r = bb_get_timeout(t->b, &v, t->ms);
    pthread_mutex_lock(&t->b->m); t->r = r; t->v = v; t->done = true; pthread_mutex_unlock(&t->b->m);
    return NULL;
}

/* ======================= 연습 2 — ISR 생산자: 기다리지 않는 put ======================= */
static bool bb_try_put(bb_t *b, int v) {
    pthread_mutex_lock(&b->m);
    if (b->count == b->cap) { b->drops++; pthread_mutex_unlock(&b->m); return false; }
    ring_put(b, v);
    pthread_cond_signal(&b->fill);
    pthread_mutex_unlock(&b->m);
    return true;
}
enum { ISR_N = 100000 };
static void *isr_thread(void *arg) {
    bb_t *b = arg;
    for (int i = 0; i < ISR_N; i++) (void)bb_try_put(b, i);
    bb_close(b);                                     /* 다 보냈다 → 소비자는 남은 걸 비우고 끝 */
    return NULL;
}

/* ======================= 연습 3 — 카운팅 세마포어 (mutex + cond) ======================= */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int v; } csem_t;
static void csem_init(csem_t *s, int v) { pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->v = v; }
static void csem_destroy(csem_t *s) { pthread_cond_destroy(&s->c); pthread_mutex_destroy(&s->m); }
static void csem_wait(csem_t *s) {
    pthread_mutex_lock(&s->m);
    while (s->v <= 0) pthread_cond_wait(&s->c, &s->m);
    s->v--;
    pthread_mutex_unlock(&s->m);
}
static void csem_post(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->v++;
    pthread_cond_signal(&s->c);            /* 값 1 증가 = 기다리는 한 명이 진행 가능 → signal 로 충분 */
    pthread_mutex_unlock(&s->m);
}
/* 세마포어 버퍼 (OSTEP 31장): empty = 빈 칸 수, full = 찬 칸 수, mutex 는 링만 보호 */
typedef struct { csem_t empty, full; pthread_mutex_t m; int buf[BB_MAX], cap, head, count; } sbuf_t;
static void sbuf_put(sbuf_t *s, int v) {
    csem_wait(&s->empty);                  /* 순서 중요: 락을 먼저 잡고 기다리면 교착 */
    pthread_mutex_lock(&s->m);
    s->buf[(s->head + s->count) % s->cap] = v; s->count++;
    pthread_mutex_unlock(&s->m);
    csem_post(&s->full);
}
static int sbuf_get(sbuf_t *s) {
    csem_wait(&s->full);
    pthread_mutex_lock(&s->m);
    int v = s->buf[s->head]; s->head = (s->head + 1) % s->cap; s->count--;
    pthread_mutex_unlock(&s->m);
    csem_post(&s->empty);
    return v;
}
enum { SB_P = 4, SB_C = 4, SB_PER = 10000 };
typedef struct { sbuf_t *s; int id; uint8_t *seen; long bad; } sbarg_t;
static void *sb_prod(void *arg) {
    sbarg_t *a = arg;
    for (int i = 0; i < SB_PER; i++) sbuf_put(a->s, a->id * SB_PER + i);
    return NULL;
}
static void *sb_cons(void *arg) {           /* 소비자마다 정확히 SB_PER 개 */
    sbarg_t *a = arg; int last[SB_P];
    for (int i = 0; i < SB_P; i++) last[i] = -1;
    for (int i = 0; i < SB_PER; i++) {
        int v = sbuf_get(a->s), p = v / SB_PER, q = v % SB_PER;
        if (p < 0 || p >= SB_P || q <= last[p]) { a->bad++; continue; }
        last[p] = q; a->seen[v]++;
    }
    return NULL;
}

/* ======================= 연습 4 — 재사용 배리어 ======================= */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int n, arrived; unsigned gen; } barrier_t;
static void barrier_init(barrier_t *b, int n) {
    pthread_mutex_init(&b->m, NULL); pthread_cond_init(&b->c, NULL); b->n = n; b->arrived = 0; b->gen = 0;
}
/* 마지막으로 도착한 스레드만 true. 깨어난 이유가 "내 세대가 끝남" 인지 세대 번호로 확인한다. */
static bool barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->m);
    unsigned my_gen = b->gen;
    if (++b->arrived == b->n) {
        b->arrived = 0; b->gen++;
        pthread_cond_broadcast(&b->c);     /* 모두가 진행해야 한다 → broadcast */
        pthread_mutex_unlock(&b->m);
        return true;
    }
    while (my_gen == b->gen) pthread_cond_wait(&b->c, &b->m);   /* count 가 아니라 gen 을 본다 */
    pthread_mutex_unlock(&b->m);
    return false;
}
enum { BAR_T = 4, BAR_ROUNDS = 2000 };
static barrier_t g_bar;
static int g_slot[BAR_T];                  /* 배리어가 happens-before 를 만들어 주므로 일반 변수 */
typedef struct { int id; long mismatch, serial; } barg_t;
static void *bar_thread(void *arg) {
    barg_t *a = arg;
    for (int r = 1; r <= BAR_ROUNDS; r++) {
        g_slot[a->id] = r;
        if (barrier_wait(&g_bar)) a->serial++;
        for (int i = 0; i < BAR_T; i++) if (g_slot[i] != r) a->mismatch++;
        if (barrier_wait(&g_bar)) a->serial++;   /* 다음 라운드 쓰기 전에 모두 다 읽었는지 */
    }
    return NULL;
}

/* ======================= 연습 5 — covering condition ======================= */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int bytes_left, waiting; } heap_t;
static void heap_alloc(heap_t *h, int n) {
    pthread_mutex_lock(&h->m);
    while (h->bytes_left < n) { h->waiting++; pthread_cond_wait(&h->c, &h->m); h->waiting--; }
    h->bytes_left -= n;
    pthread_mutex_unlock(&h->m);
}
static void heap_free(heap_t *h, int n) {
    pthread_mutex_lock(&h->m);
    h->bytes_left += n;
    pthread_cond_broadcast(&h->c);         /* 누가 진행 가능한지 free 는 모른다 → 모두 깨워 각자 재검사 */
    pthread_mutex_unlock(&h->m);
}
typedef struct { heap_t *h; int n; bool done; } aarg_t;
static void *alloc_thread(void *arg) {
    aarg_t *a = arg;
    heap_alloc(a->h, a->n);
    pthread_mutex_lock(&a->h->m); a->done = true; pthread_mutex_unlock(&a->h->m);
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    bb_t b; int v = 0;

    /* 연습 1 */
    bb_init(&b, 4);
    CHECK(!bb_get_timeout(&b, &v, 30) && b.waits >= 1);          /* 빈 버퍼: 시간 초과 → false */
    bb_put(&b, 5);
    long w0 = b.waits;
    CHECK(bb_get_timeout(&b, &v, 30) && v == 5 && b.waits == w0);  /* 있으면 기다리지 않음 */
    {   /* 기다리는 동안 상태 변화 없는 broadcast 5번 → 그래도 항목 없이 마감까지 기다렸다 false */
        tget_t t = { &b, 300, 0, true, false };
        pthread_t th; pthread_create(&th, NULL, tget_thread, &t);
        WAIT_FOR(&b.m, TMO_MS, b.waiting == 1);
        long w1 = 0;
        for (int i = 0; i < 5; i++) {
            pthread_mutex_lock(&b.m); w1 = b.waits; pthread_cond_broadcast(&b.fill); pthread_mutex_unlock(&b.m);
            WAIT_FOR(&b.m, TMO_MS, b.waits > w1 || t.done);       /* 깨어나 재검사 후 다시 잠들 때까지 */
        }
        pthread_join(th, NULL);
        CHECK(!t.r && b.waits >= w1 + 1);
    }
    {   /* 기다리는 중에 들어온 항목은 받는다 */
        tget_t t = { &b, 5000, 0, false, false };
        pthread_t th; pthread_create(&th, NULL, tget_thread, &t);
        WAIT_FOR(&b.m, TMO_MS, b.waiting == 1);
        bb_put(&b, 9);
        pthread_join(th, NULL);
        CHECK(t.r && t.v == 9);
    }
    bb_destroy(&b);

    /* 연습 2 */
    bb_init(&b, 2);
    CHECK(bb_try_put(&b, 1) && bb_try_put(&b, 2) && !bb_try_put(&b, 3) && b.drops == 1);
    CHECK(bb_get(&b, &v) && v == 1 && bb_try_put(&b, 4) && b.count == 2);
    bb_destroy(&b);
    {
        bb_init(&b, 8);
        pthread_t th; pthread_create(&th, NULL, isr_thread, &b);
        long got = 0, bad = 0; int last = -1;
        while (bb_get(&b, &v)) {                                  /* 로거: 닫힐 때까지 */
            if (v <= last) bad++;
            last = v; got++;
        }
        pthread_join(th, NULL);
        printf("try_put: received=%ld dropped=%ld\n", got, b.drops);
        CHECK(got + b.drops == ISR_N && bad == 0);                /* 받은 것 + 버린 것 = 보낸 것, 순서 유지 */
        bb_destroy(&b);
    }

    /* 연습 3 */
    {
        csem_t s; csem_init(&s, 2);
        csem_wait(&s); csem_wait(&s);
        CHECK(s.v == 0);
        csem_post(&s);
        CHECK(s.v == 1);
        csem_destroy(&s);

        sbuf_t sb; memset(&sb, 0, sizeof sb);
        sb.cap = 4; csem_init(&sb.empty, sb.cap); csem_init(&sb.full, 0); pthread_mutex_init(&sb.m, NULL);
        uint8_t *seen = calloc(SB_P * SB_PER, 1);
        pthread_t tp[SB_P], tc[SB_C]; sbarg_t ap[SB_P], ac[SB_C];
        for (int i = 0; i < SB_C; i++) { ac[i] = (sbarg_t){ &sb, i, seen, 0 }; pthread_create(&tc[i], NULL, sb_cons, &ac[i]); }
        for (int i = 0; i < SB_P; i++) { ap[i] = (sbarg_t){ &sb, i, seen, 0 }; pthread_create(&tp[i], NULL, sb_prod, &ap[i]); }
        for (int i = 0; i < SB_P; i++) pthread_join(tp[i], NULL);
        for (int i = 0; i < SB_C; i++) pthread_join(tc[i], NULL);
        long bad = 0; bool once = true;
        for (int i = 0; i < SB_C; i++) bad += ac[i].bad;
        for (int i = 0; i < SB_P * SB_PER; i++) if (seen[i] != 1) once = false;
        CHECK(once && bad == 0);
        CHECK(sb.count == 0 && sb.empty.v == sb.cap && sb.full.v == 0);
        free(seen);
        csem_destroy(&sb.empty); csem_destroy(&sb.full); pthread_mutex_destroy(&sb.m);
    }

    /* 연습 4 */
    {
        barrier_init(&g_bar, BAR_T);
        pthread_t th[BAR_T]; barg_t a[BAR_T];
        for (int i = 0; i < BAR_T; i++) { a[i] = (barg_t){ i, 0, 0 }; pthread_create(&th[i], NULL, bar_thread, &a[i]); }
        long mism = 0, serial = 0;
        for (int i = 0; i < BAR_T; i++) { pthread_join(th[i], NULL); mism += a[i].mismatch; serial += a[i].serial; }
        CHECK(mism == 0);                                         /* 모든 라운드에서 모두가 같은 라운드 값을 봄 */
        CHECK(serial == 2L * BAR_ROUNDS);                         /* 배리어마다 "마지막 도착자" 는 정확히 1명 */
        pthread_cond_destroy(&g_bar.c); pthread_mutex_destroy(&g_bar.m);
    }

    /* 연습 5: Ta 는 100, Tb 는 10 을 원한다. free(50) → Tb 만 진행, Ta 는 계속 대기 */
    {
        heap_t h = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };
        aarg_t ta = { &h, 100, false }, tb = { &h, 10, false };
        pthread_t tha, thb;
        pthread_create(&tha, NULL, alloc_thread, &ta);
        WAIT_FOR(&h.m, TMO_MS, h.waiting == 1);
        pthread_create(&thb, NULL, alloc_thread, &tb);
        WAIT_FOR(&h.m, TMO_MS, h.waiting == 2);
        heap_free(&h, 50);
        WAIT_FOR(&h.m, TMO_MS, tb.done);
        WAIT_FOR(&h.m, TMO_MS, h.waiting == 1);
        pthread_mutex_lock(&h.m); bool step1 = tb.done && !ta.done && h.waiting == 1 && h.bytes_left == 40; pthread_mutex_unlock(&h.m);
        CHECK(step1);
        heap_free(&h, 60);
        pthread_join(tha, NULL); pthread_join(thb, NULL);
        CHECK(ta.done && h.bytes_left == 0 && h.waiting == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
