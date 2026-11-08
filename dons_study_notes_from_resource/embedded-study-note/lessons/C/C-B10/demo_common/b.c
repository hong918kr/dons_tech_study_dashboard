/* b.c — 같은 이름을 100바이트 배열로 정의 (역시 tentative) */
#include <stdio.h>
char buf[100];
void a_touch(void);
int main(void) {
    buf[0] = 'Z';
    a_touch();                          /* a.c 가 buf[0..3] 을 덮어쓴다 */
    printf("buf[0]=0x%02X sizeof(buf) here=%zu\n", (unsigned char)buf[0], sizeof buf);
    return 0;
}
