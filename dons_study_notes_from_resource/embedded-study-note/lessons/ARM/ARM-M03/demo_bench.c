/* ARM-M03 demo_bench.c — 실제 측정 (CHECK 없음, 관찰용). 테스트된 하네스(solution.c)를 그대로 쓴다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c solution.S demo_isb.S practice.S -o /tmp/ARM-M03_bench
 * run:   /tmp/ARM-M03_bench res      카운터 해상도: CNTFRQ vs 실제 눈금, isb+mrs 비용
 *        /tmp/ARM-M03_bench table    지연/처리량 표 (QoS USER_INTERACTIVE 요청)
 *        /tmp/ARM-M03_bench table bg 같은 표를 QoS BACKGROUND 로 (E 코어로 밀려남)
 *        /tmp/ARM-M03_bench div      SDIV/UDIV 지연이 피연산자 값에 따라 변하나
 *        /tmp/ARM-M03_bench isb      256 sdiv 구간을 ISB 있게/없게 (demo_isb.S)
 *        /tmp/ARM-M03_bench prac     추가 연습 4·5 커널 (인덱스 추적 load, 사슬 4개 fadd)
 *        /tmp/ARM-M03_bench fp       FADD/FMUL/FMADD 지연이 피연산자 값에 따라 변하나
 *        /tmp/ARM-M03_bench ldr      포인터 추적 고리 크기별 LDR 지연 (값/주소 예측이 사슬을 끊나)
 * macOS 는 코어 고정이 안 된다 → pthread_cpu_number_np 로 실행 코어 번호를 찍는다
 * (이 M6: 0–5 = E, 6–7 = Super, 8–11 = P — ARCH-M08 측정).
 */
#define _DARWIN_C_SOURCE
#define BENCH_NO_MAIN
#include "solution.c"          /* 테스트를 통과한 하네스 함수 그대로 (main 제외) */
#include <mach/mach_time.h>
#include <pthread.h>
#include <pthread/qos.h>


static int cpu_lo = 99, cpu_hi = -1;
static void note_cpu(void) {
    size_t c = 0;
    if (pthread_cpu_number_np(&c) == 0) {
        if ((int)c < cpu_lo) cpu_lo = (int)c;
        if ((int)c > cpu_hi) cpu_hi = (int)c;
    }
}

/* FP 커널을 정수 커널 모양으로 감싼다 (비트 그대로 전달) */
static double d_of(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }
static uint64_t b_of(double d) { uint64_t b; memcpy(&b, &d, sizeof b); return b; }
#define FWRAP(name) static uint64_t w_##name(uint64_t it, uint64_t x, uint64_t y) { \
    return b_of(name(it, d_of(x), d_of(y))); }
FWRAP(lat_fadd) FWRAP(tput_fadd) FWRAP(lat_fmul) FWRAP(tput_fmul) FWRAP(lat_fmadd) FWRAP(tput_fmadd)

static uint64_t g_x = 0x2545F4914F6CDD1Dull;
static uint64_t rnd_x(void) { g_x ^= g_x << 13; g_x ^= g_x >> 7; g_x ^= g_x << 17; return g_x; }

static uint64_t g_overhead;            /* 빈 구간(시계 2번 + 호출) 의 최소 tick */

static void calibrate(void) {
    uint64_t raw[201], sc[201]; struct bench_stats st;
    bench_measure(cnt_read, k_empty, 0, 0, 0, raw, 201);
    bench_summarize(raw, 201, 0, sc, &st);
    g_overhead = st.min;
    printf("overhead (빈 구간, 201회): min %" PRIu64 " / med %" PRIu64 " / max %" PRIu64 " tick\n",
           st.min, st.med, st.max);
}

static uint64_t g_self;                 /* lat_ldr: *p == p */
static uint64_t g_arr[16];

/* 커널 샘플과 lat_add 샘플을 번갈아 잰다. 쌍마다 비(커널/add)를 밀리사이클로 → 그 비들의 중앙값.
 * 바로 옆에서 잰 add 가 그 순간의 클럭 → 코어가 바뀌거나 클럭이 변해도 쌍 안에서는 같은 조건.
 * 쌍 앞뒤의 코어 번호가 같을 때만 그 코어 종류(S/P/E) 칸에 넣는다 (이 M6: 0–5 E, 6–7 S, 8–11 P). */
