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

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

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
  for(int i = 0; i < NCPU; i++){ // 初始化每个 CPU 的页缓存链表
    initlock(&kmem[i].lock, "kmem"); // 初始化当前 CPU 页缓存的锁
    kmem[i].freelist = 0; // 当前 CPU 的缓存链表初始为空
  }
  for(int i = 0; i < NCPU * 64; i++){ // 为各 CPU 预取少量页面以减少首次竞争
    struct run *page = bd_malloc(PGSIZE); // 从 buddy 后备分配器申请一个整页
    if(page == 0) // 后备分配器耗尽时停止预取
      break; // 结束预取循环
    int cpu = i % NCPU; // 轮流将页面分配给每个 CPU 的缓存
    page->next = kmem[cpu].freelist; // 将页面插入该 CPU 缓存链表头部
    kmem[cpu].freelist = page; // 更新链表头指针
  }
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
  int free_page; // 记录当前调用是否释放最后一个页面引用
  push_off(); // 关闭中断后才能安全地读取当前 CPU 编号
  int cpu = cpuid(); // 获取归还页面的 CPU 编号
  acquire(&reflock); // 保护物理页引用计数
  free_page = --refcnt[refindex(pa)] == 0; // 减少引用并判断是否应回收页面
  release(&reflock); // 释放引用计数锁
  if(free_page){ // 只有最后一个引用消失时才回收页面
    struct run *page = pa; // 将空闲物理页解释为链表节点
    acquire(&kmem[cpu].lock); // 锁住当前 CPU 的页缓存
    page->next = kmem[cpu].freelist; // 将页面插入本地缓存链表头部
    kmem[cpu].freelist = page; // 更新本地缓存链表头指针
    release(&kmem[cpu].lock); // 释放本地缓存锁
  }
  pop_off(); // 恢复调用前的中断状态
  //my code end
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  //my code begin
  void *pa = 0; // 保存从本地、其他 CPU 或 buddy 获得的物理页
  push_off(); // 关闭中断后才能安全地读取当前 CPU 编号
  int cpu = cpuid(); // 获取当前 CPU 编号
  acquire(&kmem[cpu].lock); // 锁住当前 CPU 的页缓存
  if(kmem[cpu].freelist){ // 本地缓存存在空闲页面
    pa = kmem[cpu].freelist; // 取出链表头部页面
    kmem[cpu].freelist = kmem[cpu].freelist->next; // 更新本地缓存链表头
  }
  release(&kmem[cpu].lock); // 释放本地缓存锁
  for(int i = 0; pa == 0 && i < NCPU; i++){ // 本地为空时依次尝试从其他 CPU 偷取
    if(i == cpu) // 不重复检查当前 CPU
      continue; // 继续检查下一 CPU
    acquire(&kmem[i].lock); // 锁住被偷取 CPU 的页缓存
    if(kmem[i].freelist){ // 被偷取缓存中存在空闲页面
      pa = kmem[i].freelist; // 取出一个空闲页面
      kmem[i].freelist = kmem[i].freelist->next; // 更新被偷取缓存的链表头
    }
    release(&kmem[i].lock); // 释放被偷取缓存锁
  }
  if(pa == 0) // 所有 CPU 缓存都为空时才访问全局 buddy 分配器
    pa = bd_malloc(PGSIZE); // 从 buddy 后备分配器申请一个整页
  if(pa){ // 分配成功时初始化引用计数
    acquire(&reflock); // 保护引用计数数组
    refcnt[refindex(pa)] = 1; // 新页面由当前调用者独占
    release(&reflock); // 释放引用计数锁
  }
  pop_off(); // 恢复调用前的中断状态
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
