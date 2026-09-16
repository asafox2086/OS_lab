// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#define NLOCK 1000

static int nlock;
static struct spinlock *locks[NLOCK];

void
initlock(struct spinlock *lk, char *name)
{
  //my code begin
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
  lk->nts = 0; // 清零该锁的竞争次数
  lk->n = 0; // 清零该锁的获取次数
  if(nlock >= NLOCK) // 防止锁统计表越界
    panic("initlock"); // 锁数量超出统计表容量时终止内核
  locks[nlock] = lk; // 将新锁登记到统计表
  nlock++; // 更新已登记锁数量
  //my code end
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  //my code begin
  push_off(); // disable interrupts to avoid deadlock.
  if(holding(lk))
    panic("acquire");

  __sync_fetch_and_add(&lk->n, 1); // 记录一次获取锁的调用

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0) {
     __sync_fetch_and_add(&lk->nts, 1); // 记录一次因锁被占用导致的自旋
  }
  
  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen after the lock is acquired.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
  //my code end
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released.
  // On RISC-V, this turns into a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
int
holding(struct spinlock *lk)
{
  int r;
  push_off();
  r = (lk->locked && lk->cpu == mycpu());
  pop_off();
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  c->noff -= 1;
  if(c->noff < 0)
    panic("pop_off");
  if(c->noff == 0 && c->intena)
    intr_on();
}

uint64
sys_ntas(void)
{
  //my code begin
  int print; // 保存用户请求的统计模式
  int total = 0; // 累加 kmem 与 bcache 锁的竞争次数
  if(argint(0, &print) < 0) // 读取用户传入的模式参数
    return -1; // 参数读取失败
  if(print == 0){ // 模式零用于清空全部锁统计
    for(int i = 0; i < nlock; i++){ // 遍历已登记的所有锁
      locks[i]->nts = 0; // 清零竞争次数
      locks[i]->n = 0; // 清零获取次数
    }
    return 0; // 清零完成
  }
  printf("=== lock kmem/bcache stats\n"); // 输出实验关注锁的统计标题
  for(int i = 0; i < nlock; i++){ // 遍历所有已登记锁
    if(strncmp(locks[i]->name, "bcache", strlen("bcache")) == 0 || // 匹配块缓存锁
       strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0){ // 匹配每 CPU 内存锁
      total += locks[i]->nts; // 累加相关锁的竞争次数
      if(locks[i]->n > 0) // 跳过从未使用的锁
        printf("lock: %s: #test-and-set %d #acquire() %d\n", locks[i]->name, locks[i]->nts, locks[i]->n); // 输出该锁统计
    }
  }
  return total; // 返回实验关注锁的竞争次数总和
  //my code end
}
