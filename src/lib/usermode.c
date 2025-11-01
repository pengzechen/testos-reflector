#include "lib/usermode.h"
#include "lib/elf.h"
#include "lib/process.h"
#include "lib/t_logger.h"
#include "lib/rkmem.h"
#include "lib/t_string.h"

#define USER_STACK_SIZE (1024 * 1024)  /* 1MB stack */

/**
 * Load and execute a user program from ELF data
 * @param elf_data: Pointer to ELF file in memory
 * @return: Does not return on success, returns -1 on failure
 */
int exec_user_program(const void *elf_data)
{
    uint64_t entry_point;
    void *user_stack;

    logger_info("Loading user program...\n");

    /* Validate and load ELF */
    if (elf_load(elf_data, &entry_point) != 0) {
        logger_error("Failed to load ELF\n");
        return -1;
    }

    logger_info("ELF loaded, entry point: 0x%lx\n", entry_point);

    /* Allocate user stack */
    user_stack = rkmem_alloc(USER_STACK_SIZE);
    if (user_stack == NULL) {
        logger_error("Failed to allocate user stack\n");
        return -1;
    }

    /* Stack grows downward, so point to the top */
    uint64_t user_stack_top = (uint64_t)user_stack + USER_STACK_SIZE;

    logger_info("User stack: %p - %p\n", user_stack, (void *)user_stack_top);

    /* Set up user mode environment */
    setup_user_mode();

    logger_info("Entering user mode at 0x%lx with stack at 0x%lx\n", 
                entry_point, user_stack_top);

    /* Enter user mode - this does not return unless the program exits */
    enter_user_mode(entry_point, user_stack_top);

    /* Should not reach here */
    logger_error("Returned from user mode unexpectedly\n");
    return -1;
}
