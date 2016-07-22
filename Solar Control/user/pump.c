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
#include "mqtt.h"
#include "pump.h"

static os_timer_t flowCheck_timer;
static uint8  cloud[3] = { 10, 10, 10 };
typedef struct { int sunAzimuth; int sunAltitdude; } sunPosition_t;
static sunPosition_t sunPosition[3];

static float lastSupplyTemperature = 0;

static void ICACHE_FLASH_ATTR checkPumpActionFlash(void) {
	static pumpState_t lastPumpState = PUMP_UNKNOWN;
	pumpState_t thisPumpState = pumpState();
	static bool lastConfigMode;

	if (switchInConfigMode()) { // Don't flash in Config Mode
		easygpio_outputSet(ACTION_LED, 1);
		lastConfigMode = true;
		return;
	} else {
		easygpio_outputSet(ACTION_LED, 0); // If flashing in progress this will be overridden
	}
	// Don't restart Action Flash unless configMode switch just turned off
	if (lastConfigMode == false) {
		if (thisPumpState == lastPumpState) return;
		lastPumpState = thisPumpState;
	}
	lastConfigMode = false;
	TESTP("Pump state: %d. ", thisPumpState);
	switch (thisPumpState) {
	case PUMP_OFF_NORMAL:
		startActionFlash(-1, 200, 1800);
		break;
	case PUMP_ON_NORMAL:
		startActionFlash(-1, 1800, 200);
		break;
	case PUMP_OFF_OVERRIDE:
		startActionFlash(-1, 1000, 2000);
		break;
	case PUMP_ON_OVERRIDE :
		startActionFlash(-1, 2000, 1000);
		break;
	}
}

void ICACHE_FLASH_ATTR stopPumpOverride(void);

void ICACHE_FLASH_ATTR setCloud(int idx, int c) {
	if (0 <= idx && idx < sizeof(cloud)) {
		if (0 <= c && c <= 10) {
			cloud[idx] = c;
		}
	}
}

void ICACHE_FLASH_ATTR setSun(int idx, int az, int alt) {
	TESTP("Sun[%d] az:%d alt:%d", idx, az, alt);
	if (idx >= 3) return;
	if (-90 <= az && az <= 90) sunPosition[idx].sunAzimuth = az;
	if (-45 <= alt && alt <= 45) sunPosition[idx].sunAltitdude = alt;
}

bool ICACHE_FLASH_ATTR sunnyEnough(void) {
	if (cloud[0] <= 7 || cloud[1] <= 7) {
		if (-40 <= sunPosition[0].sunAltitdude && sunPosition[0].sunAltitdude <= 40)
			return true;
		if (-40 <= sunPosition[1].sunAltitdude && sunPosition[1].sunAltitdude <= 40)
			return true;
	}
	return false;
}

static void ICACHE_FLASH_ATTR flowCheck_cb(uint32_t *args) {
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

void ICACHE_FLASH_ATTR turnOffOverride(void) {
	clearPumpOverride();
	checkPumpActionFlash();
}

static void ICACHE_FLASH_ATTR startPumpNormal(void) {
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
	os_timer_arm(&flowCheck_timer, PROCESS_REPEAT-2000, 0); // Allow time for temp readings
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

static void ICACHE_FLASH_ATTR stopPumpNormal(void) {
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
}

void ICACHE_FLASH_ATTR processPump(void) {
	static pumpDelayOn = 0;
	static pumpDelayOff = 0;
	bool runPump = false;
	int tempBottom = mappedTemperature(MAP_TEMP_TS_BOTTOM);
	int tempSupply = mappedTemperature(MAP_TEMP_SUPPLY);
	int tempPanel = mappedTemperature(MAP_TEMP_PANEL);

	if (tempBottom == -99) tempBottom = 60; // Use default if not yet received it

	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
		if (tempSupply > tempBottom) {
			// Already heat in supply pipe
			runPump = tempPanel
					> (tempBottom + sysCfg.settings[SET_PANEL_TEMP]);
		} else {
			// Get extra heat to warm pipe
			runPump = tempPanel
					> (tempBottom + 2 * sysCfg.settings[SET_PANEL_TEMP]);
		}
		break;
	case PUMP_ON_NORMAL:
		do { // Keep pumping until supply gets 'colder'
			float newSupplyTemperature = mappedFloatTemperature(MAP_TEMP_SUPPLY);
			// Panel is still hot enough to get started
			runPump = tempPanel
					> (tempBottom + sysCfg.settings[SET_PANEL_TEMP]);
			// Supply temperature above tank
			runPump |= (tempSupply
					> (1 + tempBottom));
			// Keep running if supply temperature is still increasing
			runPump |= (newSupplyTemperature > lastSupplyTemperature);
			lastSupplyTemperature = newSupplyTemperature;
		} while (false);
		break;
	case PUMP_OFF_OVERRIDE:
		stopPumpOverride();
		return;
	case PUMP_ON_OVERRIDE :
		startPumpOverride();
		return;
	}
	if (runPump) {
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
	checkPumpActionFlash();
}

void ICACHE_FLASH_ATTR initPump(void) {
	os_timer_disarm(&flowCheck_timer);
	os_timer_setfn(&flowCheck_timer, (os_timer_func_t *) flowCheck_cb, (void *) 0);
}
