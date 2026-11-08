/* C-A04 demo: static 상태 strtok 을 두 스레드가 동시에 쓰면? (관찰용, 체크 테스트 아님)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_strtok_race.c -o race && ./race static && ./race tls
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_strtok_race.c -o race_tsan && ./race_tsan static
 * 각 스레드는 자기 줄 "T<id>a,T<id>b,T<id>c,T<id>d" 를 ITER 번 자르고, 기대와 다른 토큰 수를 센다. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int is_delim(char c, const char *d) { for (; *d; d++) if (*d == c) return 1; return 0; }
static char *my_strtok_r(char *s, const char *d, char **save) {
    if (s == NULL) s = *save;
    if (s == NULL) return NULL;
    while (*s && is_delim(*s, d)) s++;
    if (*s == '\0') { *save = s; return NULL; }
    char *tok = s;
    while (*s && !is_delim(*s, d)) s++;
    if (*s) { *s = '\0'; *save = s + 1; } else *save = s;
    return tok;
}
static char *tok_static(char *s, const char *d) { static char *last; return my_strtok_r(s, d, &last); }
static char *tok_tls(char *s, const char *d) { static _Thread_local char *last; return my_strtok_r(s, d, &last); }

#define NTHR 4
#define ITER 200000
static char *(*g_tok)(char *, const char *);
typedef struct { int id; long bad_runs; long foreign; } arg_t;

static void *worker(void *p) {
    arg_t *a = p;
    for (int it = 0; it < ITER; it++) {
        char line[64];
        snprintf(line, sizeof line, "T%da,T%db,T%dc,T%dd", a->id, a->id, a->id, a->id);
        int k = 0, ok = 1;
        for (char *t = g_tok(line, ","); t; t = g_tok(NULL, ",")) {
            if (t[0] != 'T' || t[1] != (char)('0' + a->id)) a->foreign++;   /* 남의 줄 토큰을 받았다 */
            if (k >= 4 || t[2] != (char)('a' + k)) ok = 0;
            k++;
        }
        if (!ok || k != 4) a->bad_runs++;
    }
    return NULL;
}

int main(int argc, char **argv) {
    int tls = argc > 1 && strcmp(argv[1], "tls") == 0;
    g_tok = tls ? tok_tls : tok_static;
    pthread_t th[NTHR];
    arg_t a[NTHR];
    for (int i = 0; i < NTHR; i++) { a[i] = (arg_t){ i, 0, 0 }; pthread_create(&th[i], NULL, worker, &a[i]); }
    long bad = 0, foreign = 0;
    for (int i = 0; i < NTHR; i++) { pthread_join(th[i], NULL); bad += a[i].bad_runs; foreign += a[i].foreign; }
    printf("%-6s: %d threads x %d lines: wrong lines=%ld, tokens from another thread's line=%ld\n",
           tls ? "tls" : "static", NTHR, ITER, bad, foreign);
    return 0;
}
