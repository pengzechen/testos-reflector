#!/bin/bash

# 快速代码格式化脚本
# 简化版本，用于日常开发

# 检查 clang-format 是否存在
if ! command -v clang-format &> /dev/null; then
    echo "错误: clang-format 未安装"
    exit 1
fi

# 如果提供了文件参数，只格式化指定文件
if [[ $# -gt 0 ]]; then
    for file in "$@"; do
        if [[ -f "$file" && ("$file" == *.c || "$file" == *.h) ]]; then
            echo "格式化: $file"
            clang-format -i "$file"
        else
            echo "跳过: $file (不是 C/H 文件或文件不存在)"
        fi
    done
else
    # 格式化所有 C/H 文件
    echo "格式化所有 C/H 文件..."
    # 切换到项目根目录
    cd "$(dirname "$0")/.."
    find . -type f \( -name "*.c" -o -name "*.h" \) \
        ! -path "./build/*" \
        ! -path "./.git/*" \
        -exec clang-format -i {} \;
    echo "完成!"
fi
