/**
 * Minimal libc support for user programs
 * 
 * Provides a minimal runtime environment for programs using musl libc
 */

#include "t_types.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"

/**
 * 执行一个使用 libc 的用户程序
 * 
 * 这个函数准备好栈帧，然后跳转到程序的入口点（_start）
 * https://www.cnblogs.com/peifx/p/18527739
 */
int
execute_libc_program(uint64_t entry_point, int (*main_func)(int, char **, char **))
{
    // 准备有效的参数（在静态存储区，确保指针有效）
    static char  prog_name[]    = "hello.elf";
    static char  arg1[]         = "arg1";
    static char  arg2[]         = "arg2";
    static char *argv_storage[] = {prog_name, arg1, arg2, NULL};

    /* 环境变量（至少放一个空字符串，musl 期望 envp 不为 NULL） */
    static char env0[] = "";
    /* 为 AT_RANDOM 提供 16 字节缓冲区（musl 使用这个作为安全随机种子） */
    static unsigned char random_seed[16] =
        {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0, 0, 0, 0, 0, 0, 0, 0};

    static void *env_and_aux[] = {
        /* envp: 环境字符串指针列表 */
        env0,
        /* envp 的终结 NULL - __init_libc 会用它来定位 auxv */
        NULL,

        /* 接下来开始放 auxv 的 (type, value) 成对项 (以 size_t 单元存储) */
        /* AT_PAGESZ = 6, 值例如 4096 */
        (void *) (uint64_t) 6,
        (void *) (uint64_t) 4096,

        /* AT_RANDOM = 25, value = 指向 16 字节随机缓冲区的指针 */
        (void *) (uint64_t) 25,
        (void *) (uint64_t) random_seed,

        /* 其它条目可以按需加入，例如 AT_EXECFN(31) 指向程序名:
        (void*)(uint64_t)31, (void*)(uint64_t)prog_name,
        … */
        // (void*)(uint64_t)3,       (void*)(uint64_t)0, // AT_PHDR
        // (void*)(uint64_t)4,       (void*)(uint64_t)0, // AT_PHENT
        // (void*)(uint64_t)5,       (void*)(uint64_t)0, // AT_PHNUM

        /* AT_NULL 结束 */
        (void *) (uint64_t) 0,
        (void *) (uint64_t) 0};

    /* 使用方式 */
    char **argv = argv_storage;
    /* 这里把 envp 指向 env_and_aux 开头，__init_libc 会在 NULL 后找到 auxv */
    char **envp = (char **) env_and_aux;

    int        argc    = 3;
    static int init_ok = 0;


    if (init_ok) {
        logger_info("  envp = %p\n", envp);
        logger_info("  argv[0] = %s\n", argv[0]);
    }


    if (!init_ok) {
        // 方案：手动调用 __init_libc 初始化 libc，然后直接调用 main
        typedef void (*init_libc_t)(char **, char *);

        // libc.so 的 __init_libc 在 0x2528c
        init_libc_t init_libc = (init_libc_t) (0x80000000UL + 0x2528c);
        logger_info("Calling __init_libc at 0x%lx\n", (uint64_t) init_libc);

        // 调用 __init_libc 初始化
        init_libc(envp, argv[0]);
    }


    /*
        typedef void (*libc_start_init_t)(void);
        libc_start_init_t libc_start_init = (libc_start_init_t) (0x80000000UL + 0x642f4);
        logger_info("Calling __libc_start_init at 0x%lx\n", (uint64_t) libc_start_init);
        (void)libc_start_init;
    */

    if (!main_func) {
        logger("main_func is NULL, init clib only!\n");
        init_ok = 1;
        return 0;
    }

    logger_info("libc initialized, calling main at 0x%lx\n", (uint64_t) main_func);

    // 直接调用 main
    int ret = main_func(argc, argv, envp);

    logger_info("main() returned %d\n", ret);

    return ret;
}

#define LOCALE_NAME_MAX 23
struct __locale_map {
	const void *map;
	size_t map_size;
	char name[LOCALE_NAME_MAX+1];
	const struct __locale_map *next;
};
struct __locale_struct {
	const struct __locale_map *cat[6];
};
static struct __locale_map g_dummy_locale_map = {
    .map = NULL,
    .map_size = 0,
    .name = "C",
    .next = NULL
};
enum { LC_CTYPE = 0, LC_NUMERIC, LC_TIME, LC_COLLATE, LC_MONETARY, LC_MESSAGES };


/**
 * 提供一个简化的 TLS（线程局部存储）支持
 * 
 * musl libc 的某些函数需要 TLS 来存储 errno 等
 */
struct __testos_tls
{
    void *dtv;        // Dynamic Thread Vector
    void *self;       // Pointer to itself
    int   errno_val;  // errno storage
    struct __locale_struct locale;
    uint32_t padding1;
    uint64_t padding[4];
};

static struct __testos_tls g_tls[2] = {0};

void
__testos_init_tls(void)
{
    struct __testos_tls *tls = &g_tls[1];

    memset(g_tls, 0, sizeof(*g_tls));

    g_tls->self = g_tls;
    g_tls->errno_val = 0;
    g_tls->locale.cat[0] = &g_dummy_locale_map;
    g_tls->locale.cat[1] = &g_dummy_locale_map;
    g_tls->locale.cat[2] = &g_dummy_locale_map;
    g_tls->locale.cat[3] = &g_dummy_locale_map;
    g_tls->locale.cat[4] = &g_dummy_locale_map;
    g_tls->locale.cat[5] = &g_dummy_locale_map;

    // 设置 TPIDR_EL0 (Thread Pointer)
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(tls));

    logger_info("TLS initialized at %p\n", tls);
}

/* 读取 TP（TPIDR_EL0） */
static inline void *
read_tp(void)
{
    void *tp;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
    return tp;
}

/* resolver: 参数是 descriptor 指针（在调用点 x0） */
void *
__tls_get_addr(void *desc)
{
    uint64_t arg = ((uint64_t *) desc)[1]; /* 第二字：偏移 */
    void    *tp  = read_tp();

    /* 返回 TP + offset */
    return (void *) ((char *) tp + (size_t) arg);
}

/**
 * 获取当前的 errno 指针
 */
int *
__errno_location(void)
{
    return &g_tls[1].errno_val;
}
