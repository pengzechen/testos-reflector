#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#define ELF_MAGIC   0x454C4654u  // "ELFT"
#define ELF_VERSION 1
#define MAX_ENTRIES 32

struct elf_entry {
    char name[64];
    uint64_t start_addr;
    uint64_t size;
    uint32_t flags;     // 1=library, 0=executable
    uint32_t reserved;
} __attribute__((packed));

struct elf_table {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
    struct elf_entry entries[MAX_ENTRIES];
} __attribute__((packed));

static int is_library(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 1;  // 没有后缀的也算库
    // 如果是 .elf 就返回 0（可执行），其他都返回 1（库）
    return strcmp(dot, ".elf") != 0;
}

static uint64_t get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        perror(path);
        return 0;
    }
    return (uint64_t)st.st_size;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s --output elf_table.bin --entry <file> <load_addr> [--entry ...]\n", argv[0]);
        printf("Example:\n");
        printf("  %s --output elf_table.bin \\\n", argv[0]);
        printf("      --entry libc.so 0x80000000 \\\n");
        printf("      --entry hello.elf 0x81000000 \\\n");
        printf("      --entry simple.elf 0x82000000\n");
        return 1;
    }

    struct elf_table table = {0};
    table.magic = ELF_MAGIC;
    table.version = ELF_VERSION;
    table.count = 0;

    const char *out_path = NULL;

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--output") == 0) {
            out_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--entry") == 0) {
            if (i + 3 > argc) {
                fprintf(stderr, "Error: --entry requires <file> <addr>\n");
                return 1;
            }

            if (table.count >= MAX_ENTRIES) {
                fprintf(stderr, "Error: too many entries (max=%d)\n", MAX_ENTRIES);
                return 1;
            }

            const char *file = argv[i + 1];
            uint64_t addr = strtoull(argv[i + 2], NULL, 0);

            struct elf_entry *e = &table.entries[table.count++];
            memset(e, 0, sizeof(*e));

            // basename
            const char *basename = strrchr(file, '/');
            if (basename) basename++;
            else basename = file;

            strncpy(e->name, basename, sizeof(e->name) - 1);
            e->start_addr = addr;
            e->size = get_file_size(file);
            e->flags = is_library(e->name) ? 1 : 0;
            e->reserved = 0;

            i += 3;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!out_path) {
        fprintf(stderr, "Error: no --output specified\n");
        return 1;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        perror(out_path);
        return 1;
    }

    fwrite(&table, sizeof(struct elf_table), 1, f);
    fclose(f);

    printf("✅ Generated %s\n", out_path);
    printf("Entries (%u):\n", table.count);
    for (unsigned i = 0; i < table.count; i++) {
        struct elf_entry *e = &table.entries[i];
        printf("  [%02u] %-20s addr=0x%llx size=%llu flags=%s\n",
               i, e->name,
               (unsigned long long)e->start_addr,
               (unsigned long long)e->size,
               e->flags ? "LIB" : "EXE");
    }

    return 0;
}


/*
./gen_table \
  --output ./elf_table.bin \
  --entry ../tools/musl-libs/lib/libc.so                 0x80000000 \
  --entry ../tools/musl-libs/lib/libgcc_s.so.1           0x81000000 \
  --entry ./libstdc++.so.6     0x82000000 \
  --entry ../tools/musl-libs/lib/libitm.so.1.0.0         0x84000000 \
  --entry hello.elf                                      0x85000000 \
  --entry simple.elf                                     0x86000000 \
  --entry hello_cpp.elf                                  0x87000000
*/