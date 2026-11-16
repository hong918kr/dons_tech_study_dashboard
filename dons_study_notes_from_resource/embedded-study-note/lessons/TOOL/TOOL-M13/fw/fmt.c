/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 fmt.c — printf 대신 쓰는 작은 포매터 (Thumb-2 -Os 약 570 B). -DUSE_PRINTF 빌드에서는 컴파일되지 않는다.
 * 지원: %d %i %u %x %X %c %s %%, 플래그 '-' '0', 폭(width), 길이 'l'(32비트라 무시).
 * 미지원: %f, 정밀도(.N), %p, %lld — 펌웨어 로그에 필요한 만큼만. 지원 안 하는 서식은 그대로 찍는다.
 * 버퍼도 malloc 도 FILE 도 없다: 한 글자씩 uart_putc 로 바로 보낸다.
 */
#ifndef USE_PRINTF
#include <stdarg.h>
#include "fw.h"

static int out_pad(char c, int n) {
    for (int i = 0; i < n; i++) uart_putc(c);
    return n > 0 ? n : 0;
}

static int out_field(const char *s, int len, int width, int left, char pad) {
    int n = 0;
    if (!left) n += out_pad(pad, width - len);
    for (int i = 0; i < len; i++) uart_putc(s[i]);
    n += len;
    if (left) n += out_pad(' ', width - len);
    return n;
}

static int out_num(uint32_t v, unsigned base, int neg, int upper, int width, int left, char pad) {
    char buf[12];
    int i = (int)sizeof buf;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        buf[--i] = dig[v % base];
        v /= base;
    } while (v);
    int n = 0;
    if (neg) {
        if (pad == '0') { /* "-0042": 부호가 0 채움보다 앞 */
            uart_putc('-');
            n = 1;
            width--;
        } else {
            buf[--i] = '-';
        }
    }
    return n + out_field(&buf[i], (int)sizeof buf - i, width, left, pad);
}

int log_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            uart_putc(*p);
            n++;
            continue;
        }
        const char *start = p++;
        int left = 0, width = 0;
        char pad = ' ';
        for (; *p == '-' || *p == '0'; p++) {
            if (*p == '-') left = 1;
            else pad = '0';
        }
        if (left) pad = ' ';
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        while (*p == 'l') p++;
        switch (*p) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            uint32_t mag = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
            n += out_num(mag, 10u, v < 0, 0, width, left, pad);
            break;
        }
        case 'u': n += out_num(va_arg(ap, uint32_t), 10u, 0, 0, width, left, pad); break;
        case 'x': n += out_num(va_arg(ap, uint32_t), 16u, 0, 0, width, left, pad); break;
        case 'X': n += out_num(va_arg(ap, uint32_t), 16u, 0, 1, width, left, pad); break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            n += out_field(&c, 1, width, left, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            while (s[len]) len++;
            n += out_field(s, len, width, left, ' ');
            break;
        }
        case '%': uart_putc('%'); n++; break;
        default: /* 모르는 서식: 그대로 찍는다 (조용히 삼키지 않는다) */
            for (const char *q = start; q <= p && *q; q++, n++) uart_putc(*q);
            if (!*p) p--;
            break;
        }
    }
    va_end(ap);
    return n;
}

/* ---- dead: 디버깅용 hexdump (호출하는 곳 없음) ---- */
void log_hexdump(const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        if (i % 16u == 0) log_printf("%08lx:", (unsigned long)i);
        log_printf(" %02x", b[i]);
        if (i % 16u == 15u || i + 1 == n) log_printf("\n");
    }
}
#endif
