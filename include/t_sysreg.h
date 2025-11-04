#ifndef __T_SYSREG_H__
#define __T_SYSREG_H__

#include "t_types.h"

/*
 * AArch64 System Register Access Macros
 *
 * This header provides unified access to system registers actually used in the project.
 * Only includes registers that are currently being used to keep the file minimal.
 */

/* ========================================================================
 * CPU Identification Registers
 * ======================================================================== */

// MPIDR_EL1 - Multiprocessor Affinity Register
#define READ_MPIDR_EL1()                                                                           \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(val));                                     \
        val;                                                                                       \
    })

// Get current CPU ID (simplified)
static inline uint32_t
get_current_cpu_id(void)
{
    return (uint32_t) ((READ_MPIDR_EL1() >> 8) & 0xFF);
}

/* 返回当前 EL（0..3） */
#define READ_CURRENTEL() ({                        \
    unsigned long __val;                           \
    __asm__ __volatile__("mrs %0, CurrentEL"      \
                         : "=r"(__val));           \
    (unsigned)((__val >> 2) & 0x3);                \
})

/* ========================================================================
 * Exception and Interrupt Control Registers
 * ======================================================================== */

// DAIF - Debug, SError, IRQ, FIQ mask bits
#define READ_DAIF()                                                                                \
    ({                                                                                             \
        uint32_t val;                                                                              \
        __asm__ __volatile__("mrs %0, daif" : "=r"(val));                                          \
        val;                                                                                       \
    })

// DAIF control - Set/Clear specific bits
#define DAIF_SET(mask) __asm__ __volatile__("msr daifset, %0" : : "i"(mask) : "memory")

#define DAIF_CLR(mask) __asm__ __volatile__("msr daifclr, %0" : : "i"(mask) : "memory")

// Convenience functions for interrupt control
static inline void
enable_interrupts(void)
{
    DAIF_CLR(2);  // Clear IRQ mask
}

static inline void
disable_interrupts(void)
{
    DAIF_SET(2);  // Set IRQ mask
}

static inline uint32_t
get_daif(void)
{
    return READ_DAIF();
}

static inline uint32_t
get_current_el(void)
{
    uint32_t val;
    __asm__ __volatile__("mrs %0, currentel" : "=r"(val));
    return (val >> 2) & 0x3;
}

// VBAR_EL1 - Vector Base Address Register
#define WRITE_VBAR_EL1(val) __asm__ __volatile__("msr vbar_el1, %0" : : "r"(val))

/* ========================================================================
 * Exception Syndrome and Fault Address Registers
 * ======================================================================== */

// ESR_EL1 - Exception Syndrome Register (used in exception handling)
#define READ_ESR_EL1()                                                                             \
    ({                                                                                             \
        uint32_t val;                                                                              \
        __asm__ __volatile__("mrs %0, esr_el1" : "=r"(val));                                       \
        val;                                                                                       \
    })

#define READ_ESR_EL2()                                                                             \
    ({                                                                                             \
        uint32_t val;                                                                              \
        __asm__ __volatile__("mrs %0, esr_el2" : "=r"(val));                                       \
        val;                                                                                       \
    })

#define READ_ESR_EL3()                                                                             \
    ({                                                                                             \
        uint32_t val;                                                                              \
        __asm__ __volatile__("mrs %0, esr_el3" : "=r"(val));                                       \
        val;                                                                                       \
    })

#define READ_FAR_EL2()                                                                             \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, far_el2" : "=r"(val));                                       \
        val;                                                                                       \
    })

#define READ_HPFAR_EL2()                                                                           \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, hpfar_el2" : "=r"(val));                                     \
        val;                                                                                       \
    })

/* ========================================================================
 * Exception Context Registers (used in exception.S)
 * ======================================================================== */

#define WRITE_SP_EL0(val) __asm__ __volatile__("msr sp_el0, %0" : : "r"(val))

#define WRITE_ELR_EL1(val) __asm__ __volatile__("msr elr_el1, %0" : : "r"(val))

