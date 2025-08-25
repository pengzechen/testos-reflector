
#include "t_types.h"

#include "lib/t_logger.h"

#include "t_gicv2.h"
#include "t_uart.h"
#include "t_timer.h"
#include "simplebash.h"
#include "t_task.h"

#include "t_sysreg.h"

// SMP函数声明
extern void
start_secondary_cpus(void);


// 测试任务1 - 抢占式调度测试
void
test_task1_entry(void *arg)
{
    (void) arg;
    uint32_t cpu_id = get_current_cpu_id();

    // logger_info("Test Task 1 started on CPU %u (preemptive scheduling)\n", cpu_id);
    // logger_info("Test Task 1: Checking interrupt status...\n");
    // uint32_t daif       = get_daif();
    // uint32_t current_el = get_current_el();
    // // 正确检查IRQ状态：DAIF.I位在位7
    // bool irq_disabled = (daif & (1 << 7)) != 0;
    // logger_info("Test Task 1: DAIF register = 0x%x (IRQ %s)\n",
    //             daif,
    //             irq_disabled ? "disabled" : "enabled");
    // if (irq_disabled) {
    //     logger_error("IRQ is disabled! Enabling IRQ...\n");
    //     enable_interrupts();
    //     uint32_t new_daif         = get_daif();
    //     bool     new_irq_disabled = (new_daif & (1 << 7)) != 0;
    //     logger_info("After enable_interrupts: DAIF = 0x%x (IRQ %s)\n",
    //                 new_daif,
    //                 new_irq_disabled ? "disabled" : "enabled");
    // }
    // logger_info("Test Task 1: Current EL = %u\n", current_el);

    // // 检查定时器状态
    // uint32_t timer_ctl = CNTV_CTL_EL0_READ();
    // int32_t  timer_val = CNTV_TVAL_EL0_READ();
    // logger_info("Test Task 1: Timer CTL=0x%x, TVAL=%d\n", timer_ctl, timer_val);
    // logger_info("Test Task 1: Timer enabled=%s, masked=%s\n",
    //             (timer_ctl & CNTV_CTL_ENABLE) ? "yes" : "no",
    //             (timer_ctl & CNTV_CTL_IMASK) ? "yes" : "no");

    for (int i = 0; i < 50; i++) {
        logger_info("Test Task 1 on CPU %u: iteration %d\n", cpu_id, i);

        // 每10次迭代检查定时器状态
        if (i % 10 == 0) {
            int32_t timer_val = (int32_t) CNTV_TVAL_EL0_READ();
            logger_info("Test Task 1: iteration %d, TVAL=%d\n", i, timer_val);

            // 如果定时器已经过期（变成负值），说明中断没有触发
            if (timer_val < 0) {
                logger_warn("Timer expired but no interrupt! TVAL=%d\n", timer_val);
            }
        }

        // 短的忙等待，让定时器中断有机会抢占
        task_sleep(1000);
    }

    logger_info("Test Task 1 on CPU %u finished\n", cpu_id);
    task_exit();
}

// 测试任务2 - 睡眠测试
void
test_task2_entry(void *arg)
{
    (void) arg;
    uint32_t cpu_id = get_current_cpu_id();

    logger_info("Test Task 2 started on CPU %u (sleep test)\n", cpu_id);

    for (int i = 0; i < 50; i++) {
        logger_info("Test Task 2 on CPU %u: iteration %d\n", cpu_id, i);
        task_sleep(1000);
    }

    logger_info("Test Task 2 on CPU %u finished\n", cpu_id);
    task_exit();
}

// Shell任务入口函数
void
shell_task_entry(void *arg)
{
    (void) arg;  // 未使用的参数

    uint32_t cpu_id = get_current_cpu_id();
    logger_info("Shell task started on CPU %u\n", cpu_id);

    // Initialize the shell
    simplebash_init();

    // Main shell loop
    while (1) {
        // Check if there's input available and process it
        if (uart_rx_available()) {
            simplebash_run();
        } else {
            // 让出CPU给其他任务
            task_yield();
        }
    }
}

// 所有核的入口函数
void
t_main_entry()
{
    uint32_t cpu_id = get_current_cpu_id();
    logger_info("Entering main entry function on CPU %u\n", cpu_id);

    // 初始化当前CPU的调度器
    scheduler_init(cpu_id);

    // 为当前CPU创建idle任务
    task_t *idle_task = task_create("idle", idle_task_entry, NULL, cpu_id);
    if (!idle_task) {
        logger_error("Failed to create idle task for CPU %u\n", cpu_id);
        while (1)
            WFI();
    }

    // 设置idle任务并从就绪队列中移除（idle任务不应该在就绪队列中）
    g_task_manager.schedulers[cpu_id].idle_task = idle_task;
    scheduler_remove_task(idle_task);

    // 只在CPU 0上创建测试任务（单核测试）
    if (cpu_id == 0) {
        task_t *test1 = task_create("test1", test_task1_entry, NULL, 0);
        task_t *test2 = task_create("test2", test_task2_entry, NULL, 0);

        if (!test1) {
            logger_error("Failed to create test task 1\n");
        } else {
            logger_info("Created test task 1 on CPU 0\n");
        }

        if (!test2) {
            logger_error("Failed to create test task 2\n");
        } else {
            logger_info("Created test task 2 on CPU 0\n");
        }
    }

    // 启用定时器
    timer_enable();

    // 启用中断
    logger_info("Enabling interrupts on CPU %u\n", cpu_id);
    enable_interrupts();

    while (1) {
        WFI();
        // 检查是否有任务需要调度
        // scheduler_schedule(cpu_id);
    }
}

void
t_main_entry2()
{
    uint32_t cpu_id = get_current_cpu_id();
    // 启用定时器
    timer_enable();

    // 启用中断
    logger_info("Enabling interrupts on CPU %u\n", cpu_id);
    enable_interrupts();

    simplebash_init();
    while (1) {
        if (uart_rx_available()) {
            simplebash_run();
        } else {
            // 让出CPU给其他任务
            WFI();
        }
    }
}

// 主内核入口函数
void
t_kernel_main(void)
{
    // 初始化gic芯片
    gic_init();
    // 显示GIC测试信息
    // gic_test_init();

    // 初始化UART
    uart_init();

    // 初始化定时器
    timer_init();
    // 显示定时器信息
    // timer_dump_info();

    // 初始化任务管理器
    task_manager_init();

    logger_info("Task manager initialized, starting main entry\n");

    // 单核测试，不启动其他CPU核心

    // 调用 main_entry
    t_main_entry();
}

// 副核CPU核心的入口函数
void
t_second_kernel_main(void)
{
    // 初始化gicc
    gicc_init();

    // 初始化定时器
    timer_init();

    // 调用 main_entry
    t_main_entry();
}
