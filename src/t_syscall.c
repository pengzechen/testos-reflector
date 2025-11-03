/**
 * System Call Implementation
 */

#include "t_syscall.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "dev/t_dw_uart.h"
#include "mem/t_mem.h"

/**
 * sys_write - write to file descriptor
 * @fd: file descriptor (0=stdin, 1=stdout, 2=stderr)
 * @buf: buffer to write from
 * @count: number of bytes to write
 * @return: number of bytes written, or negative error code
 */
static int64_t sys_write(int fd, const char *buf, size_t count)
{
    // 简单实现：只支持 stdout 和 stderr，直接写到串口
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -9; // -EBADF (Bad file descriptor)
    }

    // 写入串口
    for (size_t i = 0; i < count; i++) {
        dw_uart_putchar(buf[i]);
    }

    return (int64_t)count;
}

/**
 * sys_writev - write vector (multiple buffers)
 */
static int64_t sys_writev(int fd, const void *iov, int iovcnt)
{
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -9; // -EBADF
    }

    // iovec 结构: {void *iov_base; size_t iov_len;}
    struct {
        void *iov_base;
        size_t iov_len;
    } *iovec = (void *)iov;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        const char *buf = (const char *)iovec[i].iov_base;
        size_t len = iovec[i].iov_len;
        
        for (size_t j = 0; j < len; j++) {
            dw_uart_putchar(buf[j]);
        }
        total += len;
    }

    return total;
}

/**
 * sys_ioctl - I/O control operations
 */
static int64_t sys_ioctl(int fd, unsigned long request, unsigned long arg)
{
    (void)fd;
    (void)request;
    (void)arg;
    
    // 简化实现：大多数 ioctl 调用可以安全地返回成功
    // 常见的 ioctl：TCGETS (0x5401), TIOCGWINSZ (0x5413) 等
    logger_info("sys_ioctl: fd=%d, request=0x%lx (ignored)\n", fd, request);
    return 0;  // 假装成功
}

/**
 * sys_read - read from file descriptor
 */
static int64_t sys_read(int fd, char *buf, size_t count)
{
    if (fd != STDIN_FILENO) {
        return -9; // -EBADF
    }

    // 简单实现：读取一个字符
    for (size_t i = 0; i < count; i++) {
        buf[i] = dw_uart_getchar();
    }

    return (int64_t)count;
}

/**
 * sys_brk - change data segment size (for malloc)
 */

// 定义用户程序的堆区域
// 为每个 ELF 预留独立的堆空间
#define HELLO_HEAP_START  0x81100000UL  // hello.elf 的堆起始地址（紧跟代码段后）
#define HELLO_HEAP_END    0x82000000UL  // 堆结束地址（下一个 ELF 前）
#define SIMPLE_HEAP_START 0x82100000UL  // simple.elf 的堆起始地址
#define SIMPLE_HEAP_END   0x83000000UL

static uint64_t hello_brk_current = HELLO_HEAP_START;
static uint64_t simple_brk_current = SIMPLE_HEAP_START;

static int64_t sys_brk(void *addr)
{
    uint64_t requested_addr = (uint64_t)addr;
    
    // 简化：总是使用 hello 的堆（因为现在主要是测试 hello.elf）
    uint64_t *current_brk = &hello_brk_current;
    uint64_t heap_start = HELLO_HEAP_START;
    uint64_t heap_end = HELLO_HEAP_END;
    
    // brk(NULL) - 返回当前的 brk 值
    if (requested_addr == 0) {
        logger_info("sys_brk(NULL) -> %p\n", (void*)*current_brk);
        return *current_brk;
    }
    
    // brk(addr) - 设置新的 brk 值
    // 检查地址是否在合法范围内
    if (requested_addr < heap_start || requested_addr > heap_end) {
        logger_warn("sys_brk: addr %p out of range [%p, %p], returning current\n",
                    (void*)requested_addr, (void*)heap_start, (void*)heap_end);
        return *current_brk; // 返回旧值表示失败
    }
    
    logger_info("sys_brk: %p -> %p (size: %lu KB)\n",
                (void*)*current_brk, (void*)requested_addr,
                (requested_addr - heap_start) / 1024);
    
    *current_brk = requested_addr;
    return requested_addr;
}

/**
 * sys_exit - terminate process
 */
