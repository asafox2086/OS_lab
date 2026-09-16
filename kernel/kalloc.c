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
  return ((uint64)pa - KERNBASE) / PGSIZE; // 将物理页地址转换为引用计数数组下标
  //my code end
}


void
kinit()
{
  //my code begin
  char *p = (char*) PGROUNDUP((uint64)end); // 计算内核镜像之后的首个页对齐地址
  initlock(&reflock, "refcnt"); // 初始化物理页引用计数锁
  bd_init(p,(void*)PHYSTOP); // 使用剩余物理内存初始化伙伴分配器
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
  acquire(&reflock); // 保护物理页引用计数
  if(--refcnt[refindex(pa)] == 0) // 减少引用，检查是否为最后一个引用
    bd_free(pa); // 最后一个引用消失时归还伙伴分配器
  release(&reflock); // 释放引用计数锁
  //my code end
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  //my code begin
  void *pa = bd_malloc(PGSIZE); // 从伙伴分配器申请一个物理页
  if(pa){ // 分配成功时初始化引用计数
    acquire(&reflock); // 保护引用计数数组
    refcnt[refindex(pa)] = 1; // 新页面由当前调用者独占
    release(&reflock); // 释放引用计数锁
  }
  return pa; // 返回物理页地址或空指针
  //my code end
}

void
krefinc(void *pa)
{
  //my code begin
  acquire(&reflock); // 保护引用计数数组
  refcnt[refindex(pa)]++; // 增加共享页面的引用计数
  release(&reflock); // 释放引用计数锁
  //my code end
}

int
krefcnt(void *pa)
{
  //my code begin
  int count; // 保存读取到的引用计数
  acquire(&reflock); // 保护引用计数数组
  count = refcnt[refindex(pa)]; // 读取指定页面的引用次数
  release(&reflock); // 释放引用计数锁
  return count; // 返回引用次数
  //my code end
}
