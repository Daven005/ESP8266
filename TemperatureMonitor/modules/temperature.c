/*
 * temperature.c
 *
 *  Created on: 19 Dec 2015
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <uart.h>
#include <ds18b20.h>
#include "temperature.h"
#include "config.h"
#include "debug.h"

static struct Temperature temperature[MAX_TEMPERATURE_SENSOR];
LOCAL os_timer_t ds18b20_timer;

uint8 ICACHE_FLASH_ATTR temperatureSensorCount(void) {
	uint8 idx, sensors = 0;
	for (idx = 0; idx < MAX_DS18B20_SENSOR; idx++) {
		if (temperature[idx].set) {
			if (temperature[idx].missed < 5) {
				sensors++;
			} else {
				publishError(97, idx); // Know temperature sensor missed for 5 readings
			}
		}
	}
	return sensors;
}

static void ICACHE_FLASH_ATTR incMissed(void) {
	uint8 idx;
	for (idx = 0; idx < MAX_DS18B20_SENSOR; idx++) {
		if (temperature[idx].missed < 250) {
			temperature[idx].missed++;
		}
	}
}

bool ICACHE_FLASH_ATTR getUnmappedTemperature(int i, struct Temperature **t) {
	if (i >= MAX_TEMPERATURE_SENSOR)
		return false;
	if (!temperature[i].set)
		return false;
	*t = &temperature[i];
	return true;
}

uint8 ICACHE_FLASH_ATTR sensorIdx(char *sensorID) {
	int i;
	for (i = 0; i < MAX_TEMPERATURE_SENSOR; i++) {
		if (strcmp(sensorID, temperature[i].address) == 0)
			return i;
	}
	return 0xff;
}

int ICACHE_FLASH_ATTR checkAddNewTemperature(int idx, char* sensorID, int start) {
	if (idx == 0xff) { // Not Found
		int i;
		for (i = start; i < MAX_TEMPERATURE_SENSOR; i++) {
			if (!temperature[i].set) {
				idx = i;
				strcpy(temperature[idx].address, sensorID);
				TESTP("New ");
				break;
			}
		}
	}
	return idx;
}

void ICACHE_FLASH_ATTR checkSetTemperature(int idx, int val, int fract, char* sensorID) {
	if (idx < MAX_TEMPERATURE_SENSOR) {
		TESTP("Sensor[%d] %s = %d\n", idx, sensorID, val);
		temperature[idx].set = true;
		if (!temperature[idx].override) {
			if (val < 0) {
				temperature[idx].sign = '-';
				temperature[idx].val = -val;
			} else {
				temperature[idx].sign = '+';
				temperature[idx].val = val;
			}
			temperature[idx].fract = fract;
			temperature[idx].missed = 0;
		}
	}
}

uint8 ICACHE_FLASH_ATTR setUnmappedSensorTemperature(char *sensorID, int val, int fract) {
	int idx = sensorIdx(sensorID);
	idx = checkAddNewTemperature(idx, sensorID, 0);
	checkSetTemperature(idx, val, fract, sensorID);
	return idx;
}

uint8 ICACHE_FLASH_ATTR setUnmappedTemperature(char *sensorID, int val, int fract) {
	int idx = sensorIdx(sensorID);
	idx = checkAddNewTemperature(idx, sensorID, MAX_DS18B20_SENSOR);
	checkSetTemperature(idx, val, fract, sensorID);
	return idx;
}

bool ICACHE_FLASH_ATTR printTemperature(int idx) {
	struct Temperature *t;
	if (getUnmappedTemperature(idx, &t)) {
		os_printf((const char*) "[%d]: %s  %c%d.%02d%s {%d}. ",
			idx, t->address, t->sign, t->val, t->fract, t->override ? " (OR)" : "", t->missed);
	} else {
		os_printf("[%d]. ", idx);
		return false;
	}
	return true;
}

bool ICACHE_FLASH_ATTR printMappedTemperature(int idx) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[idx], &t)) {
		os_printf((const char*) "[%d]->%d {%s}: %s  %c%d.%02d%s {%d}. ", idx, sysCfg.mapping[idx],
				sysCfg.mappingName[idx], t->address, t->sign, t->val, t->fract,
				t->override ? " (OR)" : "", t->missed);
	} else {
		os_printf("[%d]->%d. ", idx, sysCfg.mapping[idx]);
		return false;
	}
	return true;
}

static void ICACHE_FLASH_ATTR ds18b20_cb() { // after  750mS
	int i;
	uint8 addr[8], data[12];
	char bfr[10];
	int idx = 0;

	reset_search();
	incMissed();
	do {
		if (ds_search(addr)) {
			if (crc8(addr, 7) != addr[7]) {
				TESTP("CRC mismatch, crc=%xd, addr[7]=%xd\n", crc8(addr, 7), addr[7]);
			}
			switch (addr[0]) {
			case DS18B20:
				TESTP( "Device is DS18B20 family.\n" );
				break;
			default:
				TESTP("Device is unknown family.\n");
				return;
			}
		} else {
			break;
		}
		reset();
		select(addr);
		write(DS1820_READ_SCRATCHPAD, 0);
		for (i = 0; i < 9; i++) {
			data[i] = read();
		}

		uint16 tReading, tVal, tFract;
		char tSign;

		tReading = (data[1] << 8) | data[0];
		if (tReading & 0x8000) {
			tReading = (tReading ^ 0xffff) + 1;				// 2's complement
			tSign = '-';
		} else {
			tSign = '+';
		}
		tVal = tReading >> 4;  // separate off the whole and fractional portions
		tFract = (tReading & 0xf) * 100 / 16;
		os_sprintf(bfr, "%02x%02x%02x%02x%02x%02x%02x%02x", addr[0], addr[1],
				addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
		idx = setUnmappedSensorTemperature(bfr, (tSign=='+') ? tVal : -tVal, tFract);
		INFO(if (idx != 0xff) {printTemperature(idx); os_printf("\n");});
	} while (true);

#if SLEEP_MODE == 0
	extraPublishTemperatures(0xff);
#endif
	return;
}

void ICACHE_FLASH_ATTR ds18b20StartScan(void) {
	ds_init();
	reset();
	write(DS1820_SKIP_ROM, 1);
	write(DS1820_CONVERT_T, 1);
	os_timer_disarm(&ds18b20_timer);
	os_timer_setfn(&ds18b20_timer, (os_timer_func_t *) ds18b20_cb, (void *) 0);
	os_timer_arm(&ds18b20_timer, 750, false);
}

int ICACHE_FLASH_ATTR mappedTemperature(uint8 name) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t))
		return t->val;
	//publishError(1, name);
	return -99;
}

uint8 ICACHE_FLASH_ATTR setTemperatureOverride(char *sensorID, char *value) {
	int idx = sensorIdx(sensorID);
	int val;
	if (idx <= MAX_TEMPERATURE_SENSOR) {
		temperature[idx].set = true;
		temperature[idx].override = true;
		val = atoi(value);
		if (val < 0) {
			temperature[idx].sign = '-';
			temperature[idx].val = -val;
		} else {
			temperature[idx].sign = '+';
			temperature[idx].val = val;
		}
		while (*value && *value != '.') value++;
		if (*value) {
			value++;
			temperature[idx].fract = atoi(value);
		}
	}
	return idx;
}

uint8 ICACHE_FLASH_ATTR clearTemperatureOverride(char *sensorID) {
	int idx = sensorIdx(sensorID);
	if (idx <= MAX_TEMPERATURE_SENSOR) {
		temperature[idx].set = false;
		temperature[idx].override = false;
		ds18b20StartScan();
	}
	return idx;
}
