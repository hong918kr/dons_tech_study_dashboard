/* ARCH-B11 추가 연습 — 한 번에 검증
 *   연습 1  정렬된 맵에서 이진 탐색 디코더 (선형 탐색과 동등성: 경계 + 무작위 20만)
 *   연습 2  ARMv7-M 기본 메모리 맵 분류 (Code/SRAM/Peripheral/External RAM/External device/PPB/Vendor)
 *           + 메모리 타입 + XN(실행 금지)
 *   연습 3  Cortex-M3/M4 bit-band: 주소 ↔ alias 변환, alias 쓰기 = 한 비트 RMW
 *   연습 4  비정렬 접근을 자연 정렬 버스 전송으로 쪼개기
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* ---------------- 연습 1: 이진 탐색 디코더 ---------------- */
typedef struct { uint32_t base, size; } rgn_t;      /* base 오름차순, 겹침 없음 (map_validate 통과) */

static int find_linear(const rgn_t *m, int n, uint32_t a) {
    for (int i = 0; i < n; i++) if (a - m[i].base < m[i].size) return i;
    return -1;
}
/* base <= a 인 마지막 영역을 찾고, 그 영역 안인지 확인 */
static int find_bsearch(const rgn_t *m, int n, uint32_t a) {
    int lo = 0, hi = n - 1, cand = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (m[mid].base <= a) { cand = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return (cand >= 0 && a - m[cand].base < m[cand].size) ? cand : -1;
}

/* ---------------- 연습 2: ARMv7-M 기본 메모리 맵 ---------------- */
typedef enum { MT_NORMAL, MT_DEVICE, MT_SO } mtype_t;   /* SO = Strongly-ordered */
typedef struct { const char *name; mtype_t type; bool xn; } cm_class_t;

static cm_class_t cm_class(uint32_t a) {
    if (a < 0x20000000u) return (cm_class_t){ "Code",            MT_NORMAL, false };
    if (a < 0x40000000u) return (cm_class_t){ "SRAM",            MT_NORMAL, false };
    if (a < 0x60000000u) return (cm_class_t){ "Peripheral",      MT_DEVICE, true  };
    if (a < 0xA0000000u) return (cm_class_t){ "External RAM",    MT_NORMAL, false };
    if (a < 0xE0000000u) return (cm_class_t){ "External device", MT_DEVICE, true  };
    if (a < 0xE0100000u) return (cm_class_t){ "PPB",             MT_SO,     true  };
    return (cm_class_t){ "Vendor system", MT_DEVICE, true };
}

/* ---------------- 연습 3: bit-band ---------------- */
/* 비트밴드 영역 1 MB 두 개: SRAM 0x2000_0000 → alias 0x2200_0000, 주변장치 0x4000_0000 → alias 0x4200_0000 */
static uint32_t bb_alias(uint32_t addr, unsigned bit) {
    if (bit > 7) return 0;
    if (addr - 0x20000000u < 0x100000u) return 0x22000000u + (addr - 0x20000000u) * 32u + bit * 4u;
    if (addr - 0x40000000u < 0x100000u) return 0x42000000u + (addr - 0x40000000u) * 32u + bit * 4u;
    return 0;                                         /* 비트밴드 영역 아님 */
}
static bool bb_target(uint32_t alias, uint32_t *addr, unsigned *bit) {
    uint32_t region;
    if (alias - 0x22000000u < 0x2000000u) region = 0x20000000u;
    else if (alias - 0x42000000u < 0x2000000u) region = 0x40000000u;
    else return false;
    uint32_t off = alias - (region + 0x02000000u);
    if (off & 3u) return false;                       /* alias 는 워드 단위 */
    *addr = region + off / 32u;
    *bit = (off % 32u) / 4u;
    return true;
}
static uint8_t bb_sram[0x100];                        /* 0x2000_0000.. 의 앞부분만 모델 */
static bool bb_write(uint32_t alias, uint32_t val) {  /* CPU 쪽에선 쓰기 한 번, 버스가 RMW */
    uint32_t a; unsigned bit;
    if (!bb_target(alias, &a, &bit) || a - 0x20000000u >= sizeof bb_sram) return false;
    uint8_t *p = &bb_sram[a - 0x20000000u];
    *p = (uint8_t)((val & 1u) ? (*p | (1u << bit)) : (*p & ~(1u << bit)));
    return true;
}

/* ---------------- 연습 4: 자연 정렬 전송으로 쪼개기 ---------------- */
typedef struct { uint32_t addr; unsigned size; } xfer_t;
/* 매 단계에서 "현재 주소에 정렬되고 남은 길이를 넘지 않는 가장 큰 폭(4/2/1)" 을 고른다 */
static int split_aligned(uint32_t addr, unsigned size, xfer_t *out) {
    int n = 0;
    while (size) {
        unsigned w = 4;
        while (w > size || (addr & (w - 1u))) w >>= 1;
        out[n].addr = addr; out[n].size = w; n++;
        addr += w; size -= w;
    }
    return n;
}

int main(void) {
    /* 연습 1 */
    const rgn_t m[] = { { 0x00000000u, 0x10000u }, { 0x08000000u, 0x10000u }, { 0x20000000u, 0x4000u },
                        { 0x40004400u, 0x400u },   { 0x40020000u, 0x400u },   { 0xE000E000u, 0x1000u },
                        { 0xFFFFF000u, 0x1000u } };
    const int n = (int)(sizeof m / sizeof m[0]);
    int bad = 0;
    for (int i = 0; i < n; i++)
        for (int d = -4; d <= 4; d++) {
            uint32_t e0 = m[i].base + (uint32_t)d, e1 = m[i].base + m[i].size + (uint32_t)d;
            if (find_bsearch(m, n, e0) != find_linear(m, n, e0)) bad++;
            if (find_bsearch(m, n, e1) != find_linear(m, n, e1)) bad++;
        }
    for (int k = 0; k < 200000; k++) {
        uint32_t a = (k & 1) ? xorshift32() : m[xorshift32() % (uint32_t)n].base + (xorshift32() % 0x20000u);
        if (find_bsearch(m, n, a) != find_linear(m, n, a)) bad++;
    }
    CHECK(bad == 0);
    CHECK(find_bsearch(m, n, 0xFFFFFFFFu) == 6 && find_bsearch(m, n, 0x40004000u) == -1);

    /* 연습 2 */
    CHECK(strcmp(cm_class(0x08000000u).name, "Code") == 0 && !cm_class(0x08000000u).xn);
    CHECK(strcmp(cm_class(0x20001000u).name, "SRAM") == 0 && cm_class(0x20001000u).type == MT_NORMAL);
    CHECK(strcmp(cm_class(0x40004400u).name, "Peripheral") == 0 && cm_class(0x40004400u).type == MT_DEVICE
          && cm_class(0x40004400u).xn);
    CHECK(strcmp(cm_class(0x60000000u).name, "External RAM") == 0);
    CHECK(strcmp(cm_class(0xA0000000u).name, "External device") == 0);
    CHECK(strcmp(cm_class(0xE000ED00u).name, "PPB") == 0 && cm_class(0xE000ED00u).type == MT_SO);   /* SCB */
    CHECK(strcmp(cm_class(0xE0100000u).name, "Vendor system") == 0);
    CHECK(strcmp(cm_class(0x1FFFFFFFu).name, "Code") == 0 && strcmp(cm_class(0xDFFFFFFFu).name, "External device") == 0);

    /* 연습 3 — ARM 문서의 예 */
    CHECK(bb_alias(0x20000000u, 0) == 0x22000000u);
    CHECK(bb_alias(0x20000000u, 7) == 0x2200001Cu);
    CHECK(bb_alias(0x200FFFFFu, 0) == 0x23FFFFE0u);
    CHECK(bb_alias(0x400FFFFFu, 7) == 0x43FFFFFCu);
    CHECK(bb_alias(0x20100000u, 0) == 0 && bb_alias(0x08000000u, 0) == 0);
    {
        uint32_t a; unsigned b;
        CHECK(bb_target(0x23FFFFE0u, &a, &b) && a == 0x200FFFFFu && b == 0);
        CHECK(bb_target(0x2200001Cu, &a, &b) && a == 0x20000000u && b == 7);
        CHECK(!bb_target(0x22000002u, &a, &b) && !bb_target(0x24000000u, &a, &b));
        int rt = 0;
        for (uint32_t off = 0; off < 0x100000u; off += 0x3FFu)
            for (unsigned bit = 0; bit < 8; bit++) {
                uint32_t al = bb_alias(0x40000000u + off, bit);
                if (!bb_target(al, &a, &b) || a != 0x40000000u + off || b != bit) rt++;
            }
        CHECK(rt == 0);
    }
    bb_sram[0x10] = 0xF0u;
    CHECK(bb_write(bb_alias(0x20000010u, 0), 1) && bb_sram[0x10] == 0xF1u);
    CHECK(bb_write(bb_alias(0x20000010u, 7), 0) && bb_sram[0x10] == 0x71u);
    CHECK(bb_write(bb_alias(0x20000010u, 3), 0xFFFFFFFEu) && bb_sram[0x10] == 0x71u);   /* bit0 만 본다 */

    /* 연습 4 */
    {
        xfer_t x[8];
        CHECK(split_aligned(0x20000000u, 4, x) == 1 && x[0].size == 4);
        CHECK(split_aligned(0x20000001u, 4, x) == 3 && x[0].size == 1 && x[1].addr == 0x20000002u
              && x[1].size == 2 && x[2].addr == 0x20000004u && x[2].size == 1);
        CHECK(split_aligned(0x20000002u, 4, x) == 2 && x[0].size == 2 && x[1].size == 2);
        CHECK(split_aligned(0x20000003u, 2, x) == 2 && x[0].size == 1 && x[1].size == 1);
        CHECK(split_aligned(0x20000003u, 4, x) == 3 && x[0].size == 1 && x[1].addr == 0x20000004u
              && x[1].size == 2 && x[2].addr == 0x20000006u && x[2].size == 1);
        int tot = 0, okb = 1;
        for (uint32_t a = 0; a < 4; a++)
            for (unsigned s = 1; s <= 4; s++) {
                int k = split_aligned(a, s, x); unsigned sum = 0; uint32_t next = a;
                for (int j = 0; j < k; j++) {
                    if (x[j].addr != next || (x[j].addr & (x[j].size - 1u))) okb = 0;
                    sum += x[j].size; next += x[j].size;
                }
                if (sum != s) okb = 0;
                tot += k;
            }
        CHECK(okb && tot == 27);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
