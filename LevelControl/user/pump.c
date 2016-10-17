/*
 * pump.c
 *
 *  Created on: 7 Oct 2016
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
#include "LevelSignal.h"
#include "flowMonitor.h"
#include "debug.h"
#include "sounder.h"
#include "pump.h"

static pumpState_t _pumpState;
static uint16 currentPressure;
static int pumpOnCount = 0;
static bool overridePressure = false;

void ICACHE_FLASH_ATTR initPump(void) {
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(PUMP, 0);
	_pumpState = AUTO_OFF;
}

void ICACHE_FLASH_ATTR overrideSetPressure(uint16 p) {
	currentPressure = p;
	overridePressure = true;
}

void ICACHE_FLASH_ATTR overrideClearPressure(void) {
	overridePressure = false;
}

static void ICACHE_FLASH_ATTR pumpState_OffManual(void) {
	_pumpState = MANUAL_OFF;
	startMultiFlash(-1, 2, 100, 2700);
}

static void ICACHE_FLASH_ATTR pumpState_OnManual(void) {
	_pumpState = MANUAL_ON;
	startMultiFlash(-1, 2, 700, 900);
}

static void ICACHE_FLASH_ATTR pumpState_OffAuto(void) {
	_pumpState = AUTO_OFF;
	startMultiFlash(-1, 1, 100, 2900);
}

static void ICACHE_FLASH_ATTR pumpState_OnAuto(void) {
	_pumpState = AUTO_ON;
	startMultiFlash(-1, 1, 700, 2300);
}

void ICACHE_FLASH_ATTR setPump_Manual(void) { // Pump On/OFF
	sounderClear();
	startCheckIsFlowing();
	switch (_pumpState) {
	case AUTO_OFF:
	case AUTO_ON:
	case MANUAL_OFF:
		startCheckIsFlowing();
		pumpState_OnManual();
		break;
	case MANUAL_ON:
		pumpState_OffManual();
		break;
	}
	TESTP("Pump->%d\n", _pumpState);
	processPump();
}

void ICACHE_FLASH_ATTR setPump_Auto(void) {
	sounderClear();
	pumpState_OffAuto();
	TESTP("Pump->%d\n", _pumpState);
	processPump();
}

static void ICACHE_FLASH_ATTR checkNotFlowing(void) {
	static isflowingCount = 0;

	if (flowCurrentReading() > 1) {
		isflowingCount++;
		if (isflowingCount > 3) {
			publishAlarm(3, flowCurrentReading()); // Still Flowing when pump off
		}
	} else {
		isflowingCount = 0;
	}
}

void ICACHE_FLASH_ATTR processPump(void) { // Every 100mS
	uint16 newPressure = system_adc_read();
	if (!overridePressure) {
		currentPressure = (currentPressure * 9 + newPressure)/10; // Average over 1 second
	}
	switch (_pumpState) {
	case MANUAL_OFF:
		easygpio_outputSet(PUMP, 0);
		pumpOnCount = 0;
		checkNotFlowing();
		break;
	case MANUAL_ON:
		if (secondsNotFlowing() > 30) {
			pumpState_OffManual();
			easygpio_outputSet(PUMP, 0);
			sounderAlarm(3); // No flow for 30S in Manual
		} else {
			easygpio_outputSet(PUMP, 1);
		}
		break;
	case AUTO_OFF:
		if (currentPressure < sysCfg.settings[SET_PUMP_ON]) {
			startCheckIsFlowing();
			pumpState_OnAuto();
			easygpio_outputSet(PUMP, 1);
		} else {
			easygpio_outputSet(PUMP, 0);
			pumpOnCount = 0;
			checkNotFlowing();
		}
		sounderClear();
		break;
	case AUTO_ON:
		if (currentPressure > sysCfg.settings[SET_PUMP_OFF]) {
			pumpState_OffAuto();
			easygpio_outputSet(PUMP, 0);
		} else {
			if (secondsNotFlowing() > 10) {
				publishAlarm(1, flowCurrentReading()); // No flow for 10S in Auto
				sounderAlarm(1);
				pumpState_OffManual();
			} else {
				pumpOnCount++;
				if (pumpOnCount >= MAX_PUMP_ON_WARNING
						&& currentPressure < sysCfg.settings[SET_LOW_PRESSURE_WARNING]) {
					publishError(3, currentPressure);
				}
				if (pumpOnCount == MAX_PUMP_ON_WARNING) { // 1 minute
					publishError(1, pumpOnCount);
				} else if (pumpOnCount == MAX_PUMP_ON_ERROR) { // 5 minutes
					publishAlarm(2, pumpOnCount); // Running for 5 Minutes in Auto
					sounderAlarm(2);
				} else if (pumpOnCount > MAX_PUMP_ON_ERROR) {
					pumpOnCount = MAX_PUMP_ON_ERROR + 1;
				}
			}
		}
		break;
	}
}

uint16 getCurrentPressure(void) {
	return currentPressure;
}

uint16 getPumpOnCount(void){
	return pumpOnCount;
}

pumpState_t ICACHE_FLASH_ATTR pumpState(void) {
	return _pumpState;
}
