/* OS-B04 demo_argbug.c — 고전 버그: 루프 변수의 주소(&i)를 스레드 인자로 넘기기
 * build: cc -std=c11 -O2 -pthread demo_argbug.c -o argbug && ./argbug
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_argbug.c -o argbug_t && ./argbug_t
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>

#define NT 8
static int seen[NT + 1];

static void *worker(void *arg) {
    int id = *(int *)arg;          /* 이 순간의 i — main 이 이미 ++ 했을 수 있다 */
    if (id >= 0 && id <= NT) seen[id]++;   /* (seen 자체도 보호 없음: 데모 전용) */
    return NULL;
}

int main(void) {
    pthread_t t[NT];
    for (int i = 0; i < NT; i++)
        pthread_create(&t[i], NULL, worker, &i);   /* BUG: 모든 스레드가 같은 i 를 가리킨다 */
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    printf("id seen count:");
    for (int i = 0; i <= NT; i++) printf(" [%d]=%d", i, seen[i]);
    printf("\n");
    return 0;
}
