
#include "t_types.h"

#include "dev/t_gicv3.h"
#include "dev/t_dw_uart.h"
#include "dev/t_timer.h"
#include "npu/rknpu.h"
#include "dev/cru.h"
#include "dev/scmi.h"

#include "t_sysreg.h"


#include "lib/t_logger.h"
#include "lib/rand.h"
#include "npu/rkmem.h"

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


// ============================== 首核 ============================

// 主内核入口函数
void
t_kernel_main(uint64_t id)
{
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

    // dw_uart_init();

    timer_init();
    // timer_dump_info();

    // 启动多核
    start_secondary_cpus();
    
    {
        // 最多等待 5 秒让所有副核就绪；全部就绪则提前结束等待
        uint64_t freq        = timer_get_frequency();
        uint64_t start_ticks = CNTPCT_EL0_READ();
        uint64_t deadline    = start_ticks + 5ULL * freq;  // 5 秒超时

        int all_online = 0;
        while (CNTPCT_EL0_READ() < deadline) {
            all_online = 1;
            for (int i = 1; i < T_SMP_NUM; i++) {
                if (cpu_online[i] == 0) {
                    all_online = 0;
                    break;
                }
            }
            if (all_online)
                break;
            // 小幅让步，避免过度占用总线
            asm volatile("nop");
        }

        if (all_online) {
            logger_info("All %d secondary cores online within 5 seconds.\n", T_SMP_NUM - 1);
        } else {
            logger_warn("Timeout after 5 seconds: some secondary cores are not online.\n");
            for (int i = 1; i < T_SMP_NUM; i++) {
                if (cpu_online[i] == 0) {
                    logger_warn("  - core %d NOT online\n", i);
                }
            }
        }
    }


    // 启用定时器
    timer_enable();
    enable_interrupts();  // daifclr 2
    logger_info("After enabling interrupts\n");

    t_run_printf_tests();


    // 随机数模块测试
    srand_tick();

    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());

    // 申请内存测试
    size_t heap_size = (1 << 28);  // 1 G
    rkmem_init(heap_size);

    void *mem1 = rkmem_alloc(256 * 1024);  // 256 KB
    void *mem2 = rkmem_alloc(512 * 1024);  // 512 KB

    logger_info("Memory allocation test:\n");
    logger_info("  Allocated 256 KB at %p\n", mem1);
    logger_info("  Allocated 512 KB at %p\n", mem2);

    // scmi 时钟
    // todo fix.
    // enable_scmi_clock(6);

    // cru 时钟
    // enable_rk3588_npu_clocks();

    // RKNPU 初始化测试
    rknpu_init();

    // 测试
    rknpu_test();

    // reorder_test();

    while (1) {
        WFI();
    }
}
