TARGET = TX_Beacon
BUILD  = build

CUBE  = C:/Users/prymv/STM32Cube/Repository/STM32Cube_FW_WB_V1.23.0
HAL   = $(CUBE)/Drivers/STM32WBxx_HAL_Driver
CMSIS = $(CUBE)/Drivers/CMSIS
WPAN  = $(CUBE)/Middlewares/ST/STM32_WPAN

TOOLBIN  = C:/ST/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin
CC       = $(TOOLBIN)/arm-none-eabi-gcc
CP       = $(TOOLBIN)/arm-none-eabi-objcopy
SZ       = $(TOOLBIN)/arm-none-eabi-size

# Compiler base flags (CPU + arch)
CPU_FLAGS  = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# Application code: strict warnings
APP_CFLAGS  = $(CPU_FLAGS)
APP_CFLAGS += -Os -g3 -std=gnu11
APP_CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
APP_CFLAGS += -DSTM32WB1Mxx -DUSE_HAL_DRIVER
APP_CFLAGS += -ffunction-sections -fdata-sections
APP_CFLAGS += --specs=nano.specs

# HAL / CMSIS: compile silently (ST code has many -Wextra warnings by design)
HAL_CFLAGS  = $(CPU_FLAGS)
HAL_CFLAGS += -Os -g3 -std=gnu11 -w
HAL_CFLAGS += -DSTM32WB1Mxx -DUSE_HAL_DRIVER
HAL_CFLAGS += -ffunction-sections -fdata-sections
HAL_CFLAGS += --specs=nano.specs

INC  = -ICore/Inc
INC += -I$(HAL)/Inc
INC += -I$(HAL)/Inc/Legacy
INC += -I$(CMSIS)/Device/ST/STM32WBxx/Include
INC += -I$(CMSIS)/Include

# BLE middleware include paths
BLE_INC  = -I$(WPAN)
BLE_INC += -I$(WPAN)/ble
BLE_INC += -I$(WPAN)/interface/patterns/ble_thread
BLE_INC += -I$(WPAN)/interface/patterns/ble_thread/tl
BLE_INC += -I$(WPAN)/interface/patterns/ble_thread/shci
BLE_INC += -I$(WPAN)/ble/core
BLE_INC += -I$(WPAN)/ble/core/auto
BLE_INC += -I$(WPAN)/ble/core/template
BLE_INC += -I$(WPAN)/ble/svc/Inc
BLE_INC += -I$(WPAN)/ble/svc/Src
BLE_INC += -I$(WPAN)/utilities
BLE_INC += -I$(CUBE)/Utilities/sequencer
BLE_INC += -ISTM32_WPAN/App
BLE_INC += -ISTM32_WPAN/Target

LDFLAGS  = $(CPU_FLAGS)
LDFLAGS += -TSTM32WB1MMCHX_FLASH.ld
LDFLAGS += --specs=nosys.specs --specs=nano.specs
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--no-warn-rwx-segments
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map
LDFLAGS += -Wl,--start-group -lc -lm -Wl,--end-group

# ── Application sources ────────────────────────────────────────────────────
APP_SRCS  = Core/Src/main.c
APP_SRCS += Core/Src/stm32wbxx_it.c
APP_SRCS += Core/Src/drv_rf_tx.c
APP_SRCS += Core/Src/drv_led.c
APP_SRCS += Core/Src/drv_uart.c
APP_SRCS += Core/Src/drv_gekon.c
APP_SRCS += Core/Src/svc_power.c
APP_SRCS += Core/Src/svc_uart_cmd.c
APP_SRCS += Core/Src/flash_config.c
APP_SRCS += Core/Src/flash_test.c
APP_SRCS += Core/Src/hw_desc.c
APP_SRCS += Core/Src/drv_chip_temp.c
APP_SRCS += Core/Src/drv_batt_adc.c
APP_SRCS += Core/Src/drv_light_adc.c
APP_SRCS += Core/Src/flash_log.c
APP_SRCS += Core/Src/proto_uart.c
APP_SRCS += Core/Src/lis2dw12.c
APP_SRCS += Core/Src/svc_wake.c
APP_SRCS += Core/Src/cmd_layer.c
APP_SRCS += Core/Src/drv_ota.c
APP_SRCS += Core/Src/syscalls.c

