/* OS-M04 demo_bench.c — 공유 트레이스 T1~T4 처리량(ops/s)을 할당기 설정별로 잰다.
 *
 * solution.c 의 할당기와 트레이스 생성기를 그대로 쓴다 (MM_NO_MAIN 으로 main 만 뺌).
 * 시간은 연산 루프만 잰다 (mm_init · 표 준비 · 패턴 검사 · mm_check 제외).
 * 트레이스 하나는 수십 µs 라서, 한 측정 = 합계 ≥ 20 ms 가 될 때까지 반복 재생. 측정 7번의 중앙값.
 * 비교용으로 macOS libc malloc/realloc/free (libmalloc) 도 같은 트레이스로 돈다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench
 *   (solution.c 와 같은 폴더에서. 공유 맥이라 부하가 있다 — 비율만 믿을 것)
 */
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트용 도우미 */
#define MM_NO_MAIN
#include "solution.c"
#include <time.h>

typedef struct { const char *name; int libc, kind, fit; } cfg_t;
static const cfg_t cfgs[] = {
    { "implicit first", 0, KIND_IMPLICIT, FIT_FIRST },
    { "explicit first", 0, KIND_EXPLICIT, FIT_FIRST },
    { "explicit next",  0, KIND_EXPLICIT, FIT_NEXT },
    { "explicit best",  0, KIND_EXPLICIT, FIT_BEST },
    { "libc malloc",    1, 0, 0 },
};
enum { NCFG = sizeof cfgs / sizeof cfgs[0], RUNS = 7 };

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static volatile uint8_t sink;
static double run_once(const trace_t *t, const cfg_t *c, void **ptr) {
    memset(ptr, 0, ((size_t)t->max_id + 1) * sizeof *ptr);
    if (!c->libc) { mm_config(c->kind, c->fit); mm_init(); }
    double t0 = now_s();
    for (int i = 0; i < t->n; i++) {
        const op_t *o = &t->ops[i];
        void *p;
        if (o->op == 'a') {
            p = c->libc ? malloc(o->size) : mm_malloc(o->size);
            if (p) ((uint8_t *)p)[0] = (uint8_t)i;           /* 한 바이트만 만진다 */
            ptr[o->id] = p;
        } else if (o->op == 'r') {
            p = c->libc ? realloc(ptr[o->id], o->size) : mm_realloc(ptr[o->id], o->size);
            if (p) ptr[o->id] = p;
        } else {
            if (ptr[o->id]) sink ^= ((uint8_t *)ptr[o->id])[0];
            if (c->libc) free(ptr[o->id]); else mm_free(ptr[o->id]);
            ptr[o->id] = NULL;
        }
    }
    return now_s() - t0;
}

int main(void) {
    make_traces();
    printf("ops/s (median of %d), higher is better. U = utilization from solution.c (deterministic)\n", RUNS);
    printf("| trace | ops | %s | %s | %s | %s | %s |\n", cfgs[0].name, cfgs[1].name, cfgs[2].name,
           cfgs[3].name, cfgs[4].name);
    for (int k = 0; k < NTRACE; k++) {
        const trace_t *t = &g_tr[k];
        void **ptr = calloc((size_t)t->max_id + 1, sizeof *ptr);
        printf("| %s | %d |", t->name, t->n);
        for (int ci = 0; ci < NCFG; ci++) {
            double v[RUNS];
            run_once(t, &cfgs[ci], ptr);                          /* 워밍업 (페이지 첫 접근 등) */
            for (int r = 0; r < RUNS; r++) {
                double sec = 0;
                long reps = 0;
                while (sec < 0.02) { sec += run_once(t, &cfgs[ci], ptr); reps++; }
                v[r] = (double)t->n * (double)reps / sec;            /* ops/s */
            }
            qsort(v, RUNS, sizeof v[0], cmp_d);
            printf(" %.1fM |", v[RUNS / 2] / 1e6);
            fflush(stdout);
        }
        printf("\n");
        free(ptr);
    }
    for (int k = 0; k < NTRACE; k++) free(g_tr[k].ops);
    mem_deinit();
    return (int)(sink & 0);
}
