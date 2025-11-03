# ELF Loader API Documentation

## Overview

The ELF loader provides functionality to load and execute ELF (Executable and Linkable Format) files on ARM64 (AArch64) architecture. It supports both executable files and dynamic libraries.

## Architecture

The bootloader copies user-mode programs to memory above 2GB (0x80000000). Each program is described by an `elf_descriptor_t` structure containing:
- ELF file name
- Start address in memory (where bootloader placed it)
- Size in bytes

## Data Structures

### ELF Descriptor

```c
typedef struct {
    char     name[64];       // ELF file name
    uint64_t start_addr;     // Start address in memory (>= 0x80000000)
    uint64_t size;           // Size in bytes
    uint64_t entry_point;    // Entry point address (filled by loader)
    uint16_t type;           // ELF type (ET_EXEC, ET_DYN, etc.)
    bool     is_loaded;      // Whether the ELF is loaded
} elf_descriptor_t;
```

### Error Codes

```c
typedef enum {
    ELF_SUCCESS = 0,
    ELF_ERROR_INVALID_MAGIC,
    ELF_ERROR_INVALID_CLASS,
    ELF_ERROR_INVALID_ENDIAN,
    ELF_ERROR_INVALID_VERSION,
    ELF_ERROR_INVALID_TYPE,
    ELF_ERROR_INVALID_MACHINE,
    ELF_ERROR_NO_MEMORY,
    ELF_ERROR_INVALID_SEGMENT,
    ELF_ERROR_RELOCATION_FAILED,
    ELF_ERROR_SYMBOL_NOT_FOUND,
    ELF_ERROR_INVALID_DESCRIPTOR
} elf_result_t;
```

## API Functions

### Header Validation

```c
elf_result_t elf_validate_header(const elf64_ehdr_t *ehdr);
```

Validates an ELF header to ensure it's a valid ARM64 64-bit ELF file.

**Returns:** `ELF_SUCCESS` if valid, error code otherwise.

### Loading Executables

```c
elf_result_t elf_load_executable(elf_descriptor_t *desc);
```

Loads an executable ELF file into memory and performs necessary relocations.

**Parameters:**
- `desc`: ELF descriptor with `name`, `start_addr`, and `size` filled in

**Returns:** `ELF_SUCCESS` if successful, error code otherwise.

**Side Effects:** Updates `entry_point`, `type`, and `is_loaded` fields in descriptor.

### Loading Dynamic Libraries

```c
elf_result_t elf_load_dynamic(elf_descriptor_t *desc, uint64_t base_addr);
```

Loads a dynamic library (shared object) ELF file into memory.

**Parameters:**
- `desc`: ELF descriptor with `name`, `start_addr`, and `size` filled in
- `base_addr`: Base address to load the library (0 for auto-allocation)

**Returns:** `ELF_SUCCESS` if successful, error code otherwise.

### Symbol Lookup

```c
elf_result_t elf_find_symbol(const elf_descriptor_t *desc,
                              const char *symbol_name,
                              uint64_t *symbol_addr);
```

Finds a symbol in a loaded ELF file.

**Parameters:**
- `desc`: Loaded ELF descriptor
- `symbol_name`: Name of the symbol to find
- `symbol_addr`: Output parameter for symbol address

**Returns:** `ELF_SUCCESS` if found, `ELF_ERROR_SYMBOL_NOT_FOUND` otherwise.

### Error Handling

```c
const char *elf_error_string(elf_result_t result);
```

Returns a human-readable error message for an error code.

### Debug Functions

```c
void elf_dump_descriptor(const elf_descriptor_t *desc);
void elf_dump_header(const elf64_ehdr_t *ehdr);
```

Dump ELF descriptor or header information for debugging.

## Usage Examples

### Example 1: Loading an Executable

```c
#include "lib/t_elf.h"
#include "lib/t_logger.h"

void load_user_program(void)
{
    // Bootloader has placed the ELF at 0x80000000
    elf_descriptor_t desc = {
        .name = "user_app.elf",
        .start_addr = 0x80000000,
        .size = 65536,  // 64KB
        .is_loaded = false
    };

    // Load the executable
    elf_result_t result = elf_load_executable(&desc);
    if (result != ELF_SUCCESS) {
        logger_error("Failed to load ELF: %s\n", elf_error_string(result));
        return;
    }

    logger_info("ELF loaded successfully!\n");
    logger_info("Entry point: 0x%llx\n", desc.entry_point);

    // Execute the program
    void (*entry)(void) = (void (*)(void))desc.entry_point;
    entry();
}
```

### Example 2: Loading a Dynamic Library

