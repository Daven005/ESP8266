/*
 * output.c
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "debug.h"
#include "easygpio.h"
#include "output.h"

#include "include/user_conf.h"

#ifdef USE_OUTPUTS
#pragma message "Outputs"
static const uint8 outputMap[MAX_OUTPUT] = { LED, RELAY_1, RELAY_2, RELAY_3, RELAY_4 };
static bool currentOutputs[MAX_OUTPUT];
static bool outputOverrides[MAX_OUTPUT];

uint8 ICACHE_FLASH_ATTR getOutput(uint8 idx) {
	return currentOutputs[idx];
}

void ICACHE_FLASH_ATTR setOutput(int id, int value) {
	if (0 <= id && id < MAX_OUTPUT) {
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
	for (idx = 0; idx < MAX_OUTPUT; idx++) {
		easygpio_pinMode(outputMap[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[idx], 0);
	}
}
#else
#pragma message "No Outputs"

#endif
