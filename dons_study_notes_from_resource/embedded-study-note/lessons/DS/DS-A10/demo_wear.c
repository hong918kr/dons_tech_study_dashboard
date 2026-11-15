/* DS-A10 demo: 링 컴팩션의 WAF(쓰기 증폭) · 소거 분포 · 마운트 재생 비용 — 관찰용, 체크 없음.
 * solution.c 를 그대로 포함해 쓴다 (main 만 이름을 바꾼다).
 * build: cc -std=c11 -O2 -w demo_wear.c -o dw && ./dw
 *
 * WAF 정의: (flash 에 프로그램한 전체 바이트) / (사용자가 요청한 레코드 바이트). 1.0 = 컴팩션 없음.
 * 시간은 macOS 시계(CLOCK_UPTIME_RAW)로 5회 측정의 중앙값. 값 비교만 의미가 있다.
 */
#define _DARWIN_C_SOURCE 1
#include <time.h>
#define main solution_main
#include "solution.c"
#undef main
#include <stdlib.h>

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static kv_t kv;
static nor_t f;

/* 키 nlive 개(64 바이트 값)를 cold 로 깔고, 키 1 개를 hot 으로 sets 번 덮어쓴다. */
static void run_fill(uint32_t sec, uint32_t nsec, uint32_t nlive, uint32_t sets) {
    nor_fresh(&f, sec, nsec, 64);
    if (kv_mount(&kv, &f) != KV_OK) { puts("mount fail"); return; }
    uint8_t v[64];
    memset(v, 0x3C, sizeof v);
    for (uint16_t k = 2; k < (uint16_t)(2u + nlive); k++) if (kv_set(&kv, k, v, 64) != KV_OK) { puts("cold set fail"); return; }
    uint64_t pb0 = f.prog_bytes, e0 = f.n_erase;
    for (uint32_t i = 0; i < sets; i++) { v[0] = (uint8_t)i; if (kv_set(&kv, 1, v, 64) != KV_OK) { puts("hot set fail"); return; } }
    uint64_t pb = f.prog_bytes - pb0, e = f.n_erase - e0;
    uint32_t emin = UINT32_MAX, emax = 0;
    for (uint32_t s = 0; s < nsec; s++) { uint32_t x = f.erase_cnt[s]; if (x < emin) emin = x; if (x > emax) emax = x; }
    double fill = 100.0 * (nlive + 1u) * 72.0 / (double)kv.cap;
    double user = (double)sets * 72.0;
    printf("| %ux%u | %u keys | %.0f%% | %.2f | %.1f | %u/%u |\n", nsec, sec, nlive + 1u, fill, (double)pb / user, 1000.0 * (double)e / sets, emin, emax);
}

int main(void) {
    crc_init();
    puts("### WAF vs live fill (8 sectors x 512B, cap = 2136B live, 1 hot key + cold 64B keys, 30000 hot sets)");
    puts("| geom | live keys | fill | WAF | erases/1k sets | wear min/max |");
    puts("|---|---|---|---|---|---|");
    for (uint32_t n = 1; n <= 28; n += (n < 8 ? 3 : 6)) run_fill(512, 8, n, 30000);
    puts("\n### WAF vs sector count (live = 8 keys = 576B, 30000 hot sets)");
    puts("| geom | live keys | fill | WAF | erases/1k sets | wear min/max |");
    puts("|---|---|---|---|---|---|");
    run_fill(512, 4, 7, 30000);
    run_fill(512, 6, 7, 30000);
    run_fill(512, 8, 7, 30000);
    run_fill(512, 16, 7, 30000);

    puts("\n### mount cost vs log occupancy (8 sectors x 4096B, median of 5 mounts, host-only)");
    puts("| log bytes replayed | sectors with data | mount µs (median) | spread |");
    puts("|---|---|---|---|");
    static kv_t kvm;
    for (uint32_t target = 2000; target <= 28000; target = (target < 16000 ? target * 2 : 28000u + (target == 28000 ? 1u : 0u))) {
        nor_fresh(&f, 4096, 8, 256);
        (void)kv_mount(&kv, &f);
        uint8_t v[64]; memset(v, 0x5A, sizeof v);
        uint32_t k = 0;
        for (;;) {
            uint32_t bytes = 0, used = 0;
            for (uint32_t s = 0; s < 8; s++) if (kv.sm[s].kind == S_ACTIVE) { bytes += (s == kv.head ? kv.head_off : kv.sm[s].end) - SH_SIZE; used++; }
            (void)used;
            if (bytes >= target) break;
            v[0] = (uint8_t)k;
            (void)kv_set(&kv, (uint16_t)(1u + k % 32u), v, 64);
            k++;
            /* 머리 섹터가 끝나면 sm[].end 가 갱신되지 않으므로 대략적이다 — 마운트로 정확한 값을 구한다 */
            if (k % 8u == 0u) (void)kv_mount(&kv, &f);
        }
        (void)kv_mount(&kv, &f);
        uint32_t replayed = 0, sectors = 0;
        for (uint32_t s = 0; s < 8; s++) if (kv.sm[s].kind == S_ACTIVE) { replayed += kv.sm[s].end - SH_SIZE; sectors++; }
        double t[5];
        for (int i = 0; i < 5; i++) {
            double a = now_us();
            (void)kv_mount(&kvm, &f);
            t[i] = now_us() - a;
        }
        qsort(t, 5, sizeof t[0], cmp_d);
        printf("| %u | %u | %.1f | %.1f–%.1f |\n", replayed, sectors, t[2], t[0], t[4]);
        if (target >= 28000) break;
    }
    return 0;
}
