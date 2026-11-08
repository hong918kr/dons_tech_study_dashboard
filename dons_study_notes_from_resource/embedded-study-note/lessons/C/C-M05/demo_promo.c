/* C-M05 demo: 32/64비트 값을 바이트로 조립할 때의 정수 승격(C-B02) 함정 3가지.
 *   cc -std=c11 -O2 demo_promo.c -o demo_promo && ./demo_promo
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_promo.c -o demo_promo_ub && ./demo_promo_ub
 * (일부러 UB 를 담은 관찰용 파일 — 하네스 아님) */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* ① 캐스트 없이 << 24 : p[0] 은 int 로 승격 → 0xFF << 24 는 int 범위 밖 = UB (6.5.7p4) */
static uint32_t be32_bug(const uint8_t *p) {
    return (uint32_t)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
}
/* ② 32비트 조립을 int 로 한 뒤 uint64_t 로 넓힘 : 음수 int → 64비트 부호 확장 */
static uint64_t be64_bug_sext(const uint8_t *p) {
    return ((uint64_t)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) << 32) |
           (uint64_t)(p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7]);
}
/* ③ uint32_t 로 캐스트하고 32 이상 시프트 : 시프트 폭 >= 타입 폭 = UB (6.5.7p3) */
static uint64_t be64_bug_width(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint32_t)p[i] << (56 - 8 * i);
    return v;
}
/* 정답 : 시프트하기 전에 결과 폭의 무부호 타입으로 */
static uint64_t be64_ok(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (56 - 8 * i);
    return v;
}

int main(void) {
    volatile uint8_t src[8] = { 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8 };
    uint8_t p[8];
    for (int i = 0; i < 8; i++) p[i] = src[i];       /* 상수 접기 방지 */
    printf("be32_bug       = 0x%08" PRIX32 "          (기대 0xFFFEFDFC)\n", be32_bug(p));
    printf("be64_bug_sext  = 0x%016" PRIX64 "  (기대 0xFFFEFDFCFBFAF9F8)\n", be64_bug_sext(p));
    printf("be64_bug_width = 0x%016" PRIX64 "\n", be64_bug_width(p));
    printf("be64_ok        = 0x%016" PRIX64 "\n", be64_ok(p));
    return 0;
}
