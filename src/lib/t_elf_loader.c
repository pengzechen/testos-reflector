/**
 * ELF Loader Integration Example
 * 
 * This file demonstrates how to integrate the ELF loader with bootloader
 * data. The bootloader places user programs at addresses above 2GB and
 * provides a table describing each ELF file.
 */

#include "lib/t_elf_loader.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "t_types.h"

/**
 * Global array to store loaded ELF descriptors
 */
static elf_descriptor_t loaded_programs[MAX_ELF_FILES];
static size_t num_loaded_programs = 0;

/**
 * Global pointer to bootloader table for dependency resolution
 */
static const bootloader_elf_table_t *g_elf_table = NULL;

/**
 * Helper function to find an ELF in the bootloader table by name
 */
static const bootloader_elf_info_t *
find_elf_in_table(const char *name)
{
    if (!g_elf_table || !name) {
        return NULL;
    }

    for (uint32_t i = 0; i < g_elf_table->count; i++) {
        const bootloader_elf_info_t *info = &g_elf_table->entries[i];
        
        // Compare names
        const char *s1 = info->name;
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
            return info;
        }
    }
    
    return NULL;
}

/**
 * Helper function to check if an ELF is already loaded
 */
static bool
is_elf_loaded(const char *name)
{
    if (!name) {
        return false;
    }

    for (size_t i = 0; i < num_loaded_programs; i++) {
        const elf_descriptor_t *desc = &loaded_programs[i];
        if (!desc->is_loaded) {
            continue;
        }
        
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
            return true;
        }
    }
    
    return false;
}

/**
 * Load an ELF and its dependencies recursively
 * 
 * @param name ELF name to load
 * @return true if loaded successfully, false otherwise
 */
static bool
load_elf_with_dependencies(const char *name)
{
    if (!name || !g_elf_table) {
        return false;
    }

    // Check if already loaded
    if (is_elf_loaded(name)) {
        logger_info("  %s: Already loaded\n", name);
        return true;
    }

    // Find ELF in table
    const bootloader_elf_info_t *info = find_elf_in_table(name);
    if (!info) {
        logger_error("  %s: Not found in table\n", name);
        return false;
    }

    // Check dependencies first
    char deps[8][64];  // Support up to 8 dependencies
    size_t dep_count = elf_get_dependencies(info->start_addr, deps, 8);
    
    if (dep_count > 0) {
        logger_info("  %s: Found %zu dependencies\n", name, dep_count);
        for (size_t i = 0; i < dep_count; i++) {
            logger_info("    - %s\n", deps[i]);
            if (!load_elf_with_dependencies(deps[i])) {
                logger_error("  %s: Failed to load dependency %s\n", name, deps[i]);
                return false;
            }
        }
    }

    // Now load this ELF
    if (num_loaded_programs >= MAX_ELF_FILES) {
        logger_error("  %s: Too many programs loaded\n", name);
        return false;
    }

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

    logger_info("  %s: Loading at 0x%llx (%llu bytes), flags=0x%x\n", 
                desc->name, desc->start_addr, desc->size, info->flags);

    // Validate address range (should be >= 2GB)
    if (desc->start_addr < 0x80000000ULL) {
        logger_error("  %s: Invalid address below 2GB\n", name);
        return false;
    }

    // Load based on type
    elf_result_t result;
    if (info->flags & ELF_FLAG_IS_LIBRARY) {
        result = elf_load_dynamic(desc, 0);
    } else {
        result = elf_load_executable(desc);
    }

    if (result == ELF_SUCCESS) {
        logger_info("  %s: Loaded successfully (entry: 0x%llx)\n", 
                    desc->name, desc->entry_point);
        num_loaded_programs++;
        return true;
    } else {
        logger_error("  %s: Failed - %s\n", desc->name, elf_error_string(result));
        return false;
    }
}

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

    // Store table reference for dependency resolution
    g_elf_table = table;
    num_loaded_programs = 0;

    // Load each ELF file with dependency resolution
    for (uint32_t i = 0; i < table->count; i++) {
        const bootloader_elf_info_t *info = &table->entries[i];
        
        logger_info("--- Processing ELF #%u: %s ---\n", i, info->name);
        
        // Load with dependencies
        load_elf_with_dependencies(info->name);
    }

    logger_info("=== Total: Loaded %zu ELF file(s) ===\n", num_loaded_programs);

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
        
        // 根据 flags 判断类型（更准确）
        // 从 bootloader table 中查找对应的 flags
        const bootloader_elf_info_t *info = find_elf_in_table(desc->name);
        bool is_library = info && (info->flags & ELF_FLAG_IS_LIBRARY);
        
        logger_info("    Type: %s\n", is_library ? "Library" : "Executable");
    }

    logger_info("======================\n");
}

