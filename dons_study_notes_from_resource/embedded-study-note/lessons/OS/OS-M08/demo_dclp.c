/* OS-M08 demo_dclp.c — 지연 초기화(lazy init) 5가지를 라운드마다 "처음부터" 경합시켜 본다.
 *
 *   -DV=0  naive       : if (!p) { init; p = obj; }                 락 없음 → 중복 init
 *   -DV=1  dclp_plain  : 일반 포인터로 double-checked locking      → 데이터 레이스 (UB)
 *   -DV=2  dclp_relaxed: _Atomic 포인터지만 relaxed load/store     → 포인터는 OK, 내용은 레이스
 *   -DV=3  dclp_acqrel : 빠른 경로 acquire load + 락 안 release store  (정답)
 *   -DV=4  once        : pthread_once                                (정답, 라운드마다 새 once 객체)
 *
 * 라운드: main 이 객체를 0 으로 지우고 포인터를 NULL 로 되돌린 뒤, 워커 NT 개를 동시에 풀어 준다.
 * 워커는 0~DELAY 회 무작위로 쉬었다가 get() 을 부르고 받은 객체를 검사한다 (magic + 체크섬).
 *
 *   cc -std=c11 -O2 -pthread -DV=1 demo_dclp.c -o dclp && ./dclp
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DV=1 -DROUNDS=50 demo_dclp.c -o dclp_t && ./dclp_t
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef V
#define V 3
#endif
#ifndef NT
#define NT 6
#endif
#ifndef ROUNDS
#if V == 4
#define ROUNDS 1024     /* V=4 는 라운드마다 정적 once 객체가 하나씩 필요 → 1024 상한 */
#else
#define ROUNDS 200000
#endif
#endif
#ifndef DELAY
#define DELAY 300u      /* 도착 시각을 0~DELAY 회 빈 루프만큼 흩뜨린다 (작을수록 init 직후에 몰린다) */
#endif
#define MAGIC 0xC0FFEE11u
#define NTAB 64

typedef struct { uint32_t magic; uint32_t tab[NTAB]; uint32_t sum; } table_t;
static table_t storage;
static atomic_int init_calls;

static void table_init(void) {          /* 무거운 초기화라고 치자: CRC 표, 보정 계수, 레지스터 기본값 ... */
    atomic_fetch_add_explicit(&init_calls, 1, memory_order_relaxed);
    uint32_t s = 0;
    for (int i = 0; i < NTAB; i++) { storage.tab[i] = (uint32_t)i * 2654435761u; s += storage.tab[i]; }
    storage.sum = s;
    storage.magic = MAGIC;
}
static int table_ok(const table_t *t) {
    if (!t || t->magic != MAGIC) return 0;
    uint32_t s = 0;
    for (int i = 0; i < NTAB; i++) s += t->tab[i];
    return s == t->sum;
}

#if V == 1 || V == 2 || V == 3
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
#endif
#if V == 0 || V == 1
static table_t *g_p;
static void reset_ptr(void) { g_p = NULL; }
#elif V == 2 || V == 3
static _Atomic(table_t *) g_p;
static void reset_ptr(void) { atomic_store(&g_p, NULL); }
#if V == 2
#define LD memory_order_relaxed
#define ST memory_order_relaxed
#else
#define LD memory_order_acquire
#define ST memory_order_release
#endif
#else
#define O1 PTHREAD_ONCE_INIT
#define O2 O1, O1
#define O4 O2, O2
#define O8 O4, O4
#define O16 O8, O8
#define O32 O16, O16
#define O64 O32, O32
#define O128 O64, O64
#define O256 O128, O128
#define O512 O256, O256
#define O1024 O512, O512
static pthread_once_t g_once[1024] = { O1024 };
static int g_round;                    /* 라운드 사이(워커가 모두 쉬는 동안)에만 바뀐다 */
static void reset_ptr(void) { }
_Static_assert(ROUNDS <= 1024, "V=4 needs one once object per round");
#endif

static const table_t *get(void) {
#if V == 0
    if (!g_p) { table_init(); g_p = &storage; }
    return g_p;
#elif V == 1
    if (!g_p) {
        pthread_mutex_lock(&mu);
        if (!g_p) { table_init(); g_p = &storage; }
        pthread_mutex_unlock(&mu);
    }
    return g_p;
#elif V == 2 || V == 3
    table_t *p = atomic_load_explicit(&g_p, LD);
    if (!p) {
        pthread_mutex_lock(&mu);
        p = atomic_load_explicit(&g_p, memory_order_relaxed);
        if (!p) { table_init(); p = &storage; atomic_store_explicit(&g_p, p, ST); }
        pthread_mutex_unlock(&mu);
    }
    return p;
#else
    pthread_once(&g_once[g_round], table_init);
    return &storage;
#endif
}

/* ---- 라운드 동기화: atomic 세대 카운터 (macOS 엔 pthread_barrier_t 가 없다) ---- */
static atomic_uint start_gen, done_cnt;
static atomic_long bad, rounds_multi;
static uint32_t xs(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static void *worker(void *arg) {
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)(uintptr_t)arg;
    for (unsigned r = 1; r <= ROUNDS; r++) {
        while (atomic_load(&start_gen) != r) { }
        for (volatile uint32_t d = xs(&seed) % DELAY; d; d--) { }       /* 도착 시각 흩뜨리기 */
        if (!table_ok(get())) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&done_cnt, 1);
    }
    return NULL;
}

int main(void) {
    pthread_t th[NT];
    for (intptr_t i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, (void *)i);
    for (unsigned r = 1; r <= ROUNDS; r++) {
        memset(&storage, 0, sizeof storage);                 /* 새 부팅처럼: 객체도 포인터도 처음 상태 */
        reset_ptr();
        atomic_store(&init_calls, 0);
#if V == 4
        g_round = (int)r - 1;
#endif
        atomic_store(&done_cnt, 0);
        atomic_store(&start_gen, r);                          /* seq_cst: 위 초기화가 워커에게 보인다 */
        while (atomic_load(&done_cnt) != NT) { }
        if (atomic_load(&init_calls) != 1) atomic_fetch_add(&rounds_multi, 1);
    }
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    static const char *name[] = { "naive", "dclp_plain", "dclp_relaxed", "dclp_acqrel", "once" };
    printf("V=%d %-12s rounds=%d threads=%d  rounds_with_init!=1: %ld  bad_reads: %ld\n",
           V, name[V], ROUNDS, NT, atomic_load(&rounds_multi), atomic_load(&bad));
    return 0;
}
