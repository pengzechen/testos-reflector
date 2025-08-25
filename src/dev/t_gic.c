

/*   ============= gic.c ================*/

#include "t_gicv2.h"
#include "t_types.h"
#include "t_mmio.h"
#include "lib/t_logger.h"

struct gic_t _gicv2;

void
gic_test_init(void)
{
    logger_info("\n============= GIC Initialization Test =============\n");

    uint32_t gicd_status = read32((void *) GICD_CTLR);
    uint32_t gicc_status = read32((void *) GICC_CTLR);

    if (gicd_status) {
        logger_info("GICD: ENABLED (0x%08x)\n", gicd_status);
    } else {
        logger_error("GICD: DISABLED (0x%08x)\n", gicd_status);
    }

    if (gicc_status) {
        logger_info("GICC: ENABLED (0x%08x)\n", gicc_status);
    } else {
        logger_error("GICC: DISABLED (0x%08x)\n", gicc_status);
    }

    logger_info("IRQ Numbers: %d\n", _gicv2.irq_nr);
    logger_info("CPU Count: %d\n", cpu_num());
    logger_info("============= GIC Test Complete =============\n\n");
}

// gicd g0, g1  gicc enable
void
gic_init(void)
{
    _gicv2.irq_nr = GICD_TYPER_IRQS(read32((void *) GICD_TYPER));
    if (_gicv2.irq_nr > 1020) {
        _gicv2.irq_nr = 1020;
    }

    write32(GICD_CTRL_ENABLE_GROUP0 | GICD_CTRL_ENABLE_GROUP1, (void *) GICD_CTLR);

    // 允许所有优先级的中断
    write32(0xff, (void *) GICC_PMR);
    write32(GICC_CTRL_ENABLE | (1 << 9), (void *) GICC_CTLR);
}

void
gicc_init()
{
    // 允许所有优先级的中断
    write32(0xff, (void *) GICC_PMR);
    write32(GICC_CTRL_ENABLE | (1 << 9), (void *) GICC_CTLR);
}

// get iar
uint32_t
gic_read_iar(void)
{
    return read32((void *) GICC_IAR);
}

// iar to vector
uint32_t
gic_iar_irqnr(uint32_t iar)
{
    return iar & GICC_IAR_INT_ID_MASK;
}

void
gic_write_eoir(uint32_t irqstat)
{
    write32(irqstat, (void *) GICC_EOIR);
}

void
gic_write_dir(uint32_t irqstat)
{
    write32(irqstat, (void *) GICC_DIR);
}


// 发送给特定的核（某个核）
void
gic_ipi_send_single(int irq, int cpu)
{
    // assert(cpu < 8);
    // assert(irq < 16);
    write32(1 << (cpu + 16) | irq, (void *) GICD_SGIR);
}

// The number of implemented CPU interfaces.
uint32_t
cpu_num(void)
{
    return GICD_TYPER_CPU_NUM(read32((void *) GICD_TYPER));
}


void
gic_enable_int(int vector, bool enabled)
{
    int reg  = vector >> 5;         // vector / 32
    int mask = 1 << (vector & 31);  // vector % 32

    if (enabled) {
        logger_info("GIC: Enable IRQ %d (reg=%d, bit=%d, mask=0x%08x)\n",
                    vector,
                    reg,
                    vector & 31,
                    mask);
        write32(mask, (void *) (uint64_t) GICD_ISENABLER(reg));
    } else {
        logger_info("GIC: Disable IRQ %d (reg=%d, bit=%d, mask=0x%08x)\n",
                    vector,
                    reg,
                    vector & 31,
                    mask);
        write32(mask, (void *) (uint64_t) GICD_ICENABLER(reg));
    }
}

// check the given interrupt.
int
gic_get_enable(int vector)
{
    int reg  = vector >> 5;                     //  vec / 32
    int mask = 1 << (vector & ((1 << 5) - 1));  //  vec % 32

    uint32_t val        = read32((void *) (uint64_t) GICD_ISENABLER(reg));
    bool     is_enabled = (val & mask) != 0;

    logger_debug("GIC: Check IRQ %d status: %s (reg=%d, bit=%d, reg_val=0x%08x)\n",
                 vector,
                 is_enabled ? "ENABLED" : "DISABLED",
                 reg,
                 vector & 31,
                 val);
    return is_enabled;
}

void
gic_set_isenabler(uint32_t n, uint32_t value)
{
    write32(value, (void *) (uint64_t) GICD_ISENABLER(n));
}

void
gic_set_ipriority(uint32_t n, uint32_t value)
{
    write32(value, (void *) (uint64_t) GICD_IPRIORITYR(n));
}

void
gic_set_icenabler(uint32_t n, uint32_t value)
{
    write32(value, (void *) (uint64_t) GICD_ICENABLER(n));
}