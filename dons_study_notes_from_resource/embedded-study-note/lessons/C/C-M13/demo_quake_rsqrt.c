/* C-M13 demo — Quake III 빠른 역제곱근 원본(1999)을 64비트 호스트에서 돌려 본다. 일부러 UB 포함.
 *   cc -std=c11 -O0 demo_quake_rsqrt.c -o /tmp/q0 && /tmp/q0
 *   cc -std=c11 -O2 demo_quake_rsqrt.c -o /tmp/q2 && /tmp/q2
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined demo_quake_rsqrt.c -o /tmp/qs && /tmp/qs
 * q_long : 원본 그대로 (long). LP64 에서 long = 8바이트 → float(4바이트) 객체를 8바이트로 읽는다.
 * q_i32  : 크기는 맞춘 버전 (int32_t). 크기 문제는 없지만 여전히 6.5p7 aliasing 위반.
 * q_safe : memcpy 버전. 정의된 동작.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

__attribute__((noinline)) float q_long(float number) {
    long i; float x2, y; const float threehalfs = 1.5F;
    x2 = number * 0.5F; y = number;
    i = *(long *)&y;                       /* evil floating point bit level hacking */
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;
    y = y * (threehalfs - (x2 * y * y));
    return y;
}
__attribute__((noinline)) float q_i32(float number) {
    int32_t i; float x2, y; const float threehalfs = 1.5F;
    x2 = number * 0.5F; y = number;
    i = *(int32_t *)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;
    y = y * (threehalfs - (x2 * y * y));
    return y;
}
__attribute__((noinline)) float q_safe(float x) {
    uint32_t i; memcpy(&i, &x, sizeof i);
    i = 0x5F3759DFu - (i >> 1);
    float y; memcpy(&y, &i, sizeof y);
    return y * (1.5f - 0.5f * x * y * y);
}

int main(void) {
    printf("sizeof(long) = %zu\n", sizeof(long));
    printf("q_long(4) = %g   (첫 호출)\n", (double)q_long(4.0f));
    printf("q_safe(4) = %g\n", (double)q_safe(4.0f));
    printf("q_i32(4)  = %g\n", (double)q_i32(4.0f));
    printf("q_long(4) = %g   (q_i32 다음 호출)\n", (double)q_long(4.0f));
    return 0;
}
