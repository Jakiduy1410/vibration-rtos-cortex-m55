CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
OBJDUMP := arm-none-eabi-objdump
GDB     := gdb-multiarch
QEMU    := qemu-system-arm

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/firmware.elf

# Compiler & Architecture Flags (Cortex-M55 + MVE + FPU Hard-Float)
CPU_FLAGS := -mcpu=cortex-m55 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard
CFLAGS    := $(CPU_FLAGS) -O2 -g3 -Wall -Wextra -ffunction-sections -fdata-sections
CFLAGS    += -DARMCM55

# Include Directories
INCLUDES  := -Iinclude \
             -Isrc \
             -Ifreertos_kernel/include \
             -Ifreertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure \
             -Icmsis/CMSIS/Core/Include \
             -Icmsis/Device/ARM/ARMCM55/Include

# Linker Flags
LDFLAGS   := $(CPU_FLAGS) -T mps3_an547.ld \
             -Wl,--gc-sections \
             -Wl,-Map=$(BUILD_DIR)/firmware.map \
             --specs=nano.specs \
             --specs=nosys.specs \
             -lm

# Source files
SRCS := src/main.c \
        src/uart.c \
        src/startup_ARMCM55.c \
        src/system_ARMCM55.c \
        freertos_kernel/tasks.c \
        freertos_kernel/queue.c \
        freertos_kernel/list.c \
        freertos_kernel/timers.c \
        freertos_kernel/event_groups.c \
        freertos_kernel/portable/MemMang/heap_4.c \
        freertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure/port.c \
        freertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure/portasm.c

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET) size

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

size: $(TARGET)
	@echo "=== Firmware Size ==="
	$(SIZE) -A -B $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all size clean
