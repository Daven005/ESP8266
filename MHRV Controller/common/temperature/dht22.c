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

static inline float ICACHE_FLASH_ATTR scale_temperature(struct dht_sensor_data *reading, int8 *data) {
	if (reading->sensorType == DHT11) {
		return data[2];
	} else {
		float temperature = data[2] & 0x7f;
		temperature *= 256;
		temperature += data[3];
		temperature /= 10;
		if (data[2] & 0x80)
			temperature *= -1;
		return temperature;
	}
}

static inline float ICACHE_FLASH_ATTR scale_humidity(struct dht_sensor_data *reading, int8 *data) {
	if (reading->sensorType == DHT11) {
		return data[0];
	} else {
		float humidity = data[0] * 256 + data[1];
		return humidity /= 10;
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

static enum errorCode ICACHE_FLASH_ATTR readByte(struct dht_sensor_data *reading, uint8 *byte) {
	uint8 bitCount;
	int32 t;

	// Assumes already Lo
	for (bitCount = 0; bitCount < 8; bitCount++) {
		if (waitWhile(reading->pin, 0, 40, 60) <= 0) { // ~50uS
			pulses(3);
			TESTP("Bit -> Hi error %d @ %d", t, bitCount);
			return E_MAXCOUNT0;
		}
		if ((t = waitWhile(reading->pin, 1, 18, 80)) <= 0) { // ~27uS (0) or ~70uS (1)
			pulses(2);
			TESTP("Bit -> Lo error %d @ %d", t, bitCount);
			return E_MAXCOUNT1;
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

static void ICACHE_FLASH_ATTR dhtInput(struct dht_sensor_data *reading) {
	int8 data[10] = { 0, 0, 0, 0, 0 };
	uint32 t = system_get_time();
	int32 tDiff;
	uint8 byteCounter;

	pulses(5);

	//   Section   b
	easygpio_outputSet(reading->pin, 1);
	easygpio_outputDisable(reading->pin);
	if ((tDiff = waitWhile(reading->pin, 1, 1, 50)) < 0) { //    Section b
		TESTP("No response in %duS\n", -tDiff);
		reading->error = E_NO_START;
		reading->success = false;
		return;
	}pulse();
	if ((tDiff = waitWhile(reading->pin, 0, 40, 90)) < 0) { //    Section c
		TESTP("No pulse Hi in %duS\n", -tDiff);
		reading->error = E_MAXCOUNT0;
		reading->success = false;
		return;
	}pulse();
	if ((tDiff = waitWhile(reading->pin, 1, 70, 90)) < 0) { //    Section d
		TESTP("No pulse Lo in %duS\n", -tDiff);
		reading->error = E_MAXCOUNT1;
		reading->success = false;
		return;
	}
	// read data
	for (byteCounter = 0; byteCounter < 5; byteCounter++) {
		if ((reading->error = readByte(reading, &data[byteCounter])) != E_NONE) {
			TESTP(" - %d\n", byteCounter);
			break;
		}
	}
	if (reading->error != E_NONE) {
		reading->success = false;
		return;
	}
	INFO(for (byteCounter=0; byteCounter < 5; byteCounter++) TESTP("%02x ", data[byteCounter]));
	INFOP("\n");
	if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
		INFOP("Checksum was incorrect. Expected %d\n", data[4]);
		reading->success = false;
		reading->error = E_CRC;
		return;
	}
	// checksum is valid
	reading->temperature = scale_temperature(reading, data);
	reading->humidity = scale_humidity(reading, data);
	if (reading->count == 0) {
		reading->avgTemperature = reading->temperature;
		reading->avgHumidity = reading->humidity;
		reading->count = 1;
	} else {
		reading->avgTemperature = (reading->avgTemperature * 4 + reading->temperature) / 5;
		reading->avgHumidity = (reading->avgHumidity * 4 + reading->humidity) / 5;
	}
	INFOP("DHT %d (%d-%d) Average Temperature: %dC, Humidity: %d%%\n", reading->id,
			reading->sensorType, reading->pin, (int) (reading->avgTemperature),
			(int) (reading->avgHumidity));
	reading->success = true;
	return;
}

static void dhtCb(struct dht_sensor_data *reading) {
	static uint32 lastGoodReadingTime;
	dhtInput(reading);
	if (reading->success) {
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

