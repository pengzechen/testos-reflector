
#ifndef XMODEM_DW_UART_H
#define XMODEM_DW_UART_H


#include "t_types.h"

ssize_t
xmodem_receive_1k(uint8_t *dst, size_t maxlen);

#endif /* XMODEM_DW_UART_H */