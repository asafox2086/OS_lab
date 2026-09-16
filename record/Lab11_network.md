# xv6 Lab 11：网络实验报告

## 一、改了哪里

- 新增课程提供的网络框架：`kernel/e1000.c`、`kernel/e1000_dev.h`、`kernel/net.c`、`kernel/net.h`、`kernel/pci.c`、`kernel/sysnet.c`、`user/nettests.c` 与测试服务脚本。
- `e1000_transmit()` 与 `e1000_recv()`：实现 TX/RX 环描述符的发送、回收、替换和协议层递交。
- `sockclose()`、`sockread()`、`sockwrite()`、`sockrecvudp()`：实现 socket 生命周期、阻塞读取、UDP 发送和按地址端口投递。
- `file.c`、`file.h`：新增 `FD_SOCK` 文件类型并接入读、写、关闭方法；`sys_connect()` 和系统调用表提供用户接口。
- `main.c`、`plic.c`、`trap.c`、`vm.c`、`Makefile`：初始化 PCI/E1000，映射 MMIO，处理网卡中断，构建 `nettests` 并启用 QEMU 用户网络。

## 二、改之后的算法

发送时使用 `E1000_TDT` 找到空闲描述符；描述符完成后回收旧 mbuf，写入新报文地址、长度和完成状态请求，再推进尾指针。接收中断从 `E1000_RDT + 1` 扫描所有完成描述符，将原 mbuf 交给协议栈，同时立即补入新 mbuf 并更新接收尾指针。

完整发送流程为：用户程序调用 `connect()`，`sys_connect()` 读取远端 IP、本地端口和远端端口，`sockalloc()` 创建 `FD_SOCK` 文件对象并将 socket 加入全局链表；随后 `write(fd, buf, n)` 经 `filewrite()` 调用 `sockwrite()`。`sockwrite()` 分配带 128 字节头部预留区的 mbuf，把用户数据 `copyin()` 到尾部；`net_tx_udp()` 依次在头部压入 UDP、IP、以太网头，最后 `e1000_transmit()` 将 DMA 地址和长度写入 TX 描述符。网卡完成 DMA 后，驱动下一次使用该槽位时释放旧 mbuf，避免发送缓冲区泄漏。

每个 socket 由远端 IP、本地端口和远端端口唯一匹配，并有独立接收队列和锁。UDP 报文到达时在全局表找到 socket，再在其队列锁保护下入队并唤醒读者；空队列的 `read` 睡眠等待。`write` 将用户数据复制到预留报头的 mbuf，再交给 UDP/IP/以太网层；关闭时先从全局表移除，再释放所有未读取 mbuf。

完整接收流程为：E1000 产生 PLIC 中断，`devintr()` 调用 `e1000_intr()`；驱动确认 DD 位后取出已填充的 mbuf，记录描述符报告的长度，先用新 mbuf 重新填充该 RX 槽位并推进 `RDT`，再把旧 mbuf 交给 `net_rx()`。协议层依次校验以太网类型、IP 版本/校验和/目的地址、UDP 长度，去掉各层头部后调用 `sockrecvudp()`。该函数在全局链表中匹配三元组，在 socket 锁保护下入队并 `wakeup(si)`；阻塞在 `sockread()` 的进程被唤醒，出队一个报文，`copyout()` 到用户缓冲区并释放 mbuf。关闭最后一个文件引用时，`fileclose()` 调用 `sockclose()`，先从全局表删除 socket，再清空接收队列，防止投递者访问已释放对象。

并发规则是：E1000 发送环和接收环由 `e1000_lock` 保护；socket 表由全局 `socktbl` 锁保护；每个接收队列由自己的 socket 锁保护。接收路径先持有表锁定位 socket、再取得对象锁后释放表锁，关闭路径先从表中删除、再清空队列，因此不会把报文交给已经关闭的 socket。

测试：`make fs.img`、`make kernel/kernel` 通过；启动 `make server` 后，xv6 `nettests` 的 one ping、single-process pings、multi-process pings 和 DNS 均为 `OK`，最终输出 `all tests passed.`。
