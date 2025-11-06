# Load device tree
ext4load mmc 1:1  0x300000 rk3588-orangepi-5-plus.dtb

# Load kernel
ext4load mmc 1:1  0x400000 kernel.uimg

# Load ELF table
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
ext4load mmc 1:1  0x7F000000 elf_table.bin

# Load ELF files
ext4load mmc 1:1  0x80000000 libc.so
ext4load mmc 1:1  0x81000000 libgcc_s.so.1
# libstdc++.so.6.0.29 larger than 16Mb
ext4load mmc 1:1  0x82000000 libstdc++.so.6.0.29
ext4load mmc 1:1  0x84000000 libitm.so.1.0.0

ext4load mmc 1:1  0x85000000 hello.elf
ext4load mmc 1:1  0x86000000 simple.elf



bootm 0x400000 - 0x300000