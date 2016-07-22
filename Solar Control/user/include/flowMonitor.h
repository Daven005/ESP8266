/*
 * flowMonitor.h
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_FLOWMONITOR_H_
#define USER_INCLUDE_FLOWMONITOR_H_

void resetFlowReadings(void);
uint16 flowPerReading(void);
uint16 flowMaxReading(void);
uint16 flowMinReading(void);
uint16 flowPerReading(void);
uint16 flowAverageReading(void);
uint16 flowCurrentReading(void);
uint16 flowInLitresPerHour(void);
void initFlowMonitor(void);
float energyReading(void);
void printFlows(void);
void calcFlows(void);

#endif /* USER_INCLUDE_FLOWMONITOR_H_ */
