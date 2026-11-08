/* C-B07 데모: strncpy 는 src 길이 >= n 이면 NUL 을 안 붙인다 → 다음 strlen 이 버퍼 밖을 읽는다
 *   cc -std=c11 -g -O0 -fsanitize=address demo_strncpy_nonul.c -o demo_nonul && ./demo_nonul
 */
#include <stdio.h>
#include <string.h>

static char *my_strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int main(void) {
    char name[8];
    my_strncpy(name, "sensor-1", sizeof name);   /* 정확히 8글자 → NUL 없음 */
    printf("len = %zu\n", strlen(name));          /* BUG: 종료 NUL 을 찾아 name[8] 이후를 읽음 */
    return 0;
}
