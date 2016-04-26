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

int temperatureSensorCount();
bool getUnmappedTemperature(int i, struct Temperature **t);
int mappedTemperature(uint8 name);
uint16_t averagePT100(void);
void saveLowReading(void);
void saveHighReading(void);
void readPT100(struct Temperature *temp);
void startReadTemperatures(void);

#endif /* DRIVER_INCLUDE_TEMPERATUREMONITOR_H_ */
