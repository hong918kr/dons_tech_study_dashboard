/* C-M10 itoa와 미니 printf: UART 로깅용
 *
 * Part A: my_utoa / my_itoa  — 정수 → 문자열 (base 2..36, INT_MIN 포함)
 * Part B: mini_vformat (출력 콜백 기반 코어) + mini_vsnprintf / mini_snprintf
 *         %d %u %x %X %c %s %%, 플래그 '-' '0', 폭. snprintf 와 같은 잘림(truncation) 규칙.
 *         테스트는 libc vsnprintf 와 표 + 결정적 퍼즈(xorshift32, 고정 시드)로 대조한다.
 * Part C: log_printf → log_vprintf(va_list) → 스택 줄 버퍼 → 임계 구역 안에서 TX 링 버퍼에 한 줄씩.
 *         링 버퍼의 n번째 put 직후 "인터럽트"(ISR 도 로그를 찍는다)를 끼워 넣어 줄 섞임을 검사한다.
 */
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 컴파일러가 printf 처럼 인자 타입을 검사하게 한다 (fmt 인자 번호, 첫 가변 인자 번호; va_list 판은 0) */
#define PRINTF_LIKE(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))

/* 32비트 int 기준: 부호 1 + 2진수 32자리 + NUL 1 = 34 바이트면 어떤 base 도 들어간다 */
#define ITOA_BUFSZ (sizeof(int) * CHAR_BIT + 2)

typedef void (*putc_fn)(void *ctx, char c);   /* 출력 1글자 콜백: 버퍼, UART, 링 버퍼 ... */

static int mini_vformat(putc_fn out, void *ctx, const char *fmt, va_list ap) PRINTF_LIKE(3, 0);
static int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) PRINTF_LIKE(3, 0);
static int mini_snprintf(char *buf, size_t size, const char *fmt, ...) PRINTF_LIKE(3, 4);
static int log_vprintf(char lvl, const char *fmt, va_list ap) PRINTF_LIKE(2, 0);
static int log_printf(char lvl, const char *fmt, ...) PRINTF_LIKE(2, 3);

/* ---------------- Part A: itoa ---------------- */
/* value 를 base(2..36) 진법 문자열로 buf 에 쓴다 (숫자 0-9, 소문자 a-z). 반환: buf.
 * base 가 범위 밖이면 buf[0] = '\0' 으로 만들고 NULL. buf 는 ITOA_BUFSZ 바이트 이상. */
static char *my_utoa(unsigned value, char *buf, int base) {
    (void)value; (void)base;
    buf[0] = '\0';
    return buf; /* TODO */
}
/* 부호 있는 버전: 음수면 '-' + 크기(magnitude). 어떤 base 에서도 같은 규칙.
 * INT_MIN 도 정확해야 한다 — -value / abs(value) 는 INT_MIN 에서 오버플로(UB). */
static char *my_itoa(int value, char *buf, int base) {
    (void)value; (void)base;
    buf[0] = '\0';
    return buf; /* TODO: INT_MIN 주의 */
}

/* ---------------- Part B: 미니 printf ---------------- */
static void emit_n(putc_fn out, void *ctx, char c, size_t n, size_t *count) {
    for (size_t i = 0; i < n; i++) out(ctx, c);
    *count += n;
}
/* 지원: %d %u %x %X %c %s %%, 플래그 '-' '0' (여러 개, 아무 순서), 폭(10진 숫자).
 * 정밀도(.N)·길이 수식어(l, h)는 없다. 모르는 변환은 '%...' 를 글자 그대로 내보낸다.
 * 반환: 내보낸 글자 수 (INT_MAX 초과면 -1). */
static int mini_vformat(putc_fn out, void *ctx, const char *fmt, va_list ap) {
    (void)out; (void)ctx; (void)fmt; (void)ap; (void)emit_n;
    return 0; /* TODO: 플래그 → 폭 → 변환 → 패딩 */
}

