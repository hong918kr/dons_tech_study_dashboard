/* ALGO-M01 demo_trace.c — CRC-8/SMBUS (poly 0x07) 로 '1'(0x31) 한 바이트를 손 계산하듯 두 가지로 보여 준다.
 *   (1) 레지스터(direct 알고리즘) 8단계: 나가는 비트 ⊕ 들어오는 비트 = 1 이면 poly XOR
 *   (2) 교과서식 long division: 0x31·x^8 를 G = x^8 + x^2 + x + 1 (1 0000 0111) 로 나눈다
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o demo_trace && ./demo_trace
 */
#include <stdint.h>
#include <stdio.h>

static void bits(uint32_t v, int n) { for (int i = n - 1; i >= 0; i--) putchar('0' + (int)((v >> i) & 1u)); }

int main(void) {
    const uint8_t b = 0x31;
    const uint32_t poly = 0x07;

    printf("(1) direct register, CRC-8/SMBUS, init 0x00, byte 0x31 = "); bits(b, 8); printf(" (MSB first)\n");
    printf("step  in  out  in^out  reg after shift   reg after XOR\n");
    uint32_t crc = 0;
    for (int k = 7; k >= 0; k--) {
        uint32_t in = (b >> k) & 1u, out = (crc >> 7) & 1u;
        uint32_t sh = (crc << 1) & 0xFFu;
        crc = (in ^ out) ? sh ^ poly : sh;
        printf("  %d    %u   %u     %u     ", 8 - k, in, out, in ^ out);
        bits(sh, 8); printf("        "); bits(crc, 8);
        printf("%s\n", (in ^ out) ? "  (^ 00000111)" : "");
    }
    printf("CRC = 0x%02X\n\n", crc);

    printf("(2) long division of 0x31 * x^8 by G = 100000111\n");
    uint32_t v = (uint32_t)b << 8;                 /* 16비트 피제수: 메시지 + 0 8개 (augmented) */
    const uint32_t G = 0x107;
    printf("    "); bits(v, 16); printf("\n");
    for (int e = 15; e >= 8; e--) {
        if (!((v >> e) & 1u)) continue;
        int pad = 15 - e;
        printf("  ^ %*s", pad, ""); bits(G, 9); printf("%*s(G * x^%d)\n", 7 - pad + 4, "", e - 8);
        v ^= G << (e - 8);
        printf("  = "); bits(v, 16); printf("\n");
    }
    printf("remainder = "); bits(v, 8); printf(" = 0x%02X\n", v);
    return 0;
}
