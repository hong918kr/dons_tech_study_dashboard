#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static int ready;                 /* volatile도 atomic도 아님 */

static void *setter(void *a) { (void)a; usleep(200000); ready = 1; return NULL; }

int main(void) {
    pthread_t t;
    alarm(3);                     /* 3초 뒤 강제 종료 */
    pthread_create(&t, NULL, setter, NULL);
    while (!ready) { }            /* 기다린다 */
    printf("탈출했다\n");
    pthread_join(t, NULL);
    return 0;
}
