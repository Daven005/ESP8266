#############################################################
#
# Root Level Makefile
#
# (c) by CHERTS <sleuthhound@gmail.com>
#
#############################################################

include ..\common\buildCmds\Paths.mk
export PATH := ../buildCmds;$(PATH)

VERBOSE 	?= 0

ESPPORT		?= COM3
ESPBAUD		?= 115200
DEVICE 		?= ESP-07
CONFIG_NAME	?= $(DEVICE)
MAPFILE		?= $(CONFIG_NAME).map
COMMON_MODULES ?=  common/driver common/mqtt common/modules common/temperature

include ..\common\buildCmds\DeviceType.mk

# name for the target project
TARGET		= app

# which modules (subdirectories) of the project to include in compiling
MODULES		= user $(COMMON_MODULES)
EXTRA_INCDIR    = 

include ..\common\buildCmds\Flags.mk

include ..\common\buildCmds\BuildFunctions.mk

.PHONY: all checkdirs clean newbuild verify

include ..\common\buildCmds\versions.mk

all: checkdirs $(TARGET_OUT)

$(TARGET_OUT): $(APP_AR)
	$(vecho) "LD $@"  -L$(SDK_LIBDIR) $(LD_SCRIPT)
	$(Q) $(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@
	$(vecho) "Run objcopy, please wait..."
	$(Q) $(OBJCOPY) --only-section .text -O binary $@ eagle.app.v6.text.bin
	$(Q) $(OBJCOPY) --only-section .data -O binary $@ eagle.app.v6.data.bin
	$(Q) $(OBJCOPY) --only-section .rodata -O binary $@ eagle.app.v6.rodata.bin
	$(Q) $(OBJCOPY) --only-section .irom0.text -O binary $@ eagle.app.v6.irom0text.bin
	$(vecho) "objcopy done"
#	$(Q) $(OBJDUMP) -S -l $@ >obj.txt
	$(vecho) "Run gen_appbin.exe"
	$(SDK_TOOLS)/gen_appbin_old.exe $(TARGET_OUT) v6
	$(Q) mv eagle.app.v6.flash.bin $(FIRMWARE_FLASH)
	$(Q) mv eagle.app.v6.irom0text.bin $(FIRMWARE_USER)
	$(Q) rm eagle.app.v6.*
	$(Q) rm eagle.app.sym
	$(vecho) "Generated $(FIRMWARE_USER) and  $(FIRMWARE_USER)"
	$(vecho) " $(FIRMWARE_FLASH)-------->0x00000"
	$(vecho) " $(FIRMWARE_USER)---->$(USER_ROM_ADDR)"
	$(vecho) "Done"

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)
	@echo $(BUILD_DIR)

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(FW_BASE):
	$(Q) mkdir -p $@

firmwareNoSleep:
	$(Q) mkdir -p $@

firmwareOutput:
	$(Q) mkdir -p $@

flash: all
	$(ESPTOOL) -p $(ESPPORT) -b $(ESPBAUD) write_flash --flash_size $(FLASH_SIZE) \
$(FLASH_ROM_ADDR) $(FIRMWARE_FLASH) \
$(USER_ROM_ADDR) $(FIRMWARE_USER)

verify:
	$(ESPTOOL) -p $(ESPPORT) -b $(ESPBAUD) verify_flash --diff yes \
$(FLASH_ROM_ADDR) $(FIRMWARE_FLASH) \
$(USER_ROM_ADDR) $(FIRMWARE_USER)

verifyinit:
	$(ESPTOOL) -p $(ESPPORT) -b $(ESPBAUD) verify_flash --diff yes \
$(BOOT_START) $(SDK_BASE)/bin/boot_v1.6.bin \
$(INIT_START) $(SDK_BASE)/bin/esp_init_data_default.bin \
$(PARAM_START) $(SDK_BASE)/bin/blank.bin

flashinit:
	$(ESPTOOL) -p $(ESPPORT) -b $(ESPBAUD) write_flash \
$(BOOT_START) $(SDK_BASE)/bin/boot_v1.6.bin \
$(INIT_START) $(SDK_BASE)/bin/esp_init_data_default.bin \
$(PARAM_START) $(SDK_BASE)/bin/blank.bin

fsize:
	$(ESPTOOL) -p $(ESPPORT) flash_id

rebuild: clean all

clean:
	$(Q) rm -f $(APP_AR)
	$(Q) rm -f $(TARGET_OUT)
	$(Q) rm -f *.bin
	$(Q) rm -f *.sym
	$(Q) rm -rf $(BUILD_DIR)
	$(Q) rm -rf $(BUILD_BASE)

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
