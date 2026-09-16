#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  //my code begin
  int addr; // 保存调用前的进程内存大小
  int n; // 保存用户请求扩展或收缩的字节数
  struct proc *p = myproc(); // 获取当前进程

  if(argint(0, &n) < 0) // 读取 sbrk 的增量参数
    return -1; // 参数读取失败
  addr = p->sz; // 保存旧堆顶作为返回值
  if(n < 0){ // 收缩内存时立即取消映射
    if(growproc(n) < 0) // 执行实际的内存回收
      return -1; // 回收失败
  } else {
    p->sz += n; // 扩展时只记录大小，页面在缺页时再分配
  }
  return addr; // 返回扩展前的堆顶
  //my code end
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void)
{
  //my code begin
  uint64 addr, length, offset; // 保存用户传入的地址、长度和文件偏移
  int prot, flags, fd; // 保存保护位、映射类型和文件描述符
  struct file *f; // 保存被映射的文件对象
  struct proc *p = myproc(); // 获取当前进程
  uint64 base; // 保存内核选择的映射起始地址
  int slot = -1; // 保存可用 VMA 槽位索引

  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || // 读取地址和长度参数
     argint(2, &prot) < 0 || argint(3, &flags) < 0 || // 读取权限和映射类型参数
     argint(4, &fd) < 0 || argaddr(5, &offset) < 0 || // 读取文件描述符和偏移参数
     addr != 0 || length == 0 || offset != 0 || // 仅支持内核选址、非零长度和零偏移
     (prot & ~(PROT_READ | PROT_WRITE)) || // 仅支持读写保护位
     (flags != MAP_SHARED && flags != MAP_PRIVATE) || fd < 0 || // 仅支持两种映射类型且描述符非负
     fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_INODE) // 文件描述符必须指向 inode 文件
    return -1; // 参数或文件类型不符合实验接口
  if(flags == MAP_SHARED && (prot & PROT_WRITE) && !f->writable) // 共享可写映射要求文件本身可写
    return -1; // 文件不可写时拒绝映射
  length = PGROUNDUP(length); // 将映射长度向上对齐为整页
  for(int i = 0; i < 16; i++) // 查找一个空闲 VMA 槽位
    if(!p->vmas[i].used){ // 当前槽位尚未使用
      slot = i; // 记录可用槽位
      break; // 停止查找
    }
  if(slot < 0) // 所有 VMA 槽位均已占用
    return -1; // 无法创建新映射
  base = TRAPFRAME - length; // 从 trapframe 下方开始寻找映射空间
  for(int i = 0; i < 16; i++) // 检查新映射是否与已有 VMA 重叠
    if(p->vmas[i].used && base < p->vmas[i].addr + p->vmas[i].length && // 新起点落在已有 VMA 前
       base + length > p->vmas[i].addr) // 新终点落在已有 VMA 后
      base = p->vmas[i].addr - length; // 将新映射下移至已有 VMA 下方
  if(base < PGROUNDUP(p->sz) || base + length > TRAPFRAME) // 映射不得覆盖进程内存或 trapframe
    return -1; // 地址空间不足
  p->vmas[slot].addr = base; // 保存映射起始地址
  p->vmas[slot].length = length; // 保存页对齐后的映射长度
  p->vmas[slot].offset = 0; // 保存实验要求的零文件偏移
  p->vmas[slot].prot = prot; // 保存访问权限
  p->vmas[slot].flags = flags; // 保存共享或私有映射类型
  p->vmas[slot].file = filedup(f); // 增加文件引用，允许用户关闭原描述符
  p->vmas[slot].used = 1; // 标记 VMA 槽位已使用
  return base; // 返回内核选择的映射地址
  //my code end
}

uint64
sys_munmap(void)
{
  //my code begin
  uint64 addr, length; // 保存待解除映射的地址和长度
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || // 读取 munmap 参数
     length == 0 || addr % PGSIZE || length % PGSIZE) // 要求非零且页对齐
    return -1; // 参数非法
  return mmapunmap(myproc(), addr, length); // 解除对应 VMA 中的页面
  //my code end
}
