/* ARCH-A05 demo_litmus.c — solution.c 의 하네스로 리트머스 5 개 × 변형 4 개를 N 번씩 돌려 히스토그램을 찍는다.
 * 관찰 횟수는 실행마다 다르다 (체크 아님). 같은 소스를 arm64 와 x86_64(Rosetta 2) 로 빌드해 비교한다.
 *
 *   cc -std=c11 -O2 -pthread demo_litmus.c -o lit_arm && ./lit_arm 1000000 time
 *   clang -arch x86_64 -std=c11 -O2 -pthread demo_litmus.c -o lit_x86 && ./lit_x86 1000000 time   (Rosetta 2)
 *   인자: N(반복 수)  time|each|batch  [runs]  [테스트 이름 필터, 예: SB]
 *         time  = 공통 시간축(mach_absolute_time)의 같은 눈금에 같이 출발 (기본, 200 ns 간격)
 *         each  = 반복마다 스핀 장벽,  batch = 4096 반복 배치 시작에만 장벽
 *   -DCELL=4 : 변수 배열을 빽빽하게 (이웃 반복의 변수가 같은 캐시 라인) — 배치(layout) 효과 실험
 */
#define _DARWIN_C_SOURCE 1
#define main solution_main
#include "solution.c"
#undef main
#include <sys/sysctl.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    size_t n = argc > 1 ? (size_t)strtoull(argv[1], NULL, 10) : 1000000u;
    const char *ms = argc > 2 ? argv[2] : "time";
    int mode = strcmp(ms, "batch") == 0 ? M_BATCH : strcmp(ms, "each") == 0 ? M_EACH : M_TIME;
    int runs = argc > 3 ? atoi(argv[3]) : 1;
    const char *only = argc > 4 ? argv[4] : NULL;
    int translated = 0;
    size_t sz = sizeof translated;
    if (sysctlbyname("sysctl.proc_translated", &translated, &sz, NULL, 0) != 0) translated = 0;
#if defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__aarch64__)
    const char *arch = "arm64";
#else
    const char *arch = "?";
#endif
    printf("arch=%s translated(Rosetta)=%d  N=%zu  barrier=%s  cell=%dB  runs=%d\n", arch, translated, n,
           mode == M_EACH ? "each-iteration" : mode == M_BATCH ? "batch-start" : "timebase", CELL, runs);
    for (int test = 0; test < T_N; test++) {
        const lit_test *T = &TESTS[test];
        if (only && strcmp(only, T->name) != 0) continue;
        for (int V = 0; V < V_N; V++) {
            for (int r = 0; r < runs; r++) {
                hist_t h = {{0}, 0};
                double t0 = now_s();
                lit_run_t R = { .T = T, .V = V, .mode = mode, .reset = true, .n = n, .batch = 4096 };
                if (lit_run(&R, &h) != 0) { printf("%s %s: run failed (timeout?)\n", T->name, VNAME[V]); return 1; }
                double dt = now_s() - t0;
                int md = translated || strcmp(arch, "x86_64") == 0 ? MD_TSO : MD_ARM;
                verdict_t vd = lit_verdict(h.cnt[T->weird], ALLOW[test][V][MD_C11], ALLOW[test][V][md]);
                printf("%-4s %-8s weird=%-8llu %-14s %5.2fs |", T->name, VNAME[V],
                       (unsigned long long)h.cnt[T->weird], VDNAME[vd], dt);
                for (unsigned k = 0; k < NKEY; k++) {
                    if (!h.cnt[k]) continue;
                    char buf[64];
                    key_str(T, k, buf, sizeof buf);
                    printf(" [%s]%s%llu", buf, k == T->weird ? "*" : ":", (unsigned long long)h.cnt[k]);
                }
                printf("\n");
                fflush(stdout);
            }
        }
    }
    return 0;
}
