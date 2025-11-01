#ifndef _ELF_H
#define _ELF_H

#include "t_types.h"

/* ELF 64-bit definitions */
#define EI_NIDENT 16

/* ELF header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];  /* ELF identification */
    uint16_t e_type;               /* Object file type */
    uint16_t e_machine;            /* Machine type */
    uint32_t e_version;            /* Object file version */
    uint64_t e_entry;              /* Entry point address */
    uint64_t e_phoff;              /* Program header offset */
    uint64_t e_shoff;              /* Section header offset */
    uint32_t e_flags;              /* Processor-specific flags */
    uint16_t e_ehsize;             /* ELF header size */
    uint16_t e_phentsize;          /* Size of program header entry */
    uint16_t e_phnum;              /* Number of program header entries */
    uint16_t e_shentsize;          /* Size of section header entry */
    uint16_t e_shnum;              /* Number of section header entries */
    uint16_t e_shstrndx;           /* Section name string table index */
} Elf64_Ehdr;

/* Program header */
typedef struct {
    uint32_t p_type;    /* Segment type */
    uint32_t p_flags;   /* Segment flags */
    uint64_t p_offset;  /* Segment file offset */
    uint64_t p_vaddr;   /* Segment virtual address */
    uint64_t p_paddr;   /* Segment physical address */
    uint64_t p_filesz;  /* Segment size in file */
    uint64_t p_memsz;   /* Segment size in memory */
    uint64_t p_align;   /* Segment alignment */
} Elf64_Phdr;

/* e_ident[] identification indexes */
#define EI_MAG0    0  /* File identification */
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4  /* File class */
#define EI_DATA    5  /* Data encoding */
#define EI_VERSION 6  /* File version */

/* ELF magic number */
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/* ELF class */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* Data encoding */
#define ELFDATANONE 0
#define ELFDATA2LSB 1  /* Little endian */
#define ELFDATA2MSB 2  /* Big endian */

/* Object file types */
#define ET_NONE 0  /* No file type */
#define ET_REL  1  /* Relocatable file */
#define ET_EXEC 2  /* Executable file */
#define ET_DYN  3  /* Shared object file */
#define ET_CORE 4  /* Core file */

/* Machine types */
#define EM_NONE    0   /* No machine */
#define EM_AARCH64 183 /* ARM 64-bit */

/* Program header types */
#define PT_NULL    0  /* Unused entry */
#define PT_LOAD    1  /* Loadable segment */
#define PT_DYNAMIC 2  /* Dynamic linking info */
#define PT_INTERP  3  /* Interpreter pathname */
#define PT_NOTE    4  /* Auxiliary info */
#define PT_SHLIB   5  /* Reserved */
#define PT_PHDR    6  /* Program header table */
#define PT_TLS     7  /* Thread-local storage */

/* Program header flags */
#define PF_X 0x1  /* Execute */
#define PF_W 0x2  /* Write */
#define PF_R 0x4  /* Read */

/* Auxiliary vector entry */
typedef struct {
    uint64_t a_type;
    union {
        uint64_t a_val;
        void    *a_ptr;
    } a_un;
} Elf64_auxv_t;

/* Auxiliary vector types */
#define AT_NULL   0   /* End of vector */
#define AT_IGNORE 1   /* Entry should be ignored */
#define AT_EXECFD 2   /* File descriptor of program */
#define AT_PHDR   3   /* Program headers for program */
#define AT_PHENT  4   /* Size of program header entry */
#define AT_PHNUM  5   /* Number of program headers */
#define AT_PAGESZ 6   /* System page size */
#define AT_BASE   7   /* Base address of interpreter */
#define AT_FLAGS  8   /* Flags */
#define AT_ENTRY  9   /* Entry point of program */
#define AT_UID    11  /* Real uid */
#define AT_EUID   12  /* Effective uid */
#define AT_GID    13  /* Real gid */
#define AT_EGID   14  /* Effective gid */
#define AT_RANDOM 25  /* Address of 16 random bytes */

/* ELF loader functions */
int elf_load(const void *elf_data, uint64_t *entry_point);
int elf_validate(const void *elf_data);

#endif /* _ELF_H */
