/* TOOL-B03 demo_delay.c — four busy-wait delays, timed at each -O level.
 *   bash demo_delay.sh      (table of ns per iteration + -O2 disassembly)
 *   -DDELAY_KERNEL_ONLY     only the four delays (freestanding, for Cortex-M -S)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>

#define NOINL __attribute__((noinline))

volatile uint32_t g_sink;

NOINL void delay_plain(uint32_t n) /* no volatile at all */
{
	for (uint32_t i = 0; i < n; i++) {
	}
}

NOINL void delay_sink(uint32_t n) /* MISUSE: only the result is volatile */
{
	uint32_t i;
	for (i = 0; i < n; i++) {
	}
	g_sink = i;
}

NOINL void delay_vcounter(uint32_t n) /* volatile counter: every i++ is a load + store */
{
	for (volatile uint32_t i = 0; i < n; i++) {
	}
}

NOINL void delay_asm(uint32_t n) /* opaque empty asm: loop must run, counter in a register */
{
	for (uint32_t i = 0; i < n; i++)
		__asm__ volatile("" ::: "memory");
}

#ifndef DELAY_KERNEL_ONLY
#include <stdio.h>
#include <time.h>

static double ns_per_iter(void (*fn)(uint32_t), uint32_t n)
{
	double best = 1e30;
	for (int r = 0; r < 5; r++) {
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		fn(n);
		clock_gettime(CLOCK_MONOTONIC, &b);
		double ns = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
		if (ns < best) best = ns;
	}
	return best / n;
}

int main(void)
{
	const uint32_t n = 20000000u;
	delay_vcounter(n); /* warm-up */
	printf("%8.3f %8.3f %10.3f %8.3f\n", ns_per_iter(delay_plain, n), ns_per_iter(delay_sink, n),
	       ns_per_iter(delay_vcounter, n), ns_per_iter(delay_asm, n));
	return 0;
}
#endif
