#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
static struct { int head, tail; bool stopping; } g_q;
static _Atomic bool g_running;
static bool g_started;
static pthread_t g_thread;
static void *sampler_main(void *a) { (void)a; while (atomic_load(&g_running)) {} return NULL; }

int evq_init(void) {
    if (g_started) return 0;                     /* 두 번 불려도 안전 */
    memset(&g_q, 0, sizeof g_q);                 /* 1. 상태를 먼저 완성 */
    g_q.stopping = false;
    atomic_store(&g_running, true);               /* 2. 플래그도 먼저 */
    int rc = pthread_create(&g_thread, NULL, sampler_main, NULL);
    if (rc != 0) { atomic_store(&g_running, false); return -1; }  /* 3. 되돌린다 */
    g_started = true;                             /* 4. 이제 join 대상이 있다 */
    return 0;
}
void evq_deinit(void) {
    if (!g_started) return;
    atomic_store(&g_running, false);
    pthread_join(g_thread, NULL);                 /* 기다린다 */
    g_started = false;
}
int main(void) { if (evq_init() != 0) return 1; evq_deinit(); return 0; }
