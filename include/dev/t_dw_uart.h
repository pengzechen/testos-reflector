#ifndef __T_DW_UART_H__
#define __T_DW_UART_H__

#include "t_types.h"
#include "cfg/t_cfg.h"

// DWC UART register offsets (8250/16550 兼容)
#define DW_UART_BASE      UART_BASE 
#define DW_UART_RBR   (DW_UART_BASE + 0x00) // Receiver Buffer Register (read)
#define DW_UART_THR   (DW_UART_BASE + 0x00) // Transmit Holding Register (write)
#define DW_UART_IER   (DW_UART_BASE + 0x04) // Interrupt Enable Register
#define DW_UART_IIR   (DW_UART_BASE + 0x08) // Interrupt Identification Register (read)
#define DW_UART_FCR   (DW_UART_BASE + 0x08) // FIFO Control Register (write)
#define DW_UART_LCR   (DW_UART_BASE + 0x0C) // Line Control Register
#define DW_UART_MCR   (DW_UART_BASE + 0x10) // Modem Control Register
#define DW_UART_LSR   (DW_UART_BASE + 0x14) // Line Status Register
#define DW_UART_MSR   (DW_UART_BASE + 0x18) // Modem Status Register
#define DW_UART_SCR   (DW_UART_BASE + 0x1C) // Scratch Register
#define DW_UART_USR   (DW_UART_BASE + 0x7C) // UART Status Register
#define DW_UART_DLL   (DW_UART_BASE + 0x00) // Divisor Latch Low (when DLAB=1)
#define DW_UART_DLM   (DW_UART_BASE + 0x04) // Divisor Latch High (when DLAB=1)

// LSR bits
#define DW_UART_LSR_DR   (1 << 0) // Data Ready
#define DW_UART_LSR_THRE (1 << 5) // Transmit Holding Register Empty

// IER bits
#define DW_UART_IER_RDI  (1 << 0) // Enable Received Data Available Interrupt
#define DW_UART_IER_THRI (1 << 1) // Enable Transmitter Holding Register Empty Interrupt

// FCR bits
#define DW_UART_FCR_ENABLE_FIFO (1 << 0)
#define DW_UART_FCR_CLEAR_RCVR  (1 << 1)
#define DW_UART_FCR_CLEAR_XMIT  (1 << 2)

// LCR bits
#define DW_UART_LCR_DLAB (1 << 7)

// TODO: 替换为实际中断号
#define DW_UART_IRQ    UART_IRQ

void dw_uart_init(void);
void dw_uart_putchar(char c);
bool dw_uart_putchar_nb(char c);
void dw_uart_putstr(const char *str);
void dw_uart_flush(void);
bool dw_uart_getchar_nb(char *c);
bool dw_uart_rx_available(void);
uint32_t dw_uart_tx_buffer_usage(void);
void dw_uart_get_stats(uint32_t *tx_irqs, uint32_t *rx_irqs, uint32_t *tx_usage, uint32_t *rx_usage);
bool dw_uart_is_tx_interrupt_enabled(void);
uint32_t dw_uart_get_last_iir(void);
uint32_t dw_uart_get_tx_sent_total(void);
void dw_uart_interrupt_handler(uint64_t *stack_pointer);

#endif // __T_DW_UART_H__
