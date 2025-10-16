#include "t_task.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "t_sysreg.h"
#include "lib/t_spinlock.h"
#include "t_timer.h"

// 全局任务管理器
task_manager_t g_task_manager;

// 简单的自旋锁保护任务管理器
static spinlock_t task_manager_lock = SPINLOCK_INIT;

// 初始化任务管理器
void
task_manager_init(void)
{
    spin_lock(&task_manager_lock);

    // 清零任务管理器
    memset(&g_task_manager, 0, sizeof(task_manager_t));

    // 初始化任务池
    for (int i = 0; i < MAX_TOTAL_TASKS; i++) {
        g_task_manager.task_used[i] = false;
    }

    g_task_manager.next_task_id = 1;  // 从1开始，0保留
    g_task_manager.total_tasks  = 0;

    spin_unlock(&task_manager_lock);

    logger_info("Task manager initialized\n");
}

// 初始化指定CPU的调度器
void
scheduler_init(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM) {
        logger_error("Invalid CPU ID: %u\n", cpu_id);
        return;
    }

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    // 清零调度器
    memset(scheduler, 0, sizeof(cpu_scheduler_t));

    scheduler->cpu_id           = cpu_id;
    scheduler->current_task     = NULL;
    scheduler->idle_task        = NULL;
    scheduler->ready_queue_head = NULL;
    scheduler->ready_queue_tail = NULL;
    scheduler->ready_count      = 0;
    scheduler->sleep_queue_head = NULL;
    scheduler->sleep_count      = 0;

    logger_info("Scheduler initialized for CPU %u\n", cpu_id);
}

// 分配一个新的任务槽
static task_t *
allocate_task_slot(void)
{
    spin_lock(&task_manager_lock);

    for (int i = 0; i < MAX_TOTAL_TASKS; i++) {
        if (!g_task_manager.task_used[i]) {
            g_task_manager.task_used[i] = true;
            g_task_manager.total_tasks++;

            task_t *task = &g_task_manager.task_pool[i];
            memset(task, 0, sizeof(task_t));
            task->task_id = g_task_manager.next_task_id++;

            spin_unlock(&task_manager_lock);
            return task;
        }
    }

    spin_unlock(&task_manager_lock);
    return NULL;  // 没有可用的任务槽
}

// 释放任务槽
static void
free_task_slot(task_t *task)
{
    if (!task)
        return;

    spin_lock(&task_manager_lock);

    // 找到任务在池中的索引
    int index = task - g_task_manager.task_pool;
    if (index >= 0 && index < MAX_TOTAL_TASKS && g_task_manager.task_used[index]) {
        g_task_manager.task_used[index] = false;
        g_task_manager.total_tasks--;
    }

    spin_unlock(&task_manager_lock);
}

// 分配任务栈
static uint64_t *
allocate_task_stack(void)
{
    // 简单实现：使用静态分配
    // 在实际系统中，这里应该使用动态内存分配
    static uint8_t task_stacks[MAX_TOTAL_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));
    static int     next_stack = 0;

    if (next_stack >= MAX_TOTAL_TASKS) {
        return NULL;
    }

    uint64_t *stack_base = (uint64_t *) task_stacks[next_stack];
    next_stack++;

    return stack_base;
}

// 初始化任务上下文
static void
init_task_context(task_t *task)
{
    // 清零上下文
    memset(&task->context, 0, sizeof(task_ctx_t));

    // 设置栈指针（ARM64栈向下增长）
    // 确保栈指针16字节对齐
    uint64_t stack_top = (uint64_t) (task->stack_base + (TASK_STACK_SIZE / sizeof(uint64_t)) - 1);
    stack_top &= ~0xF;  // 16字节对齐

    // 为任务第一次运行准备栈上的上下文
    // 在栈上为 trap_frame_t 分配空间
    trap_frame_t *trap_frame = (trap_frame_t *) (stack_top - sizeof(trap_frame_t));

    // 清零栈帧
    memset(trap_frame, 0, sizeof(trap_frame_t));

    // 设置栈帧中的寄存器值
    trap_frame->r[0] = (uint64_t) task->arg;          // x0 = 任务参数
    trap_frame->usp  = 0;                             // sp_el0 暂时设为0
    trap_frame->elr  = (uint64_t) task->entry_point;  // elr = 任务入口点
    trap_frame->spsr = 0x00000005;                    // spsr = EL1h, DAIF=0000

    // 更新任务上下文
    task->context.sp_elx = (uint64_t) trap_frame;        // 栈指针指向栈帧
    task->context.r[30]  = (uint64_t) exception_return;  // x30 = exception_return
}

