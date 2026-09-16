#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array

#define LEAF_SIZE     16                         // The smallest block size
#define MAXSIZE       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXSIZE) 
#define NBLK(k)       (1 << (MAXSIZE-k))         // Number of block at size k
#define NPAIR(k)      ((NBLK(k)+1)/2)            // k 阶伙伴块对数
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct sz_info {
  Bd_list free;
  char *alloc;
  char *split;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes; 
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int bit_isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

// Flip bit at position index in array
void bit_flip(char *array, int index) {
  //my code begin
  char m = (1 << (index % 8)); // 生成目标位的掩码
  array[index/8] ^= m; // 翻转目标位
  //my code end
}

// Return the index of the bit that tracks a pair of buddy blocks.
int
pair_index(int bi) {
  //my code begin
  return bi / 2; // 每两个伙伴块共用一个状态位
  //my code end
}

// Print a bit vector as a list of ranges of 1 bits
void
bd_print_vector(char *vector, int len) {
  int last, lb;
  
  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++) {
    if (last == bit_isset(vector, b))
      continue;
    if(last == 1)
      printf(" [%d, %d)", lb, b);
    lb = b;
    last = bit_isset(vector, b);
  }
  if(lb == 0 || last == 1) {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void
bd_print() {
  //my code begin
  for (int k = 0; k < nsizes; k++) { // 依次输出每个阶的状态
    printf("size %d (blksz %d nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k)); // 输出阶号、块大小和空闲链表标题
    lst_print(&bd_sizes[k].free); // 输出当前阶的空闲块链表
    printf("  alloc:"); // 输出分配位图标题
    bd_print_vector(bd_sizes[k].alloc, NPAIR(k)); // 输出伙伴对分配位图
    if(k > 0) { // 最小块不可能再被拆分
      printf("  split:"); // 输出拆分位图标题
      bd_print_vector(bd_sizes[k].split, NBLK(k)); // 输出拆分位图
    }
  }
  //my code end
}

// What is the first k such that 2^k >= n?
int
firstk(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
int
blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *
bd_malloc(uint64 nbytes)
{
  //my code begin
  int fk, k; // 保存所需最小阶和实际获得的空闲阶

  acquire(&lock); // 保护伙伴分配器的全局状态

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes); // 计算满足请求大小的最小阶
  for (k = fk; k < nsizes; k++) { // 从小到大寻找空闲块
    if(!lst_empty(&bd_sizes[k].free)) // 当前阶存在空闲块
      break; // 停止查找
  }
  if(k >= nsizes) { // 没有可用的空闲块
    release(&lock); // 释放分配器锁
    return 0; // 返回分配失败
  }

  // Found a block; pop it and potentially split it.
  char *p = lst_pop(&bd_sizes[k].free); // 从找到的空闲链表取出一个块
  bit_flip(bd_sizes[k].alloc, pair_index(blk_index(k, p))); // 更新该伙伴对的分配状态
  for(; k > fk; k--) { // 逐阶拆分，直到达到请求的阶
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    char *q = p + BLK_SIZE(k-1);   // 计算拆分后 p 的伙伴块地址
    bit_set(bd_sizes[k].split, blk_index(k, p)); // 标记高一阶块已经拆分
    bit_flip(bd_sizes[k-1].alloc, pair_index(blk_index(k-1, p))); // 标记 p 所在伙伴对的一半已分配
    lst_push(&bd_sizes[k-1].free, q); // 将另一半伙伴块加入低一阶空闲链表
  }
  release(&lock); // 完成分配后释放锁

  return p; // 返回分配出的块地址
  //my code end
}

// Find the size of the block that p points to.
int
size(char *p) {
  //my code begin
  for (int k = 0; k < MAXSIZE; k++) { // 从小到大检查所属块是否被上层拆分
    if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) { // 找到 p 作为独立块的最低阶
      return k; // 返回该块的阶
    }
  }
  return 0; // 未发现拆分记录时视为最小阶
  //my code end
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void
bd_free(void *p) {
  //my code begin
  void *q; // 保存伙伴块地址
  int k; // 保存当前合并阶

  acquire(&lock); // 保护伙伴分配器状态
  for (k = size(p); k < MAXSIZE; k++) { // 从 p 当前阶开始尝试向上合并
    int bi = blk_index(k, p); // 计算 p 在当前阶中的块索引
    int buddy = (bi % 2 == 0) ? bi+1 : bi-1; // 计算 p 的伙伴块索引
    int pi = pair_index(bi); // 计算伙伴对的状态位索引
    int merge = bit_isset(bd_sizes[k].alloc, pi); // 判断伙伴块是否已分配
    bit_flip(bd_sizes[k].alloc, pi);  // 将 p 标记为空闲
    if (merge == 0) {  // 伙伴块也为空闲时无需继续合并
      break;   // 退出循环并将 p 入空闲链表
    }
    // budy is free; merge with buddy
    q = addr(k, buddy); // 取得伙伴块地址
    lst_remove(q);    // 将伙伴块从空闲链表移除
    if(buddy % 2 == 0) { // 合并块起点应选择两个块中地址较小者
      p = q; // 更新合并后块的起始地址
    }
    // at size k+1, mark that the merged buddy pair isn't split
    // anymore
    bit_clear(bd_sizes[k+1].split, blk_index(k+1, p)); // 清除上层块的拆分标志
  }
  lst_push(&bd_sizes[k].free, p); // 将最终块加入对应阶的空闲链表
  release(&lock); // 释放分配器锁
  //my code end
}

// Compute the first block at size k that doesn't contain p
int
blk_index_next(int k, char *p) {
  int n = (p - (char *) bd_base) / BLK_SIZE(k);
  if((p - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int
log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated. 
void
bd_mark(void *start, void *stop)
{
  //my code begin
  int bi, bj; // 保存区间起止块索引

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0)) // 元数据区间必须按最小块对齐
    panic("bd_mark"); // 对齐错误时终止内核

  for (int k = 0; k < nsizes; k++) { // 在每一阶标记不可分配范围
    bi = blk_index(k, start); // 计算起始块索引
    bj = blk_index_next(k, stop); // 计算终止块后一项索引
    for(; bi < bj; bi++) { // 标记区间覆盖的每个块
      if(k > 0) { // 非最小阶块需要记录拆分状态
        // if a block is allocated at size k, mark it as split too.
        bit_set(bd_sizes[k].split, bi); // 标记该块不可作为完整块分配
      }
      bit_flip(bd_sizes[k].alloc, pair_index(bi)); // 更新伙伴对分配状态
    }
  }
  //my code end
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
int
bd_initfree_pair(int k, int bi, int free_bi) {
  //my code begin
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1; // 计算当前块的伙伴索引
  int free = 0; // 保存本次加入空闲链表的字节数
  if(bi < 0 || bi >= NBLK(k) || buddy < 0 || buddy >= NBLK(k) || // 检查伙伴对索引范围
     free_bi < 0 || free_bi >= NBLK(k)) // 检查待加入空闲链表的块索引
    return 0; // 索引无效时不处理
  if(bit_isset(bd_sizes[k].alloc, pair_index(bi))) { // 伙伴对恰有一块已占用
    // one of the pair is free
    free = BLK_SIZE(k); // 累计一块空闲内存
    lst_push(&bd_sizes[k].free, addr(k, free_bi));   // 将空闲伙伴块加入空闲链表
  }
  return free; // 返回新增空闲内存大小
  //my code end
}
  
// Initialize the free lists for each size k.  For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right.
int
bd_initfree(void *bd_left, void *bd_right) {
  //my code begin
  int free = 0; // 统计加入空闲链表的总字节数

  for (int k = 0; k < MAXSIZE; k++) {   // 跳过最大阶并初始化每阶空闲块
    int left = blk_index_next(k, bd_left); // 计算左边界后的首个块
    int right = blk_index(k, bd_right); // 计算右边界所在块
    free += bd_initfree_pair(k, left, left); // 处理左边界附近的伙伴对
    if(right <= left) // 两边界位于同一区域时无需处理右侧
      continue; // 进入下一阶
    int right_buddy = (right % 2 == 0) ? right+1 : right-1; // 计算右边界块的伙伴索引
    free += bd_initfree_pair(k, right, right_buddy); // 处理右边界附近的伙伴对
  }
  return free; // 返回总空闲内存大小
  //my code end
}

// Mark the range [bd_base,p) as allocated
int
bd_mark_data_structures(char *p) {
  int meta = p - (char*)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int
bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE(MAXSIZE)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void
bd_init(void *base, void *end) {
  //my code begin
  char *p = (char *) ROUNDUP((uint64)base, LEAF_SIZE); // 将管理区起点对齐到最小块边界
  int sz; // 保存位图所需的字节数

  initlock(&lock, "buddy"); // 初始化伙伴分配器锁
  bd_base = (void *) p; // 记录可管理内存的起始地址

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end-p)/LEAF_SIZE) + 1; // 计算覆盖可用内存所需的阶数
  if((char*)end-p > BLK_SIZE(MAXSIZE)) { // 内存不能被当前最大阶完整覆盖时
    nsizes++;  // 将最大块大小向上扩展到下一个二次幂
  }

  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char*) end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) p; // 在可管理内存前部放置阶信息数组
  p += sizeof(Sz_info) * nsizes; // 跳过阶信息数组占用的空间
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes); // 清零所有阶信息

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; k++) { // 初始化每一阶的空闲链表和分配位图
    lst_init(&bd_sizes[k].free); // 初始化当前阶空闲链表
    sz = sizeof(char)* ROUNDUP(NPAIR(k), 8)/8; // 计算当前阶伙伴对分配位图大小
    bd_sizes[k].alloc = p; // 指定当前阶分配位图位置
    memset(bd_sizes[k].alloc, 0, sz); // 清零分配位图
    p += sz; // 移动到下一段元数据空间
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  for (int k = 1; k < nsizes; k++) { // 为非最小阶分配拆分位图
    sz = sizeof(char)* (ROUNDUP(NBLK(k), 8))/8; // 计算当前阶拆分位图大小
    bd_sizes[k].split = p; // 指定当前阶拆分位图位置
    memset(bd_sizes[k].split, 0, sz); // 清零拆分位图
    p += sz; // 移动到下一段元数据空间
  }
  p = (char *) ROUNDUP((uint64) p, LEAF_SIZE); // 将可分配区域起点按最小块对齐

  // done allocating; mark the memory range [base, p) as allocated, so
  // that buddy will not hand out that memory.
  int meta = bd_mark_data_structures(p); // 标记分配器元数据占用的内存
  
  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  int unavailable = bd_mark_unavailable(end, p); // 标记物理内存末尾不可用的区域
  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable; // 计算实际可管理区域的终点
  
  // initialize free lists for each size k
  int free = bd_initfree(p, bd_end); // 将可用伙伴块加入各阶空闲链表

  // check if the amount that is free is what we expect
  if(free != BLK_SIZE(MAXSIZE)-meta-unavailable) { // 验证统计的空闲容量正确
    printf("free %d %d\n", free, BLK_SIZE(MAXSIZE)-meta-unavailable); // 输出实际与期望的空闲容量
    panic("bd_init: free mem"); // 初始化状态不一致时终止内核
  }
  //my code end
}
