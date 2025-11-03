# ELF Loader Quick Reference Guide

## For Developers

### Quick Start (3 Steps)

```c
// 1. Include the header
#include "lib/t_elf_loader.h"

// 2. Initialize with bootloader table
elf_loader_init(0x7F000000);

// 3. Execute a program
elf_loader_execute("init");
```

### Common Operations

#### Load and List All Programs
```c
size_t count = elf_loader_init(table_addr);
elf_loader_list_programs();
```

#### Find a Program
```c
const elf_descriptor_t *prog = elf_loader_get_program("shell");
if (prog) {
    printf("Entry: 0x%llx\n", prog->entry_point);
}
```

#### Find a Symbol in a Library
```c
const elf_descriptor_t *lib = elf_loader_get_program("libc.so");
uint64_t addr;
if (elf_find_symbol(lib, "printf", &addr) == ELF_SUCCESS) {
    void (*printf_func)(const char*, ...) = (void*)addr;
    printf_func("Hello from dynamic library!\n");
}
```

### For Bootloader Developers

#### Required Memory Layout
- Place ELF files at addresses >= 0x80000000 (2GB)
- Create ELF table at 0x7F000000
- Align each ELF to 64KB boundaries (recommended)

#### ELF Table Structure
```c
typedef struct {
    uint32_t magic;    // Set to 0x454C4654 ("ELFT")
    uint32_t version;  // Set to 1
    uint32_t count;    // Number of ELF files
    uint32_t reserved; // Set to 0
    struct {
        char     name[64];
        uint64_t start_addr;  // >= 0x80000000
        uint64_t size;
        uint32_t flags;       // 0=executable, 1=library
        uint32_t reserved;
    } entries[32];
} bootloader_elf_table_t;
```

#### Bootloader Workflow
```
1. Load kernel to 0x400000
2. Load user programs to >= 0x80000000
3. Create ELF table at 0x7F000000
4. Set table->magic = 0x454C4654
5. Add entries for each program
6. Jump to kernel with table address
```

### Error Codes

| Code | Meaning |
|------|---------|
| `ELF_SUCCESS` | Operation successful |
| `ELF_ERROR_INVALID_MAGIC` | Not a valid ELF file |
| `ELF_ERROR_INVALID_CLASS` | Not 64-bit ELF |
| `ELF_ERROR_INVALID_MACHINE` | Not AArch64 |
| `ELF_ERROR_SYMBOL_NOT_FOUND` | Symbol lookup failed |

Use `elf_error_string(result)` to get human-readable message.

### Memory Regions

```
0x00000000              Kernel code
    ↓
0x7F000000              ELF table (bootloader → kernel)
0x7FFFFFFF              End of kernel space
────────────────────────────────────────────────
0x80000000              User program space begins
    ↓                   (executables and libraries)
0xFFFFFFFF              End of memory
```

### Testing

```c
// Run basic tests
t_elf_run_tests();

// See bootloader example
test_bootloader_example();
```

### Important Notes

⚠️ **No dynamic linker** - Libraries must be pre-linked
⚠️ **Limited relocations** - Only basic AArch64 types supported
⚠️ **No TLS support** - Thread-Local Storage not implemented
⚠️ **Linear symbol lookup** - Performance may degrade with many symbols

### Files to Include

```c
#include "lib/t_elf.h"         // Core ELF definitions
#include "lib/t_elf_loader.h"  // High-level interface
```

### Example Programs

See these files for complete examples:
- `src/lib/t_elf_test.c` - Unit tests
- `src/lib/t_elf_loader.c` - Integration examples
- `src/lib/t_elf_bootloader_example.c` - Bootloader examples
- `docs/elf_loader.md` - Full API documentation
- `ELF_LOADER_README.md` - Complete guide (Chinese + English)

### Getting Help

1. Check `ELF_LOADER_README.md` for detailed documentation
2. Review `docs/elf_loader.md` for API reference
3. Look at example code in `src/lib/t_elf_bootloader_example.c`
4. Run tests with `t_elf_run_tests()` to verify functionality

### Build Integration

Add to your Makefile:
```makefile
C_SOURCES += src/lib/t_elf.c
C_SOURCES += src/lib/t_elf_loader.c
C_SOURCES += src/lib/t_elf_test.c
C_SOURCES += src/lib/t_elf_bootloader_example.c
```

### Kernel Initialization Example

```c
void t_kernel_main(uint64_t cpu_id)
{
    // ... other initialization ...
    
    // Initialize memory first
    t_mem_init(heap_size);
    
    // Load user programs from bootloader
    size_t loaded = elf_loader_init(0x7F000000);
    logger_info("Loaded %zu user programs\n", loaded);
    
    // Execute init process
    if (elf_loader_execute("init")) {
        logger_info("Init process started\n");
    }
    
    // Continue kernel operation...
}
```

---

## Troubleshooting

### "Invalid ELF magic"
- Check that file is actually an ELF file
- Verify file wasn't corrupted during load

### "Invalid machine type"
- Ensure ELF is compiled for ARM64 (AArch64)
- Check compiler target: `aarch64-linux-gnu-gcc`

### "Symbol not found"
- Verify symbol is exported (not static)
- Check spelling of symbol name
- Ensure library is loaded before lookup

### "Invalid address"
- Programs must be at >= 0x80000000
- Check bootloader load addresses

### "Relocation failed"
- Some relocation types not supported yet
- Try recompiling with different flags
- Check if library needs to be PIE

---

**For full documentation, see:**
- `ELF_LOADER_README.md` - Complete guide (Chinese + English)
- `docs/elf_loader.md` - Detailed API reference with examples
