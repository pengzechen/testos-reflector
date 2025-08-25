#!/bin/bash

# TestOS 代码格式化脚本
# 使用 clang-format 格式化整个项目的 C/C++ 源文件

set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 脚本配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLANG_FORMAT_CONFIG="$PROJECT_ROOT/.clang-format"

# 检查 clang-format 是否安装
check_clang_format() {
    if ! command -v clang-format &> /dev/null; then
        echo -e "${RED}错误: clang-format 未安装${NC}"
        echo "请安装 clang-format:"
        echo "  Ubuntu/Debian: sudo apt install clang-format"
        echo "  macOS: brew install clang-format"
        echo "  Arch Linux: sudo pacman -S clang"
        exit 1
    fi
    
    echo -e "${GREEN}✓ clang-format 已安装: $(clang-format --version)${NC}"
}

# 检查配置文件
check_config() {
    if [[ ! -f "$CLANG_FORMAT_CONFIG" ]]; then
        echo -e "${RED}错误: 未找到 .clang-format 配置文件${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✓ 找到配置文件: $CLANG_FORMAT_CONFIG${NC}"
}

# 查找需要格式化的文件
find_source_files() {
    local files=()
    
    # 查找 .c 和 .h 文件，排除 build 目录
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(find "$PROJECT_ROOT" -type f \( -name "*.c" -o -name "*.h" \) \
             ! -path "*/build/*" \
             ! -path "*/.git/*" \
             ! -path "*/node_modules/*" \
             -print0)
    
    echo "${files[@]}"
}

# 格式化单个文件
format_file() {
    local file="$1"
    local dry_run="$2"
    
    if [[ "$dry_run" == "true" ]]; then
        # 干运行模式：检查文件是否需要格式化
        if ! clang-format --dry-run --Werror "$file" &>/dev/null; then
            echo -e "${YELLOW}需要格式化: $file${NC}"
            return 1
        else
            echo -e "${GREEN}已格式化: $file${NC}"
            return 0
        fi
    else
        # 实际格式化
        echo -e "${BLUE}格式化: $file${NC}"
        clang-format -i "$file"
        return 0
    fi
}

# 显示帮助信息
show_help() {
    echo "TestOS 代码格式化脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help     显示此帮助信息"
    echo "  -d, --dry-run  干运行模式，只检查不修改文件"
    echo "  -v, --verbose  详细输出"
    echo "  -f, --file     格式化指定文件"
    echo ""
    echo "示例:"
    echo "  $0                    # 格式化所有源文件"
    echo "  $0 --dry-run          # 检查哪些文件需要格式化"
    echo "  $0 --file src/main.c  # 格式化指定文件"
}

# 主函数
main() {
    local dry_run=false
    local verbose=false
    local specific_file=""
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -d|--dry-run)
                dry_run=true
                shift
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -f|--file)
                specific_file="$2"
                shift 2
                ;;
            *)
                echo -e "${RED}未知选项: $1${NC}"
                show_help
                exit 1
                ;;
        esac
    done
    
    echo -e "${BLUE}=== TestOS 代码格式化工具 ===${NC}"
    echo ""
    
    # 检查依赖
    check_clang_format
    check_config
    echo ""
    
    if [[ -n "$specific_file" ]]; then
        # 格式化指定文件
        if [[ ! -f "$specific_file" ]]; then
            echo -e "${RED}错误: 文件不存在: $specific_file${NC}"
            exit 1
        fi
        
        echo -e "${BLUE}格式化指定文件: $specific_file${NC}"
        format_file "$specific_file" "$dry_run"
    else
        # 格式化所有源文件
        echo -e "${BLUE}查找源文件...${NC}"
        local files=($(find_source_files))
        
        if [[ ${#files[@]} -eq 0 ]]; then
            echo -e "${YELLOW}未找到源文件${NC}"
            exit 0
        fi
        
        echo -e "${GREEN}找到 ${#files[@]} 个源文件${NC}"
        echo ""
        
        if [[ "$dry_run" == "true" ]]; then
            echo -e "${YELLOW}=== 干运行模式 - 检查需要格式化的文件 ===${NC}"
        else
            echo -e "${BLUE}=== 开始格式化 ===${NC}"
        fi
        
        local needs_formatting=0
        local total_files=${#files[@]}
        
        for file in "${files[@]}"; do
            if [[ "$verbose" == "true" ]] || [[ "$dry_run" == "true" ]]; then
                if ! format_file "$file" "$dry_run"; then
                    ((needs_formatting++))
                fi
            else
                format_file "$file" "$dry_run" > /dev/null
            fi
        done
        
        echo ""
        if [[ "$dry_run" == "true" ]]; then
            if [[ $needs_formatting -eq 0 ]]; then
                echo -e "${GREEN}✓ 所有文件都已正确格式化${NC}"
            else
                echo -e "${YELLOW}⚠ $needs_formatting/$total_files 个文件需要格式化${NC}"
                echo "运行 '$0' 来格式化这些文件"
            fi
        else
            echo -e "${GREEN}✓ 格式化完成! 处理了 $total_files 个文件${NC}"
        fi
    fi
}

# 运行主函数
main "$@"
