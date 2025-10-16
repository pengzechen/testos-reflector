#include "t_timer.h"
#include "t_mmio.h"
#include "t_gicv3.h"
#include "lib/t_logger.h"

#include "t_exception.h"

// 调度器函数声明（在task模块中实现）
extern void
scheduler_tick(uint32_t cpu_id);

// 调度标志位（在exception模块中定义）
extern volatile int need_schedule_flag;
// 检查并唤醒到期的睡眠任务
extern void wake_up_sleeping_tasks(uint32_t cpu_id);

// 全局变量
volatile uint64_t g_system_ticks    = 0;
volatile uint64_t g_timer_frequency = 0;
timer_stats_t     g_timer_stats     = {0};

// 初始化定时器
void
timer_init(void)
{
    // 读取定时器频率
    g_timer_frequency = CNTFRQ_EL0_READ();

    logger_info("Timer initialization:\n");
    logger_info("  Timer frequency: %llu Hz\n", g_timer_frequency);
    logger_info("  Target frequency: %d Hz\n", TIMER_FREQUENCY_HZ);
    logger_info("  Tick interval: %d ms\n", TIMER_TICK_MS);

    // 禁用定时器
    timer_disable();

    // 清零统计信息
    timer_reset_stats();

    // 安装中断回调函数
    irq_install(CNTP_TIMER, timer_handler);

    // 在GIC中启用定时器中断
    gicv3_enable_int(CNTP_TIMER, true);

    logger_info("Timer initialized successfully\n");
}

// 启用定时器
void
timer_enable(void)
{
    // 计算下一次中断的时间
    uint64_t ticks_per_interrupt = g_timer_frequency / TIMER_FREQUENCY_HZ;

    // 设置定时器值
    CNTP_TVAL_EL0_WRITE(ticks_per_interrupt);

    // 启用定时器，不屏蔽中断
    CNTP_CTL_EL0_WRITE(CNTV_CTL_ENABLE);

    logger_info("Timer enabled with %llu ticks per interrupt\n", ticks_per_interrupt);
}

// 禁用定时器
void
timer_disable(void)
{
    // 禁用定时器并屏蔽中断
    CNTP_CTL_EL0_WRITE(CNTV_CTL_IMASK);

    logger_info("Timer disabled\n");
}

// 设置下一次中断
void
timer_set_next_interrupt(uint64_t ticks_from_now)
{
    CNTP_TVAL_EL0_WRITE(ticks_from_now);
}




// 调度下一个tick
void
timer_schedule_next_tick(void)
{
    uint64_t ticks_per_interrupt = g_timer_frequency / TIMER_FREQUENCY_HZ;
    timer_set_next_interrupt(ticks_per_interrupt);
}

// 定时器中断处理函数
void
timer_handler(uint64_t *stack_pointer)
{
    (void) stack_pointer;  // Suppress unused parameter warning

    logger_info("Timer interrupt handler invoked\n");

    // 更新系统tick计数
    g_system_ticks++;

    // 更新统计信息
    g_timer_stats.total_interrupts++;
    g_timer_stats.last_interrupt_time = timer_get_uptime_ms();

    // 调度下一个tick
    timer_schedule_next_tick();

    // 调用调度器tick函数更新统计信息（但不直接调度）
    uint32_t current_cpu = get_current_cpu_id();
    scheduler_tick(current_cpu);

    wake_up_sleeping_tasks(current_cpu);

    // 设置调度标志位，延迟到中断处理完成后再调度
    // 这样可以确保GIC的EOIR和DIR已经写入，避免中断丢失
    need_schedule_flag = 1;

    // 更新调度统计
    g_timer_stats.total_schedules++;

    // 每100个tick打印一次信息（每1秒，因为100Hz）
    if (g_system_ticks % TIMER_FREQUENCY_HZ == 0) {
        // logger_info("Timer: %llu seconds, %llu ticks, %llu interrupts\n",
        //             g_system_ticks / TIMER_FREQUENCY_HZ,
        //             g_system_ticks,
        //             g_timer_stats.total_interrupts);
    }

    // 前几个中断打印调试信息
    if (g_system_ticks <= 10) {
        // logger_info("Timer interrupt #%llu: uptime=%llu ms\n",
        //             g_system_ticks,
        //             timer_get_uptime_ms());
    }
}

// 获取系统tick数
uint64_t
timer_get_system_ticks(void)
{
    return g_system_ticks;
}

// 获取系统运行时间（毫秒）
uint64_t
timer_get_uptime_ms(void)
{
    return g_system_ticks * TIMER_TICK_MS;
}

// 获取定时器频率
uint64_t
timer_get_frequency(void)
{
    return g_timer_frequency;
}


// 毫秒延时（忙等待）
void
timer_delay_ms(uint32_t ms)
{
    uint64_t start_time  = CNTPCT_EL0_READ();
    uint64_t delay_ticks = (g_timer_frequency * ms) / 1000;
    uint64_t target_time = start_time + delay_ticks;

    while (CNTPCT_EL0_READ() < target_time) {
        asm volatile("nop");
    }
}

// 微秒延时（忙等待）
void
timer_delay_us(uint32_t us)
{
    uint64_t start_time  = CNTPCT_EL0_READ();
    uint64_t delay_ticks = (g_timer_frequency * us) / 1000000;
    uint64_t target_time = start_time + delay_ticks;

    while (CNTPCT_EL0_READ() < target_time) {
        asm volatile("nop");
    }
}

// 检查是否应该调度
bool
timer_should_schedule(void)
{
    // 简单实现：总是返回true，让调度器决定
    return true;
}

// 获取统计信息
void
timer_get_stats(timer_stats_t *stats)
{
    if (stats) {
        *stats = g_timer_stats;
    }
}

// 重置统计信息
void
timer_reset_stats(void)
{
    g_timer_stats.total_interrupts    = 0;
    g_timer_stats.total_schedules     = 0;
    g_timer_stats.last_interrupt_time = 0;
}

// 打印定时器信息
void
timer_dump_info(void)
{
    uint64_t current_time = CNTPCT_EL0_READ();
    uint32_t ctl_reg      = CNTP_CTL_EL0_READ();

    logger_info("Timer Information:\n");
    logger_info("  Frequency: %llu Hz\n", g_timer_frequency);
    logger_info("  Current time: %llu ticks\n", current_time);
    logger_info("  System ticks: %llu\n", g_system_ticks);
    logger_info("  Uptime: %llu ms\n", timer_get_uptime_ms());
    logger_info("  Control register: 0x%x\n", ctl_reg);
    logger_info("  Timer enabled: %s\n", (ctl_reg & CNTV_CTL_ENABLE) ? "Yes" : "No");
    logger_info("  Interrupt masked: %s\n", (ctl_reg & CNTV_CTL_IMASK) ? "Yes" : "No");
    logger_info("  Interrupt pending: %s\n", (ctl_reg & CNTV_CTL_ISTATUS) ? "Yes" : "No");

    logger_info("Timer Statistics:\n");
    logger_info("  Total interrupts: %llu\n", g_timer_stats.total_interrupts);
    logger_info("  Total schedules: %llu\n", g_timer_stats.total_schedules);
    logger_info("  Last interrupt: %llu ms ago\n",
                timer_get_uptime_ms() - g_timer_stats.last_interrupt_time);
}
