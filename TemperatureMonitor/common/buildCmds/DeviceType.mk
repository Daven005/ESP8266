BOOT_START		:= 0x0
USER_ROM_ADDR   := 0x010000
FLASH_ROM_ADDR   := 0x0

ifeq ($(DEVICE), ESP-12)
USER_ROM_SIZE   := 0x400000
INIT_START		:= 0x3FC000
PARAM_START		:= 0x3FE000
LD_SCRIPT		:= eagle.app.v6.ld
FLASH_SIZE		:= 32m
else
ifeq ($(DEVICE), ESP-07)
USER_ROM_SIZE   := 0x100000
INIT_START		:= 0x1FC000
PARAM_START		:= 0x1FE000
LD_SCRIPT		:= eagle.app.v6.ld
FLASH_SIZE		:= 4m
else
ifeq ($(DEVICE), ESP-01)
USER_ROM_SIZE   := 0x080000
INIT_START		:= 0x07C000
PARAM_START		:= 0x07E000
LD_SCRIPT		:= eagle.app.v6.new.512.app1.ld
FLASH_SIZE		:= 4m
endif
endif
endif
