/* ENV: qemu */
/*
 * ELX-B09 guest helper (gcc -static), two jobs the shell can't do:
 *   systest poll <devdir>   poll(POLLPRI) on <devdir>/count, CHECK harness
 *   systest zero <file>     write(fd, "", 0) and print ZERO_RET=<ret>
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int pass, total;
#define CHECK(cond) do { total++; if (cond) { pass++; printf("PASS: poll: %s\n", #cond); } \
	else printf("FAIL: poll: %s (line %d)\n", #cond, __LINE__); } while (0)

static char path_count[256], path_sample[256], path_thr[256];

static int put(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY);
	ssize_t n;

	if (fd < 0)
		return -errno;
	n = write(fd, s, strlen(s));
	close(fd);
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

/* pread at offset 0: re-arms kernfs' "last seen event" for this fd */
static long readval(int fd)
{
	char b[32];
	ssize_t n = pread(fd, b, sizeof(b) - 1, 0);

	if (n <= 0)
		return -1;
	b[n] = '\0';
	return strtol(b, NULL, 10);
}

static int poll_pri(int fd, int timeout_ms, short *rev)
{
	struct pollfd p = { .fd = fd, .events = POLLPRI | POLLERR };
	int r = poll(&p, 1, timeout_ms);

	*rev = p.revents;
	return r;
}

static int do_poll(const char *dir)
{
	short rev = 0;
	long n0;
	pid_t pid;
	int fd, r, st = -1;

	snprintf(path_count, sizeof(path_count), "%s/count", dir);
	snprintf(path_sample, sizeof(path_sample), "%s/sample", dir);
	snprintf(path_thr, sizeof(path_thr), "%s/threshold", dir);

	CHECK(put(path_sample, "0\n") == 0);		/* known state: below */
	CHECK(put(path_thr, "500\n") == 0);
	fd = open(path_count, O_RDONLY);
	CHECK(fd >= 0);
	if (fd < 0)
		return 1;
	n0 = readval(fd);
	CHECK(n0 >= 0);

	/* sysfs files are always "readable": POLLIN tells you nothing */
	{
		struct pollfd p = { .fd = fd, .events = POLLIN };

		CHECK(poll(&p, 1, 0) == 1 && (p.revents & POLLIN));
	}
	CHECK(poll_pri(fd, 0, &rev) == 0);		/* nothing new since read */

	pid = fork();
	if (pid == 0)
		_exit(put(path_sample, "700\n") == 0 ? 0 : 1);	/* rising edge */
	r = poll_pri(fd, 5000, &rev);			/* sleeps until notify */
	CHECK(r == 1);
	CHECK((rev & POLLPRI) && (rev & POLLERR));
	waitpid(pid, &st, 0);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	CHECK(readval(fd) == n0 + 1);
	CHECK(poll_pri(fd, 0, &rev) == 0);		/* re-read cleared it */

	CHECK(put(path_sample, "800\n") == 0);		/* still above: no edge */
	CHECK(poll_pri(fd, 0, &rev) == 0);
	CHECK(put(path_sample, "100\n") == 0);		/* falling: not notified */
	CHECK(poll_pri(fd, 0, &rev) == 0);
	CHECK(put(path_sample, "900\n") == 0);		/* rising again */
	CHECK(poll_pri(fd, 0, &rev) == 1);
	CHECK(readval(fd) == n0 + 2);
	close(fd);

	printf("poll: %d/%d passed\n", pass, total);
	return pass == total ? 0 : 1;
}

static int do_zero(const char *file)
{
	int fd = open(file, O_WRONLY);
	ssize_t n;

	if (fd < 0) {
		printf("ZERO_RET=open:%s\n", strerror(errno));
		return 1;
	}
	n = write(fd, "", 0);
	printf("ZERO_RET=%zd%s%s\n", n, n < 0 ? ":" : "", n < 0 ? strerror(errno) : "");
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "poll"))
		return do_poll(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "zero"))
		return do_zero(argv[2]);
	fprintf(stderr, "usage: systest poll <devdir> | zero <file>\n");
	return 2;
}
