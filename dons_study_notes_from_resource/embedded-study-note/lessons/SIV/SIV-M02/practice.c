/* SIV-M02 추가 연습 — 한 번에 검증
 *  연습 1: CRC 이진 탐색으로 첫 불일치 바이트 찾기 (로그 대역폭이 좁은 타깃에서)
 *  연습 2: 4 KiB 경계 분할 split_4k (올바른 엔진/드라이버가 해야 하는 일)
 *  연습 3: START 전에 디스크립터 체인 lint (정렬 · 범위 · 루프 · 길이)
 *  연습 4: 길이 필드 stuck-at 결함을 잡는 길이 집합 (왜 0 과 max 가 경계 케이스인가)
 * 본문(solution.c)과 독립적으로 빌드된다 — 필요한 메모리 맵·CRC 만 다시 넣었다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---------- 연습 1: CRC 이진 탐색 ---------- */
/* a, b 의 첫 불일치 인덱스 (같으면 len). prefix CRC 만 비교한다 — 타깃이 "CRC(addr, n)" 명령만 줄 때.
 * *calls = 비교한 prefix 쌍의 수. 불변식: [0, lo) 는 같음, [0, hi] 는 다름 */
static size_t crc_first_bad(const uint8_t *a, const uint8_t *b, size_t len, unsigned *calls) {
    *calls = 1;
    if (crc32_update(0, a, len) == crc32_update(0, b, len)) return len;
    size_t lo = 0, hi = len - 1;                  /* 답은 [lo, hi] 안 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;          /* prefix [0, mid] 비교 */
        ++*calls;
        if (crc32_update(0, a, mid + 1) != crc32_update(0, b, mid + 1)) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

/* ---------- 연습 2: 4 KiB 분할 ---------- */
typedef struct { uint32_t addr, len; } piece_t;
static int split_4k(uint32_t addr, uint32_t len, piece_t *out, int max) {
    int n = 0;
    while (len) {
        uint32_t room = 0x1000u - (addr & 0xFFFu);   /* 이 페이지에 남은 바이트 */
        uint32_t take = len < room ? len : room;
        if (n == max) return -1;
        out[n++] = (piece_t){addr, take};
        addr += take; len -= take;
    }
    return n;
}

/* ---------- 연습 3: 체인 lint ---------- */
#define DRAM_BASE 0x80000000u
#define DRAM_SIZE 0x40000u
#define DESC_AREA DRAM_BASE
#define DESC_AREA_SIZE 0x1000u
#define DESC_SIZE 32u
#define CTRL_LAST (1u << 16)
static uint8_t MEM[DRAM_SIZE];
static bool in_dram(uint32_t a, uint32_t len) { return a >= DRAM_BASE && (uint64_t)(a - DRAM_BASE) + len <= DRAM_SIZE; }
static uint32_t rd32(uint32_t a) {
    const uint8_t *p = &MEM[a - DRAM_BASE];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint32_t a, uint32_t v) {
    uint8_t *p = &MEM[a - DRAM_BASE];
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put_desc(uint32_t d, uint32_t next, uint32_t src, uint32_t dst, uint32_t len, bool last) {
    wr32(d + 0x00, next); wr32(d + 0x04, src); wr32(d + 0x08, dst);
    wr32(d + 0x0C, (len & 0xFFFFu) | (last ? CTRL_LAST : 0));
}
enum { LINT_OK, LINT_MISALIGNED, LINT_DESC_RANGE, LINT_BUF_RANGE, LINT_LOOP, LINT_TOO_LONG };
static const char *const LINT_NAME[] = {"OK", "MISALIGNED", "DESC_RANGE", "BUF_RANGE", "LOOP", "TOO_LONG"};
/* head 부터 걷는다. *bad = 문제 디스크립터 인덱스(체인 순서), *n = OK 일 때 디스크립터 수 */
static int chain_lint(uint32_t head, int max_desc, int *n, int *bad) {
    uint8_t seen[DESC_AREA_SIZE / DESC_SIZE] = {0};
    uint32_t d = head;
    for (int i = 0;; i++) {
        *bad = i;
        if (i == max_desc) return LINT_TOO_LONG;
        if (d & (DESC_SIZE - 1u)) return LINT_MISALIGNED;
        if (d < DESC_AREA || d >= DESC_AREA + DESC_AREA_SIZE) return LINT_DESC_RANGE;
        uint32_t k = (d - DESC_AREA) / DESC_SIZE;
        if (seen[k]) return LINT_LOOP;
        seen[k] = 1;
        uint32_t ctl = rd32(d + 0x0C), len = ctl & 0xFFFFu;
        if (!in_dram(rd32(d + 0x04), len) || !in_dram(rd32(d + 0x08), len)) return LINT_BUF_RANGE;
        if (ctl & CTRL_LAST) { *n = i + 1; return LINT_OK; }
        d = rd32(d + 0x00);
    }
}

/* ---------- 연습 4: 길이 필드 stuck-at 커버리지 ---------- */
/* width 비트 LEN 필드의 각 비트가 0 또는 1 로 stuck 된 2*width 개 결함 중, lens 집합이 잡는 결함 수.
 * 길이 L 이 "bit b stuck-at-v" 를 잡는다 ⇔ L 의 bit b 가 v 가 아니다 (디코드 결과가 L 과 달라짐) */
static int len_faults_covered(const uint32_t *lens, int n, int width) {
    int covered = 0;
    for (int b = 0; b < width; b++)
        for (uint32_t v = 0; v <= 1; v++) {
            bool hit = false;
            for (int i = 0; i < n && !hit; i++) hit = ((lens[i] >> b) & 1u) != v;
            covered += hit;
        }
    return covered;
}

int main(void) {
    /* 연습 1 */
    {
        static uint8_t a[4096], b[4096];
        uint32_t r = 7u;
        for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)xs32(&r);
        int ok = 0;
        unsigned maxcalls = 0, calls = 0;
        size_t pos[] = {0, 1, 2, 63, 64, 1000, 2047, 2048, 4094, 4095};
        for (size_t t = 0; t < sizeof pos / sizeof pos[0]; t++) {
            memcpy(b, a, sizeof a);
            b[pos[t]] ^= 0x04;
            b[(pos[t] + 300) % sizeof b] ^= (pos[t] + 300 < sizeof b) ? 0x80 : 0;   /* 뒤에 두 번째 오류 */
            ok += crc_first_bad(a, b, sizeof a, &calls) == pos[t];
            if (calls > maxcalls) maxcalls = calls;
        }
        CHECK(ok == 10);
        CHECK(maxcalls <= 13);                         /* 1 + ceil(log2 4096) */
        memcpy(b, a, sizeof a);
        CHECK(crc_first_bad(a, b, sizeof a, &calls) == sizeof a && calls == 1);
        printf("ex1: first-bad by CRC bisect, max %u prefix-CRC pairs for 4096 B\n", maxcalls);
    }
    /* 연습 2 */
    {
        piece_t p[32];
        CHECK(split_4k(0x80020FA0u, 133, p, 32) == 2 && p[0].len == 0x60 && p[1].addr == 0x80021000u && p[1].len == 0x25);
        CHECK(split_4k(0x80020000u, 4096, p, 32) == 1);
        CHECK(split_4k(0x80020001u, 4096, p, 32) == 2 && p[0].len == 4095 && p[1].len == 1);
        CHECK(split_4k(0x80020000u, 0, p, 32) == 0);
        CHECK(split_4k(0x80020040u, 0xFFFF, p, 32) == 17);
        CHECK(split_4k(0x80020040u, 0xFFFF, p, 4) == -1);
        uint32_t r = 99u;
        int ok = 0;
        for (int t = 0; t < 1000; t++) {
            uint32_t a = 0x80020000u + (xs32(&r) & 0xFFFFu), len = xs32(&r) & 0xFFFFu, sum = 0;
            int n = split_4k(a, len, p, 32);
            bool good = n >= 0;
            for (int i = 0; good && i < n; i++) {
                sum += p[i].len;
                good = p[i].len > 0 && (p[i].addr >> 12) == ((p[i].addr + p[i].len - 1u) >> 12) &&
                       (i == 0 || p[i].addr == p[i - 1].addr + p[i - 1].len);
            }
            ok += good && sum == len;
        }
        CHECK(ok == 1000);
    }
    /* 연습 3 */
    {
        int n = 0, bad = 0;
        uint32_t d0 = DESC_AREA + 0x000, d1 = DESC_AREA + 0x300, d2 = DESC_AREA + 0x600;
        uint32_t S = DRAM_BASE + 0x1000u, T = DRAM_BASE + 0x20000u;
        put_desc(d0, d1, S, T, 64, false);
        put_desc(d1, d2, S + 64, T + 64, 64, false);
        put_desc(d2, 0, S + 128, T + 128, 64, true);
        CHECK(chain_lint(d0, 16, &n, &bad) == LINT_OK && n == 3);
        put_desc(d2, d1, S + 128, T + 128, 64, false);                 /* LAST 빠짐 + next 가 d1 → 루프 */
        CHECK(chain_lint(d0, 16, &n, &bad) == LINT_LOOP && bad == 3);
        put_desc(d2, d0 + 4, S + 128, T + 128, 64, false);             /* next 비정렬 */
        CHECK(chain_lint(d0, 16, &n, &bad) == LINT_MISALIGNED && bad == 3);
        put_desc(d2, 0, S + 128, 0x40000000u, 64, true);               /* dst 디코드 안 됨 */
        CHECK(chain_lint(d0, 16, &n, &bad) == LINT_BUF_RANGE && bad == 2);
        put_desc(d2, 0, S + 128, DRAM_BASE + DRAM_SIZE - 64, 65, true); /* 끝을 1 바이트 넘음 */
        CHECK(chain_lint(d0, 16, &n, &bad) == LINT_BUF_RANGE && bad == 2);
        put_desc(d2, 0, S + 128, T + 128, 64, true);
        CHECK(chain_lint(d0, 2, &n, &bad) == LINT_TOO_LONG && bad == 2);
        CHECK(chain_lint(0x80002000u, 16, &n, &bad) == LINT_DESC_RANGE && bad == 0);
        printf("ex3: lint codes %s/%s/%s/%s/%s/%s\n", LINT_NAME[0], LINT_NAME[1], LINT_NAME[2], LINT_NAME[3],
               LINT_NAME[4], LINT_NAME[5]);
    }
    /* 연습 4 */
    {
        uint32_t s64[] = {64}, smax[] = {0xFFFF}, sboth[] = {0, 0xFFFF}, s01[] = {0, 1};
        CHECK(len_faults_covered(s64, 1, 16) == 16);
        CHECK(len_faults_covered(smax, 1, 16) == 16);
        CHECK(len_faults_covered(sboth, 2, 16) == 32);
        CHECK(len_faults_covered(s01, 2, 16) == 17);
        uint32_t rl[2000], r = 2024u;                                  /* RANDOM kind 와 같은 0..4096 */
        for (int i = 0; i < 2000; i++) rl[i] = xs32(&r) % 4097u;
        int cov = len_faults_covered(rl, 2000, 16);
        int has4096 = 0;
        for (int i = 0; i < 2000; i++) has4096 += rl[i] == 4096u;
        CHECK(has4096 == 0 && cov == 28);       /* bit13..15 stuck-at-0 3개 + bit12 stuck-at-0(4096 이 한 번도 안 뽑힘) */
        printf("ex4: 2000 random lengths in 0..4096 cover %d/32 LEN stuck-at faults; {0,0xFFFF} covers 32/32\n", cov);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
