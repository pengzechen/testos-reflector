
#include "t_types.h"
#include "t_exception.h"
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

void
handle_sync_exception(uint64_t *stack_pointer)
{
    trap_frame_t *el1_ctx = (trap_frame_t *) stack_pointer;

    int el1_esr = read_esr_el1();

    int ec = ((el1_esr >> 26) & 0b111111);

    logger("el1 esr: %x\n", el1_esr);
    logger("ec: %x\n", ec);

    logger("This is handle_sync_exception: \n");
    for (int i = 0; i < 31; i++) {
        uint64_t value = el1_ctx->r[i];
        logger("General-purpose register: %d, value: %x\n", i, value);
    }

    uint64_t elr_el1_value = el1_ctx->elr;
    uint64_t usp_value     = el1_ctx->usp;
    uint64_t spsr_value    = el1_ctx->spsr;

    logger("usp: %x, elr: %x, spsr: %x\n", usp_value, elr_el1_value, spsr_value);

    logger_warn("wfi");
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