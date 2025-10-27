

#ifndef __CRU_H__
#define __CRU_H__

#define CRU_BASE 0xFD7C0000UL

#define RK3588_CLKSEL_CON(x)  (CRU_BASE + 0x300 + (x) * 4)
#define RK3588_CLKGATE_CON(x) (CRU_BASE + 0x800 + (x) * 4)
#define RK3588_SOFTRST_CON(x) (CRU_BASE + 0xa00 + (x) * 4)

void
enable_rk3588_npu_clocks(void);

#endif  // __CRU_H__