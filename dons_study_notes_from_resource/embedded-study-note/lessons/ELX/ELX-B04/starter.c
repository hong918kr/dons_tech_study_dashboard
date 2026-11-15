/* ENV: qemu */
/* ELX-B04 initramfs 의 PID 1 — 작은 /init 을 C 로 직접 쓴다.
 *   빌드 (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -static solution.c -o init
 *   실행: check.sh 가 cpio initramfs 를 만들고 QEMU virt 에서 rdinit=/init 으로 부팅한다.
 *   커널 인자: B04_SCRIPT=/etc/selftest.sh -> 그 BusyBox sh 스크립트를 비대화식으로 실행
 *              (없으면 /dev/console 에서 대화형 셸)      '-- b04arg' -> init 의 argv[1]
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------------- Part A: 가상 파일시스템 + 콘솔 ---------------- */
/* proc->/proc, sysfs->/sys, devtmpfs->/dev 를 마운트한다 (디렉터리가 없으면 mkdir 0755).
 * 이미 마운트돼 있으면(EBUSY) 성공으로 친다. 성공한 개수(0..3)를 돌려준다. */
static int mount_early(void)
{
	/* TODO: 3개 마운트 (mkdir 먼저, EBUSY 는 성공) */
	return 0;
}

/* fd 0/1/2 중 하나라도 닫혀 있으면(EBADF) /dev/console 을 열어 0,1,2 로 dup2 한다.
 * 반환: 0 = 원래 정상, 1 = 고쳤다, -1 = 못 고쳤다 */
static int fix_console(void)
{
	/* TODO: fcntl(i, F_GETFD) 로 0/1/2 검사 -> /dev/console open + dup2 */
	return 0;
}

/* ---------------- Part B: 커널 명령줄 파싱 ---------------- */
/* cmdline("/proc/cmdline" 내용, 공백 구분, 끝에 '\n' 가능)에서 key 의 값을 찾는다.
 * 값은 "..." 로 감쌀 수 있다(따옴표는 떼고 안의 공백은 유지). 같은 key 가 여러 번이면 마지막이 이긴다.
 * '=' 없이 key 만 있으면 val="" 로 찾은 것으로 친다.
 * 반환: 1 = 찾음, 0 = 없음, -1 = val 버퍼(n)가 작음 (잘린 값 + NUL) */
static int cmdline_get(const char *cmdline, const char *key, char *val, size_t n)
{
	/* TODO: 토큰 단위로 자르고(따옴표 안 공백은 유지) key 와 정확히 일치하는 것만 */
	(void)cmdline; (void)key; (void)val; (void)n;
	return 0;
}

/* ---------------- Part C: 자식 거두기 · 스크립트 실행 · 끄기 ---------------- */
/* 자식(입양된 고아 포함)을 모두 거둔다: 블로킹 waitpid(-1) 를 ECHILD 가 날 때까지.
 * 거둔 개수를 돌려주고, 정상 종료 코드를 codes[] 에 (최대 max 개) 적는다. */
static int reap_all(int *codes, int max)
{
	/* TODO: waitpid(-1, &st, 0) 를 ECHILD 까지 (EINTR 은 다시) */
	(void)codes; (void)max;
	return 0;
}

/* argv[0] 을 실행하고 그 프로세스가 끝날 때까지 기다린다. ctty=1 이면 자식이 setsid() +
 * TIOCSCTTY 로 콘솔을 제어 터미널로 잡는다(대화형 셸의 job control 용).
 * 기다리는 동안 거둔 다른 자식(고아)은 *orphans 에 더한다.
 * 반환: 종료 코드, 시그널로 죽으면 128+sig, fork 실패 -1 (exec 실패는 자식의 127) */
static int spawn_wait(char *const argv[], int ctty, int *orphans)
{
	/* TODO: fork -> 자식: (ctty 면 setsid+TIOCSCTTY) execv, 실패 시 _exit(127)
	 *       부모: waitpid(-1) 루프, 내 pid 가 아니면 (*orphans)++ */
	(void)argv; (void)ctty; (void)orphans;
	return -1;
}

/* PID 1 은 절대 return/exit 하면 안 된다: 버퍼를 비우고 reboot(RB_POWER_OFF) */
static void power_off(void)
{
	printf("B04 init: power off\n");
	/* TODO: fflush + sync() + reboot(RB_POWER_OFF) */
}

/* ---------------- 테스트 도우미 (TODO 아님) ---------------- */
static int g_pass, g_total;
#define CHECK(expr, name) do { g_total++; if (expr) { g_pass++; printf("PASS: %s\n", name); } \
	else printf("FAIL: %s  (%s:%d)\n", name, __FILE__, __LINE__); } while (0)

/* /proc/mounts 에 "<src> <dir> <type> ..." 줄이 있나 */
static int mount_has(const char *dir, const char *type)
{
	char line[512], d[256], t[64];
	FILE *f = fopen("/proc/mounts", "r");
	int hit = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, "%*s %255s %63s", d, t) == 2 && !strcmp(d, dir) && !strcmp(t, type))
			hit = 1;
	fclose(f);
	return hit;
}

/* /proc/<pid>/stat 의 상태가 'Z' 인 프로세스 수. /proc 이 없으면 -1 */
static int count_zombies(void)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int z = 0;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char path[300], buf[256];
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		size_t len = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[len] = '\0';
		char *rp = strrchr(buf, ')');
		if (rp && rp[1] == ' ' && rp[2] == 'Z')
			z++;
	}
	closedir(d);
	return z;
}

static int read_file(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	ssize_t r = read(fd, buf, n - 1);
	close(fd);
	if (r < 0)
		return -1;
	buf[r] = '\0';
	return (int)r;
}

