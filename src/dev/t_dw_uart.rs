// DWC UART register offsets (8250/16550 compatible)
// This is a Rust implementation of the DW UART driver with the same logic as the C version

use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

// Import required types and functions (these would need to be defined elsewhere in your Rust codebase)
// For now, we'll define placeholders for the types and functions that would be imported

// Constants from t_cfg.h
// These should be defined in your configuration
const UART_BASE: u64 = 0x0; // Replace with actual UART_BASE from cfg
const UART_IRQ: u32 = 0;    // Replace with actual UART_IRQ from cfg

// DWC UART register offsets (8250/16550 compatible)
const DW_UART_BASE: u64 = UART_BASE;
const DW_UART_RBR: u64 = DW_UART_BASE + 0x00; // Receiver Buffer Register (read)
const DW_UART_THR: u64 = DW_UART_BASE + 0x00; // Transmit Holding Register (write)
const DW_UART_IER: u64 = DW_UART_BASE + 0x04; // Interrupt Enable Register
const DW_UART_IIR: u64 = DW_UART_BASE + 0x08; // Interrupt Identification Register (read)
const DW_UART_FCR: u64 = DW_UART_BASE + 0x08; // FIFO Control Register (write)
const DW_UART_LCR: u64 = DW_UART_BASE + 0x0C; // Line Control Register
const DW_UART_MCR: u64 = DW_UART_BASE + 0x10; // Modem Control Register
const DW_UART_LSR: u64 = DW_UART_BASE + 0x14; // Line Status Register
const DW_UART_MSR: u64 = DW_UART_BASE + 0x18; // Modem Status Register
const DW_UART_SCR: u64 = DW_UART_BASE + 0x1C; // Scratch Register
const DW_UART_USR: u64 = DW_UART_BASE + 0x7C; // UART Status Register
const DW_UART_DLL: u64 = DW_UART_BASE + 0x00; // Divisor Latch Low (when DLAB=1)
const DW_UART_DLM: u64 = DW_UART_BASE + 0x04; // Divisor Latch High (when DLAB=1)

// LSR bits
const DW_UART_LSR_DR: u32 = 1 << 0;   // Data Ready
const DW_UART_LSR_THRE: u32 = 1 << 5; // Transmit Holding Register Empty

// IER bits
const DW_UART_IER_RDI: u32 = 1 << 0;  // Enable Received Data Available Interrupt
const DW_UART_IER_THRI: u32 = 1 << 1; // Enable Transmitter Holding Register Empty Interrupt

// FCR bits
const DW_UART_FCR_ENABLE_FIFO: u32 = 1 << 0;
const DW_UART_FCR_CLEAR_RCVR: u32 = 1 << 1;
const DW_UART_FCR_CLEAR_XMIT: u32 = 1 << 2;

// LCR bits
const DW_UART_LCR_DLAB: u32 = 1 << 7;

// Buffer sizes
const DW_UART_TX_BUFFER_SIZE: usize = 1024;
const DW_UART_RX_BUFFER_SIZE: usize = 1024;

// SpinLock placeholder - would need actual implementation
struct SpinLockIrq {
    locked: AtomicBool,
}

impl SpinLockIrq {
    const fn new() -> Self {
        SpinLockIrq {
            locked: AtomicBool::new(false),
        }
    }

    fn lock(&self) {
        while self.locked.swap(true, Ordering::Acquire) {
            // Spin
            core::hint::spin_loop();
        }
    }

    fn unlock(&self) {
        self.locked.store(false, Ordering::Release);
    }
}

// UART Buffer structure
struct DwUartBuffer {
    buffer: [u8; DW_UART_TX_BUFFER_SIZE],
    head: usize,
    tail: usize,
    count: usize,
    lock: SpinLockIrq,
}

impl DwUartBuffer {
    const fn new() -> Self {
        DwUartBuffer {
            buffer: [0u8; DW_UART_TX_BUFFER_SIZE],
            head: 0,
            tail: 0,
            count: 0,
            lock: SpinLockIrq::new(),
        }
    }

    fn is_empty(&self) -> bool {
        self.count == 0
    }

    fn is_full(&self) -> bool {
        self.count >= DW_UART_TX_BUFFER_SIZE
    }

    fn put(&mut self, c: u8) -> bool {
        if self.is_full() {
            return false;
        }
        self.buffer[self.head] = c;
        self.head = (self.head + 1) % DW_UART_TX_BUFFER_SIZE;
        self.count += 1;
        true
    }

    fn get(&mut self) -> Option<u8> {
        if self.is_empty() {
            return None;
        }
        let c = self.buffer[self.tail];
        self.tail = (self.tail + 1) % DW_UART_TX_BUFFER_SIZE;
        self.count -= 1;
        Some(c)
    }
}

// Global state
static mut TX_BUFFER: DwUartBuffer = DwUartBuffer::new();
static mut RX_BUFFER: DwUartBuffer = DwUartBuffer::new();
static DW_UART_INITIALIZED: AtomicBool = AtomicBool::new(false);

