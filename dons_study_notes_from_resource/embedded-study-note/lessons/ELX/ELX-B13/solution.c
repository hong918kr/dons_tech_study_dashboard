/* ENV: linux */
/* ELX-B13 esn_sensord — Type=notify fake sensor daemon with sd_notify written by hand.
 *   esn_sensord --selftest                 unit tests without systemd ("N/M passed", exit 0/1)
 *   esn_sensord <sensor_file> <hang_flag>  daemon mode (started by systemd, see check.sh)
 * sensor_file = hwmon-style text in millidegree C ("23500\n"); missing -> exit 3.
 * hang_flag   = when this file appears, the main loop removes it and gets "stuck" (no more pets).
 * env ESN_CALIB_MS = fake calibration time before READY=1 (default 300).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TICK_USEC 100000ULL /* main loop: one sensor sample every 100 ms */

/* ---------------- Part A: sd_notify by hand ---------------- */
/* NOTIFY_SOCKET value -> AF_UNIX address.
 *   "/abs/path" -> filesystem socket,  *len = offsetof(sun_path) + strlen + 1 (keeps the NUL)
 *   "@name"     -> abstract socket:    sun_path[0] = '\0', *len = offsetof(sun_path) + strlen
 * returns 0, -EINVAL (NULL, "", "/", "@", relative path), -ENAMETOOLONG (>= sizeof sun_path) */
static int notify_addr(const char *env, struct sockaddr_un *sa, socklen_t *len)
{
	if (!env || (env[0] != '/' && env[0] != '@') || env[1] == '\0')
		return -EINVAL;
	size_t n = strlen(env);
	if (n >= sizeof(sa->sun_path))
		return -ENAMETOOLONG;
	memset(sa, 0, sizeof(*sa));
	sa->sun_family = AF_UNIX;
	memcpy(sa->sun_path, env, n);
	if (env[0] == '@')
		sa->sun_path[0] = '\0';
	*len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n + (env[0] == '/'));
	return 0;
}

/* Send msg ("KEY=VALUE\nKEY=VALUE") as ONE datagram to $NOTIFY_SOCKET.
 * returns 1 = sent, 0 = NOTIFY_SOCKET unset (not under systemd -> no-op), <0 = -errno */
static int notify_send(const char *msg)
{
	const char *env = getenv("NOTIFY_SOCKET");
	struct sockaddr_un sa;
	socklen_t len;
	if (!env)
		return 0;
	int r = notify_addr(env, &sa, &len);
	if (r < 0)
		return r;
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;
	ssize_t n = sendto(fd, msg, strlen(msg), MSG_NOSIGNAL, (const struct sockaddr *)&sa, len);
	int e = errno;
	close(fd);
	if (n < 0)
		return -e;
	return (size_t)n == strlen(msg) ? 1 : -EMSGSIZE;
}

/* ---------------- Part B: watchdog period ---------------- */
/* Ping period in usec = WATCHDOG_USEC / 2 (ping at half the timeout).
 * 0 = watchdog off: WATCHDOG_USEC unset / "" / garbage / 0 / negative,
 *     or WATCHDOG_PID is set and is not our pid (a forked child inherited the env). */
static uint64_t watchdog_period_usec(void)
{
	const char *u = getenv("WATCHDOG_USEC"), *p = getenv("WATCHDOG_PID");
	char *end;
	if (!u || u[0] < '0' || u[0] > '9')
		return 0;
	errno = 0;
	unsigned long long v = strtoull(u, &end, 10);
	if (errno || *end != '\0' || v == 0)
		return 0;
	if (p) {
		errno = 0;
		long pid = strtol(p, &end, 10);
		if (errno || p[0] == '\0' || *end != '\0' || pid != (long)getpid())
			return 0;
	}
	return v / 2;
}

/* Pet only if the period elapsed AND the main loop really made progress since the last pet. */
static int pet_due(uint64_t now, uint64_t last_pet, uint64_t period, uint64_t samples, uint64_t samples_at_pet)
{
	return period != 0 && now - last_pet >= period && samples != samples_at_pet;
}

/* ---------------- Part C: the daemon ---------------- */
static volatile sig_atomic_t g_term;
static void on_term(int sig) { (void)sig; g_term = 1; }

static uint64_t now_usec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static int read_sensor(const char *path, long *milli)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int ok = fscanf(f, "%ld", milli) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

