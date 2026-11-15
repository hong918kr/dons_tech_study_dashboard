/* ARCH-M15 추가 연습 — 한 번에 검증
 *   연습 1  ARMv8-M PMSAv8 검사기: base/limit(32 B 단위), AP 2비트, 겹치면 폴트 — 같은 스택 가드를 v8 로
 *   연습 2  서브리전 없이 "정확히" 덮기 = 2의 거듭제곱 정렬 블록 그리디 분할 vs SRD 를 쓴 3영역 배치
 *   연습 3  MMFSR(CFSR[7:0]) + MMFAR 디코더 — 폴트 핸들러가 찍을 한 줄
 *   연습 4  유효 권한 맵 문자열: 주소 창을 훑으며 특권/비특권 R/W 를 한 글자씩
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

typedef enum { ACC_R, ACC_W, ACC_X } acc_t;

/* ---------------- 연습 1: ARMv8-M PMSAv8 ---------------- */
/* RBAR: BASE[31:5] SH[4:3] AP[2:1] XN[0]   RLAR: LIMIT[31:5] AttrIndx[3:1] EN[0]
 * AP[2:1]: 00 특권 RW · 01 RW 누구나 · 10 특권 RO · 11 RO 누구나 */
typedef struct { bool en; uint32_t base, limit; uint8_t ap; bool xn; } v8_region_t;
typedef enum { V8_OK_REGION, V8_OK_DEFAULT, V8_F_NOMATCH, V8_F_OVERLAP, V8_F_AP, V8_F_XN } v8_why_t;

static v8_region_t v8_decode(uint32_t rbar, uint32_t rlar) {
    v8_region_t r;
    r.en = (rlar & 1u) != 0;
    r.base = rbar & ~31u;
    r.limit = (rlar & ~31u) | 31u;             /* LIMIT 는 마지막 32 B 블록 — 아래 5비트는 1 로 본다 */
    r.ap = (uint8_t)((rbar >> 1) & 3u);
    r.xn = (rbar & 1u) != 0;
    return r;
}

static v8_why_t v8_check(const v8_region_t *rg, int n, bool privdefena, uint32_t a, acc_t acc, bool priv, int *hit) {
    int cnt = 0;
    *hit = -1;
    for (int i = 0; i < n; i++)
        if (rg[i].en && a >= rg[i].base && a <= rg[i].limit) { cnt++; *hit = i; }
    if (cnt > 1) return V8_F_OVERLAP;                     /* v7 과 다르다: 우선순위 없이 폴트 */
    if (cnt == 0) return (priv && privdefena) ? V8_OK_DEFAULT : V8_F_NOMATCH;
    const v8_region_t *r = &rg[*hit];
    if (acc == ACC_X && r->xn) return V8_F_XN;
    bool ro = (r->ap & 2u) != 0, any = (r->ap & 1u) != 0;
    if (!priv && !any) return V8_F_AP;
    if (acc == ACC_W && ro) return V8_F_AP;
    return V8_OK_REGION;
}

/* ---------------- 연습 2: 정확히 덮기 ---------------- */
/* [start, start+len) 를 서브리전 없이, 크기 정렬된 2^k(≥ 32 B) 블록들로 정확히 나눈다. 블록 수 반환,
 * start·len 이 32 의 배수가 아니면 -1, max 를 넘으면 -2. */
static int split_pow2(uint32_t start, uint32_t len, uint32_t *base, uint64_t *size, int max) {
    if ((start | len) & 31u) return -1;
    uint64_t a = start, end = (uint64_t)start + len;
    int n = 0;
    while (a < end) {
        uint64_t sz = 32;
        while ((a & (sz * 2 - 1)) == 0 && a + sz * 2 <= end && sz * 2 <= ((uint64_t)1 << 32)) sz *= 2;
        if (n == max) return -2;
        base[n] = (uint32_t)a; size[n] = sz; n++;
        a += sz;
    }
    return n;
}

typedef struct { bool en; uint32_t base; uint8_t size_f, srd, ap; } v7_region_t;
static bool v7_hit(const v7_region_t *r, uint32_t a) {
    if (!r->en) return false;
    uint64_t sz = (uint64_t)1 << (r->size_f + 1);
    uint32_t off = a - r->base;
    if ((uint64_t)off >= sz) return false;
    return sz < 256 || !((r->srd >> (off >> (r->size_f - 2))) & 1u);
}

