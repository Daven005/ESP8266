/*
/* config.c
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <c_types.h>
#include <osapi.h>
#include <spi_flash.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "config.h"
#include "sysCfg.h"

SAVE_FLAG saveFlag;
static uint32 dirtyCount = 0;
static uint32 lastSetDirty;
static uint32 lastSaved = 0;
static os_timer_t delay_timer;

static uint32 ICACHE_FLASH_ATTR doSum(void) {
	uint32 sum = 0;
	uint8 * p = (uint8 *) &sysCfg;
	uint16 idx;
	for (idx = 0; idx < sizeof(sysCfg)-sizeof(uint32); idx++) sum+= p[idx];
	return sum;
}

static bool ICACHE_FLASH_ATTR checkSum(void) {
	return sysCfg.sumcheck == doSum();
}

static void ICACHE_FLASH_ATTR saveSum(void) {
	sysCfg.sumcheck = doSum();
}

static void ICACHE_FLASH_ATTR CFG_Save(void) {
	 spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
	                   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));

	if (saveFlag.flag == 0) {
		spi_flash_erase_sector(CFG_LOCATION + 1);
		spi_flash_write((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
						(uint32 *)&sysCfg, sizeof(SYSCFG));
		saveFlag.flag = 1;
		spi_flash_erase_sector(CFG_LOCATION + 3);
		spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
						(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	} else {
		spi_flash_erase_sector(CFG_LOCATION + 0);
		spi_flash_write((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
						(uint32 *)&sysCfg, sizeof(SYSCFG));
		saveFlag.flag = 0;
		spi_flash_erase_sector(CFG_LOCATION + 3);
		spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
						(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	}
	TESTP("With saveFlag: %d\n", saveFlag.flag);
	lastSaved = system_get_time();
}

static void ICACHE_FLASH_ATTR CFG_Load() {
	os_printf("\nload (%x/%x)...", sizeof(sysCfg), SPI_FLASH_SEC_SIZE);
	spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
				   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	if (saveFlag.flag == 0) {
		spi_flash_read((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sysCfg, sizeof(SYSCFG));
	} else {
		spi_flash_read((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sysCfg, sizeof(SYSCFG));
	}
	if(sysCfg.cfg_holder != CFG_HOLDER || !checkSum()){
		TESTP("Reinitialising sgsCFG\n");
		initSysCfg();
		saveSum();
		CFG_Save();
	}
}

void ICACHE_FLASH_ATTR CFG_dirty(void) {
	dirtyCount++;
	lastSetDirty = system_get_time();
}

static void ICACHE_FLASH_ATTR checkLazyWrite(void) {
	if (dirtyCount == 0)
		return;
	if (!checkSum()) { // data has changed
		saveSum();
		CFG_Save();
		TESTP("sysCfg updated\n");
		CFG_print();
	} else {
		TESTP("sysCfg data not changed?\n");
	}
	dirtyCount = 0;
}

uint16 ICACHE_FLASH_ATTR sysCfgUpdates(void) {
#ifdef USE_WIFI
	if (sysCfg.updates) return sysCfg.updates;
	return UPDATES;
#else
	return 0;
#endif
}

void ICACHE_FLASH_ATTR CFG_print(void) {
	os_printf("saveFlag %d CFG_LOCATION %x cfg_holder %lx\n", saveFlag.flag, CFG_LOCATION, sysCfg.cfg_holder);
#ifdef USE_WIFI
	os_printf("sta_ssid %s sta_type %d\n", sysCfg.sta_ssid, sysCfg.sta_type);
	os_printf("deviceName %s deviceLocation %s\n", sysCfg.deviceName, sysCfg.deviceLocation);
	os_printf("MQTT host %s port %d\n", sysCfg.mqtt_host, sysCfg.mqtt_port);
#endif
}

uint32 ICACHE_FLASH_ATTR CFG_lastSaved(void) {
	return lastSaved;
}

void ICACHE_FLASH_ATTR CFG_init(uint32 delay) {
	os_timer_disarm(&delay_timer);
	os_timer_setfn(&delay_timer, (os_timer_func_t *) checkLazyWrite, (void *) 0);
	os_timer_arm(&delay_timer, delay, true);
	CFG_Load();
}
