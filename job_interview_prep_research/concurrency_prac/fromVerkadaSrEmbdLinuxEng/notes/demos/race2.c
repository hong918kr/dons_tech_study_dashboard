#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static struct { double lat, lon; } shared;   /* 보호 없음 */
static volatile int stop;

static void *writer(void *a) {
    (void)a;
    for (double v = 37.0; !stop; v += 0.001) { shared.lat = v; shared.lon = -(v + 85.0); }
    return NULL;
}
int main(void) {
    pthread_t w;
    pthread_create(&w, NULL, writer, NULL);
    for (int i = 0; i < 200000; i++) {
        double la = shared.lat, lo = shared.lon;      /* 찢어진 읽기 */
        if (lo != -(la + 85.0)) { printf("torn: lat=%.3f lon=%.3f\n", la, lo); break; }
    }
    stop = 1; pthread_join(w, NULL);
    return 0;
}
