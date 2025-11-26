/**
 * ELF Loading Support
 * 
 * This module provides ELF (Executable and Linkable Format) file loading
 * functionality for both executables and dynamic libraries.
 */

#ifndef T_ELF_H
#define T_ELF_H

#include "t_types.h"

/* ELF Magic Number */
#define ELF_MAGIC 0x464C457F  // "\x7FELF" in little-endian

/* ELF Class */
#define ELFCLASS32 1
#define ELFCLASS64 2

/* ELF Data Encoding */
#define ELFDATA2LSB 1  // Little-endian
#define ELFDATA2MSB 2  // Big-endian

/* ELF Version */
#define EV_CURRENT 1

/* ELF OS/ABI */
#define ELFOSABI_SYSV 0  // UNIX System V ABI

/* ELF Type */
#define ET_NONE 0  // No file type
#define ET_REL  1  // Relocatable file
#define ET_EXEC 2  // Executable file
#define ET_DYN  3  // Shared object file
#define ET_CORE 4  // Core file

/* ELF Machine */
#define EM_AARCH64 183  // ARM 64-bit architecture

/* Program Header Types */
#define PT_NULL    0  // Unused entry
#define PT_LOAD    1  // Loadable segment
#define PT_DYNAMIC 2  // Dynamic linking information
#define PT_INTERP  3  // Interpreter path
#define PT_NOTE    4  // Auxiliary information
#define PT_SHLIB   5  // Reserved
#define PT_PHDR    6  // Program header table
#define PT_TLS     7  // Thread-Local Storage

/* Program Header Flags */
#define PF_X 0x1  // Execute
#define PF_W 0x2  // Write
#define PF_R 0x4  // Read

/* Section Header Types */
#define SHT_NULL     0   // Unused section
#define SHT_PROGBITS 1   // Program data
#define SHT_SYMTAB   2   // Symbol table
#define SHT_STRTAB   3   // String table
#define SHT_RELA     4   // Relocation entries with addends
#define SHT_HASH     5   // Symbol hash table
#define SHT_DYNAMIC  6   // Dynamic linking information
#define SHT_NOTE     7   // Notes
#define SHT_NOBITS   8   // Program space with no data (bss)
#define SHT_REL      9   // Relocation entries, no addends
#define SHT_SHLIB    10  // Reserved
#define SHT_DYNSYM   11  // Dynamic linker symbol table
#define SHT_INIT_ARRAY 14 // Section type for .init_array

/* Special Section Indices */
#define SHN_UNDEF 0

/* Dynamic Table Tags */
#define DT_NULL     0   // End of dynamic section
#define DT_NEEDED   1   // Name of needed library
#define DT_PLTRELSZ 2   // Size of PLT relocation entries
#define DT_PLTGOT   3   // Address of PLT/GOT
#define DT_HASH     4   // Address of symbol hash table
#define DT_STRTAB   5   // Address of string table
#define DT_SYMTAB   6   // Address of symbol table
#define DT_RELA     7   // Address of Rela relocation table
#define DT_RELASZ   8   // Size of Rela relocation table
#define DT_RELAENT  9   // Size of one Rela entry
#define DT_STRSZ    10  // Size of string table
#define DT_SYMENT   11  // Size of one symbol table entry
#define DT_INIT     12  // Address of initialization function
#define DT_FINI     13  // Address of termination function
#define DT_SONAME   14  // Name of shared object
#define DT_RPATH    15  // Library search path (deprecated)
#define DT_SYMBOLIC                                                                                \
    16  // Alert linker to search this shared object before the executable for symbols
#define DT_REL      17  // Address of Rel relocation table
#define DT_RELSZ    18  // Size of Rel relocation table
#define DT_RELENT   19  // Size of one Rel entry
#define DT_PLTREL   20  // Type of PLT relocation entries
#define DT_DEBUG    21  // For debugging
#define DT_TEXTREL  22  // Relocation might modify .text
#define DT_JMPREL   23  // Address of PLT relocation entries
#define DT_BIND_NOW 24  // Process relocations at load time
#define	DT_INIT_ARRAY	25
#define	DT_INIT_ARRAYSZ	27


