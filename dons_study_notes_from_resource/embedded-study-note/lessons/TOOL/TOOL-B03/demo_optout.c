/* TOOL-B03 demo_optout.c — what the debugger shows at -O0 / -Og / -O2.
 *   bash demo_optout.sh        (builds 3 ways, runs lldb -b -s demo_optout.lldb)
 */
#include <stdint.h>
#include <stdio.h>

#define NOINL __attribute__((noinline))

static volatile int32_t g_last; /* stands in for "write result to a register" */

NOINL void report(int32_t v) { g_last = v; }

NOINL int32_t filter_block(const int16_t *x, int n, int16_t gain)
{
	int32_t sum = 0;
	int32_t peak = INT32_MIN;
	for (int i = 0; i < n; i++) {
		int32_t v = x[i] * gain;
		sum += v;
		if (v > peak)
			peak = v;
	}
	int32_t avg = sum / n;
	int32_t result = avg + peak / 4;
	report(result); /* BREAK HERE (line 25) */
	return result;
}

int main(void)
{
	static const int16_t samples[8] = { 10, -3, 25, 7, 0, 18, -9, 4 };
	int32_t r = filter_block(samples, 8, 3);
	printf("result=%d\n", r);
	return 0;
}
