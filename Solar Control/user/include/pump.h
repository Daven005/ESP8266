/*
 * pump.h
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_PUMP_H_
#define USER_INCLUDE_PUMP_H_

void setCloud(int idx, int c);
void setSun(int idx, int az, int alt);
bool sunnyEnough(void);
void startPumpOverride(void);
void processPump(void);
void initPump(void);
void turnOffOverride(void);

#endif /* USER_INCLUDE_PUMP_H_ */
