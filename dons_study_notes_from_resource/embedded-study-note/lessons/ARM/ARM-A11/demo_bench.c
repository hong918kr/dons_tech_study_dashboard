/* ARM-A11 demo_bench: 커널별 ns/원소 (21회 표본의 중앙값·최솟값) — 테스트 아님. 부하·코어 종류에 따라 흔들린다 → 비율로 읽는다
 *   bash demo_bench.sh > bench_mac.txt   (solution.c 를 -O2 와 -Os 로 따로 컴파일해 이 파일과 링크)
 * 커널은 다른 번역 단위(solution.o)에 있으므로 인라인·상수 접기가 일어나지 않는다.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ae_stats { uint32_t hist[256]; uint64_t sum; uint32_t bright; };
void saxpy_plain(float *y, const float *x, float a, size_t n);
void saxpy_restrict(float *restrict y, const float *restrict x, float a, size_t n);
void gain_inplace(float *buf, float g, float b, size_t n);
float csum_f32_if(const float *a, size_t n, float t);
float csum_f32_sel(const float *a, size_t n, float t);
float csum_f32_fast(const float *a, size_t n, float t);
int64_t csum_i32(const int32_t *a, size_t n, int32_t t);
int32_t dot_i16(const int16_t *a, const int16_t *b, size_t n);
void hist_plain(uint32_t *h, const uint8_t *p, size_t n);
void hist4(uint32_t *h, const uint8_t *p, size_t n);
void ae_fused(struct ae_stats *st, const uint8_t *img, size_t n, uint8_t thr);
void ae_frame_stats(struct ae_stats *st, const uint8_t *img, size_t n, uint8_t thr);

#define NF 4096
#define NB 65536
static float fx[NF], fy[NF];
static int32_t ia[NF];
static int16_t sa[NF], sb[NF];
static uint8_t rnd8[NB], same8[NB];
static uint32_t h[256];
static struct ae_stats st;
static volatile double sink;

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

enum { SAXPY, SAXPY_INPLACE, SAXPY_R, GAIN, CS_IF, CS_SEL, CS_FAST, CS_I32, DOT16, H_RND, H_SAME, H4_RND, H4_SAME, AE_FUSED, AE_SPLIT, NK };
static const char *name[NK] = { "saxpy_plain (x,y disjoint)", "saxpy_plain (y == x, in-place)", "saxpy_restrict",
    "gain_inplace", "csum_f32_if", "csum_f32_sel (ordered)", "csum_f32_fast (reassoc)", "csum_i32", "dot_i16",
    "hist_plain (random bytes)", "hist_plain (all same byte)", "hist4 (random bytes)", "hist4 (all same byte)",
    "ae_fused (hist+sum+count)", "ae_frame_stats (split)" };

static void run(int k) {
    switch (k) {
    case SAXPY: saxpy_plain(fy, fx, 0.5f, NF); break;
    case SAXPY_INPLACE: saxpy_plain(fy, fy, -0.5f, NF); break;   /* y = 0.5 y: 값이 커지지 않는다 */
    case SAXPY_R: saxpy_restrict(fy, fx, 0.5f, NF); break;
    case GAIN: gain_inplace(fy, 0.999f, 0.001f, NF); break;
    case CS_IF: sink += csum_f32_if(fx, NF, 0.0f); break;
    case CS_SEL: sink += csum_f32_sel(fx, NF, 0.0f); break;
    case CS_FAST: sink += csum_f32_fast(fx, NF, 0.0f); break;
    case CS_I32: sink += (double)csum_i32(ia, NF, 0); break;
    case DOT16: sink += dot_i16(sa, sb, NF); break;
    case H_RND: hist_plain(h, rnd8, NB); break;
    case H_SAME: hist_plain(h, same8, NB); break;
    case H4_RND: hist4(h, rnd8, NB); break;
    case H4_SAME: hist4(h, same8, NB); break;
    case AE_FUSED: ae_fused(&st, rnd8, NB, 128); break;
    case AE_SPLIT: ae_frame_stats(&st, rnd8, NB, 128); break;
    }
}

int main(int argc, char **argv) {
    for (int i = 0; i < NF; i++) {
        fx[i] = (float)((int64_t)(xs() % 2001) - 1000) / 8.0f;
        fy[i] = (float)((int64_t)(xs() % 2001) - 1000) / 8.0f;
        ia[i] = (int32_t)(xs() % 2000001) - 1000000;
        sa[i] = (int16_t)((int)(xs() % 401) - 200);
        sb[i] = (int16_t)((int)(xs() % 401) - 200);
    }
    for (int i = 0; i < NB; i++) { rnd8[i] = (uint8_t)(xs() >> 56); same8[i] = 0x80; }
    printf("# %s   (ns per element, median and min of 21 samples; each sample = enough calls for ~1 ms)\n", argc > 1 ? argv[1] : "");
    for (int k = 0; k < NK; k++) {
        size_t n = k >= H_RND ? NB : NF;
        int reps = 1;
        for (;;) {                                         /* 표본 하나가 ~2 ms 가 되도록 반복 수 결정 */
            uint64_t t0 = now_ns();
            for (int r = 0; r < reps; r++) run(k);
            if (now_ns() - t0 > 1000000 || reps > (1 << 20)) break;
            reps *= 2;
        }
        double s[21];
        for (int j = 0; j < 21; j++) {
            uint64_t t0 = now_ns();
            for (int r = 0; r < reps; r++) run(k);
            s[j] = (double)(now_ns() - t0) / ((double)reps * (double)n);
        }
        qsort(s, 21, sizeof s[0], cmp_d);
        printf("%-34s median %7.3f   min %7.3f\n", name[k], s[10], s[0]);
    }
    return 0;
}
