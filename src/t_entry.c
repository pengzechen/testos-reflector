
#include "t_types.h"

#include "dev/t_gicv3.h"
#include "dev/t_timer.h"
#include "dev/t_dw_uart.h"

#include "t_sysreg.h"

#include "lib/t_logger.h"

#include "cfg/t_cfg.h"
#include "mem/cache.h"
#include "task/t_task.h"

extern void
__bss_start();
extern void
__bss_end();
extern void
__heap_flag();

extern void
start_secondary_cpus();

volatile int cpu_online[T_SMP_NUM];

// 副核在汇编设置一些寄存器就会跳到这里，现在也运行调度器
void
t_secondary_main(uint64_t cpu_id)
{
    logger_info("secondary core %d started\n", cpu_id);
    cpu_online[cpu_id] = 1;
    DSB_SY();
    
    // 等待调度器启动
    while (!scheduler_started) {
        asm volatile("nop");
    }
    
    logger_info("secondary core %d starting scheduler\n", cpu_id);
    
    // 副核也运行调度器
    scheduler_start();
    
    // 不应该到达这里
    logger_error("secondary core %d scheduler returned\n", cpu_id);
    while (1) {
        WFI();
    }
}

// 测试任务 1
void test_task1(void *arg)
{
    (void)arg;
    logger_info("Task 1 started\n");
    
    for (int i = 0; i < 5; i++) {
        logger_info("Task 1: iteration %d\n", i);
        // 主动让出 CPU，演示 yield 功能
        task_yield();
    }
    
    logger_info("Task 1 finished\n");
}

// 测试任务 2
void test_task2(void *arg)
{
    (void)arg;
    logger_info("Task 2 started\n");
    
    for (int i = 0; i < 5; i++) {
        logger_info("Task 2: iteration %d\n", i);
        // 主动让出 CPU，演示 yield 功能
        task_yield();
    }
    
    logger_info("Task 2 finished\n");
}

// 测试任务 3
void test_task3(void *arg)
{
    (void)arg;
    logger_info("Task 3 started\n");
    
    for (int i = 0; i < 5; i++) {
        logger_info("Task 3: iteration %d\n", i);
        // 主动让出 CPU，演示 yield 功能
        task_yield();
    }
    
    logger_info("Task 3 finished\n");
}


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

    // 初始化 CPU 缓存行大小
    init_cpu_cacheline_size();

    // 初始化 GICv3 中断控制器
    gicv3_init();

    // 初始化定时器
    timer_init();

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
    // 使能中断
    enable_interrupts();
    
    // 初始化 DW UART
    dw_uart_init();
    
    // 初始化任务子系统
    task_init();
    
    logger_info("System initialization completed\n");
    logger_info("Creating test tasks...\n");
    
    // 创建测试任务
    task_create("task1", test_task1, NULL);
    task_create("task2", test_task2, NULL);
    task_create("task3", test_task3, NULL);
    
    logger_info("Starting scheduler...\n");
    
    // 启动调度器（不会返回）
    scheduler_start();
    
    // 不应该到达这里
    logger_error("Scheduler returned unexpectedly\n");
    while (1) {
        WFI();
    }
}
