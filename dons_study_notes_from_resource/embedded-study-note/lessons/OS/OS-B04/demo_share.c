/* OS-B04 demo_share.c — 스레드는 주소 공간(전역·힙·코드, 그리고 서로의 스택까지)을 공유한다.
 * build: cc -std=c11 -O2 -pthread demo_share.c -o share && ./share
 */
#define _DARWIN_C_SOURCE            /* pthread_get_stacksize_np (Darwin 전용) */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int g_global = 42;           /* __DATA: 모두 같은 주소 */
static int *g_heap;                 /* malloc: 모두 같은 힙 */

typedef struct { int id; int *peer_local; } arg_t;

/* 세 스레드가 "동시에 살아 있게" 하는 간단한 출발선 (macOS 에는 pthread_barrier_t 가 없다) */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_arrived;
static void gate(int n) {
    pthread_mutex_lock(&g_mu);
    if (++g_arrived == n) pthread_cond_broadcast(&g_cv);
    while (g_arrived < n) pthread_cond_wait(&g_cv, &g_mu);   /* while: OS-B06 */
    pthread_mutex_unlock(&g_mu);
}

static void *worker(void *p) {
    arg_t *a = p;
    int local = a->id * 100;        /* 각 스레드의 자기 스택 */
    gate(3);                        /* 셋 다 살아 있는 상태에서 주소를 찍는다 */
    pthread_mutex_lock(&g_mu);      /* 출력 한 줄씩 */
    printf("T%d: &g_global=%p  g_heap=%p  &local=%p  stack=%zu KiB\n", a->id, (void *)&g_global,
           (void *)g_heap, (void *)&local, pthread_get_stacksize_np(pthread_self()) / 1024);
    pthread_mutex_unlock(&g_mu);
    if (a->id == 1) *a->peer_local = 7777;   /* main 스택의 변수를 포인터로 고친다 — 보호 없음 */
    g_heap[a->id] = local;
    return NULL;
}

int main(void) {
    int main_local = 1;
    g_heap = calloc(4, sizeof *g_heap);
    printf("main: &g_global=%p  g_heap=%p  &main_local=%p  stack=%zu KiB\n", (void *)&g_global,
           (void *)g_heap, (void *)&main_local, pthread_get_stacksize_np(pthread_self()) / 1024);
    pthread_t t[3];
    arg_t a[3];
    for (int i = 0; i < 3; i++) {
        a[i] = (arg_t){ .id = i + 1, .peer_local = &main_local };
        pthread_create(&t[i], NULL, worker, &a[i]);
    }
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);   /* join 뒤에는 스레드의 쓰기가 보인다 */
    printf("main: main_local=%d (T1 이 고침)  g_heap = {%d, %d, %d, %d}\n", main_local, g_heap[0],
           g_heap[1], g_heap[2], g_heap[3]);
    free(g_heap);
    return 0;
}