#define PREPS 41
struct res { uint64_t ps_med, mc_all, mc[3]; size_t n[3]; uint64_t mhz[3]; };
static int cls_of(size_t c) { return c >= 8 ? 1 : c >= 6 ? 0 : 2; }   /* 0 = Super, 1 = Perf, 2 = Eff */
static void pair(ikern_fn k, uint64_t iters, uint64_t x, uint64_t y, struct res *o) {
    uint64_t rk[PREPS], ra[PREPS], mc[PREPS], sc[PREPS], f = cnt_freq(), nops = iters * OPS_PER_ITER;
    uint64_t cm[3][PREPS], ca[3][PREPS];
    struct bench_stats sk;
    memset(o, 0, sizeof *o);
    k(iters / 10 + 1, x, y);                                           /* 워밍업 */
    for (int r = 0; r < PREPS; r++) {
        size_t c0 = 99, c1 = 98;
        pthread_cpu_number_np(&c0);
        bench_measure(cnt_read, lat_add, iters, 1, 0, &ra[r], 1);
        bench_measure(cnt_read, k, iters, x, y, &rk[r], 1);
        pthread_cpu_number_np(&c1);
        note_cpu();
        uint64_t pa = per_op_ps(ticks_to_ns(u64_sub_clamp(ra[r], g_overhead), f), nops);
        mc[r] = mcycles(per_op_ps(ticks_to_ns(u64_sub_clamp(rk[r], g_overhead), f), nops), pa);
        if (c0 == c1) { int c = cls_of(c0); cm[c][o->n[c]] = mc[r]; ca[c][o->n[c]] = pa; o->n[c]++; }
    }
    bench_summarize(rk, PREPS, g_overhead, sc, &sk);
    o->ps_med = per_op_ps(ticks_to_ns(sk.med, f), nops);
    o->mc_all = u64_median(mc, PREPS);
    for (int c = 0; c < 3; c++) if (o->n[c]) {
        o->mc[c] = u64_median(cm[c], o->n[c]);
        uint64_t pa = u64_median(ca[c], o->n[c]);
        o->mhz[c] = pa ? 1000000u / pa : 0;                            /* add 1 사이클 → 클럭 추정 */
    }
}

static void hdr(const char *first) {
    printf("%-30s %7s %8s | %-14s %-14s %-14s\n", first, "ps/op", "cyc(all)", "Super", "Perf", "Eff");
}
static void row(const char *name, const struct res *o) {
    printf("%-30s %7" PRIu64 " %5" PRIu64 ".%02" PRIu64 " |", name, o->ps_med, o->mc_all / 1000, (o->mc_all % 1000) / 10);
    for (int c = 0; c < 3; c++) {
        if (o->n[c]) printf(" %3" PRIu64 ".%02" PRIu64 " (n=%2zu) ", o->mc[c] / 1000, (o->mc[c] % 1000) / 10, o->n[c]);
        else printf(" %-14s", "   -");
    }
    printf("\n");
}

