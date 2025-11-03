/**
 * ELF Loader Integration Example
 * 
 * This file demonstrates how to integrate the ELF loader with bootloader
 * data. The bootloader places user programs at addresses above 2GB and
 * provides a table describing each ELF file.
 */

#include "lib/t_elf_loader.h"
#include "lib/t_logger.h"
#include "t_types.h"

/**
 * Global array to store loaded ELF descriptors
 */
static elf_descriptor_t loaded_programs[MAX_ELF_FILES];
static size_t num_loaded_programs = 0;

/**
 * Initialize the ELF loader with bootloader data
 * 
 * @param table_addr Address of the bootloader ELF table
 * @return Number of ELF files loaded, or 0 on error
 */
size_t
elf_loader_init(uint64_t table_addr)
{
    if (table_addr == 0) {
        logger_error("Invalid ELF table address\n");
        return 0;
    }

    const bootloader_elf_table_t *table = (const bootloader_elf_table_t *) table_addr;

    // Validate table magic and version
    if (table->magic != ELF_TABLE_MAGIC) {
        logger_error("Invalid ELF table magic: 0x%08x (expected 0x%08x)\n",
                     table->magic, ELF_TABLE_MAGIC);
        return 0;
    }

    if (table->version != ELF_TABLE_VERSION) {
        logger_warn("ELF table version mismatch: %u (expected %u)\n",
                    table->version, ELF_TABLE_VERSION);
    }

    if (table->count > MAX_ELF_FILES) {
        logger_error("Too many ELF files in table: %u (max %u)\n",
                     table->count, MAX_ELF_FILES);
        return 0;
    }

    logger_info("Found %u ELF file(s) from bootloader\n", table->count);

    // Load each ELF file
    num_loaded_programs = 0;
    for (uint32_t i = 0; i < table->count; i++) {
        const bootloader_elf_info_t *info = &table->entries[i];
        elf_descriptor_t *desc = &loaded_programs[num_loaded_programs];

        // Copy name
        size_t name_len = 0;
        while (info->name[name_len] && name_len < 63) {
            desc->name[name_len] = info->name[name_len];
            name_len++;
        }
        desc->name[name_len] = '\0';

        // Copy other fields
        desc->start_addr = info->start_addr;
        desc->size = info->size;
        desc->is_loaded = false;

        logger_info("\n--- Loading ELF #%u ---\n", i);
        logger_info("  Name: %s\n", desc->name);
        logger_info("  Address: 0x%llx\n", desc->start_addr);
        logger_info("  Size: %llu bytes\n", desc->size);

        // Validate address range (should be >= 2GB)
        if (desc->start_addr < 0x80000000ULL) {
            logger_error("  Invalid address: below 2GB boundary\n");
            continue;
        }

        // Load based on type
        elf_result_t result;
        if (info->flags & ELF_FLAG_IS_LIBRARY) {
            logger_info("  Type: Dynamic Library\n");
            result = elf_load_dynamic(desc, 0);
        } else {
            logger_info("  Type: Executable\n");
            result = elf_load_executable(desc);
        }

        if (result == ELF_SUCCESS) {
            logger_info("  Status: Loaded successfully\n");
            logger_info("  Entry Point: 0x%llx\n", desc->entry_point);
            num_loaded_programs++;
        } else {
            logger_error("  Status: Failed - %s\n", elf_error_string(result));
        }
    }

    logger_info("\nLoaded %zu out of %u ELF file(s)\n", 
                num_loaded_programs, table->count);

    return num_loaded_programs;
}

/**
 * Get a loaded program by name
 * 
 * @param name Program name
 * @return Pointer to descriptor, or NULL if not found
 */
