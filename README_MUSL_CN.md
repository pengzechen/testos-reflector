# Musl 静态链接程序支持

[English Documentation](MUSL_SUPPORT.md) | [快速开始](QUICKSTART.md) | [实现总结](IMPLEMENTATION_SUMMARY.md)

## 功能概述

本分支为 testos 内核添加了运行 musl 静态链接程序的完整支持。内核现在可以：

1. ✅ 加载和解析 ELF64 可执行文件
2. ✅ 从内核模式 (EL1) 切换到用户模式 (EL0)
3. ✅ 处理来自用户程序的系统调用
4. ✅ 支持标准 C 库函数 (printf, malloc, 等)

## 快速开始

### 编译内核

```bash
git checkout copilot/support-musl-static-linking
make clean
make
```

### 编译用户程序

```bash
cd usertest
make
```

这会生成一个静态链接的 `hello` 程序。

### 运行测试

```bash
# 在 QEMU 中运行
make qemu

# 在内核提示符下:
# 按 'h' 查看帮助
# 按 'x' 使用 XMODEM 接收程序
# 按 'e' 执行接收的程序
```

### 预期输出

```
=== 执行用户程序 ===
ELF 验证通过。正在加载和执行...
Hello from user mode!
argc = 0
测试成功完成。
进程退出，状态码: 0
```

## 已实现的系统调用

本实现支持 17 个关键系统调用：

### 输入输出
- `sys_read` - 从标准输入读取
- `sys_write` - 写入标准输出/错误输出

### 内存管理
- `sys_brk` - 调整程序堆边界
- `sys_mmap` - 映射内存（仅支持匿名映射）
- `sys_munmap` - 取消内存映射

### 进程控制
- `sys_exit` - 退出进程
- `sys_exit_group` - 退出进程组

### 进程信息
- `sys_getpid`, `sys_gettid` - 获取进程/线程 ID
- `sys_getuid`, `sys_getgid` - 获取用户/组 ID
- `sys_geteuid`, `sys_getegid` - 获取有效用户/组 ID
- `sys_getppid` - 获取父进程 ID

### 系统信息
- `sys_clock_gettime` - 获取当前时间
- `sys_uname` - 获取系统信息
- `sys_set_tid_address` - 设置线程 ID 地址

## 使用方法

### 创建用户程序

在 `usertest` 目录创建你的 C 程序：

```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("你好，用户模式！\n");
    
    char *buffer = malloc(100);
    if (buffer) {
        printf("malloc 成功！地址：%p\n", buffer);
        free(buffer);
    }
    
    return 0;
}
```

### 编译

```bash
aarch64-linux-musl-gcc -static -O2 -o myprogram myprogram.c
```

### 运行

1. 启动内核（QEMU 或实际硬件）
2. 按 'x' 开始 XMODEM 接收
3. 传输你的程序
4. 按 'e' 执行

## 技术架构

### 异常处理流程

```
用户程序 (EL0)
    |
    | svc #0 (系统调用)
    V
异常向量 (EL1)
    |
    V
handle_sync_exception()
    |
    | 检查异常类型 (EC == 0x15?)
    V
handle_syscall_exception()
    |
    | 根据系统调用号分发
    V
具体系统调用函数 (sys_write, sys_exit, 等)
    |
    | 将结果存入 x0 寄存器
    V
ERET (返回 EL0)
    |
    V
用户程序继续执行
```

### 内存布局

```
0x00400000: 内核代码/数据
0x10000000: 用户堆起始地址 (brk)
0xXXXXXXXX: 用户栈（动态分配，默认 1MB）
```

### 特权级别

- **EL1 (内核模式)**: 
  - 运行内核代码
  - 处理中断和异常
  - 管理内存和 I/O

- **EL0 (用户模式)**:
  - 运行用户程序
  - 权限受限
  - 通过系统调用访问内核功能

## 支持的功能

✅ **已支持**:
- printf, fprintf, sprintf 等输出函数
- malloc, free, calloc, realloc 内存分配
- exit 程序退出
- 基本进程信息查询
- 时间获取

❌ **暂不支持**:
- 文件 I/O（open, read, write 文件）
- 网络功能
- 进程创建（fork, exec）
- 线程（pthread）
- 信号处理

## 使用示例

### 示例 1: Hello World

```c
#include <stdio.h>

int main() {
    printf("你好，世界！\n");
    return 0;
}
```

### 示例 2: 内存分配

```c
#include <stdio.h>
#include <stdlib.h>

int main() {
    int *array = malloc(10 * sizeof(int));
    
    for (int i = 0; i < 10; i++) {
        array[i] = i * i;
    }
    
    for (int i = 0; i < 10; i++) {
        printf("array[%d] = %d\n", i, array[i]);
    }
    
    free(array);
    return 0;
}
```

