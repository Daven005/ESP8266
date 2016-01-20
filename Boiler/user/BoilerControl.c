#include <c_types.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include <ds18b20.h>
#include "stdout.h"
#include "config.h"
#include "user_config.h"
#include "io.h"
#include "time.h"
#include "timezone.h"
#include "temperature.h"
#include "debug.h"

static enum { DHW_AUTO = 0, DHW_AUTO_MAX, DHW_OFF, DHW_ON } dhwOverride;
static uint8 currentHour;
static time_t currentTime;

LOCAL os_timer_t Boost_timer;
LOCAL os_timer_t OB_firing_timer;
LOCAL os_timer_t TemperatureMonitor_timer;
static enum { OB_OFF, OB_STARTING, OB_ON, OB_PURGING } firingState = OB_OFF;
static bool boostInProgress = false;;
static bool boostHadCFH = false;;
static int boostCount = 0;

static uint8 radsOffCount = 0;
static uint8 dhwWarningCount = 0;
static uint8 chWarningCount = 0;

extern void publishError(uint8 err, int info);
extern void startFlash(int t, int repeat);
extern void stopFlash(void);

static bool ICACHE_FLASH_ATTR dhwOn(void) {
	if (dhwOverride == DHW_AUTO || dhwOverride == DHW_AUTO_MAX) {
		return (sysCfg.settings[SETTING_DHW_ON_HOUR] <= currentHour
				&& currentHour <= sysCfg.settings[SETTING_DHW_OFF_HOUR]);
	}
	return (dhwOverride == DHW_ON);
}

static bool ICACHE_FLASH_ATTR dhwMax(void) {
	return (dhwOverride == DHW_ON || dhwOverride == DHW_AUTO_MAX);
}

void ICACHE_FLASH_ATTR printDHW() {
	struct tm *tm;
	tm = localtime(&currentTime);
	applyDST(tm);
	os_printf("\nDHW (%d) time: %02d:%02d (%d-%d)\n",
			dhwOverride, tm->tm_hour, tm->tm_min,
			sysCfg.settings[SETTING_DHW_ON_HOUR], sysCfg.settings[SETTING_DHW_OFF_HOUR]);
}

void printBCinfo(void) {
	os_printf("Boost: InProgress %d, hadCFH %d, Count %d\n", boostInProgress, boostHadCFH, boostCount);
	os_printf("Monitor: radsOff %d, dhwWarning %d, chWarning %d\n", radsOffCount, dhwWarningCount, chWarningCount);
}

void ICACHE_FLASH_ATTR boilerSwitchAction(void) {
	switch (dhwOverride) {
	case DHW_AUTO:
		dhwOverride = DHW_AUTO_MAX;
		startFlash(100, true);
		break;
	case DHW_AUTO_MAX:
		dhwOverride = DHW_OFF;
		startFlash(1000, true);
		break;
	case DHW_OFF:
		dhwOverride = DHW_ON;
		startFlash(200, true);
		break;
	case DHW_ON:
		dhwOverride = DHW_AUTO;
		stopFlash();
		break;
	}
}

static void ICACHE_FLASH_ATTR boilerSetCurrentTime(uint8 hour, uint8 minute) {
	currentHour = hour;
}

void ICACHE_FLASH_ATTR setTime(time_t t) {
	struct tm *tm;

	currentTime = t;
	tm = localtime(&t);
	applyDST(tm);
	boilerSetCurrentTime(tm->tm_hour, tm->tm_min);
}

void ICACHE_FLASH_ATTR setOutsideTemp(int idx, int val){
	if (idx == 2) { // Choose forecast +2 hours
		setUnmappedTemperature("Outside", val, 0);
	}
}

void ICACHE_FLASH_ATTR incTime(void) { // By 1S
	setTime(currentTime + 1);
}

void ICACHE_FLASH_ATTR firing_cb(void) {
	TESTP("OB starting complete\n");
	firingState = OB_ON;
	checkSetOutput(OP_OB_CIRC_ON, true);
}

