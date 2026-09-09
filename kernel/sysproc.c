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
  int addr;
  int n;
  struct proc *p = myproc();

  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;
  if(n < 0){
    if(growproc(n) < 0)
      return -1;
  } else {
    p->sz += n;
  }
  return addr;
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
  uint64 addr, length, offset;
  int prot, flags, fd;
  struct file *f;
  struct proc *p = myproc();
  uint64 base;
  int slot = -1;

  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 ||
     argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
     argint(4, &fd) < 0 || argaddr(5, &offset) < 0 ||
     addr != 0 || length == 0 || offset != 0 ||
     (prot & ~(PROT_READ | PROT_WRITE)) ||
     (flags != MAP_SHARED && flags != MAP_PRIVATE) || fd < 0 ||
     fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_INODE)
    return -1;
  if(flags == MAP_SHARED && (prot & PROT_WRITE) && !f->writable)
    return -1;
  length = PGROUNDUP(length);
  for(int i = 0; i < 16; i++)
    if(!p->vmas[i].used){
      slot = i;
      break;
    }
  if(slot < 0)
    return -1;
  base = TRAPFRAME - length;
  for(int i = 0; i < 16; i++)
    if(p->vmas[i].used && base < p->vmas[i].addr + p->vmas[i].length &&
       base + length > p->vmas[i].addr)
      base = p->vmas[i].addr - length;
  if(base < PGROUNDUP(p->sz) || base + length > TRAPFRAME)
    return -1;
  p->vmas[slot].addr = base;
  p->vmas[slot].length = length;
  p->vmas[slot].offset = 0;
  p->vmas[slot].prot = prot;
  p->vmas[slot].flags = flags;
  p->vmas[slot].file = filedup(f);
  p->vmas[slot].used = 1;
  return base;
  //my code end
}

uint64
sys_munmap(void)
{
  //my code begin
  uint64 addr, length;
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 ||
     length == 0 || addr % PGSIZE || length % PGSIZE)
    return -1;
  return mmapunmap(myproc(), addr, length);
  //my code end
}
