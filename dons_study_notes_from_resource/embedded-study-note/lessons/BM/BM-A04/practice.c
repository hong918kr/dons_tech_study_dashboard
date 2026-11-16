/* BM-A04 추가 연습 — 한 번에 검증
 *
 *  연습 1  버전 패킹/비교: "1.10.0" > "1.9.3" (문자열 비교 함정), 범위 검사
 *  연습 2  메모리 맵이 안 되는 외부 SPI NOR 슬롯: 읽기 콜백 + 작은 청크 버퍼로 CRC·SHA 를 한 번에 (단일 패스)
 *  연습 3  CRC-32 위조: 4 바이트로 CRC 를 아무 값(0xDEADBEEF)으로 — GF(2) 연립방정식
 *  연습 4  CRC-32 vs 바이트 합 체크섬: 64 B payload 의 모든 1/2/3 비트 오류 검출 (신드롬 전수)
 *  연습 5  온도계(thermometer) 부호 롤백 카운터: 플래시 AND-only 쓰기, 찢어진 쓰기에도 감소 불가
 *
 *  빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static uint32_t ld_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void st_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---------- CRC-32 / SHA-256 (BM-A04 solution.c 축약판) ---------- */
static uint32_t crc_tab[256];
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            crc_tab[i] = c;
        }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = crc_tab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
static uint32_t crc32(const uint8_t *p, size_t n) { return crc32_update(0, p, n); }

typedef struct { uint32_t h[8]; uint64_t nbytes; uint8_t buf[64]; uint32_t nbuf; } sha256_ctx;
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_blk(uint32_t h[8], const uint8_t *b) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++)
        w[t] = (uint32_t)b[4 * t] << 24 | (uint32_t)b[4 * t + 1] << 16 | (uint32_t)b[4 * t + 2] << 8 | b[4 * t + 3];
    for (int t = 16; t < 64; t++)
        w[t] = w[t - 16] + (ROR(w[t - 15], 7) ^ ROR(w[t - 15], 18) ^ (w[t - 15] >> 3)) + w[t - 7] +
               (ROR(w[t - 2], 17) ^ ROR(w[t - 2], 19) ^ (w[t - 2] >> 10));
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
static void sha_init(sha256_ctx *c) {
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->h, H0, sizeof H0); c->nbytes = 0; c->nbuf = 0;
}
static void sha_update(sha256_ctx *c, const uint8_t *p, size_t n) {
    c->nbytes += n;
    while (n) {
        size_t k = 64u - c->nbuf;
        if (k > n) k = n;
        memcpy(c->buf + c->nbuf, p, k);
        c->nbuf += (uint32_t)k; p += k; n -= k;
        if (c->nbuf == 64u) { sha_blk(c->h, c->buf); c->nbuf = 0; }
    }
}
static void sha_final(sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->nbytes * 8u;
    c->buf[c->nbuf++] = 0x80;
    if (c->nbuf > 56u) { memset(c->buf + c->nbuf, 0, 64u - c->nbuf); sha_blk(c->h, c->buf); c->nbuf = 0; }
    memset(c->buf + c->nbuf, 0, 56u - c->nbuf);
    for (int i = 0; i < 8; i++) c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_blk(c->h, c->buf);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(c->h[i] >> 24); out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(c->h[i] >> 8); out[4 * i + 3] = (uint8_t)c->h[i];
    }
}
static void sha256(const uint8_t *p, size_t n, uint8_t out[32]) { sha256_ctx c; sha_init(&c); sha_update(&c, p, n); sha_final(&c, out); }

/* ---------- 헤더 (BM-A04 헤더 축약판: 검사 순서 동일) ---------- */
#define IMG_MAGIC 0x474D4942u
#define EXEC_ADDR 0x08020000u
#define EXEC_MAX  0x00060000u
enum { OK = 0, EMPTY = 2, BAD_MAGIC, BAD_HDR_VERSION, BAD_HDR_CRC, BAD_FLAGS, BAD_SIZE, TOO_BIG, BAD_LOAD_ADDR,
       ROLLBACK, BAD_CRC, BAD_SHA };
