#ifndef __T_CFG_H__
#define __T_CFG_H__

#ifndef __LOAD_ADDR__
    // qemu entry addr default
    #define __LOAD_ADDR__ 0x40080000
#endif

#ifndef T_SMP_NUM
    // qemu smp num default
    #define T_SMP_NUM 1
#endif

// Stack configuration
#define T_STACK_SIZE 0x4000  // 16KB stack size

// Logger configuration
#define GUEST_LABEL "[TESTOS] "




// Timer interrupt numbers
#define CNTP_TIMER 30  // Generic timer interrupt
#define CNTV_TIMER 27  // Virtual timer interrupt

// UART configuration for rk3588
#define UART_BASE 0xfeb50000
#define UART_IRQ  (333 + 32)

// GIC configuration for rk3588
#define GICD_BASE_ADDR 0xfe600000  // GIC Distributor base address
#define GICC_BASE_ADDR 0xfe610000  // GIC CPU Interface base address
#define GICR_BASE_ADDR 0xfe680000  // GIC Redistributor base address

// PNU configuration for rk3588
#define NPU_BASE_ADDR 0xfdab0000 // NPU base (npu@fdab0000, npu0,npu1,npu2,len=0x3000)




// 定时器频率和时间片配置
#define TIMER_FREQUENCY_HZ 100  // 100Hz = 10ms per tick
#define TIMER_TICK_MS      10   // 每个tick 10毫秒
#define TIME_SLICE_TICKS   5    // 时间片为5个tick (50ms)

#endif