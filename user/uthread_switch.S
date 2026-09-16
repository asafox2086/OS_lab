	.text

/* Switch from current_thread to next thread_thread, and make
 * next_thread the current_thread.  Use t0 as a temporary register,
 * which should be caller saved. */

	.globl uthread_switch
uthread_switch:
	# //my code begin
	sd ra, 0(a0) # 保存当前线程的返回地址
	sd sp, 8(a0) # 保存当前线程的栈指针
	sd s0, 16(a0) # 保存当前线程的 s0 寄存器
	sd s1, 24(a0) # 保存当前线程的 s1 寄存器
	sd s2, 32(a0) # 保存当前线程的 s2 寄存器
	sd s3, 40(a0) # 保存当前线程的 s3 寄存器
	sd s4, 48(a0) # 保存当前线程的 s4 寄存器
	sd s5, 56(a0) # 保存当前线程的 s5 寄存器
	sd s6, 64(a0) # 保存当前线程的 s6 寄存器
	sd s7, 72(a0) # 保存当前线程的 s7 寄存器
	sd s8, 80(a0) # 保存当前线程的 s8 寄存器
	sd s9, 88(a0) # 保存当前线程的 s9 寄存器
	sd s10, 96(a0) # 保存当前线程的 s10 寄存器
	sd s11, 104(a0) # 保存当前线程的 s11 寄存器
	ld ra, 0(a1) # 恢复下一线程的返回地址
	ld sp, 8(a1) # 恢复下一线程的栈指针
	ld s0, 16(a1) # 恢复下一线程的 s0 寄存器
	ld s1, 24(a1) # 恢复下一线程的 s1 寄存器
	ld s2, 32(a1) # 恢复下一线程的 s2 寄存器
	ld s3, 40(a1) # 恢复下一线程的 s3 寄存器
	ld s4, 48(a1) # 恢复下一线程的 s4 寄存器
	ld s5, 56(a1) # 恢复下一线程的 s5 寄存器
	ld s6, 64(a1) # 恢复下一线程的 s6 寄存器
	ld s7, 72(a1) # 恢复下一线程的 s7 寄存器
	ld s8, 80(a1) # 恢复下一线程的 s8 寄存器
	ld s9, 88(a1) # 恢复下一线程的 s9 寄存器
	ld s10, 96(a1) # 恢复下一线程的 s10 寄存器
	ld s11, 104(a1) # 恢复下一线程的 s11 寄存器
	ret # 跳转到下一线程保存的返回地址
	# //my code end