/* Relocation Types for AArch64 */
#define R_AARCH64_NONE         0
#define R_AARCH64_ABS64        257
#define R_AARCH64_ABS32        258
#define R_AARCH64_ABS16        259
#define R_AARCH64_PREL64       260
#define R_AARCH64_PREL32       261
#define R_AARCH64_PREL16       262

#define R_AARCH64_GLOB_DAT     1025
#define R_AARCH64_JUMP_SLOT    1026
#define R_AARCH64_RELATIVE     1027
#define R_AARCH64_TLS_DTPREL64 1028
#define R_AARCH64_TLS_DTPMOD64 1029
#define R_AARCH64_TLS_TPREL64  1030
#define R_AARCH64_TLSDESC      1031	/* TLS Descriptor.  */

/* Symbol Binding */
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

/* Symbol Type */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

/* ELF64 Header */
typedef struct
{
    uint8_t  e_ident[16];  // ELF identification
    uint16_t e_type;       // Object file type
    uint16_t e_machine;    // Machine type
    uint32_t e_version;    // Object file version
    uint64_t e_entry;      // Entry point address
    uint64_t e_phoff;      // Program header offset
    uint64_t e_shoff;      // Section header offset
    uint32_t e_flags;      // Processor-specific flags
    uint16_t e_ehsize;     // ELF header size
    uint16_t e_phentsize;  // Program header entry size
    uint16_t e_phnum;      // Number of program header entries
    uint16_t e_shentsize;  // Section header entry size
    uint16_t e_shnum;      // Number of section header entries
    uint16_t e_shstrndx;   // Section name string table index
} __attribute__((packed)) elf64_ehdr_t;

/* ELF64 Program Header */
typedef struct
{
    uint32_t p_type;    // Segment type
    uint32_t p_flags;   // Segment flags
    uint64_t p_offset;  // Segment file offset
    uint64_t p_vaddr;   // Segment virtual address
    uint64_t p_paddr;   // Segment physical address
    uint64_t p_filesz;  // Segment size in file
    uint64_t p_memsz;   // Segment size in memory
    uint64_t p_align;   // Segment alignment
} __attribute__((packed)) elf64_phdr_t;

/* ELF64 Section Header */
typedef struct
{
    uint32_t sh_name;       // Section name (string table index)
    uint32_t sh_type;       // Section type
    uint64_t sh_flags;      // Section flags
    uint64_t sh_addr;       // Section virtual address
    uint64_t sh_offset;     // Section file offset
    uint64_t sh_size;       // Section size in bytes
    uint32_t sh_link;       // Link to another section
    uint32_t sh_info;       // Additional section information
    uint64_t sh_addralign;  // Section alignment
    uint64_t sh_entsize;    // Entry size if section holds table
} __attribute__((packed)) elf64_shdr_t;

/* ELF64 Symbol Table Entry */
typedef struct
{
    uint32_t st_name;   // Symbol name (string table index)
    uint8_t  st_info;   // Symbol type and binding
    uint8_t  st_other;  // Symbol visibility
    uint16_t st_shndx;  // Section index
    uint64_t st_value;  // Symbol value
    uint64_t st_size;   // Symbol size
} __attribute__((packed)) elf64_sym_t;

/* ELF64 Relocation Entry */
typedef struct
{
    uint64_t r_offset;  // Address
    uint64_t r_info;    // Relocation type and symbol index
} __attribute__((packed)) elf64_rel_t;

/* ELF64 Relocation Entry with Addend */
typedef struct
{
    uint64_t r_offset;  // Address
    uint64_t r_info;    // Relocation type and symbol index
    int64_t  r_addend;  // Addend
} __attribute__((packed)) elf64_rela_t;

/* ELF64 Dynamic Entry */
typedef struct
{
    int64_t d_tag;  // Dynamic entry type
    union {
        uint64_t d_val;  // Integer value
        uint64_t d_ptr;  // Address value
    } d_un;
} __attribute__((packed)) elf64_dyn_t;

/* ELF Descriptor - describes an ELF file loaded in memory */
typedef struct
{
    char     name[64];     // ELF file name
    uint64_t start_addr;   // Start address in memory
    uint64_t size;         // Size in bytes
    uint64_t entry_point;  // Entry point address
    uint16_t type;         // ELF type (ET_EXEC, ET_DYN, etc.)
    bool     is_loaded;    // Whether the ELF is loaded
} elf_descriptor_t;

/* ELF Load Result */
typedef enum
{
    ELF_SUCCESS = 0,
    ELF_ERROR_INVALID_MAGIC,
    ELF_ERROR_INVALID_CLASS,
    ELF_ERROR_INVALID_ENDIAN,
    ELF_ERROR_INVALID_VERSION,
    ELF_ERROR_INVALID_TYPE,
    ELF_ERROR_INVALID_MACHINE,
    ELF_ERROR_NO_MEMORY,
    ELF_ERROR_INVALID_SEGMENT,
    ELF_ERROR_RELOCATION_FAILED,
    ELF_ERROR_SYMBOL_NOT_FOUND,
    ELF_ERROR_INVALID_DESCRIPTOR
} elf_result_t;

/* Macros for symbol info */
#define ELF64_ST_BIND(i)    ((i) >> 4)
#define ELF64_ST_TYPE(i)    ((i) & 0xf)
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

/* Macros for relocation info */
#define ELF64_R_SYM(i)     ((i) >> 32)
#define ELF64_R_TYPE(i)    ((i) & 0xffffffffL)
#define ELF64_R_INFO(s, t) (((s) << 32) + ((t) & 0xffffffffL))

/**
 * Validate an ELF header
 * @param ehdr Pointer to ELF header
 * @return ELF_SUCCESS if valid, error code otherwise
 */
elf_result_t
elf_validate_header(const elf64_ehdr_t *ehdr);

/**
 * Load an executable ELF file
 * @param desc ELF descriptor with name, start_addr, and size filled in
 * @return ELF_SUCCESS if successful, error code otherwise
 */
elf_result_t
elf_load_executable(elf_descriptor_t *desc);

/**
 * Load a dynamic library ELF file
 * @param desc ELF descriptor with name, start_addr, and size filled in
 * @param base_addr Base address to load the library (0 for auto-allocation)
 * @return ELF_SUCCESS if successful, error code otherwise
 */
elf_result_t
elf_load_dynamic(elf_descriptor_t *desc, uint64_t base_addr);

/**
 * Find a symbol in a loaded ELF
 * @param desc ELF descriptor
 * @param symbol_name Symbol name to find
 * @param symbol_addr Output: address of the symbol
 * @return ELF_SUCCESS if found, error code otherwise
 */
elf_result_t
elf_find_symbol(const elf_descriptor_t *desc, const char *symbol_name, uint64_t *symbol_addr);

/**
 * Get list of library dependencies from an ELF file (DT_NEEDED entries)
 * @param base_addr Base address where ELF is loaded in memory
 * @param deps Array to store dependency names (output)
 * @param max_deps Maximum number of dependencies to return
 * @return Number of dependencies found, or 0 if none
 */
size_t
elf_get_dependencies(uint64_t base_addr, char deps[][64], size_t max_deps);

/**
 * Get human-readable error message for an error code
 * @param result Error code
 * @return Error message string
 */
const char *
elf_error_string(elf_result_t result);

/**
 * Dump ELF descriptor information (for debugging)
 * @param desc ELF descriptor
 */
void
elf_dump_descriptor(const elf_descriptor_t *desc);

/**
 * Dump ELF header information (for debugging)
 * @param ehdr ELF header
 */
void
elf_dump_header(const elf64_ehdr_t *ehdr);

#endif /* T_ELF_H */
