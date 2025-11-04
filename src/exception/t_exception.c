
#include "t_types.h"
#include "t_exception.h"
#include "t_syscall.h"
#include "dev/t_gicv3.h"
#include "cfg/t_cfg.h"
#include "dev/t_timer.h"
#include "lib/t_logger.h"

irq_handler_t g_handler_vec[512] = {0};

// 调度标志位，用于延迟调度
volatile int need_schedule_flag = 0;

void
irq_install(int vector, void (*h)(uint64_t *))
{
    g_handler_vec[vector] = h;
}

static inline uint64_t read_far_el1(void) {
    uint64_t val;
    __asm__ volatile (
        "mrs %0, far_el1"   // 将 FAR_EL1 寄存器的值读到 val
        : "=r"(val)         // 输出操作数
        :                   // 无输入操作数
        :                   // 无破坏的寄存器
    );
    return val;
}

void
handle_sync_exception(uint64_t *stack_pointer)
{
    trap_frame_t *el1_ctx = (trap_frame_t *) stack_pointer;

    int el1_esr = read_esr_el1();

    int ec = ((el1_esr >> 26) & 0b111111);

    // EC = 0x15 (21) 表示 SVC (Supervisor Call) 系统调用
    if (ec == 0x15) {
        // 系统调用处理
        // x8 = 系统调用号, x0-x5 = 参数
        uint64_t syscall_num = el1_ctx->r[8];
        uint64_t arg0 = el1_ctx->r[0];
        uint64_t arg1 = el1_ctx->r[1];
        uint64_t arg2 = el1_ctx->r[2];
        uint64_t arg3 = el1_ctx->r[3];
        uint64_t arg4 = el1_ctx->r[4];
        uint64_t arg5 = el1_ctx->r[5];

        // uint64_t sp_before = el1_ctx->usp;
        // logger("  [SYSCALL_DEBUG] Before: ELR=0x%lx, x30=0x%lx, SP=0x%lx\n", 
        //        el1_ctx->elr, el1_ctx->r[30], sp_before);

        // 调用系统调用处理函数
        uint64_t ret = handle_syscall(syscall_num, arg0, arg1, arg2, arg3, arg4, arg5);

        // 将返回值放入 x0
        el1_ctx->r[0] = ret;

        // ARM64 架构：对于 SVC 指令，ELR 应该指向 SVC 指令本身
        // 但是实际测试发现 ELR 已经指向了下一条指令
        // 所以我们再 +4 会跳过一条指令！
        // 暂时注释掉这行，看看是否解决问题
        // el1_ctx->elr += 4;

        // uint64_t sp_after = el1_ctx->usp;
        // logger("  [SYSCALL_DEBUG] After: ELR=0x%lx, x30=0x%lx, SP=0x%lx\n", 
        //        el1_ctx->elr, el1_ctx->r[30], sp_after);
        
        // if (sp_before != sp_after) {
        //     logger_warn("  [SYSCALL_DEBUG] WARNING: Stack pointer changed!\n");
        // }

        return;
    }

    // 其他异常的处理
    logger("el1 esr: %x\n", el1_esr);
    logger("ec: %x\n", ec);
    logger("far_el1: %x\n", read_far_el1());

    logger("This is handle_sync_exception: \n");
    for (int i = 0; i < 31; i++) {
        uint64_t value = el1_ctx->r[i];
        logger("General-purpose register: %d, value: %x\n", i, value);
    }

    uint64_t elr_el1_value = el1_ctx->elr;
    uint64_t usp_value     = el1_ctx->usp;
    uint64_t spsr_value    = el1_ctx->spsr;

    logger("usp: %x, elr: %x, spsr: %x\n", usp_value, elr_el1_value, spsr_value);

    logger_warn("wfi\n");
    while (1)
        WFI();
}

void
handle_irq_exception(uint64_t *stack_pointer)
{
    trap_frame_t *el1_ctx = (trap_frame_t *) stack_pointer;

    uint64_t x1_value     = el1_ctx->r[1];
    uint64_t sp_el0_value = el1_ctx->usp;
    (void) x1_value;
    (void) sp_el0_value;

    uint32_t iar    = gicv3_read_iar();
    uint32_t vector = gicv3_iar_irqnr(iar);

    // 调用中断处理函数
    if (g_handler_vec[vector] != NULL) {
        g_handler_vec[vector]((uint64_t *) el1_ctx);
    }

    // 先完成中断确认，确保GIC知道中断已处理完毕
    gicv3_write_eoir(iar);
    // gicv3_write_dir(iar);

    // 检查是否需要调度
    if (need_schedule_flag) {
        need_schedule_flag = 0;
    }
}

void
invalid_exception(uint64_t *stack_pointer, uint64_t kind, uint64_t source)
{
    trap_frame_t *el1_ctx  = (trap_frame_t *) stack_pointer;
    uint64_t      x0_value = el1_ctx->r[0];

    (void) x0_value;
    (void) kind;
    (void) source;
}