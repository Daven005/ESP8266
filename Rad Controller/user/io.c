/*
 * output.c
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

//#define DEBUG_OVERRIDE
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "easygpio.h"
#include "io.h"
#include "user_conf.h"
#include "overrideTemp.h"

#define MAX_OVERRIDE_LEVEL 3
static const uint8 outputMap[OUTPUTS] = { LED, RELAY_1/*, RELAY_2, RELAY_3, RELAY_4*/ };
static enum override_t currentOutputs[MAX_OVERRIDE_LEVEL][OUTPUTS];

uint8 ICACHE_FLASH_ATTR getOutput(uint8 idx) {
	uint8 level = 2;
	while ((currentOutputs[level][idx] == NO_OVERRIDE) && level > 0) {
		level--;
	}
	return currentOutputs[level][idx] == OVERRIDE_ON; // NO_OVERRIDE implies OFF at Level 0
}

void ICACHE_FLASH_ATTR overrideClearOutput(int id) { // Clear level 1 override
	if (0 <= id && id < OUTPUTS) {
		currentOutputs[1][id] = NO_OVERRIDE;
		easygpio_outputSet(outputMap[id], getOutput(id));
	}
}

void ICACHE_FLASH_ATTR overrideSetOutput(int id, int value) { // Set Level 1 override (ON or OFF)
	if (0 <= id && id < OUTPUTS) {
		switch (value) {
		case 0:
			currentOutputs[1][id] = OVERRIDE_OFF;
			break;
		case 1:
			currentOutputs[1][id] = OVERRIDE_ON;
			break;
		case -1:
			currentOutputs[1][id] = NO_OVERRIDE;
			break;
		}
		easygpio_outputSet(outputMap[id], getOutput(id));
		TESTP("<%d> Output %d set to %d\n", id, outputMap[id], getOutput(id));
	}
}

void ICACHE_FLASH_ATTR setLevelTwoOverride(int id, enum override_t or) {
	if (id == 1) {
		switch (or) {
		case OVERRIDE_OFF:
		case OVERRIDE_ON:
		case NO_OVERRIDE:
			currentOutputs[2][id] = or;
			break;
		}
		easygpio_outputSet(outputMap[id], getOutput(id));
		TESTP("<%d> Output %d set to %d\n", id, outputMap[id], getOutput(id));
	}
}

void ICACHE_FLASH_ATTR initOutput(void) {
	int idx, level;
	for (idx = 1; idx < OUTPUTS; idx++) { // Ignore LED as already set up
		easygpio_pinMode(outputMap[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[idx], RELAY_OFF);
		for (level=0; level < MAX_OVERRIDE_LEVEL; level++) {
			currentOutputs[level][idx] = NO_OVERRIDE;
		}
	}
}
