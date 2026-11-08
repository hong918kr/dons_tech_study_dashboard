/* C-M04 demo — 자연 정렬(non-packed) 구조체를 홀수 오프셋에 캐스팅하면?
 *   cc -std=c11 -g -O1 -fsanitize=alignment demo_misaligned_cast.c -o mcast && ./mcast
 *   cc -std=c11 -Wall -Wextra -Wcast-align -fsyntax-only demo_misaligned_cast.c
 * 일부러 UB 를 담은 관찰용 파일이다 (하네스 아님).
 */
#include <stdint.h>
#include <stdio.h>

struct nat_hdr { uint8_t type; uint16_t len; uint32_t seq; };                           /* 8 B, align 4 */
struct __attribute__((packed)) wire_hdr { uint8_t type; uint16_t len; uint32_t seq; };  /* 7 B, align 1 */

static _Alignas(4) uint8_t rx[16] = {
    0xA5,                                   /* [0] sync                 */
    0x02, 0x10, 0x00, 0x78, 0x56, 0x34, 0x12 /* [1..7] type len seq (LE) */
};

int main(void) {
    const uint8_t *p = rx + 1;                       /* 헤더는 오프셋 1 (홀수) */

    /* (1) 오답: 자연 정렬 구조체로 캐스팅 — 정렬 4 요구, 주소는 4k+1 */
    const struct nat_hdr *n = (const struct nat_hdr *)p;
    printf("nat   : type=%u len=%u seq=0x%08x\n",
           (unsigned)n->type, (unsigned)n->len, (unsigned)n->seq);

    /* (2) packed 구조체로 캐스팅 — 정렬 1 이라 주소 요구가 없다 */
    const struct wire_hdr *w = (const struct wire_hdr *)p;
    printf("packed: type=%u len=%u seq=0x%08x\n",
           (unsigned)w->type, (unsigned)w->len, (unsigned)w->seq);
    return 0;
}
