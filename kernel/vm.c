#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void print(pagetable_t);

/*
 * create a direct-map page table for the kernel and
 * turn on paging. called early, in supervisor mode.
 * the page allocator is already initialized.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface 0
  kvmmap(VIRTION(0), VIRTION(0), PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface 1
  kvmmap(VIRTION(1), VIRTION(1), PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..39 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..12 -- 12 bits of byte offset within the page.
static pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  //my code begin
  pte_t *pte; // 保存目标虚拟地址对应的页表项
  uint64 pa; // 保存页表项中的物理页地址

  if(va >= MAXVA) // 拒绝超出 Sv39 虚拟地址范围的地址
    return 0; // 地址非法时返回失败

  pte = walk(pagetable, va, 0); // 查找地址已有的页表项
  if(pte == 0 || (*pte & PTE_V) == 0){ // 尚未建立有效映射时按需分配
    struct proc *p = myproc(); // 获取当前进程的 VMA 信息
    for(int i = 0; i < 16; i++) // 遍历所有文件映射区域
      if(p->vmas[i].used && va >= p->vmas[i].addr && // 判断地址是否落在 VMA 起点之后
         va < p->vmas[i].addr + p->vmas[i].length) // 判断地址是否落在 VMA 终点之前
        return 0; // 文件映射只能由缺页异常处理，不能在内核拷贝中分配
    if(lazyalloc(va) < 0) // 为普通懒分配地址建立页面
      return 0; // 分配失败时返回失败
    pte = walk(pagetable, va, 0); // 重新取得新建的页表项
  }
  if(pte == 0) // 页表项仍不存在
    return 0; // 返回失败
  if((*pte & PTE_V) == 0) // 页表项无效
    return 0; // 返回失败
  if((*pte & PTE_U) == 0) // 页面不允许用户访问
    return 0; // 返回失败
  pa = PTE2PA(*pte); // 从页表项提取物理页地址
  return pa; // 返回物理页地址
  //my code end
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

int
lazyalloc(uint64 va)
{
  //my code begin
  char *mem; // 保存新分配的物理页
  struct proc *p = myproc(); // 获取当前进程
  uint64 a = PGROUNDDOWN(va); // 将缺页地址向下对齐到页边界

  if(p == 0 || va >= p->sz || va < PGROUNDDOWN(p->tf->sp)) // 检查地址是否属于进程的堆区域
    return -1; // 非法地址不能懒分配

  mem = kalloc(); // 分配一个用户物理页
  if(mem == 0) // 检查物理内存是否耗尽
    return -1; // 分配失败
  memset(mem, 0, PGSIZE); // 清零新页面
  if(mappages(p->pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){ // 建立用户可读写执行映射
    kfree(mem); // 映射失败时回收页面
    return -1; // 返回失败
  }
  return 0; // 懒分配成功
  //my code end
}

int
mmapalloc(uint64 va, int scause)
{
  //my code begin
  struct proc *p = myproc(); // 获取发生缺页的进程
  struct vma *v = 0; // 保存命中的虚拟内存区域
  uint64 a = PGROUNDDOWN(va); // 计算待映射页面的起始地址
  char *mem; // 保存新分配的物理页
  uint n; // 保存本页需要读取或写回的字节数
  uint64 off; // 保存本页对应的文件偏移
  int perm = PTE_U; // 初始化用户页权限

  for(int i = 0; i < 16; i++) // 查找包含缺页地址的 VMA
    if(p->vmas[i].used && va >= p->vmas[i].addr && // VMA 必须已使用且地址不小于起点
       va < p->vmas[i].addr + p->vmas[i].length) // 地址必须小于 VMA 终点
      v = &p->vmas[i]; // 记录命中的 VMA
  if(v == 0 || (scause == 13 && !(v->prot & PROT_READ)) || // 读异常必须具有读权限
     (scause == 15 && !(v->prot & PROT_WRITE))) // 写异常必须具有写权限
    return -1; // 地址或访问权限无效
  if((mem = kalloc()) == 0) // 为文件页分配物理内存
    return -1; // 内存不足
  memset(mem, 0, PGSIZE); // 先清零，以保证文件尾部为零
  off = v->offset + a - v->addr; // 计算本虚拟页在文件中的偏移
  ilock(v->file->ip); // 锁定文件 inode 后读取内容
  n = 0; // 默认文件尾部没有可读数据
  if(off < v->file->ip->size) // 仅在偏移位于文件内时读取
    n = v->file->ip->size - off < PGSIZE ? v->file->ip->size - off : PGSIZE; // 限制读取长度不超过一页
  if(n && readi(v->file->ip, 0, (uint64)mem, off, n) != n){ // 将文件内容读入物理页
    iunlock(v->file->ip); // 读取失败前释放 inode 锁
    kfree(mem); // 回收已分配页面
    return -1; // 返回失败
  }
  iunlock(v->file->ip); // 文件读取完成后释放 inode 锁
  if(v->prot & PROT_READ) // 根据 VMA 读权限设置页权限
    perm |= PTE_R; // 允许用户读取此页
  if(v->prot & PROT_WRITE) // 根据 VMA 写权限设置页权限
    perm |= PTE_W; // 允许用户写入此页
  if(mappages(p->pagetable, a, PGSIZE, (uint64)mem, perm) < 0){ // 将物理页映射到用户地址
    kfree(mem); // 映射失败时回收物理页
    return -1; // 返回失败
  }
  return 0; // 文件页映射成功
  //my code end
}

static void
mmapunmap_page(struct proc *p, struct vma *v, uint64 va)
{
  //my code begin
  pte_t *pte = walk(p->pagetable, va, 0); // 获取待解除映射的页表项
  if(pte == 0 || (*pte & PTE_V) == 0) // 未实际缺页分配的页面无需处理
    return; // 直接返回
  if(v->flags == MAP_SHARED){ // 共享映射需要将修改写回文件
    uint64 off = v->offset + va - v->addr; // 计算该页的文件偏移
    uint n = 0; // 初始化需要写回的长度
    begin_op(v->file->ip->dev); // 开始文件系统日志事务
    ilock(v->file->ip); // 锁定 inode 后更新文件
    if(off < v->file->ip->size) // 仅写回文件范围内的内容
      n = v->file->ip->size - off < PGSIZE ? v->file->ip->size - off : PGSIZE; // 限制写回长度不超过一页
    if(n){ // 有有效文件数据时才写回
      writei(v->file->ip, 0, PTE2PA(*pte), off, n); // 将物理页内容写入文件
    }
    iunlock(v->file->ip); // 释放 inode 锁
    end_op(v->file->ip->dev); // 提交文件系统日志事务
  }
  uvmunmap(p->pagetable, va, PGSIZE, 1); // 移除映射并释放物理页
  //my code end
}

int
mmapunmap(struct proc *p, uint64 addr, uint64 length)
{
  //my code begin
  for(int i = 0; i < 16; i++){ // 遍历进程的所有 VMA
    struct vma *v = &p->vmas[i]; // 取得当前 VMA
    if(!v->used || addr < v->addr || addr + length > v->addr + v->length) // 跳过不包含解除范围的 VMA
      continue; // 继续查找下一项
    if(addr != v->addr && addr + length != v->addr + v->length) // 只允许解除 VMA 的前缀、后缀或全部
      return -1; // 中间切分 VMA 时拒绝请求
    for(uint64 va = PGROUNDDOWN(addr); va < addr + length; va += PGSIZE) // 按页解除映射
      mmapunmap_page(p, v, va); // 写回共享页并删除该页映射
    if(addr == v->addr && length == v->length){ // 整个 VMA 都被解除
      fileclose(v->file); // 释放 VMA 持有的文件引用
      memset(v, 0, sizeof(*v)); // 清空 VMA 槽位
    } else if(addr == v->addr){ // 解除 VMA 的前缀
      v->addr += length; // 将 VMA 起点向后移动
      v->length -= length; // 缩短 VMA 长度
      v->offset += length; // 同步推进文件偏移
    } else {
      v->length -= length; // 解除后缀时只缩短 VMA 长度
    }
    return 0; // 成功处理一个 VMA
  }
  return -1; // 没有找到匹配的 VMA
  //my code end
}

void
mmapexit(struct proc *p)
{
  //my code begin
  for(int i = 0; i < 16; i++) // 遍历进程的所有 VMA
    if(p->vmas[i].used) // 仅处理已使用的映射槽位
      mmapunmap(p, p->vmas[i].addr, p->vmas[i].length); // 解除整段映射并释放文件引用
  //my code end
}

int
cowalloc(pagetable_t pagetable, uint64 va)
{
  //my code begin
  pte_t *pte; // 保存发生写异常的页表项
  uint64 pa; // 保存原共享物理页地址
  uint flags; // 保存并修改页表权限位
  char *mem; // 保存复制后的新物理页

  va = PGROUNDDOWN(va); // 将异常地址对齐到页边界
  if(va >= MAXVA || (pte = walk(pagetable, va, 0)) == 0 || // 查找有效页表项
     (*pte & PTE_V) == 0 || (*pte & PTE_COW) == 0 || (*pte & PTE_U) == 0) // 验证它是用户 COW 页
    return -1; // 非 COW 页不能由此函数处理

  pa = PTE2PA(*pte); // 取得原共享物理页
  if(krefcnt((void*)pa) == 1){ // 仅剩当前进程引用时无需复制
    *pte = (*pte | PTE_W) & ~PTE_COW; // 恢复可写权限并清除 COW 标记
    return 0; // COW 处理完成
  }

  if((mem = kalloc()) == 0) // 为私有副本分配新页面
    return -1; // 内存不足
  memmove(mem, (char*)pa, PGSIZE); // 复制原共享页的全部内容
  flags = PTE_FLAGS(*pte); // 读取旧页的权限位
  flags = (flags | PTE_W) & ~PTE_COW; // 为新页设置可写且非 COW 权限
  *pte = PA2PTE(mem) | flags | PTE_V; // 将页表项改为指向私有副本
  kfree((void*)pa); // 释放原共享页的一次引用
  return 0; // COW 复制成功
  //my code end
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove mappings from a page table. The mappings in
// the given range must exist. Optionally free the
// physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 size, int do_free)
{
  //my code begin
  uint64 a, last; // 保存当前页地址和解除范围的最后一页地址
  pte_t *pte; // 保存当前页的页表项
  uint64 pa; // 保存待释放的物理页地址

  a = PGROUNDDOWN(va); // 对齐解除范围的起始地址
  last = PGROUNDDOWN(va + size - 1); // 计算解除范围的最后一页
  for(;;){ // 逐页删除映射
    if((pte = walk(pagetable, a, 0)) == 0){ // 当前页没有页表项
      if(a == last) // 已处理到最后一页
        break; // 结束循环
      a += PGSIZE; // 移动到下一页
      continue; // 跳过不存在的映射
    }
    if((*pte & PTE_V) == 0){ // 页表项无效
      if(a == last) // 已处理到最后一页
        break; // 结束循环
      a += PGSIZE; // 移动到下一页
      continue; // 跳过未分配的懒映射页
    }
    if(PTE_FLAGS(*pte) == PTE_V) // 不允许删除中间页表节点
      panic("uvmunmap: not a leaf"); // 发现非叶子项时终止内核
    if(do_free){ // 调用者要求回收物理页
      pa = PTE2PA(*pte); // 取得物理页地址
      kfree((void*)pa); // 减少引用计数并在需要时释放页面
    }
    *pte = 0; // 清除当前页的页表映射
    if(a == last) // 已处理到最后一页
      break; // 结束循环
    a += PGSIZE; // 移动到下一页
    pa += PGSIZE; // 保持物理地址变量的页步进
  }
  //my code end
}

// create an empty user page table.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    panic("uvmcreate: out of memory");
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  a = oldsz;
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  uint64 newup = PGROUNDUP(newsz);
  if(newup < PGROUNDUP(oldsz))
    uvmunmap(pagetable, newup, oldsz - newup, 1);

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, 0, sz, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  //my code begin
  pte_t *pte; // 保存父进程当前页的页表项
  uint64 pa, i; // 保存共享物理页地址和页循环索引
  uint flags; // 保存复制到子进程的页权限

  for(i = 0; i < sz; i += PGSIZE){ // 遍历父进程的每个用户页
    if((pte = walk(old, i, 0)) == 0) // 跳过没有页表项的懒分配页
      continue; // 继续处理下一页
    if((*pte & PTE_V) == 0) // 跳过无效页表项
      continue; // 继续处理下一页
    pa = PTE2PA(*pte); // 取得父子进程将共享的物理页
    flags = PTE_FLAGS(*pte); // 取得原页面权限
    if(flags & PTE_W) // 原可写页需要转换为写时复制页
      flags = (flags | PTE_COW) & ~PTE_W; // 子进程页设为只读 COW
    if(mappages(new, i, PGSIZE, pa, flags) != 0) // 在子进程页表建立共享映射
      goto err; // 映射失败时清理已经建立的映射
    if(PTE_FLAGS(*pte) & PTE_W) // 父进程原页可写时同步改为 COW
      *pte = (*pte | PTE_COW) & ~PTE_W; // 移除父页写权限并设置 COW 标志
    krefinc((void*)pa); // 增加共享物理页的引用计数
  }
  return 0; // 所有页面复制成功

 err:
  uvmunmap(new, 0, i, 1); // 释放子进程中已建立的共享映射
  return -1; // 返回复制失败
  //my code end
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  //my code begin
  uint64 n, va0, pa0; // 保存本次拷贝长度、页地址和物理地址

  while(len > 0){ // 按页向用户空间复制数据
    va0 = PGROUNDDOWN(dstva); // 计算目标地址所在页的起点
    if(va0 < MAXVA){ // 仅处理合法用户虚拟地址
      pte_t *pte = walk(pagetable, va0, 0); // 查找目标页的页表项
      if(pte && (*pte & PTE_COW) && cowalloc(pagetable, va0) < 0) // 内核写入 COW 页前先创建私有副本
        return -1; // COW 分配失败
    }
    pa0 = walkaddr(pagetable, va0); // 将目标虚拟页转换为物理页
    if(pa0 == 0) // 地址转换或懒分配失败
      return -1; // 返回失败
    n = PGSIZE - (dstva - va0); // 计算当前页剩余可写字节数
    if(n > len) // 最后一页可能不足整页
      n = len; // 限制拷贝长度为剩余数据长度
    memmove((void *)(pa0 + (dstva - va0)), src, n); // 将内核数据写入用户物理页

    len -= n; // 扣除已复制字节数
    src += n; // 移动内核源地址
    dstva = va0 + PGSIZE; // 移动到下一用户页
  }
  return 0; // 全部数据复制成功
  //my code end
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
