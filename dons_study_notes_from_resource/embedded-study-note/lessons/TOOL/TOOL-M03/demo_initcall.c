/* ENV: linux — demo_elf.sh 가 VM 에서 initcall_host.ld 와 함께 링크한다. */
/* TOOL-M03 demo: 레벨 번호를 섹션 이름에 넣고, 링커 스크립트의 SORT 로 레벨 순서를 만든다 (Linux initcall 방식). */
#include <stdio.h>
typedef int (*initcall_t)(void);
#define INITCALL(level, fn) \
    static const initcall_t initcall_##fn __attribute__((used, section(".initcall." #level))) = fn
extern const initcall_t __initcall_start[], __initcall_end[];

static int app(void)   { puts("  3 app");   return 0; }
static int clock(void) { puts("  0 clock"); return 0; }
static int uart(void)  { puts("  2 uart");  return 0; }
static int gpio(void)  { puts("  1 gpio");  return 0; }
INITCALL(3, app);
INITCALL(0, clock);
INITCALL(2, uart);
INITCALL(1, gpio);

int main(void) {
    printf("%td initcalls:\n", __initcall_end - __initcall_start);
    for (const initcall_t *f = __initcall_start; f < __initcall_end; f++) (*f)();
    return 0;
}