static int run_daemon(const char *sensor, const char *hang_flag)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_term;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	const char *ns = getenv("NOTIFY_SOCKET"), *cm = getenv("ESN_CALIB_MS");
	uint64_t period = watchdog_period_usec();
	long milli = 0, calib_ms = cm ? atol(cm) : 300;
	char msg[160];
	printf("esn_sensord: pid=%d NOTIFY_SOCKET=%s ping_period_us=%llu\n", (int)getpid(),
	       ns ? ns : "(unset)", (unsigned long long)period);
	if (read_sensor(sensor, &milli) < 0) {
		printf("esn_sensord: sensor %s missing -> exit 3\n", sensor);
		/* TODO(C1): tell systemd why we fail: STATUS=sensor missing + ERRNO=19 (ENODEV) */
		notify_send("STATUS=sensor missing\nERRNO=19");
		return 3;
	}
	usleep((useconds_t)(calib_ms * 1000)); /* pretend: sensor calibration */
	/* TODO(C2): start-up done -> READY=1 with a STATUS= line ("ready temp=<milli> mC") */
	snprintf(msg, sizeof msg, "READY=1\nSTATUS=ready temp=%ld mC", milli);
	notify_send(msg);
	printf("esn_sensord: start-up done (temp=%ld mC)\n", milli);

	uint64_t samples = 0, samples_at_pet = 0, pets = 0, last_pet = now_usec();
	while (!g_term) {
		usleep(TICK_USEC);
		if (access(hang_flag, F_OK) == 0) {
			unlink(hang_flag);
			printf("esn_sensord: hang injected: main loop stuck, no more WATCHDOG=1\n");
			for (;;)
				pause(); /* stuck: SIGTERM handler returns here; only SIGABRT/SIGKILL end it */
		}
		if (read_sensor(sensor, &milli) == 0)
			samples++;
		uint64_t now = now_usec();
		/* TODO(C3): when pet_due(), send WATCHDOG=1 + STATUS=temp=<milli> mC samples=<n> pets=<n> */
		if (pet_due(now, last_pet, period, samples, samples_at_pet)) {
			pets++;
			snprintf(msg, sizeof msg, "WATCHDOG=1\nSTATUS=temp=%ld mC samples=%llu pets=%llu", milli,
				 (unsigned long long)samples, (unsigned long long)pets);
			notify_send(msg);
			last_pet = now;
			samples_at_pet = samples;
		}
	}
	/* TODO(C4): SIGTERM -> tell systemd we are stopping (STOPPING=1) before cleanup + exit 0 */
	notify_send("STOPPING=1\nSTATUS=stopping");
	printf("esn_sensord: SIGTERM -> exit 0 (samples=%llu pets=%llu)\n",
	       (unsigned long long)samples, (unsigned long long)pets);
	return 0;
}

/* ---------------- 테스트 하네스 (systemd 없이: 내가 PID 1 역할) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
	else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* fake PID 1: an abstract SOCK_DGRAM socket with SO_PASSCRED (systemd does the same) */
static int fake_pid1_open(char *name, size_t cap)
{
	int n = snprintf(name, cap, "@esn-b13-selftest-%d", (int)getpid());
	struct sockaddr_un sa = { .sun_family = AF_UNIX }; /* own encoding: the harness must not use the TODO code */
	memcpy(sa.sun_path + 1, name + 1, (size_t)n - 1);   /* sun_path[0] = '\0' -> abstract */
	socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (size_t)n);
	int one = 1, fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof one) < 0 ||
	    bind(fd, (struct sockaddr *)&sa, len) < 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	return fd;
}

/* receive one datagram (timeout_ms), NUL-terminate it, return sender pid (or -1) */
static int fake_pid1_recv(int fd, char *buf, size_t cap, int timeout_ms)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	if (poll(&p, 1, timeout_ms) != 1)
		return -1;
	union { struct cmsghdr h; char b[CMSG_SPACE(sizeof(struct ucred))]; } c;
	struct iovec iov = { .iov_base = buf, .iov_len = cap - 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = c.b,
			    .msg_controllen = sizeof c.b };
	ssize_t n = recvmsg(fd, &m, 0);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	struct cmsghdr *h = CMSG_FIRSTHDR(&m);
	if (!h || h->cmsg_type != SCM_CREDENTIALS)
		return -1;
	struct ucred cr;
	memcpy(&cr, CMSG_DATA(h), sizeof cr);
	return (int)cr.pid;
}

static void write_file(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	if (f) {
		fputs(s, f);
		fclose(f);
	}
}

