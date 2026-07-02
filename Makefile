# 编译器和工具链设置
CROSS_COMPILE ?= aarch64-linux-musl-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# 项目名称
PROJECT_NAME = testos

SMP ?= 8

# 目录设置
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# 编译标志
CFLAGS = -g -O0 -Wall -nostdlib -nostartfiles -ffreestanding -mgeneral-regs-only
CFLAGS += -I$(INCLUDE_DIR) -I$(SRC_DIR) -D__LOAD_ADDR__=0x400000 -DT_SMP_NUM=$(SMP)
# CPU 对照版：make CPU_MATMUL=1 —— matmul 走纯 CPU 而非 NPU（逐 token 对照用）
ifeq ($(CPU_MATMUL),1)
CFLAGS += -DLLM_CPU_MATMUL
endif
ASFLAGS = -g -O0 -Wall -I$(INCLUDE_DIR) -D__LOAD_ADDR__=0x400000 -DT_SMP_NUM=$(SMP)

# 链接标志
LDFLAGS = -T src/boot/t_link.lds --defsym=__LOAD_ADDR__=0x400000

# 源文件
C_SOURCES = $(shell find $(SRC_DIR) -name "*.c")
ASM_SOURCES = $(shell find $(SRC_DIR) -name "*.S")

# 目标文件
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR)/%_asm.o,$(ASM_SOURCES))

# 确保 boot.S 编译的目标文件在最前面
BOOT_OBJECT = $(BUILD_DIR)/boot/t_boot_asm.o
OTHER_OBJECTS = $(filter-out $(BOOT_OBJECT),$(C_OBJECTS) $(ASM_OBJECTS))
OBJECTS = $(BOOT_OBJECT) $(OTHER_OBJECTS)

# 目标文件
ELF_TARGET = $(BUILD_DIR)/$(PROJECT_NAME).elf
BIN_TARGET = $(BUILD_DIR)/$(PROJECT_NAME).bin

# 默认目标
all: $(BIN_TARGET)

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译C源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# YOLO 与 NPU matmul 是纯整数计算密集型代码，用 -O2 编译（无 MMIO/volatile 依赖）。
# 其余内核保持 -O0 以便调试。
$(BUILD_DIR)/yolo/%.o: $(SRC_DIR)/yolo/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O0,$(CFLAGS)) -O2 -c $< -o $@

$(BUILD_DIR)/npulib/npu_matmul.o: $(SRC_DIR)/npulib/npu_matmul.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O0,$(CFLAGS)) -O2 -c $< -o $@

# 编译汇编源文件
$(BUILD_DIR)/%_asm.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

# 链接生成ELF文件
$(ELF_TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

# 生成二进制文件
$(BIN_TARGET): $(ELF_TARGET)
	$(OBJCOPY) -O binary $< $@

# 反汇编
disasm: $(ELF_TARGET)
	$(OBJDUMP) -x -d -S $< > $(BUILD_DIR)/$(PROJECT_NAME).disasm

# 运行QEMU
qemu: $(BIN_TARGET)
	qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a53 -kernel $< -nographic

# 调试模式运行QEMU
qemu-debug: $(BIN_TARGET)
	qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a53 -kernel $< -nographic -s -S

uimg: $(BIN_TARGET)
	mkimage -A arm64 -O linux -T kernel -C none -a 0x400000 -e 0x400000 -n "testos Kernel" -d build/testos.bin testos-reflector-src_aarch64-opi5p.uimg 

uboot: uimg
	cp testos-reflector-src_aarch64-opi5p.uimg /data/docker/tftpboot/data/kernel.uimg
	cp tools/orangepi5/rk3588-orangepi-5-plus.dtb /data/docker/tftpboot/data/rk3588-orangepi-5-plus.dtb
	echo "uboot done"

# Deploy for the YOLO boot script (boot_yolo.cmd tftps kernel.uimg + yolov5n.ydev).
# Mirrors the layout the board expects: <tftproot>/testos-reflector/...
uboot-yolo: uimg
	mkdir -p /data/docker/tftpboot/data/testos-reflector/tools/yolo/weights
	mkdir -p /data/docker/tftpboot/data/testos-reflector/tools/orangepi5
	cp testos-reflector-src_aarch64-opi5p.uimg /data/docker/tftpboot/data/testos-reflector/kernel.uimg
	cp tools/yolo/weights/yolov5n.ydev /data/docker/tftpboot/data/testos-reflector/tools/yolo/weights/yolov5n.ydev
	cp tools/orangepi5/rk3588-orangepi-5-plus.dtb /data/docker/tftpboot/data/testos-reflector/tools/orangepi5/rk3588-orangepi-5-plus.dtb
	echo "uboot-yolo done"

# 清理
clean:
	rm -rf $(BUILD_DIR)

# 显示帮助
help:
	@echo "Available targets:"
	@echo "  all       - Build the kernel binary"
	@echo "  clean     - Remove build files"
	@echo "  disasm    - Generate disassembly"
	@echo "  qemu      - Run kernel in QEMU"
	@echo "  qemu-debug- Run kernel in QEMU with GDB server"
	@echo "  help      - Show this help message"

.PHONY: all clean disasm qemu qemu-debug help