static void mode_table(int bg) {
    pthread_set_qos_class_self_np(bg ? QOS_CLASS_BACKGROUND : QOS_CLASS_USER_INTERACTIVE, 0);
    calibrate();
    g_self = (uint64_t)&g_self;
    for (int i = 0; i < 16; i++) g_arr[i] = (uint64_t)i;
    const uint64_t one = b_of(1.0), half = b_of(0.5), onep = b_of(1.0 + 0x1p-40);
    struct { const char *name; ikern_fn k; uint64_t x, y; } K[] = {
        {"lat_add      add x1,x1,#1",      lat_add,      1, 0},
        {"tput_add     (16 chains)",       tput_add,     1, 0},
        {"lat_mul      mul x1,x1,x2",      lat_mul,      3, 5},
        {"tput_mul     (16 chains)",       tput_mul,     3, 5},
        {"lat_madd     via Rn",            lat_madd,     3, 5},
        {"lat_madd_acc via Ra",            lat_madd_acc, 3, 5},
        {"lat_sdiv     2^62/1",            lat_sdiv,     1ull << 62, 1},
        {"tput_sdiv    (16 chains)",       tput_sdiv,    1ull << 62, 1},
        {"lat_udiv     2^62/1",            lat_udiv,     1ull << 62, 1},
        {"lat_ldr      self *p==p",        lat_ldr,      0, 0},
        {"tput_ldr     (16 indep)",        tput_ldr,     0, 0},
        {"lat_fadd     fadd d0,d0,d1",     w_lat_fadd,   one, half},
        {"tput_fadd    (16 chains)",       w_tput_fadd,  one, half},
        {"lat_fmul     fmul d0,d0,d1",     w_lat_fmul,   one, onep},
        {"tput_fmul    (16 chains)",       w_tput_fmul,  one, onep},
        {"lat_fmadd    fmadd d0,d0,d1,d1", w_lat_fmadd,  half, half},
        {"tput_fmadd   (16 chains)",       w_tput_fmadd, half, half},
        {"k_empty      (loop tail only)",  k_empty,      0, 0},
    };
    K[9].x = g_self; K[10].x = (uint64_t)g_arr;
    const uint64_t iters = 20000;                                      /* 32만 명령/샘플 ≈ 80 us */
    printf("iters=%" PRIu64 " x16 = %" PRIu64 " ops/sample, %d pairs (lat_add, kernel), cntfrq %" PRIu64 " Hz\n",
           iters, iters * OPS_PER_ITER, PREPS, cnt_freq());
    hdr("kernel");
    uint64_t mhz[3] = {0};
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        struct res o;
        pair(K[i].k, iters, K[i].x, K[i].y, &o);
        row(K[i].name, &o);
        if (i == 0) for (int c = 0; c < 3; c++) mhz[c] = o.mhz[c];
    }
    printf("clock est. from lat_add (1 cycle): Super %" PRIu64 " / Perf %" PRIu64 " / Eff %" PRIu64 " MHz\n",
           mhz[0], mhz[1], mhz[2]);
    printf("cpus seen %d..%d, sink=%016" PRIx64 "\n", cpu_lo, cpu_hi, (uint64_t)g_sink);
}

/* 포인터 고리: n 칸, 칸마다 다른 64 B 줄. order: 0 = 순서대로(i→i+1), 1 = 무작위 순열 */
static uint64_t g_ring[512 * 8] __attribute__((aligned(64)));
static uint64_t make_ring(size_t n, int random) {
    size_t perm[512];
    for (size_t i = 0; i < n; i++) perm[i] = i;
    if (random) for (size_t i = n - 1; i > 0; i--) { size_t j = (size_t)(rnd_x() % (i + 1)); size_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (size_t i = 0; i < n; i++) g_ring[perm[i] * 8] = (uint64_t)&g_ring[perm[(i + 1) % n] * 8];
    return (uint64_t)&g_ring[perm[0] * 8];
}

static void mode_ldr(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    calibrate();
    struct { size_t n; int random; const char *note; } R[] = {
        {1, 0, "자기 자신 (*p == p)"}, {2, 0, "2칸: PC마다 주소·값 고정"}, {3, 0, "3칸: 16 mod 3 = 1"},
        {15, 0, "15칸"}, {16, 0, "16칸: PC마다 주소·값 고정"}, {17, 0, "17칸"},
        {64, 1, "64줄 무작위 (4 KB)"}, {512, 1, "512줄 무작위 (32 KB)"},
    };
    hdr("ring");
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        uint64_t p = make_ring(R[i].n, R[i].random); struct res o; char nm[64];
        pair(lat_ldr, 20000, p, 0, &o);
        snprintf(nm, sizeof nm, "%3zu %s", R[i].n, R[i].note);
        row(nm, &o);
    }
    printf("cpus seen %d..%d\n", cpu_lo, cpu_hi);
}

