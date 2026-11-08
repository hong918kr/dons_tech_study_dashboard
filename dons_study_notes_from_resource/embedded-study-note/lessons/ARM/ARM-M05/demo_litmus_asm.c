/* ARM-M05 demo_litmus_asm.c — MP / SB 리트머스를 arm64 어셈블리로 직접 돌려 "재배치 관찰 횟수" 를 센다
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_litmus_asm.c -o demo_litmus_asm
 *   ./demo_litmus_asm [iters_millions=20] [mp|sb|all]
 *
 * MP (message passing), 같은 두 칸을 계속 덮어쓰는 "연속" 버전:
 *   writer:  data = i ; [W 배리어] ; flag = i          (i = 1, 2, 3, ...)
 *   reader:  f = flag ; [R 배리어] ; d = data          → d < f 이면 "flag 는 새것, data 는 옛것" = 재배치 관찰
 * SB (store buffering):
 *   A: x = i ; [배리어] ; r1 = y        B: y = i ; [배리어] ; r2 = x
 *   같은 회차 i 에서 r1 < i && r2 < i 이면 두 store 가 모두 두 load 뒤로 밀린 것 = 순차 일관성(SC) 위반
 * 공유 변수는 asm 으로만 접근한다 (C 코드는 주소만 넘김) — 컴파일러가 순서를 바꿀 여지가 없고,
 * TSan 도 이 접근을 보지 못한다. macOS 에는 CPU 고정(affinity) API 가 없다 — QoS 힌트만 준다.
 * 결과는 실행마다 다르다: 관찰용이고 CHECK 에 쓰지 않는다.
 */
#define _DARWIN_C_SOURCE
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__aarch64__)
#error "arm64 전용"
#endif

static uint64_t ITERS = 20000000u;
static _Alignas(128) uint32_t g_data;
static _Alignas(128) uint32_t g_flag;
static _Alignas(128) uint32_t g_x;
static _Alignas(128) uint32_t g_y;
static uint32_t *g_r1, *g_r2;
static atomic_int g_go, g_ready;
static int g_affinity_kr = -1;

static void start_line(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   /* P 코어 쪽으로 가라는 "힌트" */
    atomic_fetch_add(&g_ready, 1);
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
}

/* ---------------- MP writer 3종 + reader 9종 (asm 본문 문자열) ---------------- */
#define W_PLAIN  "str %w[v], [%[d]]\n\t" "str %w[v], [%[f]]"
#define W_DMB    "str %w[v], [%[d]]\n\t" "dmb ish\n\t"   "str %w[v], [%[f]]"
#define W_DMBST  "str %w[v], [%[d]]\n\t" "dmb ishst\n\t" "str %w[v], [%[f]]"
#define W_STLR   "str %w[v], [%[d]]\n\t" "stlr %w[v], [%[f]]"

#define R_PLAIN  "ldr %w[rf], [%[f]]\n\t"  "ldr %w[rd], [%[d]]"
#define R_DMB    "ldr %w[rf], [%[f]]\n\t"  "dmb ish\n\t"   "ldr %w[rd], [%[d]]"
#define R_DMBLD  "ldr %w[rf], [%[f]]\n\t"  "dmb ishld\n\t" "ldr %w[rd], [%[d]]"
#define R_DMBST  "ldr %w[rf], [%[f]]\n\t"  "dmb ishst\n\t" "ldr %w[rd], [%[d]]"
#define R_LDAR   "ldar %w[rf], [%[f]]\n\t" "ldr %w[rd], [%[d]]"
#define R_LDAPR  "ldapr %w[rf], [%[f]]\n\t" "ldr %w[rd], [%[d]]"
#define R_ADDR   "ldr %w[rf], [%[f]]\n\t"  "eor %x[t], %x[rf], %x[rf]\n\t" "ldr %w[rd], [%[d], %x[t]]"
#define R_CTRL   "ldr %w[rf], [%[f]]\n\t"  "cbz %w[rf], 1f\n" "1:\n\t" "ldr %w[rd], [%[d]]"
#define R_CTRLISB "ldr %w[rf], [%[f]]\n\t" "cbz %w[rf], 1f\n" "1:\n\t" "isb\n\t" "ldr %w[rd], [%[d]]"