/* ---------------- 연습 3: MMFSR 디코더 ---------------- */
/* MMFSR = CFSR[7:0]: bit0 IACCVIOL, bit1 DACCVIOL, bit3 MUNSTKERR, bit4 MSTKERR, bit5 MLSPERR, bit7 MMARVALID */
static void mmfsr_decode(uint8_t v, uint32_t mmfar, char *buf, size_t n) {
    static const char *const name[8] = { "IACCVIOL", "DACCVIOL", NULL, "MUNSTKERR", "MSTKERR", "MLSPERR", NULL, NULL };
    size_t len = 0;
    buf[0] = '\0';
    for (int b = 0; b < 7; b++)
        if (((v >> b) & 1u) && name[b])
            len += (size_t)snprintf(buf + len, n - len, "%s%s", len ? " " : "", name[b]);
    if (v & 0x80u) len += (size_t)snprintf(buf + len, n - len, "%sMMFAR=0x%08X", len ? " " : "", (unsigned)mmfar);
    if (len == 0) snprintf(buf, n, "none");
}

/* ---------------- 연습 4: 유효 권한 맵 ---------------- */
static bool ap_ok(uint8_t ap, bool priv, bool w) {
    static const char *const t[8] = { "----", "RW--", "RWR-", "RWRW", "----", "R---", "R-R-", "R-R-" };
    return t[ap & 7u][(priv ? 0 : 2) + (w ? 1 : 0)] != '-';
}
/* 창 [start, start + step*n) 를 step 마다 한 번 (그 칸 첫 주소에서) 판정: 'W' 읽기+쓰기, 'R' 읽기만, '-' 금지.
 * 영역 7→0 첫 히트, 히트 없으면 특권만 기본 맵(PRIVDEFENA=1 가정 → 'W'). */
static void perm_map(const v7_region_t *rg, uint32_t start, uint32_t step, int n, bool priv, char *out) {
    for (int k = 0; k < n; k++) {
        uint32_t a = start + (uint32_t)k * step;
        int win = -1;
        for (int i = 7; i >= 0 && win < 0; i--) if (v7_hit(&rg[i], a)) win = i;
        bool r, w;
        if (win < 0) { r = w = priv; }
        else { r = ap_ok(rg[win].ap, priv, false); w = ap_ok(rg[win].ap, priv, true); }
        out[k] = w ? 'W' : r ? 'R' : '-';
    }
    out[n] = '\0';
}

