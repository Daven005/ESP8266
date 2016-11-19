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
#include "io.h"
#include "publish.h"

#include "user_conf.h"
#include "IOdefs.h"

static bool currentOutputs[OUTPUTS];
static override_t outputOverrides[OUTPUTS];

pumpState_t ICACHE_FLASH_ATTR pumpState(void) {
	switch (outputOverrides[OP_PUMP]) {
	case OR_NOT_SET:
		if (currentOutputs[OP_PUMP])
			return PUMP_ON_NORMAL;
		return PUMP_OFF_NORMAL;
	case OR_ON: return PUMP_ON_OVERRIDE;
	case OR_OFF: return PUMP_OFF_OVERRIDE;
	}
	return PUMP_OFF_NORMAL;
}

void ICACHE_FLASH_ATTR clearPumpOverride() {
	overrideClearOutput(OP_PUMP);
}

bool ICACHE_FLASH_ATTR switchInConfigMode(void) {
	return easygpio_inputGet(TOGGLE);
}

void ICACHE_FLASH_ATTR initIO(void) {
	easygpio_pinMode(LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(LED, 1);
	easygpio_pinMode(ACTION_LED, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(ACTION_LED, 0);
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(PUMP, 1); // Inverted

	easygpio_pinMode(ANALOGUE_SELECT, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputSet(ANALOGUE_SELECT, 0);

	easygpio_pinMode(FLOW_SENSOR, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(FLOW_SENSOR);
	easygpio_pinMode(SWITCH, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(SWITCH);
	easygpio_pinMode(TOGGLE, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	easygpio_outputDisable(TOGGLE);
}

bool ICACHE_FLASH_ATTR outputState(uint8 id) {
	if (id < OUTPUTS) {
		switch (outputOverrides[id]) {
		case OR_NOT_SET: return currentOutputs[id];
		case OR_ON: return true;
		case OR_OFF: return false;
		}
	}
	return false;
}

void ICACHE_FLASH_ATTR checkSetOutput(uint8 op, bool newSetting) {
	if (op < OUTPUTS) {
		if (currentOutputs[op] != newSetting) {
			publishOutput(op, newSetting);
			currentOutputs[op] = newSetting;
		}
		switch (outputOverrides[op]) {
		case OR_NOT_SET: easygpio_outputSet(PUMP, !newSetting); break;
		case OR_OFF: easygpio_outputSet(PUMP, true); break;
		case OR_ON: easygpio_outputSet(PUMP, false); break;
		}
		return;
	}
	publishError(92, op); // Invalid Output ID
	return;
}

void ICACHE_FLASH_ATTR overrideSetOutput(uint8 op, uint8 set) {
	if (op < OUTPUTS) {
		outputOverrides[op] = set ? OR_ON : OR_OFF;
		easygpio_outputSet(PUMP, !set);
		printOutput(op);
	}
}

void ICACHE_FLASH_ATTR overrideClearOutput(uint8 op) {
	if (op < OUTPUTS) {
		outputOverrides[op] = OR_NOT_SET;
		easygpio_outputSet(PUMP, !currentOutputs[op]);
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

void ICACHE_FLASH_ATTR checkOutputs(void) {
	uint8 op;
	for (op=0; op < OUTPUTS; op++) { // Repeat outputs in case corrupted
		checkSetOutput(op, currentOutputs[op]);
	}
}

override_t ICACHE_FLASH_ATTR getOverride(uint8 op) {
	if (op < OUTPUTS) {
		return outputOverrides[op];
	}
	return OR_INVALID;

}
