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

#ifndef __DHT22_H__
#define __DHT22_H__

#include "ets_sys.h"
#include "osapi.h"

enum DHTType {
	DHT11 = 1, DHT22 = 2
};

struct dht_sensor_data {
	float temperature;
	float humidity;
	float avgTemperature;
	float avgHumidity;
	int count;
	bool success;
	bool valid;
	enum DHTType sensorType;
	uint8 id;
	uint8 pin;
	ETSTimer timer;
	ETSTimer wakeTimer;
	enum errorCode {
		E_NONE, E_MAXCOUNT0, E_MAXCOUNT1, E_CRC, E_NO_START
	} error;
};

void dhtInit(int id, enum DHTType dht_type, uint8 pin, uint32_t poll_time);
struct dht_sensor_data *dhtRead(int id);

#endif
