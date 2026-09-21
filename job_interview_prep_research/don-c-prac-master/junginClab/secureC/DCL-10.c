#if 0
// 위험한 코드
#include <stdio.h>
#include <windows.h>
#include <signal.h>

int total;

int main() {
    int i = 0;
    for (i = 0; i < 10; i++)
        total++;
    printf("%d\n", total);

    return 0;
}
#endif



#if 0
// 위험한 코드
#include <stdio.h>
#include <windows.h>
#include <signal.h>

int flag;

void handler(int signum) {
    printf("\thandler\n");
    flag = 0;
}

int main() {
    flag = 1;
    signal(SIGINT, handler);

    while (flag) {
        printf("do something\n");
        Sleep(1000);
    }

    return 0;
}
#endif

#if 1
// 해결 방법
#include <stdio.h>
#include <windows.h>
#include <signal.h>

volatile int flag;

void handler(int signum) {
    printf("\thandler\n");
    flag = 0;
}

int main() {
    flag = 1;
    signal(SIGINT, handler);

    while (flag) {
        printf("do something\n");
        Sleep(1000);
    }

    return 0;
}
#endif

