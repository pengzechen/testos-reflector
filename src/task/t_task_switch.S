.section .text
.global task_switch_context

/*
 * 任务上下文切换函数 - 重构版本
 * void task_switch_context(task_ctx_t *old_ctx, task_ctx_t *new_ctx)
 *
 * 重构思路：
 * 1. 如果有旧任务，保存当前寄存器状态到 old_ctx
 * 2. 切换到新任务的栈 (new_ctx->sp_elx)
 * 3. 设置 x30 为 exception_return
 * 4. 通过 ret 跳转到 exception_return，然后 eret 进入任务
 *
 * 参数:
 *   x0: old_ctx - 指向旧任务上下文的指针 (task_ctx_t*)
 *   x1: new_ctx - 指向新任务上下文的指针 (task_ctx_t*)
 *
 * task_ctx_t 结构体布局:
 *   offset 0x00: r[0-30]  - 通用寄存器 x0-x30 (31 * 8 = 248 bytes)
 *   offset 0xF8: sp_elx   - 栈指针 (8 bytes)
 *   offset 0x100: tpidr   - 线程指针 (8 bytes)
 */

task_switch_context:
    // 如果 old_ctx 为 NULL，跳过保存
    cbz x0, restore_context

    // 保存旧任务的上下文到 old_ctx
save_context:
    // 保存通用寄存器 x0-x30
    stp x0, x1,   [x0, #0x00]    // 保存 x0, x1
    stp x2, x3,   [x0, #0x10]    // 保存 x2, x3
    stp x4, x5,   [x0, #0x20]    // 保存 x4, x5
    stp x6, x7,   [x0, #0x30]    // 保存 x6, x7
    stp x8, x9,   [x0, #0x40]    // 保存 x8, x9
    stp x10, x11, [x0, #0x50]    // 保存 x10, x11
    stp x12, x13, [x0, #0x60]    // 保存 x12, x13
    stp x14, x15, [x0, #0x70]    // 保存 x14, x15
    stp x16, x17, [x0, #0x80]    // 保存 x16, x17
    stp x18, x19, [x0, #0x90]    // 保存 x18, x19
    stp x20, x21, [x0, #0xA0]    // 保存 x20, x21
    stp x22, x23, [x0, #0xB0]    // 保存 x22, x23
    stp x24, x25, [x0, #0xC0]    // 保存 x24, x25
    stp x26, x27, [x0, #0xD0]    // 保存 x26, x27
    stp x28, x29, [x0, #0xE0]    // 保存 x28, x29
    str x30,      [x0, #0xF0]    // 保存 x30 (链接寄存器)

    // 保存当前栈指针到 sp_elx 字段
    mov x2, sp
    str x2, [x0, #0xF8]          // 保存栈指针到 sp_elx

    // 保存 tpidr (如果需要的话)
    // mrs x2, tpidr_el1
    // str x2, [x0, #0x100]       // 保存 tpidr


restore_context:
    // 恢复新任务的上下文从 new_ctx
    // 如果 new_ctx 为 NULL，直接返回
    cbz x1, switch_return_point

    // 保存 new_ctx 指针，因为 x1 会被覆盖
    mov x9, x1                   // x9 = new_ctx 指针

    // 切换到目标任务的栈
    ldr x2, [x9, #0xF8]          // 加载目标任务的栈指针 (sp_elx)
    mov sp, x2                   // 切换栈指针

    // 先加载 x30，因为我们需要通过它跳转
    ldr x30, [x9, #0xF0]         // 加载 x30 (应该指向 exception_return)

    // 恢复寄存器 x0-x8
    ldp x0, x1,   [x9, #0x00]    // 加载 x0, x1
    ldp x2, x3,   [x9, #0x10]    // 加载 x2, x3
    ldp x4, x5,   [x9, #0x20]    // 加载 x4, x5
    ldp x6, x7,   [x9, #0x30]    // 加载 x6, x7
    ldr x8,       [x9, #0x40]    // 加载 x8

    // 恢复寄存器 x10-x29 (跳过x9，最后处理)
    ldp x10, x11, [x9, #0x50]    // 加载 x10, x11
    ldp x12, x13, [x9, #0x60]    // 加载 x12, x13
    ldp x14, x15, [x9, #0x70]    // 加载 x14, x15
    ldp x16, x17, [x9, #0x80]    // 加载 x16, x17
    ldp x18, x19, [x9, #0x90]    // 加载 x18, x19
    ldp x20, x21, [x9, #0xA0]    // 加载 x20, x21
    ldp x22, x23, [x9, #0xB0]    // 加载 x22, x23
    ldp x24, x25, [x9, #0xC0]    // 加载 x24, x25
    ldp x26, x27, [x9, #0xD0]    // 加载 x26, x27
    ldp x28, x29, [x9, #0xE0]    // 加载 x28, x29

    // 最后恢复 x9
    ldr x9,       [x9, #0x48]    // 加载 x9 (注意：这必须是最后一步)

    // 现在通过 ret 跳转到 x30 指向的地址 (exception_return)
    // exception_return 会执行 RESTORE_REGS 和 eret，进入任务
    ret

switch_return_point:
    // 任务切换完成后的返回点
    ret