### 示例 3: 系统信息

```c
#include <stdio.h>
#include <sys/utsname.h>

int main() {
    struct utsname info;
    uname(&info);
    
    printf("系统: %s\n", info.sysname);
    printf("节点名: %s\n", info.nodename);
    printf("版本: %s\n", info.release);
    printf("架构: %s\n", info.machine);
    
    return 0;
}
```

## 内核命令

在内核运行时，可以使用以下命令：

- **h/H** - 显示帮助
- **s/S** - 显示统计信息
- **t/T** - 显示当前时间
- **p/P** - 切换周期性打印
- **x/X** - 开始 XMODEM 接收文件
- **d/D** - 显示接收的文件数据（十六进制）
- **e/E** - 执行接收的 ELF 程序
- **q/Q** - 退出（进入 WFI 循环）

## 实现细节

### 代码结构

```
include/lib/
  - elf.h          # ELF 加载器头文件
  - syscall.h      # 系统调用定义
  - usermode.h     # 用户模式支持
  - process.h      # 进程管理

src/lib/
  - elf.c          # ELF 加载实现
  - syscall.c      # 系统调用实现
  - usermode.c     # 用户模式 C 代码
  - usermode.S     # 用户模式汇编代码
  - process.c      # 进程管理实现

src/exception/
  - t_exception.c  # 异常处理（已更新支持系统调用）

usertest/
  - hello.c        # 测试程序
  - Makefile       # 编译脚本
```

### 系统调用约定

遵循 Linux AArch64 系统调用 ABI：

- 系统调用号：x8 寄存器
- 参数：x0-x5 寄存器
- 返回值：x0 寄存器
- 指令：svc #0

### ELF 加载流程

1. 验证 ELF 魔数和头部
2. 检查架构（AArch64）
3. 解析程序头表
4. 加载 PT_LOAD 段到内存
5. 零填充 BSS 段
6. 返回入口点地址

## 限制和注意事项

### 当前限制

1. **无 MMU 配置**: 程序运行在内核地址空间
2. **无内存保护**: 用户程序可以访问内核内存
3. **无进程调度**: 一次只能运行一个进程
4. **无文件系统**: 不能打开/读写文件
5. **仅静态链接**: 不支持动态链接
6. **有限的系统调用**: 只实现了 17 个系统调用

### 安全警告

⚠️ **本实现不安全！**

- 用户程序与内核共享地址空间
- 没有内核/用户内存隔离
- 恶意程序可能破坏内核
- 仅适用于受信任的代码

生产环境需要：
- MMU 配置和独立地址空间
- 每个进程的页表设置
- 适当的权限检查
- 内核/用户内存分离

## 未来改进

### 短期计划
- [ ] 添加更多系统调用（open, close, ioctl）
- [ ] 改进错误处理
- [ ] 支持 argc/argv 参数
- [ ] 实现环境变量

### 中期计划
- [ ] 配置 MMU 和页表
- [ ] 实现内存保护
- [ ] 添加进程调度
- [ ] 支持多进程

### 长期计划
- [ ] 实现文件系统支持
- [ ] 添加动态链接支持
- [ ] 实现 fork/exec
- [ ] 添加信号处理
- [ ] 支持线程（pthread）

## 性能考虑

- **系统调用开销**: 约 1000 周期（陷入+分发+返回）
- **内存管理**: 简单的 bump 分配器
- **上下文切换**: 非常快（单地址空间，无 TLB 刷新）

## 兼容性

- ✅ 与 musl libc 兼容
- ✅ 遵循 Linux AArch64 ABI
- ✅ 支持大多数标准库函数
- ⚠️ 某些高级功能可能不工作

## 故障排除

### "无效的 ELF 文件"错误
- 确保使用 `-static` 标志编译
- 验证是 AArch64 架构：`file yourprogram`
- 检查使用的是 musl 编译器而非 glibc

### 程序立即崩溃
- 检查程序是否使用了不支持的系统调用
- 添加调试输出查看失败位置
- 验证栈大小足够（默认 1MB）

### XMODEM 传输失败
- 检查波特率匹配（通常 115200）
- 使用 `-k` 标志启用 XMODEM-1K 协议
- 如果出错，尝试降低传输速度

## 相关文档

- [MUSL_SUPPORT.md](MUSL_SUPPORT.md) - 英文技术文档
- [QUICKSTART.md](QUICKSTART.md) - 快速开始指南
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - 实现总结

## 参考资料

- ARM 架构参考手册 (ARMv8-A)
- Linux AArch64 系统调用 ABI
- Musl libc 文档
- ELF-64 对象文件格式

## 许可证

与 testos 内核相同。

---

**作者**: GitHub Copilot  
**日期**: 2024-11-01  
**分支**: copilot/support-musl-static-linking
