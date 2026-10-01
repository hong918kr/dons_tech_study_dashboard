/*
 * C44_checksums.c — XOR / ADD / Fletcher / CRC32 를 직접 구현하고 비교
 *   1) 원문의 16바이트 XOR 예제 재현 (답: 0x201b9403)
 *   2) 4KB 블록에 여러 종류의 손상을 주입 → 각 체크섬이 "못 잡은" 횟수
 *   3) 속도 (MB/s, -O0 빌드라 절대값보다 상대 비교만)
 *   4) misdirected write / lost write: 블록 체크섬만으로는 왜 부족한가
 *
 * build: cc -Wall -Wextra -O0 code/C44_checksums.c -o .work/bin/C44_checksums
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- 체크섬 함수들 ---------------- */
static uint32_t ck_xor(const uint8_t *p, size_t n) { /* 4바이트 단위 XOR (빅엔디언으로 묶음) */
    uint32_t c = 0;
    for (size_t i = 0; i + 4 <= n; i += 4)
        c ^= (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3];
    return c;
}
static uint32_t ck_add(const uint8_t *p, size_t n) { /* 4바이트 단위 2의 보수 덧셈, overflow 무시 */
    uint32_t c = 0;
    for (size_t i = 0; i + 4 <= n; i += 4)
        c += (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3];
    return c;
}
static uint32_t ck_fletcher16(const uint8_t *p, size_t n) { /* 원문 정의: s1+=d mod 255, s2+=s1 mod 255 */
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255; s2 = (s2 + s1) % 255; }
    return s2 << 8 | s1;
}
static uint32_t crc_table[256];
static void crc_init(void) { /* CRC-32 (IEEE 802.3, reflected poly 0xEDB88320) */
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}
static uint32_t ck_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

typedef uint32_t (*ckfn)(const uint8_t *, size_t);
static const char *names[] = { "XOR32", "ADD32", "Fletcher16", "CRC32" };
static ckfn fns[] = { ck_xor, ck_add, ck_fletcher16, ck_crc32 };

