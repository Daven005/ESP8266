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
#include "IOdefs.h"
#include "debug.h"
#include "mqtt.h"
#include "flowMonitor.h"
#include "pump.h"

#include "io.h"
#include "sysCfg.h"
#include "user_conf.h"

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
		startActionMultiFlash(-1, 1, 50, 1950); // Pump Off Normal
		break;
	case PUMP_ON_NORMAL:
		startActionMultiFlash(-1, 2, 500, 1000); // Pump On Normal
		break;
	case PUMP_OFF_OVERRIDE:
		startActionMultiFlash(-1, 3, 100, 2000); // Pump Off Override
		break;
	case PUMP_ON_OVERRIDE :
		startActionMultiFlash(-1, 5, 100, 2000);// Pump On Override
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

static void ICACHE_FLASH_ATTR flowCheck(void) {
	if (flowAverageReading() <= 1) {
		switch (pumpState()) {
		case PUMP_OFF_NORMAL:
		case PUMP_OFF_OVERRIDE:
			break;
		case PUMP_ON_NORMAL:
			if (secondsNotFlowing() > 10) {
				stopPumpOverride();
				publishAlarm(1, flowCurrentReading());
			}
			break;
		case PUMP_ON_OVERRIDE:
			if (secondsNotFlowing() > 30) {
				stopPumpOverride();
				publishAlarm(2, flowCurrentReading());
			}
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
		startCheckIsFlowing();
		break;
	case PUMP_ON_NORMAL:
		break;
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_OVERRIDE :
		return;
	}
}

void ICACHE_FLASH_ATTR startPumpOverride(void) {
	switch (pumpState()) {
	case PUMP_OFF_NORMAL:
	case PUMP_OFF_OVERRIDE:
	case PUMP_ON_NORMAL:
		TESTP("Start Pump with override\n");
		overrideSetOutput(OP_PUMP, 1);
		startCheckIsFlowing();
		break;
	case PUMP_ON_OVERRIDE :
		return;
	}
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
}

void ICACHE_FLASH_ATTR processPump(void) { // 5 Secs
	bool runPump = false;
	int tempBottom = mappedTemperature(MAP_TEMP_TS_BOTTOM);
	int tempSupply = mappedTemperature(MAP_TEMP_SUPPLY);
	int tempPanel = mappedTemperature(MAP_TEMP_PANEL);
	float newSupplyTemperature = mappedFloatTemperature(MAP_TEMP_SUPPLY);

	if (tempBottom == -99) tempBottom = 60; // Use default if not yet received it
	flowCheck();
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
		if (runPump) startPumpNormal(); else stopPumpNormal();
		break;
	case PUMP_ON_NORMAL:
		// Panel is still hot enough to get started
		runPump = tempPanel > (tempBottom + sysCfg.settings[SET_PANEL_TEMP]);
		// Supply temperature above tank
		runPump |= (tempSupply > (1 + tempBottom));
		// Keep running if supply temperature is still increasing
		runPump |= (newSupplyTemperature > lastSupplyTemperature);
		lastSupplyTemperature = newSupplyTemperature;
		if (runPump) startPumpNormal(); else stopPumpNormal();
		break;
	case PUMP_OFF_OVERRIDE:
		stopPumpOverride();
		break;
	case PUMP_ON_OVERRIDE :
		startPumpOverride();
		break;
	}
	checkPumpActionFlash();
}

void ICACHE_FLASH_ATTR initPump(void) {
}
