/* ENV: qemu (OS-A09 overlay on xv6-riscv fa2ddcb; built + booted by check.sh inside the Lima VM) */
// practicetest args | files | clock | procs   (OS-A09 9. 추가 연습)
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/sysinfo.h"
#include "kernel/procinfo.h"
#include "user/user.h"

static int npass, nfail;

static void
check(int ok, char *name)
{
  if (ok) {
    printf("PASS: %s\n", name);
    npass++;
  } else {
    printf("FAIL: %s\n", name);
    nfail++;
  }
}

// P1: argument tracing. run_xv6.py compares the kernel lines exactly.
static void
args(void)
{
  char buf[10];
  int m = (1 << SYS_open) | (1 << SYS_read) | (1 << SYS_close) | (1 << SYS_unlink);
  int flags = O_CREATE | O_RDWR;
  int r1 = trace(m);
  int r2 = traceargs(m & ~(1 << SYS_close)); // close: name only, no args
  int fd = open("pt.args", flags);
  int n = read(fd, buf, sizeof(buf)); // new empty file -> 0
  int c = close(fd);
  int u = unlink("pt.args");
  trace(0);
  traceargs(0);
  printf("args: pid=%d fd=%d flags=%d\n", getpid(), fd, flags);
  check(r1 == 0 && r2 == 0 && fd >= 0 && n == 0 && c == 0 && u == 0,
        "args: traced calls return normally");
}

static uint64
nfiles(void)
{
  struct sysinfo si;
  si.nopenfiles = 0;
  if (sysinfo(&si) < 0)
    return 0;
  return si.nopenfiles;
}

// P2: sysinfo.nopenfiles counts open-file TABLE entries, not descriptors.
static void
files(void)
{
  uint64 base = nfiles();
  int a = open("README", O_RDONLY);
  int b = open("README", O_RDONLY);
  int c = open("README", O_RDONLY);
  uint64 three = nfiles();
  int d = dup(a);
  uint64 dupped = nfiles();
  int p[2];
  pipe(p);
  uint64 piped = nfiles();
  close(a); close(b); close(c); close(d); close(p[0]); close(p[1]);
  uint64 after = nfiles();
  printf("files: %ld -> %ld -> %ld -> %ld -> %ld\n", base, three, dupped, piped, after);
  check(base >= 1, "files: console entry is counted");
  check(three == base + 3, "files: 3 opens -> +3 entries");
  check(dupped == three, "files: dup shares the entry -> +0");
  check(piped == three + 2, "files: pipe -> +2 entries");
  check(after == base, "files: back to base after close");
}

// P3: uptime_ms from the time CSR. Lower bounds only (they follow from how
// pause() counts ticks), no performance claims.
static void
clock(void)
{
  uint64 prev = uptime_ms();
  int mono = 1;
  for (int i = 0; i < 1000; i++) {
    uint64 t = uptime_ms();
    if (t < prev)
      mono = 0;
    prev = t;
  }
  int t0 = uptime();
  uint64 m0 = uptime_ms();
  pause(3);
  uint64 m1 = uptime_ms();
  int t1 = uptime();
  printf("clock: ms %ld -> %ld (%ld ms), ticks %d -> %d\n", m0, m1, m1 - m0, t0, t1);
  check(mono, "clock: uptime_ms never goes backwards (1000 calls)");
  check(t1 - t0 >= 3, "clock: pause(3) waits >= 3 ticks");
  check(m1 - m0 >= 200, "clock: >= 3 ticks of ~100 ms minus one partial tick -> >= 200 ms");
  check(m0 / 100 + 5 >= (uint64)t0, "clock: ms/100 and ticks agree roughly (ticks <= ms/100 + 5)");
}

// P4: getprocs snapshot.
static void
procs(void)
{
  struct procinfo pi[NPROC];
  struct sysinfo si;
  int fds[2];
  char c;
  pipe(fds);
  int child = fork();
  if (child == 0) {
    read(fds[0], &c, 1);
    exit(0);
  }
  pause(1); // let the child reach read() and sleep (CPUS=1: it runs now)
  int n = getprocs(pi, NPROC);
  sysinfo(&si);
  int one = getprocs(pi + NPROC - 1, 1);
  int bad = getprocs((struct procinfo *)0, NPROC); // text page
  int me = getpid();
  int sawinit = 0, sawme = 0, sawchild = 0;
  for (int i = 0; i < n; i++) {
    printf("procs: %d %d %s\n", pi[i].pid, pi[i].state, pi[i].name);
    if (pi[i].pid == 1 && strcmp(pi[i].name, "init") == 0 && pi[i].state == 2)
      sawinit = 1;
    if (pi[i].pid == me && pi[i].state == 4)
      sawme = 1;
    if (pi[i].pid == child && pi[i].state == 2 && strcmp(pi[i].name, "practicetest") == 0)
      sawchild = 1;
  }
  write(fds[1], "x", 1);
  wait(0);
  close(fds[0]);
  close(fds[1]);
  check(n > 0 && (uint64)n == si.nproc, "procs: count == sysinfo.nproc");
  check(sawinit, "procs: pid 1 init is SLEEPING");
  check(sawme, "procs: caller is RUNNING");
  check(sawchild, "procs: blocked child is SLEEPING with the parent's name");
  check(one == 1 && bad == -1, "procs: max respected, bad pointer -> -1");
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "usage: practicetest args|files|clock|procs\n");
    exit(1);
  }
  if (strcmp(argv[1], "args") == 0)
    args();
  else if (strcmp(argv[1], "files") == 0)
    files();
  else if (strcmp(argv[1], "clock") == 0)
    clock();
  else if (strcmp(argv[1], "procs") == 0)
    procs();
  else {
    fprintf(2, "practicetest: unknown case %s\n", argv[1]);
    exit(1);
  }
  printf("practicetest: %d passed, %d failed\n", npass, nfail);
  exit(nfail != 0);
}
