// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 53

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket bucket[NBUCKET];
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  //my code begin
  return (dev + blockno) % NBUCKET; // 将设备号和块号分散到固定数量的哈希桶
  //my code end
}

void
binit(void)
{
  //my code begin
  struct buf *b;

  initlock(&bcache.lock, "bcache"); // 初始化仅用于串行化 buffer 回收的全局锁
  for(int i = 0; i < NBUCKET; i++){ // 初始化所有哈希桶
    initlock(&bcache.bucket[i].lock, "bcache.bucket"); // 初始化当前桶的锁
    bcache.bucket[i].head = 0; // 当前桶的 buffer 链表初始为空
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){ // 将所有初始空闲 buffer 分散到各哈希桶
    int index = (b - bcache.buf) % NBUCKET; // 计算当前 buffer 的初始桶编号
    b->dev = (uint)-1; // 用非法设备号标记尚未缓存任何磁盘块
    b->next = bcache.bucket[index].head; // 将 buffer 插入当前桶链表头部
    bcache.bucket[index].head = b; // 更新当前桶链表头指针
    initsleeplock(&b->lock, "buffer"); // 初始化每个 buffer 的睡眠锁
  }
  //my code end
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //my code begin
  struct buf *b; // 保存命中或新分配的块缓存
  uint index = bhash(dev, blockno); // 计算目标磁盘块所属哈希桶

  acquire(&bcache.bucket[index].lock); // 锁住目标桶以查找已有缓存
  for(b = bcache.bucket[index].head; b; b = b->next){ // 遍历目标桶中的 buffer
    if(b->dev == dev && b->blockno == blockno){ // 找到同一设备的同一磁盘块
      b->refcnt++; // 增加该缓存块的使用计数
      release(&bcache.bucket[index].lock); // 释放目标桶锁
      acquiresleep(&b->lock); // 等待并锁住该 buffer 的数据
      return b; // 返回已缓存的 buffer
    }
  }
  release(&bcache.bucket[index].lock); // 初次查找未命中后释放目标桶锁

  acquire(&bcache.lock); // 串行化未命中时的 buffer 回收过程
  acquire(&bcache.bucket[index].lock); // 再次锁住目标桶，防止并发未命中创建重复缓存
  for(b = bcache.bucket[index].head; b; b = b->next){ // 重新检查目标块是否已被其他 CPU 缓存
    if(b->dev == dev && b->blockno == blockno){ // 其他 CPU 已在等待期间创建该缓存
      b->refcnt++; // 增加该缓存块的使用计数
      release(&bcache.bucket[index].lock); // 释放目标桶锁
      release(&bcache.lock); // 释放回收锁
      acquiresleep(&b->lock); // 等待并锁住该 buffer 的数据
      return b; // 返回已有的唯一缓存副本
    }
  }

  for(int i = 0; i < NBUCKET; i++){ // 搜索所有桶以寻找可重用的空闲 buffer
    if(i != index) // 目标桶锁已经持有，无需重复获取
      acquire(&bcache.bucket[i].lock); // 锁住当前候选桶
    struct buf **link = &bcache.bucket[i].head; // 保存当前链表节点的前驱链接地址
    for(; *link; link = &(*link)->next){ // 遍历当前桶中的所有 buffer
      if((*link)->refcnt == 0){ // 找到没有使用者的 buffer
        b = *link; // 保存将被重新利用的 buffer
        *link = b->next; // 从旧桶链表中移除该 buffer
        b->dev = dev; // 记录新缓存块的设备号
        b->blockno = blockno; // 记录新缓存块的块号
        b->valid = 0; // 新块尚未从磁盘读入数据
        b->refcnt = 1; // 当前调用者持有第一个引用
        b->next = bcache.bucket[index].head; // 将 buffer 插入目标桶链表头部
        bcache.bucket[index].head = b; // 更新目标桶的链表头指针
        if(i != index) // 当前候选桶不是目标桶时
          release(&bcache.bucket[i].lock); // 释放旧桶锁
        release(&bcache.bucket[index].lock); // 释放目标桶锁
        release(&bcache.lock); // 释放回收锁
        acquiresleep(&b->lock); // 锁住新分配 buffer 的数据
        return b; // 返回新映射的 buffer
      }
    }
    if(i != index) // 非目标桶的搜索完成后
      release(&bcache.bucket[i].lock); // 释放当前候选桶锁
  }
  release(&bcache.bucket[index].lock); // 没有可用 buffer 时释放目标桶锁
  release(&bcache.lock); // 没有可用 buffer 时释放回收锁
  panic("bget: no buffers"); // 所有 buffer 均被占用时终止内核
  //my code end
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  //my code begin
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint index = bhash(b->dev, b->blockno); // 计算该缓存块所在的哈希桶
  acquire(&bcache.bucket[index].lock); // 锁住所属桶以修改引用计数
  b->refcnt--; // 释放当前调用者对该缓存块的引用
  release(&bcache.bucket[index].lock); // 释放所属桶锁
  //my code end
}

void
bpin(struct buf *b) {
  //my code begin
  uint index = bhash(b->dev, b->blockno); // 计算该缓存块所在的哈希桶
  acquire(&bcache.bucket[index].lock); // 锁住所属桶以增加引用计数
  b->refcnt++; // 为日志固定该缓存块
  release(&bcache.bucket[index].lock); // 释放所属桶锁
  //my code end
}

void
bunpin(struct buf *b) {
  //my code begin
  uint index = bhash(b->dev, b->blockno); // 计算该缓存块所在的哈希桶
  acquire(&bcache.bucket[index].lock); // 锁住所属桶以减少引用计数
  b->refcnt--; // 取消日志对该缓存块的固定
  release(&bcache.bucket[index].lock); // 释放所属桶锁
  //my code end
}
