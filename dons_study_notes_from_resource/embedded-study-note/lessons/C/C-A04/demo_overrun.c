/* C-A04 demo: 흔한 strtok_r 구현 버그 — 마지막 토큰 뒤에서 save 를 NUL "너머" 로 옮긴다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_overrun.c -o overrun && ./overrun
 *   cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overrun.c -o overrun_asan && ./overrun_asan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_delim(char c, const char *d) { for (; *d; d++) if (*d == c) return 1; return 0; }
static char *bad_strtok_r(char *s, const char *d, char **save) {
    if (s == NULL) s = *save;
    while (*s && is_delim(*s, d)) s++;
    if (*s == '\0') { *save = s; return NULL; }
    char *tok = s;
    while (*s && !is_delim(*s, d)) s++;
    *s = '\0';
    *save = s + 1;                /* BUG: s 가 원래 NUL(문자열 끝)이었어도 한 칸 넘어간다 */
    return tok;
}

int main(void) {
    const char *src = "ON,OFF";
    char *line = malloc(strlen(src) + 1);       /* 딱 맞는 크기 — 실제 RX 버퍼처럼 */
    if (!line) return 1;
    memcpy(line, src, strlen(src) + 1);
    char *sv;
    for (char *t = bad_strtok_r(line, ",", &sv); t; t = bad_strtok_r(NULL, ",", &sv))
        printf("token '%s'\n", t);
    free(line);
    return 0;
}
