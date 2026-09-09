# xv6 Lab 5：COW Fork 实验报告

本实验严格按照 `doc/Lab4/clipboard.txt` 实现 fork 的写时拷贝（Copy-on-Write，COW）。代码遵循最少改动原则，只修改 COW 必需的函数。

## 一、代码修改位置

### `kernel/riscv.h`

新增 `PTE_COW`，使用 RISC-V PTE 的 RSW 位标记 COW 页面。

### `kernel/kalloc.c`

修改 `kinit()`、`kalloc()`、`kfree()`；新增 `krefinc()`、`krefcnt()` 和内部函数 `refindex()`。

- `refcnt[]` 为每个物理页保存引用计数。
- `kalloc()` 分配页面后将引用计数设为 1。
- `krefinc()` 在 fork 共享页面时增加引用计数。
- `kfree()` 将引用计数减 1，只有减到 0 时才真正调用 `bd_free()`。
- `reflock` 保护引用计数，避免多核并发修改。

### `kernel/vm.c`

修改 `uvmcopy()`、`copyout()`；新增 `cowalloc()`。

- `uvmcopy()` 不再为每个父页面分配和复制新物理页，而是把父页面的物理地址映射到子进程页表。
- 如果页面可写，则同时清除父子 PTE 的 `PTE_W` 并设置 `PTE_COW`。
- 共享映射后调用 `krefinc()`。
- `cowalloc()` 处理写 COW 页：引用数为 1 时直接恢复写权限；引用数大于 1 时分配新页、复制内容、建立可写映射并释放旧引用。
- `copyout()` 在内核向用户 COW 页面写数据前调用 `cowalloc()`，处理系统调用触发的写时拷贝。

### `kernel/trap.c`

修改 `usertrap()`。

- 当 `r_scause() == 15` 时，先调用 `cowalloc()` 处理 COW 写页错误。
- 如果不是合法 COW 页面，再按原有 lazy allocation 路径处理；处理失败则保留原有的杀死进程逻辑。

### `kernel/defs.h`

新增 `krefinc()`、`krefcnt()` 和 `cowalloc()` 的函数声明。

### 未修改的文件

`kernel/proc.c` 中的 `fork()` 保持不变，因为它原本就通过 `uvmcopy()` 建立子进程地址空间；COW 逻辑只需放在 `uvmcopy()` 中即可。

## 二、修改后的 COW 算法

### 1. fork 阶段

父进程调用 `fork()` 后，`uvmcopy()` 逐页检查父进程页表：

```text
已映射页面
  -> 父子 PTE 指向同一物理页
  -> 清除 PTE_W
  -> 设置 PTE_COW
  -> 物理页引用计数加 1

未映射的 lazy 页面
  -> 继续跳过
```

fork 阶段不复制物理页，因此可以在内存不足时成功创建子进程。

### 2. 用户写入阶段

父进程或子进程写入 COW 页面时，PTE 没有写权限，CPU 产生 store page fault（`scause == 15`）：

```text
用户写入 COW 页面
  -> page fault
  -> usertrap()
  -> cowalloc()
```

`cowalloc()` 的分支如下：

- 引用计数为 1：没有其他进程共享，直接清除 `PTE_COW` 并恢复 `PTE_W`。
- 引用计数大于 1：申请新物理页，复制旧页内容，将当前进程 PTE 改为新页并设置可写，然后对旧页执行一次 `kfree()`。
- `kalloc()` 失败或地址不是合法 COW 页面：返回失败，`usertrap()` 杀死当前进程。

### 3. 释放阶段

进程退出或缩小地址空间时，`uvmunmap()` 对映射页面调用 `kfree()`：

```text
kfree()
  -> 引用计数减 1
  -> 计数仍大于 0：保留物理页
  -> 计数变为 0：调用 bd_free() 真正释放
```

因此父子进程共享页面时不会提前释放，最后一个引用消失后页面才回收到 buddy allocator。

### 4. 系统调用写入

当 `copyout()` 向用户地址写入时，即使不会经过用户态指令，也必须检查 COW：

```text
系统调用 copyout()
  -> 发现目标 PTE 是 PTE_COW
  -> cowalloc()
  -> 再执行 walkaddr() 和 memmove()
```

这样 `read()` 等系统调用写入子进程内存时，不会修改父进程的共享页面。

## 三、实验要求对应处理

- fork 时不分配新物理页：`uvmcopy()` 共享物理地址并增加引用计数。
- 父子页面不可写：共享页面的父子 PTE 都清除 `PTE_W`，并用 `PTE_COW` 标记。
- 写 COW 页面时复制：`usertrap()` 和 `copyout()` 都调用 `cowalloc()`。
- 多进程共享释放：引用计数保证只有最后一个进程释放时才调用 `bd_free()`。
- lazy 页面兼容：`uvmcopy()` 遇到未映射页面继续跳过，保留之前实验的 lazy allocation 行为。
- 内存耗尽：COW 分配新页失败时返回错误，`usertrap()` 将当前进程标记并结束。
- 原有代码复用：`fork()`、`uvmunmap()` 和 buddy allocator 的整体调用流程保持不变。

验证结果：`make` 编译通过，`cowtest` 输出 `ALL COW TESTS PASSED`。
