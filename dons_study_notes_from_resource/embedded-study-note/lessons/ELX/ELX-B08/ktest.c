/* ENV: qemu */
/*
 * ELX-B08 guest test for /dev/kecho ioctls. 같은 소스를 두 번 빌드한다:
 *   gcc -static ...                  -> /ktest   (AArch64, native ioctl 경로)
 *   arm-linux-gnueabihf-gcc -static  -> /ktest32 (AArch32, compat ioctl 경로)
 * usage: ktest base | practice
 * 출력: PASS:/FAIL: 줄 + "N/M passed", 실패가 있으면 exit 1.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "kecho_ioctl.h"

#if defined(__aarch64__)
#define ABI "a64"
#elif defined(__arm__)
#define ABI "a32"
#else
#define ABI "???"
#endif

/* 컴파일 시점 검사: 32비트 빌드에서도 같은 레이아웃이어야 한다 */
_Static_assert(sizeof(struct kecho_stats) == 40, "kecho_stats must be 40 bytes");
_Static_assert(offsetof(struct kecho_stats, __pad) == 36, "__pad at 36");
_Static_assert(sizeof(struct kecho_peek) == 16, "kecho_peek must be 16 bytes");

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS: [%s] %s\n", ABI, #expr); } \
	else printf("FAIL: [%s] %s  (line %d)\n", ABI, #expr, __LINE__); } while (0)

/* ioctl 결과를 0 또는 -errno 로 */
static int io(int fd, unsigned long cmd, void *arg)
{
	return ioctl(fd, cmd, arg) == 0 ? 0 : -errno;
}

/* ---------------- 번호 해부: 비트를 손으로 잘라 매크로와 비교 ---------------- */
struct cmdinfo {
	const char *name;
	unsigned int cmd, expect, dir, size, nr;
};

static void bits(char *out, unsigned int v)	/* "dd ssssssssssssss tttttttt nnnnnnnn" */
{
	int i, k = 0;

	for (i = 31; i >= 0; i--) {
		out[k++] = (char)('0' + ((v >> i) & 1));
		if (i == 30 || i == 16 || i == 8)
			out[k++] = ' ';
	}
	out[k] = '\0';
}

static void decode_table(const struct cmdinfo *t, int n)
{
	char b[40];
	int i;

	printf("%-20s %-10s %-3s %-4s %-4s %-2s  bits(dir|size|type|nr)\n",
	       "name", "value", "dir", "size", "type", "nr");
	for (i = 0; i < n; i++) {
		unsigned int c = t[i].cmd;
		unsigned int dir = c >> 30, size = (c >> 16) & 0x3fff;
		unsigned int type = (c >> 8) & 0xff, nr = c & 0xff;

		bits(b, c);
		printf("%-20s 0x%08x %-3u %-4u '%c'  %-2u  %s\n", t[i].name, c, dir, size, type, nr, b);
		CHECK(c == t[i].expect);
		CHECK(dir == _IOC_DIR(c) && size == _IOC_SIZE(c) && type == _IOC_TYPE(c) && nr == _IOC_NR(c));
		CHECK(dir == t[i].dir && size == t[i].size && type == KECHO_IOC_MAGIC && nr == t[i].nr);
	}
}

static void layout_table(void)
{
	printf("ABI %s: sizeof(long)=%u sizeof(void *)=%u sizeof(struct kecho_stats)=%u\n", ABI,
	       (unsigned)sizeof(long), (unsigned)sizeof(void *), (unsigned)sizeof(struct kecho_stats));
	printf("  offsetof: bytes_written=%u bytes_read=%u nwrites=%u nreads=%u data_len=%u mode=%u bad_cmds=%u __pad=%u\n",
	       (unsigned)offsetof(struct kecho_stats, bytes_written),
	       (unsigned)offsetof(struct kecho_stats, bytes_read),
	       (unsigned)offsetof(struct kecho_stats, nwrites),
	       (unsigned)offsetof(struct kecho_stats, nreads),
	       (unsigned)offsetof(struct kecho_stats, data_len),
	       (unsigned)offsetof(struct kecho_stats, mode),
	       (unsigned)offsetof(struct kecho_stats, bad_cmds),
	       (unsigned)offsetof(struct kecho_stats, __pad));
	CHECK(sizeof(struct kecho_stats) == 40);
	CHECK(offsetof(struct kecho_stats, nwrites) == 16 && offsetof(struct kecho_stats, mode) == 28);
}

