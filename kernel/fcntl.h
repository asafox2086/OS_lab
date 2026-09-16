#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_NOFOLLOW 0x400 // 打开符号链接本身而不跟随目标
#define PROT_READ  0x1 // 映射页允许读取
#define PROT_WRITE 0x2 // 映射页允许写入
#define MAP_SHARED 0x1 // 共享映射在解除时写回文件
#define MAP_PRIVATE 0x2 // 私有映射的修改不写回文件
