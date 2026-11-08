/* LLAT-M05 demo_pingpong.c — 두 스레드가 캐시 라인으로 핑퐁할 때 왕복(RT) 지연 측정 (관찰용, CHECK 없음)
 *
 *   ./pp map                                    cpu 번호 → 코어 종류 추정 (빈 루프 속도, 약 1.5 초)
 *   ./pp rt <wait> <layout> <qping> <qpong> [runs] [samples] [batch]
 *        wait   : spin | wfe | ulock | cond
 *        layout : one  (64비트 seq 하나, 핑=홀수 퐁=짝수)
 *                 d8 | d64 | d128 | d256 (깃발 두 개 a,b 를 그만큼 떨어뜨림 — d8/d64 는 128B 라인 하나 안)
 *        qping/qpong : ui (USER_INTERACTIVE) | in (USER_INITIATED) | def | ut (UTILITY) | bg (BACKGROUND)
 *   ./pp fs [qos]                                  두 스레드가 각자 카운터만 쓸 때 거리 8~256 B (라인 크기 확인)
 *   PP_SPLIT=1 ./pp rt ...                      표본을 코어 쌍 종류 (E/S/P × E/S/P) 별로 나눈 p50 도 출력
 *   ./pp suite                                  레슨 표에 쓴 실험 묶음 전체 (약 20 초)
 *
 * 표본 1개 = batch 번 왕복의 시간 (mach_absolute_time, 1 tick = 41.67 ns) → /batch → 왕복 1번 ns.
 * 앞 10% 표본은 워밍업으로 버린다. 핑은 표본마다, 퐁은 1024 왕복마다 pthread_cpu_number_np 로 코어를 기록.
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_pingpong.c -o pp
 */
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <os/os_sync_wait_on_address.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE 128
enum { W_SPIN, W_WFE, W_ULOCK, W_COND };

static _Alignas(512) unsigned char g_buf[1024];
static _Atomic uint64_t *g_a, *g_b;     /* layout one: g_a == g_b */
static int g_one, g_wait, g_split;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_ecut = 6;                  /* cpu 번호 < g_ecut 이면 E 코어로 본다 (map 으로 확인) */
static int g_scut = 8;                  /* g_ecut <= cpu < g_scut 이면 Super, 그 위는 Performance (map 으로 추정) */
static _Alignas(LINE) _Atomic unsigned g_pong_cpu;   /* 퐁이 256 왕복마다 자기 cpu 번호를 적는 칸 (따로 라인) */
static char cls_of(size_t c) { return c < (size_t)g_ecut ? 'E' : c < (size_t)g_scut ? 'S' : 'P'; }

static qos_class_t qos_of(const char *s) {
    if (!strcmp(s, "ui")) return QOS_CLASS_USER_INTERACTIVE;
    if (!strcmp(s, "in")) return QOS_CLASS_USER_INITIATED;
    if (!strcmp(s, "ut")) return QOS_CLASS_UTILITY;
    if (!strcmp(s, "bg")) return QOS_CLASS_BACKGROUND;
    return QOS_CLASS_DEFAULT;
}
static size_t cpu_now(void) { size_t c = 0; pthread_cpu_number_np(&c); return c; }

/* ---- 기다리기 / 알리기 : 4가지 방식 ---- */
static inline void wait_eq(_Atomic uint64_t *p, uint64_t want) {
    switch (g_wait) {
    case W_SPIN:
        while (atomic_load_explicit(p, memory_order_acquire) != want) { }
        return;
    case W_WFE: {
        uint64_t v;
        __asm__ volatile("sevl\n"
                         "1: wfe\n"
                         "ldaxr %0, [%1]\n"
                         "cmp %0, %2\n"
                         "b.ne 1b\n"
                         : "=&r"(v) : "r"(p), "r"(want) : "cc", "memory");
        return;
    }
    case W_ULOCK:
        for (;;) {
            uint64_t v = atomic_load_explicit(p, memory_order_acquire);
            if (v == want) return;
            os_sync_wait_on_address((void *)p, v, 8, OS_SYNC_WAIT_ON_ADDRESS_NONE);   /* v 와 다르면 바로 반환 */
        }
    default:
        pthread_mutex_lock(&g_mu);
        while (atomic_load_explicit(p, memory_order_relaxed) != want) pthread_cond_wait(&g_cv, &g_mu);
        pthread_mutex_unlock(&g_mu);
    }
}
static inline void post(_Atomic uint64_t *p, uint64_t v) {
    switch (g_wait) {
    case W_SPIN: case W_WFE:
        atomic_store_explicit(p, v, memory_order_release);
        return;
    case W_ULOCK:
        atomic_store_explicit(p, v, memory_order_release);
        os_sync_wake_by_address_any((void *)p, 8, OS_SYNC_WAKE_BY_ADDRESS_NONE);    /* 기다리는 이 없으면 ENOENT */
        return;
    default:
        pthread_mutex_lock(&g_mu);
        atomic_store_explicit(p, v, memory_order_relaxed);
        pthread_cond_signal(&g_cv);
        pthread_mutex_unlock(&g_mu);
    }
}

