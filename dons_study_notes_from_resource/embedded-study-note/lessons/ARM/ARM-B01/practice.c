/* ARM-B01 추가 연습 검증 — practice.S 의 답을 한 번에 테스트
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_hostile.S -o /tmp/ARM-B01_practice && /tmp/ARM-B01_practice
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

long          add8(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7);
unsigned long min_ulong(unsigned long a, unsigned long b);
uint32_t      add_u8(uint8_t a, uint8_t b);
size_t        count_eq(const long *p, size_t n, long key);
long          sext_s32(int v);
uint64_t      zext_u32(uint32_t v);

typedef void (*anyfn)(void);
int  call2_dirty_i(anyfn fn, uint64_t r0, uint64_t r1);     /* shared_hostile.S */
long call2_dirty_l(anyfn fn, uint64_t r0, uint64_t r1);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

int main(void) {
    /* 연습 1: 인자 8개 = x0..x7 */
    CHECK(add8(1, 2, 3, 4, 5, 6, 7, 8) == 36);
    CHECK(add8(1, 10, 100, 1000, 10000, 100000, 1000000, 10000000) == 11111111);  /* 자리마다 다른 값 → 레지스터 하나라도 빠지면 티가 난다 */
    CHECK(add8(LONG_MAX, 1, 0, 0, 0, 0, 0, 0) == LONG_MIN);                        /* wrap: 손으로 쓴 기댓값 */

    /* 연습 2: unsigned 최솟값 */
    CHECK(min_ulong(1, ULONG_MAX) == 1);         /* signed 조건(lt)을 쓰면 ULONG_MAX(-1) 이 이긴다 */
    CHECK(min_ulong(ULONG_MAX, 0) == 0 && min_ulong(5, 5) == 5);

    /* 연습 3: uint8 덧셈 — 정상 호출 + 비트 8..63 에 쓰레기를 넣은 호출 */
    CHECK(add_u8(200, 100) == 300u && add_u8(255, 255) == 510u);
    {
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            uint8_t a = (uint8_t)rnd(), b = (uint8_t)rnd();
            uint64_t ra = (rnd() & ~0xFFull) | a, rb = (rnd() & ~0xFFull) | b;
            if (call2_dirty_i((anyfn)add_u8, ra, rb) != (int)(a + b)) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 4: count_eq */
    {
        long v[8] = {3, -1, 3, 7, 3, 0, -1, 3};
        CHECK(count_eq(v, 8, 3) == 4 && count_eq(v, 8, -1) == 2 && count_eq(v, 8, 9) == 0);
        CHECK(count_eq(NULL, 0, 3) == 0);
        int bad = 0;
        long buf[100];
        for (int t = 0; t < 500; t++) {
            size_t n = (size_t)(rnd() % 101), ref = 0;
            long key = (long)(rnd() % 4);
            for (size_t i = 0; i < n; i++) { buf[i] = (long)(rnd() % 4); ref += (buf[i] == key); }
            if (count_eq(buf, n, key) != ref) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 5: 부호/0 확장 — 윗 32비트 쓰레기와 무관해야 한다 */
    CHECK(sext_s32(-1) == -1L && sext_s32(INT_MIN) == (long)INT_MIN && sext_s32(INT_MAX) == (long)INT_MAX);
    CHECK(zext_u32(0xFFFFFFFFu) == 0xFFFFFFFFull);
    CHECK(call2_dirty_l((anyfn)sext_s32, 0xDEADBEEF00000005ull, 0) == 5);
    CHECK(call2_dirty_l((anyfn)sext_s32, 0x00000000FFFFFFFBull, 0) == -5);
    CHECK((uint64_t)call2_dirty_l((anyfn)zext_u32, 0xDEADBEEF80000000ull, 0) == 0x80000000ull);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
