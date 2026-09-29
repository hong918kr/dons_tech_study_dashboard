#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
static void *slow(void *a) {
    (void)a;
    usleep(200000);
    printf("slow: I finished\n");   /* 여기까지 올까? */
    return NULL;
}
int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, slow, NULL);
    printf("main: returning without join\n");
    return 0;                        /* = exit(0) = 프로세스 전체 종료 */
}
