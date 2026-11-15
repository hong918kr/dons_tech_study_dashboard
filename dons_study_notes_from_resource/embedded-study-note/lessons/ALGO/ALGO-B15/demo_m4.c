/* ALGO-B15 demo: 파서를 Cortex-M4 용으로 컴파일해 코드 크기와 스택 사용량을 본다 (컴파일만, 링크 안 함).
 * solution.c 의 함수는 static 이라 쓰이지 않으면 사라지므로, 바깥으로 노출하는 얇은 래퍼를 둔다.
 * Cortex-M4 (-Os), 맥 SDK 헤더를 빌려 씀 — 이 파일에서 libc 는 memset/memcpy 만 실제로 쓰인다:
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -fstack-usage \
 *         -isystem $SDKROOT/usr/include -c demo_m4.c -o /tmp/ALGO-B15_m4.o
 *   cat /tmp/ALGO-B15_m4.su ; xcrun llvm-nm --size-sort -S /tmp/ALGO-B15_m4.o
 * 맥에서 그냥 실행해도 된다: cc -std=c11 -Wall -Wextra -Werror -O2 demo_m4.c -o demo_m4 && ./demo_m4
 */
#define ALGO_B15_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

cp_err_t m4_cmd_parse(char *line, cmd_t *out);
void m4_rx(linebuf_t *lb, uint8_t c);
lb_status_t m4_take(linebuf_t *lb, char *dst, size_t cap);

cp_err_t m4_cmd_parse(char *line, cmd_t *out) { return cmd_parse(line, out); }
void m4_rx(linebuf_t *lb, uint8_t c) { lb_rx_byte(lb, c); }
lb_status_t m4_take(linebuf_t *lb, char *dst, size_t cap) { return lb_take(lb, dst, cap); }

#if !defined(__arm__)
int main(void) {
    char line[] = "READ 0x4000_0010 4";
    cmd_t c;
    cp_err_t e = m4_cmd_parse(line, &c);
    printf("sizeof(cmd_t)=%zu sizeof(linebuf_t)=%zu  parse -> %s addr=0x%08lX len=%lu\n", sizeof(cmd_t),
           sizeof(linebuf_t), cp_err_name[e], (unsigned long)c.u.read.addr, (unsigned long)c.u.read.len);
    return e == CP_OK ? 0 : 1;
}
#endif
