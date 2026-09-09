# xv6 Fork 实验报告

本实验严格遵循最少改动原则。fork 的普通页面复制逻辑保持不变，只处理 lazy allocation 导致的“页面可能尚未映射”这一情况。

## 一、代码修改

文件：`kernel/vm.c`

函数：`uvmcopy()`

原来的 `uvmcopy()` 假设进程 `sz` 范围内每一页都已经有物理映射，因此遇到不存在的页表项或无效 PTE 时会 panic。lazy allocation 中，`sbrk()` 只增加 `p->sz`，没有访问过的页面不会建立映射。

因此只将两处 panic 改为跳过当前页面：

```c
if((pte = walk(old, i, 0)) == 0)
  continue;
if((*pte & PTE_V) == 0)
  continue;
```

`kernel/proc.c` 的 `fork()` 不修改，继续调用原有的 `uvmcopy()`。

## 二、fork 之后的算法

1. `fork()` 创建子进程，并调用 `uvmcopy(parent, child, parent->sz)`。
2. `uvmcopy()` 按页遍历父进程的地址空间。
3. 如果父进程页面已经映射，就分配物理页、复制页面内容，并在子进程页表中建立相同权限的映射。
4. 如果父进程页面没有页表项或 PTE 无效，说明它是尚未访问的 lazy page，直接跳过，不分配物理页。
5. 子进程仍然设置 `np->sz = p->sz`，所以它保留这段逻辑地址范围。
6. 子进程以后访问被跳过的地址时产生 page fault，再由 `usertrap()` 调用 `lazyalloc()` 为子进程单独分配页面。

执行关系如下：

```text
fork()
  -> uvmcopy()
  -> 已映射页：复制物理页
  -> 未映射页：跳过
子进程访问未映射页
  -> page fault
  -> lazyalloc()
  -> 分配并映射子进程自己的物理页
```

这样既保留了已分配页面的内容，也保留了未访问页面的延迟分配特性。

## 三、注意事项处理

- lazy page 没有物理映射：`uvmcopy()` 不再对缺失 PTE 或无效 PTE panic。
- 子进程地址空间大小：仍执行原有的 `np->sz = p->sz`，只跳过物理页面复制。
- 页面内容隔离：已映射页面仍然通过 `kalloc()` 分配新页并用 `memmove()` 复制，不与父进程共享物理页。
- 后续访问：子进程访问被跳过的页面时重新触发 lazy allocation。
- 复制失败：原有的错误路径和 `uvmunmap()` 清理逻辑保持不变。

本实验没有修改 `fork()` 本身，也没有修改与 fork 无关的函数。已通过 `forktest`、`lazytests` 和 `usertests` 验证。
