#include "task/t_task.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/t_spinlock.h"
#include "t_sysreg.h"
#include "mem/cache.h"

// 全局任务池
static task_t task_pool[MAX_TASKS];
static uint8_t task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));

// 就绪队列（简单的循环链表）
static task_t *ready_queue_head = NULL;
static task_t *ready_queue_tail = NULL;

// 就绪队列锁（保护并发访问）
static spinlock_irq_t ready_queue_lock = SPINLOCK_IRQ_INIT;

// 每个 CPU 的当前运行任务
static task_t *current_tasks[T_SMP_NUM] = {NULL};

// 下一个任务 ID
static uint64_t next_task_id = 0;

// 调度器统计（每个 CPU 一份）
static sched_stats_t sched_stats[T_SMP_NUM] = {0};

// 调度器是否启动
static volatile bool scheduler_started = false;

// 获取当前 CPU ID
uint64_t get_current_cpu_id(void)
{
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xFF;
}

// 获取当前任务
task_t* get_current_task(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM) {
        return NULL;
    }
    return current_tasks[cpu_id];
}

// 从就绪队列中移除任务（需要持有锁）
static void remove_from_ready_queue(task_t *task)
{
    if (task == NULL || task->state != TASK_READY) {
        return;
    }
    
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        ready_queue_head = task->next;
    }
    
    if (task->next) {
        task->next->prev = task->prev;
    } else {
        ready_queue_tail = task->prev;
    }
    
    task->next = NULL;
    task->prev = NULL;
}

// 添加任务到就绪队列尾部（需要持有锁）
static void add_to_ready_queue(task_t *task)
{
    if (task == NULL) {
        return;
    }
    
    task->state = TASK_READY;
    task->next = NULL;
    task->prev = ready_queue_tail;
    
    if (ready_queue_tail) {
        ready_queue_tail->next = task;
    } else {
        ready_queue_head = task;
    }
    
    ready_queue_tail = task;
}

// 选择下一个任务 (Round-Robin)（需要持有锁）
static task_t* pick_next_task(void)
{
    if (ready_queue_head == NULL) {
        return NULL;
    }
    
    // RR 调度：从队列头部取出任务
    task_t *next = ready_queue_head;
    remove_from_ready_queue(next);
    
    return next;
}

// 任务模块初始化
void task_init(void)
{
    logger_info("Initializing task subsystem...\n");
    
    // 初始化任务池
    for (int i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_DEAD;
        task_pool[i].task_id = 0;
        task_pool[i].cpu_id = 0;
        task_pool[i].next = NULL;
        task_pool[i].prev = NULL;
    }
    
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    spinlock_irq_init(&ready_queue_lock);
    
    for (int i = 0; i < T_SMP_NUM; i++) {
        current_tasks[i] = NULL;
        sched_stats[i].total_switches = 0;
        sched_stats[i].total_schedules = 0;
    }
    
    next_task_id = 1;
    scheduler_started = false;
    
    logger_info("Task subsystem initialized (multi-core support: %d CPUs)\n", T_SMP_NUM);
}

// 分配一个空闲的 TCB
static task_t* alloc_task(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_DEAD) {
            return &task_pool[i];
        }
    }
    return NULL;
}

// 任务入口包装函数
static void task_entry_wrapper(void)
{
    task_t *task = get_current_task();
    
    if (task == NULL) {
        logger_error("task_entry_wrapper: current_task is NULL\n");
        while (1) WFI();
    }
    
    // 从 context.r[0] 获取入口函数指针
    void (*entry)(void*) = (void (*)(void*))task->context.r[0];
    void *arg = (void*)task->context.r[1];
    
    // 调用真正的任务函数
    if (entry) {
        entry(arg);
    }
    
    // 任务结束，标记为死亡状态
    logger_info("Task %s (ID %llu) on CPU %llu finished\n", 
               task->name, task->task_id, task->cpu_id);
    task->state = TASK_DEAD;
    
    // 触发调度
    schedule();
    
    // 不应该返回到这里
    while (1) WFI();
}