/* fork the daemon like systemd would: env set, stdout to /dev/null */
static pid_t spawn_daemon(const char *sock, const char *sensor, const char *hang, const char *wd_usec)
{
	pid_t pid = fork();
	if (pid == 0) {
		char b[32];
		int null = open("/dev/null", O_WRONLY);
		if (null >= 0)
			dup2(null, 1);
		snprintf(b, sizeof b, "%d", (int)getpid());
		setenv("NOTIFY_SOCKET", sock, 1);
		setenv("WATCHDOG_USEC", wd_usec, 1);
		setenv("WATCHDOG_PID", b, 1);
		setenv("ESN_CALIB_MS", "50", 1);
		_exit(run_daemon(sensor, hang));
	}
	return pid;
}

static int wait_exit(pid_t pid)
{
	int st;
	for (int i = 0; i < 300; i++) { /* 3 s max, then SIGKILL so the test never hangs */
		if (waitpid(pid, &st, WNOHANG) == pid)
			return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
		usleep(10000);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	return -1;
}

static void test_part_a(void)
{
	struct sockaddr_un sa;
	socklen_t len = 0;
	memset(&sa, 0, sizeof sa);
	const size_t off = offsetof(struct sockaddr_un, sun_path);
	CHECK(notify_addr("/run/systemd/notify", &sa, &len) == 0);
	CHECK(sa.sun_family == AF_UNIX && strcmp(sa.sun_path, "/run/systemd/notify") == 0);
	CHECK(len == off + 19 + 1);
	CHECK(notify_addr("@esn/abc", &sa, &len) == 0);
	CHECK(sa.sun_path[0] == '\0' && memcmp(sa.sun_path + 1, "esn/abc", 7) == 0);
	CHECK(len == off + 8);
	CHECK(notify_addr(NULL, &sa, &len) == -EINVAL);
	CHECK(notify_addr("", &sa, &len) == -EINVAL);
	CHECK(notify_addr("@", &sa, &len) == -EINVAL);
	CHECK(notify_addr("run/systemd/notify", &sa, &len) == -EINVAL);
	char big[200];
	memset(big, 'x', sizeof big - 1);
	big[0] = '/';
	big[sizeof big - 1] = '\0';
	CHECK(notify_addr(big, &sa, &len) == -ENAMETOOLONG);

	/* notify_send end to end: abstract and filesystem receivers, creds = our pid */
	char name[64], buf[256];
	int fd = fake_pid1_open(name, sizeof name);
	CHECK(fd >= 0);
	unsetenv("NOTIFY_SOCKET");
	CHECK(notify_send("READY=1") == 0);
	setenv("NOTIFY_SOCKET", name, 1);
	CHECK(notify_send("READY=1\nSTATUS=hello") == 1);
	CHECK(fd >= 0 && fake_pid1_recv(fd, buf, sizeof buf, 1000) == (int)getpid());
	CHECK(strcmp(buf, "READY=1\nSTATUS=hello") == 0);
	if (fd >= 0)
		close(fd);

	char dir[] = "/tmp/ELX-B13_selftest_XXXXXX", path[96];
	CHECK(mkdtemp(dir) != NULL);
	snprintf(path, sizeof path, "%s/notify", dir);
	int pfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0), one = 1;
	struct sockaddr_un psa = { .sun_family = AF_UNIX };
	snprintf(psa.sun_path, sizeof psa.sun_path, "%s", path);
	setsockopt(pfd, SOL_SOCKET, SO_PASSCRED, &one, sizeof one);
	CHECK(bind(pfd, (struct sockaddr *)&psa, sizeof psa) == 0);
	setenv("NOTIFY_SOCKET", path, 1);
	CHECK(notify_send("WATCHDOG=1") == 1);
	CHECK(fake_pid1_recv(pfd, buf, sizeof buf, 1000) == (int)getpid() && strcmp(buf, "WATCHDOG=1") == 0);
	close(pfd);
	unlink(path);
	CHECK(notify_send("WATCHDOG=1") < 0); /* nobody listens: -ECONNREFUSED/-ENOENT, no SIGPIPE */
	rmdir(dir);
	unsetenv("NOTIFY_SOCKET");
}

