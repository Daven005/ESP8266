# Base directory for the compiler
XTENSA_TOOLS_ROOT := c:/Espressif/xtensa-lx106-elf/bin

# select which tools to use as compiler, librarian and linker
CC		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
OBJCOPY := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objcopy
OBJDUMP := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objdump

# base directory of the ESP8266 SDK package, absolute
SDK_BASE	:= c:/Espressif/ESP8266_SDK_200
SDK_TOOLS	:= c:/Espressif/utils
ESPTOOL		:= $(SDK_TOOLS)/esptool.py

# various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include

BUILD_BASE	= build
FW_BASE		= firmware

FIRMWARE_FLASH	= firmware/flash.bin
FIRMWARE_USER	= firmware/user1.bin
