/* ARM-M04 LDXR/STXR로 원자 연산 구현 (AArch64)
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (Cortex-A 멀티코어 SoC 의 CPU0..CPUn).
 *   Part A  LL/SC (LDAXR/STLXR) 루프: fetch_add, CAS(strong, 실패 시 CLREX), 스핀락(WFE 대기)
 *   Part B  LSE (ARMv8.1): LDADDAL, CASAL, SWPAL, SWPA 스핀락 + 경계 사례(wrap, 64비트, weak CAS, 혼용)
 *   Part C  DMA 버퍼 풀(LL/SC 로 "가장 낮은 1비트 찾아 지우기") + 공정한 티켓 락(LDADD + LDAXR/WFE)
 *
 * 모든 원자 연산은 GNU extended inline asm 으로 직접 쓴다 (stdatomic 은 하네스의 출발 신호에만).
 * 체크 테스트는 "올바른 구현이면 매 실행 항상 성립" 하는 것만 본다: 최종 카운트 == N*K,
 * 임계 구역 동시 진입 0, 티켓 순서, 버퍼 이중 소유 0. 성능 비교는 demo_bench.c.
 * 각 Part 의 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 돈다 (시작 코드가 멈추지 않게).
 *
 * TSan 은 inline asm 을 계측하지 않는다 → asm 락이 만드는 happens-before 를 모른다. 그래서 하네스는
 * 락 획득/해제 직후·직전에 __tsan_acquire/__tsan_release 로 알려 준다 (TSan 빌드에서만, demo_tsan.c 참고).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if !defined(__aarch64__)
#error "ARM-M04 needs an AArch64 host (Apple Silicon / arm64 Linux)"
#endif

/* -mcpu=generic 같은 ARMv8.0 빌드에서도 LSE 명령을 어셈블하게 한다 (실행은 FEAT_LSE 가 있어야). */
#define LSE ".arch_extension lse\n\t"

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#define HB_ACQUIRE(p) __tsan_acquire((void *)(p))
#define HB_RELEASE(p) __tsan_release((void *)(p))
#endif
#endif
#ifndef HB_ACQUIRE
#define HB_ACQUIRE(p) ((void)(p))
#define HB_RELEASE(p) ((void)(p))
#endif

static inline void cpu_relax(void) { __asm__ volatile("yield" ::: "memory"); }

typedef struct { uint32_t v; } lock_t;       /* 0 = 풀림, 1 = 잠김 */
#define LOCK_INIT { 0u }

/* ======================= Part A — LL/SC (exclusive monitor) ======================= */

/* *p += v 를 원자적으로, 더하기 전 값을 반환. LDAXR(acquire) / STLXR(release) 루프 = C11 seq_cst RMW 매핑. */
static uint32_t llsc_fetch_add(uint32_t *p, uint32_t v) {
    (void)p; (void)v;
    return 0; /* TODO: 1: ldaxr / add / stlxr / cbnz 1b */
}

/* strong CAS: *p == expected 면 desired 로 바꾼다. 항상 "본 값" 을 반환 (== expected 면 성공).
 * 비교 실패 경로는 STLXR 없이 빠지므로 CLREX 로 모니터를 닫는다. STLXR 실패(가짜 실패)는 안에서 재시도. */
static uint32_t llsc_cas(uint32_t *p, uint32_t expected, uint32_t desired) {
    (void)p; (void)expected; (void)desired;
    return 0xDEADBEEFu; /* TODO: ldaxr / cmp / b.ne → clrex, stlxr 실패면 재시도 */
}

/* 한 번만 판단: 풀려 있으면 잡고 true, 잠겨 있으면 false. (STXR 가짜 실패는 재시도 — 풀린 락을 놓치지 않게) */
static bool llsc_trylock(lock_t *l) {
    (void)l;
    return false; /* TODO: ldaxr; 0 이면 stxr 1 (실패면 재시도), 아니면 clrex */
}

