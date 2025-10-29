/*
 * xmodem_dw_uart.c
 *
 * Minimal XMODEM-1K receiver (CRC mode), adapted to use the provided
 * dw_uart_* API:
 *
 * Provided by your platform (already implemented by you):
 *   void dw_uart_putchar(char c);
 *   bool dw_uart_putchar_nb(char c);
 *   bool dw_uart_getchar_nb(char *c);
 *   bool dw_uart_rx_available(void);
 *   void dw_uart_flush(void);
 *
 * Required platform functions you must provide/implement:
 *   uint64_t platform_get_time_ms(void); // returns monotonic ms
 *
 * Optional (weak) helper to write to target memory; override if writing flash:
 *   void target_write(uint8_t *dst, const uint8_t *src, size_t len)
 *
 * Usage:
 *   ssize_t r = xmodem_receive_1k((uint8_t *)RECV_ADDR, RECV_MAXLEN);
 *   // if (r > 0) {  r bytes received at RECV_ADDR  }
 *
 * Host-side send command (Linux):
 *   stty -F /dev/ttyUSB0 115200 raw -echo cs8 -cstopb -parenb
 *   sx yourfile.bin < /dev/ttyUSB0 > /dev/ttyUSB0
 *
 */

#include "dev/t_dw_uart.h"
#include "t_types.h"
#include "lib/t_string.h"
#include "dev/t_timer.h"

/* control codes */
#define SOH    0x01
#define STX    0x02
#define EOT    0x04
#define ACK    0x06
#define NAK    0x15
#define CAN    0x18
#define CHAR_C 0x43

#define XMODEM_1K         1024
#define MAX_INIT_RETRIES  16
#define GETC_TIMEOUT_MS   10000  // 增加到 10 秒，给用户更多准备时间
#define MAX_BLOCK_RETRIES 16


/*
 * Platform time function (must be provided by your platform):
 *    uint64_t platform_get_time_ms(void);
 * It should return a monotonic millisecond counter used for timeouts.
 */
uint64_t
platform_get_time_ms(void)
{
    static uint64_t freq = 0;

    /* Lazy init frequency */
    if (freq == 0) {
        freq = CNTFRQ_EL0_READ();
        if (freq == 0) {
            /* Frequency read failed or zero - avoid divide by zero */
            return 0;
        }
    }

    uint64_t cnt = CNTPCT_EL0_READ();

#if 0
    /* (cnt * 1000) / freq computed in 128-bit to avoid overflow */
    __int128 tmp = (__int128) cnt * 1000;
    return (uint64_t) (tmp / (__int128) freq);
#else
    /*
     * Fallback without int128:
     * compute (cnt / freq) * 1000 + ((cnt % freq) * 1000) / freq
     * This keeps intermediate values within 64-bit.
     */
    uint64_t secs_part = cnt / freq;
    uint64_t rem       = cnt % freq;
    uint64_t ms        = secs_part * 1000;
    ms += (rem * 1000) / freq;
    return ms;
#endif
}

/* Optional target memory write helper (override if writing to flash) */
void
target_write(uint8_t *dst, const uint8_t *src, size_t len)
{
    memcpy(dst, src, len);
}

