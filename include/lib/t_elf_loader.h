/**
 * ELF Loader Integration Header
 * 
 * High-level interface for loading and managing ELF files from bootloader
 */

#ifndef T_ELF_LOADER_H
#define T_ELF_LOADER_H

#include "t_types.h"
#include "lib/t_elf.h"

/* Maximum number of ELF files that can be loaded */
#define MAX_ELF_FILES 32

/**
 * Bootloader ELF Information Structure
 * 
 * This structure is provided by the bootloader to describe each ELF file
 * it has placed in memory. The structure should be located at a known
 * address that the bootloader and kernel agree upon.
 */
typedef struct {
    char     name[64];       // ELF file name
    uint64_t start_addr;     // Start address in memory (>= 0x80000000)
    uint64_t size;           // Size in bytes
    uint32_t flags;          // Flags (bit 0: 0=executable, 1=library)
    uint32_t reserved;       // Reserved for future use
} __attribute__((packed)) bootloader_elf_info_t;

/**
 * Bootloader ELF Table
 * 
 * Table of all ELF files loaded by the bootloader.
 * Located at a fixed address known to both bootloader and kernel.
 */
typedef struct {
    uint32_t magic;          // Magic number: 0x454C4654 ("ELF" + "T")
    uint32_t version;        // Table version
    uint32_t count;          // Number of ELF entries
    uint32_t reserved;       // Reserved for future use
    bootloader_elf_info_t entries[MAX_ELF_FILES];
} __attribute__((packed)) bootloader_elf_table_t;

/* Expected magic number for ELF table */
#define ELF_TABLE_MAGIC 0x454C4654

/* Expected table version */
#define ELF_TABLE_VERSION 1

/* Flag bits */
#define ELF_FLAG_IS_LIBRARY 0x00000001

/**
 * Initialize the ELF loader with bootloader data
 * 
 * Reads the ELF table from the bootloader and loads all ELF files
 * described in it. The table should be at a known memory address.
 * 
 * @param table_addr Address of the bootloader ELF table
 * @return Number of ELF files successfully loaded, or 0 on error
 */
size_t elf_loader_init(uint64_t table_addr);

/**
 * Get a loaded program by name
 * 
 * @param name Program name to search for
 * @return Pointer to ELF descriptor if found, NULL otherwise
 */
const elf_descriptor_t *elf_loader_get_program(const char *name);

/**
 * Get a loaded program by index
 * 
 * @param index Program index (0 to count-1)
 * @return Pointer to ELF descriptor if valid index, NULL otherwise
 */
const elf_descriptor_t *elf_loader_get_program_by_index(size_t index);

/**
 * Get the number of loaded programs
 * 
 * @return Number of successfully loaded programs
 */
size_t elf_loader_get_program_count(void);

/**
 * Execute a loaded program by name
 * 
 * Finds the program by name and jumps to its entry point.
 * Note: This is a simple implementation that doesn't handle
 * context switching or privilege levels.
 * 
 * @param name Program name to execute
 * @return true if program was found and executed, false otherwise
 */
bool elf_loader_execute(const char *name);

/**
 * List all loaded programs
 * 
 * Prints information about all successfully loaded programs
 * to the logger output.
 */
void elf_loader_list_programs(void);

/**
 * Example kernel integration function
 * 
 * Demonstrates how to integrate the ELF loader into kernel initialization.
 * This is for reference and testing purposes.
 */
void example_kernel_elf_init(void);

#endif /* T_ELF_LOADER_H */