// 创建任务
task_t* task_create(const char *name, void (*entry)(void*), void *arg)
{
    spin_lock_irqsave(&ready_queue_lock);
    
    task_t *task = alloc_task();
    if (task == NULL) {
        spin_unlock_irqrestore(&ready_queue_lock);
        logger_error("Failed to allocate task\n");
        return NULL;
    }
    
    // 初始化 TCB
    task->task_id = next_task_id++;
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    task->state = TASK_READY;
    task->cpu_id = 0;  // 初始不绑定到特定 CPU
    task->time_slice = 10;  // 10 个 timer tick
    task->runtime = 0;
    
    // 设置栈
    int task_idx = task - task_pool;
    task->stack_base = task_stacks[task_idx];
    task->stack_size = TASK_STACK_SIZE;
    
    // 初始化栈指针（栈向下增长，指向栈顶）
    task->sp = (uint64_t)task->stack_base + TASK_STACK_SIZE;
    
    // 初始化上下文
    memset(&task->context, 0, sizeof(trap_frame_t));
    
    // 设置入口函数和参数到寄存器中
    task->context.r[0] = (uint64_t)entry;   // 入口函数
    task->context.r[1] = (uint64_t)arg;     // 参数
    
    // 设置 ELR 为任务包装函数
    task->context.elr = (uint64_t)task_entry_wrapper;
    
    // 设置 SPSR: EL1h 模式，使能所有中断
    task->context.spsr = 0x0;  // EL1h, DAIF=0000 (中断使能)
    
    // 设置栈指针
    task->context.usp = task->sp;
    
    // 添加到就绪队列
    add_to_ready_queue(task);
    
    spin_unlock_irqrestore(&ready_queue_lock);
    
    logger_info("Created task: %s (ID %llu)\n", task->name, task->task_id);
    
    return task;
}

// 销毁任务
void task_destroy(task_t *task)
{
    if (task == NULL) {
        return;
    }
    
    logger_info("Destroying task: %s (ID %llu)\n", task->name, task->task_id);
    
    spin_lock_irqsave(&ready_queue_lock);
    
    // 从就绪队列移除
    if (task->state == TASK_READY) {
        remove_from_ready_queue(task);
    }
    
    // 标记为死亡
    task->state = TASK_DEAD;
    
    spin_unlock_irqrestore(&ready_queue_lock);
}

// 调度函数 (从中断上下文调用)
void schedule_on_irq(uint64_t *stack_pointer)
{
    if (!scheduler_started) {
        return;
    }
    
    uint64_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM) {
        return;
    }
    
    trap_frame_t *frame = (trap_frame_t*)stack_pointer;
    
    sched_stats[cpu_id].total_schedules++;
    
    task_t *prev_task = current_tasks[cpu_id];
    
    // 获取就绪队列锁
    spin_lock_irqsave(&ready_queue_lock);
    
    // 如果当前任务还在运行，保存上下文并放回就绪队列
    if (prev_task && prev_task->state == TASK_RUNNING) {
        memcpy(&prev_task->context, frame, sizeof(trap_frame_t));
        add_to_ready_queue(prev_task);
    }
    
    // 选择下一个任务
    task_t *next_task = pick_next_task();
    
    if (next_task == NULL) {
        spin_unlock_irqrestore(&ready_queue_lock);
        // 没有就绪任务，继续运行当前任务或进入 idle
        if (prev_task && prev_task->state != TASK_DEAD) {
            next_task = prev_task;
            next_task->state = TASK_RUNNING;
        } else {
            // 系统空闲，回到 WFI
            current_tasks[cpu_id] = NULL;
            return;
        }
    } else {
        spin_unlock_irqrestore(&ready_queue_lock);
    }
    
    // 切换任务
    if (next_task != prev_task) {
        sched_stats[cpu_id].total_switches++;
        
        current_tasks[cpu_id] = next_task;
        next_task->state = TASK_RUNNING;
        next_task->cpu_id = cpu_id;
        
        // 恢复下一个任务的上下文
        memcpy(frame, &next_task->context, sizeof(trap_frame_t));
        
        // logger_info("[CPU %llu] Task switch: %s -> %s\n", 
        //            cpu_id,
        //            prev_task ? prev_task->name : "none",
        //            next_task->name);
    } else {
        // 继续运行当前任务
        if (next_task) {
            next_task->state = TASK_RUNNING;
        }
    }
}

