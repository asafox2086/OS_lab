//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  //my code begin
  struct file *f; // 保存从伙伴分配器取得的文件对象

  f = bd_malloc(sizeof(*f)); // 从伙伴分配器申请文件对象内存
  if(f == 0) // 内存不足时不能创建文件对象
    return 0; // 返回失败

  memset(f, 0, sizeof(*f)); // 清零新文件对象的所有字段

  acquire(&ftable.lock); // 保护文件引用计数
  f->ref = 1; // 新对象由调用者持有一个引用
  release(&ftable.lock); // 释放文件表锁

  return f; // 返回初始化后的文件对象
  //my code end
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  //my code begin
  int type; // 保存关闭前的文件类型
  int writable; // 保存关闭前的可写标志
  int dev; // 保存 inode 所在设备号
  struct pipe *pipe; // 保存管道对象指针
  struct inode *ip; // 保存 inode 指针
  struct sock *sock; // 保存 socket 对象指针

  acquire(&ftable.lock); // 保护文件对象引用计数
  if(f->ref < 1) // 引用计数非法说明重复关闭
    panic("fileclose"); // 终止内核
  if(--f->ref > 0){ // 仍有其他引用时无需销毁对象
    release(&ftable.lock); // 释放文件表锁
    return; // 结束关闭操作
  }
  type = f->type; // 保存文件类型供解锁后使用
  writable = f->writable; // 保存可写标志供解锁后使用
  pipe = f->pipe; // 保存管道指针供解锁后使用
  ip = f->ip; // 保存 inode 指针供解锁后使用
  sock = f->sock; // 保存 socket 指针供解锁后使用
  if(type == FD_INODE || type == FD_DEVICE) // inode 和设备文件都需要设备号
    dev = ip->dev; // 记录设备号
  f->ref = 0; // 标记对象不再被引用
  f->type = FD_NONE; // 清除文件类型
  release(&ftable.lock); // 解锁后执行可能阻塞的资源释放

  if(type == FD_PIPE){ // 管道文件释放管道端点
    pipeclose(pipe, writable); // 关闭相应的读或写端
  } else if(type == FD_SOCK){ // socket 文件需要释放通信状态
    sockclose(sock); // 从 socket 表移除并回收报文队列
  } else if(type == FD_INODE || type == FD_DEVICE){ // inode 或设备文件释放 inode 引用
    begin_op(dev); // 开始文件系统日志事务
    iput(ip); // 释放 inode 引用
    end_op(dev); // 结束文件系统日志事务
  }

  bd_free(f); // 将文件对象内存归还伙伴分配器
  //my code end
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_SOCK){
    r = sockread(f->sock, addr, n); // 从 socket 接收队列读取 UDP 负载
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(f, 1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_SOCK){
    ret = sockwrite(f->sock, addr, n); // 通过 socket 封装并发送 UDP 负载
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(f, 1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op(f->ip->dev);
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op(f->ip->dev);

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
