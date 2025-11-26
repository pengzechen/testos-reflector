#!/bin/bash

# =============================================
# 用户应用程序编译脚本
# 使用 musl 交叉编译工具链
# =============================================

set -e  # 遇到错误立即退出

# 工具链配置
CROSS_COMPILE="aarch64-linux-musl-"
CC="${CROSS_COMPILE}gcc"
STRIP="${CROSS_COMPILE}strip"

# 路径配置
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT="${SCRIPT_DIR}/.."
MUSL_LIB_DIR="${PROJECT_ROOT}/tools/musl-libs/lib"
MUSL_INCLUDE_DIR="${PROJECT_ROOT}/tools/musl-libs/include"

# 输出配置
OUTPUT_DIR="${SCRIPT_DIR}"
PROGRAM_NAME="hello"
SOURCE_FILE="${SCRIPT_DIR}/${PROGRAM_NAME}.c"
OUTPUT_ELF="${OUTPUT_DIR}/${PROGRAM_NAME}.elf"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }
step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# 检查工具链
check_toolchain() {
    step "检查交叉编译工具链..."
    
    if ! command -v ${CC} &> /dev/null; then
        error "找不到交叉编译器: ${CC}"
    fi
    
    info "工具链: $(${CC} --version | head -n1)"
}

# 检查源文件
check_source() {
    step "检查源文件..."
    
    if [ ! -f "${SOURCE_FILE}" ]; then
        error "源文件不存在: ${SOURCE_FILE}"
    fi
    
    info "源文件: ${SOURCE_FILE}"
}

# 编译程序
compile_program() {
    step "编译用户程序..."
    
    # 编译选项
    CFLAGS=(
        -O0                          # 优化级别
        -Wall                        # 所有警告
        -Wextra                      # 额外警告
        -nostdinc                    # 不使用标准系统头文件目录
        -I"${MUSL_INCLUDE_DIR}"      # musl 头文件
    )
    
    # 链接选项
    LDFLAGS=(
        -L"${MUSL_LIB_DIR}"          # musl 库路径
        -Wl,-dynamic-linker,/lib/ld-musl-aarch64.so.1  # 动态链接器
        -Wl,--export-dynamic         # 导出所有全局符号到动态符号表
    )
    
    info "编译选项: ${CFLAGS[*]}"
    info "链接选项: ${LDFLAGS[*]}"
    
    # 显示完整的编译命令
    echo ""
    echo -e "${CYAN}[CMD]${NC} 完整编译命令:"
    echo "${CC} ${CFLAGS[*]} ${LDFLAGS[*]} ${SOURCE_FILE} -o ${OUTPUT_ELF}"
    echo ""
    
    # 执行编译
    ${CC} "${CFLAGS[@]}" "${LDFLAGS[@]}" \
        "${SOURCE_FILE}" \
        -o "${OUTPUT_ELF}"
    
    if [ ! -f "${OUTPUT_ELF}" ]; then
        error "编译失败: 未生成输出文件"
    fi
    
    info "编译成功: ${OUTPUT_ELF}"
}

# 显示程序信息
show_info() {
    step "程序信息..."
    
    # 文件大小
    local size=$(du -h "${OUTPUT_ELF}" | cut -f1)
    info "文件大小: ${size}"
    
    # ELF 信息
    if command -v ${CROSS_COMPILE}readelf &> /dev/null; then
        echo ""
        echo "=== ELF Header ==="
        ${CROSS_COMPILE}readelf -h "${OUTPUT_ELF}" | grep -E "(Class|Machine|Entry)"
        
        echo ""
        echo "=== Program Headers ==="
        ${CROSS_COMPILE}readelf -l "${OUTPUT_ELF}" | grep -A1 "LOAD\|INTERP"
        
        echo ""
        echo "=== Dynamic Section ==="
        ${CROSS_COMPILE}readelf -d "${OUTPUT_ELF}" | grep "NEEDED"
    fi
}

# 可选：strip 减小文件大小
strip_program() {
    if [ "$1" == "--strip" ]; then
        step "Strip 程序符号..."
        
        local stripped="${OUTPUT_ELF%.elf}.stripped.elf"
        cp "${OUTPUT_ELF}" "${stripped}"
        ${STRIP} "${stripped}"
        
        local orig_size=$(du -h "${OUTPUT_ELF}" | cut -f1)
        local new_size=$(du -h "${stripped}" | cut -f1)
        
        info "原始大小: ${orig_size}"
        info "Strip 后: ${new_size}"
        info "Strip 版本: ${stripped}"
    fi
}

# 主函数
main() {
    echo "=========================================="
    echo "  用户程序编译工具"
    echo "=========================================="
    echo ""
    
    check_toolchain
    check_source
    compile_program
    show_info
    strip_program "$@"
    
    echo ""
    echo "=========================================="
    echo "  编译完成!"
    echo "=========================================="
    info "输出文件: ${OUTPUT_ELF}"
    info "下一步: 运行 'make flash-app' 烧录到开发板"
    echo ""
}

# 执行主函数
main "$@"
