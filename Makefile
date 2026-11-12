CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
OBJDUMP := arm-none-eabi-objdump
GDB     := gdb-multiarch
QEMU    := qemu-system-arm

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/firmware.elf

# Compiler & Architecture Flags (Cortex-M55 + Helium MVE + FPU Hard-Float)
CPU_FLAGS := -mcpu=cortex-m55 -mthumb -mfloat-abi=hard
CFLAGS    := $(CPU_FLAGS) -O2 -g3 -Wall -Wextra -ffunction-sections -fdata-sections -flax-vector-conversions
CFLAGS    += -DARMCM55

# Include Directories
INCLUDES  := -Iinclude \
             -Isrc \
             -Ifreertos_kernel/include \
             -Ifreertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure \
             -Icmsis/CMSIS/Core/Include \
             -Icmsis/Device/ARM/ARMCM55/Include \
             -Icmsis/CMSIS/DSP/Include \
             -Icmsis/CMSIS/DSP/PrivateInclude

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
        freertos_kernel/portable/MemMang/heap_5.c \
        freertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure/port.c \
        freertos_kernel/portable/GCC/ARM_CM55_NTZ/non_secure/portasm.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c \
        cmsis/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c \
        cmsis/CMSIS/DSP/Source/CommonTables/arm_common_tables.c \
        cmsis/CMSIS/DSP/Source/CommonTables/arm_const_structs.c \
        cmsis/CMSIS/DSP/Source/CommonTables/arm_mve_tables.c \
        cmsis/CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c \
        cmsis/CMSIS/DSP/Source/StatisticsFunctions/arm_rms_f32.c \
        cmsis/CMSIS/DSP/Source/StatisticsFunctions/arm_power_f32.c \
        cmsis/CMSIS/DSP/Source/StatisticsFunctions/arm_max_f32.c \
        cmsis/CMSIS/DSP/Source/FastMathFunctions/arm_sin_f32.c

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET) size

$(BUILD_DIR)/%.o: %.c
	@python -c "import os, sys; os.makedirs(os.path.dirname(sys.argv[1]), exist_ok=True)" "$@"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	@python -c "import os, sys; os.makedirs(sys.argv[1], exist_ok=True)" "$(BUILD_DIR)"
	$(CC) $(OBJS) $(LDFLAGS) -o $@

size: $(TARGET)
	@echo "=== Firmware Size ==="
	$(SIZE) -A -B $(TARGET)

run: $(TARGET)
	$(QEMU) -machine mps3-an547 -cpu cortex-m55 -semihosting -kernel $(TARGET) -nographic -serial stdio

clean:
	python -c "import shutil; shutil.rmtree('$(BUILD_DIR)', ignore_errors=True)"

.PHONY: all size run clean
