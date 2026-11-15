/* ALGO-A04 demo_m4 — Cortex-M4 에서 push 한 번이 어떤 코드가 되나 (freestanding, libc 없음)
 * asm:   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 * 스택:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c -fstack-usage demo_m4.c -o demo_m4.o && cat demo_m4.su
 * 프리앰블 55 55 7E 는 상수 → 실패표·DFA 표를 const 로 두면 플래시(.rodata)에 들어간다.
 */
#include <stdbool.h>
#include <stdint.h>

#define M 3
static const uint8_t PAT[M] = {0x55, 0x55, 0x7E};
static const uint8_t FAIL[M] = {0, 1, 0};

/* (M+1)×256 = 1024 B. 행 q, 열 b. 손으로 채우지 않고 지정 초기화로: 기본 0, 예외만 */
#define ROW_DEFAULT(q) [256 * (q) + 0x55] = 1
static const uint8_t DFA[(M + 1) * 256] = {
    ROW_DEFAULT(0),
    [256 * 1 + 0x55] = 2,
    [256 * 2 + 0x55] = 2, [256 * 2 + 0x7E] = 3,
    ROW_DEFAULT(3),
};

typedef struct { uint8_t q; } kmp_state_t;

bool kmp_push(kmp_state_t *s, uint8_t b) {
    unsigned q = s->q;
    while (q > 0 && b != PAT[q]) q = FAIL[q - 1];
    if (b == PAT[q]) q++;
    bool hit = (q == M);
    if (hit) q = FAIL[M - 1];
    s->q = (uint8_t)q;
    return hit;
}

bool dfa_push(kmp_state_t *s, uint8_t b) {
    unsigned q = DFA[256u * s->q + b];
    s->q = (uint8_t)q;
    return q == M;
}

typedef struct { uint32_t h; uint8_t ring[M]; uint8_t head; uint8_t seen; } rk_state_t;
#define RK_B 0x01000193u
#define RK_BM1 (RK_B * RK_B)                 /* B^(M-1) mod 2^32 */
#define RK_HP ((((uint32_t)0x55 * RK_B + 0x55) * RK_B) + 0x7E)

bool rk32_push(rk_state_t *r, uint8_t b) {
    uint8_t out = r->ring[r->head];
    r->ring[r->head] = b;
    r->head = (uint8_t)(r->head + 1 == M ? 0 : r->head + 1);
    r->h = (r->h - (uint32_t)out * RK_BM1) * RK_B + b;
    if (r->seen < M) { r->seen++; if (r->seen < M) return false; }
    if (r->h != RK_HP) return false;
    for (unsigned i = 0, j = r->head; i < M; i++, j = (j + 1 == M ? 0 : j + 1))
        if (r->ring[j] != PAT[i]) return false;
    return true;
}

/* mod (2^31-1) 롤링 한 번: 64비트 곱이 M4 에선 umull/umlal, M0 에선 __aeabi_lmul 호출이 된다
 *   clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_m4.c -o - | grep -E "umull|umlal|lmul" */
#define RK_P 2147483647u
static uint32_t mod_p(uint64_t x) { x = (x & RK_P) + (x >> 31); x = (x & RK_P) + (x >> 31); return (uint32_t)(x >= RK_P ? x - RK_P : x); }
uint32_t rkp_roll(uint32_t h, uint8_t out, uint8_t in, uint32_t bm1) {
    uint32_t t = h + RK_P - mod_p((uint64_t)out * bm1);
    return mod_p((uint64_t)t * 1000003u + in);
}
