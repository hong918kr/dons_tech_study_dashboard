/* ENV: qemu */
/*
 * ELX-M05 guest test: runs inside the QEMU guest, static binary.
 * Usage: m05_test <path to sim_gpio0/pull> <starter|solution|practice>
 * Every wait is bounded; on timeout the test fails and the run moves on.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SYS "/sys/devices/platform/elx_m05/"
static int g_pass, g_total;

#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
	else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

static const char *g_pull;
static const char *g_mod;
static char g_stat[512];

static int read_file(const char *p, char *b, int n)
{
	int fd = open(p, O_RDONLY), r;

	if (fd < 0)
		return -1;
	r = read(fd, b, n - 1);
	close(fd);
	if (r < 0)
		return -1;
	b[r] = 0;
	return r;
}

static int write_str(const char *p, const char *s)
{
	int fd = open(p, O_WRONLY), r;

	if (fd < 0)
		return -1;
	r = write(fd, s, strlen(s));
	close(fd);
	return r < 0 ? -1 : 0;
}

/* value of key=NUM in a text buffer, -1 if absent */
static long key_val(const char *s, const char *key)
{
	char k[64];
	const char *p;

	snprintf(k, sizeof(k), "%s=", key);
	p = strstr(s, k);
	return p ? strtol(p + strlen(k), NULL, 10) : -1;
}

static long stat_v(const char *key)
{
	char *p = g_stat;

	if (read_file(SYS "stats", p, sizeof(g_stat)) < 0)
		return -1;
	return key_val(p, key);
}

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* bounded poll: true once stat key reaches target */
static bool wait_stat(const char *key, long target, long timeout_ms)
{
	long t0 = now_ms();

	while (now_ms() - t0 < timeout_ms) {
		if (stat_v(key) >= target)
			return true;
		usleep(200);
	}
	return false;
}

static int pull(int up)
{
	return write_str(g_pull, up ? "pull-up" : "pull-down");
}

/* sum per-CPU columns of the /proc/interrupts row whose name contains "elx_m05"; -1 if absent */
static long irq_count(void)
{
	static char b[32768];
	char *line, *save, *p;

	if (read_file("/proc/interrupts", b, sizeof(b)) < 0)
		return -1;
	for (line = strtok_r(b, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		long sum = 0;

		if (!strstr(line, "elx_m05"))
			continue;
		p = strchr(line, ':');
		if (!p)
			return -1;
		p++;
		for (int i = 0; i < 4; i++) {
			char *e;
			long v = strtol(p, &e, 10);

			if (e == p)
				break;
			sum += v;
			p = e;
		}
		return sum;
	}
	return -1;
}

static int sys_irq(void)
{
	char b[32];

	if (read_file(SYS "irq", b, sizeof(b)) < 0)
		return -1;
	return atoi(b);
}

/* find the irq/NN-elx_m05 kernel thread: fills irqnum, policy, rt prio */
static int find_irq_thread(int *irqnum, int *policy, int *rtprio, char *comm_out, int n)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	char p[300], b[256];

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char *s, *rp;
		int field = 1;

		if (e->d_name[0] < '0' || e->d_name[0] > '9')
			continue;
		snprintf(p, sizeof(p), "/proc/%s/comm", e->d_name);
		if (read_file(p, b, sizeof(b)) < 0)
			continue;
		b[strcspn(b, "\n")] = 0;
		if (strncmp(b, "irq/", 4) || !strstr(b, "elx_m05"))
			continue;
		snprintf(comm_out, n, "%.31s", b);
		*irqnum = atoi(b + 4);
		snprintf(p, sizeof(p), "/proc/%s/stat", e->d_name);
		if (read_file(p, b, sizeof(b)) < 0)
			break;
		/* stat: pid (comm) S ...; comm may contain spaces, skip past ')' */
		rp = strrchr(b, ')');
		s = rp + 2;
		for (field = 3; s && field <= 41; field++) {
			if (field == 40)
				*rtprio = atoi(s);
			if (field == 41)
				*policy = atoi(s);
			s = strchr(s, ' ');
			if (s)
				s++;
		}
		closedir(d);
		return atoi(e->d_name);
	}
	closedir(d);
	return -1;
}

#define EDGES_A 200
#define EDGES_B 64

static void part_a(void)
{
	long before, after;
	int i;
	bool ok = true;

	puts("== Part A: K spaced edges, each processed before the next ==");
	write_str(SYS "reset", "1");
	before = irq_count();
	CHECK(before >= 0);
	for (i = 1; i <= EDGES_A && ok; i++) {
		if (pull(1) < 0 || !wait_stat("processed", i, 3000)) {
			printf("timeout waiting for edge %d\n", i);
			ok = false;
		}
		pull(0);
	}
	CHECK(ok);
	after = irq_count();
	printf("K=%d /proc/interrupts delta=%ld hard=%ld thread_runs=%ld processed=%ld\n",
	       EDGES_A, after - before, stat_v("hard"), stat_v("thread_runs"), stat_v("processed"));
	CHECK(after - before == EDGES_A);
	CHECK(stat_v("hard") == EDGES_A);
	CHECK(stat_v("thread_runs") == EDGES_A);
	CHECK(stat_v("processed") == EDGES_A);
	CHECK(stat_v("dropped") == 0);
}