void ICACHE_FLASH_ATTR OB_processFiring(bool OBrequest) {
	switch (firingState) {
	case OB_OFF:
		if (OBrequest) {
			TESTP("OB firing started\n");
			firingState = OB_STARTING;
			checkSetOutput(OP_OB_ON, true);
			os_timer_disarm(&OB_firing_timer);
			os_timer_setfn(&OB_firing_timer, (os_timer_func_t *) firing_cb, (void *) 0);
			os_timer_arm(&OB_firing_timer, sysCfg.settings[SETTING_OB_PUMP_DELAY]*1000, false);
		}
		break;
	case OB_STARTING:
		break; // waiting for firing_cb; ie delay pump until boiler up to temperature
	case OB_ON:
		if (!OBrequest) { // Turn off
			TESTP("OB purging started\n");
			firingState = OB_PURGING;
			checkSetOutput(OP_OB_ON, false);
		}
		break;
	case OB_PURGING:
		if (OBrequest) { // Turn Back ON, no delays
			TESTP("OB purging restarted\n");
			firingState = OB_ON;
			checkSetOutput(OP_OB_ON, true);
			checkSetOutput(OP_OB_CIRC_ON, true);
		} else { // Keep off until OB Flow temperatures drop
			int OBflow, heatingReturn, TSmiddle;
			OBflow = mappedTemperature(MAP_OB_FLOW_TEMP);
			heatingReturn = mappedTemperature(MAP_HEATING_RETURN_TEMP);
			TSmiddle = mappedTemperature(MAP_TEMP_TS_MIDDLE);
			if (OBflow < (heatingReturn + 0) || OBflow < (TSmiddle + 0)) {
				checkSetOutput(OP_OB_ON, false);
				checkSetOutput(OP_OB_CIRC_ON, false);
				firingState = OB_OFF;
				TESTP("OB purging complete (%d < %d+2) || (%d < %d+2)\n", OBflow, heatingReturn, OBflow, TSmiddle);
			}
		}
	}
}

uint8 ICACHE_FLASH_ATTR checkCH_Logic(WB_IsHot, OB_IsOn, CH_CallForHeat, TS_MiddleHigh, TS_BottomHigh) {
	static const uint8 chLogic[] = {
			0b00, 0b00, 0b00, 0b00, 0b10, 0b01, 0b01, 0b01,
			0b10, 0b10, 0b00, 0b00, 0b10, 0b10, 0b10, 0b01,
			0b00, 0b00, 0b00, 0b00, 0b10, 0b01, 0b01, 0b01,
			0b00, 0b00, 0b00, 0b00, 0b10, 0b00, 0b00, 0b01 };
	uint8 result, idx = 0;
	static uint8 oldIdx = 0;

	if (WB_IsHot)
		idx |= 0b10000;
	if (OB_IsOn)
		idx |= 0b1000;
	if (CH_CallForHeat)
		idx |= 0b100;
	if (TS_MiddleHigh)
		idx |= 0b10;
	if (TS_BottomHigh)
		idx |= 0b1;
	result = chLogic[idx];
	if (idx != oldIdx) {
		TESTP("ch %x->%x = %d\n", oldIdx, idx, result);
		oldIdx = idx;
	}
	return (result);
}

uint8 ICACHE_FLASH_ATTR checkDHW_Logic(WB_IsHot, OB_IsOn, DHW_CallForHeat, TS_TopHigh, TS_MiddleHigh) {
	static const uint8 dhwLogic[] = {
			0b0, 0b0, 0b0, 0b0, 0b1, 0b1, 0b0, 0b0,
			0b1, 0b1, 0b0, 0b0, 0b1, 0b1, 0b1, 0b0,
			0b0, 0b0, 0b0, 0b0, 0b1, 0b0, 0b0, 0b0,
			0b1, 0b0, 0b0, 0b0, 0b1, 0b1, 0b1, 0b0 };
	uint8 idx = 0;
	static uint8 oldIdx = 0;
	if (WB_IsHot)
		idx |= 0b10000;
	if (OB_IsOn)
		idx |= 0b1000;
	if (DHW_CallForHeat)
		idx |= 0b100;
	if (TS_TopHigh)
		idx |= 0b10;
	if (TS_MiddleHigh)
		idx |= 0b1;
	if (idx != oldIdx) {
		TESTP("dhw %x->%x = %d\n", oldIdx, idx, dhwLogic[idx]);
		oldIdx = idx;
	}
	return dhwLogic[idx];
}

