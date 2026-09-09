# xv6 Lab 3：Buddy Allocator 与 Lazy Allocation

本次实验主要完成两部分：

1. 使用 buddy allocator 动态分配 `struct file`，不再受 `NFILE` 固定数组限制。
2. 实现用户内存的 lazy allocation，即 `sbrk()` 只增加进程地址空间大小，真正访问页面时才分配物理页。

## 一、动态分配文件结构体

原来的 xv6 使用固定数组：

```c
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;
```

这种方式限制了系统中同时存在的文件结构体数量。

修改后只保留锁：

```c
struct {
  struct spinlock lock;
} ftable;
```

`filealloc()` 使用 buddy allocator 分配文件结构体：

```c
struct file*
filealloc(void)
{
  struct file *f;

  f = bd_malloc(sizeof(*f));
  if(f == 0)
    return 0;

  memset(f, 0, sizeof(*f));

  acquire(&ftable.lock);
  f->ref = 1;
  release(&ftable.lock);

  return f;
}
```

由于 `bd_malloc()` 不会自动清零返回的内存，所以需要使用 `memset()` 初始化。

关闭文件时，如果引用计数降为 0，完成管道或 inode 的释放后，再释放文件结构体：

```c
bd_free(f);
```

调用流程如下：

```text
open()/pipe()
    |
    v
filealloc()
    |
    v
bd_malloc(sizeof(struct file))
    |
    v
buddy allocator 分配内存
```

## 二、Buddy allocator 的位图优化

原来的 `alloc` 位图为每个 block 保存一个 bit。优化后，每一对 buddy block 只使用一个 bit：

```c
#define NPAIR(k) ((NBLK(k)+1)/2)
```

这个 bit 表示：

```text
B1_is_free XOR B2_is_free
```

状态如下：

| B1 | B2 | XOR |
|---|---|---|
| 已分配 | 已分配 | 0 |
| 空闲 | 空闲 | 0 |
| 已分配 | 空闲 | 1 |
| 空闲 | 已分配 | 1 |

为了切换 bit，新增：

```c
void
bit_flip(char *array, int index)
{
  char m = (1 << (index % 8));
  array[index/8] ^= m;
}
```

块编号通过以下函数转换为 buddy pair 编号：

```c
int
pair_index(int bi)
{
  return bi / 2;
}
```

分配或释放一个 block 时，翻转对应 pair 的 bit。

释放时：

```c
int pi = pair_index(bi);
int merge = bit_isset(bd_sizes[k].alloc, pi);
bit_flip(bd_sizes[k].alloc, pi);
```

如果释放前 `merge == 1`，说明另一个 buddy 是空闲的，因此两个 block 可以合并；如果 `merge == 0`，则不能合并。

这样可以将 `alloc` 位图的空间开销减少一半。

## 三、`sys_sbrk()` 的修改

原来的 `sbrk()` 会调用 `growproc(n)`，立即分配物理页：

```c
addr = myproc()->sz;
if(growproc(n) < 0)
  return -1;
return addr;
```

lazy allocation 中，正数参数只增加进程的 `sz`：

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();

  if(argint(0, &n) < 0)
    return -1;

  addr = p->sz;

  if(n < 0){
    if(growproc(n) < 0)
      return -1;
  } else {
    p->sz += n;
  }

  return addr;
}
```

例如：

```c
char *p = sbrk(4096);
```

执行后只会增加：

```text
p->sz += 4096
```

此时页表中还没有对应的物理页。

负数参数仍然调用 `growproc(n)`，用于释放缩小后的地址空间。

## 四、用户态 page fault 的处理

RISC-V 中常见的用户页面错误原因是：

```text
13：load page fault
15：store/AMO page fault
```

在 `usertrap()` 中，如果发现 `scause` 是 13 或 15，就调用：

```c
lazyalloc(r_stval())
```

`r_stval()` 返回发生错误的虚拟地址。

例如：

```text
stval = 0x4008
```

页大小为 4096 字节，因此实际映射的页面起始地址是：

```c
PGROUNDDOWN(0x4008) = 0x4000
```

## 五、`lazyalloc()` 的实现过程

核心函数如下：

```c
int
lazyalloc(uint64 va)
{
  char *mem;
  struct proc *p = myproc();
  uint64 a = PGROUNDDOWN(va);

  if(p == 0 || va >= p->sz || va < PGROUNDDOWN(p->tf->sp))
    return -1;

  mem = kalloc();
  if(mem == 0)
    return -1;

  memset(mem, 0, PGSIZE);

  if(mappages(p->pagetable, a, PGSIZE, (uint64)mem,
              PTE_W|PTE_X|PTE_R|PTE_U) != 0){
    kfree(mem);
    return -1;
  }

  return 0;
}
```

执行步骤：

1. 检查进程是否存在。
2. 检查错误地址是否小于 `p->sz`。
3. 检查错误地址是否位于用户栈底部以下。
4. 使用 `kalloc()` 分配一个物理页。
5. 将物理页清零。
6. 使用 `mappages()` 建立虚拟地址到物理地址的映射。
7. 返回用户态，重新执行导致 page fault 的指令。

由于当前 `kalloc()` 内部调用 buddy allocator：

```c
void*
kalloc(void)
{
  return bd_malloc(PGSIZE);
}
```

所以 lazy allocation 的实际分配路径是：

```text
lazyalloc()
    |
    v
