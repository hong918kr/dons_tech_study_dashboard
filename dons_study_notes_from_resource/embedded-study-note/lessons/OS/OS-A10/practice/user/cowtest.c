/* ENV: qemu (xv6-riscv kernel/user file - OS-A10 overlay, built by check.sh in the Lima VM) */
// cowtest - copy-on-write fork tests for OS-A10.
// Written for this lesson; the "fork a process that owns more than half of
// RAM" idea comes from the MIT 6.1810 cow lab, the code and the other tests
// are new. Every test is deterministic: exact page counts come from the
// freepages() test hook, and processes that must not race are ordered with
// pipes. Prints "PASS name" / "FAIL name: why", then "cowtest: N/M passed".
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

static int npass, ntotal;

static char area[8 * PGSIZE] __attribute__((aligned(PGSIZE)));
static char cbuf[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
static int gstatus; // a .bss word that wait() writes while a child shares it

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

// grow the heap by n eagerly allocated pages and stamp each page
static char *
grow(int npages, int stamp)
{
  char *p = sbrk(npages * PGSIZE);
  if (p == SBRK_ERROR)
    return 0;
  for (int i = 0; i < npages; i++)
    p[i * PGSIZE] = (char)(stamp + i);
  return p;
}

static void
shrink(int npages)
{
  sbrk(-npages * PGSIZE);
}

// (1) a process that owns 60 % of free RAM can still fork
static void
test_simple(void)
{
  int free0 = freepages();
  int n = free0 / 2 + free0 / 10;
  char *p = grow(n, 1);
  if (p == 0) {
    report("simple", 0, "sbrk failed");
    return;
  }
  int pid = fork();
  if (pid < 0) {
    printf("  simple: fork of a %d-page heap failed, %d pages free\n", n,
           freepages());
    shrink(n);
    report("simple", 0, "fork failed (eager copy needs twice the memory)");
    return;
  }
  if (pid == 0) {
    for (int i = 0; i < n; i++)
      if (p[i * PGSIZE] != (char)(1 + i))
        exit(1);
    p[0] = 99; // exactly one private copy
    exit(0);
  }
  int st = -2;
  wait(&st);
  int ok = st == 0 && p[0] == (char)1;
  shrink(n);
  report("simple", ok, st != 0 ? "child saw wrong data or was killed"
                               : "parent saw the child's write");
}

// (2) fork copies no data pages; each first write in the child costs exactly
// one page; everything comes back after the child exits
static void
test_counts(void)
{
  enum { NP = 64, K = 10 };
  char *p = grow(NP, 7);
  int up[2], down[2];
  if (p == 0 || pipe(up) < 0 || pipe(down) < 0) {
    report("fork-cheap", 0, "setup failed");
    return;
  }
  int procpages = (int)((uint64)sbrk(0) / PGSIZE);
  int before = freepages();
  int pid = fork();
  if (pid == 0) {
    volatile char warm[8];
    char go;
    warm[0] = 1;           // make the stack page private before measuring
    read(down[0], &go, 1); // parent has finished its own first writes
    int a = freepages();
    for (int i = 0; i < K; i++)
      p[i * PGSIZE] = 'c';
    int b = freepages();
    int msg[2] = {a, b};
    write(up[1], msg, sizeof msg);
    exit(warm[0] == 1 ? 0 : 1);
  }
  volatile char pwarm = 1; // parent's stack page goes private here
  (void)pwarm;
  write(down[1], "g", 1);
  int msg[2] = {0, 0};
  read(up[0], msg, sizeof msg);
  int st = -1;
  wait(&st);
  int after = freepages();
  close(up[0]);
  close(up[1]);
  close(down[0]);
  close(down[1]);
  shrink(NP);

  int forkcost = before - msg[0];
  int writecost = msg[0] - msg[1];
  printf("  counts: process %d pages, fork used %d pages, %d child writes "
         "used %d pages\n",
         procpages, forkcost, K, writecost);
  report("fork-cheap", st == 0 && forkcost < NP / 2,
         "fork allocated about as many pages as the process owns");
  report("write-cost", st == 0 && writecost == K,
         "first write to a shared page must cost exactly one page");
  report("fork-no-leak", after == before, "free pages did not return");
}

static int
tagger(char tag, int gofd)
{
  char go;
  if (read(gofd, &go, 1) != 1)
    return 1;
  if (area[7 * PGSIZE] != 'A' + 7) // parent wrote page 7 after the fork
    return 2;
  for (int i = 0; i < 4; i++)
    memset(area + i * PGSIZE, tag, PGSIZE);
  for (int i = 0; i < 4 * PGSIZE; i++)
    if (area[i] != tag)
      return 3;
  for (int i = 4; i < 8; i++)
    if (area[i * PGSIZE] != 'A' + i || area[i * PGSIZE + PGSIZE - 1] != 'A' + i)
      return 4;
  return 0;
}

// (3) parent, two children and a grandchild share the same pages
static void
test_isolation(void)
{
  int go[2];
  for (int i = 0; i < 8; i++)
    memset(area + i * PGSIZE, 'A' + i, PGSIZE);
  if (pipe(go) < 0) {
    report("isolation", 0, "pipe failed");
    return;
  }
  int before = freepages();
  int c1 = fork();
  if (c1 == 0) {
    int g = fork(); // up to four page tables map the same frames
    if (g == 0)
      exit(tagger('g', go[0]));
    int r = tagger('c', go[0]);
    int st = -1;
    wait(&st);
    exit(r ? r : (st ? 10 : 0));
  }
  int c2 = fork();
  if (c2 == 0)
    exit(tagger('d', go[0]));
  memset(area + 7 * PGSIZE, 'Z', PGSIZE); // after the forks: private to parent
  write(go[1], "ggg", 3);
  int s1 = -1, s2 = -1, st = -1;
  for (int k = 0; k < 2; k++) {
    int w = wait(&st);
    if (w == c1)
      s1 = st;
    else if (w == c2)
      s2 = st;
  }
  int ok = 1;
  for (int i = 0; i < 7; i++)
    if (area[i * PGSIZE] != 'A' + i || area[i * PGSIZE + 100] != 'A' + i)
      ok = 0;
  if (area[7 * PGSIZE] != 'Z')
    ok = 0;
  int after = freepages(); // before close(): the pipe page is freed by close
  close(go[0]);
  close(go[1]);
  report("isolation", s1 == 0 && s2 == 0 && ok,
         "a process saw another process's writes");
  report("isolation-no-leak", after == before, "free pages did not return");
}

// (4) the kernel writes user memory with copyout(): no page fault happens,
// so copyout itself must break COW
static void
test_copyout(void)
{
  int fds[2];
  memset(cbuf, 'P', sizeof cbuf);
  if (pipe(fds) < 0) {
    report("pipe-copyout", 0, "pipe failed");
    return;
  }
  int pid = fork();
  if (pid == 0) {
    char *dst = cbuf + PGSIZE - 100; // straddles two COW pages
    if (read(fds[0], dst, 200) != 200)
      exit(1);
    for (int i = 0; i < 200; i++)
      if (dst[i] != (char)('a' + i % 26))
        exit(2);
    if (cbuf[0] != 'P' || cbuf[2 * PGSIZE - 1] != 'P')
      exit(3);
    exit(0);
  }
  char msg[200];
  for (int i = 0; i < 200; i++)
    msg[i] = (char)('a' + i % 26);
  write(fds[1], msg, 200);
  int st = -1;
  wait(&st);
  int ok = 1;
  for (int i = 0; i < 2 * PGSIZE; i++)
    if (cbuf[i] != 'P')
      ok = 0;
  close(fds[0]);
  close(fds[1]);
  report("pipe-copyout", st == 0 && ok,
         st != 0 ? "read() into a COW page failed in the child"
                 : "the parent's buffer changed");

  // wait(&gstatus): the status word sits in a page still shared with child a
  int hold[2];
  pipe(hold);
  gstatus = 0;
  int a = fork();
  if (a == 0) {
    char c;
    read(hold[0], &c, 1);
    exit(gstatus == 0 ? 0 : 1);
  }
  int b = fork();
  if (b == 0)
    exit(7);
  int w = wait(&gstatus); // only b can exit now
  int got = gstatus;
  write(hold[1], "x", 1);
  st = -1;
  int w2 = wait(&st);
  close(hold[0]);
  close(hold[1]);
  report("wait-copyout", w == b && got == 7 && w2 == a && st == 0,
         w != b ? "wait() could not store the status"
                : "the status leaked into the other child");

  // copyout onto a read-only, non-COW page (program text) must still fail
  pipe(fds);
  write(fds[1], "xxxxxxxx", 8);
  char *text = (char *)(uint64)report;
  char save = text[0];
  int n = read(fds[0], text, 8);
  close(fds[0]);
  close(fds[1]);
  report("copyout-readonly", n == -1 && text[0] == save,
         "the kernel wrote into a read-only text page");
}

// (5) after the child exits the parent is the only owner: no copy needed
static void
test_reuse(void)
{
  char *p = grow(16, 3);
  int pid = fork();
  if (pid == 0)
    exit(0);
  wait(0);
  int before = freepages();
  for (int i = 0; i < 16; i++)
    p[i * PGSIZE] = 'r';
  int after = freepages();
  shrink(16);
  report("reuse-refcount-1", pid > 0 && before == after,
         "a page with refcount 1 was copied instead of made writable");
}

// (5b) the refcount-1 path must not allocate: with ZERO free pages, writing a
// COW page that nobody else maps still has to succeed. Runs in a child X; an
// "eater" E takes all free memory first.
static void
reuse_at_oom_child(void)
{
  enum { NA = 16 };
  int go[2], full[2], done[2];
  char *a = grow(NA, 11);
  if (a == 0 || pipe(go) < 0 || pipe(full) < 0 || pipe(done) < 0)
    exit(2);
  int y = fork(); // after y exits, a[] is COW with refcount 1 in X
  if (y == 0)
    exit(0);
  wait(0);
  int e = fork();
  if (e == 0) {
    char c;
    close(done[1]);
    read(go[0], &c, 1);
    shrink(NA); // E drops its share of a[] -> refcount back to 1
    while (sbrk(64 * PGSIZE) != SBRK_ERROR)
      ;
    while (sbrk(PGSIZE) != SBRK_ERROR)
      ;
    write(full[1], "f", 1);
    read(done[0], &c, 1); // EOF when X exits or is killed
    exit(0);
  }
  close(done[0]);
  volatile char warm = 1; // X's stack page goes private while memory exists
  (void)warm;
  write(go[1], "g", 1);
  char c;
  read(full[0], &c, 1);
  int f = freepages();
  for (int i = 0; i < NA; i++)
    a[i * PGSIZE] = 'w'; // must not need kalloc()
  close(done[1]);
  wait(0);
  exit(f < 4 ? 0 : 3);
}

static void
test_reuse_at_oom(void)
{
  int before = freepages();
  int pid = fork();
  if (pid == 0)
    reuse_at_oom_child();
  int st = -2;
  wait(&st);
  int after = freepages();
  report("reuse-at-oom", st == 0 && after == before,
         st == -1  ? "refcount-1 write needed a new page and was killed"
         : st == 3 ? "eater did not exhaust memory"
                   : "setup failed or pages leaked");
}

// (6) running out of memory inside the COW handler kills only that process
static void
test_oom(void)
{
  int free0 = freepages();
  int n = free0 / 2 + free0 / 10;
  char *p = grow(n, 5);
  if (p == 0) {
    report("oom-kill", 0, "sbrk failed");
    return;
  }
  int before = freepages();
  int pid = fork();
  if (pid < 0) {
    shrink(n);
    report("oom-kill", 0, "fork failed");
    return;
  }
  if (pid == 0) {
    for (int i = 0; i < n; i++) // needs n new pages, only ~0.4 n exist
      p[i * PGSIZE] = 0;
    exit(0);
  }
  int st = 0;
  wait(&st);
  int intact = 1;
  for (int i = 0; i < n; i++)
    if (p[i * PGSIZE] != (char)(5 + i))
      intact = 0;
  int after = freepages();
  shrink(n);
  report("oom-kill", st == -1 && intact && after == before,
         st != -1 ? "child was not killed" : "memory or parent data lost");
}

static int
store_in_child(uint64 addr)
{
  int pid = fork();
  if (pid == 0) {
    *(volatile char *)addr = 1;
    exit(0);
  }
  int st = 0;
  wait(&st);
  return st;
}

// (7) store faults that are not COW faults must still kill the process
static void
test_kills(void)
{
  uint64 top = (uint64)sbrk(0);
  report("kill-beyond-sz", store_in_child(top + 16 * PGSIZE) == -1,
         "store above p->sz did not kill");
  report("kill-text-write", store_in_child((uint64)report) == -1,
         "store into read-only text did not kill");
  report("kill-trampoline", store_in_child(MAXVA - PGSIZE) == -1,
         "store into the trampoline did not kill");
  report("kill-maxva", store_in_child(MAXVA) == -1,
         "store at MAXVA did not kill");
  report("kill-high-va", store_in_child(0xfffffffffffff000ULL) == -1,
         "store at 0xfffffffffffff000 did not kill");
}

// (8) many fork/write/exit rounds leak nothing
static void
test_loop(void)
{
  char *p = grow(8, 9);
  int before = freepages();
  for (int r = 0; r < 20; r++) {
    int pid = fork();
    if (pid == 0) {
      for (int i = 0; i < 8; i++)
        p[i * PGSIZE] = (char)r;
      exit(0);
    }
    wait(0);
  }
  int after = freepages();
  shrink(8);
  report("fork-loop-no-leak", after == before, "pages leaked across 20 forks");
}

int
main(void)
{
  printf("cowtest: start, %d free pages\n", freepages());
  test_simple();
  test_counts();
  test_isolation();
  test_copyout();
  test_reuse();
  test_reuse_at_oom();
  test_oom();
  test_kills();
  test_loop();
  printf("cowtest: %d/%d passed\n", npass, ntotal);
  printf("cowtest: done\n");
  exit(npass == ntotal ? 0 : 1);
}
