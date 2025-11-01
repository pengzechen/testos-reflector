# Quick Start Guide - Running Musl Programs

This guide shows how to build and run statically linked musl programs on testos.

## Prerequisites

You need the following tools installed:
- `aarch64-linux-musl-gcc` - Cross compiler for AArch64 with musl
- `qemu-system-aarch64` - For testing in QEMU (optional)
- `minicom` or `screen` - For serial communication

## Building the Kernel

```bash
# Clone the repository
git clone https://github.com/pengzechen/testos-reflector.git
cd testos-reflector

# Checkout the musl support branch
git checkout copilot/support-musl-static-linking

# Build the kernel
make clean
make
```

This will produce `build/testos.bin` which is the kernel binary.

## Building a User Program

```bash
# Navigate to the usertest directory
cd usertest

# Build the test program
make

# This produces a static ELF binary: hello
file hello
# Output: hello: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, not stripped
```

## Running in QEMU

1. Start the kernel in QEMU:
```bash
cd ..
make qemu
```

2. You should see the kernel boot and display a prompt.

3. Press 'h' to see available commands:
```
=== UART Test Commands ===
  h/H - Show this help
  s/S - Show statistics
  t/T - Show current time
  p/P - Toggle periodic print
  x/X - Start XMODEM file receive
  d/D - Dump received file data
  e/E - Execute received ELF as user program
  q/Q - Quit (return to WFI loop)
==========================
```

4. Transfer your program using XMODEM:
   - Press 'x' to start XMODEM receive
   - In another terminal, send the file:
     ```bash
     sx -k usertest/hello < /dev/pts/X > /dev/pts/X
     ```
     (Replace /dev/pts/X with your QEMU serial device)

5. Execute the program:
   - Press 'e' to execute the received ELF
   - You should see output from the user program:
     ```
     === Executing User Program ===
     ELF validation passed. Loading and executing...
     Hello from user mode!
     argc = 0
     Test completed successfully.
     Process exited with status: 0
     ```

## Running on Real Hardware (Orange Pi 5)

1. Build the kernel image:
```bash
make uimg
```

2. Copy to TFTP server or SD card:
```bash
# For TFTP boot
make uboot
# Or manually copy testos-reflector-src_aarch64-opi5p.uimg
```

3. Boot the kernel on Orange Pi 5

4. Connect via serial console (e.g., using minicom)

5. Follow the same steps as QEMU to transfer and execute programs

## Creating Your Own Programs

Create a new C file in the `usertest` directory:

```c
// myprogram.c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("My program is running!\n");
    printf("I can use printf, malloc, and other standard library functions\n");
    
    char *buffer = malloc(100);
    if (buffer) {
        printf("malloc worked! Got buffer at %p\n", buffer);
        free(buffer);
    }
    
    return 42;
}
```

Update the Makefile:
```makefile
PROGRAMS = hello myprogram

myprogram: myprogram.c
	$(CC) $(CFLAGS) -o myprogram myprogram.c
```

Build and run:
```bash
cd usertest
make
# Transfer and execute as described above
```

## Supported C Library Features

The following standard library features work:
- ✅ `printf`, `fprintf`, `sprintf` - Output functions
- ✅ `malloc`, `free`, `calloc`, `realloc` - Memory allocation
- ✅ `exit` - Program termination
- ✅ `getpid`, `getuid`, etc. - Process information
- ✅ Basic time functions

Not supported (yet):
- ❌ File I/O (open, read, write to files)
- ❌ Network functions
- ❌ Process creation (fork, exec)
- ❌ Threads (pthread)
- ❌ Signals

## Troubleshooting

### "Invalid ELF file" error
- Make sure you compiled with `-static` flag
- Verify it's an AArch64 binary: `file yourprogram`
- Check that musl compiler is used, not glibc

### Program crashes immediately
- Check that your program doesn't use unsupported syscalls
- Add debug output to see where it fails
- Verify stack size is sufficient (default is 1MB)

### XMODEM transfer fails
- Check baud rate matches (usually 115200)
- Use `-k` flag for XMODEM-1K protocol
- Try reducing transfer speed if errors occur

## Next Steps

- Read `MUSL_SUPPORT.md` for technical details
- Look at `src/lib/syscall.c` to see which syscalls are implemented
- Extend the syscall table to add more functionality
- Implement file system support for more complex programs

## Example Session

```
Hi
[INFO] Compiled on Nov  1 2024 at 09:30:00
[INFO] main core id: 0
...
Press 'h' for help
> h
=== UART Test Commands ===
  h/H - Show this help
  x/X - Start XMODEM file receive
  e/E - Execute received ELF as user program
  ...

> x
=== Starting XMODEM-1K Receive ===
Please start sending file using XMODEM-1K protocol...
[Receiving...]
=== XMODEM Receive SUCCESS ===
Received: 12345 bytes

> e
=== Executing User Program ===
ELF validation passed. Loading and executing...
Hello from user mode!
Test completed successfully.
Process exited with status: 0
```

Congratulations! You've successfully run a musl program on testos!
