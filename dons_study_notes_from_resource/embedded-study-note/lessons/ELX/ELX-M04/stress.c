/* ENV: qemu */
/*
 * ELX-M04 guest helper (built with gcc -static in the VM, run in the guest).
 *   stress selftest
 *   stress run <none|mutex|spin|irqsave|atomic> <timer_us[h]> <with_list> <threads> <k> <batch> [check]
 *     timer_us: 0 = no IRQ path; suffix h = HRTIMER_MODE_REL_HARD ("500h")
 *   stress abba | stress recursive | stress nested
 * Prints RESULT lines; with "check", also PASS:/FAIL: lines.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "syncdrv_uapi.h"

static const char *const mode_names[SD_MODE_COUNT] = {
	"none", "mutex", "spin", "irqsave", "atomic",
	"raw_irqsave", "wq_mutex", "spin_irq", "percpu",
};

static int fd;
static uint32_t g_k, g_batch;
static int fails;

static void check(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		fails++;
}

static void *worker(void *arg)
{
	uint32_t left = g_k;

	(void)arg;
	while (left) {
		uint32_t n = left < g_batch ? left : g_batch;

		if (ioctl(fd, SD_IOC_UPDATE, &n)) {
			perror("UPDATE");
			exit(2);
		}
		left -= n;
	}
	return NULL;
}

static int do_selftest(void)
{
	struct sd_selftest r;

	if (ioctl(fd, SD_IOC_SELFTEST, &r)) {
		perror("SELFTEST");
		return 2;
	}
	check(r.held, "irqsave helper holds sd.lock");
	check(r.irqs_off, "irqsave helper disables IRQs");
	check(r.irqs_restored, "irqsave unlock restores IRQs");
	check(r.atomic_ok, "1000 atomic touches == 1000");
	return fails ? 1 : 0;
}

static int do_run(int argc, char **argv)
{
	struct sd_config cfg = { 0 };
	struct sd_stats st;
	pthread_t th[32];
	uint32_t i, threads;
	int m = -1, want_check;
	uint64_t expected;

	if (argc < 8)
		return 2;
	for (i = 0; i < SD_MODE_COUNT; i++)
		if (!strcmp(argv[2], mode_names[i]))
			m = (int)i;
	if (m < 0)
		return 2;
	cfg.mode = (uint32_t)m;
	cfg.timer_us = (uint32_t)atoi(argv[3]);
	cfg.timer_hard = strchr(argv[3], 'h') != NULL;
	cfg.with_list = (uint32_t)atoi(argv[4]);
	threads = (uint32_t)atoi(argv[5]);
	g_k = (uint32_t)atoi(argv[6]);
	g_batch = (uint32_t)atoi(argv[7]);
	want_check = argc > 8 && !strcmp(argv[8], "check");
	if (argc > 8 && !strcmp(argv[8], "pin0")) {	/* timer and worker on one CPU */
		cpu_set_t set;

		CPU_ZERO(&set);
		CPU_SET(0, &set);
		if (sched_setaffinity(0, sizeof(set), &set))
			perror("sched_setaffinity");
	}
	if (threads > 32)
		return 2;

	if (ioctl(fd, SD_IOC_CONFIG, &cfg)) {
		perror("CONFIG");
		return 2;
	}
	for (i = 0; i < threads; i++)
		pthread_create(&th[i], NULL, worker, NULL);
	for (i = 0; i < threads; i++)
		pthread_join(th[i], NULL);
	usleep(50000);		/* let the IRQ path fire at least once more */
	if (ioctl(fd, SD_IOC_STOP) || ioctl(fd, SD_IOC_GET_STATS, &st)) {
		perror("STOP/GET_STATS");
		return 2;
	}

	expected = (uint64_t)threads * g_k + st.irq_fires;
	printf("RESULT mode=%s timer_us=%u%s with_list=%u threads=%u k=%u counter=%llu list_len=%llu irq_fires=%llu expected=%llu lost=%lld list_lost=%lld not_hardirq=%u walk_overflow=%u pool_exhausted=%llu\n",
	       mode_names[m], cfg.timer_us, cfg.timer_hard ? "(hard)" : "", cfg.with_list, threads, g_k,
	       (unsigned long long)st.counter, (unsigned long long)st.list_len,
	       (unsigned long long)st.irq_fires, (unsigned long long)expected,
	       (long long)(expected - st.counter),
	       (long long)(cfg.with_list ? expected - st.list_len : 0),
	       st.not_hardirq, st.walk_overflow,
	       (unsigned long long)st.pool_exhausted);

	if (want_check) {
		char msg[128];

		snprintf(msg, sizeof(msg), "%s counter exact", mode_names[m]);
		check(st.counter == expected, msg);
		if (cfg.with_list) {
			snprintf(msg, sizeof(msg), "%s list length exact", mode_names[m]);
			check(st.list_len == expected && !st.walk_overflow, msg);
		}
		if (cfg.timer_us) {
			snprintf(msg, sizeof(msg), "%s IRQ path ran in hard IRQ", mode_names[m]);
			check(st.irq_fires > 0 && st.not_hardirq == 0, msg);
		}
		snprintf(msg, sizeof(msg), "%s pool not exhausted", mode_names[m]);
		check(st.pool_exhausted == 0, msg);
	}
	return fails ? 1 : 0;
}

static int do_abba(uint32_t a, uint32_t b)
{
	if (ioctl(fd, SD_IOC_ABBA, &a) || ioctl(fd, SD_IOC_ABBA, &b)) {
		perror("ABBA");
		return 2;
	}
	printf("RESULT abba orders=%u,%u done (one thread, sequential: no real deadlock possible)\n", a, b);
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	fd = open("/dev/syncdrv", O_RDWR);
	if (fd < 0) {
		perror("open /dev/syncdrv");
		return 2;
	}
	if (argc >= 2 && !strcmp(argv[1], "selftest"))
		return do_selftest();
	if (argc >= 2 && !strcmp(argv[1], "run"))
		return do_run(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "abba"))
		return do_abba(0, 1);
	if (argc >= 2 && !strcmp(argv[1], "recursive"))
		return do_abba(2, 2);
	if (argc >= 2 && !strcmp(argv[1], "nested")) {
		int rc = do_abba(3, 3);

		check(rc == 0, "nested pair lock, both call orders");
		return rc;
	}
	fprintf(stderr, "usage: see header comment\n");
	return 2;
}