static void test_part_b(void)
{
	char me[32];
	snprintf(me, sizeof me, "%d", (int)getpid());
	unsetenv("WATCHDOG_PID");
	unsetenv("WATCHDOG_USEC");
	CHECK(watchdog_period_usec() == 0);
	setenv("WATCHDOG_USEC", "5000000", 1);
	CHECK(watchdog_period_usec() == 2500000);
	setenv("WATCHDOG_PID", me, 1);
	CHECK(watchdog_period_usec() == 2500000);
	setenv("WATCHDOG_PID", "1", 1);
	CHECK(watchdog_period_usec() == 0);
	setenv("WATCHDOG_PID", me, 1);
	setenv("WATCHDOG_USEC", "abc", 1);
	CHECK(watchdog_period_usec() == 0);
	setenv("WATCHDOG_USEC", "5000000x", 1);
	CHECK(watchdog_period_usec() == 0);
	setenv("WATCHDOG_USEC", "-5", 1);
	CHECK(watchdog_period_usec() == 0);
	setenv("WATCHDOG_USEC", "0", 1);
	CHECK(watchdog_period_usec() == 0);
	unsetenv("WATCHDOG_PID");
	unsetenv("WATCHDOG_USEC");
	CHECK(pet_due(3000000, 0, 2500000, 30, 0) == 1);
	CHECK(pet_due(2000000, 0, 2500000, 20, 0) == 0); /* too early */
	CHECK(pet_due(3000000, 0, 2500000, 7, 7) == 0);  /* no progress -> no pet */
	CHECK(pet_due(9000000, 0, 0, 90, 0) == 0);       /* watchdog off */
}

static void test_part_c(void)
{
	char name[64], buf[256], dir[] = "/tmp/ELX-B13_selftest_XXXXXX", sensor[128], hang[128];
	int fd = fake_pid1_open(name, sizeof name);
	CHECK(fd >= 0 && mkdtemp(dir) != NULL);
	if (fd < 0)
		return;
	snprintf(sensor, sizeof sensor, "%s/temp1_input", dir);
	snprintf(hang, sizeof hang, "%s/hang", dir);
	write_file(sensor, "23500\n");

	/* C-1: READY first, then >= 3 WATCHDOG=1 from the main pid, SIGTERM -> STOPPING=1, exit 0 */
	pid_t pid = spawn_daemon(name, sensor, hang, "200000");
	int ready = 0, pets = 0, stopping = 0, wrong_pid = 0, first_is_ready = -1, s;
	while (pets < 3 && (s = fake_pid1_recv(fd, buf, sizeof buf, 2000)) > 0) {
		wrong_pid += s != pid;
		if (first_is_ready < 0)
			first_is_ready = strncmp(buf, "READY=1\n", 8) == 0;
		ready += strstr(buf, "READY=1") != NULL;
		pets += strstr(buf, "WATCHDOG=1") != NULL;
	}
	kill(pid, SIGTERM);
	while (!stopping && fake_pid1_recv(fd, buf, sizeof buf, 2000) > 0)
		stopping = strstr(buf, "STOPPING=1") != NULL;
	CHECK(first_is_ready == 1 && ready == 1);
	CHECK(pets == 3 && wrong_pid == 0);
	CHECK(stopping == 1);
	CHECK(wait_exit(pid) == 0);

	/* C-2: hang flag -> flag consumed, pets stop (window = 4 periods), SIGTERM ignored, SIGKILL ends it */
	pid = spawn_daemon(name, sensor, hang, "200000");
	pets = 0;
	while (pets < 1 && fake_pid1_recv(fd, buf, sizeof buf, 2000) > 0)
		pets += strstr(buf, "WATCHDOG=1") != NULL;
	write_file(hang, "1\n");
	for (int i = 0; i < 100 && access(hang, F_OK) == 0; i++)
		usleep(10000);
	CHECK(pets == 1 && access(hang, F_OK) != 0);
	while (fake_pid1_recv(fd, buf, sizeof buf, 50) > 0) /* drain a pet that raced the flag */
		;
	CHECK(fake_pid1_recv(fd, buf, sizeof buf, 400) < 0);
	kill(pid, SIGTERM);
	CHECK(fake_pid1_recv(fd, buf, sizeof buf, 300) < 0); /* stuck loop: no STOPPING=1 */
	kill(pid, SIGKILL);
	CHECK(wait_exit(pid) == 128 + SIGKILL);

	/* C-3: sensor missing -> STATUS + ERRNO=19, exit 3, never READY */
	unlink(sensor);
	pid = spawn_daemon(name, sensor, hang, "200000");
	s = fake_pid1_recv(fd, buf, sizeof buf, 2000);
	CHECK(s == pid && strstr(buf, "ERRNO=19") != NULL && strstr(buf, "READY=1") == NULL);
	CHECK(wait_exit(pid) == 3);
	close(fd);
	rmdir(dir);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0); /* stdout goes to journald: one line per printf */
	if (argc == 3)
		return run_daemon(argv[1], argv[2]);
	if (argc != 2 || strcmp(argv[1], "--selftest") != 0) {
		fprintf(stderr, "usage: %s --selftest | <sensor_file> <hang_flag>\n", argv[0]);
		return 2;
	}
	test_part_a();
	test_part_b();
	test_part_c();
	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
