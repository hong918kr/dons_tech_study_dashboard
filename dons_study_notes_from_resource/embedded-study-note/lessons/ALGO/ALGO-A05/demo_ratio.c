/* ALGO-A05 demo — 실제 데이터에서 압축률 (W × min_match), 인코더 속도 (naive vs 해시 체인), 디코더 속도, zlib 비교
 *
 * 데이터: argv[1..] = 실제 바이너리 (예: /bin/ls, /bin/zsh 의 arm64e 조각 — "펌웨어 이미지" 대용, 각 ≤ 1 MiB),
 *         그리고 합성 UART 로그 128 KiB.
 * solution.c 를 그대로 #include 한다 (ALGO_A05_NO_MAIN) → 측정하는 코드 = 테스트한 코드.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_ratio.c -o demo_ratio -lz
 * run:   lipo -thin arm64e /bin/ls  -output "$TMPDIR/ls_arm64e"
 *        lipo -thin arm64e /bin/zsh -output "$TMPDIR/zsh_arm64e"
 *        ./demo_ratio "$TMPDIR/ls_arm64e" "$TMPDIR/zsh_arm64e"
 */
#define ALGO_A05_NO_MAIN
#include "solution.c"
#include <time.h>
#include <zlib.h>

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void make_log(uint8_t *b, size_t n, uint32_t *rng) {        /* solution.c 하네스와 같은 생성기 */
    char line[96]; size_t o = 0; uint32_t t = 1000;
    while (o < n) {
        t += 7 + xs(rng) % 6;
        int len, k = (int)(xs(rng) % 10);
        if (k < 6) len = snprintf(line, sizeof line, "[%07u.%03u] imu: ax=%d ay=%d az=%d\n", (unsigned)(t / 1000),
                                  (unsigned)(t % 1000), (int)(xs(rng) % 41) - 20, (int)(xs(rng) % 41) - 20, 990 + (int)(xs(rng) % 21));
        else if (k < 8) len = snprintf(line, sizeof line, "[%07u.%03u] bat: v=%umV t=%dC\n", (unsigned)(t / 1000),
                                       (unsigned)(t % 1000), 3900u + xs(rng) % 40, 23 + (int)(xs(rng) % 3));
        else if (k < 9) len = snprintf(line, sizeof line, "[%07u.%03u] gps: fix=%u sats=%u\n", (unsigned)(t / 1000),
                                       (unsigned)(t % 1000), 3u, 7u + xs(rng) % 5);
        else len = snprintf(line, sizeof line, "[%07u.%03u] W: spi1 timeout, retry %u\n", (unsigned)(t / 1000),
                            (unsigned)(t % 1000), 1u + xs(rng) % 3);
        for (int k2 = 0; k2 < len && o < n; k2++) b[o++] = (uint8_t)line[k2];
    }
}
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

#define RUNS 5
typedef struct { uint8_t *buf; size_t len; } dsink_t;            /* 스트리밍 디코더용 메모리 싱크 (limit 이 길이를 보장) */
static void dsink_put(void *ctx, uint8_t b) { dsink_t *d = ctx; d->buf[d->len++] = b; }
static lz_hash_t hs;
static volatile long sink;

/* 한 설정의 인코딩 시간: 워밍업 1회 + RUNS 회, 중앙값·최소·최대 (ms) */
static void time_encode(const char *label, const lz_params *p, lz_method m, const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    double t[RUNS]; long c = 0;
    c = lz_encode(p, m, &hs, in, n, out, cap);
    for (int r = 0; r < RUNS; r++) { double t0 = now_ms(); c = lz_encode(p, m, &hs, in, n, out, cap); t[r] = now_ms() - t0; sink += c; }
    qsort(t, RUNS, sizeof t[0], cmpd);
    printf("  %-26s %6.1f%%  median %8.2f ms  [%.2f..%.2f]  %7.1f MB/s\n", label, 100.0 * (double)c / (double)n, t[RUNS / 2], t[0],
           t[RUNS - 1], (double)n / 1e3 / t[RUNS / 2]);
}

