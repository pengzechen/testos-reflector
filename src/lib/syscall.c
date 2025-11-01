#include "lib/syscall.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "dev/t_dw_uart.h"
#include "dev/t_timer.h"
#include "npu/rkmem.h"
#include "t_exception.h"

/* Current program break */
static uint64_t current_brk = 0x10000000; /* Start heap at 256MB */
static uint64_t brk_start = 0x10000000;

/**
 * sys_write - Write to a file descriptor
 * In our simple implementation, we only support stdout/stderr
 */
int64_t sys_write(int fd, const void *buf, size_t count)
{
    /* Only support stdout and stderr */
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -EBADF;
    }

    /* Write to UART */
    const char *str = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        dw_uart_putchar(str[i]);
    }

    return (int64_t)count;
}

/**
 * sys_read - Read from a file descriptor
 * In our simple implementation, we only support stdin
 */
int64_t sys_read(int fd, void *buf, size_t count)
{
    /* Only support stdin */
    if (fd != STDIN_FILENO) {
        return -EBADF;
    }

    char *cbuf = (char *)buf;
    size_t i = 0;

    /* Read characters (blocking) */
    while (i < count) {
        char c;
        if (dw_uart_getchar_nb(&c)) {
            cbuf[i++] = c;
            /* Echo character */
            dw_uart_putchar(c);
            /* Stop at newline */
            if (c == '\n') {
                break;
            }
        }
    }

    return (int64_t)i;
}

/**
 * sys_exit - Terminate the current process
 */
int64_t sys_exit(int status)
{
    logger_info("Process exited with status: %d\n", status);
    
    /* In a real OS, we would clean up process resources and schedule next process.
     * For now, just hang in a loop. */
    while (1) {
        WFI();
    }
    
    return 0; /* Never reached */
}

/**
 * sys_exit_group - Terminate all threads in a process group
 */
int64_t sys_exit_group(int status)
{
    /* For now, just call sys_exit */
    return sys_exit(status);
}

/**
 * sys_brk - Change the program break (heap boundary)
 */
int64_t sys_brk(void *addr)
{
    uint64_t new_brk = (uint64_t)addr;

    /* If addr is 0, return current break */
    if (new_brk == 0) {
        return (int64_t)current_brk;
    }

    /* Don't allow shrinking below initial brk */
    if (new_brk < brk_start) {
        return (int64_t)current_brk;
    }

    /* Update program break */
    current_brk = new_brk;
    
    logger_debug("brk: new break at 0x%lx\n", current_brk);
    
    return (int64_t)current_brk;
}

/**
 * sys_mmap - Map memory
 */
int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)prot;
    (void)fd;
    (void)offset;

    /* Only support anonymous mappings for now */
    if (!(flags & MAP_ANONYMOUS)) {
        logger_error("mmap: only anonymous mappings supported\n");
        return -EINVAL;
    }

    /* Allocate memory */
    void *mem = rkmem_alloc(length);
    if (mem == NULL) {
        logger_error("mmap: allocation failed\n");
        return -ENOMEM;
    }

    /* Zero the memory */
    my_memset(mem, 0, length);

    logger_debug("mmap: allocated %lu bytes at %p\n", length, mem);

    return (int64_t)mem;
}

/**
 * sys_munmap - Unmap memory
 */
int64_t sys_munmap(void *addr, size_t length)
{
    (void)addr;
    (void)length;
    
    /* In our simple implementation, we don't free memory.
     * A real implementation would call rkmem_free or equivalent. */
    logger_debug("munmap: %p (length %lu) - not implemented\n", addr, length);
    
    return 0;
}

/**
 * sys_getpid - Get process ID
 */
int64_t sys_getpid(void)
{
    return 1; /* Always return 1 for now */
}

/**
 * sys_gettid - Get thread ID
 */
int64_t sys_gettid(void)
{
    return 1; /* Always return 1 for now */
}

/**
 * sys_getuid - Get user ID
 */
int64_t sys_getuid(void)
{
    return 0; /* Always return 0 (root) */
}

/**
 * sys_getgid - Get group ID
 */
int64_t sys_getgid(void)
{
    return 0; /* Always return 0 (root) */
}

/**
 * sys_geteuid - Get effective user ID
 */
int64_t sys_geteuid(void)
{
    return 0; /* Always return 0 (root) */
}

/**
 * sys_getegid - Get effective group ID
 */
int64_t sys_getegid(void)
{
    return 0; /* Always return 0 (root) */
}

/**
 * sys_getppid - Get parent process ID
 */
int64_t sys_getppid(void)
{
    return 0; /* No parent */
}

/**
 * sys_clock_gettime - Get time
 */
