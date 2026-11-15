/* ARM-B11 demo: C 에서는 UB, ISA 에서는 정의된 동작 — 같은 (int32_t)x 가 상황마다 다르게 나온다
 *   cc -std=c11 -O2 demo_c_cast.c -o dc && ./dc                       # 실행 시 FCVTZS → 포화
 *   cc -std=c11 -O0 demo_c_cast.c -o dc0 && ./dc0
 *   cc -std=c11 -O1 -g -fsanitize=float-cast-overflow demo_c_cast.c -o dcu && ./dcu   # UBSan 이 잡는다
 * 상수 접기(constant folding)는 UB 를 "poison" 으로 취급 → 결과가 실행 시 FCVTZS 와 같다는 보장이 없다.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) static int32_t cast_rt(float x) { return (int32_t)x; }
__attribute__((noinline)) static float dot3_c(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];      /* 기본 -ffp-contract=on: 어떤 곱을 fuse 할지 컴파일러가 정함 */
}

int main(void) {
    volatile float vnan = NAN, vbig = 3e9f, vneg = -3e9f;
    printf("runtime : (int32_t)NaN=%d  (int32_t)3e9f=%d  (int32_t)-3e9f=%d\n",
           cast_rt(vnan), cast_rt(vbig), cast_rt(vneg));
    float big = 3e9f;                                      /* 상수 → 컴파일 시간에 접힐 수 있음 */
    int32_t folded = (int32_t)big;
    printf("folded  : (int32_t)3e9f=%d\n", folded);

    const float x = 1.0f + 0x1p-12f;
    const float a[3] = { 1.0f, x, 0.0f }, b[3] = { -(1.0f + 0x1p-11f), x, 0.0f };
    printf("dot3_c  : %a   (asm dot3 = fma chain gives 0x1p-24)\n", (double)dot3_c(a, b));
    return 0;
}
