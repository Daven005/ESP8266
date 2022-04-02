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
#include "pdf8574.h"
#include "io.h"
#include "user_conf.h"
#include "IOdefs.h"

#ifdef USE_INPUTS
static const uint8 inputMap[INPUTS] = { SENSOR_1 };
#endif

#ifdef USE_OUTPUTS
/***********************************
 * INVERT_RELAYS used to invert actual output - always sets/returns logical state
 *
 */
#pragma message "Outputs"
#ifndef USE_I2C
#if OUTPUTS == 1
static const uint8 outputMap[OUTPUTS] = { LED };
#elif OUTPUTS == 2
static const uint8 outputMap[OUTPUTS] = { LED, RELAY_1 };
#else
static const uint8 outputMap[OUTPUTS] = { LED, RELAY_1, RELAY_2, RELAY_3, RELAY_4 };
#endif
#endif
static bool currentOutputs[OUTPUTS];
static bool localOutputs[OUTPUTS];
static bool outputOverrides[OUTPUTS];
static void updateOutput(int id);

uint8 ICACHE_FLASH_ATTR getOutput(uint8 id) {
	if (0 <= id && id < OUTPUTS) {
		return currentOutputs[id];
	}
	return 0;
}

static void ICACHE_FLASH_ATTR updateOutput(int id) {
#ifdef USE_I2C
	uint8 op = getExpanderOutput();
	op &= ~(1 << id);
	if (currentOutputs[id] == RELAY_ON) op |= (1 << id);
	setExpanderOutput(op);
	TESTP("I2C outputs = %u", getExpanderOutput());
#else
	easygpio_outputSet(outputMap[id], currentOutputs[id]);
	INFOP("<%d> Output %d set to %d\n", id, outputMap[id], currentOutputs[id]);
#endif
}

void ICACHE_FLASH_ATTR overrideClearOutput(int id) {
	if (0 <= id && id < OUTPUTS) {
		outputOverrides[id] = false;
		currentOutputs[id] = localOutputs[id];
		updateOutput(id);
	}
}

void ICACHE_FLASH_ATTR setOutput(int id, bool value) { // Usually used for local IO
	if (0 <= id && id < OUTPUTS) {
		localOutputs[id] = value ? RELAY_ON : RELAY_OFF;
		if (!outputOverrides[id]) currentOutputs[id] = value ? RELAY_ON : RELAY_OFF;
		updateOutput(id);
	}
}

void ICACHE_FLASH_ATTR overrideSetOutput(int id, int value) { // Usually used for remote IO
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
			currentOutputs[id] = localOutputs[id];
			break;
		}
		updateOutput(id);
	}
}

void ICACHE_FLASH_ATTR initOutput(void) {
	int idx;
#ifdef USE_I2C
	initExpander();
#else
	for (idx = 1; idx < OUTPUTS; idx++) { // Ignore LED as already set up
		easygpio_pinMode(outputMap[idx], EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
		easygpio_outputSet(outputMap[idx], RELAY_OFF);
		currentOutputs[idx] = 0;
		outputOverrides[idx] = false;
	}
#endif
}
#else
#pragma message "No Outputs"
#endif
#ifdef USE_INPUTS
void ICACHE_FLASH_ATTR overrideSetInput(uint8 op, uint8 set) {}
void ICACHE_FLASH_ATTR overrideClearInput(uint8 id) {}

uint8 ICACHE_FLASH_ATTR getInput(uint8 id) {
	uint8 ip = easygpio_inputGet(inputMap[id]);
	INFOP("<%d> Input %d is %d\n", id, inputMap[id], ip);
	return ip;
}


void ICACHE_FLASH_ATTR initInput() {
	easygpio_pinMode(inputMap[0], EASYGPIO_PULLUP, EASYGPIO_INPUT);
}
#endif

