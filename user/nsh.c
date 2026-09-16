#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAXLINE 128
#define MAXARGS 16

void
runsegment(char *line)
{
  //my code begin
  char *argv[MAXARGS]; // 保存待执行程序的参数指针
  char *input = 0; // 保存输入重定向文件名
  char *output = 0; // 保存输出重定向文件名
  int argc = 0; // 记录参数数量
  char *cursor = line; // 指向当前待解析字符

  while(*cursor){ // 逐个解析命令行字符
    while(*cursor == ' ' || *cursor == '\t') // 跳过参数之间的空白字符
      *cursor++ = 0; // 将空白替换为字符串结束符
    if(*cursor == 0) // 已到达命令行末尾
      break; // 结束解析
    if(*cursor == '<' || *cursor == '>'){ // 识别输入或输出重定向符号
      char type = *cursor++; // 保存当前重定向类型并跳过符号
      while(*cursor == ' ' || *cursor == '\t') // 跳过符号后的空白字符
        *cursor++ = 0; // 将空白替换为字符串结束符
      char *file = cursor; // 记录重定向文件名的起始位置
      while(*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '<' && *cursor != '>') // 扫描文件名
        cursor++; // 移动到文件名结尾
      if(cursor == file){ // 重定向符号后没有文件名
        fprintf(2, "nsh: missing file\n"); // 向标准错误报告语法错误
        exit(1); // 结束当前命令进程
      }
      if(*cursor) // 文件名后仍有分隔字符
        *cursor++ = 0; // 结束文件名字符串并继续解析
      if(type == '<') // 当前为输入重定向
        input = file; // 保存输入文件名
      else // 当前为输出重定向
        output = file; // 保存输出文件名
      continue; // 继续解析后续参数或重定向
    }
    if(argc >= MAXARGS - 1){ // 参数数量超过固定数组容量
      fprintf(2, "nsh: too many arguments\n"); // 向标准错误报告错误
      exit(1); // 结束当前命令进程
    }
    argv[argc++] = cursor; // 记录一个普通命令参数
    while(*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '<' && *cursor != '>') // 扫描参数结尾
      cursor++; // 移动到参数末尾
    if(*cursor) // 参数后仍有分隔字符
      *cursor++ = 0; // 结束参数字符串并继续解析
  }
  argv[argc] = 0; // 为 exec 参数数组补充空指针结尾
  if(argc == 0) // 空命令无需执行
    exit(0); // 正常结束当前命令进程
  if(input){ // 存在输入重定向时替换标准输入
    close(0); // 关闭当前标准输入描述符
    if(open(input, O_RDONLY) != 0){ // 以只读方式打开输入文件并要求占用描述符零
      fprintf(2, "nsh: open %s failed\n", input); // 向标准错误报告打开失败
      exit(1); // 结束当前命令进程
    }
  }
  if(output){ // 存在输出重定向时替换标准输出
    close(1); // 关闭当前标准输出描述符
    if(open(output, O_WRONLY | O_CREATE) != 1){ // 以创建和只写方式打开输出文件并要求占用描述符一
      fprintf(2, "nsh: open %s failed\n", output); // 向标准错误报告打开失败
      exit(1); // 结束当前命令进程
    }
  }
  exec(argv[0], argv); // 使用解析出的程序名和参数执行新程序
  fprintf(2, "nsh: exec %s failed\n", argv[0]); // 执行失败时向标准错误报告
  exit(1); // 结束执行失败的子进程
  //my code end
}