#define DEF_W(name, BODY)                                                         \
    static void *name(void *a) {                                                  \
        (void)a; start_line();                                                    \
        for (uint64_t i = 1; i <= ITERS; i++) {                                   \
            uint32_t v = (uint32_t)i;                                             \
            __asm__ __volatile__(BODY :: [d] "r"(&g_data), [f] "r"(&g_flag),      \
                                 [v] "r"(v) : "memory");                          \
        }                                                                         \
        return NULL;                                                              \
    }
#define DEF_R(name, BODY)                                                         \
    static void *name(void *a) {                                                  \
        uint64_t weak = 0; start_line();                                          \
        for (uint64_t i = 1; i <= ITERS; i++) {                                   \
            uint64_t f, d, t;                                                     \
            __asm__ __volatile__(BODY : [rf] "=&r"(f), [rd] "=&r"(d), [t] "=&r"(t)\
                                 : [f] "r"(&g_flag), [d] "r"(&g_data) : "memory");\
            (void)t;                                                              \
            weak += (d < f);                                                      \
        }                                                                         \
        *(uint64_t *)a = weak;                                                    \
        return NULL;                                                              \
    }
DEF_W(w_plain, W_PLAIN)  DEF_W(w_dmb, W_DMB)  DEF_W(w_dmbst, W_DMBST)  DEF_W(w_stlr, W_STLR)
DEF_R(r_plain, R_PLAIN)  DEF_R(r_dmb, R_DMB)  DEF_R(r_dmbld, R_DMBLD)  DEF_R(r_dmbst, R_DMBST)
DEF_R(r_ldar, R_LDAR)    DEF_R(r_ldapr, R_LDAPR) DEF_R(r_addr, R_ADDR) DEF_R(r_ctrl, R_CTRL)
DEF_R(r_ctrlisb, R_CTRLISB)

/* ---------------- SB 4종 ---------------- */
#define S_PLAIN  "str %w[v], [%[m]]\n\t"  "ldr %w[r], [%[o]]"
#define S_DMB    "str %w[v], [%[m]]\n\t"  "dmb ish\n\t" "ldr %w[r], [%[o]]"
#define S_DMBST  "str %w[v], [%[m]]\n\t"  "dmb ishst\n\t" "ldr %w[r], [%[o]]"
#define S_RCSC   "stlr %w[v], [%[m]]\n\t" "ldar %w[r], [%[o]]"
#define S_RCPC   "stlr %w[v], [%[m]]\n\t" "ldapr %w[r], [%[o]]"
#define DEF_S(name, BODY)                                                         \
    static void *name(void *a) {                                                  \
        int side = *(int *)a;                                                     \
        uint32_t *mine = side ? &g_y : &g_x, *other = side ? &g_x : &g_y;         \
        uint32_t *out = side ? g_r2 : g_r1;                                       \
        start_line();                                                             \
        for (uint64_t i = 1; i <= ITERS; i++) {                                   \
            uint32_t v = (uint32_t)i, r;                                          \
            __asm__ __volatile__(BODY : [r] "=&r"(r)                              \
                                 : [m] "r"(mine), [o] "r"(other), [v] "r"(v)      \
                                 : "memory");                                     \
            out[i] = r;                                                           \
        }                                                                         \
        return NULL;                                                              \
    }
DEF_S(s_plain, S_PLAIN) DEF_S(s_dmb, S_DMB) DEF_S(s_dmbst, S_DMBST) DEF_S(s_rcsc, S_RCSC) DEF_S(s_rcpc, S_RCPC)

