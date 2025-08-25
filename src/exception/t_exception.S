
/*
 * 保存所有寄存器到栈上
 * 栈布局与 trap_frame_t 结构体一致
 */
.macro SAVE_REGS
    sub     sp, sp, 34 * 8          // 为 trap_frame_t 分配栈空间

    // 保存通用寄存器 x0-x29
    stp     x0, x1,   [sp, 0 * 8]
    stp     x2, x3,   [sp, 2 * 8]
    stp     x4, x5,   [sp, 4 * 8]
    stp     x6, x7,   [sp, 6 * 8]
    stp     x8, x9,   [sp, 8 * 8]
    stp     x10, x11, [sp, 10 * 8]
    stp     x12, x13, [sp, 12 * 8]
    stp     x14, x15, [sp, 14 * 8]
    stp     x16, x17, [sp, 16 * 8]
    stp     x18, x19, [sp, 18 * 8]
    stp     x20, x21, [sp, 20 * 8]
    stp     x22, x23, [sp, 22 * 8]
    stp     x24, x25, [sp, 24 * 8]
    stp     x26, x27, [sp, 26 * 8]
    stp     x28, x29, [sp, 28 * 8]

    // 保存系统寄存器
    mrs     x9, sp_el0              // 用户栈指针
    mrs     x10, elr_el1            // 异常链接寄存器
    mrs     x11, spsr_el1           // 保存的处理器状态寄存器
    stp     x30, x9,  [sp, 30 * 8]  // x30 和 sp_el0
    stp     x10, x11, [sp, 32 * 8]  // elr_el1 和 spsr_el1
.endm

/*
 * 从栈上恢复所有寄存器
 */
.macro RESTORE_REGS
    // 恢复系统寄存器
    ldp     x10, x11, [sp, 32 * 8]  // elr_el1 和 spsr_el1
    ldp     x30, x9,  [sp, 30 * 8]  // x30 和 sp_el0
    msr     sp_el0, x9
    msr     elr_el1, x10
    msr     spsr_el1, x11

    // 恢复通用寄存器 x0-x29
    ldp     x28, x29, [sp, 28 * 8]
    ldp     x26, x27, [sp, 26 * 8]
    ldp     x24, x25, [sp, 24 * 8]
    ldp     x22, x23, [sp, 22 * 8]
    ldp     x20, x21, [sp, 20 * 8]
    ldp     x18, x19, [sp, 18 * 8]
    ldp     x16, x17, [sp, 16 * 8]
    ldp     x14, x15, [sp, 14 * 8]
    ldp     x12, x13, [sp, 12 * 8]
    ldp     x10, x11, [sp, 10 * 8]
    ldp     x8, x9,   [sp, 8 * 8]
    ldp     x6, x7,   [sp, 6 * 8]
    ldp     x4, x5,   [sp, 4 * 8]
    ldp     x2, x3,   [sp, 2 * 8]
    ldp     x0, x1,   [sp, 0 * 8]

    add     sp, sp, 34 * 8          // 释放栈空间
.endm

/*
 * 无效异常处理宏
 */
.macro INVALID_EXCP, kind, source
.p2align 7
    SAVE_REGS
    mov     x0, sp                  // 传递栈指针作为第一个参数
    mov     x1, \kind               // 异常类型
    mov     x2, \source             // 异常源
    bl      invalid_exception
    b       .Lexception_return
.endm

/*
 * 同步异常处理宏
 */
.macro HANDLE_SYNC
.p2align 7
    SAVE_REGS
    mov     x0, sp                  // 传递栈指针作为参数
    bl      handle_sync_exception
    b       .Lexception_return
.endm

/*
 * 中断异常处理宏
 */
.macro HANDLE_IRQ
.p2align 7
    SAVE_REGS
    mov     x0, sp                  // 传递栈指针作为参数
    bl      handle_irq_exception
    b       .Lexception_return
.endm

/*
 * 异常向量表
 * ARM64 异常向量表必须 2KB 对齐
 */
.section .text
.p2align 11

.extern invalid_exception
.extern handle_sync_exception
.extern handle_irq_exception

// 全局符号声明
.global exception_vector_base
.global exception_return
.global curr_el_sp0_sync
.global curr_el_sp0_irq
.global curr_el_sp0_fiq
.global curr_el_sp0_serror
.global curr_el_spx_sync
.global curr_el_spx_irq
.global curr_el_spx_fiq
.global curr_el_spx_serror
.global lower_el_aarch64_sync
.global lower_el_aarch64_irq
.global lower_el_aarch64_fiq
.global lower_el_aarch64_serror
.global lower_el_aarch32_sync
.global lower_el_aarch32_irq
.global lower_el_aarch32_fiq
.global lower_el_aarch32_serror

/*
 * ARM64 异常向量表
 * 每个入口占用 128 字节 (0x80)
 * 总共 16 个入口，分为 4 组：
 * 1. Current EL with SP_EL0
 * 2. Current EL with SP_ELx
 * 3. Lower EL (AArch64)
 * 4. Lower EL (AArch32)
 */
exception_vector_base:

/*=============================================================================
 * Current EL with SP_EL0 (通常不会使用)
 *============================================================================*/
curr_el_sp0_sync:
    INVALID_EXCP 0 0                // 同步异常

curr_el_sp0_irq:
    INVALID_EXCP 1 0                // IRQ

curr_el_sp0_fiq:
    INVALID_EXCP 2 0                // FIQ

curr_el_sp0_serror:
    INVALID_EXCP 3 0                // SError

/*=============================================================================
 * Current EL with SP_ELx (内核异常)
 *============================================================================*/
curr_el_spx_sync:
    HANDLE_SYNC                     // 同步异常

curr_el_spx_irq:
    HANDLE_IRQ                      // IRQ

curr_el_spx_fiq:
    INVALID_EXCP 2 1                // FIQ

curr_el_spx_serror:
    INVALID_EXCP 3 1                // SError

/*=============================================================================
 * Lower EL (AArch64) - 用户空间异常
 *============================================================================*/
lower_el_aarch64_sync:
    HANDLE_SYNC                     // 同步异常

lower_el_aarch64_irq:
    HANDLE_IRQ                      // IRQ

lower_el_aarch64_fiq:
    INVALID_EXCP 2 2                // FIQ

lower_el_aarch64_serror:
    INVALID_EXCP 3 2                // SError

/*=============================================================================
 * Lower EL (AArch32) - 32位用户空间异常
 *============================================================================*/
lower_el_aarch32_sync:
    INVALID_EXCP 0 3                // 同步异常

lower_el_aarch32_irq:
    INVALID_EXCP 1 3                // IRQ

lower_el_aarch32_fiq:
    INVALID_EXCP 2 3                // FIQ

lower_el_aarch32_serror:
    INVALID_EXCP 3 3                // SError

/*=============================================================================
 * 异常返回处理
 *============================================================================*/

/*
 * 本地异常返回标签
 * 用于异常处理完成后返回
 */
.Lexception_return:
    RESTORE_REGS
    eret

/*
 * 全局异常返回标签
 * 供任务切换使用，从任务栈恢复上下文并返回到任务
 */
exception_return:
    RESTORE_REGS
    eret

