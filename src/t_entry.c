
#include "t_types.h"

#include "t_gicv3.h"
#include "t_dw_uart.h"
#include "t_timer.h"
#include "npu/rknpu.h"
#include "cru.h"
#include "scmi.h"

#include "simplebash.h"
#include "t_task.h"
#include "t_sysreg.h"


#include "lib/t_logger.h"
#include "lib/rand.h"
#include "npu/rkmem.h"

extern void
__bss_start();
extern void
__bss_end();
extern void
__heap_flag();

// 测试任务1 - 抢占式调度测试
void
test_task1_entry(void *arg)
{
    (void) arg;
    uint32_t cpu_id = get_current_cpu_id();
    for (int i = 0; i < 10; i++) {
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
    for (int i = 0; i < 10; i++) {
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
        if (dw_uart_rx_available()) {
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

    // 只在CPU 0上创建任务（单核测试）
    if (cpu_id == 0) {
        task_t *test1 = task_create("test1", test_task1_entry, NULL, 0);
        task_t *test2 = task_create("test2", test_task2_entry, NULL, 0);
        task_t *shell = task_create("shell", shell_task_entry, NULL, 0);

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

        if (!shell) {
            logger_error("Failed to create shell task\n");
        } else {
            logger_info("Created shell task on CPU 0\n");
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


static inline unsigned
read_currentel(void)
{
    unsigned el;
    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    return (el >> 2) & 0x3;
}


// SMP函数声明
extern void
start_secondary_cpus(void);

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
        if (dw_uart_rx_available()) {
            simplebash_run();
        } else {
            // 让出CPU给其他任务
            WFI();
        }
    }
}

// 副核CPU核心的入口函数
void
t_second_kernel_main(void)
{
    // rk3588 先不考虑多核
    // gicc_init();

    // 初始化定时器
    timer_init();

    // 调用 main_entry
    t_main_entry();
}




// 主内核入口函数
void
t_kernel_main(void)
{
    logger_info("bss start: %p, end: %p, size: %u KB\n",
                &__bss_start,
                &__bss_end,
                ((uint64_t) &__bss_end - (uint64_t) &__bss_start) / 1024);
    
    logger_info("heap flag address: %p\n", &__heap_flag);


    // 在 main 里打印
    logger_warn("CurrentEL = %u\n", read_currentel());

    // 初始化gicv3芯片
    gicv3_init();

    // 初始化uart
    // dw_uart_init();

    // 初始化定时器
    timer_init();

    // 显示定时器信息
    // timer_dump_info();

    // 启用定时器
    timer_enable();
    enable_interrupts();  // daifclr 2
    logger_info("After enabling interrupts\n");


    // 随机数模块测试
    srand_tick();
    
    logger_info("Random number test: %ld\n",rand_tick());
    logger_info("Random number test: %ld\n",rand_tick());
    logger_info("Random number test: %ld\n",rand_tick());
    logger_info("Random number test: %ld\n",rand_tick());

    // 申请内存测试
    size_t heap_size = (1 << 28);  // 1 G
    rkmem_init(heap_size);
    
    void *mem1 = rkmem_alloc(256 * 1024);  // 256 KB
    void *mem2 = rkmem_alloc(512 * 1024);  // 512 KB
    
    logger_info("Memory allocation test:\n");
    logger_info("  Allocated 256 KB at %p\n", mem1);
    logger_info("  Allocated 512 KB at %p\n", mem2);
    
    // scmi 时钟
    enable_scmi_clock(6);

    // cru 时钟
    enable_rk3588_npu_clocks();

    // RKNPU 初始化测试
    rknpu_init();

    // 测试
    rknpu_test();

    while (1) {
        WFI();
    }
}
