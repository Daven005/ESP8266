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
static uint16_t averagePT100Reading;
static struct Temperature temperature[MAX_TEMPERATURE_SENSOR];
static int sensors = 0;

int temperatureSensorCount() {
	return sensors;
}

uint16_t ICACHE_FLASH_ATTR averagePT100(void) {
	uint32 sTime = system_get_rtc_time();
	SELECT_PTC;
	uint16_t rx = system_adc_read();
	uint32 timeTaken = system_get_rtc_time() - sTime;
	if (50 < rx && rx < 1000)
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
		if (d >= 0 && d <= 9) {
			if (point_seen)
				fact /= 10.0f;
			rez = rez * 10.0f + (float) d;
		};
	};
	return rez * fact;
}

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
	TESTP("rx=%d, m=%d, c=%d, temp=%d (%d)\n", rx, (int)(m*100), (int)(c*100), (int)t, mappedTemperature(2));
	temp->set = true;
	strcpy(temp->address, "0");
	setTemp(t, temp);
	if (sensors == 0) sensors = 1;
}

void ICACHE_FLASH_ATTR saveTSbottom(char *t) {
	float temp = atof(t);
	temperature[1].set = true;
	strcpy(temperature[1].address, "1");
	setTemp(temp, &temperature[1]);
	if (sensors < 2) sensors = 2;
	TESTP("TSb: %c%d.%d (%s)\n", temperature[1].sign, temperature[1].val, temperature[1].fract, t);
}

static void ICACHE_FLASH_ATTR ds18b20TimerCb(void) {
	uint8_t addr[8], data[12];
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
		os_sprintf(temperature[idx].address, "%02x%02x%02x%02x%02x%02x%02x%02x", addr[0], addr[1],
				addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);
		//os_printf("%s %c%d.%02d\n", temperature[idx].address, tSign, tVal, tFract);

		temperature[idx].set = true;
		temperature[idx].sign = tSign;
		temperature[idx].val = tVal;
		temperature[idx].fract = tFract;
		idx++;
	} while (true);
	sensors = idx;
	return;
}

static void ICACHE_FLASH_ATTR ds18b20Start() {
	ds_init();
	reset();
	write(DS1820_SKIP_ROM, 1);
	write(DS1820_CONVERT_T, 1);

	//750ms 1x, 375ms 0.5x, 188ms 0.25x, 94ms 0.12x
	os_timer_disarm(&ds18b20_timer);
	os_timer_setfn(&ds18b20_timer, (os_timer_func_t *) ds18b20TimerCb, NULL);
	os_timer_arm(&ds18b20_timer, 750, false); // 750mS
}

void ICACHE_FLASH_ATTR startReadTemperatures(void) {
	ds18b20Start();
	readPT100(&temperature[0]);
}

void ICACHE_FLASH_ATTR initTemperatureMonitor(void) {
	SELECT_PTC;
	averagePT100Reading = system_adc_read();
}
