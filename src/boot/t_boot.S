.section .text
.global _start

.global _t_stack_top
.global _t_stack_top_second
.global t_second_entry

// 从el3跳转到el1
_start:

    // 设置 SPSR_EL2 寄存器
    // 将目标处理器状态设置为 EL1h 模式
    mov x0,     #(0b0101 << 0)   // 设置 M[4:0] 为 0b0101，即 EL1h 模式
    orr x0, x0, #(0b1111 << 6)   // 设置 DAIF[3:0] 为 0b1111，禁用所有中断
    msr spsr_el2, x0

    mrs x0, hcr_el2
    orr x0, x0, #(1UL << 31)   // RW = 1 -> lower ELs are AArch64
    msr hcr_el2, x0
    isb

    // 设置 ELR_EL3 寄存器
    // 设置返回地址为 EL1 的入口点
    ldr x0, =el1_start
    msr elr_el2, x0

    // 设置 SP_EL1 寄存器
    // 设置 EL1 的堆栈指针
    ldr x0, =_t_stack_top
    msr sp_el1, x0

    // 使用 ERET 指令返回到 EL1
    eret


el1_start:

    // Uart0 = 0xfeb50000
    mov x9, #0x0
    movk x9, #0xfeb5, lsl #16
    lsr x19, x19, #8
    // ID Hi
    mov x10, #72
    str x10, [x9]
    mov x10, #105
    str x10, [x9]
    mov x10, #13      // '\r'
    str x10, [x9]
    mov x10, #10
    str x10, [x9]

    msr daifset, #2   // 关闭所有中断

    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)    // set FPEN[21:20] = 0b11
    msr     cpacr_el1, x0
    isb

    // 关闭
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 2)   // C bit (D-cache disable)
    bic x0, x0, #(1 << 12)  // I bit (I-cache disable)
    msr sctlr_el1, x0
    isb

    adrp    x0, exception_vector_base
    add     x0, x0, :lo12:exception_vector_base
    // ldr x0, =exception_vector_base
    msr     vbar_el1, x0
    dsb     sy      
    isb        

    // 清空bss
    bl t_clear_bsss
    
    mrs     x0, mpidr_el1     // 读取 MPIDR_EL1
    and     x0, x0, #0xFF     // 提取 Affinity level 0（即 CPU ID）
    
    // 调用 C 语言的 main 函数
    adrp x1, t_kernel_main
    add  x1, x1, :lo12:t_kernel_main
    blr  x1

    // 死循环，防止返回
    2:  b 2b


t_clear_bsss:
    ldr x1, =__bss_start
    ldr x2, =__bss_end
    mov x3, #0
0:
    cmp x1, x2
    b.ge 1f
    str x3, [x1], #8
    b 0b
1:
    ret


// todo: 逻辑和上面相同可以优化
t_second_entry:

    // x0: core_id（从 smc_call 传过来的）
    // 先计算该核的 stack 高地址
    adrp    x1, _t_stack_top_second        // 基地址
    add     x1, x1, :lo12:_t_stack_top_second
    // 假设每个核的 stack 大小是 0x4000
    mov     x2, 0x4000
    mul     x3, x0, x2                     // x0 = core_id
    sub     x1, x1, x3                     // 高地址 - 栈偏移
    
    msr     sp_el1, x1                     // 设置 EL1 栈指针
    mov     x19, x0


    // 设置 SPSR_EL2 寄存器
    // 将目标处理器状态设置为 EL1h 模式
    mov x0,     #(0b0101 << 0)   // 设置 M[4:0] 为 0b0101，即 EL1h 模式
    orr x0, x0, #(0b1111 << 6)   // 设置 DAIF[3:0] 为 0b1111，禁用所有中断
    msr spsr_el2, x0

    mrs x0, hcr_el2
    orr x0, x0, #(1UL << 31)   // RW = 1 -> lower ELs are AArch64
    msr hcr_el2, x0
    isb

    // 设置 ELR_EL3 寄存器
    // 设置返回地址为 EL1 的入口点
    ldr x0, =el1_second_start
    msr elr_el2, x0

    
    // 使用 ERET 指令返回到 EL1
    eret

el1_second_start:

    msr daifset, #2   // 关闭所有中断

    adrp    x0, exception_vector_base
    add     x0, x0, :lo12:exception_vector_base
    msr     vbar_el1, x0
    dsb     sy     
    isb             

    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)    // set FPEN[21:20] = 0b11
    msr     cpacr_el1, x0
    isb

    adrp x1, t_secondary_main
    add  x1, x1, :lo12:t_secondary_main
    mov  x0, x19  //todo: core id
    blr  x1

    b .



// 栈空间
.section .bss
.align 14

_t_stack_bottom:
    .skip 0x4000  // 4KB 的栈空间
_t_stack_top:


.align 14
_t_stack_bottom_second:
    .skip 0x4000 * 8 // 每个核 16KB 的栈空间
_t_stack_top_second: