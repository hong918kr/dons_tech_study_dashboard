/* TOOL-B03 demo_bench.c — deterministic CPU work for the -O level table.
 *   kernels: bitwise CRC-32 (4 KB), insertion sort (512 x int16), Q15 FIR (16 taps x 2048)
 *   -DBENCH_KERNEL_ONLY  -> only the kernels (freestanding: also builds for
 *                           --target=thumbv7em-none-eabi / arm-none-eabi-gcc -c)
 * Hosted:  cc -std=c11 -O2 demo_bench.c -o bench && ./bench [reps]
 * Output: one checksum line (must be identical at every -O level) + elapsed ms.
 */
#ifndef BENCH_KERNEL_ONLY
#define _POSIX_C_SOURCE 200809L /* clock_gettime under -std=c11 */
#endif
#include <stddef.h>
#include <stdint.h>

#define BUF_N 4096
#define SORT_N 512
#define SIG_N 2048
#define TAPS 16

static uint8_t g_buf[BUF_N];
static int16_t g_arr[SORT_N];
static int16_t g_sig[SIG_N];
static int16_t g_out[SIG_N];
static const int16_t g_taps[TAPS] = { 120,  -340, 910,  2048, 4096, 6144, 7800, 8191,
				      8191, 7800, 6144, 4096, 2048, 910,  -340, 120 };

static uint32_t lcg(uint32_t *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

uint32_t crc32_sw(const uint8_t *p, size_t n)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void isort_i16(int16_t *a, size_t n)
{
	for (size_t i = 1; i < n; i++) {
		int16_t key = a[i];
		size_t j = i;
		while (j > 0 && a[j - 1] > key) {
			a[j] = a[j - 1];
			j--;
		}
		a[j] = key;
	}
}

int32_t fir_q15(const int16_t *x, int16_t *y, size_t n, const int16_t *h, size_t taps)
{
	int32_t sum = 0;
	for (size_t i = 0; i + taps <= n; i++) {
		int32_t acc = 0;
		for (size_t k = 0; k < taps; k++)
			acc += (int32_t)x[i + k] * h[k];
		y[i] = (int16_t)(acc >> 15);
		sum += y[i];
	}
	return sum;
}

uint32_t bench_run(uint32_t seed)
{
	uint32_t s = seed;
	for (size_t i = 0; i < BUF_N; i++)
		g_buf[i] = (uint8_t)(lcg(&s) >> 24);
	for (size_t i = 0; i < SORT_N; i++)
		g_arr[i] = (int16_t)(lcg(&s) >> 16);
	for (size_t i = 0; i < SIG_N; i++)
		g_sig[i] = (int16_t)(lcg(&s) >> 17);

	uint32_t crc = crc32_sw(g_buf, BUF_N);
	isort_i16(g_arr, SORT_N);
	int32_t fir = fir_q15(g_sig, g_out, SIG_N, g_taps, TAPS);
	return crc ^ ((uint32_t)g_arr[0] << 16) ^ (uint16_t)g_arr[SORT_N - 1] ^ (uint32_t)fir;
}

#ifndef BENCH_KERNEL_ONLY
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
	int reps = argc > 1 ? atoi(argv[1]) : 400;
	struct timespec t0, t1;
	uint32_t acc = 0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int r = 0; r < reps; r++)
		acc = acc * 31u + bench_run((uint32_t)r + 1u);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
	printf("checksum=0x%08x reps=%d\n", acc, reps);
	fprintf(stderr, "%.2f\n", ms);
	return 0;
}
#endif
