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

// UART configuration for QEMU virt machine
#define UART_BASE 0x09000000
#define UART_IRQ  33

// GIC configuration for QEMU virt machine
#define GICD_BASE_ADDR 0x08000000  // GIC Distributor base address
#define GICC_BASE_ADDR 0x08010000  // GIC CPU Interface base address

// 定时器频率和时间片配置
#define TIMER_FREQUENCY_HZ 100  // 100Hz = 10ms per tick
#define TIMER_TICK_MS      10   // 每个tick 10毫秒
#define TIME_SLICE_TICKS   5    // 时间片为5个tick (50ms)

#endif