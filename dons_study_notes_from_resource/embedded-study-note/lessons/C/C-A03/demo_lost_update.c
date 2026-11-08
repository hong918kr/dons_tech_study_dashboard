/* C-A03 demo_lost_update.c — 원자 load + 원자 store 는 원자 RMW 가 아니다. 그리고 TSan 은 이걸 못 잡는다.
 * build: cc -std=c11 -O2 -pthread demo_lost_update.c -o dlu && ./dlu
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_lost_update.c -o dlu_t && ./dlu_t
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define NT 4
#define K 1000000
static atomic_uint c_ls, c_fa;
static void *w(void *arg) {
    (void)arg;
    for (int i = 0; i < K; i++) {
        unsigned v = atomic_load_explicit(&c_ls, memory_order_relaxed);   /* 읽고 */
        atomic_store_explicit(&c_ls, v + 1u, memory_order_relaxed);       /* 쓴다: 사이가 틈 */
        atomic_fetch_add_explicit(&c_fa, 1u, memory_order_relaxed);       /* RMW 한 번 */
    }
    return NULL;
}
int main(void) {
    pthread_t t[NT];
    for (int i = 0; i < NT; i++) pthread_create(&t[i], NULL, w, NULL);
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    printf("expect %u : load+store=%u  fetch_add=%u\n", (unsigned)(NT * K),
           atomic_load(&c_ls), atomic_load(&c_fa));
    return 0;
}
