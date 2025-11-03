/**
 * ELF Loading Implementation
 * 
 * This file implements ELF file loading functionality for executables and
 * dynamic libraries on ARM64 (AArch64) architecture.
 */

#include "lib/t_elf.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "mem/t_mem.h"

/**
 * Validate an ELF header
 */
elf_result_t
elf_validate_header(const elf64_ehdr_t *ehdr)
{
    if (!ehdr) {
        return ELF_ERROR_INVALID_DESCRIPTOR;
    }

    // Check magic number
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || 
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return ELF_ERROR_INVALID_MAGIC;
    }

    // Check class (64-bit)
    if (ehdr->e_ident[4] != ELFCLASS64) {
        return ELF_ERROR_INVALID_CLASS;
    }

    // Check endianness (little-endian)
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        return ELF_ERROR_INVALID_ENDIAN;
    }

    // Check version
    if (ehdr->e_ident[6] != EV_CURRENT) {
        return ELF_ERROR_INVALID_VERSION;
    }

    // Check machine type (AArch64)
    if (ehdr->e_machine != EM_AARCH64) {
        return ELF_ERROR_INVALID_MACHINE;
    }

    return ELF_SUCCESS;
}

/**
 * Apply relocations for a segment
 */
static elf_result_t
elf_apply_relocations(uint64_t base_addr, 
                      const elf64_rela_t *rela, 
                      size_t rela_count,
                      const elf64_sym_t *symtab,
                      const char *strtab)
{
    for (size_t i = 0; i < rela_count; i++) {
        const elf64_rela_t *r = &rela[i];
        uint64_t            type = ELF64_R_TYPE(r->r_info);
        uint64_t            sym_idx = ELF64_R_SYM(r->r_info);
        uint64_t           *reloc_addr = (uint64_t *) (base_addr + r->r_offset);

        switch (type) {
            case R_AARCH64_NONE:
                // No relocation needed
                break;

            case R_AARCH64_RELATIVE:
                // Base address + addend
                *reloc_addr = base_addr + r->r_addend;
                break;

            case R_AARCH64_GLOB_DAT:
            case R_AARCH64_JUMP_SLOT:
                // Symbol value - need to resolve from symbol table or external libraries
                if (symtab && sym_idx > 0) {
                    const elf64_sym_t *sym = &symtab[sym_idx];
                    
                    // Check if symbol is defined in this ELF
                    if (sym->st_shndx != 0 && sym->st_value != 0) {
                        // Symbol defined in this ELF
                        *reloc_addr = base_addr + sym->st_value;
                    } else if (strtab && sym->st_name != 0) {
                        // External symbol - need to resolve from other libraries
                        const char *sym_name = strtab + sym->st_name;
                        
                        // Try to resolve from loaded libraries
                        // Note: This requires elf_loader_resolve_symbol to be available
                        extern uint64_t elf_loader_resolve_symbol(const char *);
                        uint64_t sym_addr = elf_loader_resolve_symbol(sym_name);
                        
                        if (sym_addr != 0) {
                            *reloc_addr = sym_addr;
                        } else {
                            logger_warn("Could not resolve external symbol '%s', setting to 0\n", sym_name);
                            *reloc_addr = 0;
                        }
                    } else {
                        logger_error("Invalid symbol for relocation\n");
                        return ELF_ERROR_RELOCATION_FAILED;
                    }
                } else {
                    logger_error("Symbol index %llu not found for relocation type %llu\n", sym_idx, type);
                    return ELF_ERROR_RELOCATION_FAILED;
                }
                break;

            case R_AARCH64_ABS64:
                // Absolute 64-bit address
                if (symtab && sym_idx > 0) {
                    const elf64_sym_t *sym = &symtab[sym_idx];
                    *reloc_addr = base_addr + sym->st_value + r->r_addend;
                } else {
                    logger_error("Symbol index %llu not found for ABS64 relocation\n", sym_idx);
                    return ELF_ERROR_RELOCATION_FAILED;
                }
                break;

            default:
                logger_warn("Unsupported relocation type: %llu\n", type);
                return ELF_ERROR_RELOCATION_FAILED;
        }
    }

    return ELF_SUCCESS;
}