/* ---------------- 틀린 번호들 ---------------- */
struct kecho_stats_v0 {			/* "옛 앱": bad_cmds/__pad 가 없던 32 바이트 버전 */
	__u64 bytes_written, bytes_read;
	__u32 nwrites, nreads, data_len, mode;
};
#define BAD_SIZE	_IOR(KECHO_IOC_MAGIC, 1, struct kecho_stats_v0)	/* size 32 */
#define BAD_DIR		_IOW(KECHO_IOC_MAGIC, 1, struct kecho_stats)	/* dir W */
#define BAD_NONE	_IO(KECHO_IOC_MAGIC, 1)				/* dir none, size 0 */
#define BAD_NR		_IOR(KECHO_IOC_MAGIC, 9, struct kecho_stats)	/* 없는 nr */
#define BAD_NR0		_IO(KECHO_IOC_MAGIC, 0)				/* nr 0 */
#define BAD_TYPE	_IOR('y', 1, struct kecho_stats)		/* 다른 magic */

static int get_stats(int fd, struct kecho_stats *st)
{
	memset(st, 0xA5, sizeof(*st));		/* 커널이 안 채운 바이트가 보이게 */
	return io(fd, KECHO_IOC_GET_STATS, st);
}

static void test_base(void)
{
	struct kecho_stats st;
	struct { struct kecho_stats_v0 v0; __u64 canary; } cb;
	struct { unsigned char kt[36]; __u32 canary; } tb;	/* 커널 struct termios = 36 B */
	char buf[64];
	__u32 m;
	char *pg, *ro;
	int fd = open("/dev/kecho", O_RDWR);

	CHECK(fd >= 0);
	if (fd < 0)
		return;
	/* Part A: 정상 경로 (이전 실행의 상태를 지우고 시작) */
	CHECK(io(fd, KECHO_IOC_RESET, NULL) == 0);
	m = KECHO_MODE_RAW;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == 0);
	CHECK(write(fd, "hello", 5) == 5);
	CHECK(get_stats(fd, &st) == 0);
	CHECK(st.nwrites == 1 && st.bytes_written == 5 && st.data_len == 5);
	CHECK(st.nreads == 0 && st.bytes_read == 0 && st.mode == KECHO_MODE_RAW && st.bad_cmds == 0);
	CHECK(st.__pad == 0);
	m = KECHO_MODE_UPPER;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == 0);
	CHECK(write(fd, " world", 6) == 6);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(read(fd, buf, sizeof(buf)) == 11 && memcmp(buf, "hello WORLD", 11) == 0);
	CHECK(get_stats(fd, &st) == 0);
	CHECK(st.nwrites == 2 && st.bytes_written == 11 && st.nreads == 1 && st.bytes_read == 11);
	CHECK(st.data_len == 11 && st.mode == KECHO_MODE_UPPER);

	/* Part B: 모르는 값 -> -EINVAL, 상태는 그대로 */
	m = 2;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == -EINVAL);
	m = 0xffffffffu;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == -EINVAL);
	CHECK(get_stats(fd, &st) == 0 && st.mode == KECHO_MODE_UPPER);

	/* Part B: 모르는 번호 -> -ENOTTY (네 필드 각각) */
	memset(&cb, 0, sizeof(cb));
	cb.canary = 0x1122334455667788ULL;
	CHECK(io(fd, BAD_SIZE, &cb.v0) == -ENOTTY);
	CHECK(cb.canary == 0x1122334455667788ULL);	/* 커널이 40 B 를 쓰면 깨진다 */
	CHECK(io(fd, BAD_DIR, &st) == -ENOTTY);
	CHECK(io(fd, BAD_NONE, &st) == -ENOTTY);
	CHECK(io(fd, BAD_NR, &st) == -ENOTTY);
	CHECK(io(fd, BAD_NR0, &st) == -ENOTTY);
	CHECK(io(fd, BAD_TYPE, &st) == -ENOTTY);
	tb.canary = 0xdeadbeefu;
	CHECK(io(fd, TCGETS, &tb) == -ENOTTY);		/* 옛 번호 0x5401: dir=0 size=0 'T' nr=1 */
	CHECK(tb.canary == 0xdeadbeefu);
	errno = 0;
	CHECK(isatty(fd) == 0 && errno == ENOTTY);	/* glibc 2.43: TCGETS2 = 0x802c542a */
	CHECK(get_stats(fd, &st) == 0 && st.bad_cmds == 8);

	/* Part B: 나쁜 유저 포인터 -> -EFAULT */
	pg = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ro = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(pg != MAP_FAILED && ro != MAP_FAILED && mprotect(pg + 4096, 4096, PROT_NONE) == 0);
	CHECK(io(fd, KECHO_IOC_GET_STATS, NULL) == -EFAULT);
	CHECK(io(fd, KECHO_IOC_GET_STATS, (void *)1) == -EFAULT);
	CHECK(io(fd, KECHO_IOC_GET_STATS, pg + 4096 - 16) == -EFAULT);	/* 뒤 24 B 가 PROT_NONE */
	CHECK(io(fd, KECHO_IOC_GET_STATS, ro) == -EFAULT);		/* 읽기 전용 페이지 */
	CHECK(io(fd, KECHO_IOC_SET_MODE, NULL) == -EFAULT);
	CHECK(io(fd, KECHO_IOC_SET_MODE, pg + 4096) == -EFAULT);
	CHECK(get_stats(fd, &st) == 0 && st.mode == KECHO_MODE_UPPER && st.bad_cmds == 8);

	/* RESET: 데이터와 통계는 0, mode 는 유지, arg 는 무시 */
	CHECK(io(fd, KECHO_IOC_RESET, (void *)1) == 0);
	CHECK(get_stats(fd, &st) == 0);
	CHECK(st.data_len == 0 && st.nwrites == 0 && st.bytes_written == 0 && st.nreads == 0 &&
	      st.bytes_read == 0 && st.bad_cmds == 0 && st.__pad == 0 && st.mode == KECHO_MODE_UPPER);
	CHECK(lseek(fd, 0, SEEK_SET) == 0 && read(fd, buf, sizeof(buf)) == 0);
	m = KECHO_MODE_RAW;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == 0);
	munmap(pg, 8192);
	munmap(ro, 4096);
	close(fd);
}