static void dump(const char *name, const uint8_t *in, size_t n, uint8_t *out, size_t cap, uint8_t *back) {
    if (lz_bound(n) > cap) { printf("%s: too big\n", name); return; }    /* 출력 버퍼 ≥ 최악 크기 */
    printf("\n### %s: %zu bytes\n", name, n);
    printf("ratio = compressed/original (%%), greedy, full search (hash chain unlimited == naive output, tested in solution.c)\n");
    printf("   W     min2    min3    min4   max_len(min3)\n");
    for (unsigned ob = 8; ob <= 12; ob++) {
        printf("%5u", 1u << ob);
        for (unsigned mm = 2; mm <= 4; mm++) {
            lz_params p = { ob, mm, 0 };
            long c = lz_encode(&p, LZ_HASH, &hs, in, n, out, cap);
            if (lz_decode(out, (size_t)c, back, n) != (long)n || memcmp(back, in, n)) { printf(" RT-FAIL"); continue; }
            printf("  %5.1f%%", 100.0 * (double)c / (double)n);
        }
        lz_params p3 = { ob, 3, 0 };
        printf("   %zu\n", lz_maxlen(&p3));
    }
    printf("encoder speed (W=4096 and W=1024, min 3):\n");
    for (unsigned ob = 12; ob >= 10; ob -= 2) {
        char lab[64];
        lz_params p = { ob, 3, 0 };
        snprintf(lab, sizeof lab, "W=%u naive", 1u << ob);            time_encode(lab, &p, LZ_NAIVE, in, n, out, cap);
        snprintf(lab, sizeof lab, "W=%u hash chain=all", 1u << ob);   time_encode(lab, &p, LZ_HASH, in, n, out, cap);
        p.max_chain = 16; snprintf(lab, sizeof lab, "W=%u hash chain=16", 1u << ob); time_encode(lab, &p, LZ_HASH, in, n, out, cap);
        p.max_chain = 4;  snprintf(lab, sizeof lab, "W=%u hash chain=4", 1u << ob);  time_encode(lab, &p, LZ_HASH, in, n, out, cap);
    }
    {   /* 디코더 속도: 평면 vs 스트리밍(바이트 push + 싱크 콜백) */
        lz_params p = { 10, 3, 0 };
        long c = lz_encode(&p, LZ_HASH, &hs, in, n, out, cap);
        double tf[RUNS], tsr[RUNS];
        static uint8_t ring[LZ_WMAX];
        for (int r = -1; r < RUNS; r++) {
            double t0 = now_ms(); long d = lz_decode(out, (size_t)c, back, n); double t1 = now_ms();
            dsink_t ms = { back, 0 }; lzs_t s;
            lzs_init(&s, ring, sizeof ring, (uint32_t)n, dsink_put, &ms);
            for (long k = 0; k < c; k++) lzs_push(&s, out[k]);
            double t2 = now_ms(); sink += d + lzs_finish(&s);
            if (r >= 0) { tf[r] = t1 - t0; tsr[r] = t2 - t1; }
        }
        qsort(tf, RUNS, sizeof tf[0], cmpd); qsort(tsr, RUNS, sizeof tsr[0], cmpd);
        printf("decoder (W=1024 stream): flat median %.3f ms (%.0f MB/s out), streaming push median %.3f ms (%.0f MB/s out)\n",
               tf[RUNS / 2], (double)n / 1e3 / tf[RUNS / 2], tsr[RUNS / 2], (double)n / 1e3 / tsr[RUNS / 2]);
    }
    printf("zlib (DEFLATE: 32 KiB window + Huffman), same input:");
    for (int lv = 1; lv <= 9; lv += 4) {
        uLongf zl = compressBound((uLong)n);
        static uint8_t zb[1200000];
        if (zl > sizeof zb || compress2(zb, &zl, in, (uLong)n, lv) != Z_OK) { printf(" level %d: error", lv); continue; }
        printf("  level %d %.1f%%", lv, 100.0 * (double)zl / (double)n);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    static uint8_t in[1u << 20], out[(1u << 20) + (1u << 17) + 16], back[1u << 20];
    printf("zlib %s\n", zlibVersion());
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { perror(argv[a]); return 1; }
        size_t n = fread(in, 1, sizeof in, f), z = 0;
        fclose(f);
        for (size_t i = 0; i < n; i++) z += in[i] == 0;
        const char *base = strrchr(argv[a], '/');
        char name[128];
        snprintf(name, sizeof name, "%s (%.1f%% zero bytes)", base ? base + 1 : argv[a], 100.0 * (double)z / (double)n);
        dump(name, in, n, out, sizeof out, back);
    }
    uint32_t rng = 0x106u;
    make_log(in, 131072, &rng);
    dump("synthetic UART log", in, 131072, out, sizeof out, back);
    printf("(sink %ld)\n", sink);
    return 0;
}
