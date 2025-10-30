# Rust DW UART Module - Usage Examples

This document provides practical examples of how to use the Rust DW UART module.

## Basic Setup

### 1. Configuration

First, update the constants in `t_dw_uart.rs` with your platform values:

```rust
const UART_BASE: u64 = 0xFEB50000; // Replace with your UART base address
const UART_IRQ: u32 = 148;         // Replace with your UART IRQ number
```

### 2. Initialization

Initialize the UART driver at system startup:

```rust
use crate::dev::t_dw_uart;

fn main() {
    // Initialize UART
    t_dw_uart::dw_uart_init();
    
    // UART is now ready to use
    t_dw_uart::dw_uart_putstr("System initialized!\n");
}
```

## Basic I/O Operations

### Character Output

```rust
use crate::dev::t_dw_uart;

// Blocking character output
t_dw_uart::dw_uart_putchar(b'A');

// Non-blocking character output
if t_dw_uart::dw_uart_putchar_nb(b'B') {
    // Character was sent
} else {
    // Buffer was full
}

// String output
t_dw_uart::dw_uart_putstr("Hello, World!\n");
```

### Character Input

```rust
use crate::dev::t_dw_uart;

// Check if data is available
if t_dw_uart::dw_uart_rx_available() {
    // Non-blocking character input
    if let Some(c) = t_dw_uart::dw_uart_getchar_nb() {
        // Echo the character back
        t_dw_uart::dw_uart_putchar(c);
    }
}
```

## Advanced Usage

### Echo Server

Simple echo server that reads and echoes back characters:

```rust
use crate::dev::t_dw_uart;

pub fn echo_server() {
    t_dw_uart::dw_uart_putstr("Echo server started. Type something:\n");
    
    loop {
        if let Some(c) = t_dw_uart::dw_uart_getchar_nb() {
            // Echo character
            t_dw_uart::dw_uart_putchar(c);
            
            // Exit on 'q'
            if c == b'q' {
                t_dw_uart::dw_uart_putstr("\nExiting...\n");
                break;
            }
        }
    }
}
```

### Command Line Interface

Simple CLI that processes commands:

```rust
use crate::dev::t_dw_uart;

const MAX_CMD_LEN: usize = 128;

pub fn simple_cli() {
    let mut cmd_buffer = [0u8; MAX_CMD_LEN];
    let mut cmd_len = 0;
    
    t_dw_uart::dw_uart_putstr("CLI> ");
    
    loop {
        if let Some(c) = t_dw_uart::dw_uart_getchar_nb() {
            match c {
                b'\r' | b'\n' => {
                    // Execute command
                    t_dw_uart::dw_uart_putstr("\n");
                    
                    if cmd_len > 0 {
                        // Process command
                        let cmd = core::str::from_utf8(&cmd_buffer[..cmd_len]).unwrap_or("");
                        process_command(cmd);
                        
                        // Reset buffer
                        cmd_len = 0;
                    }
                    
                    t_dw_uart::dw_uart_putstr("CLI> ");
                }
                b'\x7F' | b'\x08' => {
                    // Backspace
                    if cmd_len > 0 {
                        cmd_len -= 1;
                        t_dw_uart::dw_uart_putstr("\x08 \x08");
                    }
                }
                _ if cmd_len < MAX_CMD_LEN => {
                    // Add to buffer
                    cmd_buffer[cmd_len] = c;
                    cmd_len += 1;
                    
                    // Echo character
                    t_dw_uart::dw_uart_putchar(c);
                }
                _ => {
                    // Buffer full
                    t_dw_uart::dw_uart_putstr("\nCommand too long!\n");
                    cmd_len = 0;
                    t_dw_uart::dw_uart_putstr("CLI> ");
                }
            }
        }
    }
}

fn process_command(cmd: &str) {
    match cmd.trim() {
        "help" => {
            t_dw_uart::dw_uart_putstr("Available commands:\n");
            t_dw_uart::dw_uart_putstr("  help  - Show this help\n");
            t_dw_uart::dw_uart_putstr("  stats - Show UART statistics\n");
            t_dw_uart::dw_uart_putstr("  quit  - Exit\n");
        }
        "stats" => {
            show_stats();
        }
        "quit" => {
            t_dw_uart::dw_uart_putstr("Goodbye!\n");
            // Exit application
        }
        "" => {
            // Empty command, do nothing
        }
        _ => {
            t_dw_uart::dw_uart_putstr("Unknown command: ");
            t_dw_uart::dw_uart_putstr(cmd);
            t_dw_uart::dw_uart_putstr("\n");
        }
    }
}

fn show_stats() {
    let (tx_irqs, rx_irqs, tx_usage, rx_usage) = t_dw_uart::dw_uart_get_stats();
    
    t_dw_uart::dw_uart_putstr("UART Statistics:\n");
    // Note: Need to implement number formatting
    t_dw_uart::dw_uart_putstr("  TX interrupts: (implementation needed)\n");
    t_dw_uart::dw_uart_putstr("  RX interrupts: (implementation needed)\n");
    t_dw_uart::dw_uart_putstr("  TX buffer usage: (implementation needed)\n");
    t_dw_uart::dw_uart_putstr("  RX buffer usage: (implementation needed)\n");
}
```

### Debug Statistics

Monitor UART performance:

