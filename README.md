<div align="center">

```text
╭──────────────────────────────────────────╮
│             O S   ·   L A B              │
│        xv6  /  RISC-V  /  Kernel         │
╰──────────────────────────────────────────╯
```

# OS Lab · 操作系统实践

基于 **xv6-riscv**，从代码里理解操作系统，而不只停留在概念里。

`进程` · `虚拟内存` · `文件系统` · `系统调用` · `并发与锁`

</div>

---

## 🌱 这个仓库是什么

这是一个用于操作系统课程实践与源码学习的仓库，基础代码来自 MIT 的教学操作系统 [xv6](https://pdos.csail.mit.edu/6.828/)。

实践过程以阅读 xv6、定位关键调用链、实现实验功能和验证边界情况为主。相比单纯给出最终代码，这里更关注：一个问题为什么这样设计、应该从哪里切入，以及实现时容易踩到哪些坑。

## 🧩 实践内容

| 主题 | 主要内容 | 解题思路 |
| --- | --- | --- |
| 内存管理 | Buddy 分配器、懒分配等 | [Lab 3](record/Lab3.md) |
| 进程与虚拟内存 | Copy-on-Write Fork | [Lab 5](record/Lab5_fork.md) |
| 文件系统 | 大文件、符号链接 | [Lab 6](record/Lab6_filesystem.md) |
| 内存映射 | `mmap` / `munmap`、缺页处理 | [Lab 7](record/Lab7_mmap.md) |
| 并发控制 | 页分配与 Buffer Cache 锁竞争优化 | [Lab 8](record/Lab8_lock.md) |
| Shell | 命令解析、重定向与多级管道 | [Lab 9](record/Lab9_shell.md) |
| 中断与上下文切换 | 用户态线程、定时闹钟 | [Lab 10](record/Lab10_clock.md) |
| 网络 | E1000 驱动、UDP 与 Socket | [Lab 11](record/Lab11_network.md) |

> `record/` 是解题思路整理。内容主要记录问题拆解、关键数据结构、实现路径、易错点和验证方法，方便之后快速复盘。

## 🗺️ 仓库地图

```text
OS_lab/
├── kernel/       # xv6 内核：进程、内存、文件系统、锁等
├── user/         # 用户程序与测试程序
├── record/       # 各实验的解题思路与实现复盘
├── doc/          # RISC-V 等参考资料
├── mkfs/         # 文件系统镜像构建工具
├── conf/         # 实验与测试配置
└── Makefile      # 编译、运行与调试入口
```

## 🚀 运行 xv6

准备好 RISC-V GNU 工具链、QEMU（`qemu-system-riscv64`）和 `make` 后，在仓库根目录运行：

```bash
make qemu
```

退出 QEMU：先按 `Ctrl + A`，再按 `X`。

需要调试时，可以分别启动 QEMU 的 GDB 模式和调试器：

```bash
make qemu-gdb
gdb-multiarch
```

## 🔍 阅读建议

```text
实验要求
   ↓
找到系统调用或异常入口
   ↓
顺着调用链定位核心数据结构
   ↓
先保证正确性，再处理回收、并发与边界情况
   ↓
用测试结果反查遗漏路径
```

建议把 `record/` 中的思路和对应源码一起看。记录负责回答“为什么这样改”，代码负责展示“具体怎样实现”。

## 📚 关于 xv6

xv6 是 MIT 面向操作系统课程开发的教学内核，以 Unix Version 6 的结构和思想为基础，并运行在现代 RISC-V 平台上。它规模不大，但覆盖了进程调度、页表、陷阱与系统调用、文件系统、设备驱动和并发控制等核心机制。

原始 xv6 项目及课程资料请参考：

- [MIT 6.S081 / 6.828](https://pdos.csail.mit.edu/6.828/)
- [xv6-riscv](https://github.com/mit-pdos/xv6-riscv)

本仓库保留 xv6 原项目的版权与许可信息，详见 [LICENSE](LICENSE)。
