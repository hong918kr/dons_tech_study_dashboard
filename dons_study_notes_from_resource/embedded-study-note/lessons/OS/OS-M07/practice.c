/* OS-M07 추가 연습 — 한 번에 검증
 *   연습 1  비례 백오프 ticket lock: 내 앞에 몇 명 있는지 × base 만큼 쉬고 다시 본다
 *   연습 2  시간 제한 TTAS (워치독이 있는 펌웨어용): 정해진 횟수만 돌고 실패를 돌려준다
 *   연습 3  32비트 한 워드에 next(상위 16)·owner(하위 16) — Linux arm32 arch_spinlock_t 모양
 *   연습 4  디버그 스핀락: 소유 코어 기록 → 같은 코어 재진입(자기 교착)과 남의 락 해제를 잡는다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ---------- 연습 1: 비례 백오프 ticket ---------- */
typedef struct { _Alignas(128) atomic_uint next; atomic_uint serving; } ticket_t;
/* 내 앞 대기 인원 (wrap-around 안전: 부호 없는 뺄셈) × base */
static unsigned prop_delay(unsigned my, unsigned serving, unsigned base) { return (my - serving) * base; }
static unsigned tk_lock_prop(ticket_t *t, unsigned base) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    for (;;) {
        unsigned s = atomic_load_explicit(&t->serving, memory_order_acquire);
        if (s == my) return my;
        unsigned d = prop_delay(my, s, base);
        for (unsigned i = 0; i < d; i++) cpu_relax();
    }
}
static void tk_unlock(ticket_t *t) {
    atomic_store_explicit(&t->serving, atomic_load_explicit(&t->serving, memory_order_relaxed) + 1u,
                          memory_order_release);
}

/* ---------- 연습 2: 시간 제한 TTAS ---------- */
typedef struct { _Alignas(128) atomic_uint v; } spin_t;
static bool ttas_lock_bounded(spin_t *l, unsigned max_spins) {
    for (unsigned spins = 0;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed) != 0u) {
            if (++spins >= max_spins) return false;
            cpu_relax();
        }
        if (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) == 0u) return true;
        if (++spins >= max_spins) return false;
    }
}
static void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }

/* ---------- 연습 3: 한 워드 ticket (상위 16 = next, 하위 16 = owner) ---------- */
typedef struct { _Alignas(128) atomic_uint w; } pticket_t;
#define PT_NEXT(x)  ((x) >> 16)
#define PT_OWNER(x) ((x) & 0xFFFFu)
static unsigned pt_lock(pticket_t *p) {
    unsigned old = atomic_fetch_add_explicit(&p->w, 1u << 16, memory_order_relaxed);   /* next++ (상위 carry 는 버려짐) */
    unsigned my = PT_NEXT(old);
    while (PT_OWNER(atomic_load_explicit(&p->w, memory_order_acquire)) != my) cpu_relax();
    return my;
}
/* owner 만 +1: 0xFFFF → 0 일 때 carry 가 next 로 넘어가면 안 된다 → CAS 로 하위 16비트만 바꾼다.
 * (Linux arm32 는 owner 반쪽에 16비트 store 를 쓴다고 알려져 있다 — C11 atomic 로는 반쪽만 쓸 수 없다.) */
static void pt_unlock(pticket_t *p) {
    unsigned old = atomic_load_explicit(&p->w, memory_order_relaxed), nw;
    do {
        nw = (old & 0xFFFF0000u) | ((old + 1u) & 0xFFFFu);
    } while (!atomic_compare_exchange_weak_explicit(&p->w, &old, nw, memory_order_release, memory_order_relaxed));
}
/* 한 번의 CAS 로 "next == owner 인가" 를 원자적으로 확인하고 번호표를 뽑는다 */
static bool pt_trylock(pticket_t *p) {
    unsigned old = atomic_load_explicit(&p->w, memory_order_relaxed);
    if (PT_NEXT(old) != PT_OWNER(old)) return false;
    return atomic_compare_exchange_strong_explicit(&p->w, &old, old + (1u << 16),
                                                   memory_order_acquire, memory_order_relaxed);
}
static bool pt_is_locked(pticket_t *p) {
    unsigned v = atomic_load_explicit(&p->w, memory_order_relaxed);
    return PT_NEXT(v) != PT_OWNER(v);
}

