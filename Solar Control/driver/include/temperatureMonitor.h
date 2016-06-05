/*
 * temperatureMonitor.h
 *
 *  Created on: 5 Dec 2015
 *      Author: User
 */

#ifndef DRIVER_INCLUDE_TEMPERATUREMONITOR_H_
#define DRIVER_INCLUDE_TEMPERATUREMONITOR_H_
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include "ds18b20.h"
#define MAX_DS18B20_SENSOR 3
#define MAX_TEMPERATURE_SENSOR 5

struct Temperature {
	bool set;
	bool override;
	char sign;
	uint16 val;
	uint16 fract;
	uint8 missed;
	char address[20];
};

bool getUnmappedTemperature(int i, struct Temperature **t);
void ds18b20StartScan(void);
uint8 temperatureSensorCount(void);
uint8 sensorIdx(char* sensorID);
void setTemperature(char *sensorID, int value);
bool printTemperature(int);
bool printMappedTemperature(int);
uint8 setUnmappedTemperature(char *sensorID, int val, int fract);

// Override used for testing
uint8 setTemperatureOverride(char *sensorID, char *value);
uint8 clearTemperatureOverride(char *sensorID);

int mappedTemperature(uint8 name);
uint16_t averagePT100(void);
void saveLowReading(void);
void saveHighReading(void);
void readPT100(struct Temperature *temp);
void startReadTemperatures(void);
void saveTSbottom(char *t);

#endif /* DRIVER_INCLUDE_TEMPERATUREMONITOR_H_ */