// Debug counters
static TX_IRQ_COUNT: AtomicU32 = AtomicU32::new(0);
static RX_IRQ_COUNT: AtomicU32 = AtomicU32::new(0);
static LAST_IIR_VALUE: AtomicU32 = AtomicU32::new(0);
static TX_SENT_TOTAL: AtomicU32 = AtomicU32::new(0);

// MMIO read/write functions
unsafe fn read32(addr: u64) -> u32 {
    read_volatile(addr as *const u32)
}

unsafe fn write32(value: u32, addr: u64) {
    write_volatile(addr as *mut u32, value);
}

// DW_UART_THR is empty, ready to write new data
fn dw_uart_tx_ready() -> bool {
    unsafe { (read32(DW_UART_LSR) & DW_UART_LSR_THRE) != 0 }
}

// DW_UART_RBR has data ready to read
fn dw_uart_rx_ready() -> bool {
    unsafe { (read32(DW_UART_LSR) & DW_UART_LSR_DR) != 0 }
}

// Enable transmit interrupt
fn dw_uart_enable_tx_interrupt() {
    unsafe {
        let mut ier = read32(DW_UART_IER);
        ier |= DW_UART_IER_THRI;
        write32(ier, DW_UART_IER);
    }
}

// Disable transmit interrupt
fn dw_uart_disable_tx_interrupt() {
    unsafe {
        let mut ier = read32(DW_UART_IER);
        ier &= !DW_UART_IER_THRI;
        write32(ier, DW_UART_IER);
    }
}

// Enable receive interrupt
fn dw_uart_enable_rx_interrupt() {
    unsafe {
        let mut ier = read32(DW_UART_IER);
        ier |= DW_UART_IER_RDI;
        write32(ier, DW_UART_IER);
    }
}

// Interrupt handler
pub fn dw_uart_interrupt_handler(stack_pointer: u64) {
    unsafe {
        let iir = read32(DW_UART_IIR) & 0xF;
        LAST_IIR_VALUE.store(iir, Ordering::Relaxed);

        if iir == 0x4 || iir == 0xC {
            // RX interrupt
            RX_IRQ_COUNT.fetch_add(1, Ordering::Relaxed);
            RX_BUFFER.lock.lock();
            while dw_uart_rx_ready() {
                let c = read32(DW_UART_RBR) as u8;
                RX_BUFFER.put(c);
            }
            RX_BUFFER.lock.unlock();
        }

        // 0x7 = Busy Detect, 0x2 = TX Holding Register Empty
        if iir == 0x2 || iir == 0x7 {
            if iir == 0x7 {
                // Read USR register to clear Busy Detect interrupt
                let _ = read32(DW_UART_USR);
            }

            TX_IRQ_COUNT.fetch_add(1, Ordering::Relaxed);
            TX_BUFFER.lock.lock();

            let mut sent = 0;
            const MAX_BATCH: usize = 16;
            while sent < MAX_BATCH && !TX_BUFFER.is_empty() {
                if let Some(c) = TX_BUFFER.get() {
                    write32(c as u32, DW_UART_THR);
                    sent += 1;
                    TX_SENT_TOTAL.fetch_add(1, Ordering::Relaxed);
                }
            }
            let is_empty = TX_BUFFER.is_empty();
            TX_BUFFER.lock.unlock();

            if is_empty {
                dw_uart_disable_tx_interrupt();
            }
        }
    }
}

// Delay helper function
fn delay_loop(n: u32) {
    for _ in 0..n {
        unsafe {
            core::arch::asm!("nop");
        }
    }
}

// Initialize UART
pub fn dw_uart_init() {
    if DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return;
    }

    unsafe {
        TX_BUFFER = DwUartBuffer::new();
        RX_BUFFER = DwUartBuffer::new();

        delay_loop(100);
        delay_loop(100);
        delay_loop(100);

        // Disable UART
        write32(0, DW_UART_IER);

        // Configure baud rate (24MHz, 1,500,000)
        let lcr = read32(DW_UART_LCR);
        write32(lcr | DW_UART_LCR_DLAB, DW_UART_LCR);
        write32(1, DW_UART_DLL);
        write32(0, DW_UART_DLM);
        write32(lcr & !DW_UART_LCR_DLAB, DW_UART_LCR);

        // 8N1
        write32(0x3, DW_UART_LCR);

        // Enable FIFO
        write32(
            DW_UART_FCR_ENABLE_FIFO | DW_UART_FCR_CLEAR_RCVR | DW_UART_FCR_CLEAR_XMIT,
            DW_UART_FCR,
        );

        // Install interrupt handler
        // Note: This would need to call the actual IRQ installation function
        // irq_install(DW_UART_IRQ, dw_uart_interrupt_handler);

        // Enable RX interrupt
        dw_uart_enable_rx_interrupt();

        // GICv3 configuration would go here
        // gicv3_set_int_trigger(DW_UART_IRQ, 0);
        // gicv3_set_int_target(DW_UART_IRQ, 0x1);
        // gicv3_enable_int(DW_UART_IRQ, true);
    }

    DW_UART_INITIALIZED.store(true, Ordering::Relaxed);

    // logger_info("DWC UART interrupt driver initialized\n");
}

