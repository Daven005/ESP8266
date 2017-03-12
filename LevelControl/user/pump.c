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
#include "io.h"
#include "sysCfg.h"

static int pumpOnCount = 0;

void ICACHE_FLASH_ATTR initPump(void) {
	easygpio_pinMode(PUMP, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(PUMP, 0);
	pumpState_OffAuto();
}

void ICACHE_FLASH_ATTR setPump_Manual(void) { // Pump On/OFF
	sounderClear();
	startCheckIsFlowing();
	switch (pumpState()) {
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
	TESTP("Pump->%d\n", pumpState());
	processPump();
}

void ICACHE_FLASH_ATTR setPump_Auto(void) {
	sounderClear();
	pumpState_OffAuto();
	TESTP("Pump->%d\n", pumpState());
	processPump();
}

static void ICACHE_FLASH_ATTR checkNotFlowing(void) {
	static isflowingCount = 0;

	if (flowCurrentReading() > 1) {
		isflowingCount++;
		if (isflowingCount > 20) {
			publishAlarm(3, flowCurrentReading()); // Still Flowing when pump off for 2S
		}
	} else {
		isflowingCount = 0;
	}
}

static void ICACHE_FLASH_ATTR checkLevel(void) {
	if (!(sysCfg.settings[SET_LOW_LEVEL_WARNING] < getLevel() && getLevel() < 1000)) {
		publishAlarm(10, getLevel()); // Tank level out of range
	}
}

void ICACHE_FLASH_ATTR processPump(void) { // Every 100mS
	updatePressure();
	checkLevel();
	switch (pumpState()) {
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
		if (getCurrentPressure() < sysCfg.settings[SET_PUMP_ON]) {
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
		if (getCurrentPressure() > sysCfg.settings[SET_PUMP_OFF]) {
			pumpState_OffAuto();
			easygpio_outputSet(PUMP, 0);
		} else {
			if (secondsNotFlowing() > sysCfg.settings[SET_NO_FLOW_AUTO_ERROR]) {
				publishAlarm(1, flowAverageReading()); // No flow for SET_NO_FLOW_AUTO_ERROR (10S) in Auto
				sounderAlarm(1);
				publishAlarm(6, secondsNotFlowing()); // No flow for SET_NO_FLOW_AUTO_ERROR (10S) in Auto
				pumpState_OffManual();
			} else {
				pumpOnCount++;
				if (pumpOnCount >= sysCfg.settings[SET_MAX_PUMP_ON_WARNING]
						&& getCurrentPressure() < sysCfg.settings[SET_LOW_PRESSURE_WARNING]) {
					publishError(3, getCurrentPressure()); // Low pressure and Pump On > SET_MAX_PUMP_ON_WARNING
				}
				if (pumpOnCount >= sysCfg.settings[SET_MAX_PUMP_ON_WARNING]) { // 1 minute
					publishError(1, sysCfg.settings[SET_MAX_PUMP_ON_WARNING]);
				} else if (pumpOnCount == sysCfg.settings[SET_MAX_PUMP_ON_ERROR]) { // 5 minutes
					publishAlarm(2, pumpOnCount); // Running for SET_MAX_PUMP_ON_ERROR (5 Minutes) in Auto
					sounderAlarm(2);
				} else if (pumpOnCount > sysCfg.settings[SET_MAX_PUMP_ON_ERROR]) {
					pumpOnCount = sysCfg.settings[SET_MAX_PUMP_ON_ERROR] + 1;
				}
			}
		}
		break;
	}
}

uint16 getPumpOnCount(void){
	return pumpOnCount;
}
