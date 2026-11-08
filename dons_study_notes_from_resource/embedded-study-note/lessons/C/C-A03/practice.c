/* C-A03 추가 연습 — 한 번에 검증
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 *   연습 1  DMA 버퍼 참조 카운트: fetch_sub(release) + 마지막이면 acquire fence
 *   연습 2  이벤트 플래그 그룹: fetch_or(release) / exchange(0, acquire)
 *   연습 3  크레딧(세마포어 try_take): CAS 루프로 "0 밑으로 안 내려가는" 감소
 *   연습 4  Peterson 락: store→load 순서가 필요하면 seq_cst
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#define NT 4

/* ---------------- 연습 1: 참조 카운트 ---------------- */
typedef struct { atomic_uint ref; uint32_t data[16]; } dmabuf_t;
static void buf_get(dmabuf_t *b) {
    atomic_fetch_add_explicit(&b->ref, 1u, memory_order_relaxed);   /* 이미 참조를 가진 쪽만 부른다 */
}
/* 참조를 놓는다. 마지막 참조였으면 true (호출자가 버퍼를 풀에 반납/재사용해도 안전). */
static bool buf_put(dmabuf_t *b) {
    if (atomic_fetch_sub_explicit(&b->ref, 1u, memory_order_release) != 1u)
        return false;                                  /* 내 읽기/쓰기를 release 로 "넘기고" 끝 */
    atomic_thread_fence(memory_order_acquire);         /* 다른 모든 스레드의 release 를 받아 온다 */
    return true;
}
static dmabuf_t g_buf;
static atomic_uint g_last_count, g_bad_reads;
static void *ref_worker(void *arg) {
    (void)arg;
    for (int k = 0; k < 20000; k++) {                  /* 기본 참조 1개 위에 get/put 반복 */
        buf_get(&g_buf);
        uint32_t s = 0;
        for (int i = 0; i < 16; i++) s += g_buf.data[i];
        if (s != 16u * 0xABu) atomic_fetch_add(&g_bad_reads, 1u);
        if (buf_put(&g_buf)) atomic_fetch_add(&g_last_count, 1u);   /* 여기선 절대 마지막이 아니다 */
    }
    if (buf_put(&g_buf)) {                             /* 기본 참조 반납: 정확히 한 스레드만 true */
        atomic_fetch_add(&g_last_count, 1u);
        memset(g_buf.data, 0xEE, sizeof g_buf.data);   /* "free" — 다른 스레드의 읽기와 레이스 없어야 함 */
    }
    return NULL;
}

/* ---------------- 연습 2: 이벤트 플래그 ---------------- */
static void ev_post(atomic_uint *ev, unsigned bits) {
    atomic_fetch_or_explicit(ev, bits, memory_order_release);
}
static unsigned ev_take(atomic_uint *ev) {             /* 쌓인 비트를 전부 가져가고 0 으로 */
    return atomic_exchange_explicit(ev, 0u, memory_order_acquire);
}
#define NPOST 8
static atomic_uint g_ev;
static uint32_t g_slot[NPOST];                         /* 비트 k 를 올리기 전에 g_slot[k] 를 채운다 */
static void *poster(void *arg) {
    unsigned k = (unsigned)(uintptr_t)arg;
    g_slot[k] = 1000u + k;
    ev_post(&g_ev, 1u << k);
    return NULL;
}

/* ---------------- 연습 3: 크레딧 ---------------- */
static bool credit_take(atomic_uint *c) {
    unsigned cur = atomic_load_explicit(c, memory_order_relaxed);
    do {
        if (cur == 0u) return false;                   /* 0 이면 감소하지 않는다 (fetch_sub 는 못 함) */
    } while (!atomic_compare_exchange_weak_explicit(c, &cur, cur - 1u,
                                                    memory_order_acquire, memory_order_relaxed));
    return true;
}
static void credit_give(atomic_uint *c, unsigned n) {
    atomic_fetch_add_explicit(c, n, memory_order_release);
}
static atomic_uint g_credit, g_taken;
static void *credit_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 600; i++)
        if (credit_take(&g_credit)) atomic_fetch_add_explicit(&g_taken, 1u, memory_order_relaxed);
    return NULL;
}

