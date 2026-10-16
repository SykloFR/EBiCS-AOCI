# Compilation en ligne de commande d'EBiCS M820 (remplace le projet Eclipse)
# Usage : mingw32-make            -> cible d'origine EBiCS (GD32F303, Cortex-M4F), dossier build/
#         mingw32-make CORE=m3    -> cible AOCI (APM32F103RC = GD32F10x HD, Cortex-M3 sans FPU), dossier build-m3/
#         mingw32-make clean / mingw32-make CORE=m3 clean

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

CORE   ?= m4

ifeq ($(CORE),m3)
BUILD   = build-m3
CPU     = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFS    = -DGD32F10X_HD -DUSE_STDPERIPH_DRIVER -DARM_MATH_CM3
INCS    = -Iport_f10x -Iinc -IFirmware_F10x/CMSIS -IFirmware_F10x/CMSIS/GD/GD32F10x/Include \
          -IFirmware_F10x/GD32F10x_standard_peripheral/Include
LDSCRIPT = port_f10x/aoci_flash.ld
DSPLIB  =
LIBSRCS = $(wildcard Firmware_F10x/GD32F10x_standard_peripheral/Source/*.c) \
          $(wildcard Firmware_F10x/CMSIS/GD/GD32F10x/Source/*.c) \
          Firmware_F10x/DSP/arm_sin_cos_q31.c Firmware_F10x/DSP/arm_common_tables.c
ASRCS   = port_f10x/startup_gd32f10x_hd.S
else
BUILD   = build
CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    = -DGD32F30X_HD -DUSE_STDPERIPH_DRIVER -DGD_ECLIPSE_GCC
INCS    = -Iinc -IFirmware/CMSIS -IFirmware/CMSIS/GD/GD32F30x/Include \
          -IFirmware/GD32F30x_standard_peripheral/Include
LDSCRIPT = ldscripts/gd32f30x_flash.ld
DSPLIB  = -LFirmware/CMSIS -larm_cortexM4lf_math
LIBSRCS = $(wildcard Firmware/GD32F30x_standard_peripheral/Source/*.c) \
          $(wildcard Firmware/CMSIS/GD/GD32F30x/Source/*.c)
ASRCS   = gcc_startup/startup_gd32f30x_hd.S
endif

TARGET  = $(BUILD)/ebics

CFLAGS  = $(CPU) $(DEFS) $(INCS) -Os -g -std=gnu99 -Wall \
          -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -T$(LDSCRIPT) -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map --specs=nano.specs --specs=nosys.specs \
          $(DSPLIB) -lm

SRCS    = $(wildcard src/*.c) $(LIBSRCS)
OBJS    = $(SRCS:%.c=$(BUILD)/%.o) $(ASRCS:%.S=$(BUILD)/%.o)

all: $(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -x assembler-with-cpp -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
