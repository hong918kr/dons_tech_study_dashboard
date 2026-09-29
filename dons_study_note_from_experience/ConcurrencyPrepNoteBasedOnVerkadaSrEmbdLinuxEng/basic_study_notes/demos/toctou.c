/* toctou.c — every single function is thread-safe, and the program is
 * still wrong: the gap between "check" and "act" is unprotected.      */
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static int depth = 0;                 /* items in the queue */
static long overpops = 0;             /* pops that had nothing to pop */

static int q_size(void) {             /* thread-safe */
    pthread_mutex_lock(&m); int d = depth; pthread_mutex_unlock(&m); return d;
}
static void q_pop(void) {             /* thread-safe */
    pthread_mutex_lock(&m);
    depth--;
    if (depth < 0) overpops++;
    pthread_mutex_unlock(&m);
}
static void q_push(void) {
    pthread_mutex_lock(&m); depth++; pthread_mutex_unlock(&m);
}

static void *consumer(void *arg) {
    (void)arg;
    for (long i = 0; i < 2000000L; i++)
        if (q_size() > 0)             /* CHECK  ... 여기서 선점되면? */
            q_pop();                  /* ACT    이미 남이 가져갔다 */
    return NULL;
}
static void *producer(void *arg) {
    (void)arg;
    for (long i = 0; i < 2000000L; i++) q_push();
    return NULL;
}

int main(void) {
    pthread_t p, c1, c2;
    pthread_create(&p,  NULL, producer, NULL);
    pthread_create(&c1, NULL, consumer, NULL);
    pthread_create(&c2, NULL, consumer, NULL);
    pthread_join(p, NULL); pthread_join(c1, NULL); pthread_join(c2, NULL);
    printf("final depth = %d, pops on an empty queue = %ld\n", depth, overpops);
    return 0;
}
