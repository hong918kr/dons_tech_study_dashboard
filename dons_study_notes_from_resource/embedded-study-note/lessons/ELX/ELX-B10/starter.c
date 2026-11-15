/* ENV: qemu */
/*
 * ELX-B10 — userspace GPIO through the GPIO character device uAPI v2 (no libgpiod).
 * Runs INSIDE the QEMU guest as a static binary. guest.sh creates a gpio-sim chip
 * via configfs with 8 named lines and calls
 *     b10_test /dev/gpiochipN /sys/devices/platform/gpio-sim.0/gpiochipN
 * The second path holds sim_gpioK/{value,pull} = the "board side" of line K.
 * TODO 1..5 are the uAPI helpers; everything below "test harness" stays as is.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/gpio.h>

struct line_spec {
	const unsigned *offsets;	/* lines to request; index i = bit i in masks */
	unsigned n;			/* number of lines, 1..GPIO_V2_LINES_MAX */
	uint64_t flags;			/* GPIO_V2_LINE_FLAG_* applied to every line */
	uint64_t out_bits;		/* initial values when OUTPUT (bit i = line i) */
	unsigned debounce_us;		/* 0 = no debounce attribute */
	unsigned evbuf;			/* event_buffer_size hint, 0 = kernel default */
	const char *consumer;		/* label shown in line info */
};

struct ev_audit {
	int rising, falling;
	int seq_missing;		/* events generated but never received */
	int ts_backwards;		/* timestamp_ns not strictly increasing */
	int same_dir;			/* two consecutive events with the same id */
};

static inline uint64_t mask_n(unsigned n)	/* bits 0..n-1 set */
{
	return n >= 64 ? ~0ULL : (1ULL << n) - 1;
}

/* TODO 1: offset of the line named `name` on this chip, or -ENOENT (-errno on
 * ioctl failure). GPIO_GET_CHIPINFO_IOCTL -> number of lines, then
 * GPIO_V2_GET_LINEINFO_IOCTL per offset (zero the struct: padding must be 0). */
static int gpio_find_line(int chip_fd, const char *name)
{
	(void)chip_fd;
	(void)name;
	return -ENOSYS;	/* TODO 1 */
}

/* TODO 2: request the lines in *s with one GPIO_V2_GET_LINE_IOCTL. Return the
 * line-request fd (>= 0) or -errno. OUTPUT -> initial values go in as a
 * GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES attribute (atomic with the request);
 * debounce_us != 0 -> a GPIO_V2_LINE_ATTR_ID_DEBOUNCE attribute. n > 64 -> -EINVAL. */
static int gpio_request(int chip_fd, const struct line_spec *s)
{
	(void)chip_fd;
	(void)s;
	return -ENOSYS;	/* TODO 2 */
}

/* TODO 3a: drive the lines selected by mask (bit i = i-th requested line) to bits.
 * GPIO_V2_LINE_SET_VALUES_IOCTL. 0 or -errno. */
static int gpio_set(int lfd, uint64_t mask, uint64_t bits)
{
	(void)lfd;
	(void)mask;
	(void)bits;
	return -ENOSYS;	/* TODO 3a */
}

/* TODO 3b: read the logical values of the lines in mask into *bits.
 * GPIO_V2_LINE_GET_VALUES_IOCTL. 0 or -errno. */
static int gpio_get(int lfd, uint64_t mask, uint64_t *bits)
{
	(void)lfd;
	(void)mask;
	(void)bits;
	return -ENOSYS;	/* TODO 3b */
}

/* TODO 4: collect up to max edge events into ev[]. Return as soon as max are in
 * hand, or when no event arrives for timeout_ms. poll(POLLIN) then read(); one
 * read() may return several events (bytes / sizeof(*ev)). Return count or -errno. */
static int gpio_wait_events(int lfd, struct gpio_v2_line_event *ev, int max, int timeout_ms)
{
	(void)lfd;
	(void)ev;
	(void)max;
	(void)timeout_ms;
	return 0;		/* TODO 4 */
}

