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
CFLAGS += -I$(INCLUDE_DIR) -D__LOAD_ADDR__=0x400000 -DT_SMP_NUM=$(SMP)
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
	cp testos-reflector_aarch64-opi5p.uimg /data/docker/tftpboot/data/kernel.uimg
	cp tools/orangepi5/rk3588-orangepi-5-plus.dtb /data/docker/tftpboot/data/rk3588-orangepi-5-plus.dtb
	echo "uboot done"

# 清理
clean:
	rm -rf $(BUILD_DIR)

# 编译用户程序
build-app:
	@echo "编译用户程序..."
	cd userapp && ./build.sh

# 烧录（包含用户程序）
flash: $(BIN_TARGET)
	@echo "烧录内核和用户程序到开发板..."
	bash tools/orangepi5/make_flash.sh

reboot:
	@echo "重启开发板..."
	curl http://192.168.1.24:8080/off && sleep 0.3 && curl http://192.168.1.24:8080/on

# 显示帮助
help:
	@echo "Available targets:"
	@echo "  all        - Build the kernel binary"
	@echo "  clean      - Remove build files"
	@echo "  disasm     - Generate disassembly"
	@echo "  qemu       - Run kernel in QEMU"
	@echo "  qemu-debug - Run kernel in QEMU with GDB server"
	@echo "  uimg       - Generate U-Boot image"
	@echo "  build-app  - Build user application"
	@echo "  flash      - Flash kernel and apps to board"
	@echo "  help       - Show this help message"

.PHONY: all clean disasm qemu qemu-debug help build-app flash