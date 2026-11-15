/* TOOL-B03 practice — answers to 9. 추가 연습, checked by
 *   bash check.sh practice
 * (6 levels, identical output, ASan+UBSan clean, and delay_spin must keep its
 * loop at -O2 / -Oz in objdump -d).
 *
 *   1. delay_spin(n)   busy-wait that -O2 cannot delete (volatile counter)
 *   2. delay_us(us)    deadline on a monotonic clock (DWT->CYCCNT / SysTick idea)
 *   3. mid_i32(lo, hi) floor midpoint without signed overflow
 *   4. sat_add_i32     saturating add (DSP / ADC accumulation)
 * Timing results are printed only as true/false so stdout stays identical
 * across optimization levels; raw numbers go to stderr.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define NOINL __attribute__((noinline))

/* ---- 1. busy-wait that survives the optimizer ---------------------------- */
NOINL void delay_spin(uint32_t n)
{
	/* every access to i is a side effect -> n loads + n stores must happen */
	for (volatile uint32_t i = 0; i < n; i++) {
	}
}

/* ---- 2. timer-based delay ------------------------------------------------- */
static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

NOINL void delay_us(uint32_t us)
{
	/* firmware: start = DWT->CYCCNT; while (DWT->CYCCNT - start < us * MHz); */
	const uint64_t start = now_ns();
	while (now_ns() - start < (uint64_t)us * 1000u) {
	}
}

/* ---- 3. midpoint ---------------------------------------------------------- */
NOINL int32_t mid_i32(int32_t lo, int32_t hi) /* requires lo <= hi */
{
	uint32_t d = (uint32_t)hi - (uint32_t)lo; /* 0 .. 2^32-1, no UB */
	return lo + (int32_t)(d / 2u);            /* d/2 < 2^31, result <= hi */
}

/* ---- 4. saturating add ---------------------------------------------------- */
NOINL int32_t sat_add_i32(int32_t a, int32_t b)
{
	int32_t r;
	if (__builtin_add_overflow(a, b, &r))
		return b > 0 ? INT32_MAX : INT32_MIN;
	return r;
}

/* ---- harness ---------------------------------------------------------------- */
static int g_pass, g_total;

#define CHECK_EQ(expr, want)                                                   \
	do {                                                                   \
		long long got_ = (long long)(expr), want_ = (long long)(want); \
		g_total++;                                                     \
		if (got_ == want_) {                                           \
			g_pass++;                                              \
			printf("PASS %-44s = %lld\n", #expr, got_);            \
		} else {                                                       \
			printf("FAIL %-44s = %lld (want %lld)\n", #expr, got_, \
			       want_);                                         \
		}                                                              \
	} while (0)

/* min over 7 rounds, N and 8N interleaved: robust against a cold core or
 * a preemption (noise only ever makes a run slower) */
static void spin_times(uint32_t n, uint64_t *t1, uint64_t *t8)
{
	*t1 = *t8 = UINT64_MAX;
	delay_spin(8u * n); /* warm-up: let the core clock ramp */
	for (int r = 0; r < 7; r++) {
		uint64_t a = now_ns();
		delay_spin(n);
		uint64_t b = now_ns();
		delay_spin(8u * n);
		uint64_t c = now_ns();
		if (b - a < *t1) *t1 = b - a;
		if (c - b < *t8) *t8 = c - b;
	}
}

int main(void)
{
	/* 1: the loop really runs: 8x the count takes clearly longer (>= 2x;
	 *    loose on purpose - other jobs share this machine) */
	const uint32_t n = 200000u;
	uint64_t t1, t8;
	spin_times(n, &t1, &t8);
	fprintf(stderr, "delay_spin: %u -> %llu ns, %u -> %llu ns\n", n,
		(unsigned long long)t1, 8u * n, (unsigned long long)t8);
	bool spin_scales = t1 > 0 && t8 >= 2u * t1;
	CHECK_EQ(spin_scales, true);

	/* 2: at least 2000 us (guaranteed by the deadline), and it did return */
	uint64_t t0 = now_ns();
	delay_us(2000u);
	uint64_t us_ns = now_ns() - t0;
	fprintf(stderr, "delay_us(2000): %llu ns\n", (unsigned long long)us_ns);
	bool us_ok = us_ns >= 2000000u && us_ns < 1000000000u;
	CHECK_EQ(us_ok, true);

	/* 3 */
	CHECK_EQ(mid_i32(0, 10), 5);
	CHECK_EQ(mid_i32(-7, -2), -5);
	CHECK_EQ(mid_i32(INT32_MAX - 1, INT32_MAX), INT32_MAX - 1);
	CHECK_EQ(mid_i32(INT32_MIN, INT32_MAX), -1);
	CHECK_EQ(mid_i32(INT32_MIN, INT32_MIN + 3), INT32_MIN + 1);

	/* 4 */
	CHECK_EQ(sat_add_i32(1000, 2000), 3000);
	CHECK_EQ(sat_add_i32(INT32_MAX - 5, 100), INT32_MAX);
	CHECK_EQ(sat_add_i32(INT32_MIN + 5, -100), INT32_MIN);
	CHECK_EQ(sat_add_i32(INT32_MIN, INT32_MAX), -1);

	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