/**
 * Load program segments into memory
 */
static elf_result_t
elf_load_segments(const elf64_ehdr_t *ehdr, uint64_t base_addr, uint64_t load_base)
{
    const uint8_t *elf_base = (const uint8_t *) base_addr;
    const elf64_phdr_t *phdr = (const elf64_phdr_t *) (elf_base + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *p = &phdr[i];

        if (p->p_type == PT_LOAD) {
            // Calculate destination address
            uint64_t dest_addr = load_base + p->p_vaddr;
            const uint8_t *src = elf_base + p->p_offset;

            logger_info("Loading segment %u: vaddr=0x%llx, filesz=%llu, memsz=%llu\n",
                        i, p->p_vaddr, p->p_filesz, p->p_memsz);

            // Copy file content
            if (p->p_filesz > 0) {
                memcpy((void *) dest_addr, src, p->p_filesz);
            }

            // Zero out remaining memory (BSS section)
            if (p->p_memsz > p->p_filesz) {
                memset((void *) (dest_addr + p->p_filesz), 0, p->p_memsz - p->p_filesz);
            }
        }
    }

    return ELF_SUCCESS;
}

/**
 * Process dynamic section for relocations
 */
static elf_result_t
elf_process_dynamic(const elf64_ehdr_t *ehdr, uint64_t base_addr, uint64_t load_base)
{
    const uint8_t *elf_base = (const uint8_t *) base_addr;
    const elf64_phdr_t *phdr = (const elf64_phdr_t *) (elf_base + ehdr->e_phoff);

    // Find dynamic segment
    const elf64_phdr_t *dyn_phdr = NULL;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn_phdr = &phdr[i];
            break;
        }
    }

    if (!dyn_phdr) {
        // No dynamic segment, nothing to do
        return ELF_SUCCESS;
    }

    const elf64_dyn_t *dyn = (const elf64_dyn_t *) (elf_base + dyn_phdr->p_offset);
    const elf64_rela_t *rela = NULL;
    size_t rela_sz = 0;
    const elf64_rela_t *jmprel = NULL;
    size_t pltrelsz = 0;
    const elf64_sym_t *symtab = NULL;
    const char *strtab = NULL;

    // Parse dynamic entries
    // Note: For ELF files loaded in place, d_ptr values are treated as offsets
    // from elf_base. This works for our use case where the ELF file is already
    // in memory at base_addr and we're processing it in-place.
    for (size_t i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
            case DT_RELA:
                // Treat d_ptr as offset from elf_base (works for in-place loading)
                rela = (const elf64_rela_t *) (elf_base + dyn[i].d_un.d_ptr);
                break;
            case DT_RELASZ:
                rela_sz = dyn[i].d_un.d_val;
                break;
            case DT_JMPREL:
                // PLT relocations
                jmprel = (const elf64_rela_t *) (elf_base + dyn[i].d_un.d_ptr);
                break;
            case DT_PLTRELSZ:
                // PLT relocation table size
                pltrelsz = dyn[i].d_un.d_val;
                break;
            case DT_SYMTAB:
                // Treat d_ptr as offset from elf_base
                symtab = (const elf64_sym_t *) (elf_base + dyn[i].d_un.d_ptr);
                break;
            case DT_STRTAB:
                // Treat d_ptr as offset from elf_base
                strtab = (const char *) (elf_base + dyn[i].d_un.d_ptr);
                break;
        }
    }

    // Apply .rela.dyn relocations if found
    if (rela && rela_sz > 0) {
        size_t rela_count = rela_sz / sizeof(elf64_rela_t);
        logger_info("Applying %zu .rela.dyn relocations\n", rela_count);
        elf_result_t result = elf_apply_relocations(load_base, rela, rela_count, symtab, strtab);
        if (result != ELF_SUCCESS) {
            return result;
        }
    }

    // Apply .rela.plt relocations if found
    if (jmprel && pltrelsz > 0) {
        size_t jmprel_count = pltrelsz / sizeof(elf64_rela_t);
        logger_info("Applying %zu .rela.plt relocations\n", jmprel_count);
        elf_result_t result = elf_apply_relocations(load_base, jmprel, jmprel_count, symtab, strtab);
        if (result != ELF_SUCCESS) {
            return result;
        }
    }

    return ELF_SUCCESS;
}