/* 잡을 때까지. 빠른 경로(풀려 있음)는 WFE 없이 바로 잡는다. 잠겨 있으면 WFE 로 잠든다: 방금 LDAXR 가
 * 모니터를 걸어 두었으므로 소유자의 해제 store 가 모니터를 지우며 이벤트를 만들고 WFE 가 깬다
 * (해제가 LDAXR 와 WFE 사이에 와도 이벤트 레지스터가 이미 서 있어 WFE 가 바로 통과 → 놓치지 않음). */
static void llsc_lock(lock_t *l) {
    (void)l; /* TODO: trylock 과 같은 루프 + 잠겨 있으면 wfe 후 다시 */
}

/* 해제: STLR(release) 로 0. 임계 구역 안의 쓰기가 이 store 보다 먼저 보인다. */
static void llsc_unlock(lock_t *l) {
    (void)l; /* TODO: stlr wzr */
}

/* ======================= Part B — LSE (ARMv8.1 FEAT_LSE) + 경계 사례 ======================= */

/* 명령 하나: LDADDAL Ws, Wt, [Xn]  — Wt = 옛 값, [Xn] += Ws, acquire+release */
static uint32_t lse_fetch_add(uint32_t *p, uint32_t v) {
    (void)p; (void)v;
    return 0; /* TODO: ldaddal */
}

/* CASAL Ws, Wt, [Xn]: Ws 에 expected 를 넣고 부르면 Ws 에 "본 값" 이 돌아온다. */
static uint32_t lse_cas(uint32_t *p, uint32_t expected, uint32_t desired) {
    (void)p; (void)expected; (void)desired;
    return 0xDEADBEEFu; /* TODO: casal */
}

/* SWPAL Ws, Wt, [Xn]: [Xn] = Ws, Wt = 옛 값. */
static uint32_t lse_swap(uint32_t *p, uint32_t v) {
    (void)p; (void)v;
    return 0; /* TODO: swpal */
}

/* LSE 스핀락: SWPA(acquire) 로 1 을 넣어 옛 값이 0 이면 획득. 실패하면 LDXR+WFE 로 "읽으며 잠들기"
 * (TTAS) 후 다시 SWPA. WFE 로 깨려면 모니터가 필요하므로 대기 루프는 LSE 시대에도 LDXR 를 쓴다.
 * (대기 루프는 상태를 바꾸지 않으므로 LDXR 뒤에 STXR/CLREX 가 없어도 된다 — 다음 SWPA 가 진짜 시도.) */
static void lse_lock(lock_t *l) {
    (void)l; /* TODO: swpa 루프 + ldxr/wfe 대기 */
}
static void lse_unlock(lock_t *l) { llsc_unlock(l); }   /* 해제는 둘 다 STLR WZR */

/* 64비트 판: X 레지스터 (%x). 32비트 경계를 넘는 carry 가 원자적으로 처리되는지 본다. */
static uint64_t llsc_fetch_add64(uint64_t *p, uint64_t v) {
    (void)p; (void)v;
    return 0; /* TODO: %x 레지스터로 ldaxr / add / stlxr */
}

/* weak CAS: 시도 한 번 (STLXR 실패도 그대로 false = 가짜 실패). 실패하면 *expected 에 본 값을 쓴다
 * — C11 atomic_compare_exchange_weak 와 같은 계약. 반드시 루프 안에서 쓴다. */
static bool llsc_cas_weak(uint32_t *p, uint32_t *expected, uint32_t desired) {
    (void)p; (void)expected; (void)desired;
    return false; /* TODO: 시도 한 번, 실패하면 *expected = 본 값 */
}

/* ======================= Part C — DMA 버퍼 풀 + 티켓 락 ======================= */
#define POOL_N 3u                                  /* 버퍼 3개 < 스레드 4개 → "풀 비었음" 경로도 돈다 */
typedef struct { uint32_t free_mask; } pool_t;     /* 비트 i = 1 이면 버퍼 i 가 비어 있음 */