// 创建任务
task_t *
task_create(const char *name, void (*entry_point)(void *), void *arg, uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM) {
        logger_error("Invalid CPU ID: %u\n", cpu_id);
        return NULL;
    }

    if (!entry_point) {
        logger_error("Entry point cannot be NULL\n");
        return NULL;
    }

    // 分配任务槽
    task_t *task = allocate_task_slot();
    if (!task) {
        logger_error("No available task slots\n");
        return NULL;
    }

    // 分配栈
    task->stack_base = allocate_task_stack();
    if (!task->stack_base) {
        logger_error("Failed to allocate stack for task\n");
        free_task_slot(task);
        return NULL;
    }

    // 设置任务属性
    if (name) {
        strncpy(task->name, name, sizeof(task->name) - 1);
        task->name[sizeof(task->name) - 1] = '\0';
    } else {
        my_snprintf(task->name, sizeof(task->name), "task_%u", task->task_id);
    }

    task->state             = TASK_READY;
    task->cpu_id            = cpu_id;
    task->stack_top         = task->stack_base + (TASK_STACK_SIZE / sizeof(uint64_t)) - 1;
    task->stack_size        = TASK_STACK_SIZE;
    task->time_slice        = TIME_SLICE_TICKS;
    task->remaining_ticks   = TIME_SLICE_TICKS;
    task->total_runtime     = 0;
    task->last_scheduled    = 0;
    task->sleep_until_ticks = 0;
    task->next              = NULL;
    task->prev              = NULL;
    task->entry_point       = entry_point;
    task->arg               = arg;

    // 初始化上下文
    init_task_context(task);

    // 添加到调度器
    scheduler_add_task(task);

    logger_info("Created task '%s' (ID: %u) on CPU %u\n", task->name, task->task_id, cpu_id);

    return task;
}

// 获取当前任务
task_t *
task_get_current(void)
{
    uint32_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM) {
        return NULL;
    }

    return g_task_manager.schedulers[cpu_id].current_task;
}

// 获取当前CPU ID
uint32_t
task_get_current_cpu(void)
{
    return get_current_cpu_id();
}

// 添加任务到就绪队列
void
scheduler_add_task(task_t *task)
{
    if (!task)
        return;

    uint32_t cpu_id = task->cpu_id;
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    // 设置任务状态
    task->state = TASK_READY;
    task->next  = NULL;
    task->prev  = NULL;

    // 添加到就绪队列尾部
    if (scheduler->ready_queue_tail) {
        scheduler->ready_queue_tail->next = task;
        task->prev                        = scheduler->ready_queue_tail;
        scheduler->ready_queue_tail       = task;
    } else {
        // 队列为空
        scheduler->ready_queue_head = task;
        scheduler->ready_queue_tail = task;
    }

    scheduler->ready_count++;

    // logger_debug("Added task '%s' to ready queue on CPU %u\n", task->name, cpu_id);
}

// 从就绪队列移除任务
void
scheduler_remove_task(task_t *task)
{
    if (!task)
        return;

    uint32_t cpu_id = task->cpu_id;
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    // 检查任务是否真的在队列中（通过检查prev或next指针，或者是否为头/尾节点）
    bool in_queue = (task->prev != NULL || task->next != NULL ||
                     scheduler->ready_queue_head == task || scheduler->ready_queue_tail == task);

    if (!in_queue) {
        logger_debug("Task '%s' not in ready queue, skipping removal\n", task->name);
        return;
    }

    // 从链表中移除
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        // 这是头节点
        scheduler->ready_queue_head = task->next;
    }

    if (task->next) {
        task->next->prev = task->prev;
    } else {
        // 这是尾节点
        scheduler->ready_queue_tail = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;
    scheduler->ready_count--;

    // logger_debug("Removed task '%s' from ready queue on CPU %u\n", task->name, cpu_id);
}