static void go2(void *(*fa)(void *), void *aa, void *(*fb)(void *), void *ab) {
    pthread_t ta, tb;
    atomic_store(&g_go, 0);
    atomic_store(&g_ready, 0);
    pthread_create(&ta, NULL, fa, aa);
    pthread_create(&tb, NULL, fb, ab);
    while (atomic_load(&g_ready) != 2) { }
    atomic_store_explicit(&g_go, 1, memory_order_release);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
}

static void *probe_affinity(void *a) {           /* Apple Silicon 에서 affinity 태그가 받아들여지는지 */
    (void)a;
    thread_affinity_policy_data_t pol = { 1 };
    g_affinity_kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                                      (thread_policy_t)&pol, THREAD_AFFINITY_POLICY_COUNT);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) ITERS = (uint64_t)atoll(argv[1]) * 1000000u;
    const char *which = argc > 2 ? argv[2] : "all";
    int do_mp = which[0] != 's', do_sb = which[0] != 'm';
    pthread_t pt;
    pthread_create(&pt, NULL, probe_affinity, NULL);
    pthread_join(pt, NULL);
    printf("thread_policy_set(THREAD_AFFINITY_POLICY) = %d (%s)\n", g_affinity_kr,
           g_affinity_kr == KERN_SUCCESS ? "KERN_SUCCESS" :
           g_affinity_kr == KERN_NOT_SUPPORTED ? "KERN_NOT_SUPPORTED" : "other");
    printf("iterations per test = %llu\n", (unsigned long long)ITERS);

    struct { const char *name; void *(*w)(void *); void *(*r)(void *); } mp[] = {
        { "W plain   + R plain   ", w_plain, r_plain },
        { "W dmb ish + R plain   ", w_dmb,   r_plain },
        { "W plain   + R dmb ish ", w_plain, r_dmb   },
        { "W dmb ish + R dmb ish ", w_dmb,   r_dmb   },
        { "W dmb ishst + R ishld ", w_dmbst, r_dmbld },
        { "W dmb ish + R ishst(X)", w_dmb,   r_dmbst },
        { "W stlr    + R ldar    ", w_stlr,  r_ldar  },
        { "W stlr    + R ldapr   ", w_stlr,  r_ldapr },
        { "W stlr    + R addr-dep", w_stlr,  r_addr  },
        { "W stlr    + R ctrl    ", w_stlr,  r_ctrl  },
        { "W stlr    + R ctrl+isb", w_stlr,  r_ctrlisb },
        { "W stlr    + R plain   ", w_stlr,  r_plain },
        { "W plain   + R ldapr   ", w_plain, r_ldapr },
    };
    for (size_t k = 0; do_mp && k < sizeof mp / sizeof mp[0]; k++) {
        uint64_t weak = 0;
        g_data = g_flag = 0;
        go2(mp[k].w, NULL, mp[k].r, &weak);
        printf("MP %s  d<f observed: %llu\n", mp[k].name, (unsigned long long)weak);
    }

    if (!do_sb) return 0;
    g_r1 = calloc(ITERS + 1, sizeof *g_r1);
    g_r2 = calloc(ITERS + 1, sizeof *g_r2);
    if (!g_r1 || !g_r2) return 1;
    struct { const char *name; void *(*s)(void *); } sb[] = {
        { "str ; ldr          ", s_plain }, { "str ; dmb ish ; ldr", s_dmb },
        { "str ; dmb ishst;ldr", s_dmbst }, { "stlr ; ldar        ", s_rcsc },
        { "stlr ; ldapr       ", s_rcpc },
    };
    for (size_t k = 0; k < sizeof sb / sizeof sb[0]; k++) {
        int a = 0, b = 1;
        g_x = g_y = 0;
        go2(sb[k].s, &a, sb[k].s, &b);
        uint64_t v = 0;
        for (uint64_t i = 1; i <= ITERS; i++) v += (g_r1[i] < (uint32_t)i && g_r2[i] < (uint32_t)i);
        printf("SB %s  r1<i && r2<i observed: %llu\n", sb[k].name, (unsigned long long)v);
    }
    free(g_r1);
    free(g_r2);
    return 0;
}