typedef struct { qos_class_t q; uint64_t rounds; uint64_t ecnt, tot; } pong_arg_t;

static void *pong(void *arg) {
    pong_arg_t *a = arg;
    pthread_set_qos_class_self_np(a->q, 0);
    for (uint64_t k = 1; k <= a->rounds; k++) {
        if (g_one) { wait_eq(g_a, 2 * k - 1); post(g_a, 2 * k); }
        else       { wait_eq(g_a, k);         post(g_b, k); }
        if ((k & 255) == 0) {
            size_t c = cpu_now();
            atomic_store_explicit(&g_pong_cpu, (unsigned)c, memory_order_relaxed);
            a->tot++; if (c < (size_t)g_ecut) a->ecnt++;
        }
    }
    return NULL;
}

static int cmp_u64(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return (a > b) - (a < b);
}

typedef struct { double min, p50, p99; double ping_e, pong_e; char cls[9][3]; int ccnt[9]; double cp50[9]; } res_t;
static int pair_idx(char a, char b) { const char *k = "ESP"; return (int)(strchr(k, a) - k) * 3 + (int)(strchr(k, b) - k); }

static res_t one_run(qos_class_t qp, qos_class_t qq, int samples, int batch) {
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    memset(g_buf, 0, sizeof g_buf);
    uint64_t rounds = (uint64_t)samples * (uint64_t)batch;
    pong_arg_t pa = { qq, rounds, 0, 0 };
    pthread_t th;
    pthread_set_qos_class_self_np(qp, 0);
    pthread_create(&th, NULL, pong, &pa);
    uint64_t *s = calloc((size_t)samples, sizeof *s);
    uint64_t k = 0; int ecnt = 0;
    unsigned char *pc = calloc((size_t)samples, 1);
    atomic_store(&g_pong_cpu, 999u);
    for (int i = 0; i < samples; i++) {
        uint64_t t0 = mach_absolute_time();
        for (int j = 0; j < batch; j++) {
            k++;
            if (g_one) { post(g_a, 2 * k - 1); wait_eq(g_a, 2 * k); }
            else       { post(g_a, k);         wait_eq(g_b, k); }
        }
        uint64_t t1 = mach_absolute_time();
        s[i] = t1 - t0;
        size_t me = cpu_now();
        unsigned pcpu = atomic_load_explicit(&g_pong_cpu, memory_order_relaxed);
        if (me < (size_t)g_ecut) ecnt++;
        pc[i] = pcpu == 999u ? 255 : (unsigned char)pair_idx(cls_of(me), cls_of(pcpu));
    }
    pthread_join(th, NULL);
    int warm = samples / 10, m = samples - warm;
    res_t r;
    double f = (double)tb.numer / tb.denom / batch;   /* ticks → ns, ÷ batch */
    for (int c = 0; c < 9; c++) {                     /* 코어 쌍 종류별 p50 */
        uint64_t *t = calloc((size_t)m, sizeof *t); int n = 0;
        for (int i = warm; i < samples; i++) if (pc[i] == c) t[n++] = s[i];
        r.ccnt[c] = n;
        if (n) { qsort(t, (size_t)n, sizeof *t, cmp_u64); r.cp50[c] = t[(n - 1) / 2] * f; }
        free(t);
    }
    free(pc);
    qsort(s + warm, (size_t)m, sizeof *s, cmp_u64);
    r.min = s[warm] * f;
    r.p50 = s[warm + (m - 1) / 2] * f;
    r.p99 = s[warm + (99 * m + 99) / 100 - 1] * f;
    r.ping_e = 100.0 * ecnt / samples;
    r.pong_e = pa.tot ? 100.0 * pa.ecnt / pa.tot : -1;
    free(s);
    return r;
}