/**
 * Load an executable ELF file
 */
elf_result_t
elf_load_executable(elf_descriptor_t *desc)
{
    if (!desc || desc->start_addr == 0 || desc->size == 0) {
        return ELF_ERROR_INVALID_DESCRIPTOR;
    }

    logger_info("Loading executable ELF: %s\n", desc->name);
    logger_info("  Start address: 0x%llx\n", desc->start_addr);
    logger_info("  Size: %llu bytes\n", desc->size);

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *) desc->start_addr;

    // Validate header
    elf_result_t result = elf_validate_header(ehdr);
    if (result != ELF_SUCCESS) {
        return result;
    }

    // Check if it's an executable
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return ELF_ERROR_INVALID_TYPE;
    }

    desc->type = ehdr->e_type;
    
    // Load segments
    // For executables, load base is typically 0 (uses absolute addresses)
    // For position-independent executables (PIE), use start_addr as base
    uint64_t load_base = (ehdr->e_type == ET_DYN) ? desc->start_addr : 0;
    
    // Calculate actual entry point
    // For ET_DYN (PIE/shared objects), entry is relative to load base
    // For ET_EXEC, entry is absolute
    desc->entry_point = (ehdr->e_type == ET_DYN) ? 
                        (load_base + ehdr->e_entry) : ehdr->e_entry;
    
    result = elf_load_segments(ehdr, desc->start_addr, load_base);
    if (result != ELF_SUCCESS) {
        return result;
    }

    // Process dynamic section for relocations
    result = elf_process_dynamic(ehdr, desc->start_addr, load_base);
    if (result != ELF_SUCCESS) {
        return result;
    }

    desc->is_loaded = true;
    logger_info("Successfully loaded executable ELF\n");
    logger_info("  Entry point: 0x%llx\n", desc->entry_point);

    return ELF_SUCCESS;
}

/**
 * Load a dynamic library ELF file
 */
elf_result_t
elf_load_dynamic(elf_descriptor_t *desc, uint64_t base_addr)
{
    if (!desc || desc->start_addr == 0 || desc->size == 0) {
        return ELF_ERROR_INVALID_DESCRIPTOR;
    }

    logger_info("Loading dynamic library ELF: %s\n", desc->name);
    logger_info("  Start address: 0x%llx\n", desc->start_addr);
    logger_info("  Size: %llu bytes\n", desc->size);

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *) desc->start_addr;

    // Validate header
    elf_result_t result = elf_validate_header(ehdr);
    if (result != ELF_SUCCESS) {
        return result;
    }

    // Check if it's a shared object
    if (ehdr->e_type != ET_DYN) {
        return ELF_ERROR_INVALID_TYPE;
    }

    desc->type = ehdr->e_type;
    
    // Use provided base address or the file's location
    uint64_t load_base = base_addr ? base_addr : desc->start_addr;
    
    // Calculate actual entry point (relative to load base for ET_DYN)
    desc->entry_point = load_base + ehdr->e_entry;

    // Load segments
    result = elf_load_segments(ehdr, desc->start_addr, load_base);
    if (result != ELF_SUCCESS) {
        return result;
    }

    // Process dynamic section for relocations
    result = elf_process_dynamic(ehdr, desc->start_addr, load_base);
    if (result != ELF_SUCCESS) {
        return result;
    }

    desc->is_loaded = true;
    logger_info("Successfully loaded dynamic library ELF\n");

    return ELF_SUCCESS;
}

/**
 * Find a symbol in a loaded ELF
 */
