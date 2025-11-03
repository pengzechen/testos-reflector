/**
 * Bootloader ELF Table Example
 * 
 * This file provides an example of how the bootloader should structure
 * the ELF information table for the kernel to load user programs.
 * 
 * The bootloader should:
 * 1. Load ELF files to memory at addresses >= 0x80000000
 * 2. Create this table at a known fixed address (e.g., 0x7F000000)
 * 3. Pass the table address to the kernel via boot parameters
 */

#include "lib/t_elf_loader.h"
#include "lib/t_logger.h"

/**
 * Example: Bootloader creates ELF table
 * 
 * This shows how a bootloader would populate the ELF table
 * after loading programs into memory.
 */
void bootloader_create_elf_table_example(void)
{
    // Fixed address for ELF table (just below 2GB boundary)
    const uint64_t table_addr = 0x7F000000;
    bootloader_elf_table_t *table = (bootloader_elf_table_t *) table_addr;

    // Initialize table header
    table->magic = 0x454C4654;  // "ELFT"
    table->version = 1;
    table->count = 0;
    table->reserved = 0;

    // Example 1: Add init executable
    // Bootloader loaded this at 0x80000000, size 64KB
    bootloader_elf_info_t *entry = &table->entries[table->count++];
    
    const char *init_name = "init";
    size_t name_len = 0;
    while (init_name[name_len]) name_len++;
    for (size_t i = 0; i < name_len && i < 63; i++) {
        entry->name[i] = init_name[i];
    }
    entry->name[name_len] = '\0';
    
    entry->start_addr = 0x80000000;
    entry->size = 65536;  // 64KB
    entry->flags = 0;     // Executable (not a library)
    entry->reserved = 0;

    // Example 2: Add shell executable
    // Bootloader loaded this at 0x80020000, size 128KB
    entry = &table->entries[table->count++];
    
    const char *shell_name = "shell";
    name_len = 0;
    while (shell_name[name_len]) name_len++;
    for (size_t i = 0; i < name_len && i < 63; i++) {
        entry->name[i] = shell_name[i];
    }
    entry->name[name_len] = '\0';
    
    entry->start_addr = 0x80020000;
    entry->size = 131072;  // 128KB
    entry->flags = 0;      // Executable
    entry->reserved = 0;

    // Example 3: Add libc.so shared library
    // Bootloader loaded this at 0x80100000, size 256KB
    entry = &table->entries[table->count++];
    
    const char *libc_name = "libc.so";
    name_len = 0;
    while (libc_name[name_len]) name_len++;
    for (size_t i = 0; i < name_len && i < 63; i++) {
        entry->name[i] = libc_name[i];
    }
    entry->name[name_len] = '\0';
    
    entry->start_addr = 0x80100000;
    entry->size = 262144;  // 256KB
    entry->flags = 0x00000001;  // Library (bit 0 set)
    entry->reserved = 0;

    // Example 4: Add libm.so math library
    // Bootloader loaded this at 0x80140000, size 64KB
    entry = &table->entries[table->count++];
    
    const char *libm_name = "libm.so";
    name_len = 0;
    while (libm_name[name_len]) name_len++;
    for (size_t i = 0; i < name_len && i < 63; i++) {
        entry->name[i] = libm_name[i];
    }
    entry->name[name_len] = '\0';
    
    entry->start_addr = 0x80140000;
    entry->size = 65536;   // 64KB
    entry->flags = 0x00000001;  // Library
    entry->reserved = 0;

    logger_info("Bootloader: Created ELF table with %u entries\n", table->count);
    logger_info("Bootloader: Table located at 0x%llx\n", table_addr);
}

/**
 * Example: Kernel uses bootloader's ELF table
 * 
 * This shows how the kernel would initialize from the bootloader's table.
 */
void kernel_load_from_bootloader_example(void)
{
    logger_info("\n=== Kernel ELF Loading Example ===\n");

    // The bootloader tells us where the ELF table is
    // (typically via boot parameters or fixed address)
    const uint64_t table_addr = 0x7F000000;

    // Initialize the ELF loader
    size_t loaded = elf_loader_init(table_addr);
    
    if (loaded == 0) {
        logger_error("Failed to load any programs\n");
        return;
    }

    logger_info("Successfully loaded %zu programs\n", loaded);

    // List all loaded programs
    elf_loader_list_programs();

    // Execute the init program
    logger_info("\nStarting init process...\n");
    if (elf_loader_execute("init")) {
        logger_info("Init process completed\n");
    } else {
        logger_error("Failed to start init process\n");
    }
}

/**
 * Memory Map Example
 * 
 * This shows the expected memory layout after bootloader loads programs.
 */
