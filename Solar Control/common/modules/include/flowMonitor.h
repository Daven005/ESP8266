#ifndef __FLOWMONITOR_H__
#define __FLOWMONITOR_H__

void overrideClearFlow(void);
void overrideSetFlow(int value);
void resetFlowReadings(void);
int flowPerReading(void);
uint16 flowMaxReading(void);
uint16 flowMinReading(void);
uint32 flowInLitresPerHour(void);
uint16 flowAverageReading(void);
uint16 flowCurrentReading(void);
int flowTimesReading(void);
float energyReading(void);
void calcFlows(void);
void printFlows(void);
void initFlowMonitor(void);
uint16 secondsNotFlowing(void);
void startCheckIsFlowing(void);
void isrFlowMonitor(uint32 gpio_status);

#endif
