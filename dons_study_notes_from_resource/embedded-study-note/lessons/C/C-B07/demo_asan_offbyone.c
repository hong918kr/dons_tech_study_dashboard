/* C-B07 데모: strcpy off-by-one 을 ASan 이 잡는다 (일부러 버그가 있는 코드)
 *   cc -std=c11 -g -O0 -fsanitize=address demo_asan_offbyone.c -o demo_asan && ./demo_asan heap
 *   ./demo_asan stack
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *my_strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') { }
    return dst;
}

int main(int argc, char **argv) {
    const char *src = "sensor-1";                 /* 8글자 + NUL = 9바이트 */
    if (argc > 1 && strcmp(argv[1], "stack") == 0) {
        char name[8];                             /* BUG: NUL 자리 없음 */
        my_strcpy(name, src);
        printf("stack: %s\n", name);
    } else {
        char *name = malloc(strlen(src));         /* BUG: +1 빠짐 */
        if (!name) return 2;
        my_strcpy(name, src);
        printf("heap: %s\n", name);
        free(name);
    }
    return 0;
}
