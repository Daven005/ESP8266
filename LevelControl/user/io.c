/*
 * io.c
 *
 *  Created on: 7 Mar 2017
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "gpio.h"
#include "easygpio.h"
#include "stdout.h"
#include "user_conf.h"
#include "config.h"
#include "io.h"
#include "flash.h"

static bool overridePressure = false;
static uint16 currentPressure;
static pumpState_t _pumpState;

uint16 ICACHE_FLASH_ATTR getCurrentPressure(void) {
	return currentPressure;
}

void ICACHE_FLASH_ATTR updatePressure(void) {
	uint16 newPressure = system_adc_read();
	if (!overridePressure) {
		currentPressure = (currentPressure * 9 + newPressure) / 10; // Average over 1 second
	}
}

void ICACHE_FLASH_ATTR overrideSetPressure(uint16 p) {
	currentPressure = p;
	overridePressure = true;
}

void ICACHE_FLASH_ATTR overrideClearPressure(void) {
	overridePressure = false;
}

pumpState_t pumpState(void) {
	return _pumpState;
}

// Not actually using override as such (yet)

void overrideSetPump(char* param) {
	if (strcmp("off", param) == 0) {
		_pumpState = MANUAL_OFF;
	} else if (strcmp("on", param) == 0) {
		_pumpState = MANUAL_ON;
	} else if (strcmp("auto", param) == 0) {
		_pumpState = AUTO_OFF;
	}
}

void overrideClearPump(void) {
	_pumpState = AUTO_OFF;
}

void ICACHE_FLASH_ATTR pumpState_OffManual(void) {
	publishAlarm(4, _pumpState); // Warn when going Manual OFF
	_pumpState = MANUAL_OFF;
	startMultiFlash(-1, 2, 100, 2700);
}

void ICACHE_FLASH_ATTR pumpState_OnManual(void) {
	publishAlarm(5, _pumpState); // Warn when going Manual ON
	_pumpState = MANUAL_ON;
	startMultiFlash(-1, 2, 700, 900);
}

void ICACHE_FLASH_ATTR pumpState_OffAuto(void) {
	_pumpState = AUTO_OFF;
	startMultiFlash(-1, 1, 100, 2900);
}

void ICACHE_FLASH_ATTR pumpState_OnAuto(void) {
	_pumpState = AUTO_ON;
	startMultiFlash(-1, 1, 700, 2300);
}