# ── HAL driver sources ─────────────────────────────────────────────────────
HAL_SRCS  = $(HAL)/Src/stm32wbxx_hal.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_cortex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_flash.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_flash_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_gpio.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_pwr.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_pwr_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_rcc.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_rcc_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_rtc.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_rtc_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_adc.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_adc_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_dma.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_hsem.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_i2c.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_i2c_ex.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_iwdg.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_uart.c
HAL_SRCS += $(HAL)/Src/stm32wbxx_hal_uart_ex.c
HAL_SRCS += $(CMSIS)/Device/ST/STM32WBxx/Source/Templates/system_stm32wbxx.c

ASM_SRCS = $(CMSIS)/Device/ST/STM32WBxx/Source/Templates/gcc/startup_stm32wb1mxx_cm4.s

# ── BLE middleware sources (from CUBE, compile with suppressed warnings) ───
BLE_SRCS  = $(WPAN)/interface/patterns/ble_thread/tl/tl_mbox.c
BLE_SRCS += $(WPAN)/interface/patterns/ble_thread/tl/hci_tl.c
BLE_SRCS += $(WPAN)/interface/patterns/ble_thread/tl/hci_tl_if.c
BLE_SRCS += $(WPAN)/interface/patterns/ble_thread/tl/shci_tl.c
BLE_SRCS += $(WPAN)/interface/patterns/ble_thread/tl/shci_tl_if.c
BLE_SRCS += $(WPAN)/interface/patterns/ble_thread/shci/shci.c
BLE_SRCS += $(WPAN)/ble/core/auto/ble_hci_le.c
BLE_SRCS += $(WPAN)/ble/core/auto/ble_gap_aci.c
BLE_SRCS += $(WPAN)/ble/core/auto/ble_gatt_aci.c
BLE_SRCS += $(WPAN)/ble/core/auto/ble_hal_aci.c
BLE_SRCS += $(WPAN)/ble/core/auto/ble_l2cap_aci.c
BLE_SRCS += $(WPAN)/ble/core/template/osal.c
BLE_SRCS += $(WPAN)/ble/svc/Src/svc_ctl.c
BLE_SRCS += $(CUBE)/Utilities/sequencer/stm32_seq.c
BLE_SRCS += $(WPAN)/utilities/otp.c
BLE_SRCS += $(WPAN)/utilities/stm_list.c

# ── Project BLE application sources (compile with APP_CFLAGS) ─────────────
BLEAPP_SRCS  = STM32_WPAN/App/app_ble.c
BLEAPP_SRCS += STM32_WPAN/App/ble_beacon_service.c
BLEAPP_SRCS += STM32_WPAN/App/ble_nus_service.c
BLEAPP_SRCS += STM32_WPAN/Target/hw_ipcc.c

BLE_OBJS    = $(addprefix $(BUILD)/, $(notdir $(BLE_SRCS:.c=.o)))
BLEAPP_OBJS = $(addprefix $(BUILD)/, $(notdir $(BLEAPP_SRCS:.c=.o)))

# ── Object lists ───────────────────────────────────────────────────────────
APP_OBJS = $(addprefix $(BUILD)/, $(notdir $(APP_SRCS:.c=.o)))
HAL_OBJS = $(addprefix $(BUILD)/, $(notdir $(HAL_SRCS:.c=.o)))
ASM_OBJS = $(addprefix $(BUILD)/, $(notdir $(ASM_SRCS:.s=.o)))
ALL_OBJS = $(APP_OBJS) $(HAL_OBJS) $(ASM_OBJS) $(BLE_OBJS) $(BLEAPP_OBJS)

# ── Build rules ────────────────────────────────────────────────────────────
.PHONY: all clean

all: $(BUILD)/$(TARGET).hex
	@$(SZ) $(BUILD)/$(TARGET).elf

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(CP) -O ihex $< $@

$(BUILD)/$(TARGET).elf: $(ALL_OBJS) | $(BUILD)
	$(CC) $(LDFLAGS) $^ -o $@

