/*
 * temperature.h
 *
 *  Created on: 19 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_TEMPERATURE_H_
#define INCLUDE_TEMPERATURE_H_

#define MAX_DS18B20_SENSOR 10
#define MAX_TEMPERATURE_SENSOR 15

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

#endif /* INCLUDE_TEMPERATURE_H_ */
