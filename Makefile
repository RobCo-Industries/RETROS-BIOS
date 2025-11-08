# Makefile for RETROS-BIOS
# Supports RPi0, RPi1 (BCM2835), RPi2 (BCM2836), RPi3 (BCM2837)

# Default target
TARGET ?= BCM2835

# Toolchain
PREFIX ?= arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Architecture flags
ifeq ($(TARGET),BCM2835)
    # RPi0/1: ARM1176JZF-S (ARMv6)
    ARCH_FLAGS = -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
    DEFINES = -DBCM2835
else ifeq ($(TARGET),BCM2836)
    # RPi2: Cortex-A7 (ARMv7-A)
    ARCH_FLAGS = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
    DEFINES = -DBCM2836
else ifeq ($(TARGET),BCM2837)
    # RPi3: Cortex-A53 in 32-bit mode (ARMv7-A compatible)
    ARCH_FLAGS = -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
    DEFINES = -DBCM2837
endif

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -O2 -nostdlib -nostartfiles -ffreestanding
CFLAGS += $(ARCH_FLAGS) $(DEFINES)
CFLAGS += -I$(INC_DIR)

# Assembler flags
ASFLAGS = $(ARCH_FLAGS)

# Linker flags
LDFLAGS = -T linker.ld -nostdlib
LIBGCC = $(shell $(CC) $(ARCH_FLAGS) -print-libgcc-file-name)

# Source files
C_SOURCES = $(wildcard $(SRC_DIR)/*.c)
ASM_SOURCES = $(wildcard $(SRC_DIR)/*.S)

# Object files
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output files
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_IMG = $(BUILD_DIR)/kernel.img
KERNEL_LST = $(BUILD_DIR)/kernel.list

.PHONY: all clean bcm2835 bcm2836 bcm2837

all: $(KERNEL_IMG)

# Build for specific targets
bcm2835:
	$(MAKE) TARGET=BCM2835

bcm2836:
	$(MAKE) TARGET=BCM2836

bcm2837:
	$(MAKE) TARGET=BCM2837

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile C files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble assembly files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

# Link
$(KERNEL_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) $(LIBGCC) -o $@

# Create binary image
$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) $< -O binary $@
	$(OBJDUMP) -D $(KERNEL_ELF) > $(KERNEL_LST)
	@echo "====================================="
	@echo "Build complete: $(KERNEL_IMG)"
	@echo "Target: $(TARGET)"
	@echo "Size: $$(stat -f%z $@ 2>/dev/null || stat -c%s $@) bytes"
	@echo "====================================="

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Help
help:
	@echo "RETROS-BIOS Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all (default) - Build for BCM2835 (RPi0/1)"
	@echo "  bcm2835      - Build for BCM2835 (RPi0/1)"
	@echo "  bcm2836      - Build for BCM2836 (RPi2)"
	@echo "  bcm2837      - Build for BCM2837 (RPi3)"
	@echo "  clean        - Remove build artifacts"
	@echo "  help         - Show this help"
	@echo ""
	@echo "The output file is: $(KERNEL_IMG)"
	@echo "Copy this to kernel.img on your SD card."
