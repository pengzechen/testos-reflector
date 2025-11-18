
#include "t_types.h"

#include "dev/t_gicv3.h"
#include "dev/t_dw_uart.h"
#include "dev/t_timer.h"
#include "dev/xmodem_dw_uart.h"
#include "dev/pcie_test.h"
#include "dev/cru.h"
#include "dev/scmi.h"
#include "dev/rtl8169.h"

#include "t_sysreg.h"


#include "lib/t_logger.h"
#include "lib/rand.h"
#include "mem/t_mem.h"

#include "t_psci.h"
#include "cfg/t_cfg.h"
#include "mem/cache.h"

extern void
__bss_start();
extern void
__bss_end();
extern void
__heap_flag();

extern void
start_secondary_cpus();

// ============================= 核心抽象 =========================
// ===============================================================

#define MAX_CPUS T_SMP_NUM

typedef struct
{
    void (*entry)(int cpu_id, void *arg);  // 核心执行函数
    void        *arg;                      // 参数指针
    volatile int start_flag;               // =1 表示任务可执行
    volatile int done_flag;                // =1 表示任务完成
} cpu_task_t;

static cpu_task_t cpu_tasks[MAX_CPUS];
volatile int      cpu_task_ready[MAX_CPUS];
volatile int      cpu_task_done[MAX_CPUS];

volatile int cpu_online[MAX_CPUS];


// 为某个核心设置任务。
void
launch_on_core(int cpu_id, void (*entry)(int, void *), void *arg)
{
    cpu_tasks[cpu_id].entry      = entry;
    cpu_tasks[cpu_id].arg        = arg;
    cpu_tasks[cpu_id].done_flag  = 0;
    cpu_tasks[cpu_id].start_flag = 1;  // 唤醒该核
}

// 主核调用之后，所有副核开始执行task。
void
wake_all_cores(int num)
{
    (void) num;
    asm volatile("sev");
}

// 主核调用 wakeup 之后需要等待其它副核执行完成。
void
wait_all_cores(int num)
{
    (void) num;
    // 记录每个核是否已经报告过完成
    int reported[MAX_CPUS] = {0};

    for (int i = 1; i < MAX_CPUS; i++) {
        while (cpu_tasks[i].done_flag == 0)
            ;  // busy wait

        if (!reported[i]) {
            reported[i] = 1;
            logger_info("core %d finished its task\n", i);
        }
    }
}

// 副核在汇编设置一些寄存器就会跳到这里。循环等待任务
void
t_secondary_main(uint64_t cpu_id)
{
    logger_info("second core id: %d\n", cpu_id);
    // logger_warn("second core CurrentEL = %u\n", READ_CURRENTEL());
    cpu_online[cpu_id] = 1;
    DSB_SY();
    while (1) {
        if (cpu_tasks[cpu_id].start_flag) {
            void (*fn)(int, void *)      = cpu_tasks[cpu_id].entry;
            void *arg                    = cpu_tasks[cpu_id].arg;
            cpu_tasks[cpu_id].start_flag = 0;

            if (fn)
                fn(cpu_id, arg);

            cpu_tasks[cpu_id].done_flag = 1;
            DSB_SY();
        }
        // logger_info("core %d wait for event\n");
        asm volatile("wfe" ::: "memory");
    }
}

// XMODEM 接收缓冲区（全局变量，用于在命令间共享）
#define XMODEM_BUF_SIZE (2 * 1024 * 1024)  // 2MB 缓冲区
static uint8_t *g_xmodem_buf    = NULL;
static ssize_t  g_last_received = 0;

// 主内核入口函数
void
t_kernel_main(uint64_t id)
{
    // ========== 阶段 1: 早期初始化（无中断） ==========
    // 先初始化早期串口，这样后续的 logger_info 就能工作
    // dw_uart_early_init();

    logger_info("Compiled on %s at %s\n", __DATE__, __TIME__);

    logger_info("bss start: %p, end: %p, size: %u KB\n",
                &__bss_start,
                &__bss_end,
                ((uint64_t) &__bss_end - (uint64_t) &__bss_start) / 1024);

    logger_info("heap flag address: %p\n", &__heap_flag);

    logger_warn("CurrentEL = %u\n", READ_CURRENTEL());

    logger_info("smp: %d\n", T_SMP_NUM);

    logger_info("main core id: %d\n", id);

    init_cpu_cacheline_size();

    gicv3_init();


    timer_init();

    // 启用定时器
    timer_enable();
    enable_interrupts();  // daifclr 2
    logger_info("After enabling interrupts\n");

    // 随机数模块测试
    srand_tick();

    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());

    // 申请内存测试
    size_t heap_size = (1 << 28);  // 1 G
    t_mem_init(heap_size);

    // PCIe 网络测试
    // logger_info("========================================\n");
    // logger_info("Starting PCIe Network Test\n");
    // logger_info("========================================\n");
    // pcie_network_test_main();
    // logger_info("PCIe Network Test completed\n");

    logger_info("Network Test\n");
    test_rtl8125();

    while (1) {
        WFI();
    }
}
