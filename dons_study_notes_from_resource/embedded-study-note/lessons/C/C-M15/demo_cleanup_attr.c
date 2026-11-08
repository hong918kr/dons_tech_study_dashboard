/* C-M15 demo — __attribute__((cleanup(fn))) : 스코프를 벗어날 때 fn(&var) 자동 호출
 * GCC/clang 확장이다 (ISO C 아님). Linux 커널 <linux/cleanup.h> 의 __free()/guard() 가 이걸로 만든다.
 *   cc -std=c11 -Wall -Wextra -O2 demo_cleanup_attr.c -o demo_cleanup_attr && ./demo_cleanup_attr
 */
#include <stdio.h>

static int held;                                   /* 지금 잡혀 있는 자원 수 */
static int  res_get(const char *n) { printf("  +%s\n", n); held++; return 1; }
static void res_put(const char *n) { printf("  -%s\n", n); held--; }

/* cleanup 함수는 "변수의 주소" 를 받는다. 0 이면(안 잡았으면) 아무것도 안 한다. */
static void put_clk(int *h)  { if (*h) res_put("CLK"); }
static void put_gpio(int *h) { if (*h) res_put("GPIO"); }
static void put_dma(int *h)  { if (*h) res_put("DMA"); }
#define AUTO(fn) __attribute__((cleanup(fn)))

static int init(int fail_at) {
    AUTO(put_clk)  int clk  = res_get("CLK");
    AUTO(put_gpio) int gpio = res_get("GPIO");
    if (fail_at == 3) { printf("  !DMA\n"); return -12; }   /* gpio → clk 순서로 자동 해제 */
    AUTO(put_dma)  int dma  = res_get("DMA");
    if (fail_at == 4) { printf("  !IRQ\n"); return -19; }   /* dma → gpio → clk */
    clk = gpio = dma = 0;                          /* 성공: 소유권을 넘긴다 → 해제 안 함 */
    return 0;
}

int main(void) {
    for (int k = 3; k <= 5; k++) {
        held = 0;
        printf("fail_at=%d\n", k);
        int r = init(k);
        printf("  -> ret=%d, held=%d\n", r, held);
    }
    return 0;
}
