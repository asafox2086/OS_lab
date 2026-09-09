// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

static int refcnt[(PHYSTOP-KERNBASE)/PGSIZE];
static struct spinlock reflock;

static int
refindex(void *pa)
{
  //my code begin
  return ((uint64)pa - KERNBASE) / PGSIZE;
  //my code end
}


void
kinit()
{
  //my code begin
  char *p = (char*) PGROUNDUP((uint64)end);
  initlock(&reflock, "refcnt");
  bd_init(p,(void*)PHYSTOP);
  //my code end
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  //my code begin
  acquire(&reflock);
  if(--refcnt[refindex(pa)] == 0)
    bd_free(pa);
  release(&reflock);
  //my code end
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  //my code begin
  void *pa = bd_malloc(PGSIZE);
  if(pa){
    acquire(&reflock);
    refcnt[refindex(pa)] = 1;
    release(&reflock);
  }
  return pa;
  //my code end
}

void
krefinc(void *pa)
{
  //my code begin
  acquire(&reflock);
  refcnt[refindex(pa)]++;
  release(&reflock);
  //my code end
}

int
krefcnt(void *pa)
{
  //my code begin
  int count;
  acquire(&reflock);
  count = refcnt[refindex(pa)];
  release(&reflock);
  return count;
  //my code end
}
