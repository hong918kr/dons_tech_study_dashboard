/* ARM-B04 추가 연습 검증 — practice.S 의 5개 답을 C 참조와 대조
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B04_p
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t id; int32_t temp; uint64_t ts; } rec_t;
_Static_assert(offsetof(rec_t, temp) == 4 && sizeof(rec_t) == 16, "rec_t layout");

uint64_t  sum_ldp(const uint64_t *a, size_t n);
void      reverse_copy(uint64_t *dst, const uint64_t *src, size_t n);
uint64_t  lut_sum_u16(const uint16_t *tab, const uint8_t *keys, size_t n);
uint64_t *push2(uint64_t *sp, uint64_t a, uint64_t b);
uint64_t *pop2(uint64_t *sp, uint64_t *a, uint64_t *b);
int64_t   rec_sum_temp_asr(const rec_t *r, size_t n);

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint64_t xs64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define MAXN 40

int main(void) {
    /* 연습 1: sum_ldp */
    {
        const uint64_t ex[3] = { UINT64_MAX, 1, 7 };
        CHECK(sum_ldp(NULL, 0) == 0 && sum_ldp(ex, 1) == UINT64_MAX && sum_ldp(ex, 3) == 7);
        uint64_t a[MAXN]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            uint64_t s = 0;
            for (size_t i = 0; i < n; i++) { a[i] = xs64(); s += a[i]; }
            if (sum_ldp(a, n) != s) ok = 0;
        }
        CHECK(ok);
    }
    /* 연습 2: reverse_copy (가드 포함) */
    {
        uint64_t src[MAXN], buf[MAXN + 2]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) src[i] = xs64();
            for (size_t i = 0; i < MAXN + 2; i++) buf[i] = 0x5A5A5A5A5A5A5A5Aull;
            reverse_copy(buf + 1, src, n);
            for (size_t i = 0; i < n; i++) if (buf[1 + i] != src[n - 1 - i]) ok = 0;
            if (buf[0] != 0x5A5A5A5A5A5A5A5Aull || buf[1 + n] != 0x5A5A5A5A5A5A5A5Aull) ok = 0;
        }
        CHECK(ok);
        const uint64_t s3[3] = { 1, 2, 3 }; uint64_t d3[3];
        reverse_copy(d3, s3, 3);
        CHECK(d3[0] == 3 && d3[1] == 2 && d3[2] == 1);
    }
    /* 연습 3: lut_sum_u16 — 256 엔트리 전부 + 키 255 (uxtw 가 sxtw 였다면 음수 인덱스) */
    {
        uint16_t tab[256]; uint8_t keys[256]; uint64_t s = 0;
        for (int i = 0; i < 256; i++) { tab[i] = (uint16_t)xs64(); keys[i] = (uint8_t)(255 - i); s += tab[i]; }
        CHECK(lut_sum_u16(tab, keys, 256) == s);
        tab[255] = 0xFFFF; keys[0] = 255;
        CHECK(lut_sum_u16(tab, keys, 1) == 0xFFFF);
        int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            uint64_t r = 0;
            for (size_t i = 0; i < n; i++) { keys[i] = (uint8_t)xs64(); r += tab[keys[i]]; }
            if (lut_sum_u16(tab, keys, n) != r) ok = 0;
        }
        CHECK(ok);
    }
    /* 연습 4: push2 / pop2 — 가짜 스택은 높은 주소에서 낮은 주소로 자란다 */
    {
        _Alignas(16) uint64_t stack[8];
        memset(stack, 0, sizeof stack);
        uint64_t *sp = stack + 8;                         /* 빈 스택 = 끝 다음 */
        sp = push2(sp, 0x29, 0x30);                       /* 프롤로그처럼 fp, lr */
        CHECK(sp == stack + 6 && stack[6] == 0x29 && stack[7] == 0x30);
        sp = push2(sp, 111, 222);
        CHECK(sp == stack + 4 && stack[4] == 111 && stack[5] == 222);
        CHECK(((uintptr_t)sp & 15u) == 0);                /* 16바이트 단위라 정렬 유지 */
        uint64_t a = 0, b = 0;
        sp = pop2(sp, &a, &b);
        CHECK(sp == stack + 6 && a == 111 && b == 222);
        sp = pop2(sp, &a, &b);
        CHECK(sp == stack + 8 && a == 0x29 && b == 0x30);
    }
    /* 연습 5: rec_sum_temp_asr */
    {
        const rec_t l[3] = { { 1, -1, 5 }, { 0xFFFFFFFFu, INT32_MIN, 6 }, { 2, INT32_MAX, 7 } };
        CHECK(rec_sum_temp_asr(l, 3) == -1 + (int64_t)INT32_MIN + INT32_MAX);
        rec_t r[MAXN]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            int64_t s = 0;
            for (size_t i = 0; i < n; i++) {
                r[i].id = (uint32_t)xs64(); r[i].temp = (int32_t)(uint32_t)xs64(); r[i].ts = xs64();
                s += r[i].temp;
            }
            if (rec_sum_temp_asr(r, n) != s) ok = 0;
        }
        CHECK(ok);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
