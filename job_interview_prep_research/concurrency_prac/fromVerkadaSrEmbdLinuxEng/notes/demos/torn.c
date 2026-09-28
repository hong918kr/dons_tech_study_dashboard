/* torn.c — the invariant "lat and lon come from the SAME fix" breaks
 * without a lock. volatile only stops the compiler from caching; it
 * does NOT make the pair atomic.                                     */
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>

struct GpsFix { double lat, lon; };
static volatile struct GpsFix g_pub = { 37.5, 127.0 };
static atomic_int stop = 0;

static void *writer(void *arg) {
    (void)arg;
    while (!atomic_load(&stop)) {
        g_pub.lat = 37.5; g_pub.lon = 127.0;   /* Seoul */
        g_pub.lat = 40.7; g_pub.lon = -74.0;   /* New York */
    }
    return NULL;
}

static void *reader(void *arg) {
    long *torn = arg, n = 0;
    for (long i = 0; i < 20000000L; i++) {
        double lat = g_pub.lat;                /* two separate loads: */
        double lon = g_pub.lon;                /* the writer can slip in between */
        int seoul = (lat == 37.5 && lon == 127.0);
        int ny    = (lat == 40.7 && lon == -74.0);
        if (!seoul && !ny) n++;                /* a position that never existed */
    }
    *torn = n;
    return NULL;
}

int main(void) {
    pthread_t w, r; long torn = 0;
    pthread_create(&w, NULL, writer, NULL);
    pthread_create(&r, NULL, reader, &torn);
    pthread_join(r, NULL);
    atomic_store(&stop, 1);
    pthread_join(w, NULL);
    printf("20,000,000 reads -> %ld torn (never-existed) positions\n", torn);
    return 0;
}
