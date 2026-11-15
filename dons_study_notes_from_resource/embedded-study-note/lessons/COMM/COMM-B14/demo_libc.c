/* COMM-B14 demo_libc.c — 같은 악성 입력을 atoi / strtol / strtoul 과 이 레슨의 parse_i32 에 넣어 본다.
 * atoi 와 sscanf("%d") 는 값이 int 로 표현 안 되면 UB 라서 그 칸은 호출하지 않고 "UB" 라고 적는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_libc.c -o demo_libc && ./demo_libc
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { NUM_OK = 0, NUM_E_SYNTAX, NUM_E_RANGE } num_status_t;
static int digit_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 99;
}
static num_status_t parse_i32(const char *s, int32_t lo, int32_t hi, int32_t *out) {   /* solution.c 와 같은 것 */
    bool neg = false, big = false;
    uint32_t base = 10, acc = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (*s == '\0') return NUM_E_SYNTAX;
    const uint32_t limit = neg ? 2147483648u : 2147483647u;
    for (; *s; s++) {
        uint32_t d = (uint32_t)digit_val(*s);
        if (d >= base) return NUM_E_SYNTAX;
        if (!big && acc > (limit - d) / base) big = true;
        if (!big) acc = acc * base + d;
    }
    if (big) return NUM_E_RANGE;
    int32_t v = neg ? (acc == 2147483648u ? INT32_MIN : -(int32_t)acc) : (int32_t)acc;
    if (v < lo || v > hi) return NUM_E_RANGE;
    *out = v;
    return NUM_OK;
}

/* 10진 주제 시퀀스가 int 로 표현되나? (아니면 atoi / sscanf("%d") 는 UB 라 부르지 않는다) */
static bool safe_for_atoi(const char *s) {
    char *e; errno = 0; long v = strtol(s, &e, 10);
    return errno != ERANGE && v >= INT_MIN && v <= INT_MAX;
}

int main(void) {
    static const char *const in[] = { "42", "12abc", "", "abc", " 12", "007", "010", "0x1F", "1e3",
                                      "-1", "+-1", "0x", "2147483648", "99999999999", "99999999999999999999" };
    printf("sizeof(long) = %zu (이 맥은 LP64. Cortex-M 은 ILP32 → 4)\n\n", sizeof(long));
    printf("%-22s | %-7s | %-28s | %-14s | %-22s | %-10s | %s\n",
           "input", "atoi", "strtol(s,&e,10) val/rest/errno", "strtol base 0", "strtoul base 0", "sscanf %d", "parse_i32");
    for (size_t i = 0; i < sizeof in / sizeof in[0]; i++) {
        const char *s = in[i];
        char a[16], l10[64], l0[32], ul[40], sc[24], pi[24], q[32];
        if (safe_for_atoi(s)) snprintf(a, sizeof a, "%d", atoi(s)); else snprintf(a, sizeof a, "UB");
        { char *e; errno = 0; long v = strtol(s, &e, 10); int er = errno;
          snprintf(l10, sizeof l10, "%ld / \"%s\" / %s", v, e, er == ERANGE ? "ERANGE" : er ? "EINVAL" : "0"); }
        { char *e; errno = 0; long v = strtol(s, &e, 0);
          snprintf(l0, sizeof l0, "%ld%s", v, *e ? " (+junk)" : ""); }
        { char *e; errno = 0; unsigned long v = strtoul(s, &e, 0);
          snprintf(ul, sizeof ul, "%lu%s", v, errno == ERANGE ? " ERANGE" : ""); }
        if (safe_for_atoi(s)) { int v = 0; int n = sscanf(s, "%d", &v); snprintf(sc, sizeof sc, "n=%d v=%d", n, v); }
        else snprintf(sc, sizeof sc, "UB");
        { int32_t v = 0; num_status_t st = parse_i32(s, INT32_MIN, INT32_MAX, &v);
          snprintf(pi, sizeof pi, "%s", st == NUM_OK ? "OK " : st == NUM_E_RANGE ? "E_RANGE" : "E_SYNTAX");
          if (st == NUM_OK) snprintf(pi + 3, sizeof pi - 3, "%d", (int)v); }
        snprintf(q, sizeof q, "\"%s\"", s);
        printf("%-22s | %-7s | %-28s | %-14s | %-22s | %-10s | %s\n", q, a, l10, l0, ul, sc, pi);
    }
    return 0;
}
