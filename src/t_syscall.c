/**
 * System Call Implementation
 */

#include "t_syscall.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "dev/t_dw_uart.h"
#include "mem/t_mem.h"
#include "mem/cache.h"

// 定义用户程序的堆区域
// 为每个 ELF 预留独立的堆空间

#define HELLO_HEAP_START 0x85100000UL  // hello.elf 的堆起始地址
#define HELLO_HEAP_END   0x86000000UL

#define SIMPLE_HEAP_START 0x86100000UL  // simple.elf 的堆起始地址
#define SIMPLE_HEAP_END   0x87000000UL

static uint64_t hello_brk_current  = HELLO_HEAP_START;
static uint64_t __attribute__((unused))  simple_brk_current = SIMPLE_HEAP_START;


static int64_t
sys_write(int fd, const char *buf, size_t count)
{
    // 简单实现：只支持 stdout 和 stderr，直接写到串口
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        logger_error("[SYSCALL] sys_write: invalid fd=%d\n", fd);
        return -9;  // -EBADF (Bad file descriptor)
    }

    // 写入串口（不打印调试信息，避免干扰用户输出）
    for (size_t i = 0; i < count; i++) {
        dw_uart_putchar(buf[i]);
    }

    return (int64_t) count;
}

/**
 * sys_writev - write vector (multiple buffers)
 */
static int64_t
sys_writev(int fd, const void *iov, int iovcnt)
{
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -9;  // -EBADF
    }

    // iovec 结构: {void *iov_base; size_t iov_len;}
    struct
    {
        void  *iov_base;
        size_t iov_len;
    } *iovec = (void *) iov;

    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        const char *buf = (const char *) iovec[i].iov_base;
        size_t      len = iovec[i].iov_len;

        // 检查地址有效性
        uint64_t addr = (uint64_t) buf;
        if (len > 0 && (addr < 0x400000 || addr > 0x90000000)) {
            logger_error("[SYSCALL] writev: INVALID ADDRESS %p (len=%zu), skipping\n", buf, len);
            continue;  // 跳过无效地址
        }

        // 直接输出用户数据，不打印调试信息（避免干扰输出）
        for (size_t j = 0; j < len; j++) {
            dw_uart_putchar(buf[j]);
        }
        total += len;
    }

    //0x81103000
    clean_dcache_va_range((const void *) HELLO_HEAP_START, hello_brk_current - HELLO_HEAP_START);
    invalidate_dcache_va_range((const void *) HELLO_HEAP_START,
                               hello_brk_current - HELLO_HEAP_START);

    return total;
}

/**
 * sys_ioctl - I/O control operations
 */
static int64_t
sys_ioctl(int fd, unsigned long request, unsigned long arg)
{
    // 简化实现：大多数 ioctl 调用可以安全地返回成功
    (void) fd;
    (void) request;
    (void) arg;
    return 0;  // 假装成功
}

/**
 * sys_read - read from file descriptor
 */
static int64_t
sys_read(int fd, char *buf, size_t count)
{
    logger_info("[SYSCALL] sys_read: fd=%d, buf=%p, count=%zu\n", fd, buf, count);

    if (fd != STDIN_FILENO) {
        return -9;  // -EBADF
    }

    // 简单实现：读取一个字符
    for (size_t i = 0; i < count; i++) {
        buf[i] = dw_uart_getchar();
    }

    logger_info("[SYSCALL] sys_read: read %zu bytes\n", count);
    return (int64_t) count;
}

/**
 * sys_brk - change data segment size (for malloc)
 */
