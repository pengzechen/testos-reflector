/**
 * Simple user program for testing system calls
 * 不使用标准库函数，直接调用系统调用
 */

// 系统调用号
#define SYS_write 64
#define SYS_exit  93

// 系统调用封装
static long syscall3(long n, long a, long b, long c) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );
    
    return x0;
}

static long syscall1(long n, long a) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory"
    );
    
    return x0;
}

// write 系统调用
static long write(int fd, const char *buf, unsigned long count) {
    return syscall3(SYS_write, fd, (long)buf, count);
}

// exit 系统调用
static void exit(int status) {
    syscall1(SYS_exit, status);
    while(1);  // 永远不应该执行到这里
}

// 简单的字符串长度计算
static unsigned long strlen(const char *s) {
    unsigned long len = 0;
    while (s[len]) len++;
    return len;
}

// main 函数
int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    
    const char *msg1 = "Hello from TestOS user program!\n";
    const char *msg2 = "System calls are working!\n";
    const char *msg3 = "This is a minimal C program.\n";
    
    write(1, msg1, strlen(msg1));
    write(1, msg2, strlen(msg2));
    write(1, msg3, strlen(msg3));
    
    return 0;
}