kalloc()
    |
    v
bd_malloc(PGSIZE)
    |
    v
buddy allocator 分配一个物理页
```

## 六、`echo hi` 的执行过程

执行：

```text
$ echo hi
```

用户程序在运行过程中会访问堆空间。由于 `sbrk()` 只修改了 `p->sz`，并没有建立页表映射，因此第一次访问未映射地址时会发生 page fault：

```text
用户访问虚拟地址
        |
        v
页表中没有映射
        |
        v
RISC-V 产生 page fault
        |
        v
进入 usertrap()
        |
        v
调用 lazyalloc(r_stval())
        |
        v
kalloc() 分配物理页
        |
        v
mappages() 建立映射
        |
        v
返回用户态
        |
        v
重新执行原来的指令
```

这就是延迟分配：只有真正访问到的页面才占用物理内存。

## 七、系统调用访问 lazy page

用户程序可能将尚未实际分配的地址传给系统调用：

```c
char *p = sbrk(4096);
write(fd, p, 10);
```

内核执行 `copyin()` 或 `copyout()` 时会调用 `walkaddr()`。如果页面还没有映射，就需要分配：

```c
pte = walk(pagetable, va, 0);
if(pte == 0 || (*pte & PTE_V) == 0){
  if(lazyalloc(va) < 0)
    return 0;
  pte = walk(pagetable, va, 0);
}
```

执行流程：

```text
write()
    |
    v
copyin()
    |
    v
walkaddr()
    |
    v
发现页面未映射
    |
    v
lazyalloc()
    |
    v
重新获取物理地址
    |
    v
完成数据复制
```

## 八、`uvmunmap()` 的修改

lazy allocation 下，进程的地址空间范围内可能存在从未访问过的页面。这些页面没有实际的物理映射。

因此 `uvmunmap()` 遇到以下情况时不能 panic：

```text
页表项不存在
页表项无效
```

应该跳过这些页面，只释放实际存在的映射。

否则进程退出时可能出现：

```text
panic: uvmunmap: not mapped
```

## 九、`uvmcopy()` 的修改

`fork()` 会调用 `uvmcopy()`。父进程的 lazy page 可能尚未映射，因此不能再假设每一页都存在：

```c
if((pte = walk(old, i, 0)) == 0)
  continue;
if((*pte & PTE_V) == 0)
  continue;
```

父进程和子进程都可以暂时没有这页的物理映射。以后哪个进程访问该地址，哪个进程就通过 page fault 分配自己的物理页。

## 十、一个完整例子

用户程序执行：

```c
char *p = sbrk(8192);
p[0] = 'a';
p[4096] = 'b';
```

执行过程：

```text
1. sbrk(8192)
   p->sz 增加 8192
   不分配物理页

2. p[0] = 'a'
   第一页未映射
   触发 page fault
   lazyalloc() 分配并映射一页
   指令重新执行并成功

3. p[4096] = 'b'
   第二页未映射
   再次触发 page fault
   lazyalloc() 分配并映射一页
   指令重新执行并成功
```

最终只为真正访问过的页面分配物理内存。

## 十一、编译和测试

进入 xv6 目录：

```bash
cd /headless/xv6-riscv
```

编译：

```bash
make
```

启动 xv6：

```bash
make qemu
```

进入 xv6 shell 后运行：

```text
$ lazytests
$ usertests
```

如果 `usertests` 提示已经运行过，需要退出 QEMU，删除并重建文件系统镜像：

```bash
rm fs.img
make qemu
```

预期结果：

```text
lazytests: ALL TESTS PASSED
usertests: ALL TESTS PASSED
```
