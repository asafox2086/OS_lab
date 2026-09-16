# xv6 Lab 8：锁竞争优化实验报告

## 一、改了哪里

### `kernel/kalloc.c`

修改函数：`kinit()`、`kalloc()`、`kfree()`。

- 新增 `kmem[NCPU]`，每项包含一个名为 `kmem` 的自旋锁和一个整页空闲链表。
- `kinit()` 初始化所有 CPU 的本地链表，并从现有 buddy 分配器预取少量页面均匀放入各 CPU 链表。保留此前实验的 buddy 分配器，不替换其块级分配逻辑。
- `kalloc()` 优先从当前 CPU 的链表取页；本地为空时依次从其他 CPU 链表偷取一页；全部为空时才调用 `bd_malloc(PGSIZE)`。
- `kfree()` 在引用计数降为 0 后，将页面归还当前 CPU 的本地链表，而不是立即归还 buddy 分配器。
- 使用 `push_off()` / `pop_off()` 包围 `cpuid()`，确保读取 CPU 编号期间不会因中断迁移 CPU。

### `kernel/spinlock.h`、`kernel/spinlock.c`

修改函数：`initlock()`、`acquire()`、`sys_ntas()`；修改 `struct spinlock`。

- 每把锁增加 `nts`（未成功的 test-and-set 次数）和 `n`（`acquire()` 调用次数）。
- `initlock()` 将锁登记到固定统计表；`acquire()` 统计竞争和获取次数。
- `sys_ntas(0)` 清零统计；`sys_ntas(1)` 输出并返回名称以 `kmem` 或 `bcache` 开头的锁的竞争统计。

### `kernel/bio.c`

修改函数：新增 `bhash()`；修改 `binit()`、`bget()`、`brelse()`、`bpin()`、`bunpin()`。

- 将原来的单一 `bcache.lock` 加全局 LRU 链表改为 13 个固定哈希桶，每桶一个名为 `bcache.bucket` 的锁和一个 buffer 链表。
- `bget()` 命中时只锁目标桶；未命中时使用保留的 `bcache.lock` 串行化空闲 buffer 回收，同时在目标桶中二次查找，保证同一 `(dev, blockno)` 最多只有一个缓存副本。
- `brelse()`、`bpin()`、`bunpin()` 仅锁 buffer 所属的哈希桶，不再争用全局缓存锁。

## 二、改之后的算法

### 每 CPU 页面缓存

分配页面时先在当前 CPU 的私有链表上加锁并弹出页面，因此不同 CPU 的高频 `kalloc()`/`kfree()` 通常操作不同锁。当前 CPU 没有空闲页时，再逐个尝试从其他 CPU 链表偷取；只有所有链表都为空时才进入已有的全局 buddy 分配器。释放页面时先维护引用计数，最后一个引用消失后将该页压回当前 CPU 链表。这样兼容 COW 的引用计数，也减少 buddy 锁的访问频率。

### 哈希块缓存

块号通过 `(dev + blockno) % 13` 映射到哈希桶。读取已缓存块时仅在对应桶中查找和增加引用计数，不同桶可并行工作。缓存未命中时，先获得 `bcache.lock` 回收锁，再锁住目标桶并二次查找；若仍未命中，扫描各桶选择一个 `refcnt == 0` 的 buffer，将它从旧桶移入目标桶并重新标记。`brelse()`、日志固定和取消固定都只更新所属桶中的引用计数。二次查找与回收锁共同避免并发未命中产生同一磁盘块的重复缓存。

### 测试

- `make kernel/kernel`：通过。
- `make fs.img`：通过。
- `kalloctest`：`test0` 的相关锁竞争统计为 `5`，`test1` 分配到 `31924/32768` 页并完成。
- `bcachetest`：`test0` 的相关锁竞争统计为 `0`，`test1 done`。

现有 lazy/COW 基线的内存耗尽路径仍会在 `kalloctest test1` 中打印已有的用户缺页信息；该实验没有修改该路径，测试仍完成所需页数检查。