static int wait_of(const char *s) {
    if (!strcmp(s, "spin")) return W_SPIN;
    if (!strcmp(s, "wfe")) return W_WFE;
    if (!strcmp(s, "ulock")) return W_ULOCK;
    return W_COND;
}
static void set_layout(const char *l) {
    g_one = !strcmp(l, "one");
    size_t d = g_one ? 0 : (size_t)strtoul(l + 1, NULL, 10);
    g_a = (_Atomic uint64_t *)(void *)g_buf;
    g_b = (_Atomic uint64_t *)(void *)(g_buf + d);
}

static void rt(const char *w, const char *l, const char *qp, const char *qq, int runs, int samples, int batch) {
    g_wait = wait_of(w); set_layout(l);
    printf("%-5s %-4s %-3s/%-3s", w, l, qp, qq);
    for (int r = 0; r < runs; r++) {
        res_t x = one_run(qos_of(qp), qos_of(qq), samples, batch);
        printf(" | p50 %7.1f p99 %8.1f min %6.1f E%%%3.0f/%3.0f", x.p50, x.p99, x.min, x.ping_e, x.pong_e);
        if (g_split) {
            const char *nm = "ESP";
            printf(" [");
            for (int c = 0; c < 9; c++) if (x.ccnt[c]) printf(" %c%c:%d@%.0f", nm[c / 3], nm[c % 3], x.ccnt[c], x.cp50[c]);
            printf(" ]");
        }
    }
    printf("\n");
    fflush(stdout);
}

/* 빈 루프를 돌며 cpu 번호별 가장 빠른 20000회 시간 → 코어 종류 추정 */
typedef struct { qos_class_t q; uint64_t best[64]; uint64_t cnt[64]; } map_arg_t;
static void *mapper(void *arg) {
    map_arg_t *a = arg;
    pthread_set_qos_class_self_np(a->q, 0);
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    for (int i = 0; i < 64; i++) a->best[i] = UINT64_MAX;
    uint64_t end = mach_absolute_time() + 750ull * 1000000 * tb.denom / tb.numer;
    volatile uint64_t sink = 0;
    while (mach_absolute_time() < end) {
        size_t c = cpu_now();
        uint64_t t0 = mach_absolute_time();
        for (int i = 0; i < 20000; i++) sink += (uint64_t)i;
        uint64_t t1 = mach_absolute_time();
        if (c < 64 && cpu_now() == c) {
            uint64_t ns = (t1 - t0) * tb.numer / tb.denom;
            a->cnt[c]++;
            if (ns < a->best[c]) a->best[c] = ns;
        }
    }
    return NULL;
}
static void map(void) {
    const char *nm[2] = { "USER_INTERACTIVE", "BACKGROUND" };
    qos_class_t q[2] = { QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_BACKGROUND };
    for (int k = 0; k < 2; k++) {
        static map_arg_t a; memset(&a, 0, sizeof a); a.q = q[k];
        pthread_t t; pthread_create(&t, NULL, mapper, &a); pthread_join(t, NULL);
        printf("%-16s:", nm[k]);
        for (int c = 0; c < 64; c++)
            if (a.cnt[c]) printf(" cpu%d x%llu best %lluns |", c, (unsigned long long)a.cnt[c], (unsigned long long)a.best[c]);
        printf("\n");
    }
}

