/* ALGO-A06 demo: (1) 블록 크기별 압축률  (2) 단계별 인코드 시간 + 전체 디코드 시간  (3) zlib(DEFLATE) 참고 비교
 * 측정 = 워밍업 1회 + 7회 중 중앙값, [최소 – 최대] 를 같이 찍는다. 결과는 매번 왕복 검증 / 체크섬으로 최적화 제거를 막는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm -lz && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#define ALGO_A06_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"
#include <stdlib.h>
#include <time.h>
#include <zlib.h>

static double now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e9 + ts.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
#define RUNS 7
static volatile uint64_t sink;

typedef struct { const char *name; const int16_t *x; size_t nframes; int nch; } ds_t;

/* 한 데이터셋 전체를 blk 프레임 블록으로 처리하는 단계 s 의 시간 (ns) */
enum { ST_DZ, ST_V, ST_R, ST_H, ST_ALL, ST_DEC, ST_N };
static const char *const st_name[ST_N] = { "delta+zz", "varint", "RLE", "Huffman(VRH)", "encode total", "decode total" };
static uint16_t b_z[MAX_SAMPLES]; static uint8_t b_v[MAX_VARINT + 4], b_r[MAX_RLE + 4], b_h[MAX_HUF], b_blk[MAX_BLOCK];
static int16_t b_y[MAX_SAMPLES];
static double run_stage(const ds_t *d, size_t blk, int s) {
    double t0 = now_ns(), dec = 0;
    uint64_t acc = 0;
    for (size_t f0 = 0; f0 < d->nframes; f0 += blk) {
        size_t nf = d->nframes - f0 < blk ? d->nframes - f0 : blk, n = nf * (size_t)d->nch;
        const int16_t *x = d->x + f0 * (size_t)d->nch;
        if (s == ST_ALL || s == ST_DEC) {
            size_t b = pipe_encode(x, nf, d->nch, b_blk, sizeof b_blk, NULL);
            if (s == ST_DEC) {                              /* 디코드 시간만 따로 합산 */
                double t1 = now_ns(); int c;
                long r = pipe_decode(b_blk, b, b_y, MAX_SAMPLES, &c);
                dec += now_ns() - t1;
                if (r != (long)nf || memcmp(b_y, x, n * sizeof *x)) { printf("ROUNDTRIP FAIL\n"); exit(1); }
                acc += (uint64_t)r;
            } else acc += b;
            continue;
        }
        delta16_enc(x, n, d->nch, b_z);
        for (size_t i = 0; i < n; i++) b_z[i] = zz16_enc(b_z[i]);
        if (s == ST_DZ) { acc += b_z[n / 2]; continue; }
        size_t nv = varint_enc(b_z, n, b_v);
        if (s == ST_V) { acc += nv; continue; }
        size_t nr = rle_enc(b_v, nv, b_r);
        if (s == ST_R) { acc += nr; continue; }
        acc += huff_block_enc(b_r, nr, b_h, sizeof b_h, NULL);
    }
    sink += acc;
    return s == ST_DEC ? dec : now_ns() - t0;
}

int main(void) {
    gen_temp(g_temp, TEMP_N, 2026);
    gen_accel(g_acc, ACC_F, 1111);
    ds_t ds[2] = { { "temperature", g_temp, TEMP_N, 1 }, { "accel xyz", g_acc, ACC_F, 3 } };

    printf("== (1) block size vs compressed size (ours: chosen mode per block incl. 4 B block header) ==\n");
    printf("%-12s %8s %10s %8s %8s %12s %12s\n", "data", "samples", "raw B", "ours B", "ours %", "zlib9(raw)%", "zlib9(var)%");
    for (int k = 0; k < 2; k++) {
        const size_t blks[] = { 128, 512, 2048, 8192, 16384 };
        for (int b = 0; b < 5; b++) {
            size_t blk = blks[b] / (size_t)ds[k].nch, tot = 0, zr = 0, zv = 0, raw = 0;
            static uint8_t zb[2 * MAX_SAMPLES + 1024], rawb[2 * MAX_SAMPLES];
            for (size_t f0 = 0; f0 < ds[k].nframes; f0 += blk) {
                size_t nf = ds[k].nframes - f0 < blk ? ds[k].nframes - f0 : blk, n = nf * (size_t)ds[k].nch;
                const int16_t *x = ds[k].x + f0 * (size_t)ds[k].nch;
                pstat_t st;
                tot += pipe_encode(x, nf, ds[k].nch, b_blk, sizeof b_blk, &st);
                for (size_t i = 0; i < n; i++) { uint16_t u = i16_bits(x[i]); rawb[2 * i] = (uint8_t)u; rawb[2 * i + 1] = (uint8_t)(u >> 8); }
                uLongf zl = sizeof zb;
                if (compress2(zb, &zl, rawb, 2 * n, 9) != Z_OK) return 1;
                zr += zl;
                zl = sizeof zb;
                if (compress2(zb, &zl, st.vb, st.varint, 9) != Z_OK) return 1;
                zv += zl;
                raw += 2 * n;
            }
            printf("%-12s %8zu %10zu %8zu %7.1f%% %11.1f%% %11.1f%%\n", ds[k].name, blk * (size_t)ds[k].nch, raw, tot,
                   100.0 * (double)tot / (double)raw, 100.0 * (double)zr / (double)raw, 100.0 * (double)zv / (double)raw);
        }
    }

    printf("\n== (2) time per stage, cumulative (block = 4096 samples), ns/sample: median [min - max] of %d ==\n", RUNS);
    for (int k = 0; k < 2; k++) {
        size_t blk = 4096 / (size_t)ds[k].nch, N = ds[k].nframes * (size_t)ds[k].nch;
        for (int s = 0; s < ST_N; s++) {
            double t[RUNS];
            run_stage(&ds[k], blk, s);                      /* 워밍업 */
            for (int r = 0; r < RUNS; r++) t[r] = run_stage(&ds[k], blk, s) / (double)N;
            qsort(t, RUNS, sizeof t[0], cmpd);
            printf("%-12s %-14s %7.2f [%6.2f - %6.2f]  (%.0f MB/s of raw int16)\n", ds[k].name, st_name[s], t[RUNS / 2], t[0], t[RUNS - 1],
                   2.0 / t[RUNS / 2] * 1e3);
        }
    }
    printf("(sink %llu)\n", (unsigned long long)sink);
    return 0;
}
