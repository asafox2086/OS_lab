# xv6 Lab 3 实验记录

本次实现遵循最少改动原则：只修改实验要求涉及的函数，保留 xv6 原有的调用流程和数据结构，不增加无关功能。

## 一、改了哪里

### `kernel/file.c`

修改函数：`filealloc()`、`fileclose()`。

- 删除 `ftable.file[NFILE]`，文件结构体不再受 `NFILE` 限制。
- `filealloc()` 调用 `bd_malloc(sizeof(*f))`，并用 `memset()` 清零。因为 `bd_malloc()` 返回的内存不会自动清零。
- `fileclose()` 在引用计数降为 0 后调用 `bd_free(f)`。释放前先保存关闭文件所需的字段，并继续使用 `ftable.lock` 保护引用计数。

### `kernel/buddy.c`

修改函数：`bd_malloc()`、`bd_free()`、`bd_mark()`、`bd_initfree_pair()`、`bd_initfree()`、`bd_init()`、`bd_print()`；新增 `bit_flip()`、`pair_index()`。

- `alloc` 位图从“每个块一个 bit”改为“每对 buddy 一个 bit”。该 bit 表示两个块是否只有一个空闲，即 `B1_is_free XOR B2_is_free`。
- 分配和释放块时翻转对应 pair 的 bit；释放时若翻转前该 bit 为 1，则两个 buddy 合并。
- metadata 和不可用内存仍标记为已占用，初始化时跳过越界的 buddy pair，保证非 2 的幂大小的物理内存也能工作。
- 位图大小和 `bd_print()` 的长度改为按 pair 数量计算。

### `kernel/kalloc.c`

修改函数：`kinit()`、`kalloc()`、`kfree()`。

- `kinit()` 使用 `bd_init()` 初始化物理内存。
- `kalloc()` 和 `kfree()` 分别转调 `bd_malloc(PGSIZE)` 与 `bd_free()`，让页表页、用户页、内核栈和文件结构体使用同一个 buddy allocator。

### `kernel/sysproc.c`

修改函数：`sys_sbrk()`。

- `sbrk(n)` 为正数时只增加 `p->sz` 并返回旧地址，不立即分配物理页。
- `sbrk(n)` 为负数时保留原来的 `growproc(n)` 路径，以释放缩小范围内已经存在的页面。

### `kernel/trap.c`

修改函数：`usertrap()`。

- 判断 `r_scause()` 是否为 13（load page fault）或 15（store/AMO page fault）。
- 对这两类异常调用 `lazyalloc(r_stval())`；地址非法、分配失败或映射失败时保持原有的杀死进程路径。

### `kernel/vm.c`

修改函数：`walkaddr()`、`uvmunmap()`、`uvmcopy()`；新增 `lazyalloc()`。

- `lazyalloc()` 检查错误地址是否低于 `p->sz` 且不在用户栈底部以下，然后调用 `kalloc()`、清零页面并用 `mappages()` 建立用户页映射。
- `walkaddr()` 在系统调用访问尚未映射的合法 lazy page 时触发 `lazyalloc()`，支持 `read()`、`write()` 等内核到用户地址的复制。
- `uvmunmap()` 跳过不存在或无效的 PTE，避免释放进程时因 lazy page 从未映射而 panic。
- `uvmcopy()` 在 `fork()` 时跳过父进程尚未映射的页面；子进程以后访问该地址时再独立分配页面。

### `kernel/defs.h`

修改内容：增加 `lazyalloc()` 的函数声明，使 `trap.c` 和 `vm.c` 可以调用它。

## 二、改之后的算法和注意事项

### 1. Buddy allocator 算法

初始化时，`bd_init()` 从物理内存中划出 metadata、`alloc` 位图和 `split` 位图，并将这些区域及物理内存范围之外的部分标记为已占用。剩余区域按大小加入 free list。

分配时，`bd_malloc()` 找到能够容纳请求的最小 block；如果只有更大的 block，就不断二分，把另一半放入更小一级的 free list，并翻转对应 buddy pair 的 XOR bit。释放时，`bd_free()` 翻转 XOR bit；如果另一个 buddy 空闲，就从 free list 移除它并向上合并，否则将当前 block 放回 free list。锁保证并发分配和释放不会同时修改 allocator 状态。

### 2. Lazy allocation 算法

`sbrk(8192)` 只把进程大小从 `oldsz` 改为 `oldsz + 8192`，不分配物理页。用户第一次访问其中某一页时，硬件产生 page fault，`usertrap()` 取得 `r_stval()`，调用 `lazyalloc()`。该函数将地址用 `PGROUNDDOWN()` 对齐，检查堆边界和栈边界，使用 `kalloc()` 分配并清零一个物理页，再用 `mappages()` 建立映射；返回用户态后，CPU 重新执行原指令。

因此完整路径是：

```text
sbrk()
  -> 只增加 p->sz
用户访问未映射页
  -> page fault
  -> usertrap()
  -> lazyalloc()
  -> kalloc()
  -> buddy allocator
  -> mappages()
  -> 重新执行原指令
```

### 3. 实验文档注意事项的处理

- `bd_malloc()` 不清零：`filealloc()` 显式调用 `memset()`；`lazyalloc()` 也将新物理页清零。
- `sbrk()` 负数：仍调用 `growproc()`，由 `uvmdealloc()` 和修改后的 `uvmunmap()` 释放已有映射，并跳过从未分配的页面。
- page fault 地址超过 `sbrk()` 范围：`lazyalloc()` 检查 `va >= p->sz`，失败后进程沿原异常路径被杀死。
- 栈底以下的非法地址：`lazyalloc()` 检查 `va < PGROUNDDOWN(p->tf->sp)`，拒绝访问栈底以下地址。
- `fork()`：`uvmcopy()` 对未映射页面直接跳过，避免把 lazy page 当成已存在的物理页复制。
- 系统调用访问 lazy page：`walkaddr()` 发现合法地址没有映射时调用 `lazyalloc()`，因此 `copyin()`、`copyout()` 可以继续工作。
- 内存耗尽：`lazyalloc()` 在 `kalloc()` 失败时返回错误，`usertrap()` 不接受该异常并杀死当前进程。
- `uvmunmap()` 崩溃：对不存在的页表项和无效 PTE 直接跳过，只释放真实存在的叶子映射。

最终，buddy allocator 负责底层物理内存分配，lazy allocation 负责推迟用户页的实际分配；只有真正访问的用户页才占用物理内存。已用 `lazytests`、`usertests` 验证该流程。
