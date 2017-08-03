/*
 * temperature.c
 *
 *  Created on: 19 Dec 2015
 *      Author: User
 */

//#define DEBUG_OVERRIDE
#include "debug.h"

#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <ds18b20.h>
#include "sysCfg.h"
#include "check.h"
#include "user_conf.h"
#include "flash.h"

#ifdef READ_TEMPERATURES
#include "temperature.h"
#ifdef USE_FLOAT
#include "dtoa.h"
#include "assert.h"
#endif
static struct Temperature temperature[MAX_TEMPERATURE_SENSOR];
static os_timer_t ds18b20_start_timer;
static os_timer_t ds18b20_next_timer;
static TemperatureCallback temperatureCb;

static void ICACHE_FLASH_ATTR incMissed(void) {
	uint8 idx;
	for (idx = 0; idx < MAX_TEMPERATURE_SENSOR; idx++) {
		if (temperature[idx].temperatureType == SENSOR) {
			if (temperature[idx].missed < 250) {
				temperature[idx].missed++;
			}
		}
	}
}

bool ICACHE_FLASH_ATTR getUnmappedTemperature(int i, struct Temperature **t) {
	if (i >= MAX_TEMPERATURE_SENSOR) {
		*t = NULL;
		return false;
	}
	*t = &temperature[i];
	if (!temperature[i].set)
		return false;
	return true;
}

int ICACHE_FLASH_ATTR sensorIdx(char *sensorID) {
	int i;
	for (i = 0; i < MAX_TEMPERATURE_SENSOR; i++) {
		if (os_strcmp(sensorID, temperature[i].address) == 0
				&& temperature[i].temperatureType != NOT_SET)
			return i;
	}
	return -1;
}

int ICACHE_FLASH_ATTR checkAddNewTemperature(char* sensorID, uint8 *sensorAddress, enum temperatureType_t temperatureType) {
	int i;
	for (i = 0; i < MAX_TEMPERATURE_SENSOR; i++) {
		if (temperature[i].temperatureType == NOT_SET) {
			temperature[i].temperatureType = temperatureType;
			if (sensorID != NULL)
				os_strcpy(temperature[i].address, sensorID);
			if (sensorAddress != NULL) {
				os_memcpy(temperature[i].binAddress, sensorAddress, sizeof(temperature[i].binAddress));
			}
			TESTP("New (%d - %s)", i, sensorID);
			return i;
		}
	}
	ERRORP("No space for temperature %s\n", sensorID);
	return -1;
}

void ICACHE_FLASH_ATTR checkSetTemperature(int idx, int val, int fract) {
	if (0 <= idx && idx < MAX_TEMPERATURE_SENSOR) {
		if (-20 <= val && val <= 128) {
			INFOP("Sensor[%d] %s = %d.%02d\n", idx, temperature[idx].address, val, fract);
		} else {
			ERRORP("Sensor[%d] %s ERROR = %d.%02d\n", idx, temperature[idx].address, val, fract);
			publishError(55, val); // Temperature out of range
			return;
		}
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
	} else {
		ERRORP("Invalid Sensor[%d]\n", idx);
	}
}

int ICACHE_FLASH_ATTR setUnmappedSensorTemperature(char *sensorID,
		enum temperatureType_t temperatureType, int val, int fract) {
	int idx = sensorIdx(sensorID);
	if (idx == -1) {
		idx = checkAddNewTemperature(sensorID, NULL, temperatureType);
	}
	checkSetTemperature(idx, val, fract);
	return idx;
}

void ICACHE_FLASH_ATTR setMappedSensorTemperature(uint8 mapId, char *sensorID,
		enum temperatureType_t temperatureType, int val, int fract) {
	sysCfg.mapping[mapId] = setUnmappedSensorTemperature(sensorID, temperatureType, val, fract);
}

