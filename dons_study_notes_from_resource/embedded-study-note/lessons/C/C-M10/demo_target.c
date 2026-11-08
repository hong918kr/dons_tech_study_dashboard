/* C-M10 demo — 미니 포매터를 Cortex-M 용으로 크로스 컴파일해 코드 크기 / 나눗셈 확인 (main 없음)
 * 프리스탠딩 헤더(<stdarg.h> <stddef.h> <limits.h> <stdbool.h>)만 쓴다 — 타깃에 libc 가 없다.
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -c demo_target.c -o m4.o && xcrun llvm-size -A m4.o
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -Os -ffreestanding -S demo_target.c -o - | grep -n "bl\s"
 * -ffreestanding 이 없으면 clang 이 my_strlen 루프를 "bl strlen" 호출로 바꿔 버린다 (idiom recognition).
 */
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

_Static_assert(sizeof(va_list) == sizeof(void *), "AAPCS32: va_list = struct { void *__ap; }");

typedef void (*putc_fn)(void *ctx, char c);
#define ITOA_BUFSZ (sizeof(int) * CHAR_BIT + 2)

static size_t my_strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

/* 10진 변환만 따로: M0 에서 '/' '%' 가 무엇이 되는지 보기 */
unsigned utoa10(unsigned v, char *out) {
    char tmp[12];
    unsigned n = 0, i;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

static char *my_utoa(unsigned value, char *buf, int base) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (base < 2 || base > 36) { buf[0] = '\0'; return NULL; }
    char tmp[ITOA_BUFSZ];
    size_t n = 0;
    unsigned b = (unsigned)base;
    do { tmp[n++] = digits[value % b]; value /= b; } while (value != 0);
    for (size_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return buf;
}
static void emit_n(putc_fn out, void *ctx, char c, size_t n, size_t *count) {
    for (size_t i = 0; i < n; i++) out(ctx, c);
    *count += n;
}
/* solution.c 의 mini_vformat 과 같은 코드 (strlen → my_strlen) */
int mini_vformat(putc_fn out, void *ctx, const char *fmt, va_list ap) {
    size_t count = 0;
    char tmp[ITOA_BUFSZ];
    for (; *fmt != '\0'; fmt++) {
        if (*fmt != '%') { out(ctx, *fmt); count++; continue; }
        const char *spec = fmt++;
        bool left = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else break;
        }
        size_t width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (size_t)(*fmt++ - '0');
        const char *body = tmp;
        size_t len;
        char sign = 0;
        switch (*fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            my_utoa(v < 0 ? 0u - (unsigned)v : (unsigned)v, tmp, 10);
            if (v < 0) sign = '-';
            len = my_strlen(tmp);
            break;
        }
        case 'u': my_utoa(va_arg(ap, unsigned), tmp, 10); len = my_strlen(tmp); break;
        case 'x':
        case 'X':
            my_utoa(va_arg(ap, unsigned), tmp, 16);
            len = my_strlen(tmp);
            if (*fmt == 'X')
                for (size_t i = 0; i < len; i++)
                    if (tmp[i] >= 'a') tmp[i] = (char)(tmp[i] - 'a' + 'A');
            break;
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; zero = false; break;
        case 's':
            body = va_arg(ap, const char *);
            if (body == NULL) body = "(null)";
            len = my_strlen(body);
            zero = false;
            break;
        case '%': out(ctx, '%'); count++; continue;
        case '\0': out(ctx, '%'); count++; fmt--; continue;
        default:
            for (; spec <= fmt; spec++) { out(ctx, *spec); count++; }
            continue;
        }
        size_t total = len + (sign != 0);
        size_t pad = width > total ? width - total : 0;
        if (!left && !zero) emit_n(out, ctx, ' ', pad, &count);
        if (sign) emit_n(out, ctx, sign, 1, &count);
        if (!left && zero) emit_n(out, ctx, '0', pad, &count);
        for (size_t i = 0; i < len; i++) out(ctx, body[i]);
        count += len;
        if (left) emit_n(out, ctx, ' ', pad, &count);
    }
    return count > INT_MAX ? -1 : (int)count;
}