/**
 * Resolve a symbol from loaded libraries
 */
uint64_t
elf_loader_resolve_symbol(const char *symbol_name)
{
    if (!symbol_name) {
        return 0;
    }

    // Search all loaded libraries for the symbol
    for (size_t i = 0; i < num_loaded_programs; i++) {
        const elf_descriptor_t *desc = &loaded_programs[i];
        
        // Only search in libraries (not executables)
        if (!desc->is_loaded || desc->type != ET_DYN) {
            continue;
        }

        // Get ELF header and find symbol table
        const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *) desc->start_addr;
        const uint8_t *elf_base = (const uint8_t *) desc->start_addr;
        const elf64_phdr_t *phdr = (const elf64_phdr_t *) (elf_base + ehdr->e_phoff);

        // Find dynamic segment
        const elf64_phdr_t *dyn_phdr = NULL;
        for (uint16_t j = 0; j < ehdr->e_phnum; j++) {
            if (phdr[j].p_type == PT_DYNAMIC) {
                dyn_phdr = &phdr[j];
                break;
            }
        }

        if (!dyn_phdr) {
            continue;
        }

        // Parse dynamic section to find symbol table
        const elf64_dyn_t *dyn = (const elf64_dyn_t *) (elf_base + dyn_phdr->p_offset);
        const elf64_sym_t *symtab = NULL;
        const char *strtab = NULL;
        uint64_t syment_size = sizeof(elf64_sym_t);

        for (size_t j = 0; dyn[j].d_tag != DT_NULL; j++) {
            switch (dyn[j].d_tag) {
                case DT_SYMTAB:
                    symtab = (const elf64_sym_t *) (elf_base + dyn[j].d_un.d_ptr);
                    break;
                case DT_STRTAB:
                    strtab = (const char *) (elf_base + dyn[j].d_un.d_ptr);
                    break;
                case DT_SYMENT:
                    syment_size = dyn[j].d_un.d_val;
                    break;
            }
        }

        if (!symtab || !strtab) {
            continue;
        }

        // Search symbol table
        // Note: We don't have DT_SYMCOUNT, so we search until we find the symbol
        // or reach an invalid entry
        for (size_t j = 0; j < 10000; j++) {  // Reasonable upper limit
            const elf64_sym_t *sym = (const elf64_sym_t *)((uint8_t *)symtab + j * syment_size);
            
            // Skip undefined symbols
            if (sym->st_shndx == 0 || sym->st_name == 0) {
                continue;
            }

            // Check if name matches
            const char *name = strtab + sym->st_name;
            if (strcmp(name, symbol_name) == 0) {
                // Found the symbol!
                uint64_t symbol_addr = desc->start_addr + sym->st_value;
                logger_info("Resolved symbol '%s' to 0x%llx (in %s)\n",
                           symbol_name, symbol_addr, desc->name);
                return symbol_addr;
            }

            // Stop if we've gone too far (heuristic: symbol value is too large)
            if (sym->st_value > desc->size) {
                break;
            }
        }
    }

    logger_warn("Symbol '%s' not found in any loaded library\n", symbol_name);
    return 0;
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