static void part_b(void)
{
	long before, after;
	int i;
	bool ok = true;

	puts("== Part B: thread parked at a gate, K edges pile up ==");
	write_str(SYS "reset", "1");
	before = irq_count();
	write_str(SYS "gate", "1");
	pull(1);
	ok = wait_stat("thread_runs", 1, 3000);	/* thread is inside its handler, parked */
	pull(0);
	for (i = 2; i <= EDGES_B && ok; i++) {
		pull(1);
		ok = wait_stat("hard", i, 3000);
		pull(0);
	}
	CHECK(ok);
	printf("while parked: hard=%ld thread_runs=%ld processed=%ld\n",
	       stat_v("hard"), stat_v("thread_runs"), stat_v("processed"));
	CHECK(stat_v("hard") == EDGES_B);
	CHECK(stat_v("thread_runs") == 1);
	CHECK(stat_v("processed") == 0);
	write_str(SYS "gate", "0");
	ok = wait_stat("processed", EDGES_B, 5000);
	CHECK(ok);
	wait_stat("thread_runs", 2, 2000);
	after = irq_count();
	printf("after release: /proc/interrupts delta=%ld hard=%ld thread_runs=%ld processed=%ld dropped=%ld\n",
	       after - before, stat_v("hard"), stat_v("thread_runs"), stat_v("processed"), stat_v("dropped"));
	CHECK(after - before == EDGES_B);
	CHECK(stat_v("processed") == EDGES_B);
	CHECK(stat_v("thread_runs") == 2);
	CHECK(stat_v("dropped") == 0);
}

static void part_c(void)
{
	char b[1024], comm[32] = "";
	int irqn = -1, policy = -1, rtprio = -1, pid;

	puts("== Part C: IRQ thread identity + latency stats ==");
	pid = find_irq_thread(&irqn, &policy, &rtprio, comm, sizeof(comm));
	printf("irq thread: pid=%d comm=%s policy=%d rtprio=%d (sysfs irq=%d)\n",
	       pid, comm, policy, rtprio, sys_irq());
	CHECK(pid > 0);
	CHECK(irqn == sys_irq());
	CHECK(policy == 1);	/* SCHED_FIFO */
	CHECK(rtprio == 50);
	CHECK(stat_v("hard_ctx_bad") == 0);
	CHECK(stat_v("thread_ctx_bad") == 0);

	/* Real latencies from Part A/B: structure only, never a timing threshold. */
	if (read_file(SYS "latency", b, sizeof(b)) < 0)
		b[0] = 0;
	printf("%s", b);
	{
		long n = key_val(b, "n"), min = key_val(b, "min_ns"), avg = key_val(b, "avg_ns");
		long p99 = key_val(b, "p99_ns"), max = key_val(b, "max_ns");

		CHECK(n == EDGES_B);
		CHECK(min >= 0 && min <= avg && avg <= max && min <= p99 && p99 <= max);
	}

	/* Deterministic percentile check with injected samples 1..100. */
	write_str(SYS "reset", "1");
	for (int v = 100; v >= 1; v--) {
		char s[16];

		snprintf(s, sizeof(s), "%d", v);
		write_str(SYS "lat_inject", s);
	}
	if (read_file(SYS "latency", b, sizeof(b)) < 0)
		b[0] = 0;
	printf("%s", b);
	CHECK(key_val(b, "n") == 100);
	CHECK(key_val(b, "min_ns") == 1);
	CHECK(key_val(b, "max_ns") == 100);
	CHECK(key_val(b, "avg_ns") == 50);
	CHECK(key_val(b, "p50_ns") == 50);
	CHECK(key_val(b, "p99_ns") == 99);
}

/* ---- practice-only extras ---- */
#define EDGES_OVF 300	/* fifo holds 256 */

static void set_work(int us)
{
	char b[16];

	snprintf(b, sizeof(b), "%d", us);
	write_str(SYS "work_us", b);
}

static void reload(const char *params)
{
	char cmd[128];

	(void)!system("rmmod practice");
	snprintf(cmd, sizeof(cmd), "insmod /practice.ko %s", params);
	CHECK(system(cmd) == 0);
}

