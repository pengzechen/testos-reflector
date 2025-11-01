#include "t_exception.h"

// task_switch(trap_frame_t *prev_ctx, trap_frame_t *next_ctx)
// 保存当前任务的上下文，恢复下一个任务的上下文
.global task_switch
.type task_switch, @function

task_switch:
    // x0 = prev_ctx (可能为 NULL，表示第一次切换)
    // x1 = next_ctx
    
    // 如果 prev_ctx 不为 NULL，保存当前上下文
    cbz x0, load_next_context
    
    // 保存 x0-x30
    stp x0, x1, [x0, #(8 * 0)]
    stp x2, x3, [x0, #(8 * 2)]
    stp x4, x5, [x0, #(8 * 4)]
    stp x6, x7, [x0, #(8 * 6)]
    stp x8, x9, [x0, #(8 * 8)]
    stp x10, x11, [x0, #(8 * 10)]
    stp x12, x13, [x0, #(8 * 12)]
    stp x14, x15, [x0, #(8 * 14)]
    stp x16, x17, [x0, #(8 * 16)]
    stp x18, x19, [x0, #(8 * 18)]
    stp x20, x21, [x0, #(8 * 20)]
    stp x22, x23, [x0, #(8 * 22)]
    stp x24, x25, [x0, #(8 * 24)]
    stp x26, x27, [x0, #(8 * 26)]
    stp x28, x29, [x0, #(8 * 28)]
    str x30, [x0, #(8 * 30)]
    
    // 保存 SP (usp)
    mov x2, sp
    str x2, [x0, #(8 * 31)]
    
    // 保存 ELR_EL1
    mrs x2, elr_el1
    str x2, [x0, #(8 * 32)]
    
    // 保存 SPSR_EL1
    mrs x2, spsr_el1
    str x2, [x0, #(8 * 33)]

load_next_context:
    // 恢复下一个任务的上下文 (x1 = next_ctx)
    
    // 恢复 x0-x30
    ldp x2, x3, [x1, #(8 * 2)]
    ldp x4, x5, [x1, #(8 * 4)]
    ldp x6, x7, [x1, #(8 * 6)]
    ldp x8, x9, [x1, #(8 * 8)]
    ldp x10, x11, [x1, #(8 * 10)]
    ldp x12, x13, [x1, #(8 * 12)]
    ldp x14, x15, [x1, #(8 * 14)]
    ldp x16, x17, [x1, #(8 * 16)]
    ldp x18, x19, [x1, #(8 * 18)]
    ldp x20, x21, [x1, #(8 * 20)]
    ldp x22, x23, [x1, #(8 * 22)]
    ldp x24, x25, [x1, #(8 * 24)]
    ldp x26, x27, [x1, #(8 * 26)]
    ldp x28, x29, [x1, #(8 * 28)]
    ldr x30, [x1, #(8 * 30)]
    
    // 恢复 SP (usp)
    ldr x0, [x1, #(8 * 31)]
    mov sp, x0
    
    // 恢复 ELR_EL1
    ldr x0, [x1, #(8 * 32)]
    msr elr_el1, x0
    
    // 恢复 SPSR_EL1
    ldr x0, [x1, #(8 * 33)]
    msr spsr_el1, x0
    
    // 最后恢复 x0 和 x1
    ldp x0, x1, [x1, #(8 * 0)]
    
    // 返回到新任务
    ret
