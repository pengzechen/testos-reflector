# ELF 加载器功能

[English](#english-version) | [中文](#中文版本)

## 中文版本

### 概述

ELF 加载器为 ARM64 (AArch64) 架构提供了加载和执行 ELF (Executable and Linkable Format) 文件的功能。支持可执行文件和动态库的加载。

### 架构设计

Bootloader 将用户态程序复制到 2GB (0x80000000) 以上的内存区域。每个程序通过 `elf_descriptor_t` 结构描述，包含：
- ELF 文件名称
- 内存起始地址（bootloader 放置的位置）
- 文件大小（字节）

### 主要特性

#### 1. 可执行文件加载 (ET_EXEC)
- 加载可加载段 (PT_LOAD)
- BSS 段初始化
- 入口点执行支持

#### 2. 动态库加载 (ET_DYN)
- 位置无关加载
- 动态重定位 (RELA)
- 符号解析
- 支持的重定位类型：
  - R_AARCH64_NONE
  - R_AARCH64_RELATIVE
  - R_AARCH64_GLOB_DAT
  - R_AARCH64_JUMP_SLOT
  - R_AARCH64_ABS64

### 内存布局

```
0x00000000 - 0x7FFFFFFF: 内核空间
0x80000000 - 0xFFFFFFFF: 用户程序空间（bootloader 加载）
  0x80000000: 第一个 ELF
  0x80100000: 第二个 ELF
  ...
```

### 核心数据结构

#### ELF 描述符

```c
typedef struct {
    char     name[64];       // ELF 文件名
    uint64_t start_addr;     // 内存起始地址 (>= 0x80000000)
    uint64_t size;           // 大小（字节）
    uint64_t entry_point;    // 入口点地址（由加载器填充）
    uint16_t type;           // ELF 类型 (ET_EXEC, ET_DYN 等)
    bool     is_loaded;      // 是否已加载
} elf_descriptor_t;
```

#### Bootloader ELF 信息表

```c
typedef struct {
    char     name[64];       // ELF 文件名
    uint64_t start_addr;     // 内存起始地址
    uint64_t size;           // 大小（字节）
    uint32_t flags;          // 标志位（bit 0: 0=可执行文件, 1=库）
    uint32_t reserved;       // 保留字段
} bootloader_elf_info_t;

typedef struct {
    uint32_t magic;          // 魔数: 0x454C4654 ("ELFT")
    uint32_t version;        // 表版本
    uint32_t count;          // ELF 条目数量
    uint32_t reserved;       // 保留字段
    bootloader_elf_info_t entries[MAX_ELF_FILES];
} bootloader_elf_table_t;
```

### API 函数

#### 初始化加载器

```c
size_t elf_loader_init(uint64_t table_addr);
```

从 bootloader 提供的 ELF 表初始化加载器并加载所有程序。

#### 查找程序

```c
const elf_descriptor_t *elf_loader_get_program(const char *name);
const elf_descriptor_t *elf_loader_get_program_by_index(size_t index);
```

通过名称或索引查找已加载的程序。

#### 执行程序

```c
bool elf_loader_execute(const char *name);
```

执行指定名称的程序（跳转到入口点）。

#### 符号查找

```c
elf_result_t elf_find_symbol(const elf_descriptor_t *desc,
                              const char *symbol_name,
                              uint64_t *symbol_addr);
```

在已加载的 ELF 中查找符号。

### 使用示例

#### 示例 1: 初始化并加载程序

```c
#include "lib/t_elf_loader.h"
#include "lib/t_logger.h"

void init_user_programs(void)
{
    // Bootloader 将 ELF 表放在固定地址
    const uint64_t elf_table_addr = 0x7F000000;
    
    // 初始化加载器
    size_t loaded = elf_loader_init(elf_table_addr);
    logger_info("成功加载 %zu 个程序\n", loaded);
    
    // 列出所有程序
    elf_loader_list_programs();
}
```

#### 示例 2: 执行用户程序

```c
void run_init_process(void)
{
    // 查找并执行 init 程序
    if (elf_loader_execute("init")) {
        logger_info("Init 进程启动成功\n");
    } else {
        logger_error("Init 进程启动失败\n");
    }
}
```

#### 示例 3: 使用动态库

```c
void use_library_function(void)
{
    // 查找库
    const elf_descriptor_t *libc = elf_loader_get_program("libc.so");
    if (!libc) {
        logger_error("未找到 libc.so\n");
        return;
    }
    
    // 查找符号
    uint64_t printf_addr;
    if (elf_find_symbol(libc, "printf", &printf_addr) == ELF_SUCCESS) {
        logger_info("找到 printf() 函数: 0x%llx\n", printf_addr);
    }
}
```

### 集成步骤

1. **Bootloader 准备**：
   - 将用户程序复制到 0x80000000 以上的内存
   - 在固定地址（如 0x7F000000）创建 ELF 信息表
   - 填充每个 ELF 的名称、地址和大小

2. **内核集成**：
   ```c
   #include "lib/t_elf_loader.h"
   
   void kernel_init(void) {
       // 其他初始化...
       
       // 初始化 ELF 加载器
       uint64_t elf_table_addr = 0x7F000000;  // 从 bootloader 参数获取
       elf_loader_init(elf_table_addr);
       
       // 执行 init 程序
       elf_loader_execute("init");
   }
   ```

3. **测试功能**：
   ```c
   void t_elf_run_tests(void);  // 在 t_kernel_main() 中调用
   ```

### 文件结构

```
include/lib/
  ├── t_elf.h           # ELF 核心定义和 API
  └── t_elf_loader.h    # 高级加载器接口

src/lib/
  ├── t_elf.c           # ELF 加载器实现
  ├── t_elf_test.c      # 测试套件
  └── t_elf_loader.c    # 加载器集成实现

docs/
  └── elf_loader.md     # 详细 API 文档
```

### 限制和注意事项

1. 没有动态链接器 - 库必须预链接
2. 有限的重定位类型支持
3. 不支持延迟绑定（PLT/GOT 在加载时解析）
4. 暂不支持 TLS (线程本地存储)
5. 符号查找为线性搜索（未优化哈希表）

### 测试

运行测试套件验证 ELF 加载器功能：

```c
t_elf_run_tests();
```

测试包括：
- ELF 头部解析验证
- 描述符管理
- 错误处理
- 调试输出

---

## English Version

### Overview

The ELF loader provides functionality to load and execute ELF (Executable and Linkable Format) files on ARM64 (AArch64) architecture. It supports both executable files and dynamic libraries.

### Architecture Design

The bootloader copies user-mode programs to memory above 2GB (0x80000000). Each program is described by an `elf_descriptor_t` structure containing:
- ELF file name
- Start address in memory (where bootloader placed it)
- File size in bytes

### Key Features

#### 1. Executable Loading (ET_EXEC)
- Loading of loadable segments (PT_LOAD)
- BSS section initialization
- Entry point execution support

#### 2. Dynamic Library Loading (ET_DYN)
- Position-independent loading
- Dynamic relocations (RELA)
- Symbol resolution
- Supported relocation types:
  - R_AARCH64_NONE
  - R_AARCH64_RELATIVE
  - R_AARCH64_GLOB_DAT
  - R_AARCH64_JUMP_SLOT
  - R_AARCH64_ABS64

### Memory Layout

```
0x00000000 - 0x7FFFFFFF: Kernel space
0x80000000 - 0xFFFFFFFF: User program space (loaded by bootloader)
  0x80000000: First ELF
  0x80100000: Second ELF
  ...
```

### Core Data Structures

#### ELF Descriptor

```c
typedef struct {
    char     name[64];       // ELF file name
    uint64_t start_addr;     // Start address in memory (>= 0x80000000)
    uint64_t size;           // Size in bytes
    uint64_t entry_point;    // Entry point address (filled by loader)
    uint16_t type;           // ELF type (ET_EXEC, ET_DYN, etc.)
    bool     is_loaded;      // Whether the ELF is loaded
} elf_descriptor_t;
```

#### Bootloader ELF Information Table

```c
typedef struct {
    char     name[64];       // ELF file name
    uint64_t start_addr;     // Start address in memory
    uint64_t size;           // Size in bytes
    uint32_t flags;          // Flags (bit 0: 0=executable, 1=library)
    uint32_t reserved;       // Reserved for future use
} bootloader_elf_info_t;

typedef struct {
    uint32_t magic;          // Magic number: 0x454C4654 ("ELFT")
    uint32_t version;        // Table version
    uint32_t count;          // Number of ELF entries
    uint32_t reserved;       // Reserved for future use
    bootloader_elf_info_t entries[MAX_ELF_FILES];
} bootloader_elf_table_t;
```

### API Functions

#### Initialize Loader

```c
size_t elf_loader_init(uint64_t table_addr);
```

Initialize the loader from bootloader's ELF table and load all programs.

#### Find Programs

```c
const elf_descriptor_t *elf_loader_get_program(const char *name);
const elf_descriptor_t *elf_loader_get_program_by_index(size_t index);
```

Find loaded programs by name or index.

#### Execute Program

```c
bool elf_loader_execute(const char *name);
```

Execute a program by name (jump to entry point).

#### Symbol Lookup

```c
elf_result_t elf_find_symbol(const elf_descriptor_t *desc,
                              const char *symbol_name,
                              uint64_t *symbol_addr);
```

Find a symbol in a loaded ELF file.

### Usage Examples

#### Example 1: Initialize and Load Programs

```c
#include "lib/t_elf_loader.h"
#include "lib/t_logger.h"

void init_user_programs(void)
{
    // Bootloader places ELF table at fixed address
    const uint64_t elf_table_addr = 0x7F000000;
    
    // Initialize loader
    size_t loaded = elf_loader_init(elf_table_addr);
    logger_info("Successfully loaded %zu programs\n", loaded);
    
    // List all programs
    elf_loader_list_programs();
}
```

#### Example 2: Execute User Program

```c
void run_init_process(void)
{
    // Find and execute init program
    if (elf_loader_execute("init")) {
        logger_info("Init process started successfully\n");
    } else {
        logger_error("Failed to start init process\n");
    }
}
```

#### Example 3: Use Dynamic Library

```c
void use_library_function(void)
{
    // Find library
    const elf_descriptor_t *libc = elf_loader_get_program("libc.so");
    if (!libc) {
        logger_error("libc.so not found\n");
        return;
    }
    
    // Find symbol
    uint64_t printf_addr;
    if (elf_find_symbol(libc, "printf", &printf_addr) == ELF_SUCCESS) {
        logger_info("Found printf() function at 0x%llx\n", printf_addr);
    }
}
```

### Integration Steps

1. **Bootloader Preparation**:
   - Copy user programs to memory above 0x80000000
   - Create ELF information table at fixed address (e.g., 0x7F000000)
   - Fill in name, address, and size for each ELF

2. **Kernel Integration**:
   ```c
   #include "lib/t_elf_loader.h"
   
   void kernel_init(void) {
       // Other initialization...
       
       // Initialize ELF loader
       uint64_t elf_table_addr = 0x7F000000;  // Get from boot parameters
       elf_loader_init(elf_table_addr);
       
       // Execute init program
       elf_loader_execute("init");
   }
   ```

3. **Test Functionality**:
   ```c
   void t_elf_run_tests(void);  // Call in t_kernel_main()
   ```

### File Structure

```
include/lib/
  ├── t_elf.h           # Core ELF definitions and API
  └── t_elf_loader.h    # High-level loader interface

src/lib/
  ├── t_elf.c           # ELF loader implementation
  ├── t_elf_test.c      # Test suite
  └── t_elf_loader.c    # Loader integration implementation

docs/
  └── elf_loader.md     # Detailed API documentation
```

### Limitations and Notes

1. No dynamic linker - libraries must be pre-linked
2. Limited relocation type support
3. No lazy binding (PLT/GOT resolved at load time)
4. No TLS (Thread-Local Storage) support yet
5. Symbol lookup is linear (no hash table optimization)

### Testing

Run the test suite to verify ELF loader functionality:

```c
t_elf_run_tests();
```

Tests include:
- ELF header parsing validation
- Descriptor management
- Error handling
- Debug output

---

## 参考资料 / References

- [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [ARM64 ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
- Documentation: `docs/elf_loader.md`
