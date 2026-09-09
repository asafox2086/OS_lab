# xv6 Lab 7：mmap/munmap 实验报告

本实验遵循最少改动原则，只增加 mmap/munmap 所需的数据结构、系统调用接线、缺页处理和映射生命周期管理。官方 `mmaptest` 保留在用户程序列表中，作为评分测试入口。

## 一、改了哪里

### `kernel/proc.h`

新增 `struct vma`，记录映射地址、长度、文件偏移、权限、映射类型和文件引用；在 `struct proc` 中增加固定大小为 16 的 VMA 数组。

### `kernel/fcntl.h`

增加 `PROT_READ`、`PROT_WRITE`、`MAP_SHARED` 和 `MAP_PRIVATE` 定义。

### `kernel/syscall.h`、`kernel/syscall.c`

增加 `SYS_mmap`、`SYS_munmap`，并注册 `sys_mmap()`、`sys_munmap()`。

### `user/user.h`、`user/usys.pl`

增加用户态 mmap/munmap 声明和汇编入口生成项。

### `kernel/sysproc.c`

新增函数：`sys_mmap()`、`sys_munmap()`。

- `sys_mmap()` 只接受实验要求的 `addr == 0`、`offset == 0`、支持的权限和映射类型。
- 检查文件类型及 `MAP_SHARED | PROT_WRITE` 的可写权限。
- 在堆和 `TRAPFRAME` 之间从高地址向下选择空闲区域，只登记 VMA，不分配物理页、不读文件。
- 通过 `filedup()` 保留映射所需的文件引用。
- `sys_munmap()` 检查页对齐，并交给 `mmapunmap()` 处理映射开头、结尾或全部区域。

### `kernel/vm.c`

新增函数：`mmapalloc()`、`mmapunmap_page()`、`mmapunmap()`、`mmapexit()`。

- `mmapalloc()` 根据缺页地址查找 VMA，检查读写权限，分配并清零物理页，从文件读取对应页内容，再建立用户页映射。
- `mmapunmap_page()` 释放已实际分配的页面；`MAP_SHARED` 页面在释放前写回文件，文件末尾之外的区域保持零值。
- `mmapunmap()` 更新 VMA 的地址、长度和文件偏移，并在全部解除时关闭文件引用。
- `mmapexit()` 清理进程剩余的所有映射。

### `kernel/trap.c`

修改函数：`usertrap()`。在原有 COW 和 lazy allocation 之前优先处理 mmap 的 load/store page fault，避免映射地址被错误地当作普通堆地址分配。

### `kernel/proc.c`

修改函数：`freeproc()`、`fork()`、`exit()`。

- `freeproc()` 和 `exit()` 清理 VMA，防止物理页、文件引用和共享数据泄漏。
- `fork()` 复制 VMA 描述，并对每个映射文件调用 `filedup()`；子进程的 mmap 页面在缺页时重新从文件加载，不额外共享物理页。

### `kernel/exec.c`

修改函数：`exec()`。提交新用户地址空间前调用 `mmapexit()`，释放旧程序遗留的映射、物理页和文件引用。

### `kernel/defs.h`

增加上述 mmap 内核辅助函数的声明。

### `user/ulib.c`

新增函数：`memcmp()`，用于官方 `mmaptest` 比较两个映射文件的内容。

## 二、改后的算法和注意事项

### 1. mmap 的懒加载算法

```text
mmap()
  -> 检查参数和文件权限
  -> 从高地址向下找到空闲范围
  -> 记录 VMA
  -> 不分配物理页，不读取文件

首次访问映射页
  -> 产生 load/store page fault
  -> usertrap() 调用 mmapalloc()
  -> kalloc() 分配并清零一页
  -> 从文件读取对应内容
  -> 建立带正确权限的 PTE
  -> 返回用户态重新执行原指令
```

映射长度向上按页对齐。文件小于映射长度时，分配页先全部清零，再只读取文件中存在的部分，因此文件末尾之后访问得到零。

### 2. munmap、exit 和 fork 算法

`munmap()` 只接受整个 VMA、开头或结尾区域，不处理 VMA 中间洞。已经加载的页被逐页释放，未加载的页直接跳过。`MAP_SHARED` 在释放前把页面内容写回 inode，`MAP_PRIVATE` 不写回。全部解除后调用 `fileclose()`。

`exit()` 通过 `mmapexit()` 自动解除全部映射。`fork()` 复制 VMA 描述和文件引用，子进程不复制映射物理页，而是在自己的缺页处理中重新读取文件。

### 3. 实验要求中的注意事项

- `mmap()` 不提前分配物理内存，支持大于物理内存的文件映射。
- 页错误先判断 VMA，再进入已有的 COW/lazy allocation 路径。
- 只允许 `PROT_READ`、`PROT_WRITE` 及 `MAP_SHARED`、`MAP_PRIVATE`。
- 只允许 `addr` 和 `offset` 为 0，符合本实验限定范围。
- `MAP_SHARED | PROT_WRITE` 要求文件以可写方式打开；`MAP_PRIVATE` 可映射只读文件。
- VMA 使用固定 16 项数组，符合实验要求且不引入额外动态结构。

## 测试结果

- `mmaptest`：`mmap_test OK`、`fork_test OK`、`mmaptest: all tests succeeded`。
- `make kernel/kernel` 和包含 `mmaptest` 的 `make fs.img` 均成功。
- 官方 `mmaptest` 已加入 `Makefile` 的 `UPROGS`，会随 `fs.img` 一起构建并作为评分入口。
- `usertests` 的 `sbrkfail` 在当前 lazy/COW 基线上也会因资源压力失败，因此不能标记为整体通过；mmap 专项测试全部通过。
