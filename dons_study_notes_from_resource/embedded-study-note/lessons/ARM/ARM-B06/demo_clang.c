/* ARM-B06 demo: C 로 쓴 비트 연산을 clang -O2 가 어떤 AArch64 명령으로 바꾸나
 * clang -std=c11 -O2 -S -fno-asynchronous-unwind-tables -o - demo_clang.c | grep -v '^\s*\.' */
#include <stdint.h>

#define FIELD_LSB(m)        ((uint32_t)(m) & (~(uint32_t)(m) + 1u))
#define FIELD_GET(m, r)     (((uint32_t)(r) & (uint32_t)(m)) / FIELD_LSB(m))
#define FIELD_PREP(m, v)    (((uint32_t)(v) * FIELD_LSB(m)) & (uint32_t)(m))
#define FIELD_REPLACE(m, r, v) (((uint32_t)(r) & ~(uint32_t)(m)) | FIELD_PREP(m, v))

uint32_t c_get_state(uint32_t r)              { return FIELD_GET(0xF0u, r); }
int32_t  c_get_temp(uint32_t r)               { return (int8_t)(r >> 16); }   /* 구현 정의 변환 (clang: 2의 보수 절단) */
uint32_t c_prep_state(uint32_t v)             { return FIELD_PREP(0xF0u, v); }
uint32_t c_set_state(uint32_t r, uint32_t v)  { return FIELD_REPLACE(0xF0u, r, v); }
uint64_t c_get_var(uint64_t r, unsigned lsb, unsigned w) {
    return (r >> lsb) & (w >= 64 ? ~0ull : (1ull << w) - 1u);
}
uint32_t c_clrset(uint32_t r, uint32_t c, uint32_t s) { return (r & ~c) | s; }
int c_hsb32_builtin(uint32_t x)               { return 31 - __builtin_clz(x); }        /* x==0 은 UB */
int c_hsb32_safe(uint32_t x)                  { return x ? 31 - __builtin_clz(x) : -1; }
uint32_t c_rbit_loop(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}
uint32_t c_rbit_builtin(uint32_t x)           { return __builtin_bitreverse32(x); }
uint32_t c_bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
}
uint16_t c_bswap16(uint16_t x)                { return (uint16_t)((x >> 8) | (x << 8)); }
uint32_t c_nibrev32(uint32_t x) {
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);    /* 바이트 안 니블 교환 */
    return __builtin_bswap32(x);                                  /* 바이트 역순 */
}
uint32_t c_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
