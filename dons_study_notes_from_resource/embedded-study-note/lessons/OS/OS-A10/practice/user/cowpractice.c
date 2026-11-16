/* ENV: qemu (xv6-riscv kernel/user file - OS-A10 overlay, built by check.sh in the Lima VM) */
// cowpractice - tests for the OS-A10 extra exercises:
//   1. cowstat(): copies vs in-place reuses, and what fork+exec really copies
//   2. shared zero page for read faults on lazy (sbrklazy) memory
//   3. refaudit(): recount user mappings and compare with the refcounts
// Same output format as cowtest ("PASS name" / "FAIL name: why").
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

static int npass, ntotal;

static void
report(const char *name, int ok, const char *why)
{
  ntotal++;
  if (ok) {
    npass++;
    printf("PASS %s\n", name);
  } else {
    printf("FAIL %s: %s\n", name, why);
  }
}

static char *
grow(int npages)
{
  char *p = sbrk(npages * PGSIZE);
  if (p == SBRK_ERROR)
    return 0;
  for (int i = 0; i < npages; i++)
    p[i * PGSIZE] = (char)i;
  return p;
}

// 1. counters
static void
test_stat(void)
{
  enum { NP = 64, K = 10 };
  char *p = grow(NP);
  int up[2], down[2];
  pipe(up);
  pipe(down);
  int pid = fork();
  if (pid == 0) {
    volatile char warm[4];
    char go;
    warm[0] = 1;
    read(down[0], &go, 1);
    int c0 = cowstat(0);
    for (int i = 0; i < K; i++)
      p[i * PGSIZE] = 'x';
    int c1 = cowstat(0);
    int d = c1 - c0;
    write(up[1], &d, sizeof d);
    exit(warm[0] == 1 ? 0 : 1);
  }
  volatile char pwarm = 1;
  (void)pwarm;
  write(down[1], "g", 1);
  int d = -1;
  read(up[0], &d, sizeof d);
  wait(0);
  report("stat-copies", d == K, "child's K first writes must be K copies");

  // the child is gone: the parent owns every page alone now
  int c0 = cowstat(0), r0 = cowstat(1);
  for (int i = 0; i < K; i++)
    p[i * PGSIZE] = 'y';
  int c1 = cowstat(0), r1 = cowstat(1);
  printf("  stat: parent after child exit: %d copies, %d reuses\n", c1 - c0,
         r1 - r0);
  report("stat-reuses", c1 == c0 && r1 - r0 == K,
         "refcount-1 pages must be reused, not copied");

  // fork + exec: how many pages does the fork really copy?
  int procpages = (int)((uint64)sbrk(0) / PGSIZE);
  c0 = cowstat(0);
  r0 = cowstat(1);
  pid = fork();
  if (pid == 0) {
    char *argv[] = {"echo", "  fork-exec: hello from echo", 0};
    exec("echo", argv);
    exit(1);
  }
  int st = -1;
  wait(&st);
  c1 = cowstat(0);
  r1 = cowstat(1);
  printf("  fork-exec: process of %d pages -> %d copies, %d reuses\n",
         procpages, c1 - c0, r1 - r0);
  report("fork-exec-copies", st == 0 && c1 - c0 <= 4,
         "fork+exec should copy only a few stack/data pages");
  close(up[0]);
  close(up[1]);
  close(down[0]);
  close(down[1]);
  sbrk(-NP * PGSIZE);
}

// 2. zero page
static void
test_zero(void)
{
  enum { NP = 64, K = 10 };
  char *p = sbrklazy(NP * PGSIZE);
  int f0 = freepages();
  int sum = 0;
  for (int i = 0; i < NP; i++)
    sum += p[i * PGSIZE] + p[i * PGSIZE + PGSIZE - 1];
  int f1 = freepages();
  printf("  zero: reading %d lazy pages used %d pages\n", NP, f0 - f1);
  report("zero-page-read", sum == 0 && f0 - f1 <= 2,
         "read faults must map the shared zero page, not allocate");

  for (int i = 0; i < K; i++)
    p[i * PGSIZE] = 'z';
  int f2 = freepages();
  int ok = f1 - f2 == K;
  for (int i = 0; i < NP; i++)
    if (p[i * PGSIZE] != (i < K ? 'z' : 0) || p[i * PGSIZE + 1] != 0)
      ok = 0;
  report("zero-page-write", ok, "first write to a zero-page mapping = 1 copy");

  int pid = fork();
  if (pid == 0) {
    for (int i = 0; i < NP; i++)
      if (p[i * PGSIZE] != (i < K ? 'z' : 0))
        exit(1);
    p[NP - 1] = 7; // copy of the zero page, private to the child
    p[(NP - 1) * PGSIZE] = 7;
    exit(0);
  }
  int st = -1;
  wait(&st);
  report("zero-page-fork", st == 0 && p[(NP - 1) * PGSIZE] == 0,
         "zero page shared across fork leaked a write");
  sbrk(-NP * PGSIZE); // unmapping drops zero-page references, never frees it
}

// 3. audit
static void
test_audit(void)
{
  char *p = grow(32);
  int hold[2], ready[2];
  pipe(hold);
  pipe(ready);
  for (int k = 0; k < 2; k++) {
    if (fork() == 0) {
      char c;
      close(hold[1]);
      p[k * PGSIZE] = 'k'; // one private copy each, the rest stays shared
      write(ready[1], "r", 1);
      read(hold[0], &c, 1); // EOF releases us
      exit(0);
    }
  }
  char r2[2];
  for (int got = 0; got < 2;) // both children have made their copy
    got += read(ready[0], r2 + got, 2 - got);
  int bad = refaudit(0);
  int bad_inj = refaudit(1);
  close(hold[0]);
  close(hold[1]);
  close(ready[0]);
  close(ready[1]);
  wait(0);
  wait(0);
  int bad_after = refaudit(0);
  printf("  audit: live %d, injected %d, after exit %d mismatches\n", bad,
         bad_inj, bad_after);
  report("audit-clean", bad == 0, "refcounts disagree with page tables");
  report("audit-detects", bad_inj == 1, "audit missed one injected reference");
  report("audit-after-exit", bad_after == 0, "refcounts wrong after exit");
  sbrk(-32 * PGSIZE);
}

int
main(void)
{
  printf("cowpractice: start\n");
  test_stat();
  test_zero();
  test_audit();
  printf("cowpractice: %d/%d passed\n", npass, ntotal);
  printf("cowpractice: done\n");
  exit(npass == ntotal ? 0 : 1);
}
