/* demo_tool_gaps.c — 정적 분석 도구가 잡는 것과 못 잡는 것 (C-A10)
 *
 *   cc -std=c11 -Wall -Wextra -O2 demo_tool_gaps.c -o demo_tool_gaps && ./demo_tool_gaps
 *   cc -std=c11 -Weverything -c demo_tool_gaps.c -o /dev/null
 *   clang-tidy demo_tool_gaps.c --checks='-*,bugprone-*,cert-*,misc-*' -- -std=c11 -isysroot <SDK>
 */
#include <stdint.h>
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int g_reads;
static uint8_t g_seq[] = { 0x01, 0x90, 0x55, 0x66 };
static uint8_t bus_read(void) { return g_seq[g_reads++ & 3]; }   /* 읽을 때마다 다음 값 (FIFO 처럼) */

/* ① 부작용 있는 매크로 인자: ++ 는 잡고, 함수 호출은 못 잡는다 */
static int peak_incr(const uint8_t *p, int peak) {
    int i = 0;
    int m = MAX(p[i++], peak);
    return m * 10 + i;                     /* i 가 2 면 두 번 증가한 것 */
}
static int peak_call(int peak) { return MAX(bus_read(), peak); }

/* ② 같은 식 안의 두 함수 호출: 평가 순서 미지정 (6.5p3, 6.5.2.2p10) — 래치형 레지스터에서 치명적 */
static unsigned read16_unseq(void) { return (unsigned)(bus_read() << 8 | bus_read()); }

/* ③ 8진수 상수: 자리 맞추려고 붙인 0 */
static const int k_hz[] = { 001, 005, 010, 050, 100 };

/* ④ 뒤로 가는 goto (재시도 루프) */
static int retry_backward(int fail_times) {
    int tries = 3;
again:
    if (fail_times-- > 0) {
        if (--tries) goto again;
        return -1;
    }
    return tries;
}

/* ⑤ fall-through: 표시한 것과 안 한 것 */
static int ft(int c) {
    int r = 0;
    switch (c) {
    case 0: r += 1;
        __attribute__((fallthrough));      /* 의도 표시 (C23: [[fallthrough]];) */
    case 1: r += 10;                       /* 표시 없음 */
    case 2: r += 100; break;
    default: break;
    }
    return r;
}

int main(void) {
    static const uint8_t a[] = { 5, 9 };
    printf("peak_incr = %d  (m*10 + i)\n", peak_incr(a, 0));
    g_reads = 0;
    int pk = peak_call(0);                 /* printf 인자 안에서 부르면 그 자체가 ② 의 문제 */
    printf("peak_call = %d, bus reads = %d (1 expected)\n", pk, g_reads);
    g_reads = 0;
    unsigned r16 = read16_unseq();
    printf("read16 = 0x%04x  (H=0x01 first -> 0x0190, L first -> 0x9001)\n", r16);
    printf("k_hz = %d %d %d %d %d\n", k_hz[0], k_hz[1], k_hz[2], k_hz[3], k_hz[4]);
    printf("retry: %d %d\n", retry_backward(0), retry_backward(5));
    printf("ft: %d %d %d\n", ft(0), ft(1), ft(2));
    return 0;
}