static int64_t
sys_brk(void *addr)
{
    uint64_t requested_addr = (uint64_t) addr;

    logger_info("[SYSCALL] sys_brk: addr=%p\n", addr);

    // 简化：总是使用 hello 的堆（因为现在主要是测试 hello.elf）
    uint64_t *current_brk = &hello_brk_current;
    uint64_t  heap_start  = HELLO_HEAP_START;
    uint64_t  heap_end    = HELLO_HEAP_END;

    // brk(NULL) - 返回当前的 brk 值
    if (requested_addr == 0) {
        logger_info("[SYSCALL] sys_brk(NULL) -> %p\n", (void *) *current_brk);
        return *current_brk;
    }

    // brk(addr) - 设置新的 brk 值
    // 检查地址是否在合法范围内
    if (requested_addr < heap_start || requested_addr > heap_end) {
        logger_warn("[SYSCALL] sys_brk: addr %p out of range [%p, %p], returning current\n",
                    (void *) requested_addr,
                    (void *) heap_start,
                    (void *) heap_end);
        return *current_brk;  // 返回旧值表示失败
    }

    logger_info("[SYSCALL] sys_brk: %p -> %p (size: %d KB)\n",
                (void *) *current_brk,
                (void *) requested_addr,
                (requested_addr - heap_start) / 1024);

    *current_brk = requested_addr;
    return requested_addr;
}

/**
 * sys_exit - terminate process
 */
static void
sys_exit(int status)
{
    logger_info("[SYSCALL] sys_exit: status=%d\n", status);
    // 简单实现：直接返回到内核主循环
    // TODO: 清理进程资源
}

/**
 * sys_rt_sigprocmask - change signal mask
 * 简化实现：我们不支持信号，但返回成功以避免 libc 崩溃
 */
static int64_t
sys_rt_sigprocmask(int how, const void *set, void *oldset, size_t sigsetsize)
{
    logger_info("[SYSCALL] sys_rt_sigprocmask: how=%d, set=%p, oldset=%p, sigsetsize=%zu\n",
                how,
                set,
                oldset,
                sigsetsize);

    (void) how;
    (void) set;
    (void) sigsetsize;

    // 如果请求返回旧的信号掩码，就返回空掩码（所有信号都不被阻塞）
    if (oldset != NULL && sigsetsize >= 8) {
        uint64_t *mask = (uint64_t *) oldset;
        *mask          = 0;  // 空掩码
    }

    logger_info("[SYSCALL] sys_rt_sigprocmask: returning success (signals not supported)\n");
    return 0;  // 成功
}

/**
 * sys_getitimer - get interval timer value
 * 简化实现：返回全零的定时器值
 */
static int64_t
sys_getitimer(int which, void *curr_value)
{
    (void) which;

    logger_info("[SYSCALL] sys_getitimer: which=%d, curr_value=%p\n", which, curr_value);

    // 如果 curr_value 是 NULL，直接返回成功
    // 这是合法的调用，调用者可能只是检查系统调用是否支持
    if (curr_value == NULL) {
        logger_info("[SYSCALL] sys_getitimer: curr_value is NULL, returning success\n");
        return 0;  // 成功
    }

    // struct itimerval { struct timeval it_interval; struct timeval it_value; }
    // struct timeval { long tv_sec; long tv_usec; }
    // 总共 4 个 long (32 bytes on 64-bit)
    uint64_t *p = (uint64_t *) curr_value;
    p[0]        = 0;  // it_interval.tv_sec
    p[1]        = 0;  // it_interval.tv_usec
    p[2]        = 0;  // it_value.tv_sec
    p[3]        = 0;  // it_value.tv_usec

    logger_info("[SYSCALL] sys_getitimer: returning zero timer\n");
    return 0;
}


// mmap 标志定义
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *) -1)
#define EINVAL        22


// 简单的 mmap 区域释放标记（仅支持最近一次分配的回退，适合 musl 的大块释放）
static int64_t
sys_munmap(void *addr, size_t length)
{
    logger_info("[SYSCALL] sys_munmap: addr=%p, length=%zu (noop)\n", addr, length);
    // 幂等假释放，不做任何实际内存回退，兼容 musl free 行为
    return 0;
}


