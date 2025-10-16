#include "t_gicv3.h"
#include "t_types.h"
#include "t_mmio.h"
#include "lib/t_logger.h"

struct gicv3_t _gicv3;

// 支持 GICv3 系统寄存器访问
#define WRITE_SYSREG(reg, val)                                                                     \
    do {                                                                                           \
        if (__builtin_strcmp(reg, ICC_SRE_EL1) == 0) {                                             \
            asm volatile("msr S3_0_C12_C12_5, %0" ::"r"(val));                                     \
        } else if (__builtin_strcmp(reg, ICC_PMR_EL1) == 0) {                                      \
            asm volatile("msr S3_0_C4_C6_0, %0" ::"r"(val));                                       \
        } else if (__builtin_strcmp(reg, ICC_EOIR1_EL1) == 0) {                                    \
            asm volatile("msr S3_0_C12_C12_1, %0" ::"r"(val));                                     \
        } else if (__builtin_strcmp(reg, ICC_CTLR_EL1) == 0) {                                     \
            asm volatile("msr S3_0_C12_C12_4, %0" ::"r"(val));                                     \
        } else if (__builtin_strcmp(reg, ICC_IGRPEN1_EL1) == 0) {                                  \
            asm volatile("msr S3_0_C12_C12_7, %0" ::"r"(val));                                     \
        }                                                                                          \
    } while (0)

#define READ_SYSREG(reg, outval)                                                                   \
    do {                                                                                           \
        if (__builtin_strcmp(reg, ICC_IAR1_EL1) == 0) {                                            \
            asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(outval));                                 \
        }                                                                                          \
    } while (0)

static inline void
write_sysreg(const char *reg, uint64_t val)
{
    WRITE_SYSREG(reg, val);
}

static inline uint64_t
read_sysreg(const char *reg)
{
    uint64_t val = 0;
    READ_SYSREG(reg, val);
    return val;
}

void gicv3_init(void)
{
    logger_info("GICv3: Initializing...\n");

    // ---- Distributor ----
    // 所有 SPI 设为 Group1NS
    write32(0xFFFFFFFF, (void *)(GICD_BASE_ADDR + 0x80)); // GICD_IGROUPR0 etc.
    // 启用 Group1NS
    write32((1 << 1), (void *) GICD_CTLR);

    
    // ---- Redistributor ----
    uint32_t val = read32((void *) GICR_WAKER);
    val &= ~(1 << 1);                         // Clear ProcessorSleep
    write32(val, (void *) GICR_WAKER);
    while (read32((void *) GICR_WAKER) & (1 << 2))
        ;

    // ---- CPU interface ----
    write_sysreg(ICC_SRE_EL1, 0x7);   // 允许 System Register 接口
    write_sysreg(ICC_CTLR_EL1, 0x0);
    write_sysreg(ICC_PMR_EL1, 0xFF);  // 允许所有优先级
    write_sysreg(ICC_IGRPEN1_EL1, 0x1);

    logger_info("GICv3: Init done\n");
}

void
gicv3_enable_int(int int_id, bool enable)
{
    uint32_t cpu_id = 0;  // TODO: 多核时需获取当前 CPU 核心 ID

    uint32_t mask = 1u << (int_id % 32);

    logger_info("enable_int: int_id=%d, enable=%d\n", int_id, enable);

    if (int_id < 32) {
        // SGI/PPI (per-core)
        if (enable)
            write32(mask, (void *) GICR_ISENABLER0(cpu_id));
        else
            write32(mask, (void *) GICR_ICENABLER0(cpu_id));
    } else {
        // SPI (shared)
        uint32_t reg = int_id / 32;
        if (enable)
            write32(mask, (void *) GICD_ISENABLERn(reg));
        else
            write32(mask, (void *) GICD_ICENABLERn(reg));
    }
}


uint32_t
gicv3_read_iar(void)
{
    return (uint32_t) read_sysreg(ICC_IAR1_EL1);
}

uint32_t
gicv3_iar_irqnr(uint32_t iar)
{
    return iar & ICC_IAR_INTID_MASK;
}

void
gicv3_write_eoir(uint32_t irqstat)
{
    write_sysreg(ICC_EOIR1_EL1, irqstat);
}
