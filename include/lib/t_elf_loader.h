/**
 * ELF Loader Integration Header
 * 
 * High-level interface for loading and managing ELF files from bootloader
 */

#ifndef T_ELF_LOADER_H
#define T_ELF_LOADER_H

#include "t_types.h"
#include "lib/t_elf.h"

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
