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
#include "easygpio.h"

enum DHTType{
	DHT11,
	DHT22
};

struct dht_sensor_data {
	float temperature;
	float humidity;
	BOOL success;
	enum DHTType sensorType;
	uint8 id;
	uint8 pin;
	ETSTimer timer;
	ETSTimer wakeTimer;
};

#define DHT_MAXTIMINGS	10000
#define DHT_BREAKTIME	20
#define DHT_MAXCOUNT	32000

void dht1Init(enum DHTType dht_type, uint8 pin, uint32_t poll_time);
struct dht_sensor_data *dht1Read(void);
void dht2Init(enum DHTType dht_type, uint8 pin, uint32_t poll_time);
struct dht_sensor_data *dht2Read(void);

#endif
