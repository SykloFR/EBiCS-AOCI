# Compilation en ligne de commande d'EBiCS M820 (remplace le projet Eclipse)
# Étape 1 du portage AOCI : compilation de référence, cible d'origine GD32F303 (Cortex-M4F).
# Usage : make            -> build/ebics.elf, .bin, .map
#         make clean

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

BUILD   = build
TARGET  = $(BUILD)/ebics

CPU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    = -DGD32F30X_HD -DUSE_STDPERIPH_DRIVER -DGD_ECLIPSE_GCC
INCS    = -Iinc -IFirmware/CMSIS -IFirmware/CMSIS/GD/GD32F30x/Include \
          -IFirmware/GD32F30x_standard_peripheral/Include

CFLAGS  = $(CPU) $(DEFS) $(INCS) -Os -g -std=gnu99 -Wall \
          -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -Tldscripts/gd32f30x_flash.ld -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map --specs=nano.specs --specs=nosys.specs \
          -LFirmware/CMSIS -larm_cortexM4lf_math -lm

SRCS    = $(wildcard src/*.c) \
          $(wildcard Firmware/GD32F30x_standard_peripheral/Source/*.c) \
          $(wildcard Firmware/CMSIS/GD/GD32F30x/Source/*.c)
ASRCS   = gcc_startup/startup_gd32f30x_hd.S
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
