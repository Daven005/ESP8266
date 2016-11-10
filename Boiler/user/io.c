/*
 * io.c
 *
 *  Created on: 17 Dec 2015
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include "easygpio.h"
#include "mcp23s17.h"
#include "io.h"
#include "user_conf.h"
#include "publish.h"

typedef enum { OR_NOT_SET, OR_OFF, OR_ON } override;
static int currentInputs[INPUTS];
static bool currentOutputs[OUTPUTS];
static override inputOverrides[INPUTS];
static override outputOverrides[OUTPUTS];
static int ioTestErrors = 0;

void ICACHE_FLASH_ATTR initIO(void) {
	easygpio_pinMode(IP4, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(IP4);
	easygpio_pinMode(IO_TEST, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(IO_TEST);

	mcp23s17_init();
	mcp23s17_REG_SET(IODIR_CTRL, PORTA, 0b11110000); // Top 4 bits are inputs
	mcp23s17_REG_SET(GPPU_CTRL,  PORTA, 0b00001111); // Enable Output pull ups
	sGPIO_SET(PORTA, 0); // All outputs OFF
}

static void checkUpDown(int *val, bool ip) {
	if (ip) {
		if (*val < 10) (*val)++;
	} else {
		if (*val > 0) (*val)--;
	}
}

void ICACHE_FLASH_ATTR checkInputs(bool publish) {
	uint8 val, idx;

	checkUpDown(&currentInputs[4], easygpio_inputGet(IP4));
	val = sGPIO_READ(PORTA);
	for (idx = 0; idx < 4; idx++) {
		checkUpDown(&currentInputs[idx], (val & 0x10));
		val >>= 1;
	}
	if (publish) {
		for (idx = 0; idx < INPUTS; idx++) {
			publishInput(idx, input(idx));
		}
	}
}

bool ICACHE_FLASH_ATTR input(uint8 ip) {
	if (ip < INPUTS) {
		switch (inputOverrides[ip]) {
		case OR_NOT_SET:
			return currentInputs[ip] > 5;
		case OR_ON:
			return true;
		case OR_OFF:
			return false;
		}
	}
	return false;
}

bool ICACHE_FLASH_ATTR outputState(uint8 id) {
	if (id < OUTPUTS)
		return currentOutputs[id];
	return false;
}

void ICACHE_FLASH_ATTR checkSetOutput(uint8 op, bool newSetting) {
	if (op < OUTPUTS) {
		if (currentOutputs[op] != newSetting) {
			publishOutput(op, newSetting);
			currentOutputs[op] = newSetting;
		}
		switch (outputOverrides[op]) {
		case OR_NOT_SET: sGPIO_SET_PIN(PORTA, op+1, newSetting); break;
		case OR_OFF: sGPIO_SET_PIN(PORTA, op+1, false); break;
		case OR_ON: sGPIO_SET_PIN(PORTA, op+1, true); break;
		}
		return;
	}
	publishError(92, op); // Invalid Output ID
	return;
}

void ICACHE_FLASH_ATTR overrideSetOutput(uint8 op, uint8 set) {
	if (op < OUTPUTS) {
		outputOverrides[op] = set ? OR_ON : OR_OFF;
		sGPIO_SET_PIN(PORTA, op + 1, set);
		printOutput(op);
	}
}

void ICACHE_FLASH_ATTR overrideClearOutput(uint8 op) {
	if (op < OUTPUTS) {
		outputOverrides[op] = OR_NOT_SET;
		sGPIO_SET_PIN(PORTA, op + 1, currentOutputs[op]);
		printOutput(op);
	}
}

void ICACHE_FLASH_ATTR printOutput(uint8 op) {
	if (op < OUTPUTS) {
		if (outputOverrides[op] == OR_NOT_SET) {
			os_printf("OP[%d]=%d ", op, currentOutputs[op]);
		} else {
			os_printf("OP[%d]=%d(%d) ",
					op, currentOutputs[op], (outputOverrides[op] == OR_ON) ? 1 : 0);
		}
	}
}

void ICACHE_FLASH_ATTR overrideSetInput(uint8 ip, uint8 set) {
	if (ip < INPUTS) {
		inputOverrides[ip] = set ? OR_ON : OR_OFF;
		printInput(ip);
	}
}

void ICACHE_FLASH_ATTR overrideClearInput(uint8 ip) {
	if (ip < INPUTS) {
		inputOverrides[ip] = OR_NOT_SET;
		printInput(ip);
	}
}

void ICACHE_FLASH_ATTR printInput(uint8 ip) {
	if (ip < INPUTS) {
		if (inputOverrides[ip] == OR_NOT_SET) {
			os_printf("IP[%d]=%d ", ip, currentInputs[ip]);
		} else {
			os_printf("IP[%d]=%d(%d) ",
					ip, currentInputs[ip], (inputOverrides[ip] == OR_ON) ? 1 : 0);
		}
	}
}

void ICACHE_FLASH_ATTR printIOreg(void) {
	os_printf("OLAT = %02x, ", readOLAT());
	os_printf("GPIO = %02x, ", readGPIO());
	os_printf("IODIR = %02x\n", mcp23s17_REG_GET(IODIR_CTRL, PORTA));
}

uint8 ICACHE_FLASH_ATTR readGPIO(void) {
	return sGPIO_READ(PORTA);
}

uint8 ICACHE_FLASH_ATTR readOLAT(void) {
	return sGPIO_GET(PORTA);
}

static void ICACHE_FLASH_ATTR resetOutputs(void) {
	uint8 op;
	for (op = 0; op < OUTPUTS; op++) {
		// Repeat outputs in case corrupted
		checkSetOutput(op, currentOutputs[op]);
	}
}

void ICACHE_FLASH_ATTR checkOutputs(void) {
	resetOutputs();
	// This is just a double check that outputs are bing set
	if (easygpio_inputGet(IO_TEST) != currentOutputs[OP_EMERGENCY_DUMP_ON]) {
		publishError(93, easygpio_inputGet(IO_TEST)); // Problem with IO (GPIO2 != OP_EMERGENCY_DUMP_ON)
		ioTestErrors++;
		if (ioTestErrors > 5) {
			initIO();
			resetOutputs();
		}
	}
}