static uint32_t build(uint8_t *out, const uint8_t *pl, uint32_t n, uint32_t ver) {
    memset(out, 0, 64);
    st_le32(out, IMG_MAGIC); out[4] = 1; out[6] = 64;
    st_le32(out + 8, ver); st_le32(out + 12, n); st_le32(out + 16, EXEC_ADDR); st_le32(out + 20, 1);
    st_le32(out + 24, crc32(pl, n)); sha256(pl, n, out + 28); st_le32(out + 60, crc32(out, 60));
    memcpy(out + 64, pl, n);
    return 64 + n;
}
/* 헤더 64 B 를 이미 읽었다고 보고 헤더 단계까지만 판정 (0 = 헤더 OK) */
static int hdr_check(const uint8_t *h, uint32_t slot_len, uint32_t min_v) {
    bool ff = true;
    for (int i = 0; i < 64; i++) ff &= h[i] == 0xFF;
    if (ff) return EMPTY;
    if (ld_le32(h) != IMG_MAGIC) return BAD_MAGIC;
    if ((h[4] | h[5] << 8) != 1 || (h[6] | h[7] << 8) != 64) return BAD_HDR_VERSION;
    if (crc32(h, 60) != ld_le32(h + 60)) return BAD_HDR_CRC;
    if (ld_le32(h + 20) != 1) return BAD_FLAGS;
    uint32_t n = ld_le32(h + 12);
    if (n < 8 || n > slot_len - 64u) return BAD_SIZE;
    if (n > EXEC_MAX) return TOO_BIG;
    if (ld_le32(h + 16) != EXEC_ADDR) return BAD_LOAD_ADDR;
    if (ld_le32(h + 8) < min_v) return ROLLBACK;
    return OK;
}
static int verify_mem(const uint8_t *s, uint32_t slot_len, uint32_t min_v) {
    if (slot_len < 64) return BAD_SIZE;
    int rc = hdr_check(s, slot_len, min_v);
    if (rc) return rc;
    uint32_t n = ld_le32(s + 12);
    if (crc32(s + 64, n) != ld_le32(s + 24)) return BAD_CRC;
    uint8_t d[32];
    sha256(s + 64, n, d);
    return memcmp(d, s + 28, 32) ? BAD_SHA : OK;
}

/* ======================= 연습 1 ======================= */
static int ver_pack(unsigned ma, unsigned mi, unsigned pa, uint32_t *out) {
    if (ma > 255 || mi > 255 || pa > 0xFFFF) return -1;
    *out = (uint32_t)ma << 24 | (uint32_t)mi << 16 | pa;
    return 0;
}
static int ver_parse(const char *s, uint32_t *out) {          /* "M.m.p" 십진수만, 정확히 점 2 개 */
    unsigned f[3] = {0, 0, 0};
    int k = 0, digits = 0;
    for (; *s; s++) {
        if (*s == '.') { if (!digits || ++k > 2) return -1; digits = 0; continue; }
        if (*s < '0' || *s > '9' || f[k] > 6553) return -1;  /* 넘침 전에 자른다 (65535 보다 커질 수 있으면 거부) */
        f[k] = f[k] * 10u + (unsigned)(*s - '0'); digits++;
    }
    if (k != 2 || !digits) return -1;
    return ver_pack(f[0], f[1], f[2], out);
}
static void ex1(void) {
    printf("-- 연습 1: 버전\n");
    uint32_t a, b, c;
    CHECK(ver_parse("1.10.0", &a) == 0 && ver_parse("1.9.3", &b) == 0 && a > b);
    CHECK(strcmp("1.10.0", "1.9.3") < 0);                       /* 문자열 비교는 반대로 답한다 */
    CHECK(ver_parse("255.255.65535", &c) == 0 && c == 0xFFFFFFFFu);
    CHECK(ver_parse("256.0.0", &c) == -1 && ver_parse("1.0.65536", &c) == -1 && ver_parse("1.2", &c) == -1);
    CHECK(ver_parse("1..2", &c) == -1 && ver_parse("1.2.3.4", &c) == -1 && ver_parse("1.2.x", &c) == -1);
    CHECK(ver_parse("1.2.99999999999", &c) == -1);
    int bad = 0;                                                /* 정수 순서 == 사전식 (major, minor, patch) 순서 */
    for (int i = 0; i < 20000; i++) {
        unsigned x[3] = {xs32() % 4, xs32() % 12, xs32() % 300}, y[3] = {xs32() % 4, xs32() % 12, xs32() % 300};
        uint32_t px, py;
        ver_pack(x[0], x[1], x[2], &px); ver_pack(y[0], y[1], y[2], &py);
        int lex = x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] != y[1] ? (x[1] < y[1] ? -1 : 1)
                : x[2] != y[2] ? (x[2] < y[2] ? -1 : 1) : 0;
        int num = px < py ? -1 : px > py ? 1 : 0;
        bad += lex != num;
    }
    CHECK(bad == 0);
}

