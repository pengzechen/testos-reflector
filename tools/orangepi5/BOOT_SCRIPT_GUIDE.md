# Boot Script Guide for ELF Loader

## Overview

The `boot.cmd` script is a U-Boot boot script that loads ELF files (libraries and executables) into memory and creates the ELF table structure that the kernel's ELF loader expects.

## Memory Layout

```
0x7F000000: ELF table (bootloader_elf_table_t structure)
0x80000000: libc.so (musl C library)
0x80100000: libm.so (math library)
0x80200000: libpthread.so (POSIX threads)
0x80300000: libdl.so (dynamic linking)
0x80400000: librt.so (real-time extensions)
0x81000000: [User programs start here]
```

## ELF Table Structure

```c
struct bootloader_elf_table_t {
    uint32_t magic;           // 0x454C4654 ("ELFT")
    uint32_t version;         // 1
    uint32_t count;           // number of ELF files
    uint32_t reserved;        // 0
    struct bootloader_elf_info_t entries[32] {
        char     name[64];     // ELF file name
        uint64_t start_addr;   // load address (≥ 0x80000000)
        uint64_t size;         // file size in bytes
        uint32_t flags;        // 1=library, 0=executable
        uint32_t reserved;     // 0
    };
};
```

Each entry is 88 bytes (0x58):
- name: 64 bytes (0x40)
- start_addr: 8 bytes
- size: 8 bytes
- flags: 4 bytes
- reserved: 4 bytes

## Adding Your Own Programs

### Step 1: Load Your ELF File

Add after the library loads (around line 30):

```bash
# Load my_program executable
ext4load mmc 1:1  0x81000000 my_program
setenv myprogram_size ${filesize}
```

### Step 2: Increment Count

Update the count in the header (line 60):

```bash
mw.l 0x7F000008 0x00000006   # count: 6 (was 5, now 5 libraries + 1 program)
```

### Step 3: Add Entry to Table

Each entry starts at: `0x7F000010 + (entry_index * 0x58)`

For entry 5 (6th entry, index 5):
- Start offset: 0x7F000010 + (5 * 0x58) = 0x7F0001C8

```bash
# Entry 5: my_program (executable, flags=0)
# Offset: 0x7F0001C8

# Write name "my_program" (null-terminated)
mw 0x7F0001C8 0x6D 1         # 'm'
mw 0x7F0001C9 0x79 1         # 'y'
mw 0x7F0001CA 0x5F 1         # '_'
mw 0x7F0001CB 0x70 1         # 'p'
mw 0x7F0001CC 0x72 1         # 'r'
mw 0x7F0001CD 0x6F 1         # 'o'
mw 0x7F0001CE 0x67 1         # 'g'
mw 0x7F0001CF 0x72 1         # 'r'
mw 0x7F0001D0 0x61 1         # 'a'
mw 0x7F0001D1 0x6D 1         # 'm'
mw 0x7F0001D2 0x00 1         # null terminator

# Write start_addr (at offset +0x40 = 0x7F000208)
mw.q 0x7F000208 0x81000000 1  # start_addr: 0x81000000

# Write size (at offset +0x48 = 0x7F000210)
mw.q 0x7F000210 ${myprogram_size} 1  # size from filesize

# Write flags (at offset +0x50 = 0x7F000218)
mw.l 0x7F000218 0x00000000 1  # flags: 0 (executable, not library)

# Write reserved (at offset +0x54 = 0x7F00021C)
mw.l 0x7F00021C 0x00000000 1  # reserved: 0
```

## Quick Offset Calculator

Entry index → Start offset:
- Entry 0: 0x7F000010
- Entry 1: 0x7F000068
- Entry 2: 0x7F0000C0
- Entry 3: 0x7F000118
- Entry 4: 0x7F000170
- Entry 5: 0x7F0001C8
- Entry 6: 0x7F000220
- Entry 7: 0x7F000278
- Entry N: 0x7F000010 + (N × 0x58)

