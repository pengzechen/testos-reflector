#ifndef _PROCESS_H
#define _PROCESS_H

#include "t_types.h"
#include "t_exception.h"

/* Process states */
typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_ZOMBIE,
    PROCESS_STATE_DEAD
} process_state_t;

/* Memory region for a process */
typedef struct mem_region {
    uint64_t start;
    uint64_t end;
    uint32_t prot;  /* Protection flags (PROT_READ, PROT_WRITE, PROT_EXEC) */
    struct mem_region *next;
} mem_region_t;

/* Process control block */
typedef struct process {
    int pid;                    /* Process ID */
    process_state_t state;      /* Process state */
    trap_frame_t context;       /* Saved context (registers) */
    uint64_t brk;               /* Program break (end of heap) */
    uint64_t brk_start;         /* Start of heap */
    mem_region_t *mem_regions;  /* Memory regions list */
    struct process *next;       /* Next process in list */
} process_t;

/* Process management functions */
process_t *process_create(void);
void process_destroy(process_t *proc);
int process_load_elf(process_t *proc, const void *elf_data);
void process_switch_to(process_t *proc);
process_t *process_current(void);
void process_set_current(process_t *proc);

/* Memory region management */
int process_add_region(process_t *proc, uint64_t start, uint64_t end, uint32_t prot);
mem_region_t *process_find_region(process_t *proc, uint64_t addr);
int process_remove_region(process_t *proc, uint64_t start, uint64_t end);

#endif /* _PROCESS_H */
