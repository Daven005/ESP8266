/*
 Driver for the temperature and humidity sensor DHT11 and DHT22
 Official repository: https://github.com/CHERTS/esp8266-dht11_22

 Copyright (C) 2014 Mikhail Grigorev (CHERTS)

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ets_sys.h"
#include "osapi.h"
#include "c_types.h"
#include "user_interface.h"
#include "easygpio.h"
#include "dht22.h"
#include "debug.h"

//#define PULSE 4

static struct dht_sensor_data reading[2] = { { .success = 0, .count = 0 }, { .success = 0, .count =
		0 } };

#ifdef PULSE
static void ICACHE_FLASH_ATTR pulse(void) {
	easygpio_outputSet(PULSE, 1);
	os_delay_us(3);
	easygpio_outputSet(PULSE, 0);
	os_delay_us(1);
}

static void ICACHE_FLASH_ATTR pulses(int8 count) {
	while (count-- > 0) {
		easygpio_outputSet(PULSE, 1);
		os_delay_us(1);
		easygpio_outputSet(PULSE, 0);
		os_delay_us(1);
	}
}
#else
#define pulse()
#define pulses(x)
#endif

static void ICACHE_FLASH_ATTR printRaw(struct dht_sensor_data *reading, uint8 *data) {
	uint8 byteCount;
	for (byteCount=0; byteCount < 5; byteCount++) {
		os_printf("%02x ", data[byteCount]);
	}
	os_printf(" (pin: %d id: %d)\n", reading->pin, reading->id);
}

static inline float ICACHE_FLASH_ATTR scaleTemperature(struct dht_sensor_data *reading, uint8 *data) {
	if (reading->sensorType == DHT11) {
		return (float) data[2];
	} else {
		int32 temperature = (((int32) data[2] & 0x7f) << 8) + data[3];
		if (data[2] & 0x80)
			temperature = -temperature;
		return ((float) temperature) / 10.0;
	}
}

static inline float ICACHE_FLASH_ATTR scaleHumidity(struct dht_sensor_data *reading, uint8 *data) {
	if (reading->sensorType == DHT11) {
		return data[0];
	} else {
		int32 humidity = (((int32) data[0]) << 8) + data[1];
		return ((float) humidity) / 10.0;
	}
}

struct dht_sensor_data *ICACHE_FLASH_ATTR dhtRead(int id) {
	return &reading[id - 1];
}

static int32 ICACHE_FLASH_ATTR waitWhile(uint8 pin, bool state, uint32 min, uint32 max) {
	uint32 t = system_get_time();
	int32 tDiff;

	tDiff = system_get_time() - t;
	while (tDiff <= (max + 2) && easygpio_inputGet(pin) == state) {
		tDiff = system_get_time() - t;
	}
	if (tDiff < 0)
		return 0;
	if (min <= tDiff && tDiff < max && easygpio_inputGet(pin) != state)
		return tDiff; // In Range
	return -tDiff;
}

static void ICACHE_FLASH_ATTR bitTimingError(char *msg, uint32 tDiff, uint8 bit, uint8 byteCount, struct dht_sensor_data *reading) {
	TESTP("%s timing error %d bit %d/%d (pin: %d id: %d)\n", msg, -tDiff, bit, byteCount, reading->pin, reading->id);
}

static void ICACHE_FLASH_ATTR timingError(char *msg, uint32 tDiff, struct dht_sensor_data *reading) {
	TESTP("%s in %duS (pin: %d id: %d)\n", msg, -tDiff, reading->pin, reading->id);
}

static enum errorCode ICACHE_FLASH_ATTR readByte(struct dht_sensor_data *reading, uint8 *byte, uint8 byteCount) {
	uint8 bitCount;
	int32 t;

	// Assumes already Low
	for (bitCount = 0; bitCount < 8; bitCount++) {
		if ((t = waitWhile(reading->pin, 0, 35, 70)) <= 0) { // ~50uS
			bitTimingError("Bit High", t, bitCount, byteCount, reading);
			return E_NODATA_1;
		}
		if ((t = waitWhile(reading->pin, 1, 15, 80)) <= 0) { // ~27uS (0) or ~70uS (1)
			bitTimingError("Bit Low", t, bitCount, byteCount, reading);
			return E_NODATA_0;
		}
		*byte <<= 1;
		if (t > 50)
			*byte |= 1;
	}
	return E_NONE;
}

/* DHT22/11 Waveforms
 *     Section
 *    |     a     |    b     |     c       |    d     |   e    |    f   |
 * __	          ___40uS...              ...80uS....          ..27uS..
 *   \___20mS____/          \....80uS..../           \..50uS../        \..50uS . .etc
 *                          |----> Sensor
 *                                                             ....70uS....
 *                                                   \..50uS../            \..50uS . . etc
 */