bool ICACHE_FLASH_ATTR printTemperature(int idx) {
	struct Temperature *t;
	if (getUnmappedTemperature(idx, &t)) {
		os_printf((const char*) "[%d]: %s  %c%d.%02d%s {%d}. [%d %d]\n", idx, t->address, t->sign,
				t->val, t->fract, t->override ? " (OR)" : "", t->missed, t->val, t->fract);
	} else {
		os_printf("[%d] %s. ", idx, t->address);
		return false;
	}
	return true;
}

bool ICACHE_FLASH_ATTR printMappedTemperature(int idx) {
	struct Temperature *t;

	if (getUnmappedTemperature(sysCfg.mapping[idx], &t)) {
		os_printf((const char*) "[%d]->%d {%s}: %s  %c%d.%02d%s {%d}.\n", idx, sysCfg.mapping[idx],
				sysCfg.mappingName[idx], t->address, t->sign, t->val, t->fract,
				t->override ? " (OR)" : "", t->missed);
	} else {
		os_printf("[%d]->%d. ", idx, sysCfg.mapping[idx]);
		return false;
	}
	return true;
}

void ICACHE_FLASH_ATTR ds18b20SearchDevices(void) {
	int i;
	uint8 addr[8], data[12];
	char bfr[20];
	int idx = 0;
	int sensorCount = 0;
	uint32 t = system_get_time();

	reset_search();
	incMissed();
	do {
		if (ds_search(addr)) {
			if (crc8(addr, 7) != addr[7]) {
				ERRORP("Address CRC error, crc=%02x, addr[7]=%02x\n", crc8(addr, 7), addr[7]);
			}
			switch (addr[0]) {
			case DS18B20:
				INFOP( "Device is DS18B20 family.\n");
				os_sprintf(bfr, "%02x%02x%02x%02x%02x%02x%02x%02x", addr[0], addr[1], addr[2], addr[3],
						addr[4], addr[5], addr[6], addr[7]);
				idx = checkAddNewTemperature(bfr, addr, SENSOR);
				TEST(if (idx != 0xff) {printTemperature(idx); os_printf("\n");});
				sensorCount++;
				break;
			default:
				ERRORP("Device is unknown family.\n");
				return;
			}
		} else {
			break; // while(true)
		}
	} while (true);
	if (sensorCount == 0) {
		ERRORP("No temperature sensors!\n");
	}
	checkTime("ds18b20SearchDevices", t);
}

static void ICACHE_FLASH_ATTR ds18b20_next(int idx) {
	uint8 addr[8], data[12];
	int i;
	uint32 t = system_get_time();

	while (idx < MAX_TEMPERATURE_SENSOR && temperature[idx].temperatureType != SENSOR) {
		idx++;
	}
	if (idx >= MAX_TEMPERATURE_SENSOR) {
		if (temperatureCb) {
			temperatureCb();
		}
	} else {
		reset();
		ETS_GPIO_INTR_DISABLE();
		select(temperature[idx].binAddress);
		write(DS1820_READ_SCRATCHPAD, 0);
		for (i = 0; i < 9; i++) {
			data[i] = read();
		}
		ETS_GPIO_INTR_ENABLE();
		if (crc8(data, 8) == data[8]) {
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
			checkSetTemperature(idx, tVal, tFract);
			INFO(if (idx != 0xff) {printTemperature(idx); os_printf("\n");});
		} else {
			ERRORP("Data CRC error, crc=%02x, data[8]=%02x\n", crc8(data, 8), data[8]);
			for (i = 0; i <= 8; i++) {
				TESTP("%02x ", data[i]);
			}
			TESTP("\n");
			publishError(57, idx);  // Data CRC error
		}
		os_timer_disarm(&ds18b20_next_timer);
		os_timer_setfn(&ds18b20_next_timer, (os_timer_func_t *) ds18b20_next, (void *) (idx + 1));
		os_timer_arm(&ds18b20_next_timer, 10, false);
	}
	checkTime("ds18b20_next", t);
}

static void ICACHE_FLASH_ATTR ds18b20_cb() { // after  750mS
	incMissed();
	ds18b20_next(0);
}