// 获取下一个要运行的任务
static task_t *
get_next_task(uint32_t cpu_id)
{
    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    // 如果有就绪任务，返回队列头部的任务
    if (scheduler->ready_queue_head) {
        return scheduler->ready_queue_head;
    }

    // 没有就绪任务，返回idle任务
    return scheduler->idle_task;
}

// 执行任务调度
void
scheduler_schedule(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;
    task_t          *next      = get_next_task(cpu_id);

    // 如果没有下一个任务，继续运行当前任务
    if (!next) {
        if (current) {
            current->remaining_ticks = current->time_slice;
        }
        // logger_debug("CPU %u: No next task available\n", cpu_id);
        return;
    }

    // 如果下一个任务就是当前任务，重置时间片
    if (next == current) {
        if (current) {
            current->remaining_ticks = current->time_slice;
        }
        logger_debug("CPU %u: Continuing with current task '%s'\n", cpu_id, current->name);
        return;
    }

    // 需要切换任务
    if (current) {
        // 如果当前任务还在运行且不是idle任务，将其放回就绪队列
        if (current->state == TASK_RUNNING && current != scheduler->idle_task) {
            current->state = TASK_READY;
            // 时间片轮转：将当前任务移到队列尾部
            scheduler_remove_task(current);
            scheduler_add_task(current);
            logger_debug("CPU %u: Put '%s' back to ready queue\n", cpu_id, current->name);
        }
    }

    // 设置新的当前任务
    if (next != scheduler->idle_task) {
        scheduler_remove_task(next);
        logger_debug("CPU %u: Removed '%s' from ready queue\n", cpu_id, next->name);
    }

    next->state           = TASK_RUNNING;
    next->remaining_ticks = next->time_slice;
    next->last_scheduled  = timer_get_system_ticks();

    task_t *old_task        = scheduler->current_task;
    scheduler->current_task = next;
    scheduler->total_switches++;

    logger_info("CPU %u: Switching from '%s' to '%s' (ready: %u)\n",
                cpu_id,
                old_task ? old_task->name : "none",
                next->name,
                scheduler->ready_count);

    // 执行上下文切换
    if (old_task != next) {
        // logger_debug("About to switch context from %s to %s\n",
        //              old_task ? old_task->name : "none",
        //              next->name);

        // 统一使用上下文切换机制，无论是否为第一次运行
        task_switch_context(old_task ? &old_task->context : NULL, &next->context);

        logger_debug("Returned from context switch\n");
    }
}

// 时间片调度（由定时器中断调用）
// 现在只更新统计信息，不直接调度，调度延迟到中断处理完成后
void
scheduler_tick(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;

    scheduler->total_ticks++;

    if (!current) {
        // 没有当前任务，需要调度
        // logger_debug("CPU %u: No current task, need scheduling\n", cpu_id);
        return;
    }

    // 更新当前任务的运行时间
    current->total_runtime++;

    // 如果是idle任务且有其他任务就绪，需要调度
    if (current == scheduler->idle_task) {
        if (scheduler->ready_count > 0) {
            logger_debug("CPU %u: Idle task, %u tasks ready, need scheduling\n",
                         cpu_id,
                         scheduler->ready_count);
        }
        return;
    }

    // 减少剩余时间片
    if (current->remaining_ticks > 0) {
        current->remaining_ticks--;
    }

    // 时间片用完，需要调度
    if (current->remaining_ticks == 0) {
        logger_debug("CPU %u: Time slice expired for '%s', need scheduling\n",
                     cpu_id,
                     current->name);
    }
}

// 延迟调度函数，在中断处理完成后调用
void
scheduler_delayed_schedule(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;

    // 检查是否需要调度
    bool need_schedule = false;

    if (!current) {
        // 没有当前任务，需要调度
        // logger_debug("CPU %u: No current task, need scheduling\n", cpu_id);
        need_schedule = true;
    } else if (current == scheduler->idle_task) {
        // idle任务且有其他任务就绪，需要调度
        // logger_debug("CPU %u: Idle task running, ready_count=%u\n", cpu_id, scheduler->ready_count);
        if (scheduler->ready_count > 0) {
            logger_debug("CPU %u: Idle task, scheduling to ready task\n", cpu_id);
            need_schedule = true;
        }
    } else {
        // 检查时间片是否用完
        if (current->remaining_ticks == 0) {
            logger_debug("CPU %u: Task '%s' time slice expired, need scheduling\n",
                         cpu_id,
                         current->name);
            need_schedule = true;
        }
    }

    if (need_schedule) {
        scheduler_schedule(cpu_id);
    } else {
        // logger_debug("CPU %u: No scheduling needed\n", cpu_id);
    }
}

