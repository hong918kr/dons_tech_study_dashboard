#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
static void *bg(void *a) { (void)a; usleep(50000); printf("bg done\n"); return NULL; }
int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, bg, NULL);
    pthread_detach(t);                 /* join 하지 않겠다고 선언 */
    usleep(150000);                    /* 대신 이렇게 '기다리는 척' 해야 한다 */
    printf("main done\n");
    return 0;
}
