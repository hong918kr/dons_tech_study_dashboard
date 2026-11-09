/* ENV: qemu */
/* fire: raise COUNT rising edges on a gpio-sim line, one at a time.
 * After each rising edge it waits until the module's `done` counter advances,
 * so bottom halves never coalesce during a run. Bounded: exits 3 on timeout.
 * usage: fire <pull_attr> <done_attr> <count> <timeout_ms_per_edge> [gap_us]
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static long read_counter(int fd)
{
	char buf[32];
	ssize_t r = pread(fd, buf, sizeof(buf) - 1, 0);

	if (r <= 0)
		return -1;
	buf[r] = 0;
	return atol(buf);
}

static void write_pull(int fd, const char *v, size_t len)
{
	if (pwrite(fd, v, len, 0) != (ssize_t)len) {
		perror("pull");
		exit(4);
	}
}

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: fire pull done count timeout_ms [gap_us]\n");
		return 2;
	}
	int fd_pull = open(argv[1], O_WRONLY);
	int fd_done = open(argv[2], O_RDONLY);
	long count = atol(argv[3]), timeout = atol(argv[4]);
	long gap = argc > 5 ? atol(argv[5]) : 200;

	if (fd_pull < 0 || fd_done < 0) {
		perror("open");
		return 2;
	}
	long base = read_counter(fd_done);

	for (long i = 1; i <= count; i++) {
		write_pull(fd_pull, "pull-up\n", 8);
		long deadline = now_ms() + timeout;

		while (read_counter(fd_done) < base + i) {
			if (now_ms() > deadline) {
				printf("fire: TIMEOUT waiting for edge %ld of %ld\n", i, count);
				return 3;
			}
			usleep(100);
		}
		write_pull(fd_pull, "pull-down\n", 10);
		if (gap)
			usleep((useconds_t)gap);
	}
	printf("fire: %ld edges done\n", count);
	return 0;
}
