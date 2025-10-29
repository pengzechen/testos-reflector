#include "mem/mmu.h"
#include "mem/tcr.h"
#include "cfg/t_cfg.h"

/*==============================================================================
 * 全局符号和外部函数声明
 *============================================================================*/
.section .text
.global _start
.global _t_stack_top
.global _t_stack_top_second
.global t_second_entry

.extern set_pg_base
.extern init_page_table
.extern t_kernel_main
.extern t_secondary_main

/*==============================================================================
 * 主CPU启动入口 - 从EL2跳转到EL1
 *============================================================================*/
_start:
    // 配置EL2到EL1的切换
    bl      setup_el2_to_el1
    
    // 设置主CPU栈指针
    ldr     x0, =_t_stack_top
    msr     sp_el1, x0

    // 设置EL1入口地址并跳转
    ldr     x0, =el1_primary_start
    msr     elr_el2, x0
    eret


/*==============================================================================
 * 主CPU在EL1的启动流程
 *============================================================================*/
el1_primary_start:
    // 早期UART调试输出 "Hi\r\n"
    bl      early_uart_init
    
    // 禁用中断
    msr     daifset, #2

    // 配置EL1环境
    bl      setup_el1_env
    
    // 初始化页表和MMU
    bl      init_page_table
    bl      set_pg_base
    bl      init_mmu

    // 清空BSS段
    bl      clear_bss
    
    // 获取CPU ID
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    
    // 跳转到C语言主函数
    adrp    x1, t_kernel_main
    add     x1, x1, :lo12:t_kernel_main
    blr     x1

    // 主函数不应返回，死循环
1:  b       1b


/*==============================================================================
 * 从核启动入口 - 从EL2跳转到EL1
 *============================================================================*/
t_second_entry:
    // x0: core_id (从smc_call传入)
    
    // 保存core_id
    mov     x19, x0
    
    // 计算从核栈地址: _t_stack_top_second - (core_id * T_STACK_SIZE)
    adrp    x1, _t_stack_top_second
    add     x1, x1, :lo12:_t_stack_top_second
    sub     x0, x0, #1
    mov     x2, #T_STACK_SIZE
    mul     x3, x0, x2
    sub     x1, x1, x3
    msr     sp_el1, x1

    // 配置EL2到EL1的切换
    bl      setup_el2_to_el1
    
    // 设置EL1入口地址并跳转
    ldr     x0, =el1_secondary_start
    msr     elr_el2, x0
    eret


/*==============================================================================
 * 从核在EL1的启动流程
 *============================================================================*/
el1_secondary_start:
    // 禁用中断
    msr     daifset, #2

    // 配置EL1环境
    bl      setup_el1_env
    
    // 初始化MMU（复用主CPU的页表）
    bl      set_pg_base
    bl      init_mmu

    // 恢复core_id并跳转到C语言从核主函数
    mov     x0, x19
    adrp    x1, t_secondary_main
    add     x1, x1, :lo12:t_secondary_main
    blr     x1

    // 从核主函数不应返回，死循环
1:  b       1b


/*==============================================================================
 * 辅助函数：配置EL2到EL1的切换
 *============================================================================*/
setup_el2_to_el1:
    // 设置SPSR_EL2: EL1h模式，禁用所有中断
    mov     x0, #(0b0101 << 0)           // M[4:0] = 0b0101 (EL1h)
    orr     x0, x0, #(0b1111 << 6)       // DAIF[3:0] = 0b1111 (禁用中断)
    msr     spsr_el2, x0

    // 设置HCR_EL2: 低级别EL使用AArch64
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)           // RW = 1
    msr     hcr_el2, x0
    isb
    
    ret


/*==============================================================================
 * 辅助函数：配置EL1环境
 *============================================================================*/
setup_el1_env:
    // 使能浮点和SIMD
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)       // FPEN[21:20] = 0b11
    msr     cpacr_el1, x0
    isb

    // 设置异常向量表基地址
    adrp    x0, exception_vector_base
    add     x0, x0, :lo12:exception_vector_base
    msr     vbar_el1, x0
    dsb     sy
    isb
    
    ret


/*==============================================================================
 * 辅助函数：早期UART初始化和调试输出
 *============================================================================*/
early_uart_init:
    // UART0基地址: UART_BASE (from t_cfg.h)
    ldr     x9, =UART_BASE
    
    // 输出 "Hi\r\n"
    mov     x10, #72                // 'H'
    str     x10, [x9]
    mov     x10, #105               // 'i'
    str     x10, [x9]
    mov     x10, #13                // '\r'
    str     x10, [x9]
    mov     x10, #10                // '\n'
    str     x10, [x9]
    
    ret


/*==============================================================================
 * 辅助函数：清空BSS段
 *============================================================================*/
clear_bss:
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
    mov     x3, #0
1:
    cmp     x1, x2
    b.ge    2f
    str     x3, [x1], #8
    b       1b
2:
    ret


/*==============================================================================
 * MMU初始化函数
 *============================================================================*/
init_mmu:
    // 1. 先禁用MMU和缓存
    mrs     x1, sctlr_el1
    bic     x1, x1, #(1 << 0)        // M = 0 (禁用MMU)
    bic     x1, x1, #(1 << 2)        // C = 0 (禁用数据缓存)
    bic     x1, x1, #(1 << 12)       // I = 0 (禁用指令缓存)
    msr     sctlr_el1, x1
    isb

    // 2. 配置内存属性表MAIR_EL1
    ldr     x1, =(MAIR_VALUE)
    msr     mair_el1, x1
    isb

    // 3. 配置TCR_EL1 (地址转换控制)
    ldr     x1, =(TCR_EL1)
    msr     tcr_el1, x1
    isb

    // 4. 无效化TLB
    tlbi    vmalle1
    dsb     sy
    isb

    // 5. 使能MMU和缓存
    mrs     x1, sctlr_el1
    orr     x1, x1, #(1 << 0)        // M = 1 (使能MMU)
    orr     x1, x1, #(1 << 2)        // C = 1 (使能数据缓存)
    orr     x1, x1, #(1 << 12)       // I = 1 (使能指令缓存)
    msr     sctlr_el1, x1
    isb

    ret


/*==============================================================================
 * BSS段 - 栈空间分配
 *============================================================================*/
.section .bss
.align 14

// 主CPU栈空间
_t_stack_bottom:
    .skip   T_STACK_SIZE
_t_stack_top:

// 从核栈空间
.align 14
_t_stack_bottom_second:
    .skip   T_STACK_SIZE * (T_SMP_NUM - 1)
_t_stack_top_second:
