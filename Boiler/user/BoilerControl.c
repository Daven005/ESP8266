#include <ds18b20.h>

#include "stdout.h"

#include "config.h"
#include "user_config.h"

extern uint8 currentInputs[4];
bool ICACHE_FLASH_ATTR checkSetOutput(uint8 op, bool set);

extern int ICACHE_FLASH_ATTR mappedTemperature(uint8 name);

bool ICACHE_FLASH_ATTR setOutputs(uint8 tHigh, uint8 sHigh, uint8 tLow, uint8 sLow, bool obIsOn, const uint8 f[]) {
	uint8 idx = 0;
	uint8 temp, temp1, temp2;

	temp1 = mappedTemperature(tHigh);
	if (temp1 <= 0) return 0;
	if (temp1 < sysCfg.settings[sHigh]) idx |= 1;
	temp2 = mappedTemperature(tLow);
	if (temp2 <= 0) return 0;
	if (temp2 > sysCfg.settings[sLow]) idx |= 0b10;
	if (obIsOn) idx |= 0b100;
	temp = f[idx];
	if (temp & 0b100) {
		publishError(3, idx);
		os_printf("%d(%d) %d(%d)\n", temp1, sysCfg.settings[sHigh], temp2, sysCfg.settings[sLow]);
	}
	checkSetOutput(OP_WB_CIRC_ON, temp & 1);
	return checkSetOutput(OP_OB_CIRC_ON, temp & 0b10);
}

void ICACHE_FLASH_ATTR checkControl(void) {
	bool NoCallForHeat, WBisCool, boilerIsOn;
	int temp;

	boilerIsOn = false;
	temp = mappedTemperature(MAP_WB_FLOW_TEMP);
	if (temp <= 0) return; // Problem
	WBisCool = (temp < SETTING_WB_IS_ON_TEMP);
	NoCallForHeat = !currentInputs[IP_CALL_FOR_HEAT];
	if (WBisCool) {
		if (NoCallForHeat) {
			const uint8 f[8] = { 0b00, 0b10, 0b00, 0b100, 0b00, 0b10, 0b10, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_TOP, SETTING_DHW_START_BOILER,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		} else {
			const uint8 f[8] = { 0b01, 0b10, 0b01, 0b100, 0b11, 0b11, 0b01, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_MIDDLE, SETTING_DHW_START_BOILER,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		}
	} else { // WB is Hot
		if (NoCallForHeat) {
			const uint8 f[8] = { 0b00, 0b10, 0b00, 0b100, 0b00, 0b10, 0b10, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_TOP, SETTING_DHW_USE_ALL_HEAT,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);

		} else {
			const uint8 f[8] = { 0b01, 0b11, 0b01, 0b100, 0b01, 0b11, 0b01, 0b100 };
			boilerIsOn = setOutputs(
					MAP_TEMP_TS_MIDDLE, SETTING_DHW_USE_ALL_HEAT,
					MAP_TEMP_TS_BOTTOM, SETTING_DHW_STOP_BOILER,
					boilerIsOn, f);
		}
	}
}
