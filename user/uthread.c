#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Possible states of a thread; */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

typedef struct thread thread_t, *thread_p;
typedef struct mutex mutex_t, *mutex_p;

struct thread {
  uint64     ra;                // 保存线程切换后的返回地址
  uint64     sp;                /* saved stack pointer */
  uint64     s0;                // 保存被调用者保存寄存器 s0
  uint64     s1;                // 保存被调用者保存寄存器 s1
  uint64     s2;                // 保存被调用者保存寄存器 s2
  uint64     s3;                // 保存被调用者保存寄存器 s3
  uint64     s4;                // 保存被调用者保存寄存器 s4
  uint64     s5;                // 保存被调用者保存寄存器 s5
  uint64     s6;                // 保存被调用者保存寄存器 s6
  uint64     s7;                // 保存被调用者保存寄存器 s7
  uint64     s8;                // 保存被调用者保存寄存器 s8
  uint64     s9;                // 保存被调用者保存寄存器 s9
  uint64     s10;               // 保存被调用者保存寄存器 s10
  uint64     s11;               // 保存被调用者保存寄存器 s11
  char stack[STACK_SIZE];       /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
};
static thread_t all_thread[MAX_THREAD];
thread_p  current_thread;
thread_p  next_thread;
extern void uthread_switch(uint64, uint64);
              
void 
thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule().  it needs a stack so that the first uthread_switch() can
  // save thread 0's state.  thread_schedule() won't run the main thread ever
  // again, because its state is set to RUNNING, and thread_schedule() selects
  // a RUNNABLE thread.
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

static void 
thread_schedule(void)
{
  //my code begin
  thread_p t;

  /* Find another runnable thread. */
  next_thread = 0;
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == RUNNABLE && t != current_thread) {
       next_thread = t;
      break;
    }
  }

  if (t >= all_thread + MAX_THREAD && current_thread->state == RUNNABLE) {
    /* The current thread is the only runnable thread; run it. */
    next_thread = current_thread;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    thread_p previous_thread = current_thread; // 保存即将切出的线程
    current_thread = next_thread; // 在切换前更新当前线程指针
    uthread_switch((uint64)previous_thread, (uint64)next_thread); // 保存旧线程并恢复新线程上下文
  } else
    next_thread = 0;
  //my code end
}

void 
thread_create(void (*func)())
{
  //my code begin
  thread_p t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->ra = (uint64)func; // 让首次恢复上下文后的 ret 跳转到线程函数
  t->sp = (uint64)(t->stack + STACK_SIZE); // 将线程私有栈顶作为首次运行的栈指针
  t->state = RUNNABLE;
  //my code end
}

void 
thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

static void 
mythread(void)
{
  int i;
  printf("my thread running\n");
  for (i = 0; i < 100; i++) {
    printf("my thread %p\n", (uint64) current_thread);
    thread_yield();
  }
  printf("my thread: exit\n");
  current_thread->state = FREE;
  thread_schedule();
}


int 
main(int argc, char *argv[]) 
{
  thread_init();
  thread_create(mythread);
  thread_create(mythread);
  thread_schedule();
  exit(0);
}
