#if 0
// 위험한 코드
#include <stdio.h>
#define BUF_SIZE    (10)

int main() {
    int buf[BUF_SIZE];

    int* cur = buf;
    // buf+40 => (char*)buf + sizeof(*buf)*40
    while (cur < (buf + sizeof(buf)))
        *cur++ = 0;

    for (int i = 0; i < BUF_SIZE; i++)
        printf("buf[%d] = %d\n", i, buf[i]);

    return 0;
}
#endif

#if 0
// 해결 방법 1.
#include <stdio.h>
#define BUF_SIZE    (10)

int main() {
    int buf[BUF_SIZE];

    int* cur = buf;
    while (cur < ((char*)buf + sizeof(buf)))
        *cur++ = 0;

    for (int i = 0; i < BUF_SIZE; i++)
        printf("buf[%d] = %d\n", i, buf[i]);

    return 0;
}
#endif

#if 1
// 해결 방법 2.
#include <stdio.h>
#define BUF_SIZE    (10)

int main() {
    int buf[BUF_SIZE];

    // buf[i] => *(buf+i) => *((int*)((char*)buff+sizeof(*buff)))
    for (int i = 0; i < BUF_SIZE; i++)
        buf[i] = 0;

    for (int i = 0; i < BUF_SIZE; i++)
        printf("buf[%d] = %d\n", i, buf[i]);

    return 0;
}
#endif