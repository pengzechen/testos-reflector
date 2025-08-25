# TestOS 开发脚本

这个目录包含了 TestOS 项目的开发辅助脚本。

## 代码格式化脚本

### 1. `format_code.sh` - 完整功能格式化脚本

功能丰富的代码格式化工具，提供详细的输出和多种选项。

**功能特性：**
- 🎨 彩色输出和详细信息
- 🔍 干运行模式 - 检查哪些文件需要格式化
- 📝 详细模式 - 显示每个文件的处理状态
- 📁 单文件格式化 - 格式化指定文件
- ✅ 依赖检查 - 检查 clang-format 和配置文件
- 📊 统计信息 - 显示处理的文件数量

**使用方法：**
```bash
# 显示帮助信息
scripts/format_code.sh --help

# 格式化所有源文件
scripts/format_code.sh

# 检查哪些文件需要格式化（不修改文件）
scripts/format_code.sh --dry-run

# 格式化指定文件
scripts/format_code.sh --file src/main.c

# 详细模式（显示每个文件的处理状态）
scripts/format_code.sh --verbose
```

### 2. `fmt.sh` - 快速格式化脚本

轻量级的快速格式化工具，适合日常开发使用。

**功能特性：**
- ⚡ 快速简单，适合日常使用
- 📁 支持格式化指定文件或所有文件
- 🚀 无额外输出，专注于格式化

**使用方法：**
```bash
# 格式化所有 C/H 文件
scripts/fmt.sh

# 格式化指定文件
scripts/fmt.sh src/main.c

# 格式化多个文件
scripts/fmt.sh file1.c file2.h include/header.h
```

## 开发工作流建议

### 提交前检查
在提交代码前，使用干运行模式检查代码格式：
```bash
scripts/format_code.sh --dry-run
```

### 批量格式化
当需要格式化整个项目时：
```bash
scripts/format_code.sh
```

### 日常开发
修改文件后快速格式化：
```bash
scripts/fmt.sh src/modified_file.c
```

## 配置文件

代码格式化使用项目根目录的 `.clang-format` 配置文件。该配置文件针对底层操作系统开发进行了优化，包括：

- 4 空格缩进
- 100 字符行宽限制
- 函数大括号换行
- 指针右对齐
- 连续赋值对齐
- 等等...

## 依赖要求

- `clang-format` - 代码格式化工具

**安装方法：**
- Ubuntu/Debian: `sudo apt install clang-format`
- macOS: `brew install clang-format`
- Arch Linux: `sudo pacman -S clang`

## 注意事项

1. 脚本会自动跳过 `build/` 目录和 `.git/` 目录
2. 只处理 `.c` 和 `.h` 文件
3. 格式化是就地修改，请确保重要代码已提交到版本控制
4. 脚本可以从项目的任何位置运行，会自动找到项目根目录
