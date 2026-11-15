/* ENV: qemu */
/*
 * ELX-B07 user test for /dev/kecho. Built in the VM with
 *   gcc -std=gnu11 -Wall -Wextra -Werror -O2 -static kecho_test.c -o kecho_test
 * and run inside the QEMU guest (check.sh does both).
 *
 *   kecho_test base        Part A/B/C checks on a freshly loaded kecho
 *   kecho_test practice    base + practice.c extras (P1..P4)
 *   kecho_test stress N    only the 2-writer/1-reader tear test, N rounds
 *
 * Every syscall wrapper returns the result or -errno, so a check prints
 * "got -28 want -28" instead of a bare -1.
 */
#define _GNU_SOURCE		/* SEEK_DATA, SEEK_HOLE */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

#define KSIZE 4096
#define DEV "/dev/kecho"

static int pass, fail;
static int practice_mode;	/* practice.c accepts SEEK_DATA/SEEK_HOLE */

#define CHECK(name, expr) do {						\
	if (expr) { pass++; printf("PASS: %s\n", name); }		\
	else { fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

#define CHECKV(name, got, want) do {					\
	long g_ = (long)(got), w_ = (long)(want);			\
	if (g_ == w_) { pass++; printf("PASS: %-40s = %ld\n", name, g_); } \
	else { fail++; printf("FAIL: %-40s got %ld want %ld (line %d)\n", \
			      name, g_, w_, __LINE__); }		\
} while (0)

static long rd(int fd, void *b, size_t n)
{
	ssize_t r = read(fd, b, n);
	return r < 0 ? -errno : r;
}

static long wr(int fd, const void *b, size_t n)
{
	ssize_t r = write(fd, b, n);
	return r < 0 ? -errno : r;
}

static long sk(int fd, off_t off, int whence)
{
	off_t r = lseek(fd, off, whence);
	return r < 0 ? -errno : (long)r;
}

static long opn(const char *path, int flags)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}

static unsigned char model[KSIZE];	/* what the device should hold */
static size_t model_len;
static unsigned char buf[2 * KSIZE];

/* write through fd at pos and mirror it into the model */
static long wr_at(int fd, long pos, const void *b, size_t n)
{
	long r;

	if (sk(fd, pos, SEEK_SET) != pos)
		return -1000;
	r = wr(fd, b, n);
	if (r > 0) {
		memcpy(model + pos, b, r);
		if ((size_t)(pos + r) > model_len)
			model_len = pos + r;
	}
	return r;
}

/* read the whole device through fd and compare with the model */
static int dev_matches_model(int fd)
{
	long got = 0, r;

	if (sk(fd, 0, SEEK_SET) != 0)
		return 0;
	while ((r = rd(fd, buf + got, sizeof(buf) - got)) > 0)
		got += r;
	return r == 0 && got == (long)model_len && !memcmp(buf, model, model_len);
}

