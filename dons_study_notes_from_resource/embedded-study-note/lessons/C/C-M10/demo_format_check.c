/* C-M10 demo — __attribute__((format(printf, ...))) 가 잡아 주는 실수들 (경고를 보려는 파일, -Werror 없이)
 *   cc -std=c11 -Wall -c demo_format_check.c -o /dev/null
 */
#include <stdarg.h>
#include <stdint.h>

int log_unchecked(char lvl, const char *fmt, ...);                                     /* 검사 없음 */
int log_checked(char lvl, const char *fmt, ...) __attribute__((format(printf, 2, 3)));  /* 검사 */
int log_vchecked(char lvl, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0)));

void use(uint64_t up_ms, const char *name, double volts) {
    log_unchecked('I', "uptime=%u name=%d v=%d", up_ms, name, volts);   /* 조용히 통과 → 런타임 쓰레기 */

    log_checked('I', "uptime=%u", up_ms);       /* uint64_t 에 %u              */
    log_checked('I', "name=%d", name);          /* 포인터에 %d                 */
    log_checked('I', "v=%d", volts);            /* double 에 %d                */
    log_checked('I', "a=%d b=%d", 1);           /* 인자 부족                    */
    log_checked('I', "%s");                     /* 인자 부족 (스택 쓰레기를 포인터로) */
    log_checked('I', "x=%d", 1, 2);             /* 인자 남음                    */
}

/* va_list 를 받는 쪽: va_arg 의 타입은 "승격된" 타입이어야 한다 */
int take_char(int n, ...) {
    va_list ap;
    va_start(ap, n);
    char c = va_arg(ap, char);                  /* char 는 int 로 승격되어 넘어온다 → UB */
    va_end(ap);
    return c;
}

/* 내 v-함수를 부르는 래퍼에도 속성을 달아야 호출부가 검사된다 */
int log_wrapper(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = log_vchecked('I', fmt, ap);         /* 래퍼에 속성이 없으면 -Wformat-nonliteral 이 경고 */
    va_end(ap);
    return r;
}
