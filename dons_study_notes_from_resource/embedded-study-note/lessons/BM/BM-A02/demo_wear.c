/* BM-A02 demo — wear: 로그 구조 append vs 제자리 재작성 (모델·로그 = solution.c 그대로)
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 demo_wear.c -o demo_wear && ./demo_wear
 * 시간 추정용 대표값 (STM32F4 계열 데이터시트 Flash characteristics, x8 병렬 — 확인 권장):
 *   16 KB sector erase typ 400 ms / max 800 ms, 128 KB typ 2 s / max 4 s, word program typ 16 us / max 100 us,
 *   endurance 10 kcycles (min).
 */
#define main bm_a02_solution_main
#include "solution.c"
#undef main

#define ENDURANCE 10000u

typedef struct { uint32_t total, mx, mn; uint64_t prog_bytes; } wear_t;

static wear_t run_log(uint32_t sec_size, uint32_t n_sec, uint32_t updates) {
    flash_t *f = &g_f;
    wear_t w = { 0, 0, UINT32_MAX, 0 };
    flash_init(f, 0x08000000u, NULL, sec_size, n_sec, 4);
    log_t lg;
    uint8_t d[LOG_PAY];
    (void)log_format(&lg, f, 0, n_sec);
    for (uint32_t i = 1; i <= updates; i++) { pay_of(i, d); (void)log_append(&lg, d); }
    for (uint32_t s = 0; s < n_sec; s++) {
        if (f->erase_count[s] > w.mx) w.mx = f->erase_count[s];
        if (f->erase_count[s] < w.mn) w.mn = f->erase_count[s];
    }
    w.total = f->n_erase;
    w.prog_bytes = (uint64_t)f->n_prog * f->prog_unit;
    return w;
}

int main(void) {
    const uint32_t U = 100000;
    printf("== erase counts after %u updates of one 24 B value (32 B record incl. seq + CRC) ==\n", U);
    printf("strategy                         sectors   total erases  max/sector  min/sector  updates until %u cycles\n",
           ENDURANCE);
    printf("naive (erase + rewrite in place)  1 x 1 KB  %12u  %10u  %10u  %u\n", U, U, U, ENDURANCE);
    static const uint32_t secs[3][2] = { { 1024, 2 }, { 1024, 4 }, { 1024, 8 } };
    for (int i = 0; i < 3; i++) {
        wear_t w = run_log(secs[i][0], secs[i][1], U);
        uint64_t life = (uint64_t)ENDURANCE * U / w.mx;
        printf("log append                        %u x 1 KB  %12u  %10u  %10u  ~%llu\n", secs[i][1], w.total, w.mx,
               w.mn, (unsigned long long)life);
    }
    {
        wear_t w = run_log(16384, 2, U);
        uint64_t life = (uint64_t)ENDURANCE * U / w.mx;
        printf("log append (F4 EEPROM-emul. size) 2 x 16 KB %11u  %10u  %10u  ~%llu\n", w.total, w.mx, w.mn,
               (unsigned long long)life);
        printf("\n== time per update on an F4-class part (representative values, x8; check the datasheet) ==\n");
        double naive_ms = 400.0 + 8 * 0.016;                         /* 16 KB erase + 8 words */
        double log_ms = (double)w.total * 400.0 / U + 8 * 0.016;
        printf("naive, 16 KB sector : %.1f ms per update (typ), worst %.1f ms\n", naive_ms, 800.0 + 8 * 0.1);
        printf("log,  2 x 16 KB     : %.2f ms per update on average (typ), but one update in %u still pays a %.0f ms erase\n",
               log_ms, U / (w.total ? w.total : 1), 400.0);
        printf("128 KB sector erase : typ 2000 ms, max 4000 ms -> the CPU stalls if it fetches code from the same bank\n");
    }
    return 0;
}