```c
#include "lib/t_elf.h"
#include "lib/t_logger.h"

void load_shared_library(void)
{
    // Bootloader has placed the shared library at 0x80100000
    elf_descriptor_t lib_desc = {
        .name = "libmath.so",
        .start_addr = 0x80100000,
        .size = 32768,  // 32KB
        .is_loaded = false
    };

    // Load the library (use 0 for auto base address)
    elf_result_t result = elf_load_dynamic(&lib_desc, 0);
    if (result != ELF_SUCCESS) {
        logger_error("Failed to load library: %s\n", elf_error_string(result));
        return;
    }

    logger_info("Library loaded successfully!\n");

    // Find a symbol in the library
    uint64_t func_addr;
    result = elf_find_symbol(&lib_desc, "sqrt", &func_addr);
    if (result == ELF_SUCCESS) {
        logger_info("Found sqrt() at 0x%llx\n", func_addr);
        
        // Call the function
        double (*sqrt_func)(double) = (double (*)(double))func_addr;
        double value = sqrt_func(16.0);
        logger_info("sqrt(16.0) = %f\n", value);
    }
}
```

### Example 3: Loading Multiple Programs

```c
#include "lib/t_elf.h"
#include "lib/t_logger.h"

#define MAX_PROGRAMS 16

typedef struct {
    const char *name;
    uint64_t    addr;
    uint64_t    size;
} bootloader_elf_info_t;

void load_all_programs(const bootloader_elf_info_t *elf_list, size_t count)
{
    elf_descriptor_t programs[MAX_PROGRAMS];
    
    if (count > MAX_PROGRAMS) {
        logger_error("Too many programs to load\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        // Initialize descriptor
        programs[i].start_addr = elf_list[i].addr;
        programs[i].size = elf_list[i].size;
        programs[i].is_loaded = false;
        
        // Copy name
        size_t name_len = 0;
        while (elf_list[i].name[name_len] && name_len < 63) {
            programs[i].name[name_len] = elf_list[i].name[name_len];
            name_len++;
        }
        programs[i].name[name_len] = '\0';

        // Load the program
        elf_result_t result = elf_load_executable(&programs[i]);
        if (result == ELF_SUCCESS) {
            logger_info("Loaded: %s (entry: 0x%llx)\n",
                        programs[i].name, programs[i].entry_point);
        } else {
            logger_error("Failed to load %s: %s\n",
                         programs[i].name, elf_error_string(result));
        }
    }
}
```

### Example 4: Validating an ELF Before Loading

```c
#include "lib/t_elf.h"
#include "lib/t_logger.h"

bool validate_and_load(uint64_t elf_addr)
{
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_addr;
    
    // Validate header first
    elf_result_t result = elf_validate_header(ehdr);
    if (result != ELF_SUCCESS) {
        logger_error("Invalid ELF header: %s\n", elf_error_string(result));
        elf_dump_header(ehdr);  // Show what we got
        return false;
    }

    // Check ELF type
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        logger_error("Unsupported ELF type: %u\n", ehdr->e_type);
        return false;
    }

    // Create descriptor and load
    elf_descriptor_t desc = {
        .name = "validated.elf",
        .start_addr = elf_addr,
        .size = 0x10000,  // Size should be known from bootloader
        .is_loaded = false
    };

    result = elf_load_executable(&desc);
    return (result == ELF_SUCCESS);
}
```

## Memory Layout

The bootloader places ELF files in memory starting at 2GB (0x80000000):

```
0x00000000 - 0x7FFFFFFF: Kernel space
0x80000000 - 0xFFFFFFFF: User programs (loaded by bootloader)
  0x80000000: First ELF
  0x80100000: Second ELF
  ...
```

## Supported Features

### Executables (ET_EXEC)
- Loading of loadable segments (PT_LOAD)
- BSS section initialization
- Entry point execution

### Shared Objects (ET_DYN)
- Position-independent loading
- Dynamic relocations (RELA)
- Symbol resolution
- Supported relocation types:
  - R_AARCH64_NONE
  - R_AARCH64_RELATIVE
  - R_AARCH64_GLOB_DAT
  - R_AARCH64_JUMP_SLOT
  - R_AARCH64_ABS64

## Limitations

1. No dynamic linker - libraries must be pre-linked
2. Limited relocation type support
3. No lazy binding (PLT/GOT are resolved at load time)
4. No TLS (Thread-Local Storage) support yet
5. Symbol lookup is linear (no hash table optimization)

## Testing

Run the test suite to verify ELF loader functionality:

```c
void t_elf_run_tests(void);
```

This runs validation tests for:
- ELF header parsing
- Descriptor management
- Error handling
- Debug output

## Integration

To integrate the ELF loader into your kernel:

1. Include the header: `#include "lib/t_elf.h"`
2. Receive ELF information from bootloader
3. Create descriptors for each ELF
4. Load executables with `elf_load_executable()`
5. Load libraries with `elf_load_dynamic()`
6. Execute programs by jumping to entry points

## Future Enhancements

- Dynamic linker support
- Hash table for symbol lookup
- Additional relocation types
- TLS support
- Lazy binding (PLT/GOT)
- ELF validation checksum
- Memory protection (page permissions)
