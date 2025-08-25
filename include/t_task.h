#ifndef __T_TASK_H__
#define __T_TASK_H__

#include "t_types.h"
#include "t_exception.h"
#include "cfg/t_cfg.h"

// 任务状态
typedef enum
{
    TASK_READY = 0,  // 就绪状态
    TASK_RUNNING,    // 运行状态
    TASK_BLOCKED,    // 阻塞状态
    TASK_TERMINATED  // 终止状态
} task_state_t;

// 任务ID类型
typedef uint32_t task_id_t;

// 任务栈大小
#define TASK_STACK_SIZE (8 * 1024)  // 8KB per task

// 最大任务数量
#define MAX_TASKS_PER_CPU 16
#define MAX_TOTAL_TASKS   (MAX_TASKS_PER_CPU * T_SMP_NUM)

typedef struct task_ctx
{
    uint64_t r[31];   // General-purpose registers x0..x30
    uint64_t sp_elx;  // sp_el1 or sp_el2
    uint64_t tpidr;   // tpidr el1 or el2
} task_ctx_t;

// 任务控制块 (Task Control Block)
typedef struct task
{
    // 上下文信息
    task_ctx_t context;  // CPU上下文（寄存器状态）

    task_id_t    task_id;   // 任务ID
    char         name[32];  // 任务名称
    task_state_t state;     // 任务状态
    uint32_t     cpu_id;    // 绑定的CPU ID

    uint64_t *stack_base;  // 栈基地址
    uint64_t *stack_top;   // 栈顶地址
    uint64_t  stack_size;  // 栈大小

    // 调度信息
    uint32_t time_slice;       // 时间片（tick数）
    uint32_t remaining_ticks;  // 剩余时间片
    uint64_t total_runtime;    // 总运行时间
    uint64_t last_scheduled;   // 上次调度时间

    // 链表节点
    struct task *next;  // 下一个任务
    struct task *prev;  // 上一个任务

    // 任务入口点
    void (*entry_point)(void *arg);  // 任务入口函数
    void *arg;                       // 任务参数
} task_t;

// 每个CPU的调度器
typedef struct cpu_scheduler
{
    uint32_t cpu_id;        // CPU ID
    task_t  *current_task;  // 当前运行的任务
    task_t  *idle_task;     // idle任务

    // 就绪队列（简单的循环链表）
    task_t  *ready_queue_head;  // 就绪队列头
    task_t  *ready_queue_tail;  // 就绪队列尾
    uint32_t ready_count;       // 就绪队列中的任务数

    // 统计信息
    uint64_t total_switches;  // 总切换次数
    uint64_t total_ticks;     // 总tick数
    uint64_t idle_ticks;      // idle时间
} cpu_scheduler_t;

// 全局任务管理器
typedef struct task_manager
{
    task_t    task_pool[MAX_TOTAL_TASKS];  // 任务池
    bool      task_used[MAX_TOTAL_TASKS];  // 任务是否被使用
    task_id_t next_task_id;                // 下一个任务ID
    uint32_t  total_tasks;                 // 总任务数

    // 每个CPU的调度器
    cpu_scheduler_t schedulers[T_SMP_NUM];
} task_manager_t;

// 全局变量声明
extern task_manager_t g_task_manager;

// 任务管理函数
void
task_manager_init(void);
void
scheduler_init(uint32_t cpu_id);

// 任务操作函数
task_t *
task_create(const char *name, void (*entry_point)(void *), void *arg, uint32_t cpu_id);
void
task_exit(void);
void
task_yield(void);
task_t *
task_get_current(void);
uint32_t
task_get_current_cpu(void);

// 调度器函数
void
scheduler_tick(uint32_t cpu_id);
void
scheduler_delayed_schedule(uint32_t cpu_id);
void
scheduler_schedule(uint32_t cpu_id);
void
scheduler_add_task(task_t *task);
void
scheduler_remove_task(task_t *task);

// 上下文切换函数（汇编实现）
extern void
task_switch_context(task_ctx_t *old_ctx, task_ctx_t *new_ctx);
// 任务启动包装函数声明（汇编实现）
extern void
exception_return(void);

// idle任务
void
idle_task_entry(void *arg);

// 调试和统计函数
void
task_dump_info(uint32_t cpu_id);
void
task_dump_all_info(void);

#endif  // __T_TASK_H__
