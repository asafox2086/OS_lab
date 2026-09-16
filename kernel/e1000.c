#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //my code begin
  uint index; // 保存当前发送描述符编号
  acquire(&e1000_lock); // 保护发送环
  index = regs[E1000_TDT]; // 读取下一个发送槽位
  if((tx_ring[index].status & E1000_TXD_STAT_DD) == 0){ // 描述符仍被网卡占用
    release(&e1000_lock); // 释放发送环锁
    return -1; // 通知调用者发送失败
  }
  if(tx_mbufs[index]) // 槽位保留已完成的旧报文时
    mbuffree(tx_mbufs[index]); // 回收旧报文缓冲区
  tx_ring[index].addr = (uint64)m->head; // 设置 DMA 读取地址
  tx_ring[index].length = m->len; // 设置发送长度
  tx_ring[index].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; // 标记报文结尾并请求状态
  tx_ring[index].status = 0; // 将描述符交给网卡
  tx_mbufs[index] = m; // 保留报文直到硬件完成 DMA
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE; // 推进发送尾指针
  release(&e1000_lock); // 释放发送环锁
  return 0; // 发送描述符入队成功
  //my code end
}

static void
e1000_recv(void)
{
  //my code begin
  uint index; // 保存待处理接收描述符编号
  struct mbuf *received; // 保存网卡填充的报文
  struct mbuf *replacement; // 保存新 DMA 缓冲区
  acquire(&e1000_lock); // 保护接收环
  index = (regs[E1000_RDT] + 1) % RX_RING_SIZE; // 定位下一个未处理槽位
  while(rx_ring[index].status & E1000_RXD_STAT_DD){ // 处理所有已完成接收
    received = rx_mbufs[index]; // 取出接收报文
    mbufput(received, rx_ring[index].length); // 记录有效报文长度
    if((replacement = mbufalloc(0)) == 0) // 分配替换 DMA 缓冲区
      panic("e1000_recv"); // 内存耗尽时不能安全继续接收
    rx_mbufs[index] = replacement; // 将新缓冲区放回接收环
    rx_ring[index].addr = (uint64)replacement->head; // 更新 DMA 地址
    rx_ring[index].status = 0; // 重新开放该描述符
    regs[E1000_RDT] = index; // 通知网卡该槽位可用
    release(&e1000_lock); // 协议栈处理前释放环锁
    net_rx(received); // 向协议层递交报文
    acquire(&e1000_lock); // 继续扫描前重新加锁
    index = (regs[E1000_RDT] + 1) % RX_RING_SIZE; // 移至下一个槽位
  }
  release(&e1000_lock); // 没有报文后释放环锁
  //my code end
}

void
e1000_intr(void)
{
  e1000_recv();
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR];
}
