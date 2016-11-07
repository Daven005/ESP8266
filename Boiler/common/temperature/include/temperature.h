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
	bool override;
	char sign;
	uint8 missed;
	enum temperatureType_t temperatureType;
	uint16 val;
	uint16 fract; // 2 decimal places!!
	char address[20];
	uint8 binAddress[8];
};

void ds18b20SearchDevices(void);
bool getUnmappedTemperature(int i, struct Temperature **t);
void ds18b20StartScan(TemperatureCallback tempCb);
int sensorIdx(char* sensorID);
bool printTemperature(int);
bool printMappedTemperature(int);
double mappedFloatTemperature(uint8 name);
double mappedFloatPtrTemperature(uint8 name, double *temp);
char *mappedStrTemperature(uint8 name, char *s);
int mappedTemperature(uint8 name);
char *unmappedSensorID(uint8 name);
bool mappedTemperatureIsSet(uint8 name);
int setUnmappedSensorTemperature(char *sensorID, enum temperatureType_t temperatureType, int val, int fract);
double getUnmappedFloatTemperature(uint8 name);
void checkMappedFloat(uint8 name);

// Override used for testing
int setTemperatureOverride(char *sensorID, char *value);
int clearTemperatureOverride(char *sensorID);
int checkAddNewTemperature(char* sensorID, uint8 *sensorAddress, enum temperatureType_t temperatureType);
void checkSetTemperature(int idx, int val, int fract);
void initTemperature(void);

#endif /* INCLUDE_TEMPERATURE_H_ */