/* ---------------- 연습 4: Peterson 락 (스레드 2개) ---------------- */
static atomic_bool p_want[2];
static atomic_int p_turn;
static void peterson_lock(int me) {
    int other = 1 - me;
    atomic_store(&p_want[me], true);                   /* seq_cst: 이 store 와 아래 load 의 순서가 핵심 */
    atomic_store(&p_turn, other);
    while (atomic_load(&p_want[other]) && atomic_load(&p_turn) == other)
        cpu_relax();
}
static void peterson_unlock(int me) {
    atomic_store_explicit(&p_want[me], false, memory_order_release);
}
static unsigned long g_pcount;
static void *peterson_worker(void *arg) {
    int me = (int)(intptr_t)arg;
    for (int i = 0; i < 200000; i++) {
        peterson_lock(me);
        g_pcount++;
        peterson_unlock(me);
    }
    return NULL;
}

int main(void) {
    pthread_t th[NPOST];

    /* 연습 1 */
    dmabuf_t b1 = { .data = { 0 } };
    atomic_init(&b1.ref, 1u);
    buf_get(&b1);
    CHECK(!buf_put(&b1) && atomic_load(&b1.ref) == 1u);
    CHECK(buf_put(&b1) && atomic_load(&b1.ref) == 0u);           /* 마지막 */
    for (int i = 0; i < 16; i++) g_buf.data[i] = 0xABu;
    atomic_init(&g_buf.ref, NT);                                 /* 스레드마다 기본 참조 1개 */
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, ref_worker, NULL);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    CHECK(atomic_load(&g_last_count) == 1u);                     /* 정확히 한 번만 "마지막" */
    CHECK(atomic_load(&g_bad_reads) == 0u && atomic_load(&g_buf.ref) == 0u);

    /* 연습 2 */
    atomic_uint ev = 0u;
    ev_post(&ev, 0x5u);
    ev_post(&ev, 0x4u);                                          /* 같은 비트는 합쳐진다 */
    CHECK(ev_take(&ev) == 0x5u && ev_take(&ev) == 0u);
    for (unsigned k = 0; k < NPOST; k++) pthread_create(&th[k], NULL, poster, (void *)(uintptr_t)k);
    unsigned seen = 0u, bad = 0u;
    while (seen != (1u << NPOST) - 1u) {
        unsigned got = ev_take(&g_ev);
        for (unsigned k = 0; k < NPOST; k++)
            if (got & (1u << k)) { bad += g_slot[k] != 1000u + k; seen |= 1u << k; }
    }
    for (unsigned k = 0; k < NPOST; k++) pthread_join(th[k], NULL);
    CHECK(bad == 0u && atomic_load(&g_ev) == 0u);                /* 비트를 봤으면 슬롯도 보인다 */

    /* 연습 3 */
    atomic_uint c = 2u;
    CHECK(credit_take(&c) && credit_take(&c) && !credit_take(&c) && atomic_load(&c) == 0u);
    credit_give(&c, 1u);
    CHECK(credit_take(&c) && atomic_load(&c) == 0u);
    atomic_init(&g_credit, 1000u);
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, credit_worker, NULL);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    CHECK(atomic_load(&g_taken) == 1000u && atomic_load(&g_credit) == 0u);   /* 2400 시도 중 정확히 1000 */

    /* 연습 4 */
    pthread_create(&th[0], NULL, peterson_worker, (void *)(intptr_t)0);
    pthread_create(&th[1], NULL, peterson_worker, (void *)(intptr_t)1);
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);
    CHECK(g_pcount == 400000ul);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
