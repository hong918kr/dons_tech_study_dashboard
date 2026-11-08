/* DS-M06 관찰용 데모 — 단일 레벨 휠의 "방문 비용"과 wheel vs indexed min-heap 시간 비교
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_wheel_vs_heap.c -o demo && ./demo
 * (solution.c 의 휠을 그대로 복사 + 1단계 방문 수 카운터. 힙은 pos[] 맵으로 O(log n) 취소)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TW_BITS  8u
#define TW_SLOTS (1u << TW_BITS)
#define TW_MASK  (TW_SLOTS - 1u)
#define NMAX     (1u << 16)

typedef struct tw_link { struct tw_link *next, *prev; } tw_link_t;
static void link_init(tw_link_t *h) { h->next = h->prev = h; }
static void link_add_tail(tw_link_t *h, tw_link_t *n) { n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n; }
static void link_del(tw_link_t *n) { n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = n; }
#define container_of(p, T, m) ((T *)(void *)((char *)(p) - offsetof(T, m)))

typedef struct tw_timer { tw_link_t link; uint32_t expire, rounds, seq; uint32_t id; bool armed; } tw_timer_t;
typedef struct { tw_link_t slot[TW_SLOTS]; uint32_t now, seq, count; uint64_t visits, fires; } tw_wheel_t;
typedef void (*fire_fn)(uint32_t id);

static tw_wheel_t W;
static tw_timer_t T[NMAX];

static void tw_init(void) {
    for (uint32_t i = 0; i < TW_SLOTS; i++) link_init(&W.slot[i]);
    W.now = W.seq = W.count = 0; W.visits = W.fires = 0;
    for (uint32_t i = 0; i < NMAX; i++) { link_init(&T[i].link); T[i].armed = false; T[i].id = i; }
}
static void tw_add(uint32_t id, uint32_t delay) {
    tw_timer_t *t = &T[id];
    if (delay == 0) delay = 1;
    if (t->armed) { link_del(&t->link); W.count--; }
    t->expire = W.now + delay; t->rounds = (delay - 1u) >> TW_BITS; t->seq = W.seq++; t->armed = true;
    link_add_tail(&W.slot[t->expire & TW_MASK], &t->link); W.count++;
}
static void tw_tick(fire_fn fn) {
    tw_link_t due; link_init(&due);
    W.now++;
    tw_link_t *head = &W.slot[W.now & TW_MASK];
    for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
        nx = p->next; W.visits++;
        tw_timer_t *t = container_of(p, tw_timer_t, link);
        if (t->rounds == 0) { link_del(p); link_add_tail(&due, p); } else t->rounds--;
    }
    while (due.next != &due) {
        tw_timer_t *t = container_of(due.next, tw_timer_t, link);
        link_del(&t->link); t->armed = false; W.count--; W.fires++;
        if (fn) fn(t->id);
    }
}

/* ---- indexed min-heap: (expire, seq), pos[id] = 힙 안 위치 (-1 = 없음) ---- */
typedef struct { uint32_t expire, seq, id; } hev_t;
static hev_t H[NMAX];
static int32_t pos[NMAX];
static uint32_t hn, hnow, hseq;
static bool tb(uint32_t a, uint32_t b) { return a != b && (uint32_t)(a - b) > 0x7FFFFFFFu; }
static bool hless(hev_t a, hev_t b) { return a.expire != b.expire ? tb(a.expire, b.expire) : a.seq < b.seq; }
static void hset(uint32_t i, hev_t e) { H[i] = e; pos[e.id] = (int32_t)i; }
static void hup(uint32_t i) {
    hev_t x = H[i];
    while (i > 0 && hless(x, H[(i - 1u) / 2u])) { hset(i, H[(i - 1u) / 2u]); i = (i - 1u) / 2u; }
    hset(i, x);
}
static void hdown(uint32_t i) {
    hev_t x = H[i];
    for (;;) {
        uint32_t l = 2u * i + 1u, m = l;
        if (l >= hn) break;
        if (l + 1u < hn && hless(H[l + 1u], H[l])) m = l + 1u;
        if (!hless(H[m], x)) break;
        hset(i, H[m]); i = m;
    }
    hset(i, x);
}
static void h_init(void) { hn = hnow = hseq = 0; for (uint32_t i = 0; i < NMAX; i++) pos[i] = -1; }
static void h_remove(uint32_t id) {
    int32_t p = pos[id];
    if (p < 0) return;
    pos[id] = -1;
    uint32_t i = (uint32_t)p;
    if (i == --hn) return;
    hset(i, H[hn]);
    if (i > 0 && hless(H[i], H[(i - 1u) / 2u])) hup(i); else hdown(i);
}
static void h_add(uint32_t id, uint32_t delay) {
    if (delay == 0) delay = 1;
    h_remove(id);
    hset(hn, (hev_t){ hnow + delay, hseq++, id }); hn++;
    hup(hn - 1u);
}
static uint64_t h_fires;
static void h_tick(fire_fn fn) {
    hnow++;
    while (hn && !tb(hnow, H[0].expire)) {
        uint32_t id = H[0].id;
        h_remove(id); h_fires++;
        if (fn) fn(id);
    }
}

