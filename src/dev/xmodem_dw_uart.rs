// XMODEM-1K receiver implementation in Rust
// Minimal XMODEM-1K receiver (CRC mode), adapted to use the dw_uart API
//
// This is a Rust port of xmodem_dw_uart.c with the same logic and structure

use crate::t_dw_uart::{dw_uart_getchar_nb, dw_uart_putchar};

// Control codes
const SOH: u8 = 0x01;
const STX: u8 = 0x02;
const EOT: u8 = 0x04;
const ACK: u8 = 0x06;
const NAK: u8 = 0x15;
const CAN: u8 = 0x18;
const CHAR_C: u8 = 0x43;

const XMODEM_1K: usize = 1024;
const MAX_INIT_RETRIES: i32 = 16;
const GETC_TIMEOUT_MS: u64 = 10000; // 10 seconds
const MAX_BLOCK_RETRIES: i32 = 16;

// Platform time function placeholder
// This should return a monotonic millisecond counter
// In the actual implementation, this would use the ARM system timer
fn platform_get_time_ms() -> u64 {
    // Placeholder implementation
    // In real code, read from CNTPCT_EL0 and CNTFRQ_EL0
    unsafe {
        let freq: u64;
        let cnt: u64;
        
        // Read counter frequency
        core::arch::asm!("mrs {}, cntfrq_el0", out(reg) freq);
        
        // Read counter value
        core::arch::asm!("mrs {}, cntpct_el0", out(reg) cnt);
        
        if freq == 0 {
            return 0;
        }
        
        // Calculate milliseconds
        let secs_part = cnt / freq;
        let rem = cnt % freq;
        let ms = secs_part * 1000;
        ms + (rem * 1000) / freq
    }
}

// Optional target memory write helper
// Override this if writing to flash or special memory regions
fn target_write(dst: &mut [u8], src: &[u8]) {
    dst[..src.len()].copy_from_slice(src);
}

// CRC16-CCITT (poly 0x1021), initial 0
fn crc16_ccitt(buf: &[u8]) -> u16 {
    let mut crc: u16 = 0;
    for &byte in buf {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            if (crc & 0x8000) != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

// Helper: blocking put char
#[inline(always)]
fn uart_putc(c: u8) {
    dw_uart_putchar(c);
}

// Helper: get a character with timeout (ms)
// Returns None on timeout, Some(byte) on success
fn uart_getc_timeout(timeout_ms: u64) -> Option<u8> {
    let start = platform_get_time_ms();
    let deadline = start + timeout_ms;
    
    while platform_get_time_ms() <= deadline {
        if let Some(ch) = dw_uart_getchar_nb() {
            return Some(ch);
        }
        // Yield or spin
        core::hint::spin_loop();
    }
    None
}

/// XMODEM-1K receiver
///
/// Receives a file via XMODEM-1K protocol into the provided buffer.
///
/// # Arguments
/// * `dst` - Target buffer to write received data
/// * `maxlen` - Maximum number of bytes that can be stored
///
/// # Returns
/// * `Ok(bytes_received)` - Number of bytes successfully received
/// * `Err(())` - Transfer failed or was cancelled
///
/// # Example
/// ```rust
/// let mut buffer = [0u8; 65536];
/// match xmodem_receive_1k(&mut buffer, buffer.len()) {
///     Ok(size) => println!("Received {} bytes", size),
///     Err(_) => println!("Transfer failed"),
/// }
/// ```
pub fn xmodem_receive_1k(dst: &mut [u8], maxlen: usize) -> Result<usize, ()> {
    let mut expected_blk: u8 = 1;
    let mut write_offset: usize = 0;
    
    // Send initial 'C' (request CRC) several times until we see response
    let mut c: Option<u8> = None;
    for _ in 0..MAX_INIT_RETRIES {
        uart_putc(CHAR_C);
        c = uart_getc_timeout(GETC_TIMEOUT_MS);
        if c.is_some() {
            break;
        }
    }
    
    let mut current_byte = match c {
        Some(byte) => byte,
        None => return Err(()), // No response from host
    };
    
    // Main receive loop
    loop {
        if current_byte == EOT {
            uart_putc(ACK);
            return Ok(write_offset);
        } else if current_byte == CAN {
            // Remote cancelled
            return Err(());
        } else if current_byte == SOH || current_byte == STX {
            let block_size = if current_byte == SOH { 128 } else { 1024 };
            
            // Read block number and complement
            let b1 = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => {
                    uart_putc(NAK);
                    return Err(());
                }
            };
            
            let b2 = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => {
                    uart_putc(NAK);
                    return Err(());
                }
            };
            
            let header_blk = b1;
            let header_blk_comp = b2;
            
            if header_blk.wrapping_add(header_blk_comp) != 0xFF {
                uart_putc(NAK);
                current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                    Some(b) => b,
                    None => return Err(()),
                };
                continue;
            }
            
            // Read data block
            let mut block_buf = [0u8; XMODEM_1K];
            for i in 0..block_size {
                match uart_getc_timeout(GETC_TIMEOUT_MS) {
                    Some(ch) => block_buf[i] = ch,
                    None => {
                        uart_putc(NAK);
                        return Err(());
                    }
                }
            }
            
            // Read CRC16 hi/lo
            let crc_hi = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => {
                    uart_putc(NAK);
                    return Err(());
                }
            };
            
            let crc_lo = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => {
                    uart_putc(NAK);
                    return Err(());
                }
            };
            
            let crc_recv = ((crc_hi as u16) << 8) | (crc_lo as u16);
            
            // Verify block number
            if header_blk != expected_blk {
                // Duplicate of previous block? (sender didn't get our ACK)
                if header_blk == expected_blk.wrapping_sub(1) {
                    uart_putc(ACK); // Accept duplicate, do not write
                    current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                        Some(b) => b,
                        None => return Err(()),
                    };
                    continue;
                } else {
                    uart_putc(NAK);
                    current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                        Some(b) => b,
                        None => return Err(()),
                    };
                    continue;
                }
            }
            
            // Verify CRC
            let crc_calc = crc16_ccitt(&block_buf[..block_size]);
            if crc_calc != crc_recv {
                uart_putc(NAK);
                current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                    Some(b) => b,
                    None => return Err(()),
                };
                continue;
            }
            
            // Write to target memory (bounds check)
            if write_offset + block_size <= maxlen && write_offset + block_size <= dst.len() {
                target_write(
                    &mut dst[write_offset..write_offset + block_size],
                    &block_buf[..block_size],
                );
                write_offset += block_size;
            } else {
                // Overflow -> cancel transfer
                uart_putc(CAN);
                uart_putc(CAN);
                return Err(());
            }
            
            // Success for this block
            uart_putc(ACK);
            expected_blk = expected_blk.wrapping_add(1);
            current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => return Err(()),
            };
            continue;
        } else {
            // Unexpected char; try to read next
            current_byte = match uart_getc_timeout(GETC_TIMEOUT_MS) {
                Some(b) => b,
                None => {
                    // Timeout -> abort
                    uart_putc(CAN);
                    uart_putc(CAN);
                    return Err(());
                }
            };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc16_ccitt() {
        // Test with known values
        let data = b"123456789";
        let crc = crc16_ccitt(data);
        // Expected CRC for "123456789" is 0x29B1
        assert_eq!(crc, 0x29B1);
    }

    #[test]
    fn test_crc16_ccitt_empty() {
        let data = b"";
        let crc = crc16_ccitt(data);
        assert_eq!(crc, 0);
    }
}
