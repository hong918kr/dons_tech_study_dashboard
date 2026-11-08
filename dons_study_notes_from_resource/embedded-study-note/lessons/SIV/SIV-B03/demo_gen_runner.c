/* SIV-B03 demo: 생성된 벡터 표(gen_uart0_reset_vec.h)만으로 도는 "타깃 쪽" 리셋값 러너.
 *
 * 만드는 법 (solution 출력에서 생성 부분만 잘라 헤더로):
 *   /tmp/SIV-B03_so | sed -n '/^\/\* generated/,/^};/p' > gen_uart0_reset_vec.h
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_gen_runner.c -o /tmp/SIV-B03_gen && /tmp/SIV-B03_gen
 *
 * 실보드에서는 read32() 가 *(volatile uint32_t *)(UART0_BASE + off) 이다. 여기서는 맥에서 돌리려고
 * "실리콘에서 읽은 값" 을 배열로 흉내낸다 (CTRL.EN 이 1 로 나오는 tie-off 결함이 들어 있다).
 * 표에 RXDATA/TXDATA/TIMER 가 아예 없으므로, 러너는 그 주소를 절대 건드리지 않는다.
 */
#include <stdint.h>
#include <stdio.h>

typedef struct { const char *name; uint32_t off, exp, mask; } reset_vec_t;
#include "gen_uart0_reset_vec.h"

static const uint32_t silicon[12] = {   /* 오프셋 0x000..0x02C, 전원 직후 값 */
    0x55410100u, 0x00000001u, 0x0000001Au, 0x00000000u, 0x0000005Au, 0x00000804u,
    0x00000015u, 0x00000000u, 0x00000000u, 0x00000000u, 0x0001F3A0u, 0x7C000000u,
};
static unsigned touched[12];

static uint32_t read32(uint32_t off) { touched[off >> 2]++; return silicon[off >> 2]; }

int main(void) {
    int fail = 0, n = (int)(sizeof UART0_reset_vec / sizeof UART0_reset_vec[0]);
    for (int i = 0; i < n; i++) {
        const reset_vec_t *v = &UART0_reset_vec[i];
        uint32_t act = read32(v->off), diff = (act ^ v->exp) & v->mask;
        if (diff) {
            fail++;
            printf("FAIL reg=%s off=0x%03X exp=0x%08X act=0x%08X diff=0x%08X\n", v->name, (unsigned)v->off,
                   (unsigned)v->exp, (unsigned)act, (unsigned)diff);
        }
    }
    printf("vectors=%d fail=%d  touched RXDATA=%u TIMER=%u\n", n, fail, touched[0x010 >> 2], touched[0x028 >> 2]);
    return fail ? 1 : 0;
}
