#!/bin/bash

# ==============================================
# 创建 sparse ext4 镜像脚本
# 适用于 Rockchip 平台
# Usage: ./make_boot.sh <starry_image> <output_name> 
# ==============================================

set -e  # 遇到错误立即退出

# 判断是否有两个参数
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <starry_image> <output_name>"
    exit 1
fi

# 配置参数
IMAGE_SIZE="100M"          # 镜像大小
MOUNT_POINT="/mnt/boot_img"  # 挂载点
OUTPUT_IMAGE="$2"  # 输出镜像文件名
KERNEL_SOURCE="$1"  # 源内核文件
TARGET_PATH="/kernel.uimg"   # 镜像中的目标路径
DTB_PATH="/rk3588-orangepi-5-plus.dtb" # 设备树文件路径
ORANGEPI5_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MUSL_LIB_DIR="${ORANGEPI5_DIR}/../musl-libs/lib"  # musl 库目录
USERAPP_DIR="${ORANGEPI5_DIR}/../../userapp"  # 用户程序目录

# 颜色输出定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# 检查依赖工具
check_dependencies() {
    local tools=("mkfs.ext4" "e2fsck" "resize2fs" "sudo")
    local missing=()
    
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            missing+=("$tool")
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        error "缺少必要的工具: ${missing[*]}"
    fi
    info "所有依赖工具检查通过"
}

# 清理函数
cleanup() {
    if mountpoint -q "$MOUNT_POINT"; then
        info "卸载镜像..."
        sudo umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    
    if [ -d "$MOUNT_POINT" ]; then
        sudo rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi
}

# 注册清理函数
trap cleanup EXIT INT TERM

# 检查源文件是否存在
check_source_file() {
    if [ ! -f "$KERNEL_SOURCE" ]; then
        error "请提供有效的内核文件路径"
    fi
    info "找到内核文件: $(du -h "$KERNEL_SOURCE" | cut -f1)"
}

# 创建 sparse 镜像
create_sparse_image() {
    info "创建 sparse ext4 镜像 (大小: $IMAGE_SIZE)..."
    
    dd if=/dev/zero of="$OUTPUT_IMAGE" bs=1 count=0 seek="$IMAGE_SIZE" status=none
    mkfs.ext4 -F "$OUTPUT_IMAGE" > /dev/null
    info "使用 dd+mkfs.ext4 创建镜像"
    
    if [ ! -f "$OUTPUT_IMAGE" ]; then
        error "创建镜像失败"
    fi
    info "镜像创建成功: $(du -h "$OUTPUT_IMAGE" | cut -f1)"
}

# 挂载并复制文件
mount_and_copy() {
    info "创建挂载点..."
    sudo mkdir -p "$MOUNT_POINT"
    
    info "挂载镜像..."
    sudo mount -o loop "$OUTPUT_IMAGE" "$MOUNT_POINT"

    info "制作 boot.src 文件..."
    mkimage -A arm -T script -C none -n "TF boot" -d "${ORANGEPI5_DIR}/boot.cmd" boot.scr
    
    info "复制 boot 文件到镜像中..."
    sudo cp boot.scr "${MOUNT_POINT}"
    
    info "复制 kernel 文件到镜像中..."
    sudo cp "$KERNEL_SOURCE" "${MOUNT_POINT}${TARGET_PATH}"

    info "复制 dtb 文件到镜像中..."
    sudo cp "${ORANGEPI5_DIR}/$DTB_PATH" "${MOUNT_POINT}/rk3588-orangepi-5-plus.dtb"

    # 复制 musl 库文件
    info "复制 musl 库文件到镜像中..."
    if [ -d "$MUSL_LIB_DIR" ]; then
        info "从 ${MUSL_LIB_DIR} 复制库文件..."
        
        # 定义需要复制的库文件列表（使用 -P 保留符号链接）
        local libs=(
            "libc.so"
            "libm.so"
            "libpthread.so"
            "libdl.so"
            "librt.so"
            "ld-musl-aarch64.so.1"
            "libstdc++.so.6"
            "libgcc_s.so.1"
        )
        
        for lib in "${libs[@]}"; do
            if [ -e "${MUSL_LIB_DIR}/${lib}" ]; then
                # 使用 -P 保留符号链接，-d 不解引用
                sudo cp -P "${MUSL_LIB_DIR}/${lib}" "${MOUNT_POINT}/"
                if [ -L "${MUSL_LIB_DIR}/${lib}" ]; then
                    info "✓ ${lib} 已复制 (符号链接)"
                else
                    info "✓ ${lib} 已复制"
                fi
            else
                warn "跳过 ${lib} (不存在)"
            fi
        done
    else
        warn "未找到 musl 库目录: ${MUSL_LIB_DIR}"
        warn "跳过库文件复制"
    fi

    # 复制用户程序
    info "复制用户程序到镜像中..."
    if [ -d "$USERAPP_DIR" ]; then
        # 查找所有 .elf 文件
        local elf_files=$(find "$USERAPP_DIR" -maxdepth 1 -name "*.elf" -type f)
        
        if [ -n "$elf_files" ]; then
            for elf_file in $elf_files; do
                local filename=$(basename "$elf_file")
                sudo cp "$elf_file" "${MOUNT_POINT}/"
                info "✓ ${filename} 已复制"
            done
        else
            warn "未找到用户程序 (*.elf)"
            info "提示: 运行 'cd userapp && ./build.sh' 编译用户程序"
        fi
    else
        warn "未找到用户程序目录: ${USERAPP_DIR}"
    fi

    sudo ls -al "${MOUNT_POINT}"
    
    info "卸载镜像..."
    sudo umount "$MOUNT_POINT"
    sudo rmdir "$MOUNT_POINT"
    rm boot.scr
}

# 主执行流程
main() {
    echo "=========================================="
    echo "    Sparase Ext4 镜像创建与刷写工具"
    echo "=========================================="
    
    check_dependencies
    check_source_file
    
    # 清理之前的文件
    cleanup
    
    create_sparse_image
    mount_and_copy
    
    echo "=========================================="
    echo "镜像准备完成: $OUTPUT_IMAGE"
    echo "包含文件: "
    echo "  - kernel.uimg (内核)"
    echo "  - rk3588-orangepi-5-plus.dtb (设备树)"
    echo "  - boot.scr (U-Boot 脚本)"
    echo "  - libc.so (musl C 库)"
    echo "  - libm.so, libpthread.so, libdl.so, librt.so (musl 子库)"
    echo "  - ld-musl-aarch64.so.1 (动态链接器)"
    echo "  - libstdc++.so.6, libgcc_s.so.1 (C++ 支持)"
    echo "=========================================="
    
    info "镜像已保存为: $OUTPUT_IMAGE"
    info "您可以使用以下命令手动刷写:"
    info "sudo rkdeveloptool wl $FLASH_OFFSET $OUTPUT_IMAGE"
    info "sudo rkdeveloptool rd"
}

# 执行主函数
main "$@"