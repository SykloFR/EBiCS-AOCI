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
DEFS    = -DGD32F10X_HD -DUSE_STDPERIPH_DRIVER -DARM_MATH_CM3 -DHXTAL_VALUE=12000000U
INCS    = -Iport_f10x -Iinc -IFirmware_F10x/CMSIS -IFirmware_F10x/CMSIS/GD/GD32F10x/Include \
          -IFirmware_F10x/GD32F10x_standard_peripheral/Include
LDSCRIPT = port_f10x/aoci_flash.ld
DSPLIB  =
LIBSRCS = $(wildcard Firmware_F10x/GD32F10x_standard_peripheral/Source/*.c) \
          Firmware_F10x/DSP/arm_sin_cos_q31.c Firmware_F10x/DSP/arm_common_tables.c \
          port_f10x/system_aoci.c
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

# Brouillon pour la revue puissance (voir revue-puissance/README.md) : mingw32-make CORE=m3 PROP=1 aoci
# -> dossier build-m3-prop, image nommee ..._REVUE_NE_PAS_FLASHER.bin
ifeq ($(PROP),1)
SRCDIR   = revue-puissance/proposed/src
INCS    := -Irevue-puissance/proposed/inc $(INCS)
BUILD   := $(BUILD)-prop
AOCI_OUT = $(BUILD)/ebics_REVUE_NE_PAS_FLASHER.bin
else
SRCDIR   = src
AOCI_OUT = $(BUILD)/ebics_aoci_full.bin
endif

# Build de banc porte 4 (voir inc/config.h, bloc BENCH_BUILD) : mingw32-make CORE=m3 BENCH=1 CURRENT_SIGN=-1 aoci
# -> dossier build-m3-banc, image ..._BANC_aoci_full.bin, fichiers .su (usage de pile par fonction)
ifeq ($(BENCH),1)
ifndef CURRENT_SIGN
$(error BENCH=1 : passer CURRENT_SIGN=-1 ou CURRENT_SIGN=1 explicitement)
endif
DEFS    += -DBENCH_BUILD -DCURRENT_SIGN=$(CURRENT_SIGN)
BUILD   := $(BUILD)-banc
AOCI_OUT = $(BUILD)/ebics_BANC_aoci_full.bin
STACKFLAGS = -fstack-usage
endif

ifeq ($(OS),Windows_NT)
MKDIR_P = @if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
RM_RF   = if exist $(subst /,\,$(BUILD)) rmdir /s /q $(subst /,\,$(BUILD))
else
MKDIR_P = @mkdir -p $(1)
RM_RF   = rm -rf $(BUILD)
endif

TARGET  = $(BUILD)/ebics

CFLAGS  = $(CPU) $(DEFS) $(INCS) -Os -g -std=gnu99 -Wall \
          -ffunction-sections -fdata-sections -MMD -MP $(STACKFLAGS)
LDFLAGS = $(CPU) -T$(LDSCRIPT) -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map --specs=nano.specs --specs=nosys.specs \
          $(DSPLIB) -lm

SRCS    = $(wildcard $(SRCDIR)/*.c) $(LIBSRCS)
OBJS    = $(SRCS:%.c=$(BUILD)/%.o) $(ASRCS:%.S=$(BUILD)/%.o)

# dependances des en-tetes (sans cela, une modification de config.h ou main.h ne recompile rien)
-include $(OBJS:.o=.d)

all: $(TARGET).bin

$(BUILD)/%.o: %.c
	$(call MKDIR_P,$(dir $@))
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	$(call MKDIR_P,$(dir $@))
	$(CC) $(CPU) -x assembler-with-cpp -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Image complete pour le bootloader AOCI (CRC32 a 0x0803AFC0) : mingw32-make CORE=m3 aoci
aoci: $(TARGET).bin
	python tools/make_aoci_image.py $(TARGET).bin $(AOCI_OUT)

clean:
	$(RM_RF)

.PHONY: all aoci clean