const elf_descriptor_t *
elf_loader_get_program(const char *name)
{
    if (!name) {
        return NULL;
    }

    for (size_t i = 0; i < num_loaded_programs; i++) {
        const elf_descriptor_t *desc = &loaded_programs[i];
        
        // Compare names
        const char *s1 = desc->name;
        const char *s2 = name;
        bool match = true;
        while (*s1 || *s2) {
            if (*s1 != *s2) {
                match = false;
                break;
            }
            s1++;
            s2++;
        }

        if (match) {
            return desc;
        }
    }

    return NULL;
}

/**
 * Get a loaded program by index
 * 
 * @param index Program index
 * @return Pointer to descriptor, or NULL if invalid index
 */
const elf_descriptor_t *
elf_loader_get_program_by_index(size_t index)
{
    if (index >= num_loaded_programs) {
        return NULL;
    }
    return &loaded_programs[index];
}

/**
 * Get the number of loaded programs
 * 
 * @return Number of loaded programs
 */
size_t
elf_loader_get_program_count(void)
{
    return num_loaded_programs;
}

/**
 * Execute a loaded program
 * 
 * @param name Program name
 * @return true if executed, false if not found or not executable
 */
bool
elf_loader_execute(const char *name)
{
    const elf_descriptor_t *desc = elf_loader_get_program(name);
    if (!desc || !desc->is_loaded) {
        logger_error("Program '%s' not found or not loaded\n", name);
        return false;
    }

    if (desc->type != ET_EXEC && desc->type != ET_DYN) {
        logger_error("Program '%s' is not executable\n", name);
        return false;
    }

    logger_info("Executing program '%s' at 0x%llx\n", name, desc->entry_point);

    // Call the entry point
    // Note: In a real system, you would want to switch context here
    void (*entry)(void) = (void (*)(void)) desc->entry_point;
    entry();

    return true;
}

/**
 * List all loaded programs
 */
void
elf_loader_list_programs(void)
{
    logger_info("\n=== Loaded Programs ===\n");
    logger_info("Total: %zu\n\n", num_loaded_programs);

    for (size_t i = 0; i < num_loaded_programs; i++) {
        const elf_descriptor_t *desc = &loaded_programs[i];
        logger_info("[%zu] %s\n", i, desc->name);
        logger_info("    Address: 0x%llx\n", desc->start_addr);
        logger_info("    Size: %llu bytes\n", desc->size);
        logger_info("    Entry: 0x%llx\n", desc->entry_point);
        logger_info("    Type: %s\n", 
                    desc->type == ET_EXEC ? "Executable" : "Library");
    }

    logger_info("======================\n");
}

/**
 * Example integration with kernel initialization
 */
void
example_kernel_elf_init(void)
{
    logger_info("\n=== ELF Loader Integration Example ===\n");

    // Assume bootloader places ELF table at fixed address
    // In real implementation, this would be passed via boot parameters
    const uint64_t elf_table_addr = 0x7F000000;  // Just below 2GB

    // Initialize ELF loader with bootloader data
    size_t loaded = elf_loader_init(elf_table_addr);
    if (loaded == 0) {
        logger_error("No programs loaded\n");
        return;
    }

    // List all loaded programs
    elf_loader_list_programs();

    // Example: Find and execute a specific program
    const char *init_program = "init";
    const elf_descriptor_t *init_desc = elf_loader_get_program(init_program);
    if (init_desc) {
        logger_info("\nFound init program:\n");
        elf_dump_descriptor(init_desc);
        
        // In a real system, you would execute it here
        // elf_loader_execute(init_program);
    } else {
        logger_warn("Init program not found\n");
    }

    // Example: Find a symbol in a library
    const elf_descriptor_t *lib = elf_loader_get_program("libc.so");
    if (lib) {
        uint64_t malloc_addr;
        elf_result_t result = elf_find_symbol(lib, "malloc", &malloc_addr);
        if (result == ELF_SUCCESS) {
            logger_info("Found malloc() at 0x%llx\n", malloc_addr);
        }
    }

    logger_info("\n=== ELF Loader Integration Complete ===\n");
}