void ICACHE_FLASH_ATTR boost_cb(void) {
	if (boostInProgress) {
		if (boostHadCFH) {
			if (++boostCount > 100) {
				boostCount = 100;
			}
		} else {
			if ((--boostCount) < 0) {
				boostCount = 0;
			}
		}
	} else {
		os_timer_disarm(&Boost_timer);
		boostCount = 0;
	}
	TESTP("Boost %d\n", boostCount);
}

void ICACHE_FLASH_ATTR validateOutputs(void) {
	if (output(OP_OB_ON) && output(OP_EMERGENCY_DUMP_ON)) {
		publishError(80, 0); // Shouldn't have OB ON when emergency dump
	}
	if (output(OP_OB_ON) && output(OP_WB_CIRC_ON) && output(OP_OB_CIRC_ON)) {
		publishError(81, 0); // Shouldn't have OB and both circulations ON
		// Note circulations on when doing Thermal purge
	}
}

void ICACHE_FLASH_ATTR checkControl(void) {
#define MIN_COMP_TEMP 5
#define MAX_COMP_TEMP 20
#define RADS_OFF_DURATION 10

	bool CH_CallForHeat, WBisHot;
	static bool OB_IsOn = false;
	uint8 chResult, dhwResult;
	int tempTS_Top, tempWB_Flow;
	bool checkBoost;
	static int boostCount = 0;
	int baseSetPoint, modifiedSetPoint;
	uint8 sensorCount = temperatureSensorCount();
	int dhwSetPoint;
	int chSetPoint;

	if (sensorCount < REQUIRED_SENSORS) {
		publishError(98, sensorCount); // Sensor(s) Missing
		return;
	}

	if (input(IP_RADS_ON)) {
		radsOffCount = RADS_OFF_DURATION; // Flag that Rads have been on
	}

	// Check TS overheat
	tempTS_Top = mappedTemperature(MAP_TEMP_TS_TOP);
	if (tempTS_Top <= 0) {
		publishError(91, tempTS_Top); // Invalid TS_TOP temperature
		tempTS_Top = 0;
	}
	if (tempTS_Top >= sysCfg.settings[SETTING_EMERGENCY_DUMP_TEMP]) {
		TESTP("Emerg %d >= %d?\n", tempTS_Top, sysCfg.settings[SETTING_EMERGENCY_DUMP_TEMP]);
		publishError(99, tempTS_Top); // Emergency Heat dump
		checkSetOutput(OP_EMERGENCY_DUMP_ON, true);
	} else {
		checkSetOutput(OP_EMERGENCY_DUMP_ON, false);
	}

	CH_CallForHeat = input(IP_UFH_ON);

	tempWB_Flow = mappedTemperature(MAP_WB_FLOW_TEMP);
	if (tempWB_Flow < 0) {
		publishError(40, tempWB_Flow); // Invalid WB_FLOW temperature
		tempWB_Flow = 0;
	}
	WBisHot = (tempWB_Flow >= sysCfg.settings[SETTING_WB_IS_ON_TEMP]);

	setUnmappedTemperature("DHW setpoint", sysCfg.settings[SETTING_DHW_SET_POINT], 0);

	// Check CH set point
	if (radsOffCount > 0) { // Rads have been ON in last 10 minutes
		baseSetPoint = sysCfg.settings[SETTING_RADS_SET_POINT];
	} else {
		baseSetPoint = sysCfg.settings[SETTING_UFH_SET_POINT];
	}
	if (mappedTemperature(MAP_OUTSIDE_TEMP) <= MIN_COMP_TEMP) {
		uint8 idx = setUnmappedTemperature("CH setpoint", baseSetPoint, 0);
		INFO(extraPublishTemperatures(idx));
		checkBoost = false; // Already on max output
	} else if (mappedTemperature(MAP_OUTSIDE_TEMP) >= MAX_COMP_TEMP) {
		modifiedSetPoint = baseSetPoint - sysCfg.settings[SETTING_OUTSIDE_TEMP_COMP];
		checkBoost = true;
	} else {
		modifiedSetPoint = baseSetPoint - ((mappedTemperature(MAP_OUTSIDE_TEMP) - MIN_COMP_TEMP)
								* sysCfg.settings[SETTING_OUTSIDE_TEMP_COMP]) / MAX_COMP_TEMP;
		checkBoost = true;
	}
	if (checkBoost) {
		if (boostInProgress) {
			if (CH_CallForHeat) {
				boostHadCFH = true;
			}
			modifiedSetPoint += sysCfg.settings[SETTING_BOOST_AMOUNT] * boostCount;
			if (modifiedSetPoint > baseSetPoint) {
				modifiedSetPoint = baseSetPoint;
			}
		} else {
			boostInProgress = true;
			boostCount = 0;
			os_timer_disarm(&Boost_timer);
			os_timer_setfn(&Boost_timer, (os_timer_func_t *) boost_cb, (void *) 0);
			os_timer_arm(&Boost_timer, sysCfg.settings[SETTING_BOOST_TIME] * 60 * 1000, true);
		}
		uint8 idx = setUnmappedTemperature("CH setpoint", modifiedSetPoint, 0);
		INFO(extraPublishTemperatures(idx));
	} else {
		boostInProgress = false;
	}

	chSetPoint = mappedTemperature(MAP_CURRENT_CH_SET_POINT);
	chResult = checkCH_Logic(WBisHot, OB_IsOn, CH_CallForHeat,
			mappedTemperature(MAP_HEATING_FLOW_TEMP) > chSetPoint,
			mappedTemperature(MAP_TEMP_TS_BOTTOM)
					> (chSetPoint + sysCfg.settings[SETTING_SET_POINT_DIFFERENTIAL]));

	dhwSetPoint = mappedTemperature(MAP_CURRENT_DHW_SET_POINT);
	dhwResult = checkDHW_Logic(WBisHot, OB_IsOn, dhwOn(),
			mappedTemperature(MAP_TEMP_TS_TOP) > dhwSetPoint,
			mappedTemperature(dhwMax() ? MAP_TEMP_TS_MIDDLE : MAP_TEMP_TS_CYLINDER) // Use lower sensor for more DHW
					> (dhwSetPoint + sysCfg.settings[SETTING_SET_POINT_DIFFERENTIAL]));

	if (dhwResult) {
		OB_IsOn = true;
		OB_processFiring(true);
		checkSetOutput(OP_WB_CIRC_ON, false);
	} else {
		OB_IsOn = (chResult >> 1);
		if (OB_IsOn) {
			checkSetOutput(OP_WB_CIRC_ON, false);
			OB_processFiring(true);
		} else {
			OB_processFiring(false);
			checkSetOutput(OP_WB_CIRC_ON, chResult & 0b1);
		}
	}
	validateOutputs();
}

