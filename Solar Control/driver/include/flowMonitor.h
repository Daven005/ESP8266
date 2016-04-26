#ifndef __FLOWMONITOR_H__
#define __FLOWMONITOR_H__

void resetFlowReadings(void);
int flowPerReading(void);
int flowMaxReading(void);
int flowTimesReading(void);
int flowAverageReading(void);
int flowCurrentReading(void);
void initFlowMonitor(void);

#endif
