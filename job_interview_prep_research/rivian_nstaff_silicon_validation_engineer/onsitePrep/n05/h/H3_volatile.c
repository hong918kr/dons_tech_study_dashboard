/* H3 · volatile, runnable: a SIGALRM handler plays the role of the ISR.
 * `volatile sig_atomic_t` is exactly the case the C standard blesses
 * (C11 7.14.1.1p5). Works at -O0 and -O2 (run.sh builds both). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

static volatile sig_atomic_t done;          /* ISR-shared flag: volatile */
static volatile uint32_t fake_status;       /* "MMIO" status register    */

static void fake_isr(int sig) {
    (void)sig;
    fake_status |= 1u;   /* hardware sets READY ... (single writer here)  */
    done = 1;            /* ... and the ISR raises the flag                */
}

static void arm_timer_ms(int ms) {
    struct itimerval it = {{0, 0}, {0, ms * 1000}};
    signal(SIGALRM, fake_isr);
    setitimer(ITIMER_REAL, &it, NULL);
}

int main(void) {
    unsigned long spins = 0;
    arm_timer_ms(20);
    while (!done) { spins++; }                    /* L1 done right */
    assert(done == 1);

    while ((fake_status & 1u) == 0) { }          /* L2 done right (already set) */
    assert(fake_status & 1u);

    printf("flag seen after %s spins\n", spins > 1000 ? ">1000" : "few");
    puts("H3 PASS");
    return 0;
}
