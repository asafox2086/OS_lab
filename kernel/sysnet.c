//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

void
sockclose(struct sock *si)
{
  //my code begin
  struct sock **link; // 保存 socket 链表链接
  struct mbuf *m; // 保存待释放报文
  acquire(&lock); // 保护活跃 socket 链表
  for(link = &sockets; *link && *link != si; link = &(*link)->next) // 查找目标 socket
    ; // 只移动链表链接
  if(*link == si) // socket 仍在链表中时
    *link = si->next; // 先从投递表移除
  release(&lock); // 释放全局 socket 锁
  acquire(&si->lock); // 保护接收队列
  while((m = mbufq_pophead(&si->rxq)) != 0) // 释放所有未读取报文
    mbuffree(m); // 回收队列中的 mbuf
  release(&si->lock); // 释放队列锁
  kfree(si); // 释放 socket 对象
  //my code end
}

int
sockread(struct sock *si, uint64 addr, int n)
{
  //my code begin
  struct mbuf *m; // 保存出队报文
  int length; // 保存实际读取长度
  if(n < 0) // 拒绝负长度读取
    return -1; // 返回错误
  acquire(&si->lock); // 保护接收队列
  while(mbufq_empty(&si->rxq)){ // 队列为空时阻塞
    if(myproc()->killed){ // 进程被终止时停止等待
      release(&si->lock); // 释放队列锁
      return -1; // 返回错误
    }
    sleep(si, &si->lock); // 等待 UDP 投递唤醒
  }
  m = mbufq_pophead(&si->rxq); // 取出最早报文
  release(&si->lock); // 完成出队后释放锁
  length = n < m->len ? n : m->len; // 截断到用户缓冲区长度
  if(copyout(myproc()->pagetable, addr, m->head, length) < 0) // 复制负载到用户空间
    length = -1; // 用户地址无效时报告错误
  mbuffree(m); // 释放已消费报文
  return length; // 返回读取长度
  //my code end
}

int
sockwrite(struct sock *si, uint64 addr, int n)
{
  //my code begin
  struct mbuf *m; // 保存待发送负载
  if(n < 0 || n > MBUF_SIZE - MBUF_DEFAULT_HEADROOM) // 保留各层协议头空间
    return -1; // 长度非法
  if((m = mbufalloc(MBUF_DEFAULT_HEADROOM)) == 0) // 分配发送 mbuf
    return -1; // 内存不足
  if(copyin(myproc()->pagetable, mbufput(m, n), addr, n) < 0){ // 拷贝用户数据
    mbuffree(m); // 失败时回收 mbuf
    return -1; // 返回错误
  }
  net_tx_udp(m, si->raddr, si->lport, si->rport); // 封装并发送 UDP 报文
  return n; // 返回已写字节数
  //my code end
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //my code begin
  struct sock *si; // 保存匹配的 socket
  acquire(&lock); // 保护 socket 链表
  for(si = sockets; si; si = si->next) // 搜索 UDP 四元组
    if(si->raddr == raddr && si->lport == lport && si->rport == rport) // 检查地址和端口
      break; // 找到唯一接收者
  if(si == 0){ // 没有匹配 socket 时
    release(&lock); // 释放全局锁
    mbuffree(m); // 丢弃无人接收的报文
    return; // 结束处理
  }
  acquire(&si->lock); // 在对象仍受链表锁保护时锁住队列
  release(&lock); // 已安全持有对象锁
  mbufq_pushtail(&si->rxq, m); // 将报文加入接收队列
  wakeup(si); // 唤醒阻塞的读者
  release(&si->lock); // 释放队列锁
  //my code end
}