/* ---------- 연습 4: 디버그 스핀락 ---------- */
#define NO_OWNER (-1)
typedef struct { spin_t s; atomic_int owner; } dbg_spin_t;
enum { DBG_OK = 0, DBG_RECURSION = -1, DBG_NOT_OWNER = -2 };
static int dbg_lock(dbg_spin_t *l, int cpu) {
    if (atomic_load_explicit(&l->owner, memory_order_relaxed) == cpu) return DBG_RECURSION;  /* 같은 코어가 또 → 영원히 못 잡는다 */
    (void)ttas_lock_bounded(&l->s, 0xFFFFFFFFu);
    atomic_store_explicit(&l->owner, cpu, memory_order_relaxed);
    return DBG_OK;
}
static int dbg_unlock(dbg_spin_t *l, int cpu) {
    if (atomic_load_explicit(&l->owner, memory_order_relaxed) != cpu) return DBG_NOT_OWNER;
    atomic_store_explicit(&l->owner, NO_OWNER, memory_order_relaxed);
    spin_unlock(&l->s);
    return DBG_OK;
}

/* ---------- 스트레스 / 강제 스케줄 ---------- */
#define NT 4
#define ITERS 30000
static ticket_t g_tk;
static pticket_t g_pt;
static dbg_spin_t g_dbg = { { 0 }, NO_OWNER };
static _Alignas(128) uint64_t g_plain[3];
static atomic_int g_inside[3], g_viol[3];
static void cs(int k) {
    if (atomic_fetch_add_explicit(&g_inside[k], 1, memory_order_relaxed) != 0)
        atomic_fetch_add_explicit(&g_viol[k], 1, memory_order_relaxed);
    g_plain[k]++;
    atomic_fetch_sub_explicit(&g_inside[k], 1, memory_order_relaxed);
}
static void *stress(void *arg) {
    int cpu = (int)(long)arg;
    for (int i = 0; i < ITERS; i++) {
        (void)tk_lock_prop(&g_tk, 8u); cs(0); tk_unlock(&g_tk);
        (void)pt_lock(&g_pt); cs(1); pt_unlock(&g_pt);
        if (dbg_lock(&g_dbg, cpu) == DBG_OK) { cs(2); (void)dbg_unlock(&g_dbg, cpu); }
    }
    return NULL;
}

typedef struct { int which; int id; int *order; atomic_int *pos; } fa_t;
static ticket_t g_ft;
static pticket_t g_fpt;
static void *fifo_w(void *arg) {
    fa_t *a = arg;
    if (a->which == 0) (void)tk_lock_prop(&g_ft, 4u); else (void)pt_lock(&g_fpt);
    a->order[atomic_fetch_add_explicit(a->pos, 1, memory_order_relaxed)] = a->id;
    if (a->which == 0) tk_unlock(&g_ft); else pt_unlock(&g_fpt);
    return NULL;
}
static unsigned next_of(int which) {
    return which == 0 ? atomic_load(&g_ft.next) : PT_NEXT(atomic_load(&g_fpt.w));
}
static bool fifo_forced(int which) {
    int order[5];
    atomic_int pos = 0;
    fa_t fa[5];
    pthread_t th[5];
    if (which == 0) (void)tk_lock_prop(&g_ft, 4u); else (void)pt_lock(&g_fpt);
    unsigned base = next_of(which);
    bool started = true;
    for (int i = 0; i < 5; i++) {
        fa[i] = (fa_t){ which, i + 1, order, &pos };
        pthread_create(&th[i], NULL, fifo_w, &fa[i]);
        double t0 = now_s();
        while (((next_of(which) - base) & 0xFFFFu) != (unsigned)i + 1u) {
            if (now_s() - t0 > 2.0) { started = false; break; }
            struct timespec ts = { 0, 100000 };
            nanosleep(&ts, NULL);
        }
    }
    if (which == 0) tk_unlock(&g_ft); else pt_unlock(&g_fpt);
    for (int i = 0; i < 5; i++) pthread_join(th[i], NULL);
    bool ok = started && atomic_load(&pos) == 5;
    for (int i = 0; i < 5 && ok; i++) ok = order[i] == i + 1;
    return ok;
}