elf_result_t
elf_find_symbol(const elf_descriptor_t *desc,
                const char *symbol_name,
                uint64_t *symbol_addr)
{
    if (!desc || !symbol_name || !symbol_addr || !desc->is_loaded) {
        return ELF_ERROR_INVALID_DESCRIPTOR;
    }

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *) desc->start_addr;
    const uint8_t *elf_base = (const uint8_t *) desc->start_addr;

    // Find section headers
    if (ehdr->e_shoff == 0) {
        return ELF_ERROR_SYMBOL_NOT_FOUND;
    }

    const elf64_shdr_t *shdr = (const elf64_shdr_t *) (elf_base + ehdr->e_shoff);
    const elf64_sym_t *symtab = NULL;
    const char *strtab = NULL;
    size_t symtab_entries = 0;

    // Find symbol table and string table
    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
            symtab = (const elf64_sym_t *) (elf_base + shdr[i].sh_offset);
            symtab_entries = shdr[i].sh_size / sizeof(elf64_sym_t);
            
            // String table is linked section
            if (shdr[i].sh_link < ehdr->e_shnum) {
                strtab = (const char *) (elf_base + shdr[shdr[i].sh_link].sh_offset);
            }
            break;
        }
    }

    if (!symtab || !strtab) {
        return ELF_ERROR_SYMBOL_NOT_FOUND;
    }

    // Search for symbol
    for (size_t i = 0; i < symtab_entries; i++) {
        const elf64_sym_t *sym = &symtab[i];
        if (sym->st_name == 0) {
            continue;
        }

        const char *name = strtab + sym->st_name;
        if (strcmp(name, symbol_name) == 0) {
            uint64_t load_base = (desc->type == ET_DYN) ? desc->start_addr : 0;
            *symbol_addr = load_base + sym->st_value;
            return ELF_SUCCESS;
        }
    }

    return ELF_ERROR_SYMBOL_NOT_FOUND;
}

/**
 * Get list of library dependencies from an ELF file
 */
size_t
elf_get_dependencies(uint64_t base_addr, char deps[][64], size_t max_deps)
{
    if (base_addr == 0 || !deps || max_deps == 0) {
        return 0;
    }

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *) base_addr;
    const uint8_t *elf_base = (const uint8_t *) base_addr;

    // Validate header first
    if (elf_validate_header(ehdr) != ELF_SUCCESS) {
        return 0;
    }

    // Find program headers
    const elf64_phdr_t *phdr = (const elf64_phdr_t *) (elf_base + ehdr->e_phoff);

    // Find dynamic segment
    const elf64_phdr_t *dyn_phdr = NULL;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn_phdr = &phdr[i];
            break;
        }
    }

    if (!dyn_phdr) {
        // No dynamic segment, no dependencies
        return 0;
    }

    // Parse dynamic section
    const elf64_dyn_t *dyn = (const elf64_dyn_t *) (elf_base + dyn_phdr->p_offset);
    const char *strtab = NULL;

    // First pass: find string table
    for (size_t i = 0; dyn[i].d_tag != DT_NULL; i++) {
        if (dyn[i].d_tag == DT_STRTAB) {
            strtab = (const char *) (elf_base + dyn[i].d_un.d_ptr);
            break;
        }
    }

    if (!strtab) {
        return 0;
    }

    // Second pass: collect DT_NEEDED entries
    size_t dep_count = 0;
    for (size_t i = 0; dyn[i].d_tag != DT_NULL && dep_count < max_deps; i++) {
        if (dyn[i].d_tag == DT_NEEDED) {
            const char *lib_name = strtab + dyn[i].d_un.d_val;
            
            // Copy library name to output array
            size_t j;
            for (j = 0; j < 63 && lib_name[j] != '\0'; j++) {
                deps[dep_count][j] = lib_name[j];
            }
            deps[dep_count][j] = '\0';
            
            dep_count++;
        }
    }

    return dep_count;
}

/**
 * Get human-readable error message
 */
