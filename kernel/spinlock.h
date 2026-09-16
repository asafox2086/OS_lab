// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  uint nts;          // 未成功的 test-and-set 次数
  uint n;            // acquire 调用次数
};
