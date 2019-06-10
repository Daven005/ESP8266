# libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal phy pp net80211 lwip wpa main upgrade ssl smartconfig crypto

# compiler flags using during compilation of source files
CFLAGS		= $(DEFINES) -Os -g -O2 -Wmissing-prototypes -Wpointer-arith -Wundef -Werror -Wno-implicit -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH

# linker flags used to generate the main object file
LDFLAGS		= -Wl,-M=$(MAPFILE) -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

