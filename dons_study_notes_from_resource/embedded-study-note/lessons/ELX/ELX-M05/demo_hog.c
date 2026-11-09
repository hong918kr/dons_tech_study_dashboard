/* ENV: qemu */
/* ELX-M05 demo_hog: one spinning thread per CPU for <seconds>; bounded, then exits.
 * Usage: demo_hog <other|fifo> <prio> <seconds>   */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double g_secs;

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

struct arg { int cpu; int fifo; int prio; };

static void *spin(void *p)
{
	struct arg *a = p;
	cpu_set_t c;
	double end = now() + g_secs;
	volatile unsigned long n = 0;

	CPU_ZERO(&c);
	CPU_SET(a->cpu, &c);
	pthread_setaffinity_np(pthread_self(), sizeof(c), &c);
	if (a->fifo) {
		struct sched_param sp = { .sched_priority = a->prio };

		pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
	}
	while (now() < end)
		for (int i = 0; i < 2000; i++)
			n++;
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t th[16];
	struct arg a[16];
	int n = sysconf(_SC_NPROCESSORS_ONLN);

	if (argc < 4 || n > 16)
		return 2;
	g_secs = atof(argv[3]);
	for (int i = 0; i < n; i++) {
		a[i] = (struct arg){ i, !strcmp(argv[1], "fifo"), atoi(argv[2]) };
		pthread_create(&th[i], NULL, spin, &a[i]);
	}
	for (int i = 0; i < n; i++)
		pthread_join(th[i], NULL);
	return 0;
}
