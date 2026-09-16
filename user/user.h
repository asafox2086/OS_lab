struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int connect(uint32, uint16, uint16); // 声明创建并绑定 UDP socket 的接口
int ntas();
int crash(const char*, int);
int mount(char*, char *);
int umount(char*);
int symlink(const char*, const char*); // 声明创建符号链接的用户接口
void *mmap(void*, uint, int, int, int, uint); // 声明文件内存映射用户接口
int munmap(void*, uint); // 声明解除文件内存映射用户接口
int sigalarm(int, void (*)()); // 声明设置用户态周期闹钟的接口
int sigreturn(void); // 声明恢复闹钟打断现场的接口

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint); // 声明内存比较函数
void *memcpy(void *, const void *, uint); // 声明内存复制函数
