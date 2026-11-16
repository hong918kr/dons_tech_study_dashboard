/* ENV: qemu (OS-A09 overlay on xv6-riscv fa2ddcb; built + booted by check.sh inside the Lima VM) */
// tracetest basic | fork
// Runs a fixed syscall sequence under trace(). The kernel's trace lines go
// to the console; run_xv6.py compares them with the values printed here
// (pid, fd, ...). This program only checks the return values it can see.
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
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

// One process, no console output while tracing (user output and kernel
// trace lines would share the console).
static void
basic(void)
{
  int mask = (1 << SYS_trace) | (1 << SYS_open) | (1 << SYS_write) |
             (1 << SYS_close) | (1 << SYS_getpid);
  int r = trace(mask);                          // traced: bit SYS_trace set
  int fd = open("tt.tmp", O_CREATE | O_RDWR);   // traced
  int w = write(fd, "hello", 5);                // traced
  int c = close(fd);                            // traced
  int bad = open("no-such-file", O_RDONLY);     // traced, returns -1
  int pid = getpid();                           // traced
  int r0 = trace(0);                            // mask now 0 -> not traced
  unlink("tt.tmp");                             // not traced
  printf("basic: pid=%d fd=%d\n", pid, fd);
  check(r == 0 && r0 == 0, "basic: trace() returns 0");
  check(fd >= 0 && w == 5 && c == 0 && bad == -1,
        "basic: traced syscalls still return their normal values");
}

// Parent sets the mask, child must inherit it. The parent waits before its
// own last traced call, so per-pid sequences are deterministic.
static void
forkcase(void)
{
  int mask = (1 << SYS_fork) | (1 << SYS_getpid) | (1 << SYS_wait);
  int r = trace(mask);                          // not traced (no SYS_trace bit)
  int child = fork();                           // parent: traced; child: see 5. 풀이
  if (child == 0) {
    getpid();                                   // traced in the child
    getpid();
    exit(0);                                    // never returns -> never printed
  }
  int st = -1;
  int w = wait(&st);                            // traced
  int me = getpid();                            // traced
  trace(0);
  printf("fork: parent=%d child=%d\n", me, child);
  check(r == 0, "fork: trace() returns 0");
  check(child > 0 && w == child && st == 0, "fork: child ran and was reaped");
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "basic") == 0)
    basic();
  else if (argc == 2 && strcmp(argv[1], "fork") == 0)
    forkcase();
  else {
    fprintf(2, "usage: tracetest basic|fork\n");
    exit(1);
  }
  printf("tracetest: %d passed, %d failed\n", npass, nfail);
  exit(nfail != 0);
}