Within each entry:
- name: +0x00 (64 bytes)
- start_addr: +0x40 (8 bytes)
- size: +0x48 (8 bytes)
- flags: +0x50 (4 bytes)
- reserved: +0x54 (4 bytes)

## Example: Adding Multiple Programs

```bash
# Load programs
ext4load mmc 1:1  0x81000000 init
setenv init_size ${filesize}

ext4load mmc 1:1  0x81100000 shell
setenv shell_size ${filesize}

ext4load mmc 1:1  0x81200000 ls
setenv ls_size ${filesize}

# Update count to 8 (5 libraries + 3 programs)
mw.l 0x7F000008 0x00000008

# Entry 5: init
mw 0x7F0001C8 0x69 1         # 'i'
mw 0x7F0001C9 0x6E 1         # 'n'
mw 0x7F0001CA 0x69 1         # 'i'
mw 0x7F0001CB 0x74 1         # 't'
mw 0x7F0001CC 0x00 1         # null
mw.q 0x7F000208 0x81000000 1
mw.q 0x7F000210 ${init_size} 1
mw.l 0x7F000218 0x00000000 1
mw.l 0x7F00021C 0x00000000 1

# Entry 6: shell
mw 0x7F000220 0x73 1         # 's'
mw 0x7F000221 0x68 1         # 'h'
mw 0x7F000222 0x65 1         # 'e'
mw 0x7F000223 0x6C 1         # 'l'
mw 0x7F000224 0x6C 1         # 'l'
mw 0x7F000225 0x00 1         # null
mw.q 0x7F000260 0x81100000 1
mw.q 0x7F000268 ${shell_size} 1
mw.l 0x7F000270 0x00000000 1
mw.l 0x7F000274 0x00000000 1

# Entry 7: ls
mw 0x7F000278 0x6C 1         # 'l'
mw 0x7F000279 0x73 1         # 's'
mw 0x7F00027A 0x00 1         # null
mw.q 0x7F0002B8 0x81200000 1
mw.q 0x7F0002C0 ${ls_size} 1
mw.l 0x7F0002C8 0x00000000 1
mw.l 0x7F0002CC 0x00000000 1
```

## Character Codes Reference

Common characters for names:
- 'a'-'z': 0x61-0x7A
- 'A'-'Z': 0x41-0x5A
- '0'-'9': 0x30-0x39
- '_': 0x5F
- '.': 0x2E
- '-': 0x2D
- null: 0x00

## How It Works

1. **Bootloader Phase** (this script):
   - Loads all ELF files to memory ≥ 2GB
   - Creates the ELF table at 0x7F000000
   - Boots the kernel

2. **Kernel Phase** (automatic):
   - Kernel calls `elf_loader_init(0x7F000000)`
   - ELF loader reads the table
   - For each entry:
     - Parses DT_NEEDED to find dependencies
     - Recursively loads dependencies first
     - Loads and relocates the ELF
   - User can execute programs with `elf_loader_execute("program_name")`

## Dependencies

If your program depends on libraries (e.g., libc.so), just add it to the table. The kernel's ELF loader will automatically:
1. Parse the program's DT_NEEDED entries
2. Find "libc.so" in the table
3. Load libc.so first
4. Then load your program
5. Apply all relocations

No manual dependency management needed!

## Tips

1. **Address Alignment**: Use 1MB boundaries (0x100000) for each ELF file
2. **Library First**: Always load libraries before executables in memory (though order in table doesn't matter - kernel handles it)
3. **Flags**: Set flags=1 for libraries (.so), flags=0 for executables
4. **Names**: Keep names simple and match the actual ELF filename
5. **Testing**: Start with one program, verify it works, then add more

## Troubleshooting

- **"Invalid ELF table magic"**: Check 0x7F000000 has 0x454C4654
- **"Not found in table"**: Verify the name in the table matches DT_NEEDED entry
- **"Invalid address below 2GB"**: Ensure all start_addr ≥ 0x80000000
- **"Too many programs"**: Maximum 32 entries (MAX_ELF_FILES)
