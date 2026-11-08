/* C-M13 demo — 세 가지 punning 방식이 -O2 에서 어떤 명령어가 되는지 비교한다 (main 없음, asm 관찰용).
 * 호스트(arm64):   clang -std=c11 -O2 -S demo_punning_asm.c -o -
 * Cortex-M4F:      clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mthumb -O2 -S demo_punning_asm.c -o -
 * Cortex-M0:       clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_punning_asm.c -o -
 * 타깃용 빌드에는 libc 헤더가 없으므로 <string.h> 대신 컴파일러 내장 memcpy 를 쓴다
 * (호스트의 memcpy 도 clang 은 같은 내장 함수로 취급한다).
 */
#include <stdint.h>
#if __has_include(<string.h>) && !defined(__arm__)
#include <string.h>
#else
#define memcpy __builtin_memcpy
#endif

/* ---- float → 비트 ---- */
uint32_t bits_cast(float f)   { return *(uint32_t *)&f; }                 /* UB (6.5p7) */
uint32_t bits_union(float f)  { union { float f; uint32_t u; } x = { .f = f }; return x.u; } /* C OK */
uint32_t bits_memcpy(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }           /* C/C++ OK */

/* ---- 비트 → float ---- */
float float_from_memcpy(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

/* ---- 바이트 버퍼 → uint32_t (정렬 보장 없음) ---- */
uint32_t load_cast(const uint8_t *p)   { return *(const uint32_t *)p; }   /* aliasing + 정렬 UB */
uint32_t load_memcpy(const uint8_t *p) { uint32_t v; memcpy(&v, p, sizeof v); return v; }

/* ---- 고전 예제 (aliasing 가정으로 return 1 상수) ---- */
int f(int *i, float *fp) { *i = 1; *fp = 0.0f; return *i; }

/* ---- 빠른 역제곱근: 원본(Quake III, 1999) vs 안전한 버전 ---- */
float rsqrt_quake(float number) {          /* 원본 모양. long 이 64비트인 LP64 호스트에선 크기부터 틀린다 */
    long i; float x2, y; const float threehalfs = 1.5F;
    x2 = number * 0.5F; y = number;
    i = *(long *)&y;                       /* UB: float 객체를 long 으로 읽음 (+ 크기 불일치) */
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;                      /* UB: long 객체를 float 로 읽음 */
    y = y * (threehalfs - (x2 * y * y));
    return y;
}
float rsqrt_safe(float x) {
    uint32_t i; memcpy(&i, &x, sizeof i);
    i = 0x5F3759DFu - (i >> 1);
    float y; memcpy(&y, &i, sizeof y);
    return y * (1.5f - 0.5f * x * y * y);
}
float rsqrt_fpu(float x) { return 1.0f / __builtin_sqrtf(x); }   /* FPU 가 있으면 이게 기준선 */

/* ---- 반대 방향의 비용: 문자 타입 쓰기는 "무엇이든 바꿀 수 있다" → 컴파일러가 매번 다시 읽는다 ----
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fno-unroll-loops -S demo_punning_asm.c -o - */
struct ctx { uint32_t n; uint32_t val; };
void fill8(struct ctx *c, uint8_t *out)   { for (uint32_t i = 0; i < c->n; i++) out[i] = (uint8_t)c->val; }
void fill16(struct ctx *c, uint16_t *out) { for (uint32_t i = 0; i < c->n; i++) out[i] = (uint16_t)c->val; }