int64_t sys_clock_gettime(int clk_id, struct timespec *tp)
{
    (void)clk_id;
    
    if (tp == NULL) {
        return -EFAULT;
    }

    /* Get uptime in milliseconds */
    uint64_t uptime_ms = timer_get_uptime_ms();
    
    tp->tv_sec = uptime_ms / 1000;
    tp->tv_nsec = (uptime_ms % 1000) * 1000000;

    return 0;
}

/**
 * sys_uname - Get system information
 */
int64_t sys_uname(struct utsname *buf)
{
    if (buf == NULL) {
        return -EFAULT;
    }

    my_strncpy(buf->sysname, "TestOS", 65);
    my_strncpy(buf->nodename, "testos", 65);
    my_strncpy(buf->release, "1.0.0", 65);
    my_strncpy(buf->version, __DATE__ " " __TIME__, 65);
    my_strncpy(buf->machine, "aarch64", 65);
    my_strncpy(buf->domainname, "(none)", 65);

    return 0;
}

/**
 * sys_set_tid_address - Set pointer to thread ID
 */
int64_t sys_set_tid_address(int *tidptr)
{
    (void)tidptr;
    /* Just return thread ID */
    return 1;
}

/**
 * Syscall table - maps syscall numbers to handler functions
 */
typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int64_t syscall_stub(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -ENOSYS;
}

#define MAX_SYSCALL 282
static syscall_fn_t syscall_table[MAX_SYSCALL];

/**
 * Initialize syscall table
 */
static void init_syscall_table(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Initialize all to stub */
    for (int i = 0; i < MAX_SYSCALL; i++) {
        syscall_table[i] = syscall_stub;
    }

    /* Register implemented syscalls */
    syscall_table[SYS_read] = (syscall_fn_t)sys_read;
    syscall_table[SYS_write] = (syscall_fn_t)sys_write;
    syscall_table[SYS_exit] = (syscall_fn_t)sys_exit;
    syscall_table[SYS_exit_group] = (syscall_fn_t)sys_exit_group;
    syscall_table[SYS_brk] = (syscall_fn_t)sys_brk;
    syscall_table[SYS_mmap] = (syscall_fn_t)sys_mmap;
    syscall_table[SYS_munmap] = (syscall_fn_t)sys_munmap;
    syscall_table[SYS_getpid] = (syscall_fn_t)sys_getpid;
    syscall_table[SYS_gettid] = (syscall_fn_t)sys_gettid;
    syscall_table[SYS_getuid] = (syscall_fn_t)sys_getuid;
    syscall_table[SYS_getgid] = (syscall_fn_t)sys_getgid;
    syscall_table[SYS_geteuid] = (syscall_fn_t)sys_geteuid;
    syscall_table[SYS_getegid] = (syscall_fn_t)sys_getegid;
    syscall_table[SYS_getppid] = (syscall_fn_t)sys_getppid;
    syscall_table[SYS_clock_gettime] = (syscall_fn_t)sys_clock_gettime;
    syscall_table[SYS_uname] = (syscall_fn_t)sys_uname;
    syscall_table[SYS_set_tid_address] = (syscall_fn_t)sys_set_tid_address;
}

/**
 * handle_syscall_exception - Main syscall handler
 * Called from exception handler with saved register context
 */
void handle_syscall_exception(uint64_t *stack_pointer)
{
    trap_frame_t *ctx = (trap_frame_t *)stack_pointer;

    /* Initialize syscall table on first call */
    init_syscall_table();

    /* Get syscall number from x8 (AArch64 ABI) */
    uint64_t syscall_num = ctx->r[8];

    /* Get arguments from x0-x5 */
    uint64_t arg0 = ctx->r[0];
    uint64_t arg1 = ctx->r[1];
    uint64_t arg2 = ctx->r[2];
    uint64_t arg3 = ctx->r[3];
    uint64_t arg4 = ctx->r[4];
    uint64_t arg5 = ctx->r[5];

    int64_t result;

    /* Check syscall number */
    if (syscall_num >= MAX_SYSCALL) {
        logger_warn("Invalid syscall number: %lu\n", syscall_num);
        result = -ENOSYS;
    } else {
        /* Call syscall handler */
        logger_debug("Syscall %lu (args: 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
                    syscall_num, arg0, arg1, arg2, arg3, arg4, arg5);
        
        result = syscall_table[syscall_num](arg0, arg1, arg2, arg3, arg4, arg5);
        
        logger_debug("Syscall %lu returned: %ld\n", syscall_num, result);
    }

    /* Store result in x0 */
    ctx->r[0] = (uint64_t)result;

    /* Advance PC past SVC instruction (4 bytes) */
    ctx->elr += 4;
}