const char *
elf_error_string(elf_result_t result)
{
    switch (result) {
        case ELF_SUCCESS:
            return "Success";
        case ELF_ERROR_INVALID_MAGIC:
            return "Invalid ELF magic number";
        case ELF_ERROR_INVALID_CLASS:
            return "Invalid ELF class (not 64-bit)";
        case ELF_ERROR_INVALID_ENDIAN:
            return "Invalid endianness (not little-endian)";
        case ELF_ERROR_INVALID_VERSION:
            return "Invalid ELF version";
        case ELF_ERROR_INVALID_TYPE:
            return "Invalid ELF type";
        case ELF_ERROR_INVALID_MACHINE:
            return "Invalid machine type (not AArch64)";
        case ELF_ERROR_NO_MEMORY:
            return "Out of memory";
        case ELF_ERROR_INVALID_SEGMENT:
            return "Invalid segment";
        case ELF_ERROR_RELOCATION_FAILED:
            return "Relocation failed";
        case ELF_ERROR_SYMBOL_NOT_FOUND:
            return "Symbol not found";
        case ELF_ERROR_INVALID_DESCRIPTOR:
            return "Invalid ELF descriptor";
        default:
            return "Unknown error";
    }
}

/**
 * Dump ELF descriptor information
 */
void
elf_dump_descriptor(const elf_descriptor_t *desc)
{
    if (!desc) {
        logger_warn("NULL descriptor\n");
        return;
    }

    logger_info("=== ELF Descriptor ===\n");
    logger_info("  Name: %s\n", desc->name);
    logger_info("  Start Address: 0x%llx\n", desc->start_addr);
    logger_info("  Size: %llu bytes\n", desc->size);
    logger_info("  Entry Point: 0x%llx\n", desc->entry_point);
    logger_info("  Type: %u ", desc->type);
    switch (desc->type) {
        case ET_EXEC: logger_info("(Executable)\n"); break;
        case ET_DYN:  logger_info("(Shared Object)\n"); break;
        default:      logger_info("(Unknown)\n"); break;
    }
    logger_info("  Loaded: %s\n", desc->is_loaded ? "Yes" : "No");
    logger_info("====================\n");
}

/**
 * Dump ELF header information
 */
void
elf_dump_header(const elf64_ehdr_t *ehdr)
{
    if (!ehdr) {
        logger_warn("NULL header\n");
        return;
    }

    logger_info("=== ELF Header ===\n");
    logger_info("  Magic: 0x%02x%02x%02x%02x\n",
                ehdr->e_ident[0], ehdr->e_ident[1],
                ehdr->e_ident[2], ehdr->e_ident[3]);
    logger_info("  Class: %u (%s)\n", 
                ehdr->e_ident[4],
                ehdr->e_ident[4] == ELFCLASS64 ? "64-bit" : "32-bit");
    logger_info("  Data: %u (%s)\n",
                ehdr->e_ident[5],
                ehdr->e_ident[5] == ELFDATA2LSB ? "Little-endian" : "Big-endian");
    logger_info("  Type: %u ", ehdr->e_type);
    switch (ehdr->e_type) {
        case ET_EXEC: logger_info("(Executable)\n"); break;
        case ET_DYN:  logger_info("(Shared Object)\n"); break;
        case ET_REL:  logger_info("(Relocatable)\n"); break;
        default:      logger_info("(Unknown)\n"); break;
    }
    logger_info("  Machine: %u ", ehdr->e_machine);
    if (ehdr->e_machine == EM_AARCH64) {
        logger_info("(AArch64)\n");
    } else {
        logger_info("(Unknown)\n");
    }
    logger_info("  Entry: 0x%llx\n", ehdr->e_entry);
    logger_info("  Program Headers: %u (offset 0x%llx)\n", 
                ehdr->e_phnum, ehdr->e_phoff);
    logger_info("  Section Headers: %u (offset 0x%llx)\n",
                ehdr->e_shnum, ehdr->e_shoff);
    logger_info("==================\n");
}
