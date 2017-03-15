/*
 * output.c
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#define DEBUG_OVERRIDE
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "easygpio.h"
#include "io.h"
#include "user_conf.h"

#ifdef OUTPUTS
#pragma message "Outputs"
static const uint8 outputMap[OUTPUTS] = { LED, RELAY_1, RELAY_2, RELAY_3, RELAY_4 };
static bool currentOutputs[OUTPUTS];
static bool outputOverrides[OUTPUTS];

uint8 ICACHE_FLASH_ATTR getOutput(uint8 idx) {
	return currentOutputs[idx];
}

void ICACHE_FLASH_ATTR overrideClearOutput(int id) {
	if (0 <= id && id < OUTPUTS) {
		outputOverrides[id] = RELAY_OFF;
	}
}

void ICACHE_FLASH_ATTR overrideSetOutput(int id, int value) {
	if (0 <= id && id < OUTPUTS) {
		switch (value) {
		case 0:
			currentOutputs[id] = RELAY_OFF;
			outputOverrides[id] = true;
			break;
		case 1:
			currentOutputs[id] = RELAY_ON;
			outputOverrides[id] = true;
			break;
		case -1:
			outputOverrides[id] = false;
			break;
		}
		easygpio_outputSet(outputMap[id], currentOutputs[id]);
		INFOP("<%d> Output %d set to %d\n", id, outputMap[id], currentOutputs[id]);
	}
}

void ICACHE_FLASH_ATTR initOutput(void) {
	int idx;
	for (idx = 1; idx < OUTPUTS; idx++) { // Ignore LED as already set up
		easygpio_pinMode(outputMap[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[idx], RELAY_OFF);
		currentOutputs[idx] = RELAY_OFF;
		outputOverrides[idx] = false;
	}
}
#else
#pragma message "No Outputs"
#endif