// 非中断上下文的调度 (主动调度)
void schedule(void)
{
    if (!scheduler_started) {
        return;
    }
    
    uint64_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM) {
        return;
    }
    
    // 禁用中断
    disable_interrupts();
    
    sched_stats[cpu_id].total_schedules++;
    
    task_t *prev_task = current_tasks[cpu_id];
    
    // 获取就绪队列锁
    spin_lock_irqsave(&ready_queue_lock);
    
    // 如果当前任务还在运行，放回就绪队列
    if (prev_task && prev_task->state == TASK_RUNNING) {
        add_to_ready_queue(prev_task);
    }
    
    // 选择下一个任务
    task_t *next_task = pick_next_task();
    
    spin_unlock_irqrestore(&ready_queue_lock);
    
    if (next_task == NULL) {
        // 没有任务，进入 idle
        enable_interrupts();
        while (1) WFI();
    }
    
    // 切换任务
    if (next_task != prev_task) {
        sched_stats[cpu_id].total_switches++;
        
        task_t *old_task = current_tasks[cpu_id];
        current_tasks[cpu_id] = next_task;
        next_task->state = TASK_RUNNING;
        next_task->cpu_id = cpu_id;
        
        // logger_info("[CPU %llu] Task switch: %s -> %s\n", 
        //            cpu_id,
        //            old_task ? old_task->name : "none",
        //            next_task->name);
        
        // 执行任务切换（保存/恢复上下文）
        if (old_task) {
            task_switch(&old_task->context, &next_task->context);
        } else {
            // 第一次切换，直接跳转
            task_switch(NULL, &next_task->context);
        }
    } else {
        if (next_task) {
            next_task->state = TASK_RUNNING;
        }
    }
    
    // 使能中断
    enable_interrupts();
}

// 主动让出 CPU (yield)
void task_yield(void)
{
    // 主动让出 CPU，触发调度
    schedule();
}

// Idle 任务
void idle_task_entry(void *arg)
{
    (void)arg;
    uint64_t cpu_id = get_current_cpu_id();
    logger_info("Idle task started on CPU %llu\n", cpu_id);
    
    while (1) {
        WFI();
    }
}

// 启动调度器
void scheduler_start(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    logger_info("[CPU %llu] Starting scheduler...\n", cpu_id);
    
    // 只在主核创建 idle 任务
    if (cpu_id == 0) {
        // 为每个 CPU 创建 idle 任务
        for (int i = 0; i < T_SMP_NUM; i++) {
            char idle_name[32];
            my_snprintf(idle_name, sizeof(idle_name), "idle-%d", i);
            task_t *idle = task_create(idle_name, idle_task_entry, NULL);
            if (idle == NULL) {
                logger_error("Failed to create idle task for CPU %d\n", i);
            }
        }
    }
    
    scheduler_started = true;
    
    // 启动第一次调度
    schedule();
}

// 获取调度器统计信息
void sched_get_stats(sched_stats_t *stats)
{
    if (stats) {
        uint64_t cpu_id = get_current_cpu_id();
        if (cpu_id < T_SMP_NUM) {
            stats->total_switches = sched_stats[cpu_id].total_switches;
            stats->total_schedules = sched_stats[cpu_id].total_schedules;
        }
    }
}
