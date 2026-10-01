/*
 * C41_ffs_amortize.c — FFS 의 두 가지 숫자 감각
 *   1) amortization: 위치 잡기 10ms, 전송 40MB/s 디스크에서
 *      peak 대역폭의 F% 를 얻으려면 한 번 seek 할 때 얼마나(chunk) 읽어야 하나?
 *        D = F/(1-F) * R * T   (41장 식 41.1 을 일반화, 43장 식 43.6 과 동일)
 *   2) 큰 파일 예외: 파일을 chunk 단위로 그룹에 흩뿌렸을 때의 실효 대역폭
 *   3) 4KB 블록 vs 512B sub-block 의 내부 단편화(2KB 파일이 많을 때)
 *
 * build: cc -Wall -Wextra -O0 code/C41_ffs_amortize.c -o .work/bin/C41_ffs_amortize
 */
#include <stdio.h>

int main(void) {
    const double T = 0.010;            /* positioning time: 10 ms */
    const double R = 40.0 * 1024;      /* 40 MB/s 를 KB/s 로 (원문이 1MB=1024KB 로 계산) */

    printf("== 1. chunk size needed for F%% of peak (T=10ms, R=40MB/s) ==\n");
    double Fs[] = { 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99 };
    for (unsigned i = 0; i < sizeof Fs / sizeof Fs[0]; i++) {
        double F = Fs[i];
        double D = F / (1 - F) * R * T; /* KB */
        printf("F=%3.0f%%  chunk = %8.1f KB  (= %6.2f MiB)\n", F * 100, D, D / 1024);
    }

    printf("\n== 2. large-file exception: effective bandwidth per chunk size ==\n");
    printf("chunk     transfer   seek   effective BW   %%peak\n");
    double chunksKB[] = { 4, 48, 409.6, 4096, 40 * 1024 };
    for (unsigned i = 0; i < sizeof chunksKB / sizeof chunksKB[0]; i++) {
        double c = chunksKB[i];
        double xfer = c / R; /* s */
        double eff = c / (xfer + T) / 1024; /* MB/s */
        printf("%7.1fKB  %6.2fms  10ms   %7.2f MB/s   %5.1f%%\n", c, xfer * 1000, eff,
               eff / 40.0 * 100);
    }
    printf("(FFS spread every indirect block's 1024 blocks = 4MB per group)\n");

    printf("\n== 3. internal fragmentation per file: 1KB / 2KB / 5KB ==\n");
    int sizes[] = { 1024, 2048, 5 * 1024 };
    for (unsigned i = 0; i < 3; i++) {
        int s = sizes[i];
        int b4k = (s + 4095) / 4096 * 4096;
        /* FFS: 마지막 꼬리 부분만 512B sub-block(fragment) 로 */
        int full = s / 4096 * 4096, tail = s - full;
        int sub = full + (tail + 511) / 512 * 512;
        printf("file %5dB : 4KB-only uses %5dB (waste %4.1f%%)   with 512B sub-blocks %5dB (waste %4.1f%%)\n",
               s, b4k, 100.0 * (b4k - s) / b4k, sub, 100.0 * (sub - s) / sub);
    }
    return 0;
}
