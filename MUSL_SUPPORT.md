# Musl Static Program Support

This branch adds support for running statically linked musl programs in testos kernel.

## Features Implemented

### 1. ELF Loader
- ELF64 header validation
- Program segment loading
- Support for both ET_EXEC and ET_DYN executables
- Memory allocation for program segments

### 2. Syscall Infrastructure
The following syscalls are implemented:
- `sys_read` - Read from file descriptor (stdin support)
- `sys_write` - Write to file descriptor (stdout/stderr support)
- `sys_exit` - Terminate process
- `sys_exit_group` - Terminate process group
- `sys_brk` - Change program break (heap management)
- `sys_mmap` - Map memory (anonymous mappings)
- `sys_munmap` - Unmap memory
- `sys_getpid` - Get process ID
- `sys_gettid` - Get thread ID
- `sys_getuid` - Get user ID
- `sys_getgid` - Get group ID
- `sys_geteuid` - Get effective user ID
- `sys_getegid` - Get effective group ID
- `sys_getppid` - Get parent process ID
- `sys_clock_gettime` - Get current time
- `sys_uname` - Get system information
- `sys_set_tid_address` - Set thread ID address

### 3. Process Management
- Process control block structure
- Memory region tracking
- Basic process state management

### 4. User Mode Support
- EL0 (user mode) transition from EL1 (kernel mode)
- User stack allocation
- Register cleanup before entering user mode
- Exception handling for syscalls (SVC instructions)

## Usage

### Building User Programs

User programs should be compiled with musl as static binaries:

```bash
cd usertest
make
```

This will produce a `hello` binary that is statically linked with musl.

### Loading and Running Programs

Currently, programs need to be loaded into kernel memory and then executed. The main APIs are:

```c
#include "lib/usermode.h"

// Load and execute ELF program
int exec_user_program(const void *elf_data);
```

### Example Integration

In your kernel code (e.g., in t_entry.c):

```c
#include "lib/usermode.h"

// Assume elf_data points to a loaded ELF binary
extern uint8_t _binary_hello_start[];

// Execute the program
exec_user_program(_binary_hello_start);
```

## Architecture

### Memory Layout
- Kernel: 0x00400000 (4MB)
- User heap: 0x10000000 (256MB)
- User stack: Allocated dynamically (1MB default)

### Exception Handling
- Synchronous exceptions from EL0 are handled in `handle_sync_exception()`
- SVC (system call) exceptions (EC=0x15) trigger syscall handling
- Syscall number is passed in x8 register
- Arguments are passed in x0-x5 registers
- Return value is returned in x0 register

## Limitations

Current limitations:
1. No dynamic linking support (static binaries only)
2. No file system (no open/close/read from files)
3. No process isolation (single address space)
4. No process scheduling (single process at a time)
5. Limited syscall support (only basic syscalls)
6. No signal handling
7. No fork/exec support

## Future Improvements

Potential enhancements:
- Add page table management for memory isolation
- Implement process scheduling
- Add more syscalls (open, close, etc.)
- Support for multiple processes
- Add file system support
- Implement signal handling
- Add fork/exec support

## Testing

A simple test program is provided in `usertest/hello.c`:

```c
int main(int argc, char *argv[])
{
    printf("Hello from user mode!\n");
    printf("argc = %d\n", argc);
    return 0;
}
```

This program tests:
- Transition to user mode
- printf (which uses sys_write internally)
- Program exit (sys_exit)

## Technical Details

### Syscall Mechanism
1. User program executes `svc #0` instruction
2. CPU traps to EL1 and jumps to exception vector
3. Exception handler saves all registers
4. `handle_sync_exception()` checks Exception Class (EC)
5. If EC=0x15 (SVC from EL0), calls `handle_syscall_exception()`
6. Syscall handler dispatches to appropriate syscall implementation
7. Return value is stored in x0
8. PC is advanced past SVC instruction
9. Registers are restored and `eret` returns to user mode

### ELF Loading
1. Validate ELF magic and header
2. Check for AArch64 architecture
3. Parse program headers
4. Load PT_LOAD segments into memory
5. Zero-fill BSS sections
6. Return entry point address

## License

Same as testos kernel.
