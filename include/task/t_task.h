#ifndef __T_TASK_H__
#define __T_TASK_H__

#include "t_types.h"
#include "t_exception.h"
#include "cfg/t_cfg.h"

// 任务状态
typedef enum {
    TASK_READY,      // 就绪状态
    TASK_RUNNING,    // 运行状态
    TASK_BLOCKED,    // 阻塞状态
    TASK_DEAD        // 死亡状态
} task_state_t;

// 任务栈大小 (4KB)
#define TASK_STACK_SIZE (4096)

// 最大任务数
#define MAX_TASKS 32

// 任务控制块 (TCB)
typedef struct task_struct {
    uint64_t task_id;                    // 任务 ID
    char name[32];                       // 任务名称
    task_state_t state;                  // 任务状态
    uint64_t cpu_id;                     // 运行在哪个 CPU 上（用于多核调度）
    
    // 上下文信息
    trap_frame_t context;                // 寄存器上下文
    uint64_t sp;                         // 任务栈指针
    
    // 栈信息
    void *stack_base;                    // 栈基地址
    size_t stack_size;                   // 栈大小
    
    // 调度信息
    uint64_t time_slice;                 // 时间片
    uint64_t runtime;                    // 运行时间统计
    
    // 链表节点
    struct task_struct *next;            // 下一个任务
    struct task_struct *prev;            // 上一个任务
} task_t;

// 调度器统计信息
typedef struct {
    uint64_t total_switches;             // 总切换次数
    uint64_t total_schedules;            // 总调度次数
} sched_stats_t;

// 任务模块初始化
void task_init(void);

// 任务创建和销毁
task_t* task_create(const char *name, void (*entry)(void*), void *arg);
void task_destroy(task_t *task);

// 任务调度
void schedule(void);
void schedule_on_irq(uint64_t *stack_pointer);

// 主动让出CPU
void task_yield(void);

// 获取当前任务
task_t* get_current_task(void);

// 获取当前 CPU ID
uint64_t get_current_cpu_id(void);

// 任务切换 (汇编实现)
extern void task_switch(trap_frame_t *prev_ctx, trap_frame_t *next_ctx);

// 启动调度器
void scheduler_start(void);

// 调度器状态
extern volatile bool scheduler_started;

// 统计信息
void sched_get_stats(sched_stats_t *stats);

// Idle 任务
void idle_task_entry(void *arg);

#endif // __T_TASK_H__