/* ex2: thread parked, more edges than the fifo holds -> exact drop count */
static void ex2_overflow(void)
{
	bool ok;

	puts("== Practice 2: fifo overflow (gate, K=300, fifo=256) ==");
	write_str(SYS "reset", "1");
	set_work(0);
	write_str(SYS "gate", "1");
	pull(1);
	ok = wait_stat("thread_runs", 1, 3000);
	pull(0);
	for (int i = 2; i <= EDGES_OVF && ok; i++) {
		pull(1);
		ok = wait_stat("hard", i, 3000);
		pull(0);
	}
	CHECK(ok);
	printf("while parked: hard=%ld dropped=%ld processed=%ld\n",
	       stat_v("hard"), stat_v("dropped"), stat_v("processed"));
	CHECK(stat_v("hard") == EDGES_OVF);
	CHECK(stat_v("dropped") == EDGES_OVF - 256);
	CHECK(stat_v("processed") == 0);
	write_str(SYS "gate", "0");
	CHECK(wait_stat("processed", 256, 6000));
	usleep(200000);
	CHECK(stat_v("processed") == 256);
	set_work(100);
}

/* ex4: edges during disable_irq: replayed once on enable */
static void ex4_disable(void)
{
	long during, after;

	puts("== Practice 4: disable_irq, K edges, enable_irq ==");
	write_str(SYS "reset", "1");
	write_str(SYS "irq_off", "1");
	for (int i = 0; i < 10; i++) {
		pull(1);
		usleep(1000);
		pull(0);
		usleep(1000);
	}
	during = stat_v("hard");
	write_str(SYS "irq_off", "0");
	wait_stat("hard", 1, 2000);
	usleep(100000);
	after = stat_v("hard");
	printf("10 edges while disabled: hard during=%ld after enable=%ld processed=%ld\n",
	       during, after, stat_v("processed"));
	CHECK(during == 0);
	CHECK(after == 1);
	CHECK(stat_v("processed") == 1);
}

/* ex1: both edges, level read from the thread with _cansleep */
static void ex1_both_edges(void)
{
	int i;
	bool ok = true;

	puts("== Practice 1: both edges, level via gpiod_get_value_cansleep ==");
	reload("both_edges=1");
	for (i = 1; i <= 50 && ok; i++) {
		pull(1);
		ok = wait_stat("processed", 2 * i - 1, 3000);
		pull(0);
		ok = ok && wait_stat("processed", 2 * i, 3000);
	}
	CHECK(ok);
	printf("hard=%ld level_high=%ld level_low=%ld\n", stat_v("hard"),
	       stat_v("level_high"), stat_v("level_low"));
	CHECK(stat_v("hard") == 100);
	CHECK(stat_v("level_high") == 50);
	CHECK(stat_v("level_low") == 50);
}

/* ex3: thread-only (handler NULL + ONESHOT) */
static void ex3_thread_only(void)
{
	long before, after;
	bool ok = true;
	char b[256];

	puts("== Practice 3: handler==NULL + IRQF_ONESHOT ==");
	reload("thread_only=1");
	before = irq_count();
	for (int i = 1; i <= 50 && ok; i++) {
		pull(1);
		ok = wait_stat("processed", i, 3000);
		pull(0);
	}
	CHECK(ok);
	after = irq_count();
	if (read_file(SYS "latency", b, sizeof(b)) < 0)
		b[0] = 0;
	printf("/proc/interrupts delta=%ld hard=%ld thread_runs=%ld processed=%ld latency: %s",
	       after - before, stat_v("hard"), stat_v("thread_runs"), stat_v("processed"), b);
	CHECK(after - before == 50);
	CHECK(stat_v("hard") == 0);
	CHECK(stat_v("processed") == 50);
	CHECK(key_val(b, "n") == 0);
}

/* demo mode (no CHECKs): K spaced edges, wait for all to be processed. spacing_us between edges. */
static int latency_run(int k, int spacing_us)
{
	struct sched_param sp = { .sched_priority = 99 };

	/* Firer runs at FIFO 99 so the hog at FIFO 98 can't stop the edges themselves. */
	sched_setscheduler(0, SCHED_FIFO, &sp);
	for (int i = 1; i <= k; i++) {
		pull(1);
		usleep(spacing_us);
		pull(0);
		usleep(spacing_us);
	}
	if (!wait_stat("processed", k, 15000))
		printf("WARNING: only %ld processed after the wait\n", stat_v("processed"));
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: m05_test pull_path module\n");
		return 2;
	}
	g_pull = argv[1];
	g_mod = argv[2];
	setvbuf(stdout, NULL, _IONBF, 0);
	if (!strcmp(g_mod, "latency") && argc >= 5)
		return latency_run(atoi(argv[3]), atoi(argv[4]));
	part_a();
	part_b();
	part_c();
	if (!strcmp(g_mod, "practice")) {
		ex2_overflow();
		ex4_disable();
		ex1_both_edges();
		ex3_thread_only();
	}
	printf("TESTS passed %d/%d (module %s)\n", g_pass, g_total, g_mod);
	return g_pass == g_total ? 0 : 1;
}
