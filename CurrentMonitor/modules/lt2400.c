/*
 * lt2400.c
 *
 *  Created on: 1 Mar 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include "debug.h"
#include "easygpio.h"
#include "lt2400.h"

static uint8 status;

void ICACHE_FLASH_ATTR lt2400_IO_Init(void) {
	easygpio_pinMode(LT2400_SDO, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(LT2400_SDO);
	easygpio_pinMode(LT2400_CS, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LT2400_CS, 1);
	easygpio_pinMode(LT2400_SCK, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LT2400_SCK, 0);
	easygpio_pinMode(16, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(16, 0);
}

uint8 ICACHE_FLASH_ATTR lt2400_status(void) {
	return status;
}

bool ICACHE_FLASH_ATTR lt2400_ready(void) {
	bool ret;
	easygpio_outputSet(LT2400_SCK, 0);
	easygpio_outputSet(LT2400_CS, 0);
	ret = !easygpio_inputGet(LT2400_SDO);
	easygpio_outputSet(LT2400_CS, 1);
	return ret;
}

static uint32 ICACHE_FLASH_ATTR readBits(uint8 bits) { // Assumes CS/ enabled (0)
	uint32 ret = 0;
	easygpio_outputSet(LT2400_SCK, 0);
	while (bits--) {
		ret <<= 1;
		easygpio_outputSet(LT2400_SCK, 1);
		easygpio_outputSet(16, 1);
		ets_delay_us(5);
		if (easygpio_inputGet(LT2400_SDO))
			ret |= 1;
		easygpio_outputSet(LT2400_SCK, 0);
		easygpio_outputSet(16, 0);
		ets_delay_us(5);
	}
	return ret;
}

int32 ICACHE_FLASH_ATTR lt2400_read(void) {
	int32 value = 0;
	easygpio_outputSet(LT2400_CS, 0);
	if (!easygpio_inputGet(LT2400_SDO)) {
		status = readBits(4);
		value = readBits(28) >> 4;
		if ((status & 0b10) == 0) { // -ve
			value |= 0xff000000;
		}
	}
	easygpio_outputSet(LT2400_CS, 1);
	return value;
}