/* fs: 두 스레드가 각자 자기 카운터만 relaxed store 로 계속 증가 (공유 없음) — 거리 d 에 따라 처리량 */
static _Atomic int g_go;
typedef struct { _Atomic uint64_t *p; uint64_t n; size_t cpu; int e, tot; } fs_arg_t;
static qos_class_t g_fsq = QOS_CLASS_USER_INTERACTIVE;
static void *fs_body(void *arg) {
    fs_arg_t *a = arg;
    pthread_set_qos_class_self_np(g_fsq, 0);
    while (!atomic_load(&g_go)) { }
    for (uint64_t i = 0; i < a->n; i++) {
        atomic_fetch_add_explicit(a->p, 1, memory_order_relaxed);   /* ldadd: 매번 라인 소유권 필요 */
        if ((i & 0xFFFF) == 0) { a->tot++; if (cpu_now() < (size_t)g_ecut) a->e++; }
    }
    a->cpu = cpu_now();
    return NULL;
}
static void fs(size_t d) {
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    printf("fs d=%-4zu", d);
    for (int r = 0; r < 3; r++) {
        memset(g_buf, 0, sizeof g_buf);
        fs_arg_t x = { (_Atomic uint64_t *)(void *)g_buf, 20000000, 0, 0, 0 }, y = { (_Atomic uint64_t *)(void *)(g_buf + d), 20000000, 0, 0, 0 };
        pthread_t t1, t2; atomic_store(&g_go, 0);
        pthread_create(&t1, NULL, fs_body, &x); pthread_create(&t2, NULL, fs_body, &y);
        uint64_t t0 = mach_absolute_time(); atomic_store(&g_go, 1);
        pthread_join(t1, NULL); pthread_join(t2, NULL);
        uint64_t ns = (mach_absolute_time() - t0) * tb.numer / tb.denom;
        printf(" | %5.2f ns/op E%%%3d/%3d", (double)ns / 20000000.0, 100 * x.e / x.tot, 100 * y.e / y.tot);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "fs")) {
        if (argc >= 3) g_fsq = qos_of(argv[2]); size_t ds[] = { 8, 32, 56, 64, 96, 120, 128, 256 }; for (int i = 0; i < 8; i++) fs(ds[i]); return 0; }
    if (argc >= 2 && !strcmp(argv[1], "map")) { map(); return 0; }
    if (getenv("PP_SPLIT")) g_split = 1;   /* 표본을 코어 쌍 종류(ping cls, pong cls)별로 나눈 p50 도 출력 */
    if (argc >= 6 && !strcmp(argv[1], "rt")) {
        rt(argv[2], argv[3], argv[4], argv[5], argc > 6 ? atoi(argv[6]) : 3,
           argc > 7 ? atoi(argv[7]) : 2000, argc > 8 ? atoi(argv[8]) : 64);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "suite")) {
        printf("per round trip, ns; 3 runs; E%% = ping/pong samples on cpu < %d\n", g_ecut);
        g_wait = W_SPIN; set_layout("one");
        (void)one_run(QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_USER_INTERACTIVE, 20000, 64);   /* 워밍업 ~0.1 s (클럭 올리기), 버림 */
        /* 1) 배치: 같은 spin/one/ui-ui 를 batch 1 / 8 / 64 로 */
        rt("spin", "one", "ui", "ui", 3, 20000, 1);
        rt("spin", "one", "ui", "ui", 3, 4000, 8);
        rt("spin", "one", "ui", "ui", 3, 2000, 64);
        /* 2) 레이아웃 */
        rt("spin", "d8", "ui", "ui", 3, 2000, 64);
        rt("spin", "d64", "ui", "ui", 3, 2000, 64);
        rt("spin", "d128", "ui", "ui", 3, 2000, 64);
        rt("spin", "d256", "ui", "ui", 3, 2000, 64);
        /* 3) QoS 조합 — 표본을 코어 쌍 종류별로도 나눠 출력 */
        g_split = 1;
        const char *pairs[][2] = { {"ui","ui"}, {"def","def"}, {"ut","ut"}, {"bg","bg"}, {"ui","bg"}, {"bg","ui"}, {"ui","ut"} };
        for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
            rt("spin", "one", pairs[i][0], pairs[i][1], 3, 2000, 64);
        g_split = 0;
        /* 4) 기다리는 방식 */
        rt("wfe", "one", "ui", "ui", 3, 2000, 64);
        rt("ulock", "one", "ui", "ui", 3, 2000, 4);
        rt("cond", "one", "ui", "ui", 3, 2000, 4);
        rt("ulock", "one", "bg", "bg", 3, 1000, 4);
        rt("cond", "one", "bg", "bg", 3, 1000, 4);
        return 0;
    }
    fprintf(stderr, "usage: see header\n");
    return 2;
}
