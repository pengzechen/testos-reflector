/**
 * Minimal libc support for user programs
 * 
 * Provides a minimal runtime environment for programs using musl libc
 */

#include "t_types.h"
#include "lib/t_logger.h"

/**
 * Minimal __libc_start_main implementation
 * 
 * This is called by the _start code in dynamically linked programs.
 * We provide a minimal version that just calls main() and exits.
 */
int __testos_libc_start_main(
    int (*main)(int, char **, char **),
    int argc,
    char **argv,
    void (*init)(void),
    void (*fini)(void),
    void (*rtld_fini)(void),
    void *stack_end)
{
    (void)init;
    (void)fini;
    (void)rtld_fini;
    (void)stack_end;
    
    logger_info("__testos_libc_start_main: Starting user program\n");
    logger_info("  main = 0x%lx\n", (uint64_t)main);
    logger_info("  argc = %d\n", argc);
    
    // Call main
    int ret = main(argc, argv, NULL);
    
    logger_info("__testos_libc_start_main: main() returned %d\n", ret);
    
    return ret;
}

/**
 * 空的 init/fini 函数供 __libc_start_main 使用
 */
static void dummy_init(void)
{
    logger_info("dummy init called\n");
}

static void dummy_fini(void)
{
    logger_info("dummy fini called\n");
}

/**
 * 执行一个使用 libc 的用户程序
 * 
 * 这个函数准备好栈帧，然后跳转到程序的入口点（_start）
 */
int execute_libc_program(uint64_t entry_point, int (*main_func)(int, char **, char **))
{
    logger_info("Executing libc program at 0x%lx\n", entry_point);
    
    // 准备有效的参数（在静态存储区，确保指针有效）
    static char prog_name[] = "hello.elf";
    static char arg1[] = "arg1";
    static char arg2[] = "arg2";
    static char *argv_storage[] = {prog_name, arg1, arg2, NULL};
    static char *envp_storage[] = {NULL};
    
    int argc = 3;
    char **argv = argv_storage;
    char **envp = envp_storage;
    
    // 方案：手动调用 __init_libc 初始化 libc，然后直接调用 main
    
    // __init_libc 的签名：
    // void __init_libc(char **envp, char *pn)
    // 其中：
    //   envp = 环境变量指针数组（从 argv 末尾算起）
    //   pn   = 程序名（argv[0]）
    
    typedef void (*init_libc_t)(char **, char *);
    
    // libc.so 的 __init_libc 在 0x2528c
    init_libc_t init_libc = (init_libc_t)(0x80000000UL + 0x2528c);
    
    logger_info("Calling __init_libc at 0x%lx\n", (uint64_t)init_libc);
    logger_info("  envp = %p\n", envp);
    logger_info("  argv[0] = %s\n", argv[0]);
    
    // 调用 __init_libc 初始化
    init_libc(envp, argv[0]);
    
    logger_info("libc initialized, calling main at 0x%lx\n", (uint64_t)main_func);
    
    // 直接调用 main
    int ret = main_func(argc, argv, envp);
    
    logger_info("main() returned %d\n", ret);
    
    return ret;
}

/**
 * 提供一个简化的 TLS（线程局部存储）支持
 * 
 * musl libc 的某些函数需要 TLS 来存储 errno 等
 */
struct __testos_tls {
    void *dtv;          // Dynamic Thread Vector
    void *self;         // Pointer to itself
    int errno_val;      // errno storage
};

static struct __testos_tls g_tls = {0};

/**
 * libc 需要的一些全局变量
 * 
 * 这些变量在 libc 内部使用，我们需要提供它们
 */

// __libc 结构体（简化版）
struct __libc_t {
    int threaded;
    int secure;
    size_t *auxv;
    volatile int threads_minus_1;
    size_t tls_size;
    size_t page_size;
};

static struct __libc_t __libc_data = {
    .threaded = 0,
    .secure = 0,
    .auxv = NULL,
    .threads_minus_1 = 0,
    .tls_size = sizeof(struct __testos_tls),
    .page_size = 4096,
};

// 导出 __libc 符号（libc 内部会使用）
struct __libc_t *__libc = &__libc_data;

// __hwcap - 硬件能力标志
unsigned long __hwcap = 0;

// __sysinfo - 系统信息（VDSO 相关）
unsigned long __sysinfo = 0;

void __testos_init_tls(void)
{
    g_tls.self = &g_tls;
    g_tls.errno_val = 0;
    
    // 设置 TPIDR_EL0 (Thread Pointer)
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(&g_tls));
    
    logger_info("TLS initialized at %p\n", &g_tls);
}

/**
 * 获取当前的 errno 指针
 */
int *__errno_location(void)
{
    return &g_tls.errno_val;
}