static long proc_devices_major(const char *name)
{
	char line[128], n[64];
	long maj = -1, m;
	FILE *f = fopen("/proc/devices", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%ld %63s", &m, n) == 2 && !strcmp(n, name))
			maj = m;	/* char section comes first: keep first hit */
	fclose(f);
	return maj;
}

/* ---- Part A: registration, write/read at *ppos, partial reads ---------- */
static int part_a(void)
{
	struct stat st;
	char sysdev[32] = "", want[32];
	FILE *f;
	long fd = opn(DEV, O_RDWR), fd2, r, total, calls, last;

	CHECK("A0 open " DEV " O_RDWR", fd >= 0);
	if (fd < 0)
		return -1;
	CHECK("A0 fstat: char device", !fstat(fd, &st) && S_ISCHR(st.st_mode));
	CHECKV("A0 major == /proc/devices kecho", major(st.st_rdev),
	       proc_devices_major("kecho"));
	CHECKV("A0 minor", minor(st.st_rdev), 0);
	f = fopen("/sys/class/kecho/kecho/dev", "r");
	if (f) {
		if (!fgets(sysdev, sizeof(sysdev), f))
			sysdev[0] = 0;
		fclose(f);
	}
	snprintf(want, sizeof(want), "%u:0\n", major(st.st_rdev));
	CHECK("A0 /sys/class/kecho/kecho/dev == MAJ:0", !strcmp(sysdev, want));

	CHECKV("A1 read on fresh device (EOF)", rd(fd, buf, 64), 0);
	CHECKV("A1 write \"hello\"", wr_at(fd, 0, "hello", 5), 5);
	CHECKV("A1 f_pos after write", sk(fd, 0, SEEK_CUR), 5);
	CHECKV("A1 read at f_pos 5 (EOF)", rd(fd, buf, 64), 0);
	CHECKV("A1 lseek SET 0", sk(fd, 0, SEEK_SET), 0);
	r = rd(fd, buf, 64);
	CHECK("A1 read back \"hello\"", r == 5 && !memcmp(buf, "hello", 5));
	CHECKV("A1 write at 10 (leaves a hole)", wr_at(fd, 10, "W", 1), 1);
	CHECKV("A1 SEEK_END 0 == len", sk(fd, 0, SEEK_END), 11);
	CHECK("A1 hole reads as zeros", dev_matches_model(fd) &&
	      !memcmp(buf, "hello\0\0\0\0\0W", 11));

	fd2 = opn(DEV, O_RDWR);
	CHECKV("A2 second open: own f_pos starts at 0", sk(fd2, 0, SEEK_CUR), 0);
	r = rd(fd2, buf, 5);
	CHECK("A2 second fd sees the same data", r == 5 && !memcmp(buf, "hello", 5));
	close(fd2);

	for (int i = 0; i < KSIZE; i++)
		buf[KSIZE + i] = (unsigned char)(i * 7 + 3);
	CHECKV("A3 write 4096-byte pattern", wr_at(fd, 0, buf + KSIZE, KSIZE), KSIZE);
	CHECKV("A3 f_pos after full write", sk(fd, 0, SEEK_CUR), KSIZE);
	sk(fd, 0, SEEK_SET);
	CHECKV("A3 read(1)", rd(fd, buf, 1), 1);
	CHECKV("A3 read(7)", rd(fd, buf + 1, 7), 7);
	CHECKV("A3 read(4096) -> short read", rd(fd, buf + 8, KSIZE), KSIZE - 8);
	CHECKV("A3 read again -> EOF", rd(fd, buf, 1), 0);
	CHECK("A3 1+7+4088 bytes == pattern", !memcmp(buf, model, KSIZE));

	sk(fd, 0, SEEK_SET);
	total = calls = last = 0;
	while ((r = rd(fd, buf + total, 7)) > 0) {
		total += r;
		calls++;
		last = r;
	}
	CHECKV("A3 7-byte chunks: total bytes", total, KSIZE);
	CHECKV("A3 7-byte chunks: non-zero reads", calls, 586);
	CHECKV("A3 7-byte chunks: last read", last, 1);
	CHECK("A3 7-byte chunks == pattern", r == 0 && !memcmp(buf, model, KSIZE));

	sk(fd, 10, SEEK_SET);
	CHECKV("A4 read(fd, buf, 0)", rd(fd, buf, 0), 0);
	CHECKV("A4 write(fd, buf, 0)", wr(fd, buf, 0), 0);
	CHECKV("A4 f_pos unchanged by 0-byte I/O", sk(fd, 0, SEEK_CUR), 10);
	return fd;
}

/* ---- Part B: end of buffer, llseek, bad pointers ------------------------ */
static void part_b(int fd)
{
	unsigned char *p;
	long r;

	CHECKV("B1 SEEK_END 0", sk(fd, 0, SEEK_END), KSIZE);
	CHECKV("B1 write at 4096 -> -ENOSPC", wr(fd, "x", 1), -ENOSPC);
	CHECKV("B1 f_pos stays 4096", sk(fd, 0, SEEK_CUR), KSIZE);
	memset(buf, 'E', 200);
	CHECKV("B1 write 200 at 4000 -> short 96", wr_at(fd, 4000, buf, 200), 96);
	CHECKV("B1 f_pos after short write", sk(fd, 0, SEEK_CUR), KSIZE);
	CHECKV("B1 next write -> -ENOSPC", wr(fd, buf, 200), -ENOSPC);
	CHECK("B1 device == model after short write", dev_matches_model(fd));

	sk(fd, 123, SEEK_SET);
	CHECKV("B2 SEEK_SET 4097 -> -EINVAL", sk(fd, 4097, SEEK_SET), -EINVAL);
	CHECKV("B2 SEEK_SET -1 -> -EINVAL", sk(fd, -1, SEEK_SET), -EINVAL);
	CHECKV("B2 f_pos unchanged after bad seek", sk(fd, 0, SEEK_CUR), 123);
	CHECKV("B2 SEEK_SET 4096 ok", sk(fd, 4096, SEEK_SET), 4096);
	CHECKV("B2 SEEK_SET 100", sk(fd, 100, SEEK_SET), 100);
	CHECKV("B2 SEEK_CUR +10", sk(fd, 10, SEEK_CUR), 110);
	CHECKV("B2 SEEK_CUR -200 -> -EINVAL", sk(fd, -200, SEEK_CUR), -EINVAL);
	CHECKV("B2 SEEK_END -1", sk(fd, -1, SEEK_END), KSIZE - 1);
	CHECKV("B2 SEEK_END +1 -> -EINVAL", sk(fd, 1, SEEK_END), -EINVAL);
	CHECKV("B2 whence 99 -> -EINVAL (VFS, > SEEK_MAX)", sk(fd, 0, 99), -EINVAL);
	if (!practice_mode)
		CHECKV("B2 SEEK_DATA -> -EINVAL (driver default)", sk(fd, 0, SEEK_DATA), -EINVAL);
	r = pread(fd, buf, 5, 4000);
	CHECK("B2 pread(5 @4000) leaves f_pos alone",
	      r == 5 && !memcmp(buf, model + 4000, 5) && sk(fd, 0, SEEK_CUR) == KSIZE - 1);

	sk(fd, 0, SEEK_SET);
	CHECKV("B3 write from (void *)8 -> -EFAULT", wr(fd, (void *)8, 16), -EFAULT);
	CHECKV("B3 read into (void *)8 -> -EFAULT", rd(fd, (void *)8, 16), -EFAULT);
	CHECKV("B3 read into kernel address -> -EFAULT",
	       rd(fd, (void *)0xffff800010000000UL, 16), -EFAULT);
	CHECKV("B3 f_pos unchanged after EFAULT", sk(fd, 0, SEEK_CUR), 0);
	if (pread(fd, buf, 8, 0) == 8)
		printf("     B3 device[0..7] after the EFAULT write: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
	CHECK("B3 device unchanged after EFAULT", dev_matches_model(fd));

	/* a user buffer whose 2nd page is PROT_NONE: copies stop half way */
	p = mmap(NULL, 2 * KSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED || mprotect(p + KSIZE, KSIZE, PROT_NONE)) {
		CHECK("B4 mmap/mprotect", 0);
		return;
	}
	sk(fd, 0, SEEK_SET);
	r = rd(fd, p + KSIZE - 100, 300);
	printf("     B4 read(300) into last 100 bytes of a page returned %ld\n", r);
	CHECK("B4 partial read: 1..100 bytes, not -EFAULT", r >= 1 && r <= 100);
	CHECKV("B4 partial read: f_pos == bytes returned", sk(fd, 0, SEEK_CUR), r);
	CHECK("B4 partial read: bytes are right", r > 0 && !memcmp(p + KSIZE - 100, model, r));

	memset(p + KSIZE - 100, 'Q', 100);
	sk(fd, 0, SEEK_SET);
	r = wr(fd, p + KSIZE - 100, 300);
	printf("     B4 write(300) from last 100 bytes of a page returned %ld\n", r);
	CHECK("B4 partial write: 1..100 bytes, not -EFAULT", r >= 1 && r <= 100);
	CHECKV("B4 partial write: f_pos == bytes returned", sk(fd, 0, SEEK_CUR), r);
	if (r > 0)
		memset(model, 'Q', r);
	CHECK("B4 partial write: bytes after it NOT clobbered", dev_matches_model(fd));
	munmap(p, 2 * KSIZE);
}

/* ---- Part C: f_pos per struct file, shared data, mutex ------------------- */
static long stress(int rounds)
{
	long fdr = opn(DEV, O_RDWR), torn = 0, shorts = 0, bad_children = 0;
	pid_t kid[2];

	if (fdr < 0)
		return -1;
	memset(buf, 'A', KSIZE);		/* start uniform: any mix is a tear */
	if (pwrite(fdr, buf, KSIZE, 0) != KSIZE)
		return -1;
	for (int k = 0; k < 2; k++) {
		kid[k] = fork();
		if (kid[k] == 0) {
			unsigned char w[KSIZE];
			long fdw = opn(DEV, O_RDWR), bad = 0;

			memset(w, k ? 'B' : 'A', KSIZE);
			for (int i = 0; i < rounds; i++)
				if (fdw < 0 || pwrite(fdw, w, KSIZE, 0) != KSIZE)
					bad++;
			_exit(bad ? 1 : 0);
		}
	}
	for (int i = 0; i < rounds; i++) {
		if (pread(fdr, buf, KSIZE, 0) != KSIZE) {
			shorts++;
			continue;
		}
		for (int j = 1; j < KSIZE; j++)
			if (buf[j] != buf[0]) {
				torn++;
				break;
			}
	}
	for (int k = 0; k < 2; k++) {
		int st = 1;

		waitpid(kid[k], &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st))
			bad_children++;
	}
	close(fdr);
	printf("     C3 stress: rounds=%d torn=%ld short=%ld bad_writers=%ld\n",
	       rounds, torn, shorts, bad_children);
	return torn + shorts + bad_children;
}

static void part_c(int fd)
{
	long fd2 = opn(DEV, O_RDWR), fd3, r;
	pid_t pid;
	int st = 1;

	sk(fd2, 5, SEEK_SET);
	CHECKV("C1 write via fd1 at 50", wr_at(fd, 50, "zz", 2), 2);
	CHECKV("C1 fd2 f_pos untouched by fd1", sk(fd2, 0, SEEK_CUR), 5);
	sk(fd2, 50, SEEK_SET);
	r = rd(fd2, buf, 2);
	CHECK("C1 fd2 reads what fd1 wrote", r == 2 && !memcmp(buf, "zz", 2));
	close(fd2);

	fd3 = dup(fd);
	sk(fd3, 77, SEEK_SET);
	CHECKV("C2 dup() shares f_pos", sk(fd, 0, SEEK_CUR), 77);
	close(fd3);

	sk(fd, 0, SEEK_SET);
	pid = fork();
	if (pid == 0)
		_exit(rd(fd, buf, 10) == 10 ? 0 : 1);
	waitpid(pid, &st, 0);
	CHECK("C2 child read 10 via inherited fd", WIFEXITED(st) && !WEXITSTATUS(st));
	CHECKV("C2 parent f_pos moved by child (fork shares struct file)",
	       sk(fd, 0, SEEK_CUR), 10);

	CHECKV("C3 2 writers + 1 reader, 4096 B each: torn+short+bad", stress(200), 0);
	memcpy(buf + KSIZE, model, KSIZE);
	CHECKV("C3 restore pattern after stress", wr_at(fd, 0, buf + KSIZE, KSIZE), KSIZE);
}

/* ---- practice.c extras ---------------------------------------------------- */
static void practice(void)
{
	struct stat s0, s1, sm;
	long a, b, r;

	a = opn("/dev/kecho1", O_RDWR);
	CHECK("P1 /dev/kecho1 opens", a >= 0);
	CHECK("P1 kecho1: same major, minor 1",
	      !stat(DEV, &s0) && !stat("/dev/kecho1", &s1) &&
	      major(s0.st_rdev) == major(s1.st_rdev) && minor(s1.st_rdev) == 1);
	CHECKV("P1 write \"one\" to kecho1", wr(a, "one", 3), 3);
	b = opn(DEV, O_RDONLY);
	CHECK("P1 kecho (minor 0) untouched", dev_matches_model(b));
	close(b);
	close(a);

	a = opn("/dev/kecho1", O_WRONLY | O_TRUNC);
	CHECK("P2 open O_WRONLY|O_TRUNC", a >= 0);
	close(a);
	a = opn("/dev/kecho1", O_RDONLY);
	CHECKV("P2 after O_TRUNC: read -> EOF", rd(a, buf, 16), 0);
	close(a);
	a = opn("/dev/kecho1", O_WRONLY);
	CHECKV("P2 write \"abc\"", wr(a, "abc", 3), 3);
	close(a);
	a = opn("/dev/kecho1", O_WRONLY | O_APPEND);
	CHECKV("P2 O_APPEND write \"de\"", wr(a, "de", 2), 2);
	sk(a, 0, SEEK_SET);
	CHECKV("P2 O_APPEND ignores f_pos 0", wr(a, "f", 1), 1);
	CHECKV("P2 f_pos after append", sk(a, 0, SEEK_CUR), 6);
	close(a);
	a = opn("/dev/kecho1", O_RDONLY);
	r = rd(a, buf, 64);
	CHECK("P2 kecho1 holds \"abcdef\"", r == 6 && !memcmp(buf, "abcdef", 6));

	CHECKV("P3 SEEK_HOLE from 0 -> len", sk(a, 0, SEEK_HOLE), 6);
	CHECKV("P3 SEEK_DATA from 2", sk(a, 2, SEEK_DATA), 2);
	CHECKV("P3 SEEK_DATA at len -> -ENXIO", sk(a, 6, SEEK_DATA), -ENXIO);
	CHECKV("P3 SEEK_END -1", sk(a, -1, SEEK_END), 5);
	CHECKV("P3 SEEK_SET 4097 -> -EINVAL", sk(a, 4097, SEEK_SET), -EINVAL);
	close(a);

	a = opn("/dev/kecho_misc", O_RDWR);
	CHECK("P4 /dev/kecho_misc opens", a >= 0);
	CHECK("P4 misc: major 10", !fstat(a, &sm) && major(sm.st_rdev) == 10);
	CHECKV("P4 write \"misc\"", wr(a, "misc", 4), 4);
	sk(a, 0, SEEK_SET);
	r = rd(a, buf, 64);
	CHECK("P4 misc reads back its own 4 bytes", r == 4 && !memcmp(buf, "misc", 4));
	close(a);
	b = opn(DEV, O_RDONLY);
	CHECK("P4 kecho (minor 0) still untouched", dev_matches_model(b));
	close(b);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "base";
	int fd;

	setvbuf(stdout, NULL, _IOLBF, 0);
	practice_mode = !strcmp(mode, "practice");
	if (!strcmp(mode, "stress")) {
		long bad = stress(argc > 2 ? atoi(argv[2]) : 200);

		CHECKV("stress torn+short+bad", bad, 0);
	} else {
		fd = part_a();
		if (fd >= 0) {
			part_b(fd);
			part_c(fd);
			close(fd);
		}
		if (practice_mode)
			practice();
	}
	printf("%d/%d passed\n", pass, pass + fail);
	return fail ? 1 : 0;
}