/* ---------------- 추가 연습 (practice.c 모듈) ---------------- */
static void test_practice(void)
{
	struct kecho_peek pk;
	struct kecho_stats st;
	char buf[16];
	__u32 m;
	char *ro;
	int fd = open("/dev/kecho", O_RDWR), rfd = open("/dev/kecho", O_RDONLY);

	CHECK(fd >= 0 && rfd >= 0);
	if (fd < 0 || rfd < 0)
		return;
	ro = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);	/* 0 으로 찬 RO 페이지 */
	CHECK(ro != MAP_FAILED);
	CHECK(io(fd, KECHO_IOC_RESET, NULL) == 0);
	CHECK(write(fd, "abcdef", 6) == 6);

	/* P1 GET_MODE (_IOR __u32) */
	m = 77;
	CHECK(io(fd, KECHO_IOC_GET_MODE, &m) == 0 && m == KECHO_MODE_RAW);
	/* P2 XCHG_MODE (_IOWR): 새 값을 넣고 옛 값을 돌려받는다 */
	m = KECHO_MODE_UPPER;
	CHECK(io(fd, KECHO_IOC_XCHG_MODE, &m) == 0 && m == KECHO_MODE_RAW);
	CHECK(io(fd, KECHO_IOC_GET_MODE, &m) == 0 && m == KECHO_MODE_UPPER);
	m = 5;
	CHECK(io(fd, KECHO_IOC_XCHG_MODE, &m) == -EINVAL && m == 5);
	CHECK(io(fd, KECHO_IOC_XCHG_MODE, NULL) == -EFAULT);
	CHECK(io(fd, KECHO_IOC_XCHG_MODE, ro) == -EFAULT);	/* 읽기는 되고(0) 쓰기가 실패 */
	CHECK(io(fd, KECHO_IOC_GET_MODE, &m) == 0 && m == KECHO_MODE_UPPER);	/* 되돌려 놓았나 */

	/* P3 PEEK: 구조체 안의 __u64 유저 포인터 */
	memset(buf, 0, sizeof(buf));
	pk.buf_ptr = (__u64)(uintptr_t)buf;
	pk.offset = 2;
	pk.len = 3;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == 0 && pk.len == 3 && memcmp(buf, "cde", 3) == 0);
	pk.offset = 4;
	pk.len = 100;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == 0 && pk.len == 2 && memcmp(buf, "ef", 2) == 0);
	pk.offset = 6;
	pk.len = 1;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == 0 && pk.len == 0);
	pk.offset = 7;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == -EINVAL);
	pk.offset = 0;
	pk.len = 4097;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == -EINVAL);
	pk.len = 3;
	pk.buf_ptr = 1;
	CHECK(io(fd, KECHO_IOC_PEEK, &pk) == -EFAULT);
	CHECK(io(fd, KECHO_IOC_PEEK, NULL) == -EFAULT);

	/* P4 RESET 은 쓰기 권한이 있는 fd 에서만 */
	CHECK(io(rfd, KECHO_IOC_RESET, NULL) == -EBADF);
	CHECK(get_stats(rfd, &st) == 0 && st.data_len == 6);	/* 읽기 명령은 RO fd 도 된다 */
	CHECK(io(fd, KECHO_IOC_RESET, NULL) == 0);
	m = KECHO_MODE_RAW;
	CHECK(io(fd, KECHO_IOC_SET_MODE, &m) == 0);
	munmap(ro, 4096);
	close(rfd);
	close(fd);
}

