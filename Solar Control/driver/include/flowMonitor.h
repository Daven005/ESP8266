#ifndef __FLOWMONITOR_H__
#define __FLOWMONITOR_H__

void resetFlowReadings(void);
uint16 flowPerReading(void);
uint16 flowMaxReading(void);
uint16 flowMinReading(void);
uint16 flowPerReading(void);
uint16 flowAverageReading(void);
uint16 flowCurrentReading(void);
uint16 flowInLitresPerHour(void);
void initFlowMonitor(void);

#endif
