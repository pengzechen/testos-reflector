#ifndef __T_UART_H__
#define __T_UART_H__

#include "t_types.h"
#include "cfg/t_cfg.h"

// UART register offsets
#define UART_DR    (UART_BASE + 0x00)  // Data register
#define UART_FR    (UART_BASE + 0x18)  // Flag register
#define UART_IBRD  (UART_BASE + 0x24)  // Integer baud rate divisor
#define UART_FBRD  (UART_BASE + 0x28)  // Fractional baud rate divisor
#define UART_LCR_H (UART_BASE + 0x2C)  // Line control register
#define UART_CR    (UART_BASE + 0x30)  // Control register
#define UART_IMSC  (UART_BASE + 0x38)  // Interrupt mask set/clear
#define UART_MIS   (UART_BASE + 0x40)  // Masked interrupt status
#define UART_ICR   (UART_BASE + 0x44)  // Interrupt clear register

// UART flag register bits
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO full
#define UART_FR_RXFE (1 << 4)  // Receive FIFO empty
#define UART_FR_TXFE (1 << 3)  // Transmit FIFO empty

// UART control register bits
#define UART_CR_UARTEN (1 << 0)  // UART enable
#define UART_CR_TXE    (1 << 8)  // Transmit enable
#define UART_CR_RXE    (1 << 9)  // Receive enable

// UART interrupt bits
#define UART_INT_TX (1 << 5)  // Transmit interrupt
#define UART_INT_RX (1 << 4)  // Receive interrupt
#define UART_INT_RT (1 << 6)  // Receive timeout interrupt

// Initialize UART with interrupt support
void
uart_init(void);

// Character output functions
void
uart_putchar(char c);  // Blocking output
bool
uart_putchar_nb(char c);  // Non-blocking output
void
uart_putstr(const char *str);  // String output
void
uart_flush(void);  // Flush output buffer

// Character input functions
bool
uart_getchar_nb(char *c);  // Non-blocking input
bool
uart_rx_available(void);  // Check if RX data available

// Buffer status
uint32_t
uart_tx_buffer_usage(void);  // Get TX buffer usage

// Interrupt handler (internal use)
void
uart_interrupt_handler(uint64_t *stack_pointer);

#endif  // __T_UART_H__