/* 자식은 손자를 만들고 바로 exit(7) -> 손자는 고아가 되어 PID 1 에 입양된 뒤 exit(42) */
static pid_t make_orphan_pair(void)
{
	fflush(stdout);
	pid_t c = fork();
	if (c == 0) {
		if (fork() == 0) {
			while (getppid() != 1)
				usleep(1000);
			_exit(42);
		}
		_exit(7);
	}
	return c;
}

int main(int argc, char **argv)
{
	int mounted = mount_early();
	int con = fix_console();
	char cmdline[1024] = "", v[128];

	setvbuf(stdout, NULL, _IONBF, 0);
	setenv("PATH", "/sbin:/usr/sbin:/bin:/usr/bin", 0);   /* 커널은 PATH 를 주지 않는다 */
	printf("B04 init: pid=%d mounts=%d console=%s argc=%d\n", (int)getpid(), mounted,
	       con == 0 ? "ok" : con == 1 ? "repaired" : "broken", argc);

	/* Part A */
	struct stat so;
	CHECK(getpid() == 1, "A1 running as PID 1");
	CHECK(mounted == 3, "A2 mount_early() mounted proc+sysfs+devtmpfs");
	CHECK(mount_has("/proc", "proc") && mount_has("/sys", "sysfs") && mount_has("/dev", "devtmpfs"),
	      "A3 /proc/mounts lists proc, sysfs, devtmpfs");
	CHECK(fstat(1, &so) == 0 && S_ISCHR(so.st_mode) && major(so.st_rdev) == 5 && minor(so.st_rdev) == 1,
	      "A4 stdout is /dev/console (char 5:1)");
	CHECK(con >= 0 && fcntl(0, F_GETFD) >= 0 && fcntl(2, F_GETFD) >= 0, "A5 fd 0/1/2 open");

	/* Part B */
	int r1 = cmdline_get("console=ttyAMA0 B04_SCRIPT=/etc/s.sh quiet\n", "B04_SCRIPT", v, sizeof v);
	CHECK(r1 == 1 && !strcmp(v, "/etc/s.sh"), "B1 key=value, trailing newline");
	CHECK(cmdline_get("XB04_SCRIPT=a B04_SCRIPTX=b", "B04_SCRIPT", v, sizeof v) == 0, "B2 no prefix match");
	CHECK(cmdline_get("ro quiet panic=-1", "quiet", v, sizeof v) == 1 && v[0] == '\0', "B3 flag without =");
	CHECK(cmdline_get("a=1 msg=\"hello world\" b=2", "msg", v, sizeof v) == 1 && !strcmp(v, "hello world"),
	      "B4 quoted value keeps the space");
	CHECK(cmdline_get("root=/dev/vda root=/dev/vdb", "root", v, sizeof v) == 1 && !strcmp(v, "/dev/vdb"),
	      "B5 last one wins");
	CHECK(cmdline_get("B04_SCRIPT=/etc/s.sh", "B04_SCRIPT", v, 4) == -1 && !strcmp(v, "/et"),
	      "B6 small buffer -> -1, truncated");
	read_file("/proc/cmdline", cmdline, sizeof cmdline);
	char script[128] = "";
	int have_script = cmdline_get(cmdline, "B04_SCRIPT", script, sizeof script) == 1 && script[0];
	const char *env = getenv("B04_SCRIPT");
	CHECK(!have_script || (env && !strcmp(env, script)), "B7 kernel copied B04_SCRIPT= into init's envp");
	CHECK(argc >= 2 && !strcmp(argv[1], "b04arg"), "B8 word after '--' arrived as argv[1]");

	/* Part C: reaping */
	int codes[4] = { -1, -1, -1, -1 };
	siginfo_t si;
	pid_t c = make_orphan_pair();
	waitid(P_PID, (id_t)c, &si, WEXITED | WNOWAIT);   /* 끝날 때까지 기다리되 거두지는 않는다 */
	CHECK(count_zombies() >= 1, "C1 exited child is a zombie until PID 1 reaps it");
	int n = reap_all(codes, 4);
	CHECK(n == 2 && ((codes[0] == 7 && codes[1] == 42) || (codes[0] == 42 && codes[1] == 7)),
	      "C2 reap_all() collected child(7) + adopted orphan(42)");
	CHECK(count_zombies() == 0 && waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD, "C3 no zombies, ECHILD");

	if (have_script) {
		int orphans = 0;
		char *sargv[] = { "/bin/sh", script, NULL };
		int rc = spawn_wait(sargv, 0, &orphans);
		printf("B04 init: %s exited %d, orphans reaped meanwhile %d\n", script, rc, orphans);
		CHECK(rc == 3, "C4 script ran to the end (exit 3)");
		int late = reap_all(codes, 4);
		printf("B04 init: reaped %d late orphan(s), first code %d\n", late, codes[0]);
		CHECK(orphans + late == 1 && (orphans == 1 || codes[0] == 5), "C5 script's orphan (exit 5) reaped");
		CHECK(count_zombies() == 0, "C6 no zombies after script");
	} else {
		int orphans = 0;
		char *sh[] = { "/bin/sh", NULL };
		printf("B04 init: no B04_SCRIPT= -> interactive shell, 'exit' powers off\n");
		spawn_wait(sh, 1, &orphans);
		reap_all(codes, 4);
	}

	printf("%d/%d passed\n", g_pass, g_total);
	printf("INIT_DONE\n");
	power_off();
	return g_pass == g_total ? 0 : 1;   /* 여기까지 오면 커널 panic: Attempted to kill init! */
}