/* ======================= 연습 2 ======================= */
typedef int (*read_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t len);
typedef struct { const uint8_t *mem; uint32_t len; uint32_t calls; } nor_t;   /* 외부 NOR 흉내 */
static int nor_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t len) {
    nor_t *n = ctx;
    n->calls++;
    if (off > n->len || len > n->len - off) return -1;
    memcpy(dst, n->mem + off, len);
    return 0;
}
/* 헤더 한 번 + payload 를 chunk 단위로 한 번만 읽으며 CRC 와 SHA 를 같이 갱신 */
static int verify_stream(read_fn rd, void *ctx, uint32_t slot_len, uint32_t min_v, uint8_t *chunk, uint32_t cap) {
    uint8_t h[64];
    if (slot_len < 64 || cap == 0) return BAD_SIZE;
    if (rd(ctx, 0, h, 64)) return BAD_SIZE;
    int rc = hdr_check(h, slot_len, min_v);
    if (rc) return rc;
    uint32_t n = ld_le32(h + 12), crc = 0;
    sha256_ctx sc;
    sha_init(&sc);
    for (uint32_t off = 0; off < n; ) {
        uint32_t k = n - off < cap ? n - off : cap;
        if (rd(ctx, 64 + off, chunk, k)) return BAD_SIZE;
        crc = crc32_update(crc, chunk, k);
        sha_update(&sc, chunk, k);
        off += k;
    }
    if (crc != ld_le32(h + 24)) return BAD_CRC;
    uint8_t d[32];
    sha_final(&sc, d);
    return memcmp(d, h + 28, 32) ? BAD_SHA : OK;
}
static void ex2(void) {
    printf("-- 연습 2: 외부 NOR 스트리밍 검증 (청크 버퍼 + 단일 패스)\n");
    static uint8_t slot[4096], pl[4000];
    uint8_t chunk[256];
    int bad = 0, calls_bad = 0;
    for (int i = 0; i < 3000; i++) {
        uint32_t n = 8 + xs32() % 3000, cap = 1 + xs32() % 256;
        for (uint32_t k = 0; k < n; k++) pl[k] = (uint8_t)xs32();
        memset(slot, 0xFF, sizeof slot);
        build(slot, pl, n, 0x01020000u + xs32() % 4);
        uint32_t min_v = 0x01020000u + xs32() % 3;
        switch (xs32() % 4) {                                     /* 결함 섞기 */
        case 0: slot[64 + xs32() % n] ^= (uint8_t)(1 + xs32() % 255); break;
        case 1: slot[8 + xs32() % 52] ^= 1; break;
        default: break;
        }
        uint32_t slot_len = xs32() % 8 ? sizeof slot : xs32() % sizeof slot;
        nor_t nor = {slot, slot_len, 0};
        int s = verify_stream(nor_read, &nor, slot_len, min_v, chunk, cap);
        if (s != verify_mem(slot, slot_len, min_v)) bad++;
        if (s == OK || s == BAD_CRC || s == BAD_SHA)              /* 헤더 1 번 + ⌈n/cap⌉ 번, 두 번 읽지 않는다 */
            calls_bad += nor.calls != 1 + (n + cap - 1) / cap;
    }
    CHECK(bad == 0);
    CHECK(calls_bad == 0);
}

