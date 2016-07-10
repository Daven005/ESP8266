/*
 * temperatureMonitor.c
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */


#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include <user_interface.h>
#include "stdout.h"
#include "debug.h"

#include "gpio.h"
#include "easygpio.h"
#include "config.h"
#include "user_config.h"
#include "ds18b20.h"
#include "IOdefs.h"
#include "temperatureMonitor.h"

static os_timer_t ds18b20_timer;
static struct Temperature temperature[MAX_TEMPERATURE_SENSOR];
static bool publishTemperaturesSignal = false;
static uint8  cloud[3] = { 10, 10, 10 };
static int sunAzimuth, sunAltitdude;

#if USE_PT100
static uint16_t averagePT100Reading;
uint16_t ICACHE_FLASH_ATTR averagePT100(void) {
	uint32 sTime = system_get_rtc_time();
	SELECT_PTC;
	uint16_t rx = system_adc_read();
	uint32 timeTaken = system_get_rtc_time() - sTime;
	if (0 < rx && rx < 1000)
		return averagePT100Reading = (averagePT100Reading*4 + rx)/5;
	publishError(0, rx);
	return averagePT100Reading;
}

void ICACHE_FLASH_ATTR saveLowReading(void) {
	int rx = averagePT100();
	int t = mappedTemperature(1); // First digital
	if (-20 < t && t < 110) {
		sysCfg.settings[SET_T0_READING] = rx;
		sysCfg.settings[SET_T0] = t;
		TESTP("saveLowReading %d = %d\n", rx, t);
		CFG_Save();
	}
}

void ICACHE_FLASH_ATTR saveHighReading(void) {
	int rx = averagePT100();
	int t = mappedTemperature(1);
	if (-20 < t && t < 110) {
		sysCfg.settings[SET_T1_READING] = rx;
		sysCfg.settings[SET_T1] = t;
		TESTP("saveHighReading %d = %d\n", rx, t);
		CFG_Save();
	}
}
#endif

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

bool ICACHE_FLASH_ATTR getUnmappedTemperature(int i, struct Temperature **t) {
	if (i >= MAX_TEMPERATURE_SENSOR)
		return false;
	if (!temperature[i].set)
		return false;
	*t = &temperature[i];
	return true;
}

int ICACHE_FLASH_ATTR mappedTemperature(uint8 name) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[name], &t))
		return t->val;
	return -100;
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
				INFOP("New ");
				break;
			}
		}
	}
	return idx;
}

