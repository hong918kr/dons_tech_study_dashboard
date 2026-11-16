/* ENV: qemu (OS-A09 overlay on xv6-riscv fa2ddcb; built + booted by check.sh inside the Lima VM) */
// sysinfotest: exact checks of sysinfo(). Run from the shell with CPUS=1 and
// nothing else running: then the only allocations between two sysinfo()
// calls are the ones this program causes.
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
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

static struct sysinfo
info(void)
{
  struct sysinfo si;
  si.freemem = 0;
  si.nproc = 0;
  if (sysinfo(&si) < 0) {
    si.freemem = 0;
    si.nproc = 0;
  }
  return si;
}

static void
basics(void)
{
  struct sysinfo si;
  si.freemem = 0;
  si.nproc = 0;
  int r = sysinfo(&si);
  printf("sysinfo: r=%d freemem=%ld (%ld pages) nproc=%ld\n", r, si.freemem,
         si.freemem / PGSIZE, si.nproc);
  check(r == 0, "sysinfo returns 0");
  check(si.freemem > 0 && si.freemem % PGSIZE == 0 && si.freemem < 128 * 1024 * 1024,
        "freemem is a positive multiple of PGSIZE below 128 MiB");
  // init + sh + sysinfotest
  check(si.nproc == 3, "nproc == 3 (init, sh, sysinfotest)");
}

// Eager sbrk(k pages) must take exactly k pages from the freelist and the
// shrink must give them back. k stays small so no new page-table page is
// needed (one leaf table covers 2 MiB).
static void
sbrkpages(int k, char *name)
{
  uint64 before = info().freemem;
  char *p = sbrk(k * PGSIZE);
  uint64 grown = info().freemem;
  sbrk(-k * PGSIZE);
  uint64 after = info().freemem;
  printf("sbrk %d pages: %ld -> %ld -> %ld\n", k, before, grown, after);
  check(p != SBRK_ERROR && before > 0 && before - grown == (uint64)k * PGSIZE &&
            after == before,
        name);
}

// A blocked child is +1 process and costs memory; a zombie still counts;
// after wait() both numbers are back exactly (nothing leaked).
static void
forkcount(void)
{
  int fds[2];
  char c;
  if (pipe(fds) < 0) {
    check(0, "pipe");
    return;
  }
  struct sysinfo base = info();
  int pid = fork();
  if (pid == 0) {
    read(fds[0], &c, 1); // wait for the parent's go
    exit(0);
  }
  struct sysinfo alive = info();
  write(fds[1], "x", 1); // let the child exit
  int st = -1;
  int w = wait(&st);
  struct sysinfo reaped = info(); // pipe still open: its page is not freed yet
  close(fds[0]);
  close(fds[1]);
  printf("fork: nproc %ld -> %ld -> %ld, freemem %ld -> %ld -> %ld\n", base.nproc,
         alive.nproc, reaped.nproc, base.freemem, alive.freemem, reaped.freemem);
  check(pid > 0 && w == pid && st == 0, "fork: child ran");
  check(alive.nproc == base.nproc + 1, "fork: nproc +1 while the child lives");
  check(alive.freemem < base.freemem, "fork: the child costs memory");
  check(reaped.nproc == base.nproc && reaped.freemem == base.freemem,
        "fork: nproc and freemem back exactly after wait");
}

// A zombie is not UNUSED: it counts until the parent reaps it.
static void
zombie(void)
{
  int fds[2];
  char c;
  if (pipe(fds) < 0) {
    check(0, "pipe");
    return;
  }
  uint64 base = info().nproc;
  int pid = fork();
  if (pid == 0) {
    close(fds[0]);
    exit(0); // closing fds[1] at exit -> parent's read sees EOF
  }
  close(fds[1]);
  int n = read(fds[0], &c, 1); // returns 0 once the child closed its end
  close(fds[0]);
  uint64 dying = info().nproc; // child is exiting or ZOMBIE: still counted
  int w = wait(0);
  uint64 after = info().nproc;
  printf("zombie: nproc %ld -> %ld -> %ld\n", base, dying, after);
  check(n == 0 && w == pid, "zombie: child exited");
  check(dying == base + 1 && after == base, "zombie: counted until wait()");
}

// copyout must reject pointers the process cannot write.
static void
badptr(void)
{
  struct sysinfo si;
  char *top = sbrk(PGSIZE);           // one writable heap page [top, top+PGSIZE)
  char *end = top + PGSIZE;           // == p->sz
  int r1 = sysinfo((struct sysinfo *)0xffffffffffffff00ULL); // >= MAXVA
  int r2 = sysinfo((struct sysinfo *)(end + 8 * PGSIZE));    // above p->sz
  int r3 = sysinfo((struct sysinfo *)0);                     // text page, no PTE_W
  int r4 = sysinfo((struct sysinfo *)(end - 8));             // straddles p->sz
  int r5 = sysinfo((struct sysinfo *)(end - sizeof(si)));    // last 16 bytes: ok
  sbrk(-PGSIZE);
  printf("badptr: %d %d %d %d %d\n", r1, r2, r3, r4, r5);
  check(r1 == -1, "badptr: address >= MAXVA -> -1");
  check(r2 == -1, "badptr: address above p->sz -> -1");
  check(r3 == -1, "badptr: NULL (read-only text page) -> -1");
  check(r4 == -1, "badptr: struct straddling p->sz -> -1");
  check(r5 == 0, "badptr: struct ending exactly at p->sz -> 0");
}

int
main(void)
{
  basics();
  sbrkpages(1, "sbrk 1 page: freemem -1 page, then back");
  sbrkpages(7, "sbrk 7 pages: freemem -7 pages, then back");
  sbrkpages(64, "sbrk 64 pages: freemem -64 pages, then back");
  forkcount();
  zombie();
  badptr();
  printf("sysinfotest: %d passed, %d failed\n", npass, nfail);
  exit(nfail != 0);
}