// Non-blocking put character
pub fn dw_uart_putchar_nb(c: u8) -> bool {
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return false;
    }

    unsafe {
        TX_BUFFER.lock.lock();

        // Handle newline: if it's '\n', try to send '\r' first
        if c == b'\n' {
            if TX_BUFFER.is_empty() && dw_uart_tx_ready() {
                write32(b'\r' as u32, DW_UART_THR);
            } else {
                if !TX_BUFFER.put(b'\r') {
                    TX_BUFFER.lock.unlock();
                    return false;
                }
                dw_uart_enable_tx_interrupt();
            }
        }

        let success = if TX_BUFFER.is_empty() && dw_uart_tx_ready() {
            // Buffer is empty and hardware can send new data
            // Send directly without going through buffer
            write32(c as u32, DW_UART_THR);
            true
        } else {
            let result = TX_BUFFER.put(c);
            if result {
                dw_uart_enable_tx_interrupt();
            }
            result
        };

        // If buffer is not empty after direct send, ensure TX interrupt is enabled
        if !TX_BUFFER.is_empty() {
            dw_uart_enable_tx_interrupt();
        }

        TX_BUFFER.lock.unlock();
        success
    }
}

// Blocking put character
pub fn dw_uart_putchar(c: u8) {
    // If UART is not initialized, write directly to registers
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        unsafe {
            // If it's '\n', send '\r' first
            if c == b'\n' {
                while (read32(DW_UART_LSR) & DW_UART_LSR_THRE) == 0 {
                    // Wait until writable
                }
                write32(b'\r' as u32, DW_UART_THR);
            }

            while (read32(DW_UART_LSR) & DW_UART_LSR_THRE) == 0 {
                // Wait until writable
            }
            write32(c as u32, DW_UART_THR);
        }
        return;
    }

    if dw_uart_putchar_nb(c) {
        return;
    }

    let mut timeout = 10000;
    while timeout > 0 {
        if dw_uart_putchar_nb(c) {
            return;
        }
        for _ in 0..100 {
            unsafe {
                core::arch::asm!("nop");
            }
        }
        timeout -= 1;
    }
    // logger_warn("DWC UART TX buffer full, dropping character\n");
}

// Put string
pub fn dw_uart_putstr(s: &str) {
    for byte in s.bytes() {
        dw_uart_putchar(byte);
    }
}

// Flush UART
pub fn dw_uart_flush() {
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return;
    }

    let mut timeout = 100000;
    while timeout > 0 {
        unsafe {
            TX_BUFFER.lock.lock();
            let empty = TX_BUFFER.is_empty();
            TX_BUFFER.lock.unlock();
            if empty {
                break;
            }
        }
        for _ in 0..10 {
            unsafe {
                core::arch::asm!("nop");
            }
        }
        timeout -= 1;
    }

    timeout = 10000;
    while timeout > 0 {
        if dw_uart_tx_ready() {
            break;
        }
        for _ in 0..10 {
            unsafe {
                core::arch::asm!("nop");
            }
        }
        timeout -= 1;
    }
}

// Non-blocking get character
pub fn dw_uart_getchar_nb() -> Option<u8> {
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return None;
    }

    unsafe {
        RX_BUFFER.lock.lock();
        let result = RX_BUFFER.get();
        RX_BUFFER.lock.unlock();
        result
    }
}

// Check if RX data is available
pub fn dw_uart_rx_available() -> bool {
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return false;
    }

    unsafe {
        RX_BUFFER.lock.lock();
        let available = !RX_BUFFER.is_empty();
        RX_BUFFER.lock.unlock();
        available
    }
}

// Get TX buffer usage
pub fn dw_uart_tx_buffer_usage() -> u32 {
    if !DW_UART_INITIALIZED.load(Ordering::Relaxed) {
        return 0;
    }

    unsafe {
        TX_BUFFER.lock.lock();
        let usage = TX_BUFFER.count as u32;
        TX_BUFFER.lock.unlock();
        usage
    }
}

// Get statistics
pub fn dw_uart_get_stats() -> (u32, u32, u32, u32) {
    let tx_irqs = TX_IRQ_COUNT.load(Ordering::Relaxed);
    let rx_irqs = RX_IRQ_COUNT.load(Ordering::Relaxed);
    let tx_usage = dw_uart_tx_buffer_usage();
    let rx_usage = unsafe {
        RX_BUFFER.lock.lock();
        let usage = RX_BUFFER.count as u32;
        RX_BUFFER.lock.unlock();
        usage
    };
    (tx_irqs, rx_irqs, tx_usage, rx_usage)
}

// Debug function: check if TX interrupt is enabled
pub fn dw_uart_is_tx_interrupt_enabled() -> bool {
    unsafe {
        let ier = read32(DW_UART_IER);
        (ier & DW_UART_IER_THRI) != 0
    }
}

// Debug function: get last IIR value
pub fn dw_uart_get_last_iir() -> u32 {
    LAST_IIR_VALUE.load(Ordering::Relaxed)
}

// Debug function: get total sent bytes
pub fn dw_uart_get_tx_sent_total() -> u32 {
    TX_SENT_TOTAL.load(Ordering::Relaxed)
}
