/* argbug.c — passing &i (the loop variable) to every thread */
#include <pthread.h>
#include <stdio.h>

static void *worker(void *arg) {
    int id = *(int *)arg;              /* 언제 읽느냐에 따라 값이 달라진다 */
    printf("worker sees id=%d\n", id);
    return NULL;
}

int main(void) {
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, worker, &i);   /* BUG: 전부 같은 주소 */
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    return 0;
}