/* TODO 5: audit an event stream. first_seq = line_seqno the first event should
 * carry (1 for a fresh request). Count rising/falling, line_seqno gaps (incl. the
 * gap before ev[0]), non-increasing timestamps, and same-id neighbours. */
static void ev_audit(const struct gpio_v2_line_event *ev, int n, uint32_t first_seq,
		     struct ev_audit *a)
{
	(void)ev;
	(void)n;
	(void)first_seq;
	memset(a, 0, sizeof(*a));	/* TODO 5 */
}

/* ------------------------------ test harness ------------------------------ */
static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
	else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

static const char *g_sim;		/* .../gpio-sim.0/gpiochipN */

static uint64_t now_ns(clockid_t clk)
{
	struct timespec t;

	clock_gettime(clk, &t);
	return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static int sim_value(unsigned off)	/* board-side level of line off, -1 on error */
{
	char p[256], b[8] = "";
	int fd, r;

	snprintf(p, sizeof(p), "%s/sim_gpio%u/value", g_sim, off);
	fd = open(p, O_RDONLY);
	if (fd < 0)
		return -1;
	r = (int)read(fd, b, sizeof(b) - 1);
	close(fd);
	return r > 0 ? b[0] - '0' : -1;
}

static int sim_pull(unsigned off, int up)	/* "board" drives the line */
{
	char p[256];
	int fd, r;

	snprintf(p, sizeof(p), "%s/sim_gpio%u/pull", g_sim, off);
	fd = open(p, O_WRONLY);
	if (fd < 0)
		return -errno;
	r = (int)write(fd, up ? "pull-up" : "pull-down", up ? 7 : 9);
	close(fd);
	return r < 0 ? -errno : 0;
}

static int line_info(int cfd, unsigned off, struct gpio_v2_line_info *li)
{
	memset(li, 0, sizeof(*li));
	li->offset = off;
	return ioctl(cfd, GPIO_V2_GET_LINEINFO_IOCTL, li) < 0 ? -errno : 0;
}

static uint32_t info_debounce(const struct gpio_v2_line_info *li)
{
	for (unsigned i = 0; i < li->num_attrs; i++)
		if (li->attrs[i].id == GPIO_V2_LINE_ATTR_ID_DEBOUNCE)
			return li->attrs[i].debounce_period_us;
	return 0;
}

static struct gpio_v2_line_event mk(uint32_t id, uint64_t ts, uint32_t lseq)
{
	struct gpio_v2_line_event e;

	memset(&e, 0, sizeof(e));
	e.id = id;
	e.timestamp_ns = ts;
	e.line_seqno = lseq;
	e.seqno = lseq;
	return e;
}

#define R GPIO_V2_LINE_EVENT_RISING_EDGE
#define F GPIO_V2_LINE_EVENT_FALLING_EDGE
#define IN GPIO_V2_LINE_FLAG_INPUT
#define OUT GPIO_V2_LINE_FLAG_OUTPUT
#define EDGES (GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING)

static void test_audit_pure(void)
{
	struct gpio_v2_line_event e[6];
	struct ev_audit a;

	printf("--- Part 0: ev_audit on synthetic streams ---\n");
	e[0] = mk(R, 100, 1); e[1] = mk(F, 200, 2); e[2] = mk(R, 300, 3); e[3] = mk(F, 400, 4);
	ev_audit(e, 4, 1, &a);
	CHECK(a.rising == 2 && a.falling == 2);
	CHECK(a.seq_missing == 0 && a.ts_backwards == 0 && a.same_dir == 0);
	e[0] = mk(R, 100, 3); e[1] = mk(F, 200, 4); e[2] = mk(R, 300, 7);	/* overflow drops */
	ev_audit(e, 3, 1, &a);
	CHECK(a.seq_missing == 2 + 2);
	e[0] = mk(R, 500, 1); e[1] = mk(R, 500, 2); e[2] = mk(F, 400, 3);	/* merged edge, clock bug */
	ev_audit(e, 3, 1, &a);
	CHECK(a.same_dir == 1 && a.ts_backwards == 2 && a.rising == 2 && a.falling == 1);
	ev_audit(e, 0, 1, &a);
	CHECK(a.rising == 0 && a.seq_missing == 0);
}

int main(int argc, char **argv)
{
	struct gpio_v2_line_event ev[64];
	struct gpio_v2_line_info li;
	struct ev_audit a;
	uint64_t bits = 0;
	int cfd, fd, fd2, n, r;

	if (argc < 3) {
		fprintf(stderr, "usage: %s /dev/gpiochipN <sim sysfs dir>\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	g_sim = argv[2];
	test_audit_pure();
	cfd = open(argv[1], O_RDWR | O_CLOEXEC);
	CHECK(cfd >= 0);
	if (cfd < 0)
		goto out;

	printf("--- Part A1: find lines by name (no global numbers) ---\n");
	int led = gpio_find_line(cfd, "LED0"), btn = gpio_find_line(cfd, "BTN0");
	int rst = gpio_find_line(cfd, "IMU_RST_N"), drdy = gpio_find_line(cfd, "IMU_DRDY");
	int bus0 = gpio_find_line(cfd, "BUS0");
	CHECK(led == 0 && btn == 1 && rst == 2 && drdy == 3 && bus0 == 4);
	CHECK(gpio_find_line(cfd, "NO_SUCH_LINE") == -ENOENT);
	led = led < 0 ? 0 : led; btn = btn < 0 ? 1 : btn; rst = rst < 0 ? 2 : rst;
	drdy = drdy < 0 ? 3 : drdy; bus0 = bus0 < 0 ? 4 : bus0;

	printf("--- Part A2: output LED0, seen on the sim side ---\n");
	unsigned o_led = (unsigned)led;
	struct line_spec s_led = { &o_led, 1, OUT, 0, 0, 0, "b10-led" };
	fd = gpio_request(cfd, &s_led);
	CHECK(fd >= 0);
	CHECK(sim_value(o_led) == 0);
	CHECK(gpio_set(fd, 1, 1) == 0 && sim_value(o_led) == 1);
	CHECK(gpio_get(fd, 1, &bits) == 0 && bits == 1);
	int ok = 0;
	for (int k = 0; k < 10; k++)
		ok += gpio_set(fd, 1, (uint64_t)(k & 1)) == 0 && sim_value(o_led) == (k & 1);
	printf("toggles seen on sim side: %d/10\n", ok);
	CHECK(ok == 10);
	CHECK(line_info(cfd, o_led, &li) == 0 && (li.flags & GPIO_V2_LINE_FLAG_USED) &&
	      (li.flags & OUT) && strcmp(li.consumer, "b10-led") == 0);
	printf("--- Part B1: ownership = the fd ---\n");
	r = gpio_request(cfd, &s_led);
	printf("second request of LED0 while held: %d (%s)\n", r, strerror(-r));
	CHECK(r == -EBUSY);
	gpio_set(fd, 1, 1);
	if (fd >= 0)
		close(fd);
	CHECK(line_info(cfd, o_led, &li) == 0 && !(li.flags & GPIO_V2_LINE_FLAG_USED));
	CHECK(sim_value(o_led) == 0);	/* released: back to the pull (pull-down) */

	printf("--- Part A3: input BTN0, closed-loop edge events ---\n");
	unsigned o_btn = (unsigned)btn;
	struct line_spec s_btn = { &o_btn, 1, IN | EDGES, 0, 0, 0, "b10-btn" };
	fd = gpio_request(cfd, &s_btn);
	CHECK(fd >= 0);
	CHECK(gpio_get(fd, 1, &bits) == 0 && bits == 0);
	uint64_t t0 = now_ns(CLOCK_MONOTONIC), tw[12], td[12];
	int got = 0, causal = 0;
	for (int k = 0; k < 12; k++) {
		tw[k] = now_ns(CLOCK_MONOTONIC);
		sim_pull(o_btn, !(k & 1));		/* up, down, up, ... */
		r = gpio_wait_events(fd, ev + got, 1, 2000);
		td[k] = now_ns(CLOCK_MONOTONIC);
		if (r == 1) {
			causal += ev[got].timestamp_ns >= tw[k] && ev[got].timestamp_ns <= td[k];
			got++;
		}
		if (k == 0)
			CHECK(gpio_get(fd, 1, &bits) == 0 && bits == 1);
	}
	uint64_t t1 = now_ns(CLOCK_MONOTONIC);
	ev_audit(ev, got, 1, &a);
	for (int i = 0; i < got; i++)
		printf("  ev%-2d %-7s ts=%llu.%09llu seq=%u line_seq=%u off=%u dt=%lluus\n", i,
		       ev[i].id == R ? "RISING" : "FALLING",
		       (unsigned long long)(ev[i].timestamp_ns / 1000000000ULL),
		       (unsigned long long)(ev[i].timestamp_ns % 1000000000ULL),
		       ev[i].seqno, ev[i].line_seqno, ev[i].offset,
		       (unsigned long long)(i ? (ev[i].timestamp_ns - ev[i - 1].timestamp_ns) / 1000 : 0));
	printf("got=%d rising=%d falling=%d missing=%d backwards=%d same_dir=%d causal=%d\n",
	       got, a.rising, a.falling, a.seq_missing, a.ts_backwards, a.same_dir, causal);
	CHECK(got == 12);
	CHECK(a.rising == 6 && a.falling == 6);
	CHECK(got > 0 && ev[0].id == R && ev[0].line_seqno == 1 && ev[got - 1].seqno == (uint32_t)got);
	CHECK(a.seq_missing == 0 && a.ts_backwards == 0 && a.same_dir == 0);
	CHECK(causal == 12);			/* every ts inside [write, wakeup] */
	CHECK(got > 0 && ev[0].timestamp_ns > t0 && ev[got - 1].timestamp_ns < t1);
	CHECK(got > 0 && ev[got - 1].offset == o_btn);
	printf("--- Part B2: errors the kernel reports ---\n");
	r = gpio_set(fd, 1, 1);
	printf("SET_VALUES on an input line: %d (%s)\n", r, strerror(-r));
	CHECK(r == -EPERM);
	if (fd >= 0)
		close(fd);
	struct line_spec bad = { &o_btn, 1, GPIO_V2_LINE_FLAG_EDGE_RISING, 0, 0, 0, "b10" };
	r = gpio_request(cfd, &bad);
	printf("edge flag without INPUT: %d (%s)\n", r, strerror(-r));
	CHECK(r == -EINVAL);
	bad.flags = IN | OUT;
	CHECK(gpio_request(cfd, &bad) == -EINVAL);
	unsigned o_bad = 99;
	struct line_spec s99 = { &o_bad, 1, IN, 0, 0, 0, "b10" };
	r = gpio_request(cfd, &s99);
	printf("offset 99 on an 8-line chip: %d (%s)\n", r, strerror(-r));
	CHECK(r == -EINVAL);
	s99.offsets = &o_btn; s99.n = 0;
	CHECK(gpio_request(cfd, &s99) == -EINVAL);
	s99.n = 65;
	CHECK(gpio_request(cfd, &s99) == -EINVAL);
	memset(&li, 0, sizeof(li));
	li.offset = o_btn; li.padding[0] = 1;
	r = ioctl(cfd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0 ? -errno : 0;
	printf("LINEINFO with non-zero padding: %d (%s)\n", r, strerror(-r));
	CHECK(r == -EINVAL);

	printf("--- Part B3: rising-only ---\n");
	struct line_spec s_rise = { &o_btn, 1, IN | GPIO_V2_LINE_FLAG_EDGE_RISING, 0, 0, 0, "b10" };
	fd = gpio_request(cfd, &s_rise);
	CHECK(fd >= 0);
	got = 0;
	for (int k = 0; k < 6; k++) {
		sim_pull(o_btn, 1);
		r = gpio_wait_events(fd, ev + got, 1, 2000);
		got += r > 0 ? r : 0;
		sim_pull(o_btn, 0);
	}
	got += (r = gpio_wait_events(fd, ev + got, 8, 200)) > 0 ? r : 0;	/* nothing more */
	ev_audit(ev, got, 1, &a);
	printf("rising-only, 6 pulses: got=%d rising=%d falling=%d\n", got, a.rising, a.falling);
	CHECK(got == 6 && a.rising == 6 && a.falling == 0 && a.seq_missing == 0);
	if (fd >= 0)
		close(fd);

	printf("--- Part B4: event FIFO overflow (event_buffer_size=5) ---\n");
	struct line_spec s_ovf = { &o_btn, 1, IN | EDGES, 0, 0, 5, "b10" };
	fd = gpio_request(cfd, &s_ovf);
	CHECK(fd >= 0);
	for (int k = 0; k < 24; k++) {
		sim_pull(o_btn, !(k & 1));
		usleep(5000);			/* pacing only; verdict uses seqnos */
	}
	usleep(300000);
	n = gpio_wait_events(fd, ev, 64, 300);
	int first = n > 0 ? (int)ev[0].line_seqno : 0, last = n > 0 ? (int)ev[n - 1].line_seqno : 0;
	int inside = -1, dropped = -1;
	if (n > 0) {
		ev_audit(ev, n, ev[0].line_seqno, &a);
		inside = a.seq_missing;			/* gaps between received events */
		ev_audit(ev, n, 1, &a);
		dropped = a.seq_missing;		/* + the gap before the first one */
	}
	printf("read=%d first line_seq=%d last=%d gaps_inside=%d dropped=%d\n", n, first, last,
	       inside, dropped);
	CHECK(n == 8);				/* 5 rounded up to a power of two */
	CHECK(inside == 0);			/* overflow drops the OLDEST ... */
	CHECK(n > 0 && last - first == n - 1);	/* ... so the newest 8 are contiguous */
	CHECK(dropped == first - 1 && dropped > 0);
	if (fd >= 0)
		close(fd);

	printf("--- Part B5: software debounce 300 ms ---\n");
	struct line_spec s_deb = { &o_btn, 1, IN | EDGES, 0, 300000, 0, "b10-deb" };
	fd = gpio_request(cfd, &s_deb);
	CHECK(fd >= 0);
	CHECK(line_info(cfd, o_btn, &li) == 0 && info_debounce(&li) == 300000);
	uint64_t tl = 0;
	for (int k = 0; k < 9; k++) {		/* bounce: up down up ... up */
		tl = now_ns(CLOCK_MONOTONIC);
		sim_pull(o_btn, !(k & 1));
	}
	n = gpio_wait_events(fd, ev, 8, 1500);
	printf("9 bouncy writes -> %d event(s)", n);
	if (n > 0)
		printf(", first %s line_seq=%u, %llu ms after the last write", ev[0].id == R ? "RISING" : "FALLING",
		       ev[0].line_seqno, (unsigned long long)((ev[0].timestamp_ns - tl) / 1000000));
	printf("\n");
	CHECK(n == 1 && ev[0].id == R && ev[0].line_seqno == 1);
	CHECK(n > 0 && ev[0].timestamp_ns > tl);
	if (fd >= 0)
		close(fd);
	sim_pull(o_btn, 0);

	printf("--- Part C1: 4-line bus, one atomic SET_VALUES ---\n");
	unsigned o_bus[4] = { (unsigned)bus0, (unsigned)bus0 + 1, (unsigned)bus0 + 2, (unsigned)bus0 + 3 };
	struct line_spec s_bus = { o_bus, 4, OUT, 0x0, 0, 0, "b10-bus" };
	fd = gpio_request(cfd, &s_bus);
	CHECK(fd >= 0);
	CHECK(gpio_set(fd, 0xF, 0xA) == 0);
	int pat = 0;
	for (int i = 0; i < 4; i++)
		pat |= (sim_value(o_bus[i]) == 1) << i;
	printf("after set(mask=0xF,bits=0xA): sim pattern=0x%X\n", pat);
	CHECK(pat == 0xA);
	CHECK(gpio_set(fd, 0x1, 0x1) == 0 && sim_value(o_bus[0]) == 1 && sim_value(o_bus[1]) == 1 &&
	      sim_value(o_bus[2]) == 0 && sim_value(o_bus[3]) == 1);
	CHECK(gpio_get(fd, 0xF, &bits) == 0 && bits == 0xB);
	CHECK(gpio_set(fd, 0, 0) == -EINVAL);	/* empty mask */
	if (fd >= 0)
		close(fd);

	printf("--- Part C2: sensor bring-up: RST_N pulse -> wait DRDY rising ---\n");
	unsigned o_rst = (unsigned)rst, o_drdy = (unsigned)drdy;
	CHECK(sim_value(o_rst) == 1);		/* board pull-up holds RST_N released */
	struct line_spec s_drdy = { &o_drdy, 1, IN | GPIO_V2_LINE_FLAG_EDGE_RISING, 0, 0, 0, "imu-drdy" };
	fd2 = gpio_request(cfd, &s_drdy);
	CHECK(fd2 >= 0);
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {		/* the "sensor": sees reset, boots 20 ms, raises DRDY */
		int saw_low = 0, i;
		for (i = 0; i < 3000; i++) {
			int v = sim_value(o_rst);
			if (v == 0)
				saw_low = 1;
			if (v == 1 && saw_low)
				break;
			usleep(1000);
		}
		if (i == 3000)
			_exit(3);
		usleep(20000);
		sim_pull(o_drdy, 1);
		_exit(0);
	}
	struct line_spec s_rst = { &o_rst, 1, OUT | GPIO_V2_LINE_FLAG_ACTIVE_LOW, 1, 0, 0, "imu-rst" };
	fd = gpio_request(cfd, &s_rst);		/* logical 1 = asserted = physical 0 */
	CHECK(fd >= 0);
	CHECK(sim_value(o_rst) == 0);
	CHECK(gpio_get(fd, 1, &bits) == 0 && bits == 1);
	usleep(100000);				/* reset pulse width */
	uint64_t trel = now_ns(CLOCK_MONOTONIC);
	CHECK(gpio_set(fd, 1, 0) == 0 && sim_value(o_rst) == 1);
	n = gpio_wait_events(fd2, ev, 1, 3000);
	int st = -1;
	if (pid > 0)
		waitpid(pid, &st, 0);
	printf("DRDY events=%d", n);
	if (n == 1)
		printf(" id=%s, %llu ms after reset release", ev[0].id == R ? "RISING" : "FALLING",
		       (unsigned long long)((ev[0].timestamp_ns - trel) / 1000000));
	printf(", sensor exit=%d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	CHECK(n == 1 && ev[0].id == R && ev[0].offset == o_drdy);
	CHECK(n == 1 && ev[0].timestamp_ns >= trel + 20000000ULL);	/* sensor boots >= 20 ms */
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	if (fd >= 0)
		close(fd);
	if (fd2 >= 0)
		close(fd2);
	CHECK(sim_value(o_rst) == 1);		/* released again: pull-up wins */
	sim_pull(o_drdy, 0);
	close(cfd);
out:
	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