/* ---------------- 손상 주입 ---------------- */
#define BLK 4096
static uint32_t seed = 42;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void fault(uint8_t *b, int kind) {
    switch (kind) {
    case 0: { /* 1비트 뒤집기 */
        uint32_t bit = rnd() % (BLK * 8);
        b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        break;
    }
    case 1: { /* 서로 다른 두 4B 워드의 "같은 위치" 비트 2개 뒤집기 */
        uint32_t w1 = rnd() % (BLK / 4), w2;
        do w2 = rnd() % (BLK / 4); while (w2 == w1);
        uint32_t bit = rnd() % 32;
        b[w1 * 4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
        b[w2 * 4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
        break;
    }
    case 2: { /* 서로 다른 두 4B 워드 자리 바꾸기 (순서 뒤바뀜) */
        uint32_t w1 = rnd() % (BLK / 4), w2, t;
        do { w2 = rnd() % (BLK / 4); } while (w2 == w1 || memcmp(b + w1 * 4, b + w2 * 4, 4) == 0);
        memcpy(&t, b + w1 * 4, 4); memcpy(b + w1 * 4, b + w2 * 4, 4); memcpy(b + w2 * 4, &t, 4);
        break;
    }
    case 3: { /* 0x00 바이트를 0xFF 로 (먼저 0x00 인 바이트를 하나 만든다) */
        uint32_t i = rnd() % BLK;
        b[i] = 0x00;
        break; /* 원본 쪽에서 0 으로 만든 뒤, 손상본에서 0xFF 로 — main 에서 처리 */
    }
    case 4: { /* 32비트 이하 연속 burst */
        uint32_t start = rnd() % (BLK * 8 - 32), len = 2 + rnd() % 31;
        for (uint32_t k = 0; k < len; k++)
            if (k == 0 || k == len - 1 || (rnd() & 1)) b[(start + k) / 8] ^= (uint8_t)(1u << ((start + k) % 8));
        break;
    }
    case 5: /* 블록 전체가 쓰레기 */
        for (int i = 0; i < BLK; i++) b[i] = (uint8_t)rnd();
        break;
    }
}
static const char *fault_names[] = { "1-bit flip", "2 bits, same col", "swap 2 words", "0x00 -> 0xFF",
                                     "burst <= 32 bits", "random garbage" };

/* ---------------- misdirected / lost write ---------------- */
typedef struct { uint32_t csum; int disk, blockno; uint8_t data[64]; } dblk_t;
static dblk_t disk0[8];

static void write_blk(int target, int actual, char c, int lost) { /* target 에 쓰려 했지만 actual 에 써짐 */
    dblk_t b;
    memset(b.data, c, sizeof b.data);
    b.csum = ck_crc32(b.data, sizeof b.data);
    b.disk = 0; b.blockno = target; /* 의도한 물리 ID 를 함께 기록 */
    if (!lost) disk0[actual] = b;
}
static void read_blk(int addr, uint32_t parent_csum) {
    dblk_t *b = &disk0[addr];
    int ok_ck = ck_crc32(b->data, sizeof b->data) == b->csum;
    int ok_id = b->blockno == addr;
    int ok_parent = ck_crc32(b->data, sizeof b->data) == parent_csum;
    printf("  read blk %d: data='%c'  csum %s  physID %s  parent-csum %s\n", addr, b->data[0],
           ok_ck ? "ok " : "BAD", ok_id ? "ok " : "BAD", ok_parent ? "ok " : "BAD");
}

int main(void) {
    crc_init();
    printf("== 1. book XOR example ==\n");
    uint8_t ex[16] = { 0x36, 0x5e, 0xc4, 0xcd, 0xba, 0x14, 0x8a, 0x92,
                       0xec, 0xef, 0x2c, 0x3a, 0x40, 0xbe, 0xf6, 0x66 };
    printf("XOR32 = 0x%08x  ADD32 = 0x%08x  Fletcher16 = 0x%04x  CRC32 = 0x%08x\n", ck_xor(ex, 16),
           ck_add(ex, 16), ck_fletcher16(ex, 16), ck_crc32(ex, 16));
    uint8_t h1[4] = { 1, 2, 3, 4 }, h2[4] = { 2, 1, 3, 4 };
    printf("checksum.py -D 1,2,3,4 style (bytes): add=%u xor=%u fletcher(a,b)=(%u,%u)\n",
           (1 + 2 + 3 + 4) & 0xff, 1 ^ 2 ^ 3 ^ 4, ck_fletcher16(h1, 4) & 0xff, ck_fletcher16(h1, 4) >> 8);
    printf("reordered 2,1,3,4                : fletcher(a,b)=(%u,%u)  <- only Fletcher notices order\n",
           ck_fletcher16(h2, 4) & 0xff, ck_fletcher16(h2, 4) >> 8);

    printf("\n== 2. undetected corruptions out of 20000 trials per fault (4KB block) ==\n");
    printf("%-18s", "fault");
    for (int f = 0; f < 4; f++) printf("%12s", names[f]);
    printf("\n");
    static uint8_t orig[BLK], bad[BLK];
    const int TRIALS = 20000;
    for (int kind = 0; kind < 6; kind++) {
        int miss[4] = { 0 };
        for (int t = 0; t < TRIALS; t++) {
            for (int i = 0; i < BLK; i++) orig[i] = (uint8_t)rnd();
            if (kind == 3) { /* 원본에 0x00 바이트 하나, 손상본은 그 자리가 0xFF */
                uint32_t i = rnd() % BLK;
                orig[i] = 0x00;
                memcpy(bad, orig, BLK);
                bad[i] = 0xFF;
            } else {
                memcpy(bad, orig, BLK);
                fault(bad, kind);
            }
            if (memcmp(orig, bad, BLK) == 0) continue;
            for (int f = 0; f < 4; f++)
                if (fns[f](orig, BLK) == fns[f](bad, BLK)) miss[f]++;
        }
        printf("%-18s", fault_names[kind]);
        for (int f = 0; f < 4; f++) printf("%12d", miss[f]);
        printf("\n");
    }

    printf("\n== 3. speed over 64 MB (-O0, relative only) ==\n");
    size_t N = 64u << 20;
    uint8_t *big = malloc(N);
    if (!big) return 1;
    for (size_t i = 0; i < N; i++) big[i] = (uint8_t)(i * 2654435761u >> 13);
    for (int f = 0; f < 4; f++) {
        clock_t c0 = clock();
        volatile uint32_t r = 0;
        for (size_t off = 0; off < N; off += BLK) r ^= fns[f](big + off, BLK);
        double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
        printf("%-11s %8.1f MB/s\n", names[f], 64.0 / s);
    }
    free(big);

    printf("\n== 4. misdirected write & lost write ==\n");
    for (int i = 0; i < 8; i++) write_blk(i, i, (char)('A' + i), 0);
    uint32_t parent[8]; /* ZFS 처럼 "부모"(inode/간접블록)가 자식 체크섬을 들고 있음 */
    for (int i = 0; i < 8; i++) parent[i] = disk0[i].csum;
    printf("misdirected: write 'X' meant for blk 3 lands on blk 5\n");
    write_blk(3, 5, 'X', 0);
    { dblk_t t; memset(t.data, 'X', 64); parent[3] = ck_crc32(t.data, 64); }
    read_blk(5, parent[5]);
    read_blk(3, parent[3]);
    printf("lost write: write 'Y' to blk 6 is acked but never persisted\n");
    write_blk(6, 6, 'Y', 1);
    { dblk_t t; memset(t.data, 'Y', 64); parent[6] = ck_crc32(t.data, 64); }
    read_blk(6, parent[6]);
    return 0;
}
