#ifndef _USERMODE_H
#define _USERMODE_H

#include "t_types.h"

/**
 * Enter user mode (EL0) and execute program
 * @param entry_point: User program entry point
 * @param user_stack: User mode stack pointer
 */
void enter_user_mode(uint64_t entry_point, uint64_t user_stack);

/**
 * Set up user mode environment
 */
void setup_user_mode(void);

/**
 * Load and execute a user program from ELF data
 * @param elf_data: Pointer to ELF file in memory
 * @return: Does not return on success, returns -1 on failure
 */
int exec_user_program(const void *elf_data);

#endif /* _USERMODE_H */