// 任务主动让出CPU
void
task_yield(void)
{
    uint32_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;

    if (current && current != scheduler->idle_task) {
        // logger_debug("Task '%s' yielding CPU\n", current->name);

        // 将当前任务放回就绪队列
        current->state           = TASK_READY;
        current->remaining_ticks = current->time_slice;  // 重置时间片
        scheduler_add_task(current);

        // 选择下一个任务
        task_t *next = get_next_task(cpu_id);
        if (next && next != current) {
            // 从就绪队列移除下一个任务
            if (next != scheduler->idle_task) {
                scheduler_remove_task(next);
            }

            next->state           = TASK_RUNNING;
            next->remaining_ticks = next->time_slice;
            next->last_scheduled  = timer_get_system_ticks();

            scheduler->current_task = next;
            scheduler->total_switches++;

            // logger_debug("CPU %u: Yielding from '%s' to '%s'\n", cpu_id, current->name, next->name);

            // 执行上下文切换
            task_switch_context(&current->context, &next->context);
        } else {
            // 没有其他任务，继续运行当前任务
            current->state = TASK_RUNNING;
            scheduler_remove_task(current);  // 从就绪队列移除，因为它正在运行
        }
    }
}

// 任务退出
void
task_exit(void)
{
    uint32_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;

    if (current && current != scheduler->idle_task) {
        logger_info("Task '%s' (ID: %u) exiting\n", current->name, current->task_id);

        current->state          = TASK_TERMINATED;
        scheduler->current_task = NULL;

        // 释放任务资源
        free_task_slot(current);

        // 调度下一个任务
        scheduler_schedule(cpu_id);
    }
}

// idle任务入口
void
idle_task_entry(void *arg)
{
    (void) arg;  // 未使用的参数

    uint32_t cpu_id = get_current_cpu_id();
    // logger_info("Idle task started on CPU %u\n", cpu_id);

    while (1) {
        // 等待中断
        WFI();

        // 更新idle统计
        g_task_manager.schedulers[cpu_id].idle_ticks++;
    }
}

// 打印指定CPU的调度器信息
void
task_dump_info(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM) {
        logger_error("Invalid CPU ID: %u\n", cpu_id);
        return;
    }

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    logger_info("=== CPU %u Scheduler Info ===\n", cpu_id);
    logger_info("Current task: %s (ID: %u)\n",
                scheduler->current_task ? scheduler->current_task->name : "none",
                scheduler->current_task ? scheduler->current_task->task_id : 0);
    logger_info("Ready queue count: %u\n", scheduler->ready_count);
    logger_info("Total switches: %llu\n", scheduler->total_switches);
    logger_info("Total ticks: %llu\n", scheduler->total_ticks);
    logger_info("Idle ticks: %llu\n", scheduler->idle_ticks);

    if (scheduler->ready_count > 0) {
        logger_info("Ready tasks:\n");
        task_t *task  = scheduler->ready_queue_head;
        int     count = 0;
        while (task && count < 10) {  // 限制输出数量
            logger_info("  - %s (ID: %u, remaining: %u)\n",
                        task->name,
                        task->task_id,
                        task->remaining_ticks);
            task = task->next;
            count++;
        }
    }
}

// 打印所有CPU的调度器信息
void
task_dump_all_info(void)
{
    logger_info("=== Task Manager Global Info ===\n");
    logger_info("Total tasks: %u\n", g_task_manager.total_tasks);
    logger_info("Next task ID: %u\n", g_task_manager.next_task_id);

    for (uint32_t i = 0; i < T_SMP_NUM; i++) {
        task_dump_info(i);
    }
}

