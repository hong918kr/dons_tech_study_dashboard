/* OS-M08 추가 연습 — 한 번에 검증
 *   연습 1  fence 버전 메시지 패싱: relaxed 접근 + atomic_thread_fence(release / acquire)
 *   연습 2  CAS "경주 후 승자만 게시" 지연 초기화 (락 없음, 진 쪽은 자기 객체를 버린다)
 *   연습 3  pthread_once 로 실패할 수 있는 초기화를 감싸면: 오류가 영구히 고정된다 (재시도 불가)
 *   연습 4  relaxed 로 충분한 stop 플래그 vs release/acquire 가 필요한 done+결과
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

/* ---------------- 연습 1 — fence 버전 ---------------- */
typedef struct { uint32_t seq, a, b, c; } payload_t;
typedef struct { _Alignas(64) payload_t pl; _Alignas(64) atomic_uint flag; } slot_t;
static payload_t payload_make(uint32_t v) {
    payload_t p = { v, v * 0x9E3779B1u, (v << 13) | (v >> 19), 0 };
    p.c = p.seq ^ p.a ^ p.b;
    return p;
}
static bool payload_is(const payload_t *p, uint32_t v) {
    payload_t w = payload_make(v);
    return p->seq == w.seq && p->a == w.a && p->b == w.b && p->c == w.c;
}
/* ThreadSanitizer 는 단독 펜스(atomic_thread_fence)를 동기화로 모델링하지 않는다 → fence 버전은
 * TSan 빌드에서 거짓 양성(false positive)이 난다 (md 에 실제 보고). 그래서 TSan 빌드에서만 같은 의미의
 * release store / acquire load 로 바꾼다. -DFORCE_FENCE 로 TSan 에서도 fence 버전을 강제할 수 있다. */
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) && !defined(FORCE_FENCE)
#define USE_OP_FORM 1
#endif
#endif
#ifndef USE_OP_FORM
static void mp_post_fence(slot_t *s, uint32_t v) {
    s->pl = payload_make(v);
    atomic_thread_fence(memory_order_release);                     /* 앞의 모든 쓰기 → 뒤의 store */
    atomic_store_explicit(&s->flag, v, memory_order_relaxed);
}
static bool mp_take_fence(slot_t *s, uint32_t v, payload_t *out) {
    if (atomic_load_explicit(&s->flag, memory_order_relaxed) != v) return false;  /* 싸게 폴링 */
    atomic_thread_fence(memory_order_acquire);                     /* 본 뒤에만 한 번 */
    *out = s->pl;
    return true;
}
#else
static void mp_post_fence(slot_t *s, uint32_t v) {
    s->pl = payload_make(v);
    atomic_store_explicit(&s->flag, v, memory_order_release);
}
static bool mp_take_fence(slot_t *s, uint32_t v, payload_t *out) {
    if (atomic_load_explicit(&s->flag, memory_order_acquire) != v) return false;
    *out = s->pl;
    return true;
}
#endif
#define NSLOT1 512u
#define ROUNDS1 200u
static slot_t g_s1[NSLOT1];
static atomic_uint g_go1;
static void *w1(void *arg) {
    (void)arg;
    for (unsigned k = 1; k <= ROUNDS1; k++) {
        while (atomic_load_explicit(&g_go1, memory_order_acquire) != k) cpu_relax();
        for (unsigned i = 0; i < NSLOT1; i++) mp_post_fence(&g_s1[i], (k - 1u) * NSLOT1 + i + 1u);
    }
    return NULL;
}

/* ---------------- 연습 2 — CAS 경주 지연 초기화 ---------------- */
typedef struct { uint32_t magic, tab[16]; } obj_t;
static atomic_int g_allocs, g_frees;
static obj_t *obj_new(void) {
    obj_t *o = malloc(sizeof *o);
    if (!o) abort();
    atomic_fetch_add_explicit(&g_allocs, 1, memory_order_relaxed);
    for (int i = 0; i < 16; i++) o->tab[i] = (uint32_t)i * 40503u;
    o->magic = 0xA11CE5u;
    return o;
}
static bool obj_ok(const obj_t *o) {
    if (!o || o->magic != 0xA11CE5u) return false;
    for (int i = 0; i < 16; i++) if (o->tab[i] != (uint32_t)i * 40503u) return false;
    return true;
}
static const obj_t *lazy_cas_get(_Atomic(obj_t *) *pp) {
    obj_t *p = atomic_load_explicit(pp, memory_order_acquire);
    if (p) return p;
    obj_t *mine = obj_new();                                       /* 여러 명이 동시에 만들 수 있다 */
    obj_t *expected = NULL;
    if (atomic_compare_exchange_strong_explicit(pp, &expected, mine,
                                                memory_order_release,   /* 성공: 내 객체 게시 */
                                                memory_order_acquire))  /* 실패: 승자 객체 읽기 */
        return mine;
    free(mine);                                                    /* 졌다: 아무도 못 본 내 것은 버린다 */
    atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
    return expected;
}
#define NT2 6
#define ROUNDS2 2000u
static _Atomic(obj_t *) g_p2;
static atomic_uint g_gen2, g_done2;
static const obj_t *g_got2[NT2];
static void *w2(void *arg) {
    int id = (int)(intptr_t)arg;
    for (unsigned r = 1; r <= ROUNDS2; r++) {
        while (atomic_load(&g_gen2) != r) cpu_relax();
        g_got2[id] = lazy_cas_get(&g_p2);
        atomic_fetch_add(&g_done2, 1u);
    }
    return NULL;
}