/* ======================= 연습 3 ======================= */
static bool crc_forge4(uint8_t *buf, size_t n, size_t pos, uint32_t target) {
    uint32_t basis[32] = {0}, comb[32] = {0};
    memset(buf + pos, 0, 4);
    uint32_t base = crc32(buf, n);
    for (int j = 0; j < 32; j++) {
        buf[pos + (size_t)j / 8] = (uint8_t)(1u << (j % 8));
        uint32_t v = crc32(buf, n) ^ base, m = 1u << j;
        buf[pos + (size_t)j / 8] = 0;
        for (int b = 31; b >= 0 && v; b--) {
            if (!((v >> b) & 1u)) continue;
            if (!basis[b]) { basis[b] = v; comb[b] = m; break; }
            v ^= basis[b]; m ^= comb[b];
        }
    }
    uint32_t t = target ^ base, x = 0;
    for (int b = 31; b >= 0; b--) {
        if (!((t >> b) & 1u)) continue;
        if (!basis[b]) return false;
        t ^= basis[b]; x ^= comb[b];
    }
    st_le32(buf + pos, x);
    return crc32(buf, n) == target;
}
static void ex3(void) {
    printf("-- 연습 3: CRC 위조\n");
    uint8_t m[100];
    for (int i = 0; i < 100; i++) m[i] = (uint8_t)xs32();
    CHECK(crc_forge4(m, sizeof m, 40, 0xDEADBEEFu));
    printf("   forged bytes @40 = %02X %02X %02X %02X → crc32 = 0x%08X\n", m[40], m[41], m[42], m[43],
           (unsigned)crc32(m, sizeof m));
    int ok = 0;                                                    /* 아무 위치·목표든 4 연속 바이트면 항상 풀린다 */
    for (int i = 0; i < 2000; i++) {
        size_t pos = xs32() % 97;
        uint32_t tgt = xs32();
        ok += crc_forge4(m, sizeof m, pos, tgt);
    }
    CHECK(ok == 2000);
}