void print_memory_map_example(void)
{
    logger_info("\n=== Memory Map Example ===\n");
    logger_info("\n");
    logger_info("0x00000000 - 0x003FFFFF: Kernel code and data\n");
    logger_info("0x00400000 - 0x0FFFFFFF: Kernel heap and stacks\n");
    logger_info("0x10000000 - 0x7EFFFFFF: Device memory / Reserved\n");
    logger_info("0x7F000000 - 0x7F000FFF: ELF table (from bootloader)\n");
    logger_info("0x80000000 - 0x8000FFFF: init executable (64KB)\n");
    logger_info("0x80010000 - 0x8001FFFF: Reserved\n");
    logger_info("0x80020000 - 0x8003FFFF: shell executable (128KB)\n");
    logger_info("0x80040000 - 0x800FFFFF: Reserved\n");
    logger_info("0x80100000 - 0x8013FFFF: libc.so library (256KB)\n");
    logger_info("0x80140000 - 0x8014FFFF: libm.so library (64KB)\n");
    logger_info("0x80150000 - 0xFFFFFFFF: Available for more programs\n");
    logger_info("\n");
    logger_info("Note: Kernel space is 0x00000000 - 0x7FFFFFFF\n");
    logger_info("      User space is 0x80000000 - 0xFFFFFFFF\n");
    logger_info("===========================\n");
}

/**
 * Bootloader Pseudo-code Example
 * 
 * This shows the logic a bootloader should implement.
 */
#if 0
// Pseudo-code for bootloader

void bootloader_main(void)
{
    // 1. Initialize hardware
    uart_init();
    mmu_init();
    
    // 2. Load kernel
    load_file_to_memory("kernel.bin", 0x400000);
    
    // 3. Create ELF table at fixed address
    bootloader_elf_table_t *table = (void *)0x7F000000;
    table->magic = 0x454C4654;
    table->version = 1;
    table->count = 0;
    
    // 4. Load user programs to memory >= 2GB
    uint64_t load_addr = 0x80000000;
    
    // Load init
    size_t init_size = load_file_to_memory("init.elf", load_addr);
    add_to_table(table, "init", load_addr, init_size, 0);
    load_addr += align_up(init_size, 0x10000);  // 64KB alignment
    
    // Load shell
    size_t shell_size = load_file_to_memory("shell.elf", load_addr);
    add_to_table(table, "shell", load_addr, shell_size, 0);
    load_addr += align_up(shell_size, 0x10000);
    
    // Load libraries
    load_addr = 0x80100000;  // Libraries at different region
    size_t libc_size = load_file_to_memory("libc.so", load_addr);
    add_to_table(table, "libc.so", load_addr, libc_size, 1);  // flag=1 for library
    load_addr += align_up(libc_size, 0x10000);
    
    // 5. Jump to kernel with table address
    void (*kernel_entry)(uint64_t table_addr) = (void *)0x400000;
    kernel_entry(0x7F000000);
}

void add_to_table(bootloader_elf_table_t *table,
                  const char *name,
                  uint64_t addr,
                  size_t size,
                  uint32_t flags)
{
    if (table->count >= MAX_ELF_FILES) return;
    
    bootloader_elf_info_t *entry = &table->entries[table->count++];
    strncpy(entry->name, name, 63);
    entry->name[63] = '\0';
    entry->start_addr = addr;
    entry->size = size;
    entry->flags = flags;
    entry->reserved = 0;
}
#endif

/**
 * Test the example implementation
 */
void test_bootloader_example(void)
{
    logger_info("\n");
    logger_info("=========================================\n");
    logger_info("   Bootloader ELF Table Example\n");
    logger_info("=========================================\n");

    // Show memory map
    print_memory_map_example();

    // Note: In a real scenario, the bootloader would have already
    // created the table and loaded the programs. We're just showing
    // the structure here.
    
    logger_info("\nBootloader would create table like this:\n");
    logger_info("  Magic: 0x454C4654 (\"ELFT\")\n");
    logger_info("  Version: 1\n");
    logger_info("  Count: 4\n");
    logger_info("  Entries:\n");
    logger_info("    [0] init (0x80000000, 65536 bytes, executable)\n");
    logger_info("    [1] shell (0x80020000, 131072 bytes, executable)\n");
    logger_info("    [2] libc.so (0x80100000, 262144 bytes, library)\n");
    logger_info("    [3] libm.so (0x80140000, 65536 bytes, library)\n");
    
    logger_info("\nKernel would then call:\n");
    logger_info("  elf_loader_init(0x7F000000);\n");
    logger_info("  elf_loader_execute(\"init\");\n");

    logger_info("\n=========================================\n");
    logger_info("   Example Complete\n");
    logger_info("=========================================\n");
    logger_info("\n");
}
