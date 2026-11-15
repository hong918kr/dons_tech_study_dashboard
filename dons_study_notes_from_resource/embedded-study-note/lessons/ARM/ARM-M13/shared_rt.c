/* ENV: qemu */
/* ARM-M13 shared_rt.c — 세미호스팅 출력 + CHECK 집계 (주어짐, TODO 없음). libc 없음. */
#include <stdint.h>

uint64_t sh_call(uint32_t op, const void *param);   /* shared_start.S */
void sh_exit(uint32_t code) __attribute__((noreturn));

volatile uint64_t g_boot_el;        /* _start 가 쓴다: 부팅 직후 CurrentEL.EL */
unsigned g_pass, g_total;

void sh_puts(const char *s) { sh_call(0x04u, s); }  /* SYS_WRITE0: NUL 까지 출력 */

void sh_hex(uint64_t v) {                           /* 0x 접두 16진수, 앞의 0 생략 (최소 1자리) */
    char buf[19];
    int n = 0;
    buf[n++] = '0';
    buf[n++] = 'x';
    int started = 0;
    for (int sh = 60; sh >= 0; sh -= 4) {
        unsigned d = (unsigned)(v >> sh) & 0xFu;
        if (d || started || sh == 0) {
            buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            started = 1;
        }
    }
    buf[n] = '\0';
    sh_puts(buf);
}

void sh_dec(uint64_t v) {
    char buf[21];
    int n = 20;
    buf[n] = '\0';
    do { buf[--n] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&buf[n]);
}

void check(int ok, const char *expr, int line) {
    g_total++;
    if (ok) { g_pass++; sh_puts("PASS "); }
    else    { sh_puts("FAIL line "); sh_dec((uint64_t)line); sh_puts(": "); }
    sh_puts(expr);
    sh_puts("\n");
}

int finish(void) {                                   /* "N/M passed" + 종료 코드 */
    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
