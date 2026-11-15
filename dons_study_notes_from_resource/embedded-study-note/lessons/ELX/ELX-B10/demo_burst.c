/* ENV: qemu */
/*
 * ELX-B10 demo (not graded): things the md shows but the test does not assert.
 *   demo_burst <chip> <simdir> hold       -> hold 2 lines, print debugfs + /proc/interrupts
 *   demo_burst <chip> <simdir> burst K    -> K back-to-back pull writes, count what arrives
 *   demo_burst <chip> <simdir> hte        -> EVENT_CLOCK_HTE with no HTE provider
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/gpio.h>

static int req1(int cfd, unsigned off, unsigned long long flags, unsigned evbuf, const char *who)
{
	struct gpio_v2_line_request r;

	memset(&r, 0, sizeof(r));
	r.offsets[0] = off;
	r.num_lines = 1;
	r.event_buffer_size = evbuf;
	r.config.flags = flags;
	snprintf(r.consumer, sizeof(r.consumer), "%s", who);
	if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
		r.config.num_attrs = 1;
		r.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		r.config.attrs[0].attr.values = 1;
		r.config.attrs[0].mask = 1;
	}
	return ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &r) < 0 ? -errno : r.fd;
}

static void cat_grep(const char *path, const char *pat)
{
	char line[256];
	FILE *f = fopen(path, "r");

	if (!f) {
		printf("%s: %s\n", path, strerror(errno));
		return;
	}
	while (fgets(line, sizeof(line), f))
		if (!pat || strstr(line, pat))
			fputs(line, stdout);
	fclose(f);
}

int main(int argc, char **argv)
{
	unsigned long long both = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
				  GPIO_V2_LINE_FLAG_EDGE_FALLING;
	char pull[256];
	int cfd, fd;

	if (argc < 4)
		return 2;
	cfd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (cfd < 0)
		return perror(argv[1]), 1;
	snprintf(pull, sizeof(pull), "%s/sim_gpio1/pull", argv[2]);

	if (!strcmp(argv[3], "hold")) {
		int a = req1(cfd, 0, GPIO_V2_LINE_FLAG_OUTPUT, 0, "b10-led");
		int b = req1(cfd, 1, both, 0, "b10-btn");

		printf("held LED0 fd=%d BTN0 fd=%d\n", a, b);
		cat_grep("/sys/kernel/debug/gpio", NULL);
		printf("/proc/interrupts lines with b10 in the name:\n");
		cat_grep("/proc/interrupts", "b10");
		close(a);
		close(b);
	} else if (!strcmp(argv[3], "burst") && argc > 4) {
		int k = atoi(argv[4]), pfd, got = 0, same = 0, last_id = -1;
		unsigned last_seq = 0;
		struct gpio_v2_line_event ev[64];

		fd = req1(cfd, 1, both, 1024, "b10-burst");
		pfd = open(pull, O_WRONLY);
		if (fd < 0 || pfd < 0)
			return printf("setup failed %d %d\n", fd, pfd), 1;
		for (int i = 0; i < k; i++)	/* up, down, up, ... as fast as sysfs allows */
			if (pwrite(pfd, i & 1 ? "pull-down" : "pull-up", i & 1 ? 9 : 7, 0) < 0)
				return perror("pull"), 1;
		usleep(300000);
		for (;;) {
			struct pollfd p = { .fd = fd, .events = POLLIN };
			ssize_t r;

			if (poll(&p, 1, 200) <= 0)
				break;
			r = read(fd, ev, sizeof(ev));
			if (r <= 0)
				break;
			for (int i = 0; i < (int)(r / (ssize_t)sizeof(ev[0])); i++, got++) {
				same += (int)ev[i].id == last_id;
				last_id = (int)ev[i].id;
				last_seq = ev[i].line_seqno;
			}
		}
		printf("burst K=%-5d generated(last line_seqno)=%-5u delivered=%-5d merged=%-4d same_dir=%d\n",
		       k, last_seq, got, k - (int)last_seq, same);
		if (k & 1)
			(void)!pwrite(pfd, "pull-down", 9, 0);
		close(pfd);
		close(fd);
	} else if (!strcmp(argv[3], "hte")) {
		fd = req1(cfd, 1, both | GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE, 0, "b10-hte");
		printf("request with EVENT_CLOCK_HTE: %d (%s)\n", fd, fd < 0 ? strerror(-fd) : "ok");
		if (fd >= 0)
			close(fd);
	}
	close(cfd);
	return 0;
}