static void mode_res(void) {
    enum { N = 1000000 };
    static uint64_t t[N];
    uint64_t f = cnt_freq();
    for (int i = 0; i < N; i++) t[i] = cnt_read();
    long zero = 0, h41 = 0, h42 = 0, other = 0, changes = 0;
    uint64_t gcd_like = 0;
    for (int i = 1; i < N; i++) {
        uint64_t d = t[i] - t[i - 1];
        if (d == 0) zero++; else { changes++; if (d == 41) h41++; else if (d == 42) h42++; else other++; }
        (void)gcd_like;
    }
    printf("cntfrq_el0 = %" PRIu64 " Hz (1 tick = %.3f ns 라고 '주장')\n", f, 1e9 / (double)f);
    printf("1e6 연속 읽기: 같은 값 %ld, 변화 %ld (41: %ld, 42: %ld, 그 외: %ld)\n", zero, changes, h41, h42, other);
    printf("평균 눈금 = %.3f tick, 읽기 %.2f 번마다 값이 바뀜, isb+mrs 1회 ≈ %.2f ns\n",
           (double)(t[N - 1] - t[0]) / (double)changes, (double)N / (double)changes,
           (double)ticks_to_ns(t[N - 1] - t[0], f) / N);
    /* mach_absolute_time (24 MHz) 과 비교 */
    uint64_t c0 = cnt_read(), m0 = mach_absolute_time();
    g_sink ^= lat_add(2000000, 0, 0);
    uint64_t c1 = cnt_read(), m1 = mach_absolute_time();
    printf("같은 구간: cntvct %" PRIu64 " tick, mach_absolute_time %" PRIu64 " tick, 비 %.4f (125/3 = 41.6667)\n",
           c1 - c0, m1 - m0, (double)(c1 - c0) / (double)(m1 - m0));
    printf("cntvct %% 125 표본: ");
    for (int i = 0; i < 8; i++) { uint64_t v = cnt_read(); printf("%" PRIu64 "(x3%%125=%" PRIu64 ") ", v % 125, v * 3 % 125); for (volatile int k = 0; k < 1000; k++) {} }
    printf("\n");
}

static void mode_div(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    calibrate();
    struct { const char *what; ikern_fn k; uint64_t x, y; } D[] = {
        {"sdiv 0/1          (몫 0)",       lat_sdiv, 0, 1},
        {"sdiv 7/1          (몫 3비트)",   lat_sdiv, 7, 1},
        {"sdiv 2^16/1       (몫 17비트)",  lat_sdiv, 1ull << 16, 1},
        {"sdiv 2^31/1       (몫 32비트)",  lat_sdiv, 1ull << 31, 1},
        {"sdiv 2^32/1       (몫 33비트)",  lat_sdiv, 1ull << 32, 1},
        {"sdiv 2^48/1       (몫 49비트)",  lat_sdiv, 1ull << 48, 1},
        {"sdiv (2^63-1)/1   (몫 63비트)",  lat_sdiv, INT64_MAX, 1},
        {"sdiv -(2^62)/1    (음수)",       lat_sdiv, (uint64_t)-(1ll << 62), 1},
        {"sdiv x/0          (결과 0)",     lat_sdiv, 12345, 0},
        {"udiv 7/1",                       lat_udiv, 7, 1},
        {"udiv (2^64-1)/1   (몫 64비트)",  lat_udiv, UINT64_MAX, 1},
    };
    hdr("");
    for (size_t i = 0; i < sizeof D / sizeof D[0]; i++) {
        struct res o;
        pair(D[i].k, 20000, D[i].x, D[i].y, &o);
        row(D[i].what, &o);
    }
    printf("cpus seen %d..%d\n", cpu_lo, cpu_hi);
}

/* FP 지연이 값에 따라 달라지나 (fadd 가 소수 사이클로 나오는 이유 찾기) */
static void mode_fp(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    calibrate();
    struct { const char *what; ikern_fn k; double x, y; } F[] = {
        {"fadd 1.0 + 0.5 반복",     w_lat_fadd, 1.0, 0.5},
        {"fadd 1.0 + 0.0 (값 고정)", w_lat_fadd, 1.0, 0.0},
        {"fadd 1.0 + 1e-300",       w_lat_fadd, 1.0, 1e-300},
        {"fadd 0 + 0",              w_lat_fadd, 0.0, 0.0},
        {"fadd denormal + denorm",  w_lat_fadd, 0x1p-1060, 0x1p-1070},
        {"fmul 1.0 * 1.0",          w_lat_fmul, 1.0, 1.0},
        {"fmul denormal * 1.0",     w_lat_fmul, 0x1p-1060, 1.0},
        {"fmadd 0.5*0.5+0.5",       w_lat_fmadd, 0.5, 0.5},
        {"fmadd 1.0*1.0+1.0",       w_lat_fmadd, 1.0, 1.0},
        {"fmadd 0*0+0",             w_lat_fmadd, 0.0, 0.0},
    };
    hdr("");
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        struct res o;
        pair(F[i].k, 20000, b_of(F[i].x), b_of(F[i].y), &o);
        row(F[i].what, &o);
    }
    printf("cpus seen %d..%d\n", cpu_lo, cpu_hi);
}

