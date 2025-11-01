#include "lib/process.h"
#include "lib/t_logger.h"
#include "npu/rkmem.h"
#include "lib/t_string.h"
#include "lib/syscall.h"

/* Global current process pointer */
static process_t *current_process = NULL;

/**
 * Get current process
 */
process_t *process_current(void)
{
    return current_process;
}

/**
 * Set current process
 */
void process_set_current(process_t *proc)
{
    current_process = proc;
}

/**
 * Create a new process
 */
process_t *process_create(void)
{
    process_t *proc = (process_t *)rkmem_alloc(sizeof(process_t));
    if (proc == NULL) {
        logger_error("Failed to allocate process structure\n");
        return NULL;
    }

    /* Initialize process structure */
    my_memset(proc, 0, sizeof(process_t));
    proc->pid = 1;  /* Simple PID for now */
    proc->state = PROCESS_STATE_READY;
    proc->brk_start = 0x10000000;  /* Heap starts at 256MB */
    proc->brk = proc->brk_start;
    proc->mem_regions = NULL;
    proc->next = NULL;

    logger_info("Created process with PID %d\n", proc->pid);
    return proc;
}

/**
 * Destroy a process
 */
void process_destroy(process_t *proc)
{
    if (proc == NULL) return;

    /* Free memory regions */
    mem_region_t *region = proc->mem_regions;
    while (region != NULL) {
        mem_region_t *next = region->next;
        /* Note: In a real implementation, we would free the actual memory here */
        region = next;
    }

    /* Free process structure */
    /* Note: rkmem_free not implemented, so we just mark as dead */
    proc->state = PROCESS_STATE_DEAD;
    
    logger_info("Destroyed process PID %d\n", proc->pid);
}

/**
 * Add a memory region to process
 */
int process_add_region(process_t *proc, uint64_t start, uint64_t end, uint32_t prot)
{
    if (proc == NULL) return -1;

    mem_region_t *region = (mem_region_t *)rkmem_alloc(sizeof(mem_region_t));
    if (region == NULL) {
        logger_error("Failed to allocate memory region\n");
        return -1;
    }

    region->start = start;
    region->end = end;
    region->prot = prot;
    region->next = proc->mem_regions;
    proc->mem_regions = region;

    logger_debug("Added region [0x%lx-0x%lx] prot=0x%x to process %d\n",
                start, end, prot, proc->pid);
    return 0;
}

/**
 * Find memory region containing address
 */
mem_region_t *process_find_region(process_t *proc, uint64_t addr)
{
    if (proc == NULL) return NULL;

    mem_region_t *region = proc->mem_regions;
    while (region != NULL) {
        if (addr >= region->start && addr < region->end) {
            return region;
        }
        region = region->next;
    }
    return NULL;
}

/**
 * Remove memory region
 */
int process_remove_region(process_t *proc, uint64_t start, uint64_t end)
{
    if (proc == NULL) return -1;

    mem_region_t **prev = &proc->mem_regions;
    mem_region_t *region = proc->mem_regions;

    while (region != NULL) {
        if (region->start == start && region->end == end) {
            *prev = region->next;
            /* Note: Would free region here in real implementation */
            logger_debug("Removed region [0x%lx-0x%lx] from process %d\n",
                        start, end, proc->pid);
            return 0;
        }
        prev = &region->next;
        region = region->next;
    }

    return -1;
}

/**
 * Load ELF file into process
 */
int process_load_elf(process_t *proc, const void *elf_data)
{
    /* This is a placeholder - actual ELF loading happens in elf.c */
    (void)proc;
    (void)elf_data;
    
    logger_info("ELF loading into process not yet fully implemented\n");
    return -1;
}

/**
 * Switch to process (enter user mode)
 */
void process_switch_to(process_t *proc)
{
    if (proc == NULL) {
        logger_error("Cannot switch to NULL process\n");
        return;
    }

    current_process = proc;
    proc->state = PROCESS_STATE_RUNNING;

    logger_info("Switching to process %d\n", proc->pid);
    
    /* The actual context switch would happen here */
    /* For now, this is a placeholder */
}
