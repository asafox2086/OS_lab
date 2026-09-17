# xv6 Lab 10：用户态线程与闹钟解题思路

这一题包含两条相互独立但都与上下文保存有关的路径：用户态线程切换需要遵守 RISC-V ABI，闹钟处理则需要完整保存并恢复用户态 trapframe。

## 一、从哪里入手

### `user/uthread.c`、`user/uthread_switch.S`

修改函数：`thread_schedule()`、`thread_create()`、`uthread_switch()`；扩展 `struct thread`。

- `struct thread` 增加 `ra`、`sp` 与 `s0` 到 `s11`，用于保存 RISC-V ABI 规定的被调用者保存寄存器。
- `thread_create()` 初始化新线程的私有栈顶和返回地址，使第一次切换后的 `ret` 直接进入线程函数。
- `thread_schedule()` 在切换前保存旧线程指针、更新 `current_thread`，再调用 `uthread_switch()`。
- `uthread_switch()` 保存旧线程并恢复新线程的 `ra`、`sp`、`s0` 到 `s11`，然后以 `ret` 进入恢复后的执行位置。

### `kernel/proc.h`、`kernel/proc.c`

修改函数：`allocproc()`；扩展 `struct proc`。

- 每个进程增加闹钟周期、已计时滴答、处理函数地址、活动标记和完整 trapframe 备份。
- `allocproc()` 初始化上述状态，避免复用进程槽位时继承旧闹钟。

### `kernel/trap.c`、`kernel/sysproc.c`

修改函数：`usertrap()`、新增 `sys_sigalarm()` 与 `sys_sigreturn()`。

- `usertrap()` 只在用户态时钟中断时累计当前进程的闹钟；到期后备份 trapframe、标记处理函数正在执行并把 `epc` 改为处理函数地址。
- `sys_sigalarm()` 保存新的周期与处理函数；`sys_sigreturn()` 恢复完整 trapframe，清除活动标记。

### `kernel/syscall.h`、`kernel/syscall.c`、`user/user.h`、`user/usys.pl`、`Makefile`

- 注册 `sigalarm`、`sigreturn` 系统调用及用户态存根和声明。
- 在 `UPROGS` 中加入 `_alarmtest`，使测试程序写入 `fs.img`。

## 二、上下文切换与闹钟流程

用户态线程仅在主动调用 `thread_yield()` 时切换。调度器选择一个可运行线程，把旧线程的被调用者保存寄存器写入其线程结构，再从新线程结构恢复相同寄存器。新线程的 `ra` 预设为线程函数、`sp` 预设为私有栈顶，所以第一次恢复后 `ret` 直接开始执行该函数；后续恢复则回到上次 `thread_yield()` 之后。

具体切换顺序是：当前线程调用 `thread_yield()` 后将自身状态改为 `RUNNABLE`；`thread_schedule()` 找到另一个 `RUNNABLE` 槽位，保存旧线程指针并把 `current_thread` 指向新线程。汇编 `uthread_switch()` 按 ABI 将旧线程的 `ra`、`sp`、`s0` 至 `s11` 写入旧结构，再从新结构的相同偏移恢复它们；`ret` 使用恢复后的 `ra` 跳转。只保存这些寄存器是因为 `a*`、`t*` 寄存器本来由调用者保存，而切换函数是普通函数调用边界。

进程调用 `sigalarm(interval, handler)` 后，内核在该进程每次从用户态进入的时钟中断中增加计数。计数达到周期时，内核复制完整 trapframe，并将 `epc` 改为 `handler`；`usertrapret()` 因而返回到用户态处理函数。处理函数调用 `sigreturn()` 后，内核还原备份 trapframe 并返回原 `a0`，防止系统调用返回值覆盖已恢复寄存器。`alarm_active` 在处理函数执行期间阻止再次递交，避免重入；周期为零时关闭闹钟，处理函数地址零仍可合法递交。

具体闹钟路径是：`sigalarm()` 经系统调用把参数存入当前 `proc`，并清零已累计滴答；用户态时钟中断进入 `usertrap()` 后，仅当 `which_dev == 2`、周期非零且 `alarm_active == 0` 时递增计数。到期时先备份全部 trapframe，再置活动标志、归零周期计数并把保存的用户 PC (`epc`) 改为处理函数地址。处理函数中的 `sigreturn()` 再次进入内核，完整覆盖当前 trapframe、清除活动标志；系统调用框架随后写回的返回值被设置为备份的 `a0`，所以所有寄存器和被打断指令都准确恢复。

测试：`make fs.img` 与 `make kernel/kernel` 均通过；`uthread` 的两个线程各执行 100 次后正常退出；`alarmtest` 输出 `test0 passed`、`test1 passed`。`usertests` 在这部分改动前已有的懒分配/文件系统基线上于 `sbrkfail` 失败，之后停在 `writebig`；未为该无关问题修改代码。
