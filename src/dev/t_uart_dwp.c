#include "t_dw_uart.h"
#include "t_types.h"
#include "t_mmio.h"
#include "lib/t_spinlock.h"
#include "lib/t_logger.h"
#include "t_exception.h"
#include "t_gicv3.h"

#define DW_UART_TX_BUFFER_SIZE 1024
#define DW_UART_RX_BUFFER_SIZE 1024

typedef struct
{
    char              buffer[DW_UART_TX_BUFFER_SIZE];
    volatile uint32_t head, tail, count;
    spinlock_irq_t    lock;
} dw_uart_buffer_t;

static dw_uart_buffer_t tx_buffer           = {0};
static dw_uart_buffer_t rx_buffer           = {0};
static volatile bool    dw_uart_initialized = false;

static bool
buffer_is_empty(dw_uart_buffer_t *buf)
{
    return buf->count == 0;
}
static bool
buffer_is_full(dw_uart_buffer_t *buf)
{
    return buf->count >= DW_UART_TX_BUFFER_SIZE;
}
static bool
buffer_put(dw_uart_buffer_t *buf, char c)
{
    if (buffer_is_full(buf))
        return false;
    buf->buffer[buf->head] = c;
    buf->head              = (buf->head + 1) % DW_UART_TX_BUFFER_SIZE;
    buf->count++;
    return true;
}
static bool
buffer_get(dw_uart_buffer_t *buf, char *c)
{
    if (buffer_is_empty(buf))
        return false;
    *c        = buf->buffer[buf->tail];
    buf->tail = (buf->tail + 1) % DW_UART_TX_BUFFER_SIZE;
    buf->count--;
    return true;
}

/// DW_UART_THR 空了，可以写入新数据时，硬件会置位
static bool
dw_uart_tx_ready(void)
{
    return (read32((void *) DW_UART_LSR) & DW_UART_LSR_THRE) != 0;
}

/// DW_UART_RBR 中的数据可读了，硬件给他置位
static bool
dw_uart_rx_ready(void)
{
    return (read32((void *) DW_UART_LSR) & DW_UART_LSR_DR) != 0;
}

// 启用发送中断， 我有数据可以发送了
static void
dw_uart_enable_tx_interrupt(void)
{
    uint32_t ier = read32((void *) DW_UART_IER);
    ier |= DW_UART_IER_THRI;
    write32(ier, (void *) DW_UART_IER);
}
// 禁用发送中断， 我已经没有数据可以发送了
static void
dw_uart_disable_tx_interrupt(void)
{
    uint32_t ier = read32((void *) DW_UART_IER);
    ier &= ~DW_UART_IER_THRI;
    write32(ier, (void *) DW_UART_IER);
}
// 启用接收中断， 我准备好接收数据了
static void
dw_uart_enable_rx_interrupt(void)
{
    uint32_t ier = read32((void *) DW_UART_IER);
    ier |= DW_UART_IER_RDI;
    write32(ier, (void *) DW_UART_IER);
}

void
dw_uart_interrupt_handler(uint64_t *stack_pointer)
{
    // logger_info("Uart handler invoke...\n");

    uint32_t iir = read32((void *) DW_UART_IIR) & 0xF;
    if (iir == 0x4) {  // RX 有人按下了键盘的键， 可以读数据了

        spin_lock_irqsave(&rx_buffer.lock);
        while (dw_uart_rx_ready()) {
            char c = (char) read32((void *) DW_UART_RBR);
            logger_info("got key: %c\n", c);
            buffer_put(&rx_buffer, c);
        }
        spin_unlock_irqrestore(&rx_buffer.lock);
    }
    if (iir == 0x2) {  // TX  **TX 中断是“可以发下一个字节了”**的信号
        spin_lock_irqsave(&tx_buffer.lock);

        while (dw_uart_tx_ready() && !buffer_is_empty(&tx_buffer)) {
            char c;
            if (buffer_get(&tx_buffer, &c)) {
                write32((uint32_t) c, (void *) DW_UART_THR);
            }
        }
        if (buffer_is_empty(&tx_buffer)) {
            /*
                它的意义是：
                    当我们已经把所有待发送的数据都写给硬件时，就没必要再关心 TX 中断了。
                因为：
                    硬件下次“THR 空了”时，我们也没数据可以写；
                    如果不关中断，硬件会不停地产生 TX 中断，每次都发现“没数据”，浪费 CPU。
            */
            dw_uart_disable_tx_interrupt();
        }
        spin_unlock_irqrestore(&tx_buffer.lock);
    }
}

static inline void
delay_loop(unsigned int n)
{
    for (volatile unsigned int i = 0; i < n; i++) {
        asm volatile("nop");
    }
}