// 添加任务到睡眠队列（按唤醒时间排序）
static void
add_to_sleep_queue(task_t *task)
{
    if (!task)
        return;

    uint32_t cpu_id = task->cpu_id;
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    task->state = TASK_SLEEPING;
    task->next  = NULL;
    task->prev  = NULL;

    // 如果睡眠队列为空，直接添加
    if (!scheduler->sleep_queue_head) {
        scheduler->sleep_queue_head = task;
        scheduler->sleep_count++;
        return;
    }

    // 按唤醒时间排序插入（最早唤醒的在前面）
    task_t *current = scheduler->sleep_queue_head;
    task_t *prev    = NULL;

    while (current && current->sleep_until_ticks <= task->sleep_until_ticks) {
        prev    = current;
        current = current->next;
    }

    // 插入到正确位置
    if (!prev) {
        // 插入到队列头
        task->next                  = scheduler->sleep_queue_head;
        scheduler->sleep_queue_head = task;
        if (task->next) {
            task->next->prev = task;
        }
    } else {
        // 插入到中间或末尾
        task->next = current;
        task->prev = prev;
        prev->next = task;
        if (current) {
            current->prev = task;
        }
    }

    scheduler->sleep_count++;
    // logger_debug("Added task '%s' to sleep queue (wake at tick %llu)\n",
    //              task->name,
    //              task->sleep_until_ticks);
}

// 从睡眠队列中移除任务
static void
remove_from_sleep_queue(task_t *task)
{
    if (!task)
        return;

    uint32_t cpu_id = task->cpu_id;
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];

    // 更新链表指针
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        // 这是队列头
        scheduler->sleep_queue_head = task->next;
    }

    if (task->next) {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;
    scheduler->sleep_count--;

    // logger_debug("Removed task '%s' from sleep queue\n", task->name);
}

// 检查并唤醒到期的睡眠任务
void
wake_up_sleeping_tasks(uint32_t cpu_id)
{
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler    = &g_task_manager.schedulers[cpu_id];
    uint64_t         current_tick = timer_get_system_ticks();

    task_t *current = scheduler->sleep_queue_head;
    while (current && current->sleep_until_ticks <= current_tick) {
        task_t *next = current->next;

        // 从睡眠队列移除
        remove_from_sleep_queue(current);

        // 添加到就绪队列
        scheduler_add_task(current);

        // logger_debug("Woke up task '%s' (slept until tick %llu, current tick %llu)\n",
        //              current->name,
        //              current->sleep_until_ticks,
        //              current_tick);

        current = next;
    }
}

// 任务睡眠函数
void
task_sleep(uint32_t ms)
{
    uint32_t cpu_id = get_current_cpu_id();
    if (cpu_id >= T_SMP_NUM)
        return;

    cpu_scheduler_t *scheduler = &g_task_manager.schedulers[cpu_id];
    task_t          *current   = scheduler->current_task;

    if (!current || current == scheduler->idle_task) {
        logger_warn("Cannot sleep: no current task or idle task\n");
        return;
    }

    // 计算唤醒时间（当前tick + 睡眠时间对应的tick数）
    uint64_t sleep_ticks       = (ms * TIMER_FREQUENCY_HZ) / 1000;
    uint64_t current_tick      = timer_get_system_ticks();
    current->sleep_until_ticks = current_tick + sleep_ticks;

    // logger_info("Task '%s' sleeping for %u ms (until tick %llu)\n",
    //             current->name,
    //             ms,
    //             current->sleep_until_ticks);

    // 添加到睡眠队列
    add_to_sleep_queue(current);

    // 选择下一个任务运行
    task_t *next = get_next_task(cpu_id);
    if (!next) {
        next = scheduler->idle_task;
    }

    if (next && next != current) {
        // 从就绪队列移除下一个任务（如果不是idle任务）
        if (next != scheduler->idle_task) {
            scheduler_remove_task(next);
        }

        next->state           = TASK_RUNNING;
        next->remaining_ticks = next->time_slice;
        next->last_scheduled  = timer_get_system_ticks();

        scheduler->current_task = next;
        scheduler->total_switches++;

        // logger_debug("CPU %u: Switching from sleeping '%s' to '%s'\n",
        //              cpu_id,
        //              current->name,
        //              next->name);

        // 执行上下文切换
        task_switch_context(&current->context, &next->context);
    } else {
        logger_warn("No task available after sleep, staying with current\n");
    }
}
