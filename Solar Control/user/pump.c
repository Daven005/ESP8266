/*
 * pump.c
 *
 *  Created on: 7 Jul 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include "overrideIO.h"
#include "IOdefs.h"
#include "config.h"
#include "user_config.h"
#include "debug.h"

static os_timer_t flowCheck_timer;
static uint8  cloud[3] = { 10, 10, 10 };
static int sunAzimuth, sunAltitdude;


void stopPumpOverride(void);

void ICACHE_FLASH_ATTR setCloud(int idx, int c) {
	if (0 <= idx && idx < sizeof(cloud)) {
		if (0 <= c && c <= 10) {
			cloud[idx] = c;
		}
	}
}

void ICACHE_FLASH_ATTR setSun(int az, int alt) {
	if (-180 <= az && az <= 180) sunAzimuth = az;
	if (-45 <= alt && alt <= 45) sunAltitdude = alt;
}

bool ICACHE_FLASH_ATTR sunnyEnough(void) {
	if (cloud[0] <= 7 || cloud[1] <= 7) {
		if (-40 <= sunAltitdude && sunAltitdude <= 40)
			return true;
	}
	return false;
}

void ICACHE_FLASH_ATTR flowCheck_cb(uint32_t *args) {
	TESTP("*");
	if (flowAverageReading() <= 1) {
		switch (pumpState()) {
		case PUMP_OFF_NORMAL:
		case PUMP_OFF_OVERRIDE:
			break;
		case PUMP_ON_NORMAL:
			stopPumpOverride();
			publishAlarm(1, flowCurrentReading());
			break;
		case PUMP_ON_OVERRIDE:
			publishAlarm(2, flowCurrentReading());
			break;
		}
	}
}

void ICACHE_FLASH_ATTR turnOffOverride() {
	clearPumpOverride();
	checkActionFlash();
}

void ICACHE_FLASH_ATTR startPumpNormal(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
		TESTP("Start Pump\n");
		checkSetOutput(OP_PUMP, 1);
		break;
	case PUMP_ON_NORMAL:
		break;
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_OVERRIDE :
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	os_timer_arm(&flowCheck_timer, PROCESS_REPEAT-500, 0);
}

void ICACHE_FLASH_ATTR startPumpOverride(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_NORMAL:
		TESTP("Start Pump with override\n");
		overrideSetOutput(OP_PUMP, 1);
		break;
	case PUMP_ON_OVERRIDE :
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	os_timer_arm(&flowCheck_timer, 100000, 0); // Allow to run for 10S if override
}

void ICACHE_FLASH_ATTR stopPumpNormal(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_OVERRIDE :
		return;
	case PUMP_ON_NORMAL:
		TESTP("Stop Pump\n");
		checkSetOutput(OP_PUMP, 0);
		break;
	}
	os_timer_disarm(&flowCheck_timer);
}

void ICACHE_FLASH_ATTR stopPumpOverride(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_ON_OVERRIDE :
	case PUMP_ON_NORMAL:
		TESTP("Stop Pump with override\n");
		overrideSetOutput(OP_PUMP, 0);
		break;
	case PUMP_OFF_OVERRIDE:
		return;
	}
	os_timer_disarm(&flowCheck_timer);
	overrideSetOutput(OP_PUMP, 0);
}

void ICACHE_FLASH_ATTR processPump(void) {
	static pumpDelayOn = 0;
	static pumpDelayOff = 0;
	bool startPump = false;

	printFlows();
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
		if (mappedTemperature(MAP_TEMP_SUPPLY) > mappedTemperature(MAP_TEMP_TS_BOTTOM)) {
			// Already heat in supply pipe
			startPump = mappedTemperature(MAP_TEMP_PANEL)
					> (mappedTemperature(MAP_TEMP_TS_BOTTOM) + sysCfg.settings[SET_PANEL_TEMP]);
		} else {
			// Get extra heat to warm pipe
			startPump = mappedTemperature(MAP_TEMP_PANEL)
					> (mappedTemperature(MAP_TEMP_TS_BOTTOM) + 2 * sysCfg.settings[SET_PANEL_TEMP]);
		}
		break;
	case PUMP_ON_NORMAL:
		// Keep pumping until supply gets 'colder'
		startPump = mappedTemperature(MAP_TEMP_SUPPLY) > (1+mappedTemperature(MAP_TEMP_TS_BOTTOM));
		break;
	case PUMP_OFF_OVERRIDE:
		stopPumpOverride();
		return;
	case PUMP_ON_OVERRIDE :
		startPumpOverride();
		return;
	}
	if (startPump) {
		pumpDelayOff = 0;
		if (pumpDelayOn < sysCfg.settings[SET_PUMP_DELAY]) {
			pumpDelayOn++;
		} else {
			startPumpNormal();
		}
	} else {
		pumpDelayOn = 0;
		if (pumpDelayOff < sysCfg.settings[SET_PUMP_DELAY]) {
			pumpDelayOff++;
		} else {
			stopPumpNormal();
		}
	}
	checkActionFlash();
}

void ICACHE_FLASH_ATTR initPump(void) {
	os_timer_disarm(&flowCheck_timer);
	os_timer_setfn(&flowCheck_timer, (os_timer_func_t *) flowCheck_cb, (void *) 0);
}