void ICACHE_FLASH_ATTR checkSetTemperature(int idx, int val, int fract, char* sensorID) {
	if (idx < MAX_TEMPERATURE_SENSOR) {
		INFOP("Sensor[%d] %s = %d (%d)\n", idx, sensorID, val, temperature[idx].override);
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

bool ICACHE_FLASH_ATTR printTemperature(int idx) {
	struct Temperature *t;
	if (getUnmappedTemperature(idx, &t)) {
		os_printf((const char*) "[%d]: %s  %c%d.%02d%s {%d}.  ",
			idx, t->address, t->sign, t->val, t->fract, t->override ? " (OR)" : "", t->missed);
	} else {
		os_printf("[%d].  ", idx);
		return false;
	}
	return true;
}

bool ICACHE_FLASH_ATTR printMappedTemperature(int idx) {
	struct Temperature *t;
	if (getUnmappedTemperature(sysCfg.mapping[idx], &t)) {
		os_printf((const char*) "[%d]->%d {%s}: %s  %c%d.%02d%s {%d}\n", idx, sysCfg.mapping[idx],
				sysCfg.mappingName[idx], t->address, t->sign, t->val, t->fract,
				t->override ? " (OR)" : "", t->missed);
	} else {
		os_printf("[%d]->%d.  ", idx, sysCfg.mapping[idx]);
		return false;
	}
	return true;
}

static ICACHE_FLASH_ATTR void setTemp(float t, struct Temperature* temp) {
	if (t >= 0) {
		temp->sign = '+';
		temp->val = (int) t;
		temp->fract = t - temp->val;
	} else {
		temp->sign = '-';
		temp->val = -(int) t;
		temp->fract = -(t - temp->val);
	}
}

static double ICACHE_FLASH_ATTR atof(char *s) {
	float rez = 0, fact = 1;
	if (*s == '-') {
		s++;
		fact = -1;
	};
	for (int point_seen = 0; *s; s++) {
		if (*s == '.') {
			point_seen = 1;
			continue;
		};
		int d = *s - '0';
		if (0 <= d && d <= 9) {
			if (point_seen)
				fact /= 10.0f;
			rez = rez * 10.0f + (float) d;
		};
	};
	return rez * fact;
}

#if USE_PT100
void ICACHE_FLASH_ATTR readPT100(struct Temperature *temp) {
	int rx = averagePT100();
	if (sysCfg.settings[SET_T1_READING] == sysCfg.settings[SET_T0_READING]) {
		publishError(1, sysCfg.settings[SET_T1_READING]);
		return;
	}
	float m = (float)(sysCfg.settings[SET_T1] - sysCfg.settings[SET_T0])
			/(float)(sysCfg.settings[SET_T1_READING] - sysCfg.settings[SET_T0_READING]);
	float c = sysCfg.settings[SET_T0] - sysCfg.settings[SET_T0_READING] * m;
	float t = m * rx + c;
	INFOP("rx=%d, m=%d, c=%d, temp=%d (%d)\n",
			rx, (int)(m*100), (int)(c*100), (int)t, mappedTemperature(MAP_TEMP_SUPPLY));
	temp->set = true;
	strcpy(temp->address, "0");
	if (!temp->override) setTemp(t, temp);
}
#endif

void ICACHE_FLASH_ATTR saveTSbottom(char *t) {
	float temp = atof(t);
	temperature[1].set = true;
	strcpy(temperature[1].address, "1");
	setTemp(temp, &temperature[1]);
	TESTP("TSb: %c%d.%d (%s)\n", temperature[1].sign, temperature[1].val, temperature[1].fract, t);
}

static void ICACHE_FLASH_ATTR ds18b20TimerCb(void) {
	uint8_t addr[8], data[12];
	char bfr[10];
	int r, i;
	int idx = 2; // Allow for PTC sensor and /App/.../TS Bottom

	INFOP("ds18b20TimerCb\n");
	for (i = idx; i < MAX_TEMPERATURE_SENSOR; i++) {
		temperature[i].set = false;
	}
	reset_search();
	do {
		r = ds_search(addr);
		if (r) {
			if (crc8(addr, 7) != addr[7])
				INFOP("CRC mismatch, crc=%xd, addr[7]=%xd\n", crc8(addr, 7), addr[7]);

			switch (addr[0]) {
			case DS18B20:
				INFOP( "Device is DS18B20 family.\n" );
				break;

			default:
				INFOP("Device is unknown family.\n");
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

		uint16_t tReading, tVal, tFract;
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
	if (publishTemperaturesSignal) {
		globalPublishTemperatures();
		publishTemperaturesSignal = false;
	}
	return;
}

static void ICACHE_FLASH_ATTR ds18b20Start() {
	ds_init();
	reset();
	write(DS1820_SKIP_ROM, 1);
	write(DS1820_CONVERT_T, 1);
	os_timer_disarm(&ds18b20_timer);
	os_timer_setfn(&ds18b20_timer, (os_timer_func_t *) ds18b20TimerCb, NULL);
	os_timer_arm(&ds18b20_timer, 750, false); // 750mS
}

void ICACHE_FLASH_ATTR startReadTemperatures(void) {
	ds18b20Start();
#if USE_PT100
	readPT100(&temperature[0]);
#endif
}

void ICACHE_FLASH_ATTR initTemperatureMonitor(void) {
#if USE_PT100
	SELECT_PTC;
	averagePT100Reading = system_adc_read();
#endif
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
		publishTemperaturesSignal = true; // Now get temperatures republished once they have been read
		ds18b20Start();
	}
	return idx;
}