/* CRC16-CCITT (poly 0x1021), initial 0 */
static uint16_t
crc16_ccitt(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t) (*buf++) << 8;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000)
                crc = (uint16_t) ((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* helper: blocking put char (uses your blocking API) */
static inline void
uart_putc(uint8_t c)
{
    dw_uart_putchar((char) c);
}

/* helper: get a character with timeout (ms). returns -1 on timeout, 0..255 on success */
static int
uart_getc_timeout(int timeout_ms)
{
    uint64_t start    = platform_get_time_ms();
    uint64_t deadline = start + (uint64_t) timeout_ms;
    char     ch;
    while (platform_get_time_ms() <= deadline) {
        if (dw_uart_getchar_nb(&ch)) {
            return (int) (uint8_t) ch;
        }
        /* optionally yield or nop; if you have a light wait/sleep API you could call it here */
    }
    return -1;
}

/*
 * xmodem_receive_1k:
 *  - dst: target RAM address to write received file
 *  - maxlen: maximum number of bytes that can be stored at dst
 * returns number of bytes received (>0) or -1 on error
 */
ssize_t
xmodem_receive_1k(uint8_t *dst, size_t maxlen)
{
    uint8_t expected_blk = 1;
    size_t  write_offset = 0;
    int     c            = -1;
    int     init_try;

    /* Send initial 'C' (request CRC) several times until we see response */
    for (init_try = 0; init_try < MAX_INIT_RETRIES; ++init_try) {
        uart_putc(CHAR_C);
        c = uart_getc_timeout(GETC_TIMEOUT_MS);
        if (c >= 0)
            break;
    }
    if (c < 0) {
        /* no response from host */
        return -1;
    }

    /* main receive loop */
    for (;;) {
        if (c == EOT) {
            uart_putc(ACK);
            return (ssize_t) write_offset;
        } else if (c == CAN) {
            /* remote cancelled */
            return -1;
        } else if (c == SOH || c == STX) {
            size_t  block_size = (c == SOH) ? 128 : 1024;
            uint8_t header_blk, header_blk_comp;
            uint8_t block_buf[XMODEM_1K];
            uint8_t crc_hi, crc_lo;
            int     i;

            /* read block number and complement */
            int b1 = uart_getc_timeout(GETC_TIMEOUT_MS);
            int b2 = uart_getc_timeout(GETC_TIMEOUT_MS);
            if (b1 < 0 || b2 < 0) {
                uart_putc(NAK);
                return -1;
            }
            header_blk      = (uint8_t) b1;
            header_blk_comp = (uint8_t) b2;
            if ((uint8_t) (header_blk + header_blk_comp) != 0xFF) {
                uart_putc(NAK);
                c = uart_getc_timeout(GETC_TIMEOUT_MS);
                continue;
            }

            /* read data */
            for (i = 0; i < (int) block_size; ++i) {
                int ch = uart_getc_timeout(GETC_TIMEOUT_MS);
                if (ch < 0) {
                    uart_putc(NAK);
                    return -1;
                }
                block_buf[i] = (uint8_t) ch;
            }

            /* read CRC16 hi/lo */
            int hi = uart_getc_timeout(GETC_TIMEOUT_MS);
            int lo = uart_getc_timeout(GETC_TIMEOUT_MS);
            if (hi < 0 || lo < 0) {
                uart_putc(NAK);
                return -1;
            }
            crc_hi            = (uint8_t) hi;
            crc_lo            = (uint8_t) lo;
            uint16_t crc_recv = ((uint16_t) crc_hi << 8) | crc_lo;

            /* verify block number */
            if (header_blk != expected_blk) {
                /* duplicate of previous block? (sender didn't get our ACK) */
                if (header_blk == (uint8_t) (expected_blk - 1)) {
                    uart_putc(ACK); /* accept duplicate, do not write */
                    c = uart_getc_timeout(GETC_TIMEOUT_MS);
                    continue;
                } else {
                    uart_putc(NAK);
                    c = uart_getc_timeout(GETC_TIMEOUT_MS);
                    continue;
                }
            }

            /* verify CRC */
            uint16_t crc_calc = crc16_ccitt(block_buf, block_size);
            if (crc_calc != crc_recv) {
                uart_putc(NAK);
                c = uart_getc_timeout(GETC_TIMEOUT_MS);
                continue;
            }

            /* write to target memory (bounds check) */
            if (write_offset + block_size <= maxlen) {
                target_write(dst + write_offset, block_buf, block_size);
                write_offset += block_size;
            } else {
                /* overflow -> cancel transfer */
                uart_putc(CAN);
                uart_putc(CAN);
                return -1;
            }

            /* success for this block */
            uart_putc(ACK);
            expected_blk++;
            c = uart_getc_timeout(GETC_TIMEOUT_MS);
            continue;
        } else {
            /* unexpected char; try to read next */
            c = uart_getc_timeout(GETC_TIMEOUT_MS);
            if (c < 0) {
                /* timeout -> abort */
                uart_putc(CAN);
                uart_putc(CAN);
                return -1;
            }
        }
    }
}

// hexdump -C test.txt | head -n 2

// sudo minicom -D /dev/ttyUSB0 -b 1500000

// sudo stty -F /dev/ttyUSB0 1500000 raw -echo cs8 -cstopb -parenb

// sudo sh -c 'sx -k test.txt < /dev/ttyUSB0 > /dev/ttyUSB0'