/* snprintf 용 출력 대상: 버퍼에 들어가는 만큼만 쓰고, 길이는 끝까지 센다 */
typedef struct { char *buf; size_t size; size_t len; } bufsink_t;
static void bufsink_putc(void *ctx, char c) {
    (void)ctx; (void)c; /* TODO: 들어가는 만큼만 쓰고, 길이는 끝까지 센다 */
}
/* C11 7.21.6.12 vsnprintf 와 같은 규칙:
 *  - size > 0 이면 항상 NUL 종료 (최대 size-1 글자)   - size == 0 이면 아무것도 안 쓴다 (buf 는 NULL 가능)
 *  - 반환값 = 버퍼가 충분했다면 썼을 글자 수 (NUL 제외) → 반환값 >= size 이면 잘린 것 */
static int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    (void)buf; (void)size; (void)fmt; (void)ap; (void)bufsink_putc;
    return 0; /* TODO */
}
static int mini_snprintf(char *buf, size_t size, const char *fmt, ...) {
    (void)buf; (void)size; (void)fmt;
    return 0; /* TODO: va_start → mini_vsnprintf → va_end */
}

/* ---------------- Part C: UART 로그 (시뮬레이터 — 고치지 않는다) ---------------- */
#define RING_SZ      64u    /* TX 링 버퍼 (2의 거듭제곱) */
#define LOG_LINE_MAX 32u    /* 링에 들어가는 한 줄 최대 바이트: "L: " + 메시지 + "\r\n" */
static char     ring[RING_SZ];
static size_t   ring_head, ring_tail;          /* 단조 증가 인덱스: 사용량 = head - tail */
static unsigned g_log_dropped;                 /* 링이 꽉 차서 버린 줄(또는 글자) 수 */
static int      g_put_n, g_irq_at = -1;        /* g_irq_at 번째 ring_put 직후 인터럽트 */
static bool     g_masked, g_pending;
static unsigned g_isr_n;
static char     g_wire[512];                   /* UART TX 핀으로 나간 바이트 (uart_flush) */
static size_t   g_wire_n;

static void sim_isr(void) { log_printf('E', "isr%u", ++g_isr_n); }   /* ISR 도 로그를 찍는다 */
static size_t ring_free(void) { return RING_SZ - (ring_head - ring_tail); }
static void ring_put(char c) {
    ring[ring_head++ % RING_SZ] = c;
    if (++g_put_n == g_irq_at) {
        g_irq_at = -1;
        if (g_masked) g_pending = true;        /* 마스크 중이면 펜딩만 */
        else          sim_isr();
    }
}
/* Cortex-M 의 PRIMASK 저장/복원 흉내: prev = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(prev) */
static bool irq_save(void) { bool prev = g_masked; g_masked = true; return prev; }
static void irq_restore(bool prev) {
    g_masked = prev;
    if (!g_masked && g_pending) { g_pending = false; sim_isr(); }   /* 미뤄진 인터럽트 */
}
static void uart_flush(void) {                 /* TX 완료 ISR 이 링을 비우는 것과 같다 */
    while (ring_tail != ring_head && g_wire_n + 1 < sizeof g_wire)
        g_wire[g_wire_n++] = ring[ring_tail++ % RING_SZ];
    g_wire[g_wire_n] = '\0';
}
static void log_reset(int irq_at) {
    ring_head = ring_tail = 0; g_log_dropped = 0;
    g_put_n = 0; g_irq_at = irq_at; g_masked = g_pending = false; g_isr_n = 0;
    g_wire_n = 0; g_wire[0] = '\0';
}
/* (주어진 코드) 포맷하면서 글자마다 바로 링에 넣는 "스트리밍" 로거 — 임계 구역이 없다 */
static void ring_sink(void *ctx, char c) { (void)ctx; if (ring_free() > 0) ring_put(c); else g_log_dropped++; }
static int log_printf_stream(char lvl, const char *fmt, ...) PRINTF_LIKE(2, 3);
static int log_printf_stream(char lvl, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ring_sink(NULL, lvl); ring_sink(NULL, ':'); ring_sink(NULL, ' ');
    int n = mini_vformat(ring_sink, NULL, fmt, ap);
    va_end(ap);
    ring_sink(NULL, '\r'); ring_sink(NULL, '\n');
    return n + 5;
}