static bool ICACHE_FLASH_ATTR dhtInput(struct dht_sensor_data *reading) {
	uint8 data[10] = { 0, 0, 0, 0, 0 };
	uint32 t = system_get_time();
	int32 tDiff;
	uint8 byteCount, checkSum;

	//   Section   b
	easygpio_outputSet(reading->pin, 1);
	easygpio_outputDisable(reading->pin);
	if ((tDiff = waitWhile(reading->pin, 1, 1, 60)) < 0) { //    Section b
		timingError("No response in", -tDiff, reading);
		reading->error = E_NO_START;
		return reading->success = false;
	}
	if ((tDiff = waitWhile(reading->pin, 0, 40, 90)) < 0) { //    Section c
		timingError("No pulse Hi", -tDiff, reading);
		reading->error = E_NOACK_1;
		return reading->success = false;
	}
	if ((tDiff = waitWhile(reading->pin, 1, 70, 90)) < 0) { //    Section d
		timingError("No pulse Lo", -tDiff, reading);
		reading->error = E_NOACK_0;
		return reading->success = false;
	}
	// read data
	for (byteCount = 0; byteCount < 5; byteCount++) {
		if ((reading->error = readByte(reading, &data[byteCount], byteCount)) != E_NONE) {
			break;
		}
	}
	if (reading->error != E_NONE) {
		return reading->success = false;
	}
	checkSum = data[0] + data[1] + data[2] + data[3];
	if (data[4] != checkSum) {
		ERRORP("Checksum incorrect. Expected %02x.\n", checkSum);
		TEST(printRaw(reading, data));
		reading->error = E_CRC;
		return reading->success = false;
	}
	// checksum is valid
	reading->temperature = scaleTemperature(reading, data);
	reading->humidity = scaleHumidity(reading, data);
	if (reading->count == 0) {
		reading->avgTemperature = reading->temperature;
		reading->avgHumidity = reading->humidity;
		reading->count = 1;
	} else {
		reading->avgTemperature = (reading->avgTemperature * 4 + reading->temperature) / 5;
		reading->avgHumidity = (reading->avgHumidity * 4 + reading->humidity) / 5;
	}
	if (reading->printRaw) {
		char bfr1[20], bfr2[20];
		TEST(printRaw(reading, data));
		dtoStr(reading->temperature, 6, 1, bfr1);
		dtoStr(reading->humidity, 6, 1, bfr2);
		TESTP("DHT%d%d[%d] Temperature: %sC, Humidity: %s%%\n",
				reading->sensorType, reading->sensorType, reading->id, bfr1, bfr2);
	}
	return reading->success = true;
}

static void dhtCb(struct dht_sensor_data *reading) {
	static uint32 lastGoodReadingTime;
	if (dhtInput(reading)) {
		lastGoodReadingTime = system_get_time();
		reading->valid = true;
	} else {
		if (system_get_time() > (lastGoodReadingTime + 60 * 1000 *1000)) { // 60S
			reading->valid = false;
		}
	}
}

static void ICACHE_FLASH_ATTR dhtStartCb(struct dht_sensor_data *reading) {
	easygpio_outputEnable(reading->pin, 0); // Set as output and Hold low for 18ms
	os_timer_disarm(&reading->wakeTimer);
	os_timer_setfn(&reading->wakeTimer, dhtCb, reading);
	os_timer_arm(&reading->wakeTimer, 18, false);
}

static void ICACHE_FLASH_ATTR _dhtInit(struct dht_sensor_data *reading, int id,
		enum DHTType dht_type, uint8 pin, uint32_t poll_time) {
	reading->sensorType = dht_type;
#ifdef PULSE
	easygpio_pinMode(PULSE, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	easygpio_outputEnable(PULSE, 0);
#endif
	easygpio_pinMode((reading->pin = pin), EASYGPIO_PULLUP, EASYGPIO_OUTPUT);
	easygpio_outputDisable(pin);
	reading->id = id;
	reading->error = E_NONE;
	reading->success = false;
	os_timer_disarm(&reading->timer);
	os_timer_setfn(&reading->timer, dhtStartCb, reading);
	os_timer_arm(&reading->timer, poll_time, true);
}

void ICACHE_FLASH_ATTR dhtInit(int id, enum DHTType dht_type, uint8 pin, uint32_t poll_time) {
	_dhtInit(&reading[id - 1], id, dht_type, pin, poll_time);
}

