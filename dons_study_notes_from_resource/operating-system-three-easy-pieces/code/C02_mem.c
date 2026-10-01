// C02_mem.c — OSTEP Figure 2.3 (mem.c) 변형.
// malloc 한 뒤 fork() 해서 부모/자식이 "같은 가상 주소"를 각자 증가시킨다.
// 같은 주소인데 값이 따로 논다 = 각 프로세스가 자기만의 가상 주소 공간을 가진다.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int *p = malloc(sizeof(int));                       // a1
    assert(p != NULL);
    *p = 0;                                             // a3
    pid_t rc = fork();
    assert(rc >= 0);
    int step = (rc == 0) ? 1 : 100;                     // 자식은 +1, 부모는 +100
    printf("(%d) %s: address of p = %p\n", (int)getpid(),
           rc == 0 ? "child " : "parent", (void *)p);   // a2
    for (int i = 0; i < 3; i++) {
        usleep(100 * 1000);
        *p = *p + step;
        printf("(%d) %s: *p = %d\n", (int)getpid(), rc == 0 ? "child " : "parent", *p); // a4
        fflush(stdout);
    }
    if (rc > 0)
        wait(NULL);
    free(p);
    return 0;
}