/* ---------------- 연습 3 — pthread_once 와 실패 ---------------- */
static int g_probe_calls3, g_err3;
static uint32_t g_cfg3;
static int probe3(void) { return ++g_probe_calls3 == 1 ? -5 : 0; }    /* 첫 프로브만 실패 */
static pthread_once_t g_once3 = PTHREAD_ONCE_INIT;
static void init3(void) { g_err3 = probe3(); if (g_err3 == 0) g_cfg3 = 0x1234u; }
static int cfg_once_get(uint32_t *out) {      /* 0 이면 성공. pthread_once 는 init 을 두 번 다시 부르지 않는다 */
    pthread_once(&g_once3, init3);
    if (g_err3) return g_err3;
    *out = g_cfg3;
    return 0;
}

/* ---------------- 연습 4 — stop 플래그와 done+결과 ---------------- */
static atomic_bool g_stop4;                  /* 데이터 없음: "멈춰" 라는 뜻만 → relaxed 로 충분 */
static atomic_bool g_done4;                  /* 결과를 함께 넘긴다 → release / acquire */
static atomic_ulong g_progress4;             /* 진행 카운터 (relaxed 통계) */
static unsigned long g_result4;              /* 일반 변수: done 의 release/acquire 로 보호 */
static void *w4(void *arg) {
    (void)arg;
    unsigned long n = 0;
    while (!atomic_load_explicit(&g_stop4, memory_order_relaxed)) {
        n++;
        atomic_fetch_add_explicit(&g_progress4, 1u, memory_order_relaxed);
    }
    g_result4 = n;                                                 /* 1) 결과 */
    atomic_store_explicit(&g_done4, true, memory_order_release);   /* 2) 완료 게시 */
    return NULL;
}

int main(void) {
    /* 연습 1 */
    pthread_t t;
    payload_t q;
    pthread_create(&t, NULL, w1, NULL);
    unsigned long bad1 = 0, miss1 = 0;
    for (unsigned k = 1; k <= ROUNDS1; k++) {
        atomic_store_explicit(&g_go1, k, memory_order_release);
        for (unsigned i = 0; i < NSLOT1; i++) {
            uint32_t v = (k - 1u) * NSLOT1 + i + 1u;
            unsigned idle = 0;
            while (!mp_take_fence(&g_s1[i], v, &q) && ++idle < 100000000u) cpu_relax();
            if (idle >= 100000000u) miss1++;
            else if (!payload_is(&q, v)) bad1++;
        }
        while (atomic_load_explicit(&g_s1[NSLOT1 - 1u].flag, memory_order_acquire) != k * NSLOT1) cpu_relax();
    }
    pthread_join(t, NULL);
    CHECK(miss1 == 0u);
    CHECK(bad1 == 0u);                                   /* fence–fence 동기화로 payload 온전 */

    /* 연습 2 */
    pthread_t th[NT2];
    for (intptr_t i = 0; i < NT2; i++) pthread_create(&th[i], NULL, w2, (void *)i);
    unsigned leak = 0, differ = 0;
    obj_t *prev = NULL;
    for (unsigned r = 1; r <= ROUNDS2; r++) {
        free(prev);                                      /* 지난 라운드 게시본 정리 (워커는 모두 쉬는 중) */
        atomic_store(&g_p2, NULL);
        atomic_store(&g_allocs, 0); atomic_store(&g_frees, 0);
        atomic_store(&g_done2, 0u);
        atomic_store(&g_gen2, r);
        while (atomic_load(&g_done2) != NT2) cpu_relax();
        prev = atomic_load(&g_p2);
        if (atomic_load(&g_allocs) - atomic_load(&g_frees) != 1) leak++;
        for (int i = 0; i < NT2; i++) if (g_got2[i] != prev || !obj_ok(g_got2[i])) differ++;
    }
    for (int i = 0; i < NT2; i++) pthread_join(th[i], NULL);
    free(prev);
    CHECK(leak == 0u);                                   /* 만든 것 - 버린 것 == 게시된 1 개 */
    CHECK(differ == 0u);                                 /* 모두 같은, 완성된 객체 */

    /* 연습 3 */
    uint32_t cfg = 0;
    CHECK(cfg_once_get(&cfg) == -5);
    CHECK(cfg_once_get(&cfg) == -5 && cfg_once_get(&cfg) == -5);   /* 하드웨어가 살아나도 계속 실패 */
    CHECK(g_probe_calls3 == 1 && cfg == 0u);                        /* init 은 단 한 번 */

    /* 연습 4 */
    pthread_create(&t, NULL, w4, NULL);
    while (atomic_load_explicit(&g_progress4, memory_order_relaxed) < 100000u) cpu_relax();
    atomic_store_explicit(&g_stop4, true, memory_order_relaxed);
    while (!atomic_load_explicit(&g_done4, memory_order_acquire)) cpu_relax();
    unsigned long res = g_result4;                       /* acquire 뒤라 레이스 아님 */
    unsigned long prog = atomic_load_explicit(&g_progress4, memory_order_relaxed);
    pthread_join(t, NULL);
    CHECK(res >= 100000u);
    CHECK(res == prog);                                  /* done 이전의 마지막 증가까지 보인다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