# Application sources — full warnings (+ BLE include paths for app_ble/beacon headers)
$(APP_OBJS): $(BUILD)/%.o: Core/Src/%.c | $(BUILD)
	$(CC) $(APP_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@

# Project BLE application sources — full warnings
$(BUILD)/app_ble.o: STM32_WPAN/App/app_ble.c | $(BUILD)
	$(CC) $(APP_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_beacon_service.o: STM32_WPAN/App/ble_beacon_service.c | $(BUILD)
	$(CC) $(APP_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_nus_service.o: STM32_WPAN/App/ble_nus_service.c | $(BUILD)
	$(CC) $(APP_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/hw_ipcc.o: STM32_WPAN/Target/hw_ipcc.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@

# BLE CUBE middleware — warnings suppressed
$(BUILD)/tl_mbox.o: $(WPAN)/interface/patterns/ble_thread/tl/tl_mbox.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/hci_tl.o: $(WPAN)/interface/patterns/ble_thread/tl/hci_tl.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/hci_tl_if.o: $(WPAN)/interface/patterns/ble_thread/tl/hci_tl_if.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/shci_tl.o: $(WPAN)/interface/patterns/ble_thread/tl/shci_tl.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/shci_tl_if.o: $(WPAN)/interface/patterns/ble_thread/tl/shci_tl_if.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/shci.o: $(WPAN)/interface/patterns/ble_thread/shci/shci.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_hci_le.o: $(WPAN)/ble/core/auto/ble_hci_le.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_gap_aci.o: $(WPAN)/ble/core/auto/ble_gap_aci.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_gatt_aci.o: $(WPAN)/ble/core/auto/ble_gatt_aci.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_hal_aci.o: $(WPAN)/ble/core/auto/ble_hal_aci.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/ble_l2cap_aci.o: $(WPAN)/ble/core/auto/ble_l2cap_aci.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/osal.o: $(WPAN)/ble/core/template/osal.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/svc_ctl.o: $(WPAN)/ble/svc/Src/svc_ctl.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/stm32_seq.o: $(CUBE)/Utilities/sequencer/stm32_seq.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/otp.o: $(WPAN)/utilities/otp.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(BUILD)/stm_list.o: $(WPAN)/utilities/stm_list.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@

# HAL/CMSIS C sources — warnings suppressed (-w)
$(BUILD)/stm32wbxx_hal.o:          $(HAL)/Src/stm32wbxx_hal.c          | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_cortex.o:   $(HAL)/Src/stm32wbxx_hal_cortex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_flash.o:    $(HAL)/Src/stm32wbxx_hal_flash.c    | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_flash_ex.o: $(HAL)/Src/stm32wbxx_hal_flash_ex.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_gpio.o:     $(HAL)/Src/stm32wbxx_hal_gpio.c     | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_pwr.o:      $(HAL)/Src/stm32wbxx_hal_pwr.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_pwr_ex.o:   $(HAL)/Src/stm32wbxx_hal_pwr_ex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_rcc.o:      $(HAL)/Src/stm32wbxx_hal_rcc.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_rcc_ex.o:   $(HAL)/Src/stm32wbxx_hal_rcc_ex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_rtc.o:      $(HAL)/Src/stm32wbxx_hal_rtc.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_rtc_ex.o:   $(HAL)/Src/stm32wbxx_hal_rtc_ex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_adc.o:      $(HAL)/Src/stm32wbxx_hal_adc.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_adc_ex.o:   $(HAL)/Src/stm32wbxx_hal_adc_ex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_dma.o:      $(HAL)/Src/stm32wbxx_hal_dma.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_hsem.o:     $(HAL)/Src/stm32wbxx_hal_hsem.c     | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_i2c.o:      $(HAL)/Src/stm32wbxx_hal_i2c.c      | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_i2c_ex.o:   $(HAL)/Src/stm32wbxx_hal_i2c_ex.c   | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_iwdg.o:     $(HAL)/Src/stm32wbxx_hal_iwdg.c     | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_uart.o:     $(HAL)/Src/stm32wbxx_hal_uart.c     | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/stm32wbxx_hal_uart_ex.o:  $(HAL)/Src/stm32wbxx_hal_uart_ex.c  | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(BUILD)/system_stm32wbxx.o: \
	$(CMSIS)/Device/ST/STM32WBxx/Source/Templates/system_stm32wbxx.c | $(BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@

# ASM startup
$(BUILD)/startup_stm32wb1mxx_cm4.o: \
	$(CMSIS)/Device/ST/STM32WBxx/Source/Templates/gcc/startup_stm32wb1mxx_cm4.s | $(BUILD)
	$(CC) $(CPU_FLAGS) -x assembler-with-cpp -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

# ── OTA production build (requires BLE light stack reflash first) ──────────
# Builds the app linked for Slot A (0x08002800), with OTA_LAYOUT page map.
# Flash via SWD: st-flash write build_ota/TX_Beacon.bin 0x08002800
# (After flashing bootloader at 0x08000000 separately.)
OTA_BUILD = build_ota
OTA_CFLAGS  = $(APP_CFLAGS) -DOTA_LAYOUT
OTA_LDFLAGS = $(CPU_FLAGS)
OTA_LDFLAGS += -TSTM32WB1MMCHX_SLOTA.ld
OTA_LDFLAGS += --specs=nosys.specs --specs=nano.specs
OTA_LDFLAGS += -Wl,--gc-sections
OTA_LDFLAGS += -Wl,--no-warn-rwx-segments
OTA_LDFLAGS += -Wl,-Map=$(OTA_BUILD)/$(TARGET).map
OTA_LDFLAGS += -Wl,--start-group -lc -lm -Wl,--end-group

OTA_APP_OBJS = $(addprefix $(OTA_BUILD)/, $(notdir $(APP_SRCS:.c=.o)))
OTA_HAL_OBJS = $(addprefix $(OTA_BUILD)/, $(notdir $(HAL_SRCS:.c=.o)))
OTA_ASM_OBJS = $(addprefix $(OTA_BUILD)/, $(notdir $(ASM_SRCS:.s=.o)))
OTA_BLE_OBJS = $(addprefix $(OTA_BUILD)/, $(notdir $(BLE_SRCS:.c=.o)))
OTA_BLEAPP_OBJS = $(addprefix $(OTA_BUILD)/, $(notdir $(BLEAPP_SRCS:.c=.o)))
OTA_ALL_OBJS = $(OTA_APP_OBJS) $(OTA_HAL_OBJS) $(OTA_ASM_OBJS) $(OTA_BLE_OBJS) $(OTA_BLEAPP_OBJS)

.PHONY: ota ota_clean

ota: $(OTA_BUILD)/$(TARGET).hex
	@$(SZ) $(OTA_BUILD)/$(TARGET).elf
	@echo "OTA app binary at $(OTA_BUILD)/$(TARGET).bin (flash @ 0x08002800)"

$(OTA_BUILD)/$(TARGET).hex: $(OTA_BUILD)/$(TARGET).elf
	$(CP) -O ihex $< $@
	$(CP) -O binary $< $(OTA_BUILD)/$(TARGET).bin

$(OTA_BUILD)/$(TARGET).elf: $(OTA_ALL_OBJS) | $(OTA_BUILD)
	$(CC) $(OTA_LDFLAGS) $^ -o $@

$(OTA_APP_OBJS): $(OTA_BUILD)/%.o: Core/Src/%.c | $(OTA_BUILD)
	$(CC) $(OTA_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/app_ble.o: STM32_WPAN/App/app_ble.c | $(OTA_BUILD)
	$(CC) $(OTA_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_beacon_service.o: STM32_WPAN/App/ble_beacon_service.c | $(OTA_BUILD)
	$(CC) $(OTA_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_nus_service.o: STM32_WPAN/App/ble_nus_service.c | $(OTA_BUILD)
	$(CC) $(OTA_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/hw_ipcc.o: STM32_WPAN/Target/hw_ipcc.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -DOTA_LAYOUT -c $< -o $@
$(OTA_HAL_OBJS): $(OTA_BUILD)/%.o: | $(OTA_BUILD)
$(OTA_BUILD)/stm32wbxx_hal.o:          $(HAL)/Src/stm32wbxx_hal.c          | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_cortex.o:   $(HAL)/Src/stm32wbxx_hal_cortex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_flash.o:    $(HAL)/Src/stm32wbxx_hal_flash.c    | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_flash_ex.o: $(HAL)/Src/stm32wbxx_hal_flash_ex.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_gpio.o:     $(HAL)/Src/stm32wbxx_hal_gpio.c     | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_pwr.o:      $(HAL)/Src/stm32wbxx_hal_pwr.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_pwr_ex.o:   $(HAL)/Src/stm32wbxx_hal_pwr_ex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_rcc.o:      $(HAL)/Src/stm32wbxx_hal_rcc.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_rcc_ex.o:   $(HAL)/Src/stm32wbxx_hal_rcc_ex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_rtc.o:      $(HAL)/Src/stm32wbxx_hal_rtc.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_rtc_ex.o:   $(HAL)/Src/stm32wbxx_hal_rtc_ex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_adc.o:      $(HAL)/Src/stm32wbxx_hal_adc.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_adc_ex.o:   $(HAL)/Src/stm32wbxx_hal_adc_ex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_dma.o:      $(HAL)/Src/stm32wbxx_hal_dma.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_hsem.o:     $(HAL)/Src/stm32wbxx_hal_hsem.c     | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_i2c.o:      $(HAL)/Src/stm32wbxx_hal_i2c.c      | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_i2c_ex.o:   $(HAL)/Src/stm32wbxx_hal_i2c_ex.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_iwdg.o:     $(HAL)/Src/stm32wbxx_hal_iwdg.c     | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_uart.o:     $(HAL)/Src/stm32wbxx_hal_uart.c     | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/stm32wbxx_hal_uart_ex.o:  $(HAL)/Src/stm32wbxx_hal_uart_ex.c  | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BUILD)/system_stm32wbxx.o: \
	$(CMSIS)/Device/ST/STM32WBxx/Source/Templates/system_stm32wbxx.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) -c $< -o $@
$(OTA_BLE_OBJS): $(OTA_BUILD)/%.o: | $(OTA_BUILD)
$(OTA_BUILD)/tl_mbox.o:  $(WPAN)/interface/patterns/ble_thread/tl/tl_mbox.c  | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/hci_tl.o:   $(WPAN)/interface/patterns/ble_thread/tl/hci_tl.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/hci_tl_if.o: $(WPAN)/interface/patterns/ble_thread/tl/hci_tl_if.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/shci_tl.o:  $(WPAN)/interface/patterns/ble_thread/tl/shci_tl.c  | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/shci_tl_if.o: $(WPAN)/interface/patterns/ble_thread/tl/shci_tl_if.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/shci.o:     $(WPAN)/interface/patterns/ble_thread/shci/shci.c   | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_hci_le.o: $(WPAN)/ble/core/auto/ble_hci_le.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_gap_aci.o: $(WPAN)/ble/core/auto/ble_gap_aci.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_gatt_aci.o: $(WPAN)/ble/core/auto/ble_gatt_aci.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_hal_aci.o: $(WPAN)/ble/core/auto/ble_hal_aci.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/ble_l2cap_aci.o: $(WPAN)/ble/core/auto/ble_l2cap_aci.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/osal.o:     $(WPAN)/ble/core/template/osal.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/svc_ctl.o:  $(WPAN)/ble/svc/Src/svc_ctl.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/stm32_seq.o: $(CUBE)/Utilities/sequencer/stm32_seq.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/otp.o:      $(WPAN)/utilities/otp.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/stm_list.o: $(WPAN)/utilities/stm_list.c | $(OTA_BUILD)
	$(CC) $(HAL_CFLAGS) $(INC) $(BLE_INC) -c $< -o $@
$(OTA_BUILD)/startup_stm32wb1mxx_cm4.o: \
	$(CMSIS)/Device/ST/STM32WBxx/Source/Templates/gcc/startup_stm32wb1mxx_cm4.s | $(OTA_BUILD)
	$(CC) $(CPU_FLAGS) -x assembler-with-cpp -c $< -o $@

$(OTA_BUILD):
	mkdir -p $@

ota_clean:
	rm -rf $(OTA_BUILD)

# ── Bootloader build ───────────────────────────────────────────────────────
# Minimal standalone binary. Flash via SWD: st-flash write build_boot/bootloader.bin 0x08000000
BOOT_BUILD = build_boot
BOOT_CFLAGS  = $(CPU_FLAGS) -Os -g3 -std=gnu11 -Wall
BOOT_CFLAGS += -DSTM32WB1Mxx -ffunction-sections -fdata-sections
BOOT_CFLAGS += --specs=nano.specs
BOOT_LDFLAGS = $(CPU_FLAGS)
BOOT_LDFLAGS += -TBootloader/STM32WB1MMCHX_BOOTLOADER.ld
BOOT_LDFLAGS += --specs=nosys.specs --specs=nano.specs
BOOT_LDFLAGS += -Wl,--gc-sections -Wl,--no-warn-rwx-segments
BOOT_LDFLAGS += -Wl,-Map=$(BOOT_BUILD)/bootloader.map
BOOT_LDFLAGS += -Wl,--start-group -lc -lm -Wl,--end-group

.PHONY: boot boot_clean

boot: $(BOOT_BUILD)/bootloader.hex
	@$(SZ) $(BOOT_BUILD)/bootloader.elf
	@echo "Bootloader at $(BOOT_BUILD)/bootloader.bin (flash @ 0x08000000)"

$(BOOT_BUILD)/bootloader.hex: $(BOOT_BUILD)/bootloader.elf
	$(CP) -O ihex $< $@
	$(CP) -O binary $< $(BOOT_BUILD)/bootloader.bin

$(BOOT_BUILD)/bootloader.elf: $(BOOT_BUILD)/bootloader_main.o | $(BOOT_BUILD)
	$(CC) $(BOOT_LDFLAGS) $^ -o $@

$(BOOT_BUILD)/bootloader_main.o: Bootloader/bootloader_main.c | $(BOOT_BUILD)
	$(CC) $(BOOT_CFLAGS) -ICore/Inc -c $< -o $@

$(BOOT_BUILD):
	mkdir -p $@

boot_clean:
	rm -rf $(BOOT_BUILD)