int main(int argc, char **argv)
{
	static const struct cmdinfo base[] = {
		{ "KECHO_IOC_GET_STATS", KECHO_IOC_GET_STATS, 0x80287801u, 2, 40, 1 },
		{ "KECHO_IOC_SET_MODE",  KECHO_IOC_SET_MODE,  0x40047802u, 1, 4,  2 },
		{ "KECHO_IOC_RESET",     KECHO_IOC_RESET,     0x00007803u, 0, 0,  3 },
	};
	static const struct cmdinfo more[] = {
		{ "KECHO_IOC_GET_MODE",  KECHO_IOC_GET_MODE,  0x80047804u, 2, 4,  4 },
		{ "KECHO_IOC_XCHG_MODE", KECHO_IOC_XCHG_MODE, 0xc0047805u, 3, 4,  5 },
		{ "KECHO_IOC_PEEK",      KECHO_IOC_PEEK,      0xc0107806u, 3, 16, 6 },
	};
	int practice = argc > 1 && strcmp(argv[1], "practice") == 0;

	setvbuf(stdout, NULL, _IOLBF, 0);
	decode_table(base, 3);
	if (practice)
		decode_table(more, 3);
	layout_table();
	test_base();
	if (practice)
		test_practice();
	printf("[%s] %d/%d passed\n", ABI, g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
