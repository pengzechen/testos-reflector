/*
 * User mode support - Transition from EL1 to EL0
 */

.section .text
.global enter_user_mode

/**
 * enter_user_mode - Switch from EL1 to EL0 and start executing user code
 * @param x0: entry_point - User program entry point address
 * @param x1: user_stack - User mode stack pointer
 */
enter_user_mode:
    /* Save arguments */
    mov     x9, x0              // Save entry point in x9
    mov     x10, x1             // Save user stack in x10

    /* Set up SPSR_EL1 for EL0 return
     * Bits:
     * [3:0]  M[3:0] = 0b0000 (EL0t - EL0 with SP_EL0)
     * [4]    M[4]   = 0      (AArch64)
     * [9]    E      = 0      (Little-endian)
     * [6]    F      = 0      (FIQ unmasked)
     * [7]    I      = 0      (IRQ unmasked) 
     * [8]    A      = 0      (SError unmasked)
     * [9]    D      = 0      (Debug unmasked)
     */
    mov     x2, #0x0            // EL0t mode
    msr     spsr_el1, x2

    /* Set ELR_EL1 to user program entry point */
    msr     elr_el1, x9

    /* Set SP_EL0 to user stack */
    msr     sp_el0, x10

    /* Clear general purpose registers before entering user mode
     * This prevents leaking kernel data to user space */
    mov     x0, #0
    mov     x1, #0
    mov     x2, #0
    mov     x3, #0
    mov     x4, #0
    mov     x5, #0
    mov     x6, #0
    mov     x7, #0
    mov     x8, #0
    mov     x9, #0
    mov     x10, #0
    mov     x11, #0
    mov     x12, #0
    mov     x13, #0
    mov     x14, #0
    mov     x15, #0
    mov     x16, #0
    mov     x17, #0
    mov     x18, #0
    mov     x19, #0
    mov     x20, #0
    mov     x21, #0
    mov     x22, #0
    mov     x23, #0
    mov     x24, #0
    mov     x25, #0
    mov     x26, #0
    mov     x27, #0
    mov     x28, #0
    mov     x29, #0
    mov     x30, #0

    /* Return to EL0 - this will jump to the address in ELR_EL1 with SPSR_EL1 */
    eret

/**
 * Helper function to set up user mode environment
 * Call this before entering user mode for the first time
 */
.global setup_user_mode
setup_user_mode:
    /* Enable access to EL0 for:
     * - Floating point (FPEN)
     * - Advanced SIMD (FPEN)
     */
    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)      // FPEN = 11 (no trap)
    msr     cpacr_el1, x0
    isb

    ret