int main(void) {
    int hit;

    /* 연습 1 */
    v8_region_t v8[4];
    v8[0] = v8_decode(0x20000000u | (0u << 1) | 1u, 0x2001FFE0u | 1u);   /* SRAM 128 KB: 특권 RW, XN */
    v8[1] = v8_decode(0x20004000u | (1u << 1) | 1u, 0x200057E0u | 1u);   /* 태스크 스택 6 KB 정확히 (2^n 아님) */
    v8[2] = v8_decode(0x20004000u | (0u << 1) | 1u, 0x20004000u | 1u);   /* 가드 32 B: 특권 RW 가 최소 (no-access 없음) */
    CHECK(v8[1].limit == 0x200057FFu && v8[1].ap == 1 && v8[1].xn && v8[2].limit == 0x2000401Fu);
    CHECK(v8_check(v8, 3, true, 0x20004100u, ACC_W, false, &hit) == V8_F_OVERLAP);   /* SRAM 과 스택이 겹침 */
    v8[0] = v8_decode(0x20005800u | 1u, 0x2001FFE0u | 1u);              /* SRAM 을 스택 위로 잘라 겹침 제거 */
    v8[3] = v8_decode(0x20000000u | 1u, 0x20003FE0u | 1u);              /* 스택 아래 SRAM */
    CHECK(v8_check(v8, 4, true, 0x20004010u, ACC_W, false, &hit) == V8_F_OVERLAP);   /* 가드 ∩ 스택 */
    v8[1] = v8_decode(0x20004020u | (1u << 1) | 1u, 0x200057E0u | 1u);   /* 스택을 가드 위에서 시작 */
    CHECK(v8_check(v8, 4, true, 0x20004010u, ACC_W, false, &hit) == V8_F_AP && hit == 2);
    CHECK(v8_check(v8, 4, true, 0x20004020u, ACC_W, false, &hit) == V8_OK_REGION && hit == 1);
    CHECK(v8_check(v8, 4, true, 0x20004010u, ACC_W, true, &hit) == V8_OK_REGION);    /* 특권은 가드를 통과 */
    v8[2].en = false;                                                     /* v8 식 가드 = 영역 없는 틈 + PRIVDEFENA=0 */
    CHECK(v8_check(v8, 4, false, 0x20004010u, ACC_W, true, &hit) == V8_F_NOMATCH &&
          v8_check(v8, 4, true, 0x20004010u, ACC_W, true, &hit) == V8_OK_DEFAULT);
    v8[2].en = true;
    CHECK(v8_check(v8, 4, true, 0x20004100u, ACC_X, false, &hit) == V8_F_XN);
    CHECK(v8_check(v8, 4, false, 0x60000000u, ACC_R, true, &hit) == V8_F_NOMATCH &&
          v8_check(v8, 4, true, 0x60000000u, ACC_R, true, &hit) == V8_OK_DEFAULT);

    /* 연습 2 */
    uint32_t bb[64]; uint64_t bs[64];
    int n = split_pow2(0x20000020u, 0xFE0u, bb, bs, 64);
    CHECK(n == 7 && bb[0] == 0x20000020u && bs[0] == 32 && bb[6] == 0x20000800u && bs[6] == 2048);
    CHECK(split_pow2(0x20000020u, 0xFE0u, bb, bs, 4) == -2 && split_pow2(0x20000010u, 0x20u, bb, bs, 8) == -1);
    CHECK(split_pow2(0x20000000u, 0x6000u, bb, bs, 64) == 2);           /* 24 KB = 16 KB + 8 KB */
    CHECK(split_pow2(0x00000000u, 0xFFFFFFE0u, bb, bs, 64) == 27);       /* 2 GB, 1 GB, …, 32 B */
    {
        v7_region_t three[3] = {
            { true, 0x20000000u, 11, 0x01, 3 },     /* 4 KB, 서브리전 512 B, 0 번 끔 → [0x200, 0x1000) */
            { true, 0x20000000u,  8, 0x01, 3 },     /* 512 B, 서브리전 64 B, 0 번 끔 → [0x040, 0x200) */
            { true, 0x20000020u,  4, 0x00, 3 },     /* 32 B → [0x020, 0x040) */
        };
        int bad = 0, multi = 0;
        for (uint32_t a = 0x20000000u; a < 0x20001100u; a++) {
            int c = 0;
            for (int i = 0; i < 3; i++) c += v7_hit(&three[i], a);
            bool in = a >= 0x20000020u && a < 0x20001000u;
            if ((c > 0) != in) bad++;
            if (c > 1) multi++;
        }
        CHECK(bad == 0 && multi == 0);                                    /* 3 영역이 정확히, 겹침 없이 */
    }

    /* 연습 3 */
    char s[96];
    mmfsr_decode(0x82, 0x20004010u, s, sizeof s);
    CHECK(strcmp(s, "DACCVIOL MMFAR=0x20004010") == 0);
    mmfsr_decode(0x01, 0xDEADBEEFu, s, sizeof s);
    CHECK(strcmp(s, "IACCVIOL") == 0);                                    /* MMARVALID 없음 → 주소 안 찍음 */
    mmfsr_decode(0x10, 0, s, sizeof s);
    CHECK(strcmp(s, "MSTKERR") == 0);
    mmfsr_decode(0x00, 0, s, sizeof s);
    CHECK(strcmp(s, "none") == 0);
    mmfsr_decode(0x44, 0, s, sizeof s);                                   /* 예약 비트만 → 무시 */
    CHECK(strcmp(s, "none") == 0);

    /* 연습 4 — 본문 Part C 의 RTOS 구성 중 SRAM 쪽 */
    v7_region_t rg[8];
    memset(rg, 0, sizeof rg);
    rg[1] = (v7_region_t){ true, 0x20000000u, 16, 0x00, 1 };              /* SRAM 128 KB 특권 RW */
    rg[3] = (v7_region_t){ true, 0x20004000u, 12, 0xC0, 3 };              /* 태스크 6 KB (8 KB, 6·7 끔) */
    rg[4] = (v7_region_t){ true, 0x20008000u,  9, 0x00, 2 };              /* 공유 표 */
    rg[7] = (v7_region_t){ true, 0x20004000u,  4, 0x00, 0 };              /* 가드 */
    char mp[64];
    perm_map(rg, 0x20003000u, 0x400u, 24, false, mp);
    CHECK(strcmp(mp, "-----WWWWW----------R---") == 0);
    perm_map(rg, 0x20003000u, 0x400u, 24, true, mp);
    CHECK(strcmp(mp, "WWWW-WWWWWWWWWWWWWWWWWWW") == 0);
    perm_map(rg, 0x20004000u, 0x20u, 4, false, mp);                       /* 32 B 칸으로 가드 확대 */
    CHECK(strcmp(mp, "-WWW") == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
