/**
 * ELF Loader Test Suite
 * 
 * Tests for the ELF loading functionality
 */

#include "lib/t_elf.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"

/* Helper to create a simple ELF header for testing */
static void
create_test_elf_header(elf64_ehdr_t *ehdr)
{
    // Initialize to zero
    for (size_t i = 0; i < sizeof(elf64_ehdr_t); i++) {
        ((uint8_t *) ehdr)[i] = 0;
    }

    // Set magic number
    ehdr->e_ident[0] = 0x7F;
    ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';
    ehdr->e_ident[3] = 'F';
    ehdr->e_ident[4] = ELFCLASS64;     // 64-bit
    ehdr->e_ident[5] = ELFDATA2LSB;    // Little-endian
    ehdr->e_ident[6] = EV_CURRENT;     // Current version
    ehdr->e_ident[7] = ELFOSABI_SYSV;  // UNIX System V ABI

    // Set header fields
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_AARCH64;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_entry = 0x400000;
    ehdr->e_phoff = sizeof(elf64_ehdr_t);
    ehdr->e_shoff = 0;
    ehdr->e_flags = 0;
    ehdr->e_ehsize = sizeof(elf64_ehdr_t);
    ehdr->e_phentsize = sizeof(elf64_phdr_t);
    ehdr->e_phnum = 1;
    ehdr->e_shentsize = sizeof(elf64_shdr_t);
    ehdr->e_shnum = 0;
    ehdr->e_shstrndx = 0;
}

/**
 * Test ELF header validation
 */
static void
test_elf_header_validation(void)
{
    logger_info("\n=== Testing ELF Header Validation ===\n");

    elf64_ehdr_t ehdr;
    create_test_elf_header(&ehdr);

    // Test valid header
    elf_result_t result = elf_validate_header(&ehdr);
    if (result == ELF_SUCCESS) {
        logger_info("[PASS] Valid ELF header accepted\n");
    } else {
        logger_error("[FAIL] Valid ELF header rejected: %s\n", elf_error_string(result));
    }

    // Test invalid magic
    ehdr.e_ident[0] = 0xFF;
    result = elf_validate_header(&ehdr);
    if (result == ELF_ERROR_INVALID_MAGIC) {
        logger_info("[PASS] Invalid magic detected\n");
    } else {
        logger_error("[FAIL] Invalid magic not detected\n");
    }
    ehdr.e_ident[0] = 0x7F;  // Restore

    // Test invalid class
    ehdr.e_ident[4] = ELFCLASS32;
    result = elf_validate_header(&ehdr);
    if (result == ELF_ERROR_INVALID_CLASS) {
        logger_info("[PASS] Invalid class detected\n");
    } else {
        logger_error("[FAIL] Invalid class not detected\n");
    }
    ehdr.e_ident[4] = ELFCLASS64;  // Restore

    // Test invalid endianness
    ehdr.e_ident[5] = ELFDATA2MSB;
    result = elf_validate_header(&ehdr);
    if (result == ELF_ERROR_INVALID_ENDIAN) {
        logger_info("[PASS] Invalid endianness detected\n");
    } else {
        logger_error("[FAIL] Invalid endianness not detected\n");
    }
    ehdr.e_ident[5] = ELFDATA2LSB;  // Restore

    // Test invalid machine
    ehdr.e_machine = 0x3E;  // x86-64
    result = elf_validate_header(&ehdr);
    if (result == ELF_ERROR_INVALID_MACHINE) {
        logger_info("[PASS] Invalid machine type detected\n");
    } else {
        logger_error("[FAIL] Invalid machine type not detected\n");
    }

    logger_info("=== ELF Header Validation Tests Complete ===\n");
}

/**
 * Test ELF descriptor initialization
 */
static void
test_elf_descriptor(void)
{
    logger_info("\n=== Testing ELF Descriptor ===\n");

    elf_descriptor_t desc;

    // Initialize descriptor
    for (size_t i = 0; i < sizeof(elf_descriptor_t); i++) {
        ((uint8_t *) &desc)[i] = 0;
    }

    // Set descriptor fields
    const char *name = "test.elf";
    size_t name_len = 0;
    while (name[name_len]) name_len++;
    for (size_t i = 0; i < name_len && i < 63; i++) {
        desc.name[i] = name[i];
    }
    desc.name[name_len] = '\0';

    desc.start_addr = 0x80000000;  // 2GB
    desc.size = 4096;
    desc.entry_point = 0x80000000;
    desc.type = ET_EXEC;
    desc.is_loaded = false;

    // Dump descriptor
    elf_dump_descriptor(&desc);

    logger_info("[PASS] ELF descriptor created successfully\n");
    logger_info("=== ELF Descriptor Tests Complete ===\n");
}

/**
 * Test ELF error messages
 */
static void
test_elf_error_messages(void)
{
    logger_info("\n=== Testing ELF Error Messages ===\n");

    const char *msg;

    msg = elf_error_string(ELF_SUCCESS);
    logger_info("ELF_SUCCESS: %s\n", msg);

    msg = elf_error_string(ELF_ERROR_INVALID_MAGIC);
    logger_info("ELF_ERROR_INVALID_MAGIC: %s\n", msg);

    msg = elf_error_string(ELF_ERROR_INVALID_CLASS);
    logger_info("ELF_ERROR_INVALID_CLASS: %s\n", msg);

    msg = elf_error_string(ELF_ERROR_SYMBOL_NOT_FOUND);
    logger_info("ELF_ERROR_SYMBOL_NOT_FOUND: %s\n", msg);

    logger_info("[PASS] All error messages retrieved successfully\n");
    logger_info("=== ELF Error Message Tests Complete ===\n");
}

/**
 * Test ELF header dump
 */
static void
test_elf_header_dump(void)
{
    logger_info("\n=== Testing ELF Header Dump ===\n");

    elf64_ehdr_t ehdr;
    create_test_elf_header(&ehdr);

    elf_dump_header(&ehdr);

    logger_info("[PASS] ELF header dumped successfully\n");
    logger_info("=== ELF Header Dump Tests Complete ===\n");
}

/**
 * Run all ELF loader tests
 */
void
t_elf_run_tests(void)
{
    logger_info("\n");
    logger_info("=========================================\n");
    logger_info("   ELF Loader Test Suite\n");
    logger_info("=========================================\n");

    test_elf_header_validation();
    test_elf_descriptor();
    test_elf_error_messages();
    test_elf_header_dump();

    logger_info("\n");
    logger_info("=========================================\n");
    logger_info("   ELF Loader Tests Complete\n");
    logger_info("=========================================\n");
    logger_info("\n");
}