void
dw_uart_init(void)
{
    if (dw_uart_initialized)
        return;

    spinlock_irq_init(&tx_buffer.lock);
    spinlock_irq_init(&rx_buffer.lock);

    tx_buffer.head = tx_buffer.tail = tx_buffer.count = 0;
    rx_buffer.head = rx_buffer.tail = rx_buffer.count = 0;

    delay_loop(100);
    delay_loop(100);
    delay_loop(100);

    // 关闭 UART
    write32(0, (void *) DW_UART_IER);


    // 配置波特率（24MHz，1,500,000）
    uint32_t lcr = read32((void *) DW_UART_LCR);

    write32(lcr | DW_UART_LCR_DLAB, (void *) DW_UART_LCR);
    write32(1, (void *) DW_UART_DLL);
    write32(0, (void *) DW_UART_DLM);
    write32(lcr & ~DW_UART_LCR_DLAB, (void *) DW_UART_LCR);

    // 8N1
    write32(0x3, (void *) DW_UART_LCR);

    // 使能 FIFO
    write32(DW_UART_FCR_ENABLE_FIFO | DW_UART_FCR_CLEAR_RCVR | DW_UART_FCR_CLEAR_XMIT,
            (void *) DW_UART_FCR);

    // 安装中断处理
    irq_install(DW_UART_IRQ, dw_uart_interrupt_handler);

    // 使能 RX 中断
    dw_uart_enable_rx_interrupt();

    dw_uart_enable_tx_interrupt();


    gicv3_set_int_trigger(DW_UART_IRQ, 0);  // 设置为电平触发

    gicv3_set_int_target(DW_UART_IRQ, 0x1);  // 目标 CPU 0

    gicv3_enable_int(DW_UART_IRQ, true);

    if (gicv3_is_int_enabled(DW_UART_IRQ)) {
        logger_warn("DW UART IRQ %d is enabled in GICv3\n", DW_UART_IRQ);
    }

    dw_uart_initialized = true;

    logger_info("DWC UART interrupt driver initialized\n");
}

bool
dw_uart_putchar_nb(char c)
{
    if (!dw_uart_initialized)
        return false;
    spin_lock_irqsave(&tx_buffer.lock);
    bool success = false;

    // 处理换行：如果是 '\n'，先尝试发送 '\r'
    if (c == '\n') {
        if (buffer_is_empty(&tx_buffer) && dw_uart_tx_ready()) {
            write32((uint32_t) '\r', (void *) DW_UART_THR);
        } else {
            if (!buffer_put(&tx_buffer, '\r')) {
                // 缓冲区满，先发送 '\r' 失败
                spin_unlock_irqrestore(&tx_buffer.lock);
                return false;
            }
            dw_uart_enable_tx_interrupt();
        }
    }

    if (buffer_is_empty(&tx_buffer) && dw_uart_tx_ready()) {
        // 发送缓冲区满了，并且硬件可以发送新数据
        // 直接发送
        write32((uint32_t) c, (void *) DW_UART_THR);
        success = true;
    } else {
        success = buffer_put(&tx_buffer, c);
        if (success) {
            dw_uart_enable_tx_interrupt();
        }
    }
    spin_unlock_irqrestore(&tx_buffer.lock);
    return success;
}

void
dw_uart_putchar(char c)
{
    // 如果 UART 尚未初始化，直接写寄存器
    if (!dw_uart_initialized) {
        volatile unsigned int *const UARTDR  = (unsigned int *) DW_UART_THR;
        volatile unsigned int *const UARTLSR = (unsigned int *) DW_UART_LSR;

        // 如果是 '\n'，先发送 '\r'
        if (c == '\n') {
            while (!(*UARTLSR & DW_UART_LSR_THRE))
                ;  // 等待可写
            *UARTDR = '\r';
        }

        while (!(*UARTLSR & DW_UART_LSR_THRE))
            ;  // 等待可写
        *UARTDR = c;
        return;
    }
    if (dw_uart_putchar_nb(c))
        return;

    int timeout = 10000;
    while (timeout-- > 0) {
        if (dw_uart_putchar_nb(c))
            return;
        for (int i = 0; i < 100; i++)
            asm volatile("nop");
    }
    logger_warn("DWC UART TX buffer full, dropping character\n");
}

void
dw_uart_putstr(const char *str)
{
    while (*str)
        dw_uart_putchar(*str++);
}

void
dw_uart_flush(void)
{
    if (!dw_uart_initialized)
        return;
    int timeout = 100000;
    while (timeout-- > 0) {
        spin_lock_irqsave(&tx_buffer.lock);
        bool empty = (tx_buffer.head == tx_buffer.tail);
        spin_unlock_irqrestore(&tx_buffer.lock);
        if (empty)
            break;
        for (int i = 0; i < 10; i++)
            asm volatile("nop");
    }
    timeout = 10000;
    while (timeout-- > 0) {
        if (dw_uart_tx_ready())
            break;
        for (int i = 0; i < 10; i++)
            asm volatile("nop");
    }
}

bool
dw_uart_getchar_nb(char *c)
{
    if (!dw_uart_initialized)
        return false;
    spin_lock_irqsave(&rx_buffer.lock);
    bool success = buffer_get(&rx_buffer, c);
    spin_unlock_irqrestore(&rx_buffer.lock);
    return success;
}

bool
dw_uart_rx_available(void)
{
    if (!dw_uart_initialized)
        return false;
    spin_lock_irqsave(&rx_buffer.lock);
    bool available = !buffer_is_empty(&rx_buffer);
    spin_unlock_irqrestore(&rx_buffer.lock);
    return available;
}

uint32_t
dw_uart_tx_buffer_usage(void)
{
    if (!dw_uart_initialized)
        return 0;
    spin_lock_irqsave(&tx_buffer.lock);
    uint32_t usage = tx_buffer.count;
    spin_unlock_irqrestore(&tx_buffer.lock);
    return usage;
}
