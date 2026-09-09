# xv6 Lab 6：文件系统实验报告

本实验遵循最少改动原则，只修改大文件和符号链接所需的文件。`bigfile.c` 与 `symlinktest.c` 仅从历史提交临时恢复用于测试，测试结束后已删除，没有加入最终提交。

## 一、修改位置

### `kernel/fs.h`

- 修改宏 `NDIRECT`：由 12 改为 11，为双重间接块留出一个地址项。
- 修改宏 `MAXFILE`：增加 `NINDIRECT * NINDIRECT`。
- 修改 `struct dinode` 的 `addrs[]` 为 `NDIRECT+2`，保持磁盘 inode 总大小不变。

### `kernel/file.h`

- 修改 `struct inode` 的 `addrs[]` 为 `NDIRECT+2`，与 `struct dinode` 保持一致。

### `kernel/fs.c`

- 修改函数 `bmap()`：增加双重间接块的分配和两级索引查找。
- 修改函数 `itrunc()`：释放双重间接块、所有下一级间接块及其数据块。

### `kernel/param.h`

- 将 `FSSIZE` 改为 70000。实验要求 `bigfile` 写入 65803 个数据块，原来的 2000 个磁盘块无法容纳该文件。

### `kernel/stat.h`

- 增加文件类型 `T_SYMLINK`。

### `kernel/fcntl.h`

- 增加 `O_NOFOLLOW`，用于打开符号链接本身而不是跟随它。

### `kernel/syscall.h`

- 增加系统调用号 `SYS_symlink`。

### `kernel/syscall.c`

- 声明并注册 `sys_symlink()`。

### `kernel/sysfile.c`

- 修改函数 `sys_open()`：普通打开操作最多跟随 10 层符号链接；使用 `O_NOFOLLOW` 时保留符号链接 inode；目标不存在或形成循环时返回失败。
- 新增函数 `sys_symlink()`：创建 `T_SYMLINK` inode，并把目标路径写入其数据块。
- `sys_open()` 保持 inode 引用和 inode 锁的原有流程，符号链接解析期间先完成当前 inode 的解锁和引用释放，再查找下一个 inode，避免与 `unlink()` 并发访问失效 inode。

### `user/user.h`

- 增加用户态函数声明 `symlink()`。

### `user/usys.pl`

- 增加 `symlink` 系统调用汇编入口生成项。

## 二、修改后的算法

### 1. 双重间接块

inode 的地址项布局为：

```text
addrs[0..10]  -> 11 个直接数据块
addrs[11]     -> 单重间接块 -> 256 个数据块
addrs[12]     -> 双重间接块 -> 256 个单重间接块 -> 各 256 个数据块
```

`bmap(ip, bn)` 先处理 11 个直接块，再减去 `NDIRECT` 处理单重间接块。超过单重间接范围后再次减去 `NINDIRECT`，使用：

```text
第一级索引 = bn / NINDIRECT
第二级索引 = bn % NINDIRECT
```

只有访问到相应范围时才分配双重间接块、下一级间接块和数据块。最大逻辑块数为：

```text
11 + 256 + 256 * 256 = 65803
```

`itrunc()` 依次释放直接块、单重间接块及其数据块，然后遍历双重间接块，释放每个下一级间接块中的数据块，最后释放两级间接块本身。

### 2. 符号链接

`symlink(target, path)` 只创建链接文件，不要求 `target` 当前存在。目标路径作为普通文件内容写入符号链接 inode，因此 `link` 和 `unlink` 仍然只处理链接 inode 本身。

普通 `open(path, omode)` 的执行流程为：

```text
namei(path)
  -> 加锁当前 inode
  -> 如果是符号链接，读取目标路径
  -> 释放当前 inode
  -> namei(目标路径)
  -> 重复，最多 10 层
  -> 对最终的普通文件、设备或目录执行原有 open 流程
```

当使用 `O_NOFOLLOW` 时，`open()` 停留在符号链接 inode 上，因而可以通过 `fstat()` 判断其类型。链接目标不存在、链接层数超过限制或形成环时，系统调用返回 `-1`。

### 3. 并发处理

`namei()` 返回 inode 后，`sys_open()` 立即取得 inode 锁并持有 inode 引用。解析下一层链接前，先解锁并释放当前引用，再获取下一 inode。这样 `unlink()` 即使同时删除目录项，也不能在 `sys_open()` 仍持有引用时回收当前 inode，避免访问已经失效的 inode。

## 三、验证结果

- `make clean && make`：通过。
- `bigfile`：写出并读回 `65803` 个块，输出 `bigfile done; ok`。
- `symlinktest`：基础符号链接测试通过。
- `symlinktest`：并发符号链接测试通过。
- 原有 `usertests` 已运行到后续长时间文件测试；之前基线中的 `sbrkfail` 仍会失败，本实验没有扩大范围修改该既有 lazy allocation 问题。