/* 가장 낮은 1비트를 찾아 지우고 그 인덱스를 반환, 비었으면 -1.
 * LL/SC 의 장점: "찾기 + 지우기" 라는 임의의 계산을 LDAXR~STXR 사이 레지스터 연산으로 한 번에. */
static int pool_alloc(pool_t *p) {
    (void)p;
    return -1; /* TODO: ldaxr / rbit+clz / bic / stxr, 비었으면 clrex 후 -1 */
}

/* 반납: 버퍼 내용을 다 쓴 뒤 비트를 세운다 → release. 옛 값이 필요 없으니 STSETL (= LDSETL ..., WZR). */
static void pool_free(pool_t *p, int idx) {
    (void)p; (void)idx; /* TODO: stsetl (release) */
}

typedef struct { uint32_t next, owner; } ticket_t;  /* next: 번호표 발급기, owner: 지금 들어갈 번호 */
#define TICKET_INIT { 0u, 0u }

/* 번호표를 뽑고(LDADD, relaxed — 순서는 아래 LDAXR 가 책임) 내 차례까지 WFE 로 잔다. 내 번호를 반환.
 * 내 차례면 WFE 없이 통과. owner 를 쓰는 건 해제하는 소유자뿐 → 그 STLR 가 내 모니터를 지워 깨운다. */
static uint32_t ticket_lock(ticket_t *t) {
    (void)t;
    return 0xFFFFFFFFu; /* TODO: ldadd 로 번호표, ldaxr owner == 내 번호까지 wfe */
}

