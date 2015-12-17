#include <c_types.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include <ds18b20.h>
#include "stdout.h"
#include "config.h"
#include "user_config.h"

extern uint8 currentInputs[4];
bool checkSetOutput(uint8 op, bool set);
extern void publishError(uint8 err, int info);
extern int mappedTemperature(uint8 name);
static enum { DHW_AUTO = 0, DHW_OFF, DHW_ON } dhwOverride;
static uint8 currentHour;

extern void  startFlash(int t, int repeat);
extern void stopFlash(void);

static bool ICACHE_FLASH_ATTR dhwOn(void) {
	if (dhwOverride == DHW_AUTO) {
		return (sysCfg.settings[SETTING_DHW_ON_HOUR] <= currentHour
				&& currentHour <= sysCfg.settings[SETTING_DHW_OFF_HOUR]);
	}
	return (dhwOverride == DHW_ON);
}

void ICACHE_FLASH_ATTR boilerSwitchAction(void) {
	switch (dhwOverride) {
	case DHW_AUTO:
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

void ICACHE_FLASH_ATTR boilerSetCurrentTime(uint8 hour, uint8 minute) {
	currentHour = hour;
	os_printf("%02d:%02d (%d-%d)\n", hour, minute, sysCfg.settings[SETTING_DHW_ON_HOUR],
			sysCfg.settings[SETTING_DHW_OFF_HOUR]);
}

bool ICACHE_FLASH_ATTR setOutputs(
		uint8 tHigh, uint8 sHigh, uint8 tLow, uint8 sLow, bool obIsOn, const uint8 f[]) {
	uint8 idx = 0;
	uint8 selection, temp1, temp2;

	temp1 = mappedTemperature(tHigh);
	if (temp1 <= 0)  {
		publishError(5, temp1);
		return false;
	}
	if (temp1 < sysCfg.settings[sHigh]) idx |= 1;
	temp2 = mappedTemperature(tLow);
	if (temp2 <= 0)  {
		publishError(6, temp2);
		return false;
	}
	if (temp2 > sysCfg.settings[sLow]) idx |= 0b10;
	if (obIsOn) idx |= 0b100;
	selection = f[idx];
	if (selection & 0b100) {
		publishError(3, idx);
		os_printf("%d(%d) %d(%d)\n", temp1, sysCfg.settings[sHigh], temp2, sysCfg.settings[sLow]);
	}
	checkSetOutput(OP_WB_CIRC_ON, selection & 1);
	return checkSetOutput(OP_OB_CIRC_ON, selection & 0b10);
}

void ICACHE_FLASH_ATTR checkControl(void) {
	bool CH_CallForHeat, WBisCool, boilerIsOn, radsOn;
	int temp;

	boilerIsOn = false;
	temp = mappedTemperature(MAP_WB_FLOW_TEMP);
	if (temp <= 0) {
		publishError(4, temp);
		return; // Problem
	}
	WBisCool = (temp < SETTING_WB_IS_ON_TEMP);
	CH_CallForHeat = currentInputs[IP_CH_ON];
	radsOn = currentInputs[IP_RADS_ON];
	if (WBisCool) {
		if (CH_CallForHeat) {
			const uint8 f[8] = { 0b01, 0b10, 0b01, 0b100, 0b10, 0b10, 0b01, 0b100 };
			uint8 setting;
			if (radsOn)
				setting = SETTING_RADS_START_BOILER;
			else
				setting = SETTING_CH_START_BOILER;
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_MIDDLE, setting,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		} else { // No CH
			const uint8 f[8] = { 0b00, 0b10, 0b00, 0b100, 0b00, 0b10, 0b10, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_TOP, SETTING_DHW_START_BOILER,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		}
	} else { // WB is Hot
		if (CH_CallForHeat) {
			const uint8 f[8] = { 0b01, 0b11, 0b01, 0b100, 0b01, 0b10, 0b01, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_MIDDLE, SETTING_DHW_USE_ALL_HEAT,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		} else { // No CH
			const uint8 f[8] = { 0b00, 0b10, 0b00, 0b100, 0b00, 0b10, 0b00, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_TOP, SETTING_DHW_USE_ALL_HEAT,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		}
	}
}
