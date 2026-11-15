/* ARM-M01 추가 연습 — 한 번에 검증 (practice.S 의 답 5개)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_violators.S -o p && ./p
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { int32_t r, g, b; } rgb12;      /* 12 B, 비-HFA → x0, x1 */
typedef struct { float v[5]; } f5;              /* 20 B 비-HFA → 인자는 포인터, 반환은 x8 */
typedef struct { float w, x, y, z; } quat;      /* HFA */

rgb12 make_rgb(int32_t r, int32_t g, int32_t b);
f5    f5_scale(f5 v, float k);
quat  quat_add3(quat a, quat b, quat c);
long  mixed_iv(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7, double d, long a8);
long  vsum(long n, ...);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
static uint64_t xs = 0x2545F4914F6CDD1Du;
static uint64_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17; return xs; }

int main(void) {
    /* 연습 1 */
    rgb12 c = make_rgb(-1, 0x7fffffff, INT32_MIN);
    CHECK(c.r == -1 && c.g == 0x7fffffff && c.b == INT32_MIN);
    int ok = 1;
    for (int k = 0; k < 1000; k++) {
        int32_t r = (int32_t)(uint32_t)rnd(), g = (int32_t)(uint32_t)rnd(), b = (int32_t)(uint32_t)rnd();
        rgb12 t = make_rgb(r, g, b);
        ok &= (t.r == r && t.g == g && t.b == b);
    }
    CHECK(ok);
    /* 연습 2 */
    f5 v = {{1, -2, 0.5f, 3, 1e30f}};
    f5 s = f5_scale(v, 3.0f);
    CHECK(s.v[0] == 3.0f && s.v[1] == -6.0f && s.v[2] == 1.5f && s.v[3] == 9.0f && s.v[4] == 1e30f * 3.0f);
    CHECK(v.v[1] == -2.0f);                                   /* 원본은 안 바뀜 (호출자가 복사본을 넘김) */
    v = f5_scale(v, 2.0f);                                    /* 인자와 결과가 같은 변수 */
    CHECK(v.v[0] == 2.0f && v.v[1] == -4.0f && v.v[4] == 2e30f);
    /* 연습 3 */
    quat a = {1, 2, 3, 4}, b = {5, 6, 7, 8}, cc = {9, 10, 11, 12.5f};
    quat r = quat_add3(a, b, cc);
    CHECK(r.w == 15.0f && r.x == 18.0f && r.y == 21.0f && r.z == 24.5f);
    /* 연습 4 */
    CHECK(mixed_iv(1, 2, 3, 4, 5, 6, 7, 8, 9.5, 10) == 1 + 10 + 9);
    CHECK(mixed_iv(-100, 0, 0, 0, 0, 0, 0, 0, -2.75, 1L << 40) == -100 + (1L << 40) - 2);
    /* 연습 5 */
    CHECK(vsum(0) == 0);
    CHECK(vsum(3, 10L, 20L, 30L) == 60);
    CHECK(vsum(10, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 1000L) == 1045);   /* 표준 ABI 에선 7개 레지스터 + 스택 3개 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