void ICACHE_FLASH_ATTR ds18b20StartScan(TemperatureCallback tempCb) {
	INFOP("Start Temp\n");
	temperatureCb = tempCb;
	ds_init();
	reset();
	write(DS1820_SKIP_ROM, 1);
	write(DS1820_CONVERT_T, 1);
	os_timer_disarm(&ds18b20_start_timer);
	os_timer_setfn(&ds18b20_start_timer, (os_timer_func_t *) ds18b20_cb, (void *) 0);
	os_timer_arm(&ds18b20_start_timer, 750, false);
}

#ifdef USE_FLOAT
double ICACHE_FLASH_ATTR mappedFloatTemperature(uint8 name) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t)) {
		double val = (double) t->val;
		double fract = (double) t->fract;
		assert_equal("t->val", t->val, (uint16) val);
		assert_equal("t->fract", t->fract, (uint16) fract);
		double temp = val + fract / 100.0;
		assert_true("mft", temp < 200);
		return temp;
	}
	return -99.0;
}

double ICACHE_FLASH_ATTR mappedFloatPtrTemperature(uint8 name, double *temp) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t)) {
		double val = (double) t->val;
		double fract = (double) t->fract;
		assert_equal("t->val", t->val, (uint16) val);
		assert_equal("t->fract", t->fract, (uint16) fract);
		*temp = val + fract / 100.0;
		assert_true("mft", *temp < 200);
		return *temp;
	}
	return -99.0;
}

char * ICACHE_FLASH_ATTR mappedStrTemperature(uint8 name, char *s) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t)) {
		double val = (double) t->val;
		double fract = (double) t->fract;
		assert_equal("t->val", t->val, (uint16) val);
		assert_equal("t->fract", t->fract, (uint16) fract);
		return dtoStr(val + fract / 100.0, 7, 2, s);
	}
	return dtoStr(-99, 7, 2, s);
}

void ICACHE_FLASH_ATTR checkMappedFloat(uint8 name) {
	double test1, test2;
	test1 = mappedFloatPtrTemperature(name, &test2);
	assert_true("checkMappedFloat", test1 == test2);
}

double ICACHE_FLASH_ATTR getUnmappedFloatTemperature(uint8 name) {
	struct Temperature *t;
	if (getUnmappedTemperature(name, &t)) {
		return ((double) t->val) + (((double) t->fract) / 100.0);
	}
	return -99.0;
}
#endif

int ICACHE_FLASH_ATTR mappedTemperature(uint8 name) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t)) {
		if (t->fract > 50)
			return t->val + 1; // Round
		return t->val;
	}
	return -99;
}

char * ICACHE_FLASH_ATTR unmappedSensorID(uint8 id) {
	struct Temperature *t;
	getUnmappedTemperature(id, &t);
	if (t != NULL) {
		return t->address;
	}
	return "";
}

bool ICACHE_FLASH_ATTR mappedTemperatureIsSet(uint8 name) {
	return temperature[sysCfg.mapping[name]].set;
}

int ICACHE_FLASH_ATTR setTemperatureOverride(char *sensorID, char *value) {
	int idx = sensorIdx(sensorID);
	int val;
	if (0 <= idx && idx < MAX_TEMPERATURE_SENSOR) {
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
		while (*value && *value != '.')
			value++;
		if (*value) {
			value++;
			temperature[idx].fract = atoi(value);
		}
	}
	return idx;
}

int ICACHE_FLASH_ATTR clearTemperatureOverride(char *sensorID) {
	int idx = sensorIdx(sensorID);
	if (0 <= idx && idx <= MAX_TEMPERATURE_SENSOR) {
		temperature[idx].set = false;
		temperature[idx].override = false;
	}
	return idx;
}

bool ICACHE_FLASH_ATTR allTemperaturesSet(void) {
	int idx;
	for (idx=0; idx <MAX_TEMPERATURE_SENSOR; idx++) {
		if (!temperature[idx].set) return false;
	}
	return true;
}

void ICACHE_FLASH_ATTR initTemperature(void) {
	os_memset(temperature, 0, sizeof(temperature));
}

#endif
