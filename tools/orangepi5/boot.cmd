# Load device tree
ext4load mmc 1:1  0x300000 rk3588-orangepi-5-plus.dtb

# Load kernel
ext4load mmc 1:1  0x400000 kernel.uimg

# ========== ELF Loader Configuration ==========
# Load standard musl libraries and user programs to memory >= 2GB
# The kernel's ELF loader will automatically resolve dependencies

# --- Standard musl libraries (≥ 0x80000000 / 2GB) ---
# Note: In musl, libm/libpthread/libdl/librt are all integrated into libc.so
# We only need to load libc.so once, but register multiple names in ELF table

# Load libc.so (musl C library) - base library for all programs
ext4load mmc 1:1  0x80000000 libc.so
setenv libc_size ${filesize}

# Set the same size for all library aliases (they all point to the same libc.so)
setenv libm_size ${filesize}
setenv libpthread_size ${filesize}
setenv libdl_size ${filesize}
setenv librt_size ${filesize}

# --- User programs area (≥ 0x81000000) ---
# Reserve 0x81000000 - 0x8FFFFFFF for user executables

# Load hello.elf (needs libc)
ext4load mmc 1:1  0x81000000 hello.elf
setenv hello_size ${filesize}

# Load simple.elf (standalone, no libc needed)
ext4load mmc 1:1  0x82000000 simple.elf
setenv simple_size ${filesize}

# --- ELF Table Setup (0x7F000000 - just below 2GB boundary) ---
# Create bootloader_elf_table_t structure:
# struct {
#   uint32_t magic;           // 0x454C4654 ("ELFT")
#   uint32_t version;         // 1
#   uint32_t count;           // number of ELF files
#   uint32_t reserved;        // 0
#   struct {                  // entries[32]
#     char name[64];          // ELF file name
#     uint64_t start_addr;    // load address
#     uint64_t size;          // file size
#     uint32_t flags;         // 1=library, 0=executable
#     uint32_t reserved;      // 0
#   } entries[];
# }

# Write ELF table header
mw.l 0x7F000000 0x454C4654   # magic: "ELFT"
mw.l 0x7F000004 0x00000001   # version: 1
mw.l 0x7F000008 0x00000007   # count: 7 (5 libraries + 2 executables)
mw.l 0x7F00000C 0x00000000   # reserved: 0

# Entry 0: libc.so (library, flags=1)
# Offset: 0x7F000010
mw 0x7F000010 0x6C 1         # 'l'
mw 0x7F000011 0x69 1         # 'i'
mw 0x7F000012 0x62 1         # 'b'
mw 0x7F000013 0x63 1         # 'c'
mw 0x7F000014 0x2E 1         # '.'
mw 0x7F000015 0x73 1         # 's'
mw 0x7F000016 0x6F 1         # 'o'
mw 0x7F000017 0x00 1         # null terminator
# Fill rest of name[64] with zeros (0x7F000018-0x7F00004F)
mw.q 0x7F000050 0x80000000 1     # start_addr: 0x80000000
mw.q 0x7F000058 ${libc_size} 1   # size
mw.l 0x7F000060 0x00000001 1     # flags: library
mw.l 0x7F000064 0x00000000 1     # reserved

# Entry 1: libm.so (library, flags=1)
# Offset: 0x7F000068
mw 0x7F000068 0x6C 1         # 'l'
mw 0x7F000069 0x69 1         # 'i'
mw 0x7F00006A 0x62 1         # 'b'
mw 0x7F00006B 0x6D 1         # 'm'
mw 0x7F00006C 0x2E 1         # '.'
mw 0x7F00006D 0x73 1         # 's'
mw 0x7F00006E 0x6F 1         # 'o'
mw 0x7F00006F 0x00 1         # null terminator
mw.q 0x7F0000A8 0x80000000 1     # start_addr: 0x80000000 (same as libc.so)
mw.q 0x7F0000B0 ${libm_size} 1   # size
mw.l 0x7F0000B8 0x00000001 1     # flags: library
mw.l 0x7F0000BC 0x00000000 1     # reserved

# Entry 2: libpthread.so (library, flags=1)
# Offset: 0x7F0000C0
mw 0x7F0000C0 0x6C 1         # 'l'
mw 0x7F0000C1 0x69 1         # 'i'
mw 0x7F0000C2 0x62 1         # 'b'
mw 0x7F0000C3 0x70 1         # 'p'
mw 0x7F0000C4 0x74 1         # 't'
mw 0x7F0000C5 0x68 1         # 'h'
mw 0x7F0000C6 0x72 1         # 'r'
mw 0x7F0000C7 0x65 1         # 'e'
mw 0x7F0000C8 0x61 1         # 'a'
mw 0x7F0000C9 0x64 1         # 'd'
mw 0x7F0000CA 0x2E 1         # '.'
mw 0x7F0000CB 0x73 1         # 's'
mw 0x7F0000CC 0x6F 1         # 'o'
mw 0x7F0000CD 0x00 1         # null terminator
mw.q 0x7F000100 0x80000000 1     # start_addr: 0x80000000 (same as libc.so)
mw.q 0x7F000108 ${libpthread_size} 1  # size
mw.l 0x7F000110 0x00000001 1     # flags: library
mw.l 0x7F000114 0x00000000 1     # reserved