static int64_t
sys_mmap(void *addr, size_t length, int prot, int flags, int fd, int64_t offset)
{
    logger_info(
        "[SYSCALL] sys_mmap: addr=%p, length=%zu, prot=0x%x, flags=0x%x, fd=%d, offset=%ld\n",
        addr,
        length,
        prot,
        flags,
        fd,
        offset);

    (void) prot;
    (void) fd;
    (void) offset;

    // mmap 直接用 brk 机制分配，和 sys_brk 共享同一堆区
    if (!(flags & MAP_ANONYMOUS)) {
        logger_error("[SYSCALL] sys_mmap: only MAP_ANONYMOUS supported\n");
        return (int64_t) MAP_FAILED;
    }

    if (length == 0) {
        logger_warn("[SYSCALL] sys_mmap: length=0, allocating one page (4KB)\n");
        length = 4096;
    }

    size_t   aligned_length = (length + 0xFFF) & ~0xFFF;
    uint64_t heap_end       = HELLO_HEAP_END;
    if (hello_brk_current + aligned_length > heap_end) {
        logger_error("[SYSCALL] sys_mmap: out of memory (requested %zu bytes)\n", length);
        return (int64_t) MAP_FAILED;
    }
    void *alloc_addr = (void *) hello_brk_current;
    memset(alloc_addr, 0, aligned_length);
    hello_brk_current += aligned_length;
    logger_info("[SYSCALL] sys_mmap: allocated %p (aligned %zu bytes)\n",
                alloc_addr,
                aligned_length);
    return (int64_t) alloc_addr;
}

/**
 * System call dispatcher
 */
uint64_t
handle_syscall(uint64_t syscall_num,
               uint64_t arg0,
               uint64_t arg1,
               uint64_t arg2,
               uint64_t arg3,
               uint64_t arg4,
               uint64_t arg5)
{
    int64_t ret = 0;

    // 注释掉系统调用日志，避免干扰用户程序输出
    // logger_info("[SYSCALL] #%llu called\n", syscall_num);

    switch (syscall_num) {
        case SYS_ioctl:
            ret = sys_ioctl((int) arg0, (unsigned long) arg1, (unsigned long) arg2);
            break;

        case SYS_write:
            ret = sys_write((int) arg0, (const char *) arg1, (size_t) arg2);
            break;

        case SYS_writev:
            ret = sys_writev((int) arg0, (const void *) arg1, (int) arg2);
            break;

        case SYS_read:
            ret = sys_read((int) arg0, (char *) arg1, (size_t) arg2);
            break;

        case SYS_brk:
            ret = sys_brk((void *) arg0);
            break;

        case SYS_mmap:
            ret = sys_mmap((void *) arg0,
                           (size_t) arg1,
                           (int) arg2,
                           (int) arg3,
                           (int) arg4,
                           (int64_t) arg5);
            break;


        case SYS_munmap:
            ret = sys_munmap((void *) arg0, (size_t) arg1);
            break;
        case SYS_tkill:
            // 简化实现：忽略 kill 调用，返回成功
            ret = 0;
            break;
        case SYS_rt_sigaction:
            // 简化实现：忽略信号处理设置，返回成功
            ret = 0;
            break;
        case SYS_rt_sigprocmask:
            ret = sys_rt_sigprocmask((int) arg0, (const void *) arg1, (void *) arg2, (size_t) arg3);
            break;

        case SYS_getitimer:
            ret = sys_getitimer((int) arg0, (void *) arg1);
            break;

        case SYS_exit:
        case SYS_exit_group:
            sys_exit((int) arg0);
            ret = 0;
            break;

        default:
            logger_warn("[SYSCALL] Unknown syscall: %llu (args: %lx, %lx, %lx)\n",
                        syscall_num,
                        arg0,
                        arg1,
                        arg2);
            ret = -38;  // -ENOSYS (Function not implemented)
            break;
    }

    // 注释掉返回值日志，避免干扰用户程序输出
    // logger_info("[SYSCALL] #%llu returned %ld\n", syscall_num, ret);

    return (uint64_t) ret;
}
