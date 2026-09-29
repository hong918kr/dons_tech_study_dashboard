#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#define NB 8u
#define NTYPES 3
#define BUCKET_US 100000u
static pthread_mutex_t L = PTHREAD_MUTEX_INITIALIZER;
static unsigned n[NB][NTYPES];
static unsigned cur;
static void *cleaner(void *a) {            /* 1초마다 깨어나 다음 칸을 비운다 */
    (void)a;
    for (;;) { usleep(BUCKET_US); pthread_mutex_lock(&L);
               cur = (cur + 1) % NB; memset(n[cur], 0, sizeof n[0]);
               pthread_mutex_unlock(&L); }
}
int main(void){ (void)cleaner; return 0; }
