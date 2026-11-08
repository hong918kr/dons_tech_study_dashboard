/* SIV-M07 demo — shmoo 그림 · CSV · Vmin 탐색 전략 통계 (체크 아님: 관찰용)
 *
 * solution.c 를 그대로 끌어와 쓴다 (같은 레슨 디렉터리, main 이름만 바꿈).
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_shmoo.c -o demo_shmoo && ./demo_shmoo            # 그림 + 통계
 *   ./demo_shmoo --csv .                                                                        # CSV 파일도 쓰기
 *
 * 1) ASCII shmoo 3장: 양품 TT-01@25C, 홀 TT-03@25C, 산발 FF-02@125C
 * 2) 무작위 칩 N개 × OPP 주파수에서 Vmin 탐색 전략 4가지의 "탈출률" (새 측정 2000회 확인에서 FAIL 이 나온 비율)
 *    와 평균 실행 횟수 → 측정 1회 = 20 ms 로 가정한 시간 비용
 */
#define main siv_m07_solution_main
#include "solution.c"
#undef main

#include <stdlib.h>

static void show(chip_t *c, int t_c, const char *csv_dir) {
    shmoo_sweep(c, t_c, 20, &g_s);
    shmoo_render_ascii(g_buf, sizeof g_buf, &g_s);
    fputs(g_buf, stdout);
    if (csv_dir) {
        char path[256];
        snprintf(path, sizeof path, "%s/shmoo_%s_%dC.csv", csv_dir, c->name, t_c);
        FILE *fp = fopen(path, "w");
        if (fp) {
            shmoo_render_csv(g_buf, sizeof g_buf, &g_s);
            fprintf(fp, "# chip=%s T=%dC trials=%u value=pass count\n", c->name, t_c, (unsigned)g_s.trials);
            fputs(g_buf, fp);
            fclose(fp);
            printf("(wrote %s)\n", path);
        }
    }
    printf("\n");
}

static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }

int main(int argc, char **argv) {
    const char *csv_dir = (argc == 3 && strcmp(argv[1], "--csv") == 0) ? argv[2] : NULL;

    chip_t good = { .name = "TT-01", .corner = CORNER_TT, .seed = 0xA001, .noise_ppm = 20000 };
    chip_t hole = { .name = "TT-03", .corner = CORNER_TT, .seed = 0xA003, .noise_ppm = 20000,
                    .fault = { .hole_f = 700, .hole_v_lo = 800, .hole_v_hi = 840 } };
    chip_t spor = { .name = "FF-02", .corner = CORNER_FF, .seed = 0xA002, .noise_ppm = 20000,
                    .fault = { .spor_ppm = 50000, .spor_t_min = 125, .spor_v_lo = 850, .spor_v_hi = 1000,
                               .spor_f_lo = 400, .spor_f_hi = 800 } };
    show(&good, 25, csv_dir);
    show(&hole, 25, csv_dir);
    show(&spor, 125, csv_dir);
    printf("runs for one 36x17 shmoo with 20 trials = %u (at 20 ms/run = %.1f min)\n\n",
           (unsigned)(NV * NF * 20), NV * NF * 20 * 0.020 / 60.0);

    /* ---- Vmin 탐색 전략 통계 ---- */
    enum { NCHIP = 400 };
    static const int fs[] = { 500, 700, 900 };
    static const int ts[] = { -40, 25, 125 };
    const char *names[] = { "bisect 1 trial", "bisect 8 trials", "8 trials + confirm 200", "  ... + 10 mV guard" };
    long esc[4] = { 0 }, runs[4] = { 0 }, n = 0, over[4] = { 0 };
    uint64_t rng = 0x5EED5EEDull;
    for (int k = 0; k < NCHIP; k++) {
        chip_t c = { .name = "rnd", .corner = (corner_t)(xs64(&rng) % 3), .seed = xs64(&rng), .noise_ppm = 20000 };
        int t = ts[xs64(&rng) % 3];
        for (int q = 0; q < 3; q++) {
            int f = fs[q];
            int hi_t = vmin_truth(&c, t, f, +20000, V_LO, V_HI), lo_t = vmin_truth(&c, t, f, -20000, V_LO, V_HI);
            if (hi_t < 0 || lo_t <= V_LO) continue;                      /* 범위 안에 경계가 있는 경우만 */
            vmin_result_t r[3];
            if (vmin_bisect(&c, t, f, V_LO, V_HI, 1, 1, &r[0]) != VMIN_OK) continue;
            if (vmin_bisect(&c, t, f, V_LO, V_HI, 1, 8, &r[1]) != VMIN_OK) continue;
            if (vmin_robust(&c, t, f, V_LO, V_HI, 1, 8, 200, 40, &r[2]) != VMIN_OK) continue;
            int v[4] = { r[0].vmin, r[1].vmin, r[2].vmin, r[2].vmin + 10 };
            uint32_t ru[4] = { r[0].runs, r[1].runs, r[2].runs, r[2].runs };
            n++;
            for (int s = 0; s < 4; s++) {
                runs[s] += ru[s];
                over[s] += v[s] - hi_t;                                    /* +: 보수적, -: 낙관적 */
                if (!confirm_pass(&c, t, f, v[s], 2000, 99)) esc[s]++;     /* 새 측정 2000회 */
            }
        }
    }
    printf("Vmin search strategies over %ld (chip, T, F) cases, noise +-2%%, fresh confirm = 2000 runs\n", n);
    printf("%-24s %10s %12s %14s %12s\n", "strategy", "escape", "avg runs", "avg time@20ms", "avg V-Vhi");
    for (int s = 0; s < 4; s++)
        printf("%-24s %9.1f%% %12.1f %13.2fs %+11.1f\n", names[s], 100.0 * esc[s] / n, (double)runs[s] / n,
               (double)runs[s] / n * 0.020, (double)over[s] / n);
    return 0;
}
