/* ALGO-M13 넘침(overflow) 범위 표 — 계산 + 실제 감김 경계 실측
 * 1) 프레임 크기 × 픽셀 비트 수 → integral image 최대값과 필요한 비트 수, uint32 에 들어가나
 * 2) "감겨도 차는 정확" 규칙으로 쓸 수 있는 최대 창 크기 (uint16 / uint32 누산, 값 / 제곱)
 *    → 공식값을 모든 값이 최대인 입력으로 직접 감아 보며 처음 틀리는 크기와 대조
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_overflow.c -o demo_overflow && ./demo_overflow
 */
#include <stdint.h>
#include <stdio.h>

static int bits_needed(uint64_t v) { int b = 0; while (v) { b++; v >>= 1; } return b; }

/* 모든 값이 vmax 인 입력을 acc_bits 비트 누산기로 감아 더할 때, 처음으로 차가 틀리는 개수(실측) */
static uint64_t first_wrong(uint64_t vmax, int acc_bits) {
    uint64_t mask = (acc_bits == 64) ? ~0ull : ((1ull << acc_bits) - 1);
    uint64_t wrapped = 0, exact = 0;
    for (uint64_t k = 1;; k++) {
        wrapped = (wrapped + vmax) & mask;
        exact += vmax;
        if (wrapped != exact) return k;
    }
}

int main(void) {
    const struct { const char *name; uint64_t w, h; } fr[] = {
        { "320x240 QVGA", 320, 240 }, { "640x480 VGA", 640, 480 }, { "1280x720 HD", 1280, 720 },
        { "1920x1080 FHD", 1920, 1080 }, { "4096x3072 12MP", 4096, 3072 } };
    const int pb[] = { 8, 10, 12, 16 };

    printf("1) integral image max = W*H*(2^b-1): bits needed (uint32 ok?)\n");
    printf("%-16s", "frame \\ pixel");
    for (int j = 0; j < 4; j++) printf("  %2d-bit           ", pb[j]);
    printf("\n");
    for (int i = 0; i < 5; i++) {
        printf("%-16s", fr[i].name);
        for (int j = 0; j < 4; j++) {
            uint64_t mx = fr[i].w * fr[i].h * ((1ull << pb[j]) - 1);
            printf("  %13llu %2db%s", (unsigned long long)mx, bits_needed(mx), bits_needed(mx) <= 32 ? " " : "*");
        }
        printf("\n");
    }
    printf("   (* = full-frame sum does not fit uint32; integral of squares adds b more bits)\n");
    uint64_t sq = 640ull * 480 * 255 * 255;
    printf("   640x480 8-bit sum of squares = %llu -> %d bits\n\n", (unsigned long long)sq, bits_needed(sq));

    printf("2) largest window (number of samples/pixels) whose TRUE sum still fits -> wrapped difference exact\n");
    printf("%-10s %-8s %14s %14s %14s\n", "value", "acc", "formula kmax", "first wrong", "check");
    const struct { const char *name; uint64_t vmax; int acc; } c[] = {
        { "8-bit", 255, 16 }, { "12-bit", 4095, 16 }, { "8-bit", 255, 32 }, { "12-bit", 4095, 32 },
        { "16-bit", 65535, 32 }, { "8-bit^2", 65025, 32 }, { "12-bit^2", 4095ull * 4095, 32 } };
    for (int i = 0; i < 7; i++) {
        uint64_t lim = (1ull << c[i].acc) - 1, kmax = lim / c[i].vmax;
        uint64_t fw = first_wrong(c[i].vmax, c[i].acc);
        printf("%-10s uint%-4d %14llu %14llu %14s\n", c[i].name, c[i].acc,
               (unsigned long long)kmax, (unsigned long long)fw, fw == kmax + 1 ? "kmax+1 OK" : "MISMATCH");
    }

    printf("\n3) 1D ADC stream: 12-bit samples into a uint32 prefix\n");
    uint64_t nwrap = (1ull << 32) / 4095 + 1;        /* 처음으로 P 가 2^32 이상이 되는 샘플 수 (전부 4095 일 때) */
    printf("   P first wraps after %llu full-scale samples = %.3f s at 1 MS/s, %.1f s at 100 kS/s\n",
           (unsigned long long)nwrap, nwrap / 1e6, nwrap / 1e5);
    printf("   but any window of <= %llu samples (%.3f s at 1 MS/s) is still exact\n",
           (unsigned long long)(0xFFFFFFFFull / 4095), (0xFFFFFFFFull / 4095) / 1e6);
    return 0;
}