int main(void) {
    /* 연습 1 */
    CHECK(prop_delay(5u, 2u, 10u) == 30u && prop_delay(3u, 3u, 10u) == 0u);
    CHECK(prop_delay(1u, 0xFFFFFFFFu, 10u) == 20u);                      /* wrap-around */
    ticket_t t = { 0, 0 };
    CHECK(tk_lock_prop(&t, 8u) == 0u);
    tk_unlock(&t);
    CHECK(tk_lock_prop(&t, 8u) == 1u && atomic_load(&t.serving) == 1u);
    tk_unlock(&t);

    /* 연습 2 */
    spin_t s = { 0 };
    CHECK(ttas_lock_bounded(&s, 100u) == true && atomic_load(&s.v) == 1u);
    double t0 = now_s();
    CHECK(ttas_lock_bounded(&s, 1000u) == false && atomic_load(&s.v) == 1u);   /* 남이 쥔 상태 유지 */
    CHECK(now_s() - t0 < 1.0);                                                  /* 정말로 멈췄다 */
    spin_unlock(&s);
    CHECK(ttas_lock_bounded(&s, 1u) == true);
    spin_unlock(&s);

    /* 연습 3 */
    pticket_t p = { 0 };
    CHECK(pt_trylock(&p) == true && atomic_load(&p.w) == 0x00010000u && pt_is_locked(&p));
    CHECK(pt_trylock(&p) == false);
    pt_unlock(&p);
    CHECK(atomic_load(&p.w) == 0x00010001u && !pt_is_locked(&p));
    atomic_store(&p.w, 0xFFFFFFFFu);                                   /* next = owner = 0xFFFF */
    CHECK(!pt_is_locked(&p) && pt_lock(&p) == 0xFFFFu && atomic_load(&p.w) == 0x0000FFFFu);
    pt_unlock(&p);                                                     /* owner 0xFFFF → 0, next 는 그대로 0 */
    CHECK(atomic_load(&p.w) == 0x00000000u && !pt_is_locked(&p));

    /* 연습 4 */
    dbg_spin_t d = { { 0 }, NO_OWNER };
    CHECK(dbg_lock(&d, 0) == DBG_OK && atomic_load(&d.owner) == 0);
    CHECK(dbg_lock(&d, 0) == DBG_RECURSION);                           /* 실제라면 영원히 스핀 */
    CHECK(dbg_unlock(&d, 1) == DBG_NOT_OWNER && atomic_load(&d.s.v) == 1u);
    CHECK(dbg_unlock(&d, 0) == DBG_OK && atomic_load(&d.s.v) == 0u && atomic_load(&d.owner) == NO_OWNER);

    /* 스트레스 + 강제 스케줄 FIFO */
    pthread_t th[NT];
    for (long i = 0; i < NT; i++) pthread_create(&th[i], NULL, stress, (void *)i);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    CHECK(g_plain[0] == (uint64_t)NT * ITERS && atomic_load(&g_viol[0]) == 0);
    CHECK(g_plain[1] == (uint64_t)NT * ITERS && atomic_load(&g_viol[1]) == 0);
    CHECK(g_plain[2] == (uint64_t)NT * ITERS && atomic_load(&g_viol[2]) == 0);
    CHECK(fifo_forced(0));
    atomic_store(&g_fpt.w, 0xFFFDFFFDu);                               /* wrap 직전에서 FIFO */
    CHECK(fifo_forced(1));
    CHECK(!pt_is_locked(&g_fpt) && PT_OWNER(atomic_load(&g_fpt.w)) == 0x0003u);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
