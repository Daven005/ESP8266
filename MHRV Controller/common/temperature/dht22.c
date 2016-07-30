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
#include "gpio.h"
#include "dht22.h"
#include "debug.h"

static struct dht_sensor_data reading[2] = { { .success = 0, .count = 0 }, { .success = 0, .count =
		0 } };

static void ICACHE_FLASH_ATTR dht_Cb(struct dht_sensor_data *reading);

static inline float scale_humidity(struct dht_sensor_data *reading, int *data) {
	if (reading->sensorType == DHT11) {
		return data[0];
	} else {
		float humidity = data[0] * 256 + data[1];
		return humidity /= 10;
	}
}

static inline float scale_temperature(struct dht_sensor_data *reading, int *data) {
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

struct dht_sensor_data *ICACHE_FLASH_ATTR dhtRead(int id) {
	return &reading[id - 1];
}

static void ICACHE_FLASH_ATTR dht_Start_Cb(struct dht_sensor_data *reading) {
	easygpio_outputEnable(reading->pin, 0); // Set as output and Hold low for 20ms
	reading->error = E_NONE;
	os_timer_disarm(&reading->wakeTimer);
	os_timer_setfn(&reading->wakeTimer, dht_Cb, reading);
	os_timer_arm(&reading->wakeTimer, 18, false);
}

static void ICACHE_FLASH_ATTR dht_Cb(struct dht_sensor_data *reading) {
	int counter = 0;
	int laststate = 1;
	int i = 0;
	int j = 0;
	int checksum = 0;
	int data[10] = { 0, 0, 0, 0, 0};

	easygpio_outputSet(reading->pin, 1);
	os_delay_us(40);
	easygpio_outputDisable(reading->pin); // Set DHT_PIN pin as an input

	// wait for pin to drop?
	while (easygpio_inputGet(reading->pin) == 1 && i < DHT_MAXCOUNT) {
		os_delay_us(1);
		i++;
	}

	if (i == DHT_MAXCOUNT) {
		// TESTP("Failed to get reading, dying\n");
		easygpio_outputSet(reading->pin, 1);
		reading->error = E_MAXCOUNT;
		reading->success = 0;
		return;
	}

	// read data
	for (i = 0; i < DHT_MAXTIMINGS; i++) {
		// Count high time (in approx us)
		counter = 0;
		while (GPIO_INPUT_GET(reading->pin) == laststate && ++counter <= 1000) {
			os_delay_us(1);
		}
		laststate = GPIO_INPUT_GET(reading->pin);
		if (counter >= 1000)
			break;
		// store data after 3 reads
		if ((i > 3) && (i % 2 == 0)) {
			// shove each bit into the storage bytes
			data[j / 8] <<= 1;
			if (counter > DHT_BREAKTIME)
				data[j / 8] |= 1;
			j++;
		}
	}

	if (j >= 39) {
		checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
		//os_printf("DHT: %02x %02x %02x %02x [%02x] CS: %02x", data[0], data[1],data[2],data[3],data[4],checksum);
		if (data[4] == checksum) {
			// checksum is valid
			INFO(for (i=0; i<=5; i++) INFOP("%02x ", data[i]));
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
			INFOP("DHT %d (%d-%d) Average Temperature: %dC, Humidity: %d%%\n", reading->id, reading->sensorType, reading->pin,
					(int) (reading->avgTemperature), (int) (reading->avgHumidity));
			reading->success = true;
		} else {
			// TESTP("Checksum was incorrect after %d bits. Expected %d but got %d\n", j, data[4], checksum);
			reading->success = false;
			reading->error = E_CRC;
		}
	} else {
		// TESTP("Got too few bits: %d should be at least 40\n", j);
		reading->success = false;
		reading->error = E_BITCOUNT;
	}
	easygpio_outputSet(reading->pin, 1);
	return;

}

static void ICACHE_FLASH_ATTR _dhtInit(struct dht_sensor_data *reading, int id, enum DHTType dht_type, uint8 pin, uint32_t poll_time) {
	reading->sensorType = dht_type;
	easygpio_outputEnable((reading->pin = pin), 1);
	reading->id = id;
	reading->error = E_NONE;
	os_timer_disarm(&reading->timer);
	os_timer_setfn(&reading->timer, dht_Start_Cb, reading);
	os_timer_arm(&reading->timer, poll_time, 1);
}

void ICACHE_FLASH_ATTR dhtInit(int id, enum DHTType dht_type, uint8 pin, uint32_t poll_time) {
	_dhtInit(&reading[id-1], id, dht_type, pin, poll_time);
}

