#include "lib/elf.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "npu/rkmem.h"

/**
 * Validate ELF header
 * Returns 0 on success, -1 on failure
 */
int elf_validate(const void *elf_data)
{
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;

    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        logger_error("Invalid ELF magic number\n");
        return -1;
    }

    /* Check class (64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        logger_error("Not a 64-bit ELF file\n");
        return -1;
    }

    /* Check data encoding (little endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        logger_error("Not little endian ELF\n");
        return -1;
    }

    /* Check machine type (AArch64) */
    if (ehdr->e_machine != EM_AARCH64) {
        logger_error("Not an AArch64 ELF file (machine: %d)\n", ehdr->e_machine);
        return -1;
    }

    /* Check file type (executable or shared object) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        logger_error("Not an executable or shared object (type: %d)\n", ehdr->e_type);
        return -1;
    }

    logger_info("ELF validation passed\n");
    return 0;
}

/**
 * Load ELF file into memory
 * Returns 0 on success, -1 on failure
 * Sets entry_point to the program entry point
 */
int elf_load(const void *elf_data, uint64_t *entry_point)
{
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;
    const Elf64_Phdr *phdr;
    int i;

    /* Validate ELF */
    if (elf_validate(elf_data) != 0) {
        return -1;
    }

    /* Set entry point */
    *entry_point = ehdr->e_entry;
    logger_info("ELF entry point: 0x%lx\n", *entry_point);

    /* Get program header table */
    phdr = (const Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);

    logger_info("Loading %d program segments:\n", ehdr->e_phnum);

    /* Load each LOAD segment */
    for (i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t vaddr = phdr[i].p_vaddr;
            uint64_t memsz = phdr[i].p_memsz;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t offset = phdr[i].p_offset;
            uint32_t flags = phdr[i].p_flags;

            logger_info("  Segment %d: vaddr=0x%lx memsz=0x%lx filesz=0x%lx flags=0x%x\n",
                       i, vaddr, memsz, filesz, flags);

            /* Allocate memory for segment */
            void *seg_mem = rkmem_alloc(memsz);
            if (seg_mem == NULL) {
                logger_error("Failed to allocate memory for segment %d\n", i);
                return -1;
            }

            /* Copy segment data from file */
            if (filesz > 0) {
                my_memcpy(seg_mem, (const uint8_t *)elf_data + offset, filesz);
            }

            /* Zero out BSS (memsz > filesz) */
            if (memsz > filesz) {
                my_memset((uint8_t *)seg_mem + filesz, 0, memsz - filesz);
            }

            logger_info("    Loaded at physical address: %p\n", seg_mem);

            /* Note: In a real implementation, we would set up page tables here
             * to map the virtual address to the physical address.
             * For simplicity, we're loading to physical addresses directly.
             */
        }
    }

    logger_info("ELF load completed successfully\n");
    return 0;
}
