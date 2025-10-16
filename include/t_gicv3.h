#ifndef __GICV3_H__
#define __GICV3_H__

#include "t_types.h"
#include "t_sysreg.h"
#include "cfg/t_cfg.h"

// GICv3 Distributor base
#define GICD_CTLR           (GICD_BASE_ADDR + 0x0000)
#define GICD_TYPER          (GICD_BASE_ADDR + 0x0004)
#define GICD_IIDR           (GICD_BASE_ADDR + 0x0008)
#define GICD_ISENABLER(n)   (GICD_BASE_ADDR + 0x100 + 4 * (n))
#define GICD_ICENABLER(n)   (GICD_BASE_ADDR + 0x180 + 4 * (n))
#define GICD_IPRIORITYR(n)  (GICD_BASE_ADDR + 0x400 + 4 * (n))
#define GICD_ICFGR(n)       (GICD_BASE_ADDR + 0xc00 + 4 * (n))

// GICv3 Redistributor base (需根据平台定义)
#define GICR_CTLR           (GICR_BASE_ADDR + 0x0000)
#define GICR_WAKER          (GICR_BASE_ADDR + 0x0014)
#define GICR_IPRIORITYR(n)  (GICR_BASE_ADDR + 0x0400 + 4 * (n))
#define GICR_SGI_BASE(cpu)   (GICR_BASE_ADDR + 0x20000 * (cpu))
#define GICR_ISENABLER0(cpu) (GICR_SGI_BASE(cpu) + 0x10000 + 0x100)
#define GICR_ICENABLER0(cpu) (GICR_SGI_BASE(cpu) + 0x10000 + 0x180)
#define GICD_ISENABLERn(n)   (GICD_BASE_ADDR + 0x100 + (n) * 4)
#define GICD_ICENABLERn(n)   (GICD_BASE_ADDR + 0x180 + (n) * 4)

// System register interface
#define ICC_SRE_EL1         "S3_0_C12_C12_5"
#define ICC_PMR_EL1         "S3_0_C4_C6_0"
#define ICC_IAR1_EL1        "S3_0_C12_C12_0"
#define ICC_EOIR1_EL1       "S3_0_C12_C12_1"
#define ICC_CTLR_EL1        "S3_0_C12_C12_4"
#define ICC_IGRPEN1_EL1     "S3_0_C12_C12_7"

#define ICC_IAR_INTID_MASK   0xFFFFFFu

typedef struct gicv3_t {
    unsigned int irq_nr;
} gicv3_t;

extern struct gicv3_t _gicv3;

void gicv3_init(void);
void gicv3_enable_int(int vector, bool enable);
void gicv3_write_eoir(uint32_t irqstat);
uint32_t gicv3_read_iar(void);

#endif // __GICV3_H__