#define WRITE_SPSR_EL1(val) __asm__ __volatile__("msr spsr_el1, %0" : : "r"(val))

/* ========================================================================
 * Generic Timer Registers (used in timer.c and exception.c)
 * ======================================================================== */

// CNTFRQ_EL0 - Counter-timer Frequency Register
#define READ_CNTFRQ_EL0()                                                                          \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(val));                                    \
        val;                                                                                       \
    })

// CNTVCT_EL0 - Counter-timer Virtual Count Register
#define READ_CNTVCT_EL0()                                                                          \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));                                    \
        val;                                                                                       \
    })

#define READ_CNTPCT_EL0()                                                                          \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(val));                                    \
        val;                                                                                       \
    })

// CNTV_CTL_EL0 - Counter-timer Virtual Timer Control Register
#define READ_CNTV_CTL_EL0()                                                                        \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(val));                                  \
        val;                                                                                       \
    })

#define READ_CNTP_CTL_EL0()                                                                        \
    ({                                                                                             \
        uint64_t val;                                                                              \
        __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(val));                                  \
        val;                                                                                       \
    })

#define WRITE_CNTV_CTL_EL0(val) __asm__ __volatile__("msr cntv_ctl_el0, %0" : : "r"(val))

#define WRITE_CNTP_CTL_EL0(val) __asm__ __volatile__("msr cntp_ctl_el0, %0" : : "r"(val))

// CNTV_TVAL_EL0 - Counter-timer Virtual Timer TimerValue Register
#define READ_CNTV_TVAL_EL0()                                                                       \
    ({                                                                                             \
        int32_t val;                                                                               \
        __asm__ __volatile__("mrs %0, cntv_tval_el0" : "=r"(val));                                 \
        val;                                                                                       \
    })

#define WRITE_CNTV_TVAL_EL0(val) __asm__ __volatile__("msr cntv_tval_el0, %0" : : "r"(val))

// CNTP_TVAL_EL0 - Counter-timer Physical Timer TimerValue Register
#define WRITE_CNTP_TVAL_EL0(val) __asm__ __volatile__("msr cntp_tval_el0, %0" : : "r"(val))

/* ========================================================================
 * Memory Barrier Instructions (used in boot.S)
 * ======================================================================== */

// Data Synchronization Barrier
#define DSB_SY() __asm__ __volatile__("dsb sy" : : : "memory")

#define DSB_ISH() __asm__ __volatile__("dsb ish" ::: "memory")


// Instruction Synchronization Barrier
#define ISB() __asm__ __volatile__("isb" : : : "memory")

/* ========================================================================
 * Wait Instructions (used in PSCI)
 * ======================================================================== */

// Wait For Interrupt
#define WFI() __asm__ __volatile__("wfi" : : : "memory")

/* ========================================================================
 * Hypervisor Call and Secure Monitor Call (used in PSCI and SMP)
 * ======================================================================== */

// Hypervisor Call
static inline uint32_t
hvc_call(uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t reg0 __asm__("x0") = function_id;
    register uint64_t reg1 __asm__("x1") = arg0;
    register uint64_t reg2 __asm__("x2") = arg1;
    register uint64_t reg3 __asm__("x3") = arg2;

    __asm__ __volatile__("hvc #0" : "+r"(reg0) : "r"(reg1), "r"(reg2), "r"(reg3));

    return (uint32_t) reg0;
}

// Secure Monitor Call
static inline uint32_t
smc_call(uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t reg0 __asm__("x0") = function_id;
    register uint64_t reg1 __asm__("x1") = arg0;
    register uint64_t reg2 __asm__("x2") = arg1;
    register uint64_t reg3 __asm__("x3") = arg2;

    __asm__ __volatile__("smc #0" : "+r"(reg0) : "r"(reg1), "r"(reg2), "r"(reg3));

    return (uint32_t) reg0;
}

#endif  // __T_SYSREG_H__
