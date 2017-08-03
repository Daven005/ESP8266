#ifndef __windMONITOR_H__
#define __windMONITOR_H__

void overrideClearWind(void);
void overrideSetWind(int value);
void resetWindReadings(void);
int windPerReading(void);
uint16 windMaxReading(void);
uint16 windMinReading(void);
uint16 windNowReading(void);
uint16 windAverageReading(void);
uint16 windCurrentReading(void);
uint16 windAboveCutIn (void);
int windTimesReading(void);
float energyReading(void);
void calcWind(void);
void printWind(void);
void initWindMonitor(void);
uint16 secondsNotWindy(void);
void startCheckIsWindy(void);

#endif
