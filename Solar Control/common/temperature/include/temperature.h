/*
 * temperature.h
 *
 *  Created on: 19 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_TEMPERATURE_H_
#define INCLUDE_TEMPERATURE_H_

enum temperatureType_t { NOT_SET, SENSOR, DERIVED };
typedef void (*TemperatureCallback)(void);

struct Temperature {
	bool set;
	enum temperatureType_t temperatureType;
	bool override;
	char sign;
	uint16 val;
	uint16 fract;
	uint8 missed;
	char address[20];
};

bool getUnmappedTemperature(int i, struct Temperature **t);
void ds18b20StartScan(TemperatureCallback tempCb);
int sensorIdx(char* sensorID);
bool printTemperature(int);
bool printMappedTemperature(int);
float mappedFloatTemperature(uint8 name);
int  mappedTemperature(uint8 name);
bool mappedTemperatureIsSet(uint8 name);
int setUnmappedSensorTemperature(char *sensorID, enum temperatureType_t temperatureType, int val, int fract);

// Override used for testing
int setTemperatureOverride(char *sensorID, char *value);
int clearTemperatureOverride(char *sensorID);

#endif /* INCLUDE_TEMPERATURE_H_ */
