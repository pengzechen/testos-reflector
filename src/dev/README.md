# Device Drivers Directory

This directory contains device driver implementations in both C and Rust.

## Files Overview

### Rust Implementations (New)
| File | Lines | Description |
|------|-------|-------------|
| `mod.rs` | 5 | Rust module definition |
| `t_dw_uart.rs` | 489 | DW UART driver (Rust) |
| `xmodem_dw_uart.rs` | 291 | XMODEM-1K protocol (Rust) |

### C Implementations (Original)
| File | Lines | Description |
|------|-------|-------------|
| `t_uart_dwp.c` | 399 | DW UART driver (C) |
| `xmodem_dw_uart.c` | 269 | XMODEM-1K protocol (C) |
| `t_gicv3.c` | ~200 | GICv3 interrupt controller |
| `t_timer.c` | ~200 | ARM timer driver |
| `scmi.c` | ~350 | System Control interface |
| `cru.c` | ~500 | Clock & Reset Unit |

### Documentation
| File | Size | Description |
|------|------|-------------|
| `README_RUST.md` | 4.1KB | Comprehensive Rust module docs |
| `COMPARISON.md` | 6.0KB | C vs Rust detailed comparison |
| `USAGE_EXAMPLES.md` | 9.6KB | Practical usage examples |
| `RUST_MODULE_SUMMARY.md` | 8.3KB | Complete reference guide |

## Quick Start - Using Rust Modules

### 1. View the Summary
```bash
cat src/dev/RUST_MODULE_SUMMARY.md
```

### 2. Check Examples
```bash
cat src/dev/USAGE_EXAMPLES.md
```

### 3. Compare with C
```bash
cat src/dev/COMPARISON.md
```

## Module Structure

```
src/dev/
├── Rust Implementation
│   ├── mod.rs                    # Module definition
│   ├── t_dw_uart.rs             # UART driver
│   └── xmodem_dw_uart.rs        # XMODEM protocol
│
├── C Implementation (Original)
│   ├── t_uart_dwp.c             # UART driver
│   ├── xmodem_dw_uart.c         # XMODEM protocol
│   ├── t_gicv3.c                # Interrupt controller
│   ├── t_timer.c                # Timer
│   ├── scmi.c                   # System control
│   └── cru.c                    # Clock & reset
│
└── Documentation
    ├── README.md                 # This file
    ├── README_RUST.md           # Rust docs
    ├── COMPARISON.md            # C vs Rust
    ├── USAGE_EXAMPLES.md        # Examples
    └── RUST_MODULE_SUMMARY.md   # Complete ref
```

## Key Features of Rust Implementation

✅ **Complete Feature Parity**
- All C functions implemented
- Same register definitions
- Same interrupt logic
- Same buffer sizes
- Same performance

✅ **Enhanced Safety**
- Memory safety guarantees
- Type safety at compile time
- No null pointer dereferences
- No buffer overflows (compile-time checked)
- Thread-safe by design

✅ **Modern API**
- Returns `Option<T>` instead of bool+pointer
- Returns `Result<T, E>` for operations that can fail
- Uses string slices (`&str`) instead of raw pointers
- Type-safe register access

✅ **Well Documented**
- 4 comprehensive documentation files
- Inline code comments
- Usage examples
- Integration guide

## Function Mapping

| C Function | Rust Function | Status |
|------------|---------------|--------|
| `dw_uart_init()` | `dw_uart_init()` | ✅ Implemented |
| `dw_uart_putchar()` | `dw_uart_putchar()` | ✅ Implemented |
| `dw_uart_putchar_nb()` | `dw_uart_putchar_nb()` | ✅ Implemented |
| `dw_uart_putstr()` | `dw_uart_putstr()` | ✅ Implemented |
| `dw_uart_getchar_nb()` | `dw_uart_getchar_nb()` | ✅ Implemented |
| `dw_uart_rx_available()` | `dw_uart_rx_available()` | ✅ Implemented |
| `dw_uart_flush()` | `dw_uart_flush()` | ✅ Implemented |
| `dw_uart_interrupt_handler()` | `dw_uart_interrupt_handler()` | ✅ Implemented |
| `xmodem_receive_1k()` | `xmodem_receive_1k()` | ✅ Implemented |

## Usage Example

```rust
use crate::dev::{t_dw_uart, xmodem_dw_uart};

fn main() {
    // Initialize UART
    t_dw_uart::dw_uart_init();
    
    // Print message
    t_dw_uart::dw_uart_putstr("Hello from Rust!\n");
    
    // Read character
    if let Some(c) = t_dw_uart::dw_uart_getchar_nb() {
        t_dw_uart::dw_uart_putchar(c);
    }
    
    // Receive file via XMODEM
    let mut buffer = [0u8; 65536];
    match xmodem_dw_uart::xmodem_receive_1k(&mut buffer, 65536) {
        Ok(size) => {
            t_dw_uart::dw_uart_putstr("File received!\n");
        }
        Err(_) => {
            t_dw_uart::dw_uart_putstr("Transfer failed!\n");
        }
    }
}
```

## Integration

To use the Rust modules:

1. Configure `UART_BASE` and `UART_IRQ` in `t_dw_uart.rs`
2. Provide platform-specific functions (IRQ, GIC, etc.)
3. Build with `cargo build --target aarch64-unknown-none`
4. Link the resulting `.a` file with your C code

For detailed integration instructions, see `RUST_MODULE_SUMMARY.md`.

## Testing

### C Implementation
```bash
make clean && make
```

### Rust Implementation
```bash
cargo build --target aarch64-unknown-none
cargo test  # For unit tests (use x86_64 target)
```

## Contributing

When modifying the drivers:
1. Keep C and Rust implementations in sync
2. Update documentation when changing APIs
3. Test on actual hardware when possible
4. Run both C and Rust builds

## License

Same license as the parent project.

---

For more information:
- See `RUST_MODULE_SUMMARY.md` for complete Rust reference
- See `COMPARISON.md` for detailed C vs Rust comparison
- See `USAGE_EXAMPLES.md` for practical examples