void ICACHE_FLASH_ATTR TemperatureMonitor_cb(void) { // Every minute
	if (!input(IP_RADS_ON)) {
		if (radsOffCount > 0) {
			radsOffCount--;
		}
	}
	if (mappedTemperature(MAP_TEMP_TS_TOP) > (mappedTemperature(MAP_CURRENT_DHW_SET_POINT) - 10)) {
		dhwWarningCount = 10;
	} else {
		if (dhwWarningCount > 0) {
			dhwWarningCount--;
			if (dhwWarningCount == 0) {
				publishError(20, mappedTemperature(MAP_TEMP_TS_TOP)); // DHW low warning
			}
		}
	}
	if (mappedTemperature(MAP_TEMP_TS_MIDDLE) > (mappedTemperature(MAP_CURRENT_CH_SET_POINT) - 10)) {
		chWarningCount = 10;
	} else {
		if (chWarningCount > 0) {
			dhwWarningCount--;
			if (chWarningCount == 0) {
				publishError(21, mappedTemperature(MAP_TEMP_TS_MIDDLE)); // CH low warning
			}
		}
	}
}

void ICACHE_FLASH_ATTR initBoilerControl() {
	checkInputs(false);
	ds18b20StartScan();
	// Allocate derived unmapped temperatures
	setUnmappedTemperature("CH setpoint", 0, 0);
	setUnmappedTemperature("DHW setpoint", 0, 0);
	setUnmappedTemperature("Outside", 0, 0);
	os_timer_disarm(&TemperatureMonitor_timer);
	os_timer_setfn(&TemperatureMonitor_timer, (os_timer_func_t *) TemperatureMonitor_cb, (void *) 0);
	os_timer_arm(&TemperatureMonitor_timer, 60*1000, false);

}
