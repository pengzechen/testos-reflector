#ifndef __T_TIMER_H__
#define __T_TIMER_H__

#include "t_types.h"
#include "cfg/t_cfg.h"
#include "t_sysreg.h"


// 定时器寄存器访问宏 - 使用统一的系统寄存器定义
#define CNTFRQ_EL0_READ()        READ_CNTFRQ_EL0()
#define CNTVCT_EL0_READ()        READ_CNTVCT_EL0()
#define CNTV_CTL_EL0_READ()      READ_CNTV_CTL_EL0()
#define CNTV_CTL_EL0_WRITE(val)  WRITE_CNTV_CTL_EL0(val)
#define CNTV_CVAL_EL0_READ()     READ_CNTV_CVAL_EL0()
#define CNTV_CVAL_EL0_WRITE(val) WRITE_CNTV_CVAL_EL0(val)
#define CNTV_TVAL_EL0_READ()     READ_CNTV_TVAL_EL0()
#define CNTV_TVAL_EL0_WRITE(val) WRITE_CNTV_TVAL_EL0(val)

// 定时器控制寄存器位定义
#define CNTV_CTL_ENABLE  (1 << 0)  // 使能定时器
#define CNTV_CTL_IMASK   (1 << 1)  // 中断屏蔽
#define CNTV_CTL_ISTATUS (1 << 2)  // 中断状态

// 全局变量
extern volatile uint64_t g_system_ticks;     // 系统tick计数
extern volatile uint64_t g_timer_frequency;  // 定时器频率

// 函数声明
void
timer_init(void);
void
timer_enable(void);
void
timer_disable(void);
void
timer_set_next_interrupt(uint64_t ticks_from_now);
void
timer_handler(uint64_t *stack_pointer);

// 时间相关函数
uint64_t
timer_get_system_ticks(void);
uint64_t
timer_get_uptime_ms(void);
uint64_t
timer_get_frequency(void);
void
timer_delay_ms(uint32_t ms);
void
timer_delay_us(uint32_t us);

// 调度相关函数
void
timer_schedule_next_tick(void);
bool
timer_should_schedule(void);

// 统计信息
typedef struct
{
    uint64_t total_interrupts;     // 总中断次数
    uint64_t total_schedules;      // 总调度次数
    uint64_t last_interrupt_time;  // 上次中断时间
} timer_stats_t;

extern timer_stats_t g_timer_stats;

void
timer_get_stats(timer_stats_t *stats);
void
timer_reset_stats(void);
void
timer_dump_info(void);

#endif  // __T_TIMER_H__