/* ======================= 연습 4 ======================= */
static void ex4(void) {
    printf("-- 연습 4: 1/2/3 비트 오류 검출 (64 B payload = 512 비트)\n");
    uint8_t p[64];
    for (int i = 0; i < 64; i++) p[i] = (uint8_t)xs32();
    uint32_t S[512], base = crc32(p, 64);
    int32_t D[512];                                               /* 바이트 합 체크섬의 변화량 */
    for (int i = 0; i < 512; i++) {
        p[i / 8] ^= (uint8_t)(1u << (i % 8));
        S[i] = crc32(p, 64) ^ base;                               /* 신드롬: CRC 는 아핀 → 오류 패턴에 선형 */
        D[i] = (p[i / 8] >> (i % 8) & 1) ? (1 << (i % 8)) : -(1 << (i % 8));
        p[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    uint64_t miss1 = 0, miss2 = 0, miss3 = 0, n2 = 0, n3 = 0, sum_miss2 = 0;
    for (int i = 0; i < 512; i++) {
        miss1 += S[i] == 0;
        for (int j = i + 1; j < 512; j++) {
            uint32_t sij = S[i] ^ S[j];
            n2++; miss2 += sij == 0; sum_miss2 += D[i] + D[j] == 0;
            for (int k = j + 1; k < 512; k++) { n3++; miss3 += sij == S[k]; }
        }
    }
    printf("   CRC-32: 1-bit 512 개 미검출 %llu, 2-bit %llu 개 미검출 %llu, 3-bit %llu 개 미검출 %llu\n",
           (unsigned long long)miss1, (unsigned long long)n2, (unsigned long long)miss2,
           (unsigned long long)n3, (unsigned long long)miss3);
    printf("   바이트 합(32 비트 덧셈) 체크섬: 2-bit %llu 개 중 미검출 %llu\n",
           (unsigned long long)n2, (unsigned long long)sum_miss2);
    CHECK(miss1 == 0 && miss2 == 0 && miss3 == 0);
    CHECK(n2 == 130816 && n3 == 22238720);
    CHECK(sum_miss2 > 0);                                         /* 같은 비트 위치의 0→1, 1→0 쌍은 합이 상쇄 */
}

/* ======================= 연습 5 ======================= */
/* 롤백 카운터 = 플래시 워드 W 개의 '지워진 1 중 0 이 된 비트 수'. 지우기 없이 1→0 만 → 값은 줄 수 없다 */
#define ARB_WORDS 4
typedef struct { uint32_t w[ARB_WORDS]; uint32_t rng; uint32_t cut; } arb_t;   /* cut: 찢어질 프로그램 번호 */
static unsigned popc(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }
static unsigned arb_read(const arb_t *a) {
    unsigned v = 0;
    for (int i = 0; i < ARB_WORDS; i++) v += 32u - popc(a->w[i]);
    return v;
}
/* 플래시 프로그램 1 회: AND. 찢어지면 0 이 될 비트 중 무작위 일부만 0 이 된다 */
static bool flash_prog(arb_t *a, int i, uint32_t val) {
    if (a->cut && --a->cut == 0) {
        uint32_t drop = a->w[i] & ~val, r = a->rng;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5; a->rng = r;
        a->w[i] &= ~(drop & r);
        return false;
    }
    a->w[i] &= val;
    return true;
}
/* v 까지 올린다 (내리라고 하면 거부). 반환 0 = OK, −1 = 감소 요청/용량 초과, −2 = 전원 차단 */
static int arb_bump(arb_t *a, unsigned v) {
    unsigned cur = arb_read(a);
    if (v < cur || v > 32u * ARB_WORDS) return -1;
    for (int i = 0; i < ARB_WORDS && cur < v; i++) {
        unsigned zeros = 32u - popc(a->w[i]);                 /* 워드 안에서 아래 비트부터 0 으로 */
        unsigned want = zeros + (v - cur < 32u - zeros ? v - cur : 32u - zeros);
        uint32_t val = want >= 32u ? 0u : ~((1u << want) - 1u);
        if (val != a->w[i] && !flash_prog(a, i, val)) return -2;
        cur += want - zeros;
    }
    return 0;
}
static void ex5(void) {
    printf("-- 연습 5: 온도계 롤백 카운터 (AND-only, 찢어진 쓰기)\n");
    arb_t a;
    memset(&a, 0xFF, sizeof a.w); a.cut = 0; a.rng = 1;
    CHECK(arb_read(&a) == 0);
    CHECK(arb_bump(&a, 5) == 0 && arb_read(&a) == 5);
    CHECK(arb_bump(&a, 3) == -1 && arb_read(&a) == 5);             /* 감소 요청 거부 */
    CHECK(arb_bump(&a, 40) == 0 && arb_read(&a) == 40 && a.w[0] == 0 && a.w[1] == 0xFFFFFF00u);
    CHECK(arb_bump(&a, 128) == 0 && arb_read(&a) == 128 && arb_bump(&a, 129) == -1);
    int bad = 0, cuts = 0;                                          /* 모든 (old, new, 찢어질 쓰기 번호, 시드) */
    for (unsigned o = 0; o <= 96; o += 3)
        for (unsigned n = o; n <= 128; n += 5)
            for (uint32_t c = 1; c <= 4; c++)
                for (uint32_t seed = 1; seed <= 8; seed++) {
                    memset(&a, 0xFF, sizeof a.w); a.cut = 0; a.rng = seed;
                    arb_bump(&a, o);
                    a.cut = c;
                    int r = arb_bump(&a, n);
                    unsigned v = arb_read(&a);
                    if (r == -2) cuts++;
                    if (v < o || v > n) bad++;                       /* 찢어져도 [old, new] 밖으로 못 나간다 */
                    a.cut = 0;
                    if (arb_bump(&a, n) != 0 || arb_read(&a) != n) bad++;   /* 재부팅 후 다시 하면 완료 */
                }
    CHECK(bad == 0);
    CHECK(cuts > 1000);
    printf("   torn-write scenarios with a cut: %d, invariant violations: %d\n", cuts, bad);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