# Entry 3: libdl.so (library, flags=1)
# Offset: 0x7F000118
mw 0x7F000118 0x6C 1         # 'l'
mw 0x7F000119 0x69 1         # 'i'
mw 0x7F00011A 0x62 1         # 'b'
mw 0x7F00011B 0x64 1         # 'd'
mw 0x7F00011C 0x6C 1         # 'l'
mw 0x7F00011D 0x2E 1         # '.'
mw 0x7F00011E 0x73 1         # 's'
mw 0x7F00011F 0x6F 1         # 'o'
mw 0x7F000120 0x00 1         # null terminator
mw.q 0x7F000158 0x80000000 1     # start_addr: 0x80000000 (same as libc.so)
mw.q 0x7F000160 ${libdl_size} 1  # size
mw.l 0x7F000168 0x00000001 1     # flags: library
mw.l 0x7F00016C 0x00000000 1     # reserved

# Entry 4: librt.so (library, flags=1)
# Offset: 0x7F000170
mw 0x7F000170 0x6C 1         # 'l'
mw 0x7F000171 0x69 1         # 'i'
mw 0x7F000172 0x62 1         # 'b'
mw 0x7F000173 0x72 1         # 'r'
mw 0x7F000174 0x74 1         # 't'
mw 0x7F000175 0x2E 1         # '.'
mw 0x7F000176 0x73 1         # 's'
mw 0x7F000177 0x6F 1         # 'o'
mw 0x7F000178 0x00 1         # null terminator
mw.q 0x7F0001B0 0x80000000 1     # start_addr: 0x80000000 (same as libc.so)
mw.q 0x7F0001B8 ${librt_size} 1  # size
mw.l 0x7F0001C0 0x00000001 1     # flags: library
mw.l 0x7F0001C4 0x00000000 1     # reserved

# Entry 5: hello.elf (executable, flags=0)
# Offset: 0x7F0001C8
mw 0x7F0001C8 0x68 1         # 'h'
mw 0x7F0001C9 0x65 1         # 'e'
mw 0x7F0001CA 0x6C 1         # 'l'
mw 0x7F0001CB 0x6C 1         # 'l'
mw 0x7F0001CC 0x6F 1         # 'o'
mw 0x7F0001CD 0x2E 1         # '.'
mw 0x7F0001CE 0x65 1         # 'e'
mw 0x7F0001CF 0x6C 1         # 'l'
mw 0x7F0001D0 0x66 1         # 'f'
mw 0x7F0001D1 0x00 1         # null terminator
mw.q 0x7F000208 0x81000000 1     # start_addr: 0x81000000
mw.q 0x7F000210 ${hello_size} 1  # size
mw.l 0x7F000218 0x00000000 1     # flags: executable (0)
mw.l 0x7F00021C 0x00000000 1     # reserved

# Entry 6: simple.elf (executable, flags=0, no libc)
# Offset: 0x7F000220
mw 0x7F000220 0x73 1         # 's'
mw 0x7F000221 0x69 1         # 'i'
mw 0x7F000222 0x6D 1         # 'm'
mw 0x7F000223 0x70 1         # 'p'
mw 0x7F000224 0x6C 1         # 'l'
mw 0x7F000225 0x65 1         # 'e'
mw 0x7F000226 0x2E 1         # '.'
mw 0x7F000227 0x65 1         # 'e'
mw 0x7F000228 0x6C 1         # 'l'
mw 0x7F000229 0x66 1         # 'f'
mw 0x7F00022A 0x00 1         # null terminator
mw.q 0x7F000260 0x82000000 1     # start_addr: 0x82000000
mw.q 0x7F000268 ${simple_size} 1 # size
mw.l 0x7F000270 0x00000000 1     # flags: executable (0)
mw.l 0x7F000274 0x00000000 1     # reserved

# Add more user programs here:
# Entry N: my_program (executable, flags=0)
# Remember to increment count in header (0x7F000008)

# Boot kernel with device tree
# The kernel ELF loader will:
# 1. Read the table at 0x7F000000
# 2. Parse each ELF's DT_NEEDED entries
# 3. Recursively load dependencies from the table
# 4. Execute user programs
bootm 0x400000 - 0x300000