/* owner 는 락 소유자만 쓰므로 일반 LDR 로 읽고 +1 을 STLR(release) 로 게시. */
static void ticket_unlock(ticket_t *t) {
    (void)t; /* TODO: owner+1 을 stlr */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {        /* 단위 테스트가 실패하면 스트레스는 건너뛴다(=실패로 셈) */
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

#define NT 4                                       /* 스레드 수 */
#define ITERS 200000u
static atomic_int g_go;                            /* 출발 신호: 모든 스레드가 동시에 시작 */
static void wait_go(void) { while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax(); }
typedef struct { int id; int kind; } warg_t;
static void run_threads(void *(*fn)(void *), int kind) {
    pthread_t th[NT];
    warg_t a[NT];
    atomic_store(&g_go, 0);
    for (int i = 0; i < NT; i++) { a[i].id = i; a[i].kind = kind; pthread_create(&th[i], NULL, fn, &a[i]); }
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
}

/* --- Part A/B 스트레스: 카운터 --- */
enum { K_LLSC_ADD, K_LSE_ADD, K_LLSC_CAS, K_LSE_CAS, K_WEAK_CAS, K_MIXED, K_ADD64 };
static uint32_t g_cnt;                             /* asm 으로만 접근 (join 뒤에 읽음) */
static uint64_t g_cnt64;
static void *count_worker(void *arg) {
    const warg_t *a = arg;
    wait_go();
    for (uint32_t i = 0; i < ITERS; i++) {
        switch (a->kind) {
        case K_LLSC_ADD: (void)llsc_fetch_add(&g_cnt, 1u); break;
        case K_LSE_ADD:  (void)lse_fetch_add(&g_cnt, 1u); break;
        case K_LLSC_CAS: {                         /* CAS 루프로 +1: 본 값을 다음 expected 로 */
            uint32_t cur = 0u, seen;
            while ((seen = llsc_cas(&g_cnt, cur, cur + 1u)) != cur) cur = seen;
            break;
        }
        case K_LSE_CAS: {
            uint32_t cur = 0u, seen;
            while ((seen = lse_cas(&g_cnt, cur, cur + 1u)) != cur) cur = seen;
            break;
        }
        case K_WEAK_CAS: {
            uint32_t cur = 0u;
            while (!llsc_cas_weak(&g_cnt, &cur, cur + 1u)) { }
            break;
        }
        case K_MIXED:                              /* 같은 변수에 LL/SC 와 LSE 를 섞어도 원자적 */
            if (a->id & 1) (void)lse_fetch_add(&g_cnt, 1u); else (void)llsc_fetch_add(&g_cnt, 1u);
            break;
        case K_ADD64: (void)llsc_fetch_add64(&g_cnt64, 1u); break;
        }
    }
    return NULL;
}

/* --- Part A/B 스트레스: 락 --- */
enum { K_LLSC_LOCK, K_LSE_LOCK };
static lock_t g_lock = LOCK_INIT;
static struct { uint64_t a, b; uint32_t inside, viol; } g_cs;   /* 일반 변수 — 락으로만 보호 */
static void *lock_worker(void *arg) {
    const warg_t *a = arg;
    wait_go();
    for (uint32_t i = 0; i < ITERS; i++) {
        if (a->kind == K_LLSC_LOCK) llsc_lock(&g_lock); else lse_lock(&g_lock);
        HB_ACQUIRE(&g_lock);
        if (++g_cs.inside != 1u) g_cs.viol++;     /* 동시에 두 명이 들어오면 inside 가 2 */
        g_cs.a += 1u;
        g_cs.b += 3u;
        g_cs.inside--;
        HB_RELEASE(&g_lock);
        if (a->kind == K_LLSC_LOCK) llsc_unlock(&g_lock); else lse_unlock(&g_lock);
    }
    return NULL;
}

/* --- Part C 스트레스 --- */
#define POOL_ITERS 100000u
#define TK_ITERS   50000u
static pool_t g_pool;
static struct { uint32_t owner; uint32_t data[7]; } g_buf[POOL_N];   /* 버퍼 내용: 일반 변수 */
static uint32_t g_allocs[NT], g_empty[NT], g_pool_bad[NT];
static void *pool_worker(void *arg) {
    const warg_t *a = arg;
    uint32_t me = (uint32_t)a->id + 1u;            /* 0 = 아무도 안 가짐 */
    wait_go();
    for (uint32_t i = 0; i < POOL_ITERS; i++) {
        int idx;
        while ((idx = pool_alloc(&g_pool)) < 0) { g_empty[a->id]++; cpu_relax(); }
        HB_ACQUIRE(&g_pool);
        if (g_buf[idx].owner != 0u) g_pool_bad[a->id]++;          /* 다른 코어가 쓰는 중인 버퍼를 받았다 */
        g_buf[idx].owner = me;
        for (int k = 0; k < 7; k++) g_buf[idx].data[k] = me * 1000003u + i + (uint32_t)k;
        for (int k = 0; k < 7; k++) if (g_buf[idx].data[k] != me * 1000003u + i + (uint32_t)k) g_pool_bad[a->id]++;
        if (g_buf[idx].owner != me) g_pool_bad[a->id]++;
        g_buf[idx].owner = 0u;
        g_allocs[a->id]++;
        HB_RELEASE(&g_pool);
        pool_free(&g_pool, idx);
    }
    return NULL;
}
static ticket_t g_tk = TICKET_INIT;
static uint32_t g_tk_expect, g_tk_order_bad, g_tk_count;      /* 일반 변수 — 티켓 락으로만 보호 */
static void *ticket_worker(void *arg) {
    (void)arg;
    wait_go();
    for (uint32_t i = 0; i < TK_ITERS; i++) {
        uint32_t my = ticket_lock(&g_tk);
        HB_ACQUIRE(&g_tk);
        if (my != g_tk_expect) g_tk_order_bad++;  /* 번호표 순서대로 들어왔나 (FIFO) */
        g_tk_expect++;
        g_tk_count++;
        HB_RELEASE(&g_tk);
        ticket_unlock(&g_tk);
    }
    return NULL;
}

static bool host_has_lse(void) {
#if defined(__APPLE__)
    int v = 0;
    size_t n = sizeof v;
    return sysctlbyname("hw.optional.arm.FEAT_LSE", &v, &n, NULL, 0) == 0 && v == 1;
#else
    return true;                                   /* Linux: getauxval(AT_HWCAP) & HWCAP_ATOMICS 로 확인 */
#endif
}

int main(void) {
    if (!host_has_lse()) { printf("FEAT_LSE not reported — LSE parts would SIGILL\n"); return 1; }

    /* ---------- Part A: 단위 ---------- */
    int f0 = FAILS();
    uint32_t x = 5u;
    CHECK(llsc_fetch_add(&x, 3u) == 5u && x == 8u);
    CHECK(llsc_cas(&x, 8u, 100u) == 8u && x == 100u);          /* 성공: 본 값 == expected */
    CHECK(llsc_cas(&x, 8u, 7u) == 100u && x == 100u);          /* 실패: 메모리 그대로, 현재 값 반환 */
    lock_t l = LOCK_INIT;
    CHECK(llsc_trylock(&l) && l.v == 1u);                      /* 풀린 락은 잡힌다 */
    CHECK(!llsc_trylock(&l));                                  /* 잡힌 락은 실패 (CLREX 경로) */
    llsc_unlock(&l);
    CHECK(l.v == 0u && llsc_trylock(&l));
    llsc_unlock(&l);
    llsc_lock(&l);                                             /* 풀린 락에서 바로 돌아온다 */
    CHECK(l.v == 1u && !llsc_trylock(&l));
    llsc_unlock(&l);
    /* ---------- Part A: 스트레스 ---------- */
    if (FAILS() == f0) {
        g_cnt = 0u; run_threads(count_worker, K_LLSC_ADD);
        CHECK(g_cnt == NT * ITERS);
        g_cnt = 0u; run_threads(count_worker, K_LLSC_CAS);
        CHECK(g_cnt == NT * ITERS);
        memset(&g_cs, 0, sizeof g_cs); run_threads(lock_worker, K_LLSC_LOCK);
        CHECK(g_cs.a == (uint64_t)NT * ITERS && g_cs.b == 3u * g_cs.a);
        CHECK(g_cs.viol == 0u && g_lock.v == 0u);
    } else skip(4, "Part A");

    /* ---------- Part B: 단위 (경계 사례 포함) ---------- */
    int f1 = FAILS();
    x = 0xFFFFFFFFu;
    CHECK(lse_fetch_add(&x, 1u) == 0xFFFFFFFFu && x == 0u);    /* 32비트 wrap */
    x = 0xFFFFFFFFu;
    CHECK(llsc_fetch_add(&x, 2u) == 0xFFFFFFFFu && x == 1u);
    static const uint32_t cas_tab[][3] = {                      /* {초기값, expected, desired} */
        {0u, 0u, 1u}, {1u, 0u, 2u}, {7u, 7u, 7u}, {0xFFFFFFFFu, 0xFFFFFFFFu, 0u}, {0x80000000u, 0u, 5u},
    };
    int cas_ok = 0;
    for (size_t i = 0; i < sizeof cas_tab / sizeof cas_tab[0]; i++) {   /* LL/SC 와 LSE 가 같은 결과 */
        uint32_t m1 = cas_tab[i][0], m2 = cas_tab[i][0];
        uint32_t r1 = llsc_cas(&m1, cas_tab[i][1], cas_tab[i][2]);
        uint32_t r2 = lse_cas(&m2, cas_tab[i][1], cas_tab[i][2]);
        uint32_t want_m = cas_tab[i][0] == cas_tab[i][1] ? cas_tab[i][2] : cas_tab[i][0];
        cas_ok += r1 == cas_tab[i][0] && r2 == cas_tab[i][0] && m1 == want_m && m2 == want_m;
    }
    CHECK(cas_ok == 5);
    x = 3u;
    CHECK(lse_swap(&x, 9u) == 3u && x == 9u);
    uint64_t y = 0xFFFFFFFFull;
    CHECK(llsc_fetch_add64(&y, 1u) == 0xFFFFFFFFull && y == 0x100000000ull);   /* 32비트 carry */
    uint32_t e = 4u;
    x = 5u;
    CHECK(!llsc_cas_weak(&x, &e, 6u) && e == 5u && x == 5u);    /* 값 불일치: false + expected 갱신 */
    int tries = 0;
    while (!llsc_cas_weak(&x, &e, 6u) && ++tries < 1000) { }    /* 일치: 가짜 실패가 있어도 곧 성공 */
    CHECK(x == 6u && tries < 1000);
    l.v = 0u;
    lse_lock(&l);
    CHECK(l.v == 1u && !llsc_trylock(&l));                     /* LSE 로 잡은 락을 LL/SC 가 존중 */
    lse_unlock(&l);
    CHECK(l.v == 0u);
    /* ---------- Part B: 스트레스 ---------- */
    if (FAILS() == f1) {
        g_cnt = 0u; run_threads(count_worker, K_LSE_ADD);
        CHECK(g_cnt == NT * ITERS);
        g_cnt = 0u; run_threads(count_worker, K_LSE_CAS);
        CHECK(g_cnt == NT * ITERS);
        g_cnt = 0u; run_threads(count_worker, K_WEAK_CAS);
        CHECK(g_cnt == NT * ITERS);
        g_cnt = 0u; run_threads(count_worker, K_MIXED);
        CHECK(g_cnt == NT * ITERS);
        g_cnt64 = 0xFFFFFFFFull - 1000u; run_threads(count_worker, K_ADD64);
        CHECK(g_cnt64 == 0xFFFFFFFFull - 1000u + (uint64_t)NT * ITERS);
        memset(&g_cs, 0, sizeof g_cs); run_threads(lock_worker, K_LSE_LOCK);
        CHECK(g_cs.a == (uint64_t)NT * ITERS && g_cs.b == 3u * g_cs.a);
        CHECK(g_cs.viol == 0u && g_lock.v == 0u);
    } else skip(7, "Part B");

    /* ---------- Part C: 단위 ---------- */
    int f2 = FAILS();
    pool_t p = { (1u << POOL_N) - 1u };
    int a0 = pool_alloc(&p), a1 = pool_alloc(&p), a2 = pool_alloc(&p), a3 = pool_alloc(&p);
    CHECK(a0 == 0 && a1 == 1 && a2 == 2 && a3 == -1 && p.free_mask == 0u);   /* 낮은 번호부터, 비면 -1 */
    pool_free(&p, 1);
    CHECK(p.free_mask == 2u && pool_alloc(&p) == 1 && p.free_mask == 0u);
    pool_free(&p, 2); pool_free(&p, 0);
    CHECK(p.free_mask == 5u && pool_alloc(&p) == 0);
    ticket_t t = TICKET_INIT;
    uint32_t t0 = ticket_lock(&t);
    ticket_unlock(&t);
    uint32_t t1 = ticket_lock(&t);
    CHECK(t0 == 0u && t1 == 1u && t.next == 2u && t.owner == 1u);
    ticket_unlock(&t);
    CHECK(t.owner == 2u);
    /* ---------- Part C: 스트레스 ---------- */
    if (FAILS() == f2) {
        g_pool.free_mask = (1u << POOL_N) - 1u;
        run_threads(pool_worker, 0);
        uint32_t allocs = 0, empty = 0, bad = 0;
        for (int i = 0; i < NT; i++) { allocs += g_allocs[i]; empty += g_empty[i]; bad += g_pool_bad[i]; }
        printf("Part C pool: allocs=%u empty_retries=%u\n", allocs, empty);
        CHECK(allocs == NT * POOL_ITERS && bad == 0u);                    /* 이중 소유·찢어진 버퍼 0 */
        CHECK(g_pool.free_mask == (1u << POOL_N) - 1u);                   /* 전부 반납됨 */
        run_threads(ticket_worker, 0);
        CHECK(g_tk_count == NT * TK_ITERS && g_tk_order_bad == 0u);       /* 정확 + 번호표 순서 */
        CHECK(g_tk.next == NT * TK_ITERS && g_tk.owner == NT * TK_ITERS);
    } else skip(4, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