void
runline(char *line)
{
  //my code begin
  char *pipechar = 0; // 保存第一个管道符号的位置
  for(char *cursor = line; *cursor; cursor++) // 查找命令行中的管道符号
    if(*cursor == '|'){ // 找到管道符号时
      pipechar = cursor; // 保存其位置
      break; // 仅处理当前最左侧管道，其右侧递归处理
    }
  if(pipechar == 0){ // 当前命令行不含管道
    runsegment(line); // 直接解析并执行单个命令段
  }

  *pipechar = 0; // 将管道符号替换为字符串结束符，分割左右命令
  int pipefd[2]; // 保存新管道的读写端描述符
  if(pipe(pipefd) < 0){ // 创建左右命令间的管道
    fprintf(2, "nsh: pipe failed\n"); // 向标准错误报告创建失败
    exit(1); // 结束当前命令进程
  }
  int left = fork(); // 创建执行管道左侧命令的子进程
  if(left < 0){ // 检查创建子进程是否失败
    fprintf(2, "nsh: fork failed\n"); // 向标准错误报告创建失败
    exit(1); // 结束当前命令进程
  }
  if(left == 0){ // 左侧子进程将标准输出接到管道写端
    close(1); // 关闭原标准输出
    if(dup(pipefd[1]) != 1){ // 复制管道写端到标准输出
      fprintf(2, "nsh: dup failed\n"); // 向标准错误报告复制失败
      exit(1); // 结束左侧子进程
    }
    close(pipefd[0]); // 左侧进程不使用管道读端
    close(pipefd[1]); // 复制后关闭原管道写端
    runline(line); // 执行左侧命令，支持左侧继续包含管道
  }
  int right = fork(); // 创建执行管道右侧命令的子进程
  if(right < 0){ // 检查创建子进程是否失败
    fprintf(2, "nsh: fork failed\n"); // 向标准错误报告创建失败
    exit(1); // 结束当前命令进程
  }
  if(right == 0){ // 右侧子进程将标准输入接到管道读端
    close(0); // 关闭原标准输入
    if(dup(pipefd[0]) != 0){ // 复制管道读端到标准输入
      fprintf(2, "nsh: dup failed\n"); // 向标准错误报告复制失败
      exit(1); // 结束右侧子进程
    }
    close(pipefd[0]); // 复制后关闭原管道读端
    close(pipefd[1]); // 右侧进程不使用管道写端
    runline(pipechar + 1); // 执行右侧命令，支持更多管道段
  }
  close(pipefd[0]); // 父进程关闭管道读端以便读者看到文件结束
  close(pipefd[1]); // 父进程关闭管道写端以便读者看到文件结束
  wait(0); // 等待左侧命令结束
  wait(0); // 等待右侧命令结束
  exit(0); // 管道父进程在子命令完成后结束
  //my code end
}

int
main(void)
{
  //my code begin
  static char line[MAXLINE]; // 为每行输入提供固定大小的全局缓冲区
  int fd; // 保存打开控制台时返回的文件描述符

  while((fd = open("console", O_RDWR)) >= 0){ // 确保标准输入、输出和错误描述符都已打开
    if(fd >= 3){ // 已经拥有前三个标准描述符时
      close(fd); // 关闭多余的控制台描述符
      break; // 结束初始化循环
    }
  }
  while(1){ // 持续读取并执行用户输入的命令行
    fprintf(2, "@ "); // 将提示符写入标准错误，避免干扰重定向输出
    memset(line, 0, sizeof(line)); // 清空上次命令留下的字符
    gets(line, sizeof(line)); // 将一行标准输入读入固定缓冲区
    if(line[0] == 0) // 标准输入结束时停止 shell
      break; // 退出读取循环
    int length = strlen(line); // 获取读入命令行的字符数量
    if(length > 0 && line[length - 1] == '\n') // 检查末尾是否为换行符
      line[length - 1] = 0; // 删除换行符，避免其成为参数或文件名的一部分
    if(line[0] == 0) // 空输入无需创建子进程
      continue; // 读取下一行
    int pid = fork(); // 为当前命令行创建独立执行进程
    if(pid < 0){ // 检查创建子进程是否失败
      fprintf(2, "nsh: fork failed\n"); // 向标准错误报告创建失败
      continue; // 继续读取后续命令
    }
    if(pid == 0){ // 子进程负责解析并执行命令行
      runline(line); // 执行命令、重定向或管道
    }
    wait(0); // 父 shell 等待本行命令全部执行完成
  }
  exit(0); // 输入结束后正常退出 shell
  //my code end
}
