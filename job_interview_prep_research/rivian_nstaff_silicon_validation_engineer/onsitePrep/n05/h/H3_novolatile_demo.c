/* H3 · the broken versions: same as H3_volatile.c but WITHOUT volatile.
 * Build at -O0 and -O2 (warning-free; no -Werror needed) and run under a
 * 2 s watchdog (run.sh). Mode "flag": wait on an ISR flag. Mode "poll":
 * poll a status word through a plain uint32_t * (like L2_poll). */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static int done;                     /* BUG: not volatile */
static uint32_t status;              /* BUG: "register" not volatile */

static void fake_isr(int sig) { (void)sig; status |= 1u; done = 1; }

__attribute__((noinline)) static void poll_ready(uint32_t *st) {
    while ((*st & 1u) == 0) { }
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    struct itimerval it = {{0, 0}, {0, 20 * 1000}};   /* ISR fires at 20 ms */
    signal(SIGALRM, fake_isr);
    setitimer(ITIMER_REAL, &it, NULL);
    double t0 = now_ms();
    if (argc > 1 && strcmp(argv[1], "poll") == 0) poll_ready(&status);
    else                                          while (!done) { }
    printf("left the loop after %.1f ms (ISR fires at 20 ms), done=%d\n",
           now_ms() - t0, done);
    return 0;
}
