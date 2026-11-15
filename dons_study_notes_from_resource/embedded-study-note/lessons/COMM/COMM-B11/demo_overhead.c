/* COMM-B11 demo: SLIP 오버헤드 측정 — 데이터 종류 × 길이별 인코딩 크기와 115200 8N1 전송 시간
 *
 * 인코더는 solution.c 의 slip_encode 와 같은 규칙 (lead END 사용 → 최악 2n+2).
 * 랜덤은 xorshift32 고정 시드, 길이마다 10000회. 이론 평균 = n + n·2/256 + 2 (이스케이프 확률 2/256).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_overhead.c -o demo_overhead && ./demo_overhead
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define END 0xC0u
#define ESC 0xDBu

static size_t slip_len(const uint8_t *p, size_t n) {          /* lead END + 본문 + 끝 END 의 바이트 수 */
    size_t k = 2;
    for (size_t i = 0; i < n; i++) k += (p[i] == END || p[i] == ESC) ? 2 : 1;
    return k;
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static double ms_8n1(double bytes, double baud) { return bytes * 10.0 / baud * 1000.0; }

int main(void) {
    static const size_t NS[] = { 16, 64, 256, 1006 };
    static uint8_t buf[1006];
    uint32_t seed = 12345u;

    printf("%-26s %5s %9s %9s %9s %8s %9s\n", "data", "n", "min", "mean", "max", "ovh%", "ms@115k2");
    for (size_t k = 0; k < sizeof NS / sizeof NS[0]; k++) {
        size_t n = NS[k];
        /* 균일 랜덤 10000회 */
        size_t mn = (size_t)-1, mx = 0; double sum = 0, sq = 0;
        for (int t = 0; t < 10000; t++) {
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            size_t L = slip_len(buf, n);
            if (L < mn) mn = L; if (L > mx) mx = L;
            sum += (double)L; sq += (double)L * (double)L;
        }
        double mean = sum / 10000.0, sd = sqrt(sq / 10000.0 - mean * mean);
        printf("%-26s %5zu %9zu %9.2f %9zu %7.2f%% %9.3f   (sd %.2f | theory mean %.2f, sd %.2f)\n", "uniform random x10000", n, mn, mean, mx,
               (mean - (double)n) * 100.0 / (double)n, ms_8n1(mean, 115200), sd, (double)n + (double)n * 2 / 256 + 2,
               sqrt((double)n * (2.0 / 256) * (254.0 / 256)));
        /* 고정 패턴들 */
        struct { const char *name; int kind; } pats[] = {
            { "all 0x00", 0 }, { "ASCII text", 1 }, { "float32 LE in (-4,-2]", 2 }, { "all 0xC0 (END)", 3 }, { "all 0xDB (ESC)", 4 } };
        for (size_t q = 0; q < sizeof pats / sizeof pats[0]; q++) {
            static const char txt[] = "AT+CSQ\r\n+CSQ: 21,99\r\nOK\r\ntemp=23.5C rh=41% ";
            for (size_t i = 0; i < n; i++) {
                switch (pats[q].kind) {
                case 0: buf[i] = 0x00; break;
                case 1: buf[i] = (uint8_t)txt[i % (sizeof txt - 1)]; break;
                case 2: if (i % 4 == 0) {                         /* (-4,-2] 랜덤 float, 리틀 엔디언으로 4바이트 */
                            float f = -2.0f - 2.0f * (float)(xs32(&seed) >> 8) / 16777216.0f;
                            uint32_t u; memcpy(&u, &f, 4);
                            for (size_t j = 0; j < 4 && i + j < n; j++) buf[i + j] = (uint8_t)(u >> (8 * j));
                        } break;
                case 3: buf[i] = 0xC0; break;
                default: buf[i] = 0xDB; break;
                }
            }
            size_t L = slip_len(buf, n);
            printf("%-26s %5zu %9zu %9zu %9zu %7.2f%% %9.3f\n", pats[q].name, n, L, L, L,
                   (double)(L - n) * 100.0 / (double)n, ms_8n1((double)L, 115200));
        }
        printf("\n");
    }
    printf("worst case 2n+2 for n=1006: %u bytes = %.3f ms at 115200 8N1 (payload alone %.3f ms)\n",
           2 * 1006 + 2, ms_8n1(2014, 115200), ms_8n1(1006, 115200));
    return 0;
}