/* ---------------- Part C: 줄 단위 로거 ---------------- */
/* 한 줄 = "<lvl>: " + 메시지 + "\r\n", 최대 LOG_LINE_MAX 바이트.
 * 메시지가 길면 잘라서 마지막 글자를 '~' 로 표시. 줄 전체가 링에 들어갈 자리가 없으면
 * 통째로 버리고 g_log_dropped++ (부분 줄을 남기지 않는다). ISR 이 끼어들어도 줄이 섞이면 안 된다.
 * 반환: 링에 넣은 바이트 수, 버렸으면 -1. */
static int log_vprintf(char lvl, const char *fmt, va_list ap) {
    (void)lvl; (void)fmt; (void)ap; (void)irq_save; (void)irq_restore;
    return -1; /* TODO */
}
static int log_printf(char lvl, const char *fmt, ...) {
    (void)lvl; (void)fmt; (void)log_vprintf;
    return -1; /* TODO: va_list 를 log_vprintf 로 넘긴다 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEEu;             /* xorshift32, 고정 시드 → 매번 같은 케이스 */
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* libc vsnprintf 와 mini_vsnprintf 를 같은 인자로 돌려 반환값 + 버퍼 64바이트 전체를 비교.
 * 버퍼는 0xAA 로 채워 두므로 size 밖에 1바이트라도 쓰면 들킨다. va_list 를 두 번 쓰려면 va_copy. */
static int g_shown;
static void show(const char *p, size_t n) {     /* 실패 출력용: 못 찍는 바이트(0xAA 센티널 등)는 '.' */
    putchar('"');
    for (size_t i = 0; i < n && p[i] != '\0'; i++) putchar(p[i] >= ' ' && p[i] <= '~' ? p[i] : '.');
    putchar('"');
}
static int cmp_libc(size_t size, const char *fmt, ...) PRINTF_LIKE(2, 3);
static int cmp_libc(size_t size, const char *fmt, ...) {
    char a[64], b[64];
    memset(a, 0xAA, sizeof a);
    memset(b, 0xAA, sizeof b);
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int ra = vsnprintf(size ? a : NULL, size, fmt, ap);
    int rb = mini_vsnprintf(size ? b : NULL, size, fmt, ap2);
    va_end(ap2);
    va_end(ap);
    int same = ra == rb && memcmp(a, b, sizeof a) == 0;
    if (!same && g_shown++ < 5) {
        printf("  mismatch size=%zu fmt=\"%s\": libc %d ", size, fmt, ra);
        show(a, size);
        printf("  mini %d ", rb);
        show(b, size);
        putchar('\n');
    }
    return same;
}
static int pick_int(void) {
    static const int edge[] = { 0, 1, -1, 9, -10, 42, INT_MAX, INT_MIN, INT_MIN + 1, 100000 };
    uint32_t r = rnd();
    switch (r % 4) {
    case 0:  return edge[(r >> 8) % 10];
    case 1:  return (int)(rnd() % 2001) - 1000;
    default: { uint32_t u = rnd(); int v; memcpy(&v, &u, sizeof v); return v; }   /* 전 범위 */
    }
}

int main(void) {
    char buf[ITOA_BUFSZ], ref[ITOA_BUFSZ + 8];

    /* ===== Part A: itoa ===== */
    CHECK(my_itoa(0, buf, 10) == buf && strcmp(buf, "0") == 0);
    CHECK(strcmp(my_itoa(-1, buf, 10), "-1") == 0);
    CHECK(strcmp(my_itoa(INT_MIN, buf, 10), "-2147483648") == 0);
    CHECK(strcmp(my_itoa(INT_MAX, buf, 10), "2147483647") == 0);
    CHECK(strcmp(my_itoa(INT_MIN, buf, 16), "-80000000") == 0);
    CHECK(strcmp(my_itoa(INT_MIN, buf, 2), "-10000000000000000000000000000000") == 0);
    CHECK(strcmp(my_itoa(INT_MAX, buf, 36), "zik0zj") == 0);
    CHECK(strcmp(my_itoa(-255, buf, 16), "-ff") == 0);
    CHECK(strcmp(my_itoa(35, buf, 36), "z") == 0);
    CHECK(strcmp(my_utoa(UINT_MAX, buf, 16), "ffffffff") == 0);
    CHECK(strcmp(my_utoa(UINT_MAX, buf, 2), "11111111111111111111111111111111") == 0);
    CHECK(strcmp(my_utoa(0x80000000u, buf, 36), "zik0zk") == 0);
    strcpy(buf, "junk");
    CHECK(my_itoa(5, buf, 1) == NULL && buf[0] == '\0');      /* base 범위 밖 */
    CHECK(my_utoa(5, buf, 37) == NULL && buf[0] == '\0');
    {   /* 퍼즈 1: base 10/16/8 은 libc snprintf 와 문자열 비교 */
        int bad = 0;
        for (int i = 0; i < 3000; i++) {
            int v = pick_int();
            unsigned mag = v < 0 ? 0u - (unsigned)v : (unsigned)v;
            snprintf(ref, sizeof ref, "%d", v);
            if (strcmp(my_itoa(v, buf, 10), ref) != 0) bad++;
            snprintf(ref, sizeof ref, "%s%x", v < 0 ? "-" : "", mag);
            if (strcmp(my_itoa(v, buf, 16), ref) != 0) bad++;
            snprintf(ref, sizeof ref, "%s%o", v < 0 ? "-" : "", mag);
            if (strcmp(my_itoa(v, buf, 8), ref) != 0) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 퍼즈 2: 모든 base 2..36 에서 libc strtol 로 되읽으면 원래 값 (long 은 64비트) */
        int bad = 0;
        for (int i = 0; i < 2000; i++) {
            int v = pick_int();
            for (int base = 2; base <= 36; base++) {
                char *end;
                if (my_itoa(v, buf, base) == NULL) { bad++; continue; }
                long back = strtol(buf, &end, base);
                if (back != v || *end != '\0' || strlen(buf) >= ITOA_BUFSZ) bad++;
            }
        }
        CHECK(bad == 0);
    }

    /* ===== Part B: 표 기반 libc 대조 ===== */
    CHECK(cmp_libc(32, "%d|%d|%d", 0, -1, 42));
    CHECK(cmp_libc(32, "%d %d", INT_MIN, INT_MAX));
    CHECK(cmp_libc(32, "%u %x %X", UINT_MAX, 0xdeadbeefu, 0xdeadbeefu));
    CHECK(cmp_libc(32, "%c%c%c", 'a', 'b', 'c'));
    CHECK(cmp_libc(32, "[%s|%s]", "", "abc"));
    CHECK(cmp_libc(32, "100%% done"));
    CHECK(cmp_libc(32, "[%5d][%-5d][%05d]", 42, 42, 42));
    CHECK(cmp_libc(32, "[%05d][%5d]", -42, -42));               /* 부호는 0 앞에 */
    const char *f_mz = "[%-05d]";                   /* '-' 가 '0' 을 이긴다 (리터럴이면 -Wformat 경고) */
    CHECK(cmp_libc(32, f_mz, -42));
    CHECK(cmp_libc(32, "[%08x][%-8X][%2u]", 0xbeefu, 0xbeefu, 12345u));  /* 폭보다 길면 안 자른다 */
    CHECK(cmp_libc(32, "[%10s][%-10s]", "hi", "hi"));
    CHECK(cmp_libc(32, "[%5c][%-3c]", 'z', 'z'));
    CHECK(cmp_libc(32, "[%011d]", INT_MIN));
    /* 잘림 · size 경계 */
    CHECK(cmp_libc(8, "%s", "abcdefghij"));        /* 10 반환, "abcdefg" + NUL */
    CHECK(cmp_libc(1, "%d", 123));                 /* NUL 만 */
    CHECK(cmp_libc(0, "%d", 123));                 /* 아무것도 안 씀 (NULL 버퍼), 3 반환 */
    CHECK(cmp_libc(5, "%05d", -7));                /* "-000" + NUL, 5 반환 */
    CHECK(cmp_libc(4, "a%cb", 0));                 /* %c 의 '\0' 도 한 글자: "a\0b\0" */
    CHECK(cmp_libc(6, "%-8d|", 7));
    {   /* mini 전용 동작 (표준상 UB 라 libc 와 비교하지 않음) + 직접 호출 */
        char s[16];
        CHECK(mini_snprintf(s, sizeof s, "%s", (const char *)NULL) == 6 && strcmp(s, "(null)") == 0);
        CHECK(mini_snprintf(s, 4, "%d", 12345) == 5 && strcmp(s, "123") == 0);
        CHECK(mini_snprintf(NULL, 0, "%s-%d", "ab", 7) == 4);   /* 크기 재기 (2-pass) */
        const char *odd = "%q|%";                   /* 모르는 변환, 끝의 '%' (리터럴이면 -Wformat 오류) */
        int m = mini_snprintf(s, sizeof s, odd, 0);
        CHECK(m == 4 && strcmp(s, "%q|%") == 0);
    }
    {   /* 퍼즈 3: 무작위 플래그/폭/변환/size 3000 케이스 */
        static const char *strs[] = { "", "a", "hello", "firmware-v1.2.3", "0123456789abcdef" };
        const char convs[] = "duxXcs";
        int bad = 0;
        for (int i = 0; i < 3000; i++) {
            char f[24];
            size_t k = 0;
            char conv = convs[rnd() % 6];
            f[k++] = '<'; f[k++] = '%';
            uint32_t fl = rnd() % 4;
            if (fl & 1) f[k++] = '-';
            if ((fl & 2) && conv != 'c' && conv != 's') f[k++] = '0';   /* %0s/%0c 는 UB */
            unsigned w = rnd() % 14;
            if (w) k += (size_t)snprintf(f + k, sizeof f - k, "%u", w);
            f[k++] = conv; f[k++] = '>'; f[k] = '\0';
            size_t size = rnd() % 41;
            int ok;
            switch (conv) {
            case 'd': ok = cmp_libc(size, f, pick_int()); break;
            case 'c': ok = cmp_libc(size, f, (int)(' ' + rnd() % 95)); break;
            case 's': ok = cmp_libc(size, f, strs[rnd() % 5]); break;
            default:  ok = cmp_libc(size, f, (unsigned)pick_int()); break;
            }
            if (!ok) bad++;
        }
        CHECK(bad == 0);
    }

    /* ===== Part C: UART 로그 ===== */
    log_reset(-1);
    CHECK(log_printf('I', "temp=%d", 25) == 12);
    uart_flush();
    CHECK(strcmp(g_wire, "I: temp=25\r\n") == 0);
    log_reset(-1);
    CHECK(log_printf('D', "%c%c %04x %-3u|", 'o', 'k', 0xbeefu, 7u) == 17);   /* va_list 로 여러 타입 전달 */
    uart_flush();
    CHECK(strcmp(g_wire, "D: ok beef 7  |\r\n") == 0);
    log_reset(-1);
    CHECK(log_printf('W', "%s", "abcdefghijklmnopqrstuvwxyz0123456789") == 32);   /* 잘림 */
    uart_flush();
    CHECK(strcmp(g_wire, "W: abcdefghijklmnopqrstuvwxyz~\r\n") == 0);
    log_reset(-1);                                  /* 링 64B = 32B 줄 2개 → 3번째는 통째로 버림 */
    CHECK(log_printf('A', "%027d", 1) == 32 && log_printf('B', "%027d", 2) == 32);
    CHECK(log_printf('C', "x") == -1 && g_log_dropped == 1);
    uart_flush();
    CHECK(g_wire_n == 64 && g_wire[0] == 'A' && g_wire[32] == 'B' && g_wire[63] == '\n');
    CHECK(log_printf('C', "x") == 6);               /* 비운 뒤엔 다시 들어간다 */
    /* 인터럽트가 줄 중간(5번째 put 직후)에 들어오고, ISR 도 로그를 찍는다 */
    log_reset(5);
    log_printf_stream('I', "temp=%d", 25);
    uart_flush();
    CHECK(strcmp(g_wire, "I: teE: isr1\r\nmp=25\r\n") == 0);   /* 스트리밍: 줄이 섞인다 (재현) */
    log_reset(5);
    CHECK(log_printf('I', "temp=%d", 25) == 12);
    uart_flush();
    CHECK(strcmp(g_wire, "I: temp=25\r\nE: isr1\r\n") == 0);   /* 줄 단위 + 임계 구역: 안 섞인다 */
    CHECK(g_isr_n == 1 && !g_masked);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
