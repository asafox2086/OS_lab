# xv6 Lab 11：网络实验报告

## 一、改了哪里

- 新增课程提供的网络框架：`kernel/e1000.c`、`kernel/e1000_dev.h`、`kernel/net.c`、`kernel/net.h`、`kernel/pci.c`、`kernel/sysnet.c`、`user/nettests.c` 与测试服务脚本。
- `e1000_transmit()` 与 `e1000_recv()`：实现 TX/RX 环描述符的发送、回收、替换和协议层递交。
- `sockclose()`、`sockread()`、`sockwrite()`、`sockrecvudp()`：实现 socket 生命周期、阻塞读取、UDP 发送和按地址端口投递。
- `file.c`、`file.h`：新增 `FD_SOCK` 文件类型并接入读、写、关闭方法；`sys_connect()` 和系统调用表提供用户接口。
- `main.c`、`plic.c`、`trap.c`、`vm.c`、`Makefile`：初始化 PCI/E1000，映射 MMIO，处理网卡中断，构建 `nettests` 并启用 QEMU 用户网络。

## 二、改之后的算法

发送时使用 `E1000_TDT` 找到空闲描述符；描述符完成后回收旧 mbuf，写入新报文地址、长度和完成状态请求，再推进尾指针。接收中断从 `E1000_RDT + 1` 扫描所有完成描述符，将原 mbuf 交给协议栈，同时立即补入新 mbuf 并更新接收尾指针。

每个 socket 由远端 IP、本地端口和远端端口唯一匹配，并有独立接收队列和锁。UDP 报文到达时在全局表找到 socket，再在其队列锁保护下入队并唤醒读者；空队列的 `read` 睡眠等待。`write` 将用户数据复制到预留报头的 mbuf，再交给 UDP/IP/以太网层；关闭时先从全局表移除，再释放所有未读取 mbuf。

测试：`make fs.img`、`make kernel/kernel` 通过；启动 `make server` 后，xv6 `nettests` 的 one ping、single-process pings、multi-process pings 和 DNS 均为 `OK`，最终输出 `all tests passed.`。
