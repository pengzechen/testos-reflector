/**
 * Minimal libc support header
 */

#ifndef LIBC_SUPPORT_H
#define LIBC_SUPPORT_H

#include "t_types.h"

/**
 * Initialize TLS (Thread Local Storage) for libc
 */
void __testos_init_tls(void);

/**
 * Execute a libc-dependent program
 * 
 * @param entry_point Program entry point (_start)
 * @param main_func Pointer to main function
 * @return Return value from main()
 */
int execute_libc_program(uint64_t entry_point, int (*main_func)(int, char **, char **));

/**
 * Get errno location (for libc)
 */
int *__errno_location(void);

#endif /* LIBC_SUPPORT_H */