```rust
use crate::dev::t_dw_uart;

pub fn show_uart_debug_info() {
    let (tx_irqs, rx_irqs, tx_usage, rx_usage) = t_dw_uart::dw_uart_get_stats();
    let last_iir = t_dw_uart::dw_uart_get_last_iir();
    let tx_sent_total = t_dw_uart::dw_uart_get_tx_sent_total();
    let tx_int_enabled = t_dw_uart::dw_uart_is_tx_interrupt_enabled();
    
    // Display statistics
    // (Note: You'll need to implement number-to-string conversion)
    t_dw_uart::dw_uart_putstr("=== UART Debug Info ===\n");
    // Print values...
}
```

## XMODEM File Transfer

### Receiving Files

Use XMODEM to receive files over serial:

```rust
use crate::dev::{t_dw_uart, xmodem_dw_uart};

const RECV_BUFFER_SIZE: usize = 1024 * 1024; // 1MB

pub fn receive_file() {
    let mut buffer = [0u8; RECV_BUFFER_SIZE];
    
    t_dw_uart::dw_uart_putstr("Ready to receive file via XMODEM-1K\n");
    t_dw_uart::dw_uart_putstr("Start sending now...\n");
    
    match xmodem_dw_uart::xmodem_receive_1k(&mut buffer, RECV_BUFFER_SIZE) {
        Ok(bytes_received) => {
            t_dw_uart::dw_uart_putstr("File received successfully!\n");
            // Process the received data
            process_received_data(&buffer[..bytes_received]);
        }
        Err(_) => {
            t_dw_uart::dw_uart_putstr("File transfer failed!\n");
        }
    }
}

fn process_received_data(data: &[u8]) {
    // Process the received file data
    t_dw_uart::dw_uart_putstr("Processing received data...\n");
    // Your processing logic here
}
```

### Host-side XMODEM Send

On Linux host, use `sx` to send files:

```bash
# Configure serial port
sudo stty -F /dev/ttyUSB0 1500000 raw -echo cs8 -cstopb -parenb

# Send file via XMODEM-1K
sudo sh -c 'sx -k yourfile.bin < /dev/ttyUSB0 > /dev/ttyUSB0'
```

## Integration with C Code

If you need to call Rust functions from C code:

### Create FFI Wrappers

Add to `t_dw_uart.rs`:

```rust
// FFI wrappers for C code
#[no_mangle]
pub extern "C" fn dw_uart_init_rust() {
    dw_uart_init();
}

#[no_mangle]
pub extern "C" fn dw_uart_putchar_rust(c: u8) {
    dw_uart_putchar(c);
}

#[no_mangle]
pub extern "C" fn dw_uart_putstr_rust(s: *const u8, len: usize) {
    if !s.is_null() {
        unsafe {
            let slice = core::slice::from_raw_parts(s, len);
            if let Ok(s) = core::str::from_utf8(slice) {
                dw_uart_putstr(s);
            }
        }
    }
}
```

### Call from C

```c
// Declare external Rust functions
extern void dw_uart_init_rust(void);
extern void dw_uart_putchar_rust(uint8_t c);
extern void dw_uart_putstr_rust(const uint8_t *s, size_t len);

// Use in C code
void my_c_function(void) {
    dw_uart_init_rust();
    dw_uart_putstr_rust("Hello from C!\n", 15);
}
```

## Building

### Cargo.toml Configuration

```toml
[package]
name = "testos-uart"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["staticlib"]

[profile.release]
panic = "abort"
lto = true
opt-level = "z"  # Optimize for size

[dependencies]
```

### Build for ARM64

```bash
# Build for bare-metal ARM64
cargo build --release --target aarch64-unknown-none

# The static library will be in:
# target/aarch64-unknown-none/release/libtestos_uart.a
```

### Link with C Code

In your Makefile:

```makefile
# Add Rust library to linker flags
RUST_LIB = target/aarch64-unknown-none/release/libtestos_uart.a
LDFLAGS += $(RUST_LIB)

# Build Rust library
$(RUST_LIB):
	cargo build --release --target aarch64-unknown-none
```

## Troubleshooting

### Common Issues

1. **No output**: Check UART_BASE address and ensure UART is properly initialized
2. **Garbled output**: Verify baud rate configuration matches host
3. **Missing characters**: Check buffer sizes if high throughput
4. **Interrupt not firing**: Verify GICv3 configuration and IRQ number

### Debug Functions

```rust
// Check interrupt status
let tx_enabled = t_dw_uart::dw_uart_is_tx_interrupt_enabled();

// Get last interrupt type
let last_iir = t_dw_uart::dw_uart_get_last_iir();

// Get total bytes sent
let total_sent = t_dw_uart::dw_uart_get_tx_sent_total();

// Get buffer usage
let tx_usage = t_dw_uart::dw_uart_tx_buffer_usage();
```

## Performance Tips

1. **Batch operations**: Use `dw_uart_putstr()` instead of multiple `dw_uart_putchar()` calls
2. **Non-blocking I/O**: Use `dw_uart_putchar_nb()` when you can't afford to wait
3. **Flush when needed**: Call `dw_uart_flush()` before critical operations
4. **Monitor buffers**: Check buffer usage if experiencing drops

## Safety Notes

- All MMIO operations use volatile reads/writes
- Buffers are protected by spinlocks
- Interrupt handlers are re-entrant safe
- No dynamic memory allocation (suitable for bare-metal)

## License

This Rust implementation mirrors the C version and maintains the same license terms.
