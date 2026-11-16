/* BM-A04 demo — 검증 비용과 검출 범위 (관찰용, CHECK 없음)
 *
 *  1) 호스트에서 CRC-32(표) / CRC-32(비트 단위) / SHA-256 처리량 '비율' (호스트 부하에 따라 절대값은 흔들림)
 *  2) 단일 비트 플립 지도: 헤더 필드별로 어떤 코드로 거부되나 (solution.c 의 검사 순서를 축약 복사)
 *  3) 타깃 빌드: 같은 sha256_compress 를 Cortex-M4 Thumb-2 로 컴파일해 명령어 수를 센다 (아래 명령)
 *
 *  호스트: cc -std=c11 -Wall -Wextra -Werror -O2 demo_cost.c -o demo_cost && ./demo_cost
 *  타깃:   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fno-unroll-loops -ffreestanding \
 *              -DTARGET_BUILD -c demo_cost.c -o /tmp/BM-A04_m4.o && xcrun llvm-objdump -d --no-show-raw-insn /tmp/BM-A04_m4.o
 *          → loop 별 명령어 수 × 반복 횟수를 손으로(또는 md 의 표대로) 더한다
 */
#include <stdint.h>
#include <stddef.h>

/* ---- SHA-256 압축 함수 (solution.c 와 같은 코드) ---- */
static const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static uint32_t ld_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
void sha256_compress(uint32_t h[8], const uint8_t blk[64]);   /* 타깃 빌드에서 심볼이 남도록 extern */
void sha256_compress(uint32_t h[8], const uint8_t blk[64]) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++) w[t] = ld_be32(blk + 4 * t);
    for (int t = 16; t < 64; t++) {
        uint32_t s0 = ROR32(w[t - 15], 7) ^ ROR32(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = ROR32(w[t - 2], 17) ^ ROR32(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
        uint32_t t1 = hh + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + SHA_K[t] + w[t];
        uint32_t t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/* ---- CRC-32 표 방식 (표는 호스트에선 crc_init 이 채움; 타깃 빌드에선 명령어 수만 본다) ---- */
uint32_t crc_tab[256];
uint32_t crc32_table(const uint8_t *p, size_t n);
uint32_t crc32_table(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

#ifndef TARGET_BUILD
#include <stdio.h>
#include <string.h>
#include <time.h>

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        crc_tab[i] = c;
    }
}
static uint32_t crc32_bitwise(const uint8_t *p, size_t n) {     /* 표 1 KB 를 아끼는 대신 8 배 루프 */
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static uint32_t sha_blocks(const uint8_t *p, size_t n) {         /* 패딩 없이 블록만 (비용 측정용) */
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (size_t i = 0; i + 64 <= n; i += 64) sha256_compress(h, p + i);
    return h[0];
}
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#define BUF (384u * 1024u)                                       /* 이 보드의 실행 영역 최대 이미지 */
static uint8_t buf[BUF];

int main(void) {
    crc_init();
    uint32_t x = 0x1234567u;
    for (size_t i = 0; i < BUF; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; buf[i] = (uint8_t)x; }

    printf("1) 384 KB 이미지 검증 비용 (호스트, 같은 버퍼 반복; 절대값은 부하에 따라 변함 — 비율을 볼 것)\n");
    const char *name[3] = {"CRC-32 table (1 KB 표)", "CRC-32 bitwise (표 없음)", "SHA-256 (블록만)"};
    double mbps[3];
    volatile uint32_t sink = 0;
    for (int k = 0; k < 3; k++) {
        int reps = k == 1 ? 8 : 40;
        double t0 = now_s();
        for (int r = 0; r < reps; r++)
            sink += k == 0 ? crc32_table(buf, BUF) : k == 1 ? crc32_bitwise(buf, BUF) : sha_blocks(buf, BUF);
        double dt = now_s() - t0;
        mbps[k] = (double)BUF * reps / dt / 1e6;
    }
    for (int k = 0; k < 3; k++)
        printf("   %-26s %8.1f MB/s   (CRC table 대비 %.2fx 느림)\n", name[k], mbps[k], mbps[0] / mbps[k]);
    (void)sink;

    printf("\n2) 64 B 헤더 단일 비트 플립 지도 (solution.c 의 검사 순서)\n");
    static const struct { const char *field; int lo, hi; const char *code; } map[] = {
        {"magic        [0,4)", 0, 4, "BAD_MAGIC"},
        {"hdr_version  [4,6)", 4, 6, "BAD_HDR_VERSION"},
        {"hdr_size     [6,8)", 6, 8, "BAD_HDR_VERSION"},
        {"img_version..sha256 [8,60)", 8, 60, "BAD_HDR_CRC"},
        {"hdr_crc32    [60,64)", 60, 64, "BAD_HDR_CRC"},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        printf("   %-28s %3d 비트 → %s\n", map[i].field, (map[i].hi - map[i].lo) * 8, map[i].code);
    printf("   payload                      모든 비트 → BAD_CRC (CRC-32 는 32 비트 이하 버스트를 전부 검출)\n");

    printf("\n3) 384 KB 를 SHA-256 하려면 블록 %u 개, CRC 는 바이트 %u 번\n", BUF / 64u, BUF);
    printf("\n4) SHA-256 패딩: 메시지 n 바이트 → 압축 블록 수 = ceil((n + 9) / 64)\n   ");
    static const unsigned ns[] = {0, 3, 55, 56, 63, 64, 119, 120, 256, 393216};
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) printf("n=%u→%u  ", ns[i], (ns[i] + 9u + 63u) / 64u);
    printf("\n");
    return 0;
}
#endif
