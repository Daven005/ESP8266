/*
 * pump.h
 *
 *  Created on: 7 Oct 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_PUMP_H_
#define USER_INCLUDE_PUMP_H_

typedef enum { AUTO_OFF, AUTO_ON, MANUAL_OFF, MANUAL_ON } pumpState_t;
void initPump(void);
void overrideSetPressure(uint16 p);
void overrideClearPressure(void);
void setPump_Manual(void); // Pump On/OFF
void setPump_Auto(void);
void processPump(void);
pumpState_t pumpState(void);
uint16 getCurrentPressure(void);
uint16 getPumpOnCount(void);

#endif /* USER_INCLUDE_PUMP_H_ */
