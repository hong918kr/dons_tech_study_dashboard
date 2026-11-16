/* ENV: qemu (xv6-riscv kernel/user file - OS-A10 overlay, built by check.sh in the Lima VM) */
// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// OS-A10: one reference count per physical page of RAM, indexed by
// (pa - KERNBASE) / PGSIZE. A page shared copy-on-write by k page tables
// has count k; it goes back on the free list only when the count reaches 0.
#define NPHYS     ((PHYSTOP - KERNBASE) / PGSIZE)
#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  int cnt[NPHYS];
} kref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    kref.cnt[PA2IDX(p)] = 1; // kfree() below drops it to 0 and frees
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kref.lock);
  if (kref.cnt[PA2IDX(pa)] < 1)
    panic("kfree: refcount");
  int left = --kref.cnt[PA2IDX(pa)];
  release(&kref.lock);
  if (left > 0)
    return; // another page table still maps it

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r) {
    acquire(&kref.lock);
    kref.cnt[PA2IDX(r)] = 1;
    release(&kref.lock);
    memset((char *)r, 5, PGSIZE); // fill with junk
  }
  return (void *)r;
}

// OS-A10: one more page table maps pa.
void
krefinc(void *pa)
{
  acquire(&kref.lock);
  if (kref.cnt[PA2IDX(pa)] < 1)
    panic("krefinc");
  kref.cnt[PA2IDX(pa)]++;
  release(&kref.lock);
}

// OS-A10: current reference count of pa.
int
krefcnt(void *pa)
{
  int n;

  acquire(&kref.lock);
  n = kref.cnt[PA2IDX(pa)];
  release(&kref.lock);
  return n;
}

// OS-A10 test hook: number of pages on the free list.
int
kfreepages(void)
{
  int n = 0;

  acquire(&kmem.lock);
  for (struct run *r = kmem.freelist; r; r = r->next)
    n++;
  release(&kmem.lock);
  return n;
}

// OS-A10 practice 3: refcount audit (a "fsck" for kref.cnt).
// For every process, count how many page tables map each frame below p->sz
// (+1 for the kernel's pin on the shared zero page) and compare with
// kref.cnt. Only frames mapped into some user page table are audited
// (page-table pages, kernel stacks and pipes are not). This is a test hook
// for a quiescent system: it does not stop other CPUs from changing page
// tables while it walks them.
static int expect[NPHYS];
extern struct proc proc[NPROC];

int
krefaudit(int inject)
{
  int bad = 0;
  uint64 victim = 0;

  memset(expect, 0, sizeof expect);
  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->pagetable) {
      for (uint64 va = 0; va < p->sz; va += PGSIZE) {
        pte_t *pte = walk(p->pagetable, va, 0);
        if (pte && (*pte & PTE_V))
          expect[PA2IDX(PTE2PA(*pte))]++;
      }
    }
    release(&p->lock);
  }
  if (zeropage)
    expect[PA2IDX(zeropage)]++;

  if (inject) // negative control: one extra reference on the caller's page 0
    victim = walkaddr(myproc()->pagetable, 0);
  acquire(&kref.lock);
  if (victim)
    kref.cnt[PA2IDX(victim)]++;
  for (int i = 0; i < NPHYS; i++)
    if (expect[i] && expect[i] != kref.cnt[i])
      bad++;
  if (victim)
    kref.cnt[PA2IDX(victim)]--;
  release(&kref.lock);
  return bad;
}
