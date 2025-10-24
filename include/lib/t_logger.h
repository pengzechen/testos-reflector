#ifndef __IO_H__
#define __IO_H__

#include "t_types.h"
#include "t_sysreg.h"
#include <stdarg.h>

// UART functions (now interrupt-driven)
extern void
uart_putchar(char c);
extern void
uart_putstr(const char *str);
extern bool
uart_putchar_nb(char c);
extern bool
uart_getchar_nb(char *c);
extern bool
uart_rx_available(void);
extern uint32_t
uart_tx_buffer_usage(void);

/*  printf 函数库  */
extern int
my_vprintf(const char *fmt, va_list va);
extern int
my_snprintf(char *buf, int size, const char *fmt, ...);
extern int
my_vsnprintf(char *buf, int size, const char *fmt, va_list va);

extern int
logger(const char *fmt, ...);
extern int
logger_debug(const char *fmt, ...);
extern int
logger_info(const char *fmt, ...);
extern int
logger_warn(const char *fmt, ...);
extern int
logger_error(const char *fmt, ...);


void
dumpmem_as_u64(uint64_t *addr, int nums);

size_t
u64_to_hex(uint64_t val, char *buf, size_t buf_size);

extern void
t_run_printf_tests();

// 使用统一的系统寄存器访问函数
#define t_get_current_cpu_id() get_current_cpu_id()

#endif  // __IO_H__