/* 짧은 구간: lat_mul(k) = 16k 개 mul 사슬 (≈ 48k 사이클). ISB 유무로 잰 값 비교 */
uint64_t region_isb(uint64_t x, uint64_t y), region_noisb(uint64_t x, uint64_t y);
uint64_t region_isb_dep(uint64_t x, uint64_t y), region_ss(uint64_t x, uint64_t y);

/* 구간이 asm 한 함수 안: isb; mrs; 256 sdiv 사슬; [isb]; mrs. 함수가 돌려준 t1-t0 를 그대로 모은다 */
static void mode_isb(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    enum { R = 2001 };
    static uint64_t a[4][R], sc[R];
    const char *nm[4] = {"isb; mrs ... isb; mrs", "mrs ... mrs (ISB 없음)", "isb; mrs ... (dep load) mrs", "mrs cntvctss ... cntvctss"};
    for (int r = 0; r < R; r++) {
        a[0][r] = region_isb(1ull << 62, 1);
        a[1][r] = region_noisb(1ull << 62, 1);
        a[2][r] = region_isb_dep(1ull << 62, 1);
        a[3][r] = region_ss(1ull << 62, 1);
    }
    uint64_t lr[31]; struct bench_stats st;                            /* 참고: 긴 사슬로 잰 sdiv 1개 */
    bench_measure(cnt_read, lat_sdiv, 20000, 1ull << 62, 1, lr, 31);
    bench_summarize(lr, 31, 0, sc, &st);
    uint64_t ps = per_op_ps(ticks_to_ns(st.min, cnt_freq()), 20000 * OPS_PER_ITER);
    printf("256 sdiv 사슬: 긴 측정 기준 %" PRIu64 " ps/sdiv → 기대 ≈ %" PRIu64 " ns\n", ps, 256 * ps / 1000);
    for (int v = 0; v < 4; v++) {
        bench_summarize(a[v], R, 0, sc, &st);
        printf("%-30s min %4" PRIu64 "  med %4" PRIu64 "  max %6" PRIu64 " tick (%d회)\n", nm[v], st.min, st.med, st.max, R);
    }
}

/* 추가 연습 4, 5 의 커널 (practice.S) */
uint64_t lat_ldr_idx(uint64_t iters, uint64_t i0, const uint64_t *tab);
double tput_fadd4(uint64_t iters, double x, double y);
static uint64_t g_tab[64];
static uint64_t w_ldr_idx(uint64_t it, uint64_t x, uint64_t y) { (void)y; return lat_ldr_idx(it, x, g_tab); }
FWRAP(tput_fadd4)
static void mode_prac(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    calibrate();
    size_t perm[64];
    for (size_t i = 0; i < 64; i++) perm[i] = i;
    for (size_t i = 63; i > 0; i--) { size_t j = (size_t)(rnd_x() % (i + 1)); size_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (size_t i = 0; i < 64; i++) g_tab[perm[i]] = perm[(i + 1) % 64];
    uint64_t p = make_ring(64, 1);
    struct res o;
    hdr("");
    pair(lat_ldr, 20000, p, 0, &o);                  row("ldr x1,[x1]   (64줄 무작위)", &o);
    pair(w_ldr_idx, 20000, perm[0], 0, &o);          row("ldr x1,[x2,x1,lsl #3] (64칸)", &o);
    pair(w_lat_fadd, 20000, b_of(1.0), b_of(0.5), &o);  row("lat_fadd   (사슬 1)", &o);
    pair(w_tput_fadd4, 20000, b_of(1.0), b_of(0.5), &o); row("tput_fadd4 (사슬 4)", &o);
    pair(w_tput_fadd, 20000, b_of(1.0), b_of(0.5), &o);  row("tput_fadd  (사슬 16)", &o);
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "table";
    if (!strcmp(m, "res")) mode_res();
    else if (!strcmp(m, "table")) mode_table(argc > 2 && !strcmp(argv[2], "bg"));
    else if (!strcmp(m, "div")) mode_div();
    else if (!strcmp(m, "isb")) mode_isb();
    else if (!strcmp(m, "ldr")) mode_ldr();
    else if (!strcmp(m, "fp")) mode_fp();
    else if (!strcmp(m, "prac")) mode_prac();
    else { fprintf(stderr, "usage: %s res|table [bg]|div|isb|ldr\n", argv[0]); return 2; }
    return 0;
}