static void sys_exit(int status)
{
    logger_info("Process exited with status: %d\n", status);
    // 简单实现：直接返回到内核主循环
    // TODO: 清理进程资源
}

/**
 * sys_mmap - map memory into address space
 * 
 * mmap 参数：
 *   addr   - 建议的映射地址（NULL = 让内核选择）
 *   length - 映射长度
 *   prot   - 保护标志（PROT_READ|PROT_WRITE|PROT_EXEC）
 *   flags  - 映射标志（MAP_PRIVATE|MAP_ANONYMOUS 等）
 *   fd     - 文件描述符（MAP_ANONYMOUS 时忽略）
 *   offset - 文件偏移（MAP_ANONYMOUS 时忽略）
 */

// mmap 标志定义
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void*)-1)

// 为 mmap 分配的内存区域（在堆后面）
#define HELLO_MMAP_START  0x82000000UL
#define HELLO_MMAP_END    0x84000000UL
#define SIMPLE_MMAP_START 0x83000000UL
#define SIMPLE_MMAP_END   0x85000000UL

static uint64_t hello_mmap_current = HELLO_MMAP_START;
static uint64_t simple_mmap_current = SIMPLE_MMAP_START;

static int64_t sys_mmap(void *addr, size_t length, int prot, int flags,
                       int fd, int64_t offset)
{
    (void)prot;
    (void)fd;
    (void)offset;
    
    // 简化：总是使用 hello 的 mmap 区域
    uint64_t *current_mmap = &hello_mmap_current;
    uint64_t mmap_start = HELLO_MMAP_START;
    uint64_t mmap_end = HELLO_MMAP_END;
    
    // 简化实现：只支持匿名映射
    if (!(flags & MAP_ANONYMOUS)) {
        logger_error("sys_mmap: only MAP_ANONYMOUS supported\n");
        return (int64_t)MAP_FAILED;
    }
    
    // 对齐到页边界（4KB）
    size_t aligned_length = (length + 0xFFF) & ~0xFFF;
    
    // 如果指定了地址，尝试使用它
    uint64_t alloc_addr;
    if (addr != NULL && (uint64_t)addr >= mmap_start && (uint64_t)addr < mmap_end) {
        alloc_addr = (uint64_t)addr;
    } else {
        // 否则从当前位置分配
        alloc_addr = *current_mmap;
    }
    
    // 检查是否有足够空间
    if (alloc_addr + aligned_length > mmap_end) {
        logger_error("sys_mmap: out of memory (requested %zu bytes)\n", length);
        return (int64_t)MAP_FAILED;
    }
    
    // 清零内存（MAP_ANONYMOUS 要求）
    memset((void*)alloc_addr, 0, aligned_length);
    
    // 更新当前位置
    *current_mmap = alloc_addr + aligned_length;
    
    logger_info("sys_mmap: addr=%p, length=%zu -> %p (aligned to %zu bytes)\n",
                addr, length, (void*)alloc_addr, aligned_length);
    
    return (int64_t)alloc_addr;
}

/**
 * System call dispatcher
 */
uint64_t handle_syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, 
                        uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    (void)arg3;
    (void)arg4;
    (void)arg5;

    int64_t ret = 0;

    switch (syscall_num) {
        case SYS_ioctl:
            ret = sys_ioctl((int)arg0, (unsigned long)arg1, (unsigned long)arg2);
            break;

        case SYS_write:
            ret = sys_write((int)arg0, (const char *)arg1, (size_t)arg2);
            break;

        case SYS_writev:
            ret = sys_writev((int)arg0, (const void *)arg1, (int)arg2);
            break;

        case SYS_read:
            ret = sys_read((int)arg0, (char *)arg1, (size_t)arg2);
            break;

        case SYS_brk:
            ret = sys_brk((void *)arg0);
            break;

        case SYS_mmap:
            ret = sys_mmap((void *)arg0, (size_t)arg1, (int)arg2, 
                          (int)arg3, (int)arg4, (int64_t)arg5);
            break;

        case SYS_exit:
        case SYS_exit_group:
            sys_exit((int)arg0);
            ret = 0;
            break;

        default:
            logger_warn("Unknown syscall: %llu\n", syscall_num);
            ret = -38; // -ENOSYS (Function not implemented)
            break;
    }

    return (uint64_t)ret;
}
