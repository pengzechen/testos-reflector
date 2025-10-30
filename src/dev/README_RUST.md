# DW UART Rust Module

This directory contains the Rust implementation of the DesignWare UART driver.

## Structure

- `t_dw_uart.rs` - Main UART driver implementation in Rust, mirroring the logic from `t_uart_dwp.c`
- `mod.rs` - Module definition file

## Overview

The Rust implementation provides the same functionality as the C version:

### Key Features

1. **Interrupt-driven I/O** - Uses interrupts for efficient TX/RX operations
2. **Ring buffers** - Separate 1024-byte TX and RX buffers
3. **Thread-safe** - Uses spinlocks for concurrent access protection
4. **Non-blocking API** - Provides both blocking and non-blocking character I/O

### Register Definitions

The module defines all DW UART registers (8250/16550 compatible):
- RBR/THR - Receive/Transmit Buffer Register
- IER - Interrupt Enable Register
- IIR/FCR - Interrupt ID / FIFO Control Register
- LCR - Line Control Register
- LSR - Line Status Register
- USR - UART Status Register
- DLL/DLM - Divisor Latch Low/High (for baud rate)

### Main Functions

- `dw_uart_init()` - Initialize UART hardware and buffers
- `dw_uart_putchar(c)` - Blocking character output
- `dw_uart_putchar_nb(c)` - Non-blocking character output
- `dw_uart_putstr(s)` - String output
- `dw_uart_getchar_nb()` - Non-blocking character input
- `dw_uart_rx_available()` - Check if RX data available
- `dw_uart_flush()` - Flush TX buffer
- `dw_uart_interrupt_handler(sp)` - Interrupt handler
- `dw_uart_get_stats()` - Get debug statistics

### Implementation Details

The Rust implementation closely follows the C version:

1. **Atomic operations** - Uses `AtomicBool` and `AtomicU32` for thread-safe state
2. **Spinlocks** - Custom spinlock implementation for buffer protection
3. **MMIO access** - Uses `read_volatile` and `write_volatile` for register access
4. **Buffer management** - Circular buffer with head/tail pointers
5. **Interrupt handling** - Handles both RX and TX interrupts with batch processing

### Differences from C Version

- Uses Rust's type system for safety (no null pointers)
- Uses `Option<u8>` for getchar instead of boolean + pointer
- Uses atomic types instead of volatile variables
- More idiomatic Rust patterns (match, if let, etc.)

### Integration Notes

To integrate this Rust module with the existing C codebase:

1. **Configuration** - Update `UART_BASE` and `UART_IRQ` constants with actual values
2. **Dependencies** - Implement or link required functions:
   - IRQ installation (`irq_install`)
   - GICv3 functions (`gicv3_*`)
   - Logger functions (optional)
3. **FFI** - May need to create C-compatible wrapper functions using `#[no_mangle]` and `extern "C"`
4. **Build system** - Add Rust compilation to Makefile or use a build script

### Example Usage (Rust)

```rust
// Initialize UART
dw_uart_init();

// Print a string
dw_uart_putstr("Hello from Rust UART!\n");

// Non-blocking character input
if let Some(c) = dw_uart_getchar_nb() {
    // Echo character
    dw_uart_putchar(c);
}

// Get statistics
let (tx_irqs, rx_irqs, tx_usage, rx_usage) = dw_uart_get_stats();
```

### Building

Since this is a bare-metal Rust module, it needs to be compiled with:
- `#![no_std]` - No standard library
- Target: `aarch64-unknown-none` or similar bare-metal target
- Appropriate linker settings

## Compatibility

This Rust implementation is designed to be a drop-in replacement for the C version, with the same:
- Register layout and offsets
- Buffer sizes
- Interrupt handling logic
- Baud rate configuration (24MHz, 1,500,000 bps)
- FIFO configuration

## Testing

To test this module:
1. Configure UART_BASE and UART_IRQ for your platform
2. Compile with appropriate target and flags
3. Test basic character output
4. Test interrupt-driven I/O
5. Verify buffer overflow handling
6. Test newline conversion (\n -> \r\n)

## Notes

- The module currently has placeholder implementations for:
  - IRQ installation
  - GICv3 configuration
  - Logger functions
  
- These need to be connected to actual implementations for full functionality
- The spinlock implementation is basic and may need platform-specific optimizations