static uint32_t rs = 1u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static double now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e9 + ts.tv_nsec; }

/* ---------- 표 1: delay 범위별 방문 비용 (주기 타이머 2048개, 콜백에서 [1,D] 재등록) ---------- */
static uint32_t g_D;
static void rearm_cb(uint32_t id) { tw_add(id, 1u + rnd() % g_D); }

static void table1(void) {
    static const uint32_t Ds[] = { 64, 256, 1024, 4096, 16384, 65536 };
    const uint32_t N = 2048, TICKS = 200000;
    printf("table1: N=%u periodic timers, delay uniform [1,D], %u ticks\n", N, TICKS);
    printf("| D | fires | visits/fire (measured) | visits/fire (theory) | visits/tick |\n|---|---|---|---|---|\n");
    for (unsigned k = 0; k < sizeof Ds / sizeof Ds[0]; k++) {
        g_D = Ds[k]; rs = 777u;
        tw_init();
        for (uint32_t i = 0; i < N; i++) tw_add(i, 1u + rnd() % g_D);
        for (uint32_t i = 0; i < TICKS; i++) tw_tick(rearm_cb);
        double theory = 0;                          /* d 의 방문 수 = rounds + 1 = (d-1)/256 + 1 */
        for (uint32_t d = 1; d <= g_D; d++) theory += (double)((d - 1u) / TW_SLOTS + 1u);
        theory /= g_D;
        printf("| %u | %llu | %.2f | %.2f | %.2f |\n", g_D, (unsigned long long)W.fires,
               (double)W.visits / (double)W.fires, theory, (double)W.visits / TICKS);
    }
}

/* ---------- 표 2: 시간 비교 ---------- */
typedef struct { double wheel, heap; uint64_t wf, hf; } res_t;

/* W1: L 개를 건 뒤, 매 연산마다 임의 타이머를 새 delay 로 재설정(restart). 10 연산마다 1 tick */
static res_t w1(uint32_t L, uint32_t ops, uint32_t D) {
    res_t r;
    rs = 4242u; tw_init();
    for (uint32_t i = 0; i < L; i++) tw_add(i, 1u + rnd() % D);
    double t0 = now_ns();
    for (uint32_t k = 0; k < ops; k++) { uint32_t x = rnd(); tw_add(x % L, 1u + (x >> 7) % D); if (k % 10u == 9u) tw_tick(NULL); }
    r.wheel = (now_ns() - t0) / ops; r.wf = W.fires;
    rs = 4242u; h_init(); h_fires = 0;
    for (uint32_t i = 0; i < L; i++) h_add(i, 1u + rnd() % D);
    t0 = now_ns();
    for (uint32_t k = 0; k < ops; k++) { uint32_t x = rnd(); h_add(x % L, 1u + (x >> 7) % D); if (k % 10u == 9u) h_tick(NULL); }
    r.heap = (now_ns() - t0) / ops; r.hf = h_fires;
    return r;
}
/* W2: L 개를 delay [1,D] 로 걸고 전부 만료될 때까지 tick. 타이머 1개당 (등록+만료) 시간 */
static res_t w2(uint32_t L, uint32_t D) {
    res_t r;
    rs = 99u; tw_init();
    double t0 = now_ns();
    for (uint32_t i = 0; i < L; i++) tw_add(i, 1u + rnd() % D);
    while (W.count) tw_tick(NULL);
    r.wheel = (now_ns() - t0) / L; r.wf = W.fires;
    rs = 99u; h_init(); h_fires = 0;
    t0 = now_ns();
    for (uint32_t i = 0; i < L; i++) h_add(i, 1u + rnd() % D);
    while (hn) h_tick(NULL);
    r.heap = (now_ns() - t0) / L; r.hf = h_fires;
    return r;
}
static void row(const char *name, res_t r) {
    printf("| %s | %.1f | %.1f | %.2fx | %llu / %llu |\n", name, r.wheel, r.heap, r.heap / r.wheel,
           (unsigned long long)r.wf, (unsigned long long)r.hf);
}
static res_t min3(res_t a, res_t b, res_t c) {
    res_t m = a;
    if (b.wheel < m.wheel) m.wheel = b.wheel;
    if (c.wheel < m.wheel) m.wheel = c.wheel;
    if (b.heap < m.heap) m.heap = b.heap;
    if (c.heap < m.heap) m.heap = c.heap;
    return m;
}

int main(void) {
    table1();
    printf("\ntable2: ns per op (min of 3 runs), Apple Silicon -O2\n");
    printf("| workload | wheel ns | heap ns | heap/wheel | fires wheel / heap |\n|---|---|---|---|---|\n");
    row("W1 restart L=10000 D=10000 (ns/op)",
        min3(w1(10000, 2000000, 10000), w1(10000, 2000000, 10000), w1(10000, 2000000, 10000)));
    row("W2 arm+expire L=65536 D=4096 (ns/timer)", min3(w2(65536, 4096), w2(65536, 4096), w2(65536, 4096)));
    row("W2 arm+expire L=8192 D=262144 (ns/timer)", min3(w2(8192, 262144), w2(8192, 262144), w2(8192, 262144)));
    return 0